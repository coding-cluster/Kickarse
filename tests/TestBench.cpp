// Kickarse — per-mode benchmarks and illustrative WAV renders (kick + bass through each mode).
#include <chrono>
#include <initializer_list>
#include <utility>

#include "TestHarness.h"
#include "TestSignals.h"

using namespace kick;
using kt::Stereo;

namespace {

constexpr double kSr  = 48000.0;
constexpr double kBpm = 126.0;

using ParamList = std::initializer_list<std::pair<ParamId, float>>;

struct Demo {
    Stereo main;                          // bass (+ pad for the spectral demos)
    Stereo kick;                          // sidechain
    std::vector<kt::Driver::Note> notes;  // one note per kick (MIDI mode)
};

// Four bars of four-on-the-floor kicks with an offbeat hat, over a sustained bass line and a
// quiet high pad (so spectral ducking has something to leave alone).
Demo makeDemo(double sr, double seconds)
{
    const size_t n = size_t(seconds * sr);
    const double beat = 60.0 / kBpm * sr;
    Demo d;
    d.main = Stereo(n);
    d.kick = Stereo(n);
    std::vector<float> kick(n, 0.f), bass(n, 0.f), pad(n, 0.f);
    int k = 0;
    for (double t = 0.0; size_t(t) < n; t += beat, ++k) {
        kt::addKick(kick, sr, size_t(t), 0.9f, true, uint32_t(k + 1));
        kt::addHat(kick, sr, size_t(t + beat / 2), 0.02f, uint32_t(k + 50));
        d.notes.push_back({size_t(t), 36, 110});
    }
    const double roots[] = {55.0, 55.0, 43.65, 49.0};
    const size_t bar = size_t(4.0 * beat);
    for (size_t b = 0; b * bar < n; ++b)
        kt::addBass(bass, sr, b * bar, bar, roots[b % 4], 0.5f);
    for (double hz : {880.0, 1108.7, 1318.5, 5274.0})
        kt::addSine(pad, sr, 0, n, hz, 0.03f);
    d.main.addMono(bass);
    d.main.addMono(pad);
    d.kick.addMono(kick);
    return d;
}

void setup(Engine& e, ParamList ps, double sr = kSr)
{
    e.prepare(sr, 512);
    for (const auto& p : ps)
        e.setParameter(p.first, p.second);
    e.reset();
}

struct BenchMode {
    const char* name;
    ParamList   params;
};

const BenchMode kModes[] = {
    {"sync", {{kParamMode, float(kModeSync)}}},
    {"sync_multi", {{kParamMode, float(kModeSync)}, {kParamMulti, 1.f}, {kParamEnvLink, 0.f}, {kParamHiMix, 40.f}}},
    {"midi", {{kParamMode, float(kModeMidi)}}},
    {"audio", {{kParamMode, float(kModeAudio)}}},
    {"spectral", {{kParamMode, float(kModeSpectral)}, {kParamTrigSource, float(kTrigAudio)}}},
    {"spectral_continuous", {{kParamMode, float(kModeSpectral)}, {kParamTrigSource, float(kTrigContinuous)}}},
    {"spectral_multi", {{kParamMode, float(kModeSpectral)}, {kParamTrigSource, float(kTrigAudio)}, {kParamMulti, 1.f},
                        {kParamEnvLink, 0.f}, {kParamSpecTarget, float(kSpecTargetVolume)}}},
    {"ringmod", {{kParamMode, float(kModeRing)}, {kParamTrigSource, float(kTrigContinuous)}}},
    {"ringmod_multi", {{kParamMode, float(kModeRing)}, {kParamTrigSource, float(kTrigAudio)}, {kParamMulti, 1.f}}},
    {"delta_sync", {{kParamMode, float(kModeSync)}, {kParamDelta, 1.f}}},
};

} // namespace

TEST_CASE(bench_modes_realtime_factor)
{
    const double seconds = 20.0;
    const Demo demo = makeDemo(kSr, seconds);
    double worst = 1e9;
    const char* worstName = "";
    for (const BenchMode& m : kModes) {
        Engine e;
        setup(e, m.params);
        Stereo out;
        kt::Driver d{e, kSr};
        d.transportValid = d.transportPlaying = true;
        d.bpm = kBpm;
        d.run(demo.main, &demo.kick, out, 512, demo.notes); // warm-up (page in, caches)
        setup(e, m.params);
        const auto t0 = std::chrono::steady_clock::now();
        d.run(demo.main, &demo.kick, out, 512, demo.notes);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const double factor = seconds / secs;
        kt::note("%-22s %8.1f x realtime  (%.3f %% of one core, 48 kHz stereo, 512-sample blocks)", m.name,
                 factor, 100.0 / factor);
        if (factor < worst) {
            worst     = factor;
            worstName = m.name;
        }
    }
    kt::note("worst case: %s at %.1f x realtime", worstName, worst);
    if (kt::isReleaseBuild())
        CHECK_MSG(worst > 40.0, "worst case %s only %.1f x realtime", worstName, worst);
}

TEST_CASE(render_demo_wavs)
{
    const Demo demo = makeDemo(kSr, 4 * 4 * 60.0 / kBpm);
    const std::string dir = kt::renderDir();
    auto save = [&](const char* name, const Stereo& s) {
        const std::string path = dir + "\\" + name + ".wav";
        CHECK_MSG(kt::writeWav(path, s, int(kSr)), "cannot write %s", path.c_str());
    };
    Stereo dry = demo.main;
    for (size_t i = 0; i < dry.size(); ++i) {
        dry.l[i] += demo.kick.l[i];
        dry.r[i] += demo.kick.r[i];
    }
    save("00_dry_kick_plus_bass", dry);
    int idx = 1;
    for (const BenchMode& m : kModes) {
        Engine e;
        setup(e, m.params);
        Stereo out;
        kt::Driver d{e, kSr};
        d.transportValid = d.transportPlaying = true;
        d.bpm = kBpm;
        d.run(demo.main, &demo.kick, out, 512, demo.notes);
        const bool isDelta = std::string(m.name).find("delta") != std::string::npos;
        if (!isDelta) {
            for (size_t i = 0; i < out.size(); ++i) {
                out.l[i] += demo.kick.l[i];
                out.r[i] += demo.kick.r[i];
            }
        }
        char name[64];
        std::snprintf(name, sizeof(name), "%02d_%s", idx++, m.name);
        save(name, out);
        float peak = 0.f;
        for (size_t i = 0; i < out.size(); ++i)
            peak = std::max(peak, std::max(std::fabs(out.l[i]), std::fabs(out.r[i])));
        CHECK(std::isfinite(peak) && peak < 4.f);
    }
    kt::note("renders written to %s", dir.c_str());
}
