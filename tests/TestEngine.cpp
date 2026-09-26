// Kickarse — engine behaviour tests (timing, triggers, modes, routing, robustness).
#include <atomic>
#include <cfloat>
#include <initializer_list>
#include <limits>
#include <thread>
#include <utility>

#include "TestHarness.h"
#include "TestSignals.h"
#include "dsp/Trigger.h"

using namespace kick;
using kt::Stereo;

namespace {

constexpr double kSr = 48000.0;

using ParamList = std::initializer_list<std::pair<ParamId, float>>;

void setup(Engine& e, ParamList ps, double sr = kSr)
{
    e.prepare(sr, 512);
    for (const auto& p : ps)
        e.setParameter(p.first, p.second);
    e.reset(); // snap smoothers to the new values
}

// y = 0 on [0, at), 1 on [at, 1): the output of a DC input shows exactly when cycles start.
Envelope stepEnvelope(float at)
{
    Envelope e = Envelope::flat();
    e.node(0).y = 0.f;
    e.insert(EnvNode{at, 0.f, 0.f, 0});
    e.insert(EnvNode{at, 1.f, 0.f, 0});
    return e;
}

// y = x: with depth 100 % and a DC input of 1 the output is the node-space phase.
Envelope rampEnvelope()
{
    Envelope e = Envelope::flat();
    e.node(0).y = 0.f;
    return e;
}

Stereo dc(size_t n, float v)
{
    Stereo s(n);
    std::fill(s.l.begin(), s.l.end(), v);
    std::fill(s.r.begin(), s.r.end(), v);
    return s;
}

Stereo noise(size_t n, float amp, uint32_t seed)
{
    kt::Rng rng;
    rng.s = seed;
    Stereo s(n);
    for (size_t i = 0; i < n; ++i) {
        s.l[i] = amp * rng.bipolar();
        s.r[i] = amp * rng.bipolar();
    }
    return s;
}

std::vector<size_t> drops(const std::vector<float>& out, float thr = 0.5f)
{
    std::vector<size_t> d;
    for (size_t i = 0; i < out.size(); ++i)
        if (out[i] < thr && (i == 0 || out[i - 1] >= thr))
            d.push_back(i);
    return d;
}

double circularDiff(double a, double b)
{
    const double d = std::fabs(a - b);
    return std::min(d, 1.0 - d);
}

struct KickPattern {
    Stereo              sc;
    std::vector<size_t> onsets;
};

KickPattern kickPattern(double sr, int kicks, double bpm, float hatGain, size_t start)
{
    const size_t interval = size_t(60.0 / bpm * sr);
    KickPattern p;
    p.sc = Stereo(start + size_t(kicks + 1) * interval);
    std::vector<float> mono(p.sc.size(), 0.f);
    for (int k = 0; k < kicks; ++k) {
        const size_t on = start + size_t(k) * interval;
        kt::addKick(mono, sr, on, 0.9f, true, uint32_t(k + 1));
        kt::addHat(mono, sr, on + interval / 2, hatGain, uint32_t(k + 100));
        p.onsets.push_back(on);
    }
    p.sc.addMono(mono);
    return p;
}

} // namespace

TEST_CASE(engine_parameters_clamped)
{
    Engine e;
    CHECK(e.getParameter(kParamDepth) == kParams[kParamDepth].def);
    e.setParameter(kParamDepth, 250.f);
    CHECK(e.getParameter(kParamDepth) == 100.f);
    e.setParameter(kParamDepth, std::numeric_limits<float>::quiet_NaN());
    CHECK(e.getParameter(kParamDepth) == 100.f);
    e.setParameter(kParamCrossover, 1.f);
    CHECK(e.getParameter(kParamCrossover) == 20.f);
    e.setParameter(kParamCount + 3, 1.f); // ignored
    CHECK(e.getParameter(kParamCount + 3) == 0.f);
    for (int b = 0; b < kSpecBands; ++b)
        CHECK(e.bridge().specCenterHz[b].load() > 25.f && e.bridge().specCenterHz[b].load() < 17000.f);
    CHECK_NEAR(e.bridge().specCenterHz[0].load(), 30.0, 0.01);
    CHECK_NEAR(e.bridge().specCenterHz[kSpecBands - 1].load(), 16000.0, 1.0);
}

TEST_CASE(engine_sync_default_duck)
{
    Engine e;
    setup(e, {});
    const size_t n = size_t(2.0 * kSr);
    const Stereo in = dc(n, 0.5f);
    Stereo out;
    kt::Driver d{e, kSr};
    d.transportValid = d.transportPlaying = true;
    d.run(in, nullptr, out);
    for (size_t cycle = 1; cycle < 4; ++cycle) {
        const size_t start = cycle * 24000; // 1/4 at 120 bpm = 0.5 s
        CHECK_MSG(out.l[start + 100] < 0.02f, "cycle %zu start %.4f", cycle, out.l[start + 100]);
        CHECK_NEAR(out.l[start + 18000], 0.5, 1e-4);
        CHECK_NEAR(out.r[start + 18000], 0.5, 1e-4);
    }
    const Bridge& b = e.bridge();
    CHECK_NEAR(b.cycleSeconds.load(), 0.5, 1e-6);
    CHECK_NEAR(b.bpm.load(), 120.0, 1e-4);
    CHECK(b.hostPlaying.load() == 1);
    CHECK(b.envelopeActive.load() == 1);
}

TEST_CASE(engine_sync_phase_continuity)
{
    for (int rate : {6, 9, 2}) { // 1/4, 1/8, 1/1
        Engine e;
        setup(e, {{kParamSmooth, 0.f}, {kParamRate, float(rate)}});
        e.setEnvelope(0, rampEnvelope());
        const size_t n = size_t(3.0 * kSr);
        const Stereo in = dc(n, 1.f);
        Stereo out;
        kt::Driver d{e, kSr};
        d.transportValid = d.transportPlaying = true;
        d.bpm      = 137.3;
        d.ppqStart = 3.7;
        kt::Rng rng;
        std::vector<double> blockEndPhase;
        d.run(in, nullptr, out, [&](int) { return rng.range(1, 700); });
        const double beats = kRates[rate].beats;
        double worst = 0.0;
        for (size_t i = 0; i < n; ++i) {
            double expect = (d.ppqStart + double(i) * d.bpm / 60.0 / kSr) / beats;
            expect -= std::floor(expect);
            worst = std::max(worst, circularDiff(out.l[i], expect));
        }
        CHECK_MSG(worst < 1e-4, "rate %s: worst phase error %.6f", kRates[rate].label, worst);
        double lastExpect = (d.ppqStart + double(n - 1) * d.bpm / 60.0 / kSr) / beats;
        lastExpect -= std::floor(lastExpect);
        CHECK(circularDiff(e.bridge().phase.load(), lastExpect) < 1e-5);
    }
}

TEST_CASE(engine_sync_free_runs_when_stopped)
{
    Engine e;
    setup(e, {{kParamSmooth, 0.f}});
    e.setEnvelope(0, rampEnvelope());
    const size_t n = size_t(1.5 * kSr);
    Stereo out;
    kt::Driver d{e, kSr};
    d.transportValid   = true;
    d.transportPlaying = false; // host stopped: the ppq it reports is ignored
    d.bpm              = 90.0;
    d.run(dc(n, 1.f), nullptr, out, 333);
    double worst = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double expect = double(i) * 90.0 / 60.0 / kSr;
        expect -= std::floor(expect);
        worst = std::max(worst, circularDiff(out.l[i], expect));
    }
    CHECK_MSG(worst < 1e-4, "worst %.6f", worst);
    CHECK(e.bridge().hostPlaying.load() == 0);
}

TEST_CASE(engine_free_time_loop)
{
    Engine e;
    setup(e, {{kParamSmooth, 0.f}, {kParamTimeMode, 1.f}, {kParamLengthMs, 100.f}});
    e.setEnvelope(0, rampEnvelope());
    const size_t n = size_t(kSr);
    Stereo out;
    kt::Driver d{e, kSr};
    d.run(dc(n, 1.f), nullptr, out, 100);
    double worst = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double expect = double(i % 4800) / 4800.0;
        worst = std::max(worst, circularDiff(out.l[i], expect));
    }
    CHECK_MSG(worst < 1e-4, "worst %.6f", worst);
    CHECK_NEAR(e.bridge().cycleSeconds.load(), 0.1, 1e-6);
}

TEST_CASE(engine_rotate_and_swing_playback)
{
    Engine e;
    // rotate 90° shifts a 1/4 cycle by 1/16; swing 100 % on a 1/16 grid moves 2nd cells to 75 %.
    setup(e, {{kParamSmooth, 0.f}, {kParamRotate, 90.f}});
    e.setEnvelope(0, stepEnvelope(0.5f));
    Stereo out;
    kt::Driver d{e, kSr};
    d.transportValid = d.transportPlaying = true;
    d.run(dc(size_t(kSr), 1.f), nullptr, out);
    auto dr = drops(out.l);
    CHECK(!dr.empty() && dr[0] == 6000); // 0.25 of the 24000-sample cycle

    setup(e, {{kParamSmooth, 0.f}, {kParamSwing, 100.f}, {kParamRotate, 0.f}});
    e.setEnvelope(0, stepEnvelope(0.25f)); // step at node 0.25 = start of the 2nd 1/16 cell
    d.run(dc(size_t(kSr), 1.f), nullptr, out);
    // Node 0.25 plays at timeline 0.375 → rises at sample 9000 instead of 6000, exactly.
    std::vector<size_t> rises;
    for (size_t i = 1; i < 24000; ++i)
        if (out.l[i] >= 0.5f && out.l[i - 1] < 0.5f)
            rises.push_back(i);
    CHECK_MSG(rises.size() == 1 && rises[0] == 9000, "rise at %zu", rises.empty() ? size_t(0) : rises[0]);
}

TEST_CASE(engine_audio_trigger_kick_pattern)
{
    for (double sr : {44100.0, 48000.0, 96000.0}) {
        Engine e;
        setup(e,
              {{kParamMode, float(kModeAudio)}, {kParamThreshold, -24.f}, {kParamRetrigMs, 60.f},
               {kParamPlayMode, float(kPlayOneShot)}, {kParamTimeMode, 1.f}, {kParamLengthMs, 100.f},
               {kParamSmooth, 0.f}},
              sr);
        e.setEnvelope(0, stepEnvelope(0.5f));
        const KickPattern kp = kickPattern(sr, 16, 128.0, 0.015f, size_t(0.25 * sr));
        const Stereo in = dc(kp.sc.size(), 1.f);
        Stereo out;
        kt::Driver d{e, sr};
        kt::Rng rng;
        d.run(in, &kp.sc, out, [&](int) { return rng.range(16, 1024); });
        const auto dr = drops(out.l);
        CHECK_MSG(dr.size() == kp.onsets.size(), "sr %.0f: %zu drops for %zu kicks", sr, dr.size(), kp.onsets.size());
        CHECK(e.bridge().triggerCount.load() == kp.onsets.size());
        const double tol = 0.001 * sr;
        double worstMs = 0.0;
        for (size_t k = 0; k < std::min(dr.size(), kp.onsets.size()); ++k) {
            const double err = std::fabs(double(dr[k]) - double(kp.onsets[k]));
            worstMs = std::max(worstMs, err / sr * 1000.0);
            CHECK_MSG(err <= tol, "sr %.0f kick %zu: trigger %zu vs onset %zu", sr, k, dr[k], kp.onsets[k]);
        }
        kt::note("sr %.0f: %zu/%zu kicks detected, worst timing error %.3f ms", sr, dr.size(), kp.onsets.size(), worstMs);
        CHECK(out.l[kp.onsets[3] + size_t(0.02 * sr)] == 0.f);  // ducked inside the step
        CHECK(out.l[kp.onsets[3] + size_t(0.08 * sr)] == 1.f);  // released after 50 ms
    }
}

TEST_CASE(engine_audio_trigger_filter_rejects_hats)
{
    auto count = [](bool filter, int& triggers, double& worstMs) {
        Engine e;
        setup(e, {{kParamMode, float(kModeAudio)}, {kParamThreshold, -12.f}, {kParamRetrigMs, 30.f},
                  {kParamPlayMode, float(kPlayOneShot)}, {kParamTimeMode, 1.f}, {kParamLengthMs, 50.f},
                  {kParamSmooth, 0.f}, {kParamTrigFilter, filter ? 1.f : 0.f}, {kParamTrigLowCut, 20.f},
                  {kParamTrigHighCut, 150.f}});
        e.setEnvelope(0, stepEnvelope(0.5f));
        const KickPattern kp = kickPattern(kSr, 12, 124.0, 0.5f, 12000); // loud hats between kicks
        Stereo out;
        kt::Driver d{e, kSr};
        d.run(dc(kp.sc.size(), 1.f), &kp.sc, out, 256);
        const auto dr = drops(out.l);
        triggers = int(e.bridge().triggerCount.load());
        worstMs  = 0.0;
        size_t j = 0;
        for (size_t on : kp.onsets) {
            while (j < dr.size() && dr[j] + 48 < on)
                ++j;
            if (j < dr.size())
                worstMs = std::max(worstMs, std::fabs(double(dr[j]) - double(on)) / kSr * 1000.0);
        }
        return kp.onsets.size();
    };
    int    unfiltered = 0, filtered = 0;
    double w0 = 0.0, w1 = 0.0;
    const size_t kicks = count(false, unfiltered, w0);
    count(true, filtered, w1);
    CHECK_MSG(unfiltered > int(kicks), "hats should trigger without the filter (%d)", unfiltered);
    CHECK_MSG(filtered == int(kicks), "filtered: %d triggers for %zu kicks", filtered, kicks);
    CHECK_MSG(w1 <= 5.0, "filtered timing error %.3f ms", w1); // the 150 Hz low-pass adds group delay
    kt::note("unfiltered %d triggers, filtered %d (kicks %zu), filtered timing error %.3f ms", unfiltered,
             filtered, kicks, w1);
}

TEST_CASE(engine_midi_trigger_note_filter_velocity)
{
    const size_t n = size_t(1.2 * kSr);
    const std::vector<kt::Driver::Note> notes = {
        {4800, 36, 127}, {9000, 36, 0} /* note-off */, {24000, 38, 127}, {43200, 36, 64},
    };
    auto run = [&](float midiNote, float velocityPct, Stereo& out) {
        Engine e;
        setup(e, {{kParamMode, float(kModeMidi)}, {kParamMidiNote, midiNote}, {kParamVelocity, velocityPct},
                  {kParamPlayMode, float(kPlayOneShot)}, {kParamTimeMode, 1.f}, {kParamLengthMs, 100.f},
                  {kParamSmooth, 0.f}});
        e.setEnvelope(0, stepEnvelope(0.5f));
        kt::Driver d{e, kSr};
        kt::Rng rng;
        d.run(dc(n, 1.f), nullptr, out, [&](int) { return rng.range(1, 900); }, notes);
        return e.bridge().triggerCount.load();
    };
    Stereo out;
    CHECK(run(36.f, 0.f, out) == 2);
    auto dr = drops(out.l);
    CHECK(dr.size() == 2 && dr[0] == 4800 && dr[1] == 43200);
    CHECK(out.l[2000] == 1.f);        // unity before the first trigger
    CHECK(out.l[4800 + 1000] == 0.f); // velocity ignored at 0 %
    CHECK(out.l[43200 + 1000] == 0.f);

    CHECK(run(-1.f, 0.f, out) == 3);  // any note
    dr = drops(out.l);
    CHECK(dr.size() == 3 && dr[1] == 24000);

    run(36.f, 100.f, out);            // velocity 100 %: depth scales with velocity
    CHECK_NEAR(out.l[4800 + 1000], 0.0, 1e-5);
    CHECK_NEAR(out.l[43200 + 1000], 1.0 - 64.0 / 127.0, 1e-3);
}

TEST_CASE(engine_one_shot_vs_loop)
{
    const size_t n = size_t(0.6 * kSr);
    const std::vector<kt::Driver::Note> notes = {{4800, 60, 100}};
    for (int mode : {kPlayLoop, kPlayOneShot}) {
        Engine e;
        setup(e, {{kParamMode, float(kModeMidi)}, {kParamPlayMode, float(mode)}, {kParamTimeMode, 1.f},
                  {kParamLengthMs, 100.f}, {kParamSmooth, 0.f}});
        e.setEnvelope(0, stepEnvelope(0.5f));
        Stereo out;
        kt::Driver d{e, kSr};
        d.run(dc(n, 1.f), nullptr, out, 128, notes);
        const auto dr = drops(out.l);
        if (mode == kPlayLoop) {
            CHECK(dr.size() == 5); // 4800, 9600, ... 24000 inside the 28800-sample run
            for (size_t k = 0; k < dr.size(); ++k)
                CHECK(dr[k] + 1 >= 4800 + 4800 * k && dr[k] <= 4801 + 4800 * k);
            CHECK(e.bridge().envelopeActive.load() == 1);
        } else {
            CHECK(dr.size() == 1 && dr[0] == 4800);
            CHECK(out.l[n - 1] == 1.f); // holds the last node's value
            CHECK(e.bridge().envelopeActive.load() == 0);
        }
        CHECK(e.bridge().triggerCount.load() == 1);
    }
    // Waiting for the first trigger: exact passthrough, envelope inactive.
    Engine e;
    setup(e, {{kParamMode, float(kModeAudio)}});
    const Stereo in = noise(20000, 0.5f, 3);
    Stereo out;
    kt::Driver d{e, kSr};
    d.run(in, nullptr, out, 256);
    float worst = 0.f;
    for (size_t i = 0; i < in.size(); ++i)
        worst = std::max(worst, std::max(std::fabs(out.l[i] - in.l[i]), std::fabs(out.r[i] - in.r[i])));
    CHECK(worst < 1e-6f);
    CHECK(e.bridge().envelopeActive.load() == 0);
}

TEST_CASE(engine_multiband_sums_flat)
{
    for (float slope : {0.f, 1.f}) {
        for (float fc : {80.f, 150.f, 2000.f}) {
            Engine e;
            setup(e, {{kParamMulti, 1.f}, {kParamDepth, 0.f}, {kParamSlope, slope}, {kParamCrossover, fc}});
            const size_t n = 1u << 16;
            Stereo in(n);
            in.l[0] = in.r[0] = 1.f;
            Stereo out;
            kt::Driver d{e, kSr};
            d.run(in, nullptr, out, 512);
            double worst = 0.0;
            for (int k = 0; k <= 40; ++k) {
                const double hz = 20.0 * std::pow(1000.0, double(k) / 40.0);
                double re = 0.0, im = 0.0;
                for (size_t i = 0; i < n; ++i) {
                    re += out.l[i] * std::cos(2.0 * kt::kPi * hz * double(i) / kSr);
                    im -= out.l[i] * std::sin(2.0 * kt::kPi * hz * double(i) / kSr);
                }
                worst = std::max(worst, std::fabs(10.0 * std::log10(re * re + im * im)));
            }
            CHECK_MSG(worst < 0.05, "slope %s fc %.0f: %.4f dB", slope > 0.f ? "24" : "12", fc, worst);
        }
    }
}

TEST_CASE(engine_multiband_band_gains_and_solo)
{
    const size_t n = size_t(2.0 * kSr);
    Stereo in(n);
    std::vector<float> m(n, 0.f);
    kt::addSine(m, kSr, 0, n, 50.0, 0.4f);
    kt::addSine(m, kSr, 0, n, 5000.0, 0.4f);
    in.addMono(m);
    auto run = [&](float solo, Stereo& out) {
        Engine e;
        setup(e, {{kParamMulti, 1.f}, {kParamCrossover, 500.f}, {kParamLoMix, 100.f}, {kParamHiMix, 0.f},
                  {kParamRate, 2.f}, {kParamSmooth, 0.f}, {kParamBandSolo, solo}});
        e.setEnvelope(0, stepEnvelope(0.5f)); // 1/1 at 120 bpm: ducked for the first second
        kt::Driver d{e, kSr};
        d.transportValid = d.transportPlaying = true;
        d.run(in, nullptr, out, 512);
    };
    Stereo out;
    run(0.f, out);
    const size_t a = 8000, b = 40000;
    CHECK(kt::toneAmplitude(out.l, a, b, 50.0, kSr) < 0.4 * 0.01);                 // low band ducked
    CHECK_NEAR(kt::toDb(kt::toneAmplitude(out.l, a, b, 5000.0, kSr) / 0.4), 0.0, 0.1); // high untouched
    CHECK_NEAR(kt::toDb(kt::toneAmplitude(out.l, 56000, 90000, 50.0, kSr) / 0.4), 0.0, 0.1);
    run(float(kSoloHigh), out);
    CHECK(kt::toneAmplitude(out.l, 56000, 90000, 50.0, kSr) < 0.4 * 0.01);
    CHECK_NEAR(kt::toDb(kt::toneAmplitude(out.l, 56000, 90000, 5000.0, kSr) / 0.4), 0.0, 0.1);
    run(float(kSoloLow), out);
    CHECK(kt::toneAmplitude(out.l, 56000, 90000, 5000.0, kSr) < 0.4 * 0.01);
    CHECK_NEAR(kt::toDb(kt::toneAmplitude(out.l, 56000, 90000, 50.0, kSr) / 0.4), 0.0, 0.1);
}

TEST_CASE(engine_spectral_cuts_competing_bands)
{
    const size_t n = size_t(1.6 * kSr);
    Stereo main(n), sc(n);
    std::vector<float> m(n, 0.f), s(n, 0.f);
    kt::addSine(m, kSr, 0, n, 55.0, 0.5f);    // bass
    kt::addSine(m, kSr, 0, n, 6000.0, 0.1f);  // highs (hats / air)
    kt::addSine(s, kSr, size_t(0.5 * kSr), size_t(1.0 * kSr), 60.0, 0.7f); // sustained kick body
    main.addMono(m);
    sc.addMono(s);

    Engine e;
    setup(e, {{kParamMode, float(kModeSpectral)}, {kParamTrigSource, float(kTrigContinuous)}});
    Stereo out;
    kt::Driver d{e, kSr};
    d.run(main, &sc, out, 512);

    const size_t b0 = size_t(0.2 * kSr), b1 = size_t(0.45 * kSr);
    const size_t d0 = size_t(0.8 * kSr), d1 = size_t(1.4 * kSr);
    const double bassBefore = kt::toDb(kt::toneAmplitude(out.l, b0, b1, 55.0, kSr) / 0.5);
    const double hiBefore   = kt::toDb(kt::toneAmplitude(out.l, b0, b1, 6000.0, kSr) / 0.1);
    const double bassDuring = kt::toDb(kt::toneAmplitude(out.l, d0, d1, 55.0, kSr) / 0.5);
    const double hiDuring   = kt::toDb(kt::toneAmplitude(out.l, d0, d1, 6000.0, kSr) / 0.1);
    kt::note("55 Hz: %.2f dB before, %.2f dB under the kick; 6 kHz: %.2f dB before, %.2f dB under the kick",
             bassBefore, bassDuring, hiBefore, hiDuring);
    CHECK(std::fabs(bassBefore) < 0.3);
    CHECK(std::fabs(hiBefore) < 0.3);
    CHECK(bassDuring < -9.0);
    CHECK(bassDuring > -24.0); // capped by spec_range (18 dB) plus neighbouring-band overlap
    CHECK(std::fabs(hiDuring) < 1.0);

    // Presence weighting: a sidechain band the main signal does not occupy is not cut.
    Stereo sc2(n);
    std::vector<float> s2(n, 0.f);
    kt::addSine(s2, kSr, 0, n, 60.0, 0.5f);
    kt::addSine(s2, kSr, 0, n, 3000.0, 0.5f);
    sc2.addMono(s2);
    Stereo bassOnly(n);
    std::vector<float> m2(n, 0.f);
    kt::addSine(m2, kSr, 0, n, 55.0, 0.5f);
    bassOnly.addMono(m2);
    int band3k = 0, band60 = 0;
    for (int b = 0; b < kSpecBands; ++b) {
        const float hz = e.bridge().specCenterHz[b].load();
        if (std::fabs(std::log2(hz / 3000.f)) < std::fabs(std::log2(e.bridge().specCenterHz[band3k].load() / 3000.f)))
            band3k = b;
        if (std::fabs(std::log2(hz / 60.f)) < std::fabs(std::log2(e.bridge().specCenterHz[band60].load() / 60.f)))
            band60 = b;
    }
    setup(e, {{kParamMode, float(kModeSpectral)}, {kParamTrigSource, float(kTrigContinuous)}});
    d.run(bassOnly, &sc2, out, 512);
    const float cut3kAlone = e.bridge().specCutDb[band3k].load();
    const float cut60      = e.bridge().specCutDb[band60].load();
    CHECK_MSG(cut3kAlone > -1.f, "3 kHz band cut %.2f dB without main content there", cut3kAlone);
    CHECK_MSG(cut60 < -12.f, "60 Hz band cut %.2f dB", cut60);
    CHECK(e.bridge().specScDb[band60].load() > -12.f);

    Stereo withHigh = bassOnly;
    std::vector<float> h(n, 0.f);
    kt::addSine(h, kSr, 0, n, 3000.0, 0.3f);
    withHigh.addMono(h);
    setup(e, {{kParamMode, float(kModeSpectral)}, {kParamTrigSource, float(kTrigContinuous)}});
    d.run(withHigh, &sc2, out, 512);
    const float cut3kShared = e.bridge().specCutDb[band3k].load();
    CHECK_MSG(cut3kShared < -6.f, "3 kHz band cut %.2f dB with competing main content", cut3kShared);
    kt::note("cut @60 Hz %.1f dB, @3 kHz %.1f dB (main absent) / %.1f dB (main present)", cut60, cut3kAlone,
             cut3kShared);
    CHECK(e.bridge().gainReductionDb.load() < -6.f);
}

TEST_CASE(engine_spectral_envelope_scales_depth)
{
    // spec_target = Depth with a Sync envelope: no cut where y = 1, full cut where y = 0.
    const size_t n = size_t(2.0 * kSr);
    Stereo main(n), sc(n);
    std::vector<float> m(n, 0.f), s(n, 0.f);
    kt::addSine(m, kSr, 0, n, 55.0, 0.5f);
    kt::addSine(s, kSr, 0, n, 60.0, 0.7f);
    main.addMono(m);
    sc.addMono(s);
    for (float target : {float(kSpecTargetDepth), float(kSpecTargetVolume)}) {
        Engine e;
        setup(e, {{kParamMode, float(kModeSpectral)}, {kParamTrigSource, float(kTrigSync)}, {kParamRate, 2.f},
                  {kParamSmooth, 0.f}, {kParamSpecTarget, target}});
        e.setEnvelope(0, stepEnvelope(0.5f));
        Stereo out;
        kt::Driver d{e, kSr};
        d.transportValid = d.transportPlaying = true;
        d.run(main, &sc, out, 512);
        const double ducked   = kt::toDb(kt::toneAmplitude(out.l, 8000, 40000, 55.0, kSr) / 0.5);  // y = 0
        const double released = kt::toDb(kt::toneAmplitude(out.l, 56000, 90000, 55.0, kSr) / 0.5); // y = 1
        if (target == float(kSpecTargetDepth)) {
            CHECK_MSG(ducked < -9.0, "depth target ducked %.2f", ducked);
            CHECK_MSG(std::fabs(released) < 0.5, "depth target released %.2f", released);
        } else {
            CHECK_MSG(ducked < -60.0, "volume target ducked %.2f", ducked);       // volume fully ducked
            CHECK_MSG(released < -9.0, "volume target released %.2f", released);   // spectral cut stays
        }
    }
}

TEST_CASE(engine_ring_mod_controls_overlap_peak)
{
    const size_t n = size_t(2.0 * kSr);
    std::vector<float> kick(n, 0.f), bass(n, 0.f);
    for (int k = 0; k < 4; ++k)
        kt::addKick(kick, kSr, size_t(0.1 * kSr) + size_t(k) * 24000, 0.9f, false);
    kt::addSine(bass, kSr, 0, n, 55.0, 0.8f);
    Stereo main(n), sc(n);
    main.addMono(bass);
    sc.addMono(kick);

    Engine e;
    setup(e, {{kParamMode, float(kModeRing)}, {kParamTrigSource, float(kTrigContinuous)},
              {kParamRingAttack, 0.01f}, {kParamRingRelease, 1.f}});
    Stereo out;
    kt::Driver d{e, kSr};
    d.run(main, &sc, out, 512);

    float peakDry = 0.f, peakWet = 0.f;
    const size_t a = size_t(0.1 * kSr) + 24000, b = a + size_t(0.25 * kSr); // 2nd kick: tracker warmed up
    for (size_t i = a; i < b; ++i) {
        peakDry = std::max(peakDry, std::fabs(kick[i] + bass[i]));
        peakWet = std::max(peakWet, std::fabs(kick[i] + out.l[i]));
    }
    const float kickPeak = kt::peakAbs(kick, a, b);
    kt::note("overlap peak: dry %.3f, ring %.3f (kick %.3f, bass 0.800)", peakDry, peakWet, kickPeak);
    CHECK(peakWet < peakDry * 0.8f);
    CHECK(peakWet <= std::max(kickPeak, 0.8f) * 1.05f);
    // Between kicks the bass is untouched.
    CHECK_NEAR(kt::toDb(kt::toneAmplitude(out.l, 46000, 52000, 55.0, kSr) / 0.8), 0.0, 0.5);
    // With slow settings it becomes a smooth ducker: less sideband content, still reduces the level.
    setup(e, {{kParamMode, float(kModeRing)}, {kParamTrigSource, float(kTrigContinuous)},
              {kParamRingAttack, 5.f}, {kParamRingRelease, 150.f}});
    d.run(main, &sc, out, 512);
    CHECK(kt::toneAmplitude(out.l, a + 2400, a + 9600, 55.0, kSr) < 0.8 * 0.5);
}

TEST_CASE(engine_delta_is_dry_minus_wet)
{
    const Stereo in = noise(size_t(kSr), 0.5f, 11);
    for (float mode : {float(kModeSync), float(kModeSpectral), float(kModeRing)}) {
        Stereo wet, delta;
        const Stereo sc = noise(size_t(kSr), 0.3f, 12);
        for (int pass = 0; pass < 2; ++pass) {
            Engine e;
            setup(e, {{kParamMode, mode}, {kParamTrigSource, float(kTrigSync)}, {kParamDelta, float(pass)},
                      {kParamMidSide, 30.f}, {kParamOutGain, -3.f}});
            kt::Driver d{e, kSr};
            d.transportValid = d.transportPlaying = true;
            d.run(in, &sc, pass ? delta : wet, 333);
        }
        const float og = std::pow(10.f, -3.f / 20.f);
        float worst = 0.f;
        for (size_t i = 0; i < in.size(); ++i) {
            worst = std::max(worst, std::fabs(delta.l[i] - (og * in.l[i] - wet.l[i])));
            worst = std::max(worst, std::fabs(delta.r[i] - (og * in.r[i] - wet.r[i])));
        }
        CHECK_MSG(worst < 2e-6f, "mode %.0f: |delta - (dry - wet)| = %g", mode, worst);
    }
    // Multiband with nothing removed: delta is silent (reference is phase-matched).
    Engine e;
    setup(e, {{kParamMulti, 1.f}, {kParamDepth, 0.f}, {kParamDelta, 1.f}});
    Stereo out;
    kt::Driver d{e, kSr};
    d.run(in, nullptr, out, 256);
    CHECK(kt::peakAbs(out.l) < 1e-5f && kt::peakAbs(out.r) < 1e-5f);
}

TEST_CASE(engine_mid_side_routing)
{
    const size_t n = 12000;
    Stereo midSig(n), sideSig(n);
    for (size_t i = 0; i < n; ++i) {
        const float v = 0.5f * std::sin(0.05f * float(i));
        midSig.l[i] = midSig.r[i] = v;
        sideSig.l[i] = v;
        sideSig.r[i] = -v;
    }
    auto run = [&](float ms, const Stereo& in) {
        Engine e;
        setup(e, {{kParamMidSide, ms}, {kParamSmooth, 0.f}});
        e.setEnvelope(0, stepEnvelope(0.5f)); // first 12000 samples of each 1/4 cycle fully ducked
        Stereo out;
        kt::Driver d{e, kSr};
        d.transportValid = d.transportPlaying = true;
        d.run(in, nullptr, out, 512);
        return std::max(kt::peakAbs(out.l, 100), kt::peakAbs(out.r, 100));
    };
    CHECK(run(0.f, midSig) < 1e-6f);
    CHECK(run(0.f, sideSig) < 1e-6f);
    CHECK(run(-100.f, midSig) < 1e-6f);             // mid processed
    CHECK_NEAR(run(-100.f, sideSig), 0.5, 1e-3);    // side passes dry
    CHECK_NEAR(run(100.f, midSig), 0.5, 1e-3);
    CHECK(run(100.f, sideSig) < 1e-6f);
    CHECK_NEAR(run(-50.f, sideSig), 0.25, 1e-3);    // continuous in between
}

TEST_CASE(engine_bypass_crossfade_and_out_gain)
{
    Engine e;
    setup(e, {{kParamOutGain, 6.f}});
    e.setEnvelope(0, Envelope::flat());
    const Stereo in = noise(size_t(kSr), 0.5f, 5);
    Stereo out;
    kt::Driver d{e, kSr};
    d.transportValid = d.transportPlaying = true;
    d.run(in, nullptr, out, 512);
    const double og = std::pow(10.0, 6.0 / 20.0);
    CHECK_NEAR(out.l[20000] / in.l[20000], og, 1e-4);

    e.setParameter(kParamBypass, 1.f);
    d.ppqStart = 0.0;
    Stereo out2;
    kt::Driver d2{e, kSr};
    d2.transportValid = d2.transportPlaying = true;
    d2.run(in, nullptr, out2, 64);
    int notDry = 0;
    for (size_t i = 481; i < 5000; ++i)
        notDry += (out2.l[i] == in.l[i] && out2.r[i] == in.r[i]) ? 0 : 1;
    CHECK(notDry == 0);                                        // bit-exact dry after 10 ms
    CHECK_NEAR(out2.l[240] / in.l[240], 0.5 * og + 0.5, 0.01); // linear crossfade, halfway at 5 ms
    CHECK(e.bridge().gainReductionDb.load() == 0.f);
}

TEST_CASE(engine_trigger_listen)
{
    Engine e;
    setup(e, {{kParamMode, float(kModeAudio)}, {kParamTrigFilter, 1.f}, {kParamTrigLowCut, 40.f},
              {kParamTrigHighCut, 300.f}, {kParamTrigListen, 1.f}});
    const KickPattern kp = kickPattern(kSr, 4, 120.0, 0.3f, 1000);
    Stereo out;
    kt::Driver d{e, kSr};
    d.run(noise(kp.sc.size(), 0.5f, 9), &kp.sc, out, 512);
    dsp::SidechainFilter f;
    f.prepare(kSr);
    f.setCutoffs(40.f, 300.f);
    float worst = 0.f;
    for (size_t i = 0; i < kp.sc.size(); ++i) {
        const float ref = f.process(0.5f * (kp.sc.l[i] + kp.sc.r[i]));
        worst = std::max(worst, std::max(std::fabs(out.l[i] - ref), std::fabs(out.r[i] - ref)));
    }
    CHECK_MSG(worst < 1e-5f, "listen differs from the filtered sidechain by %g", worst);
}

TEST_CASE(engine_recording_handshake)
{
    // Sync: kicks on every beat, arm → records exactly the next full cycle → Done.
    Engine e;
    setup(e, {});
    const size_t n = size_t(2.0 * kSr);
    Stereo sc(n);
    std::vector<float> k(n, 0.f);
    for (size_t on = 12000; on < n; on += 24000) // transport starts at ppq 0.5: beats at 12000 + 24000 k
        kt::addKick(k, kSr, on, 0.9f, false);
    sc.addMono(k);
    Bridge& b = e.bridge();
    b.recState.store(Bridge::kRecArmed);
    kt::Driver d{e, kSr};
    d.transportValid = d.transportPlaying = true;
    d.ppqStart = 0.5;
    Stereo out;
    const Stereo main = dc(n, 0.1f);
    std::vector<int> states;
    int     sawRecording = 0;
    d.run(main, &sc, out, [&](int) {
        const int st = b.recState.load();
        sawRecording += st == Bridge::kRecRecording ? 1 : 0;
        return 512;
    });
    CHECK(sawRecording > 0);
    CHECK(b.recState.load(std::memory_order_acquire) == Bridge::kRecDone);
    CHECK(b.recProgress.load() == 1.f);
    auto binPeak = [&](int a, int z) {
        float m = 0.f;
        for (int i = a; i < z; ++i)
            m = std::max(m, b.recBuf[i].load());
        return m;
    };
    CHECK(binPeak(0, 20) > 0.5f);                          // kick at the start of the cycle
    CHECK(binPeak(Bridge::kRecBins - 40, Bridge::kRecBins) < 0.02f); // silent at the end
    CHECK(binPeak(100, 200) > binPeak(300, 400));          // decaying contour
    CHECK(binPeak(300, 400) > binPeak(500, 600));

    // UI consumes, DSP stays idle and leaves the buffer alone.
    b.recState.store(Bridge::kRecIdle);
    const float keep = b.recBuf[4].load();
    b.recBuf[4].store(-1.f);
    d.run(main, &sc, out, 512);
    CHECK(b.recState.load() == Bridge::kRecIdle);
    CHECK(b.recBuf[4].load() == -1.f);
    b.recBuf[4].store(keep);

    // Cancel mid-recording: the DSP stops and never reports Done.
    setup(e, {});
    b.recState.store(Bridge::kRecArmed);
    kt::Driver d3{e, kSr};
    d3.transportValid = d3.transportPlaying = true;
    d3.ppqStart = 0.5;
    d3.run(main, &sc, out, [&](int blk) {
        if (b.recState.load() == Bridge::kRecRecording && blk > 40)
            b.recState.store(Bridge::kRecIdle);
        return 512;
    });
    CHECK(b.recState.load() == Bridge::kRecIdle);

    // Audio-triggered: recording starts at the next trigger.
    setup(e, {{kParamMode, float(kModeAudio)}, {kParamTimeMode, 1.f}, {kParamLengthMs, 200.f}});
    b.recState.store(Bridge::kRecArmed);
    kt::Driver d4{e, kSr};
    d4.run(main, &sc, out, 256);
    CHECK(b.recState.load(std::memory_order_acquire) == Bridge::kRecDone);
    CHECK(binPeak(0, 20) > 0.5f);
}

TEST_CASE(engine_bridge_waveforms_and_meter)
{
    Engine e;
    setup(e, {{kParamSmooth, 0.f}});
    e.setEnvelope(0, stepEnvelope(0.5f));
    const size_t n = size_t(1.5 * kSr);
    Stereo in(n);
    for (size_t i = 0; i < n; ++i)
        if ((i % 24000) < 12000)
            in.l[i] = in.r[i] = 0.4f; // main only in the first half of each cycle
    Stereo out;
    kt::Driver d{e, kSr};
    d.transportValid = d.transportPlaying = true;
    d.run(in, nullptr, out, 480);
    const Bridge& b = e.bridge();
    int mainFirst = 0, mainSecond = 0, outFirst = 0;
    for (int i = 5; i < Bridge::kWaveBins / 2 - 5; ++i) {
        mainFirst += b.mainWave[i].load() > 0.3f ? 1 : 0;
        outFirst += b.outWave[i].load() < 1e-6f ? 1 : 0;
    }
    for (int i = Bridge::kWaveBins / 2 + 5; i < Bridge::kWaveBins - 5; ++i)
        mainSecond += b.mainWave[i].load() < 1e-6f ? 1 : 0;
    CHECK(mainFirst > 200);
    CHECK(mainSecond > 200);
    CHECK(outFirst > 200); // ducked to silence in the first half
    CHECK(e.bridge().gainReductionDb.load() == 0.f); // block ends in the released half

    // The meter shows the duck while inside it.
    Stereo shortIn = dc(2000, 0.5f), shortOut;
    setup(e, {{kParamSmooth, 0.f}});
    kt::Driver d2{e, kSr};
    d2.transportValid = d2.transportPlaying = true;
    d2.run(shortIn, nullptr, shortOut, 512);
    CHECK(e.bridge().gainReductionDb.load() < -20.f);
    CHECK(e.bridge().valueA.load() < 0.5f);
}

TEST_CASE(engine_block_size_independence)
{
    const KickPattern kp = kickPattern(kSr, 8, 128.0, 0.02f, 3000);
    const size_t n = kp.sc.size();
    Stereo main(n);
    std::vector<float> bass(n, 0.f);
    kt::addBass(bass, kSr, 0, n, 55.0, 0.6f);
    main.addMono(bass);
    struct Cfg {
        const char* name;
        ParamList   params;
    };
    const Cfg cfgs[] = {
        {"audio", {{kParamMode, float(kModeAudio)}}},
        {"sync-free-time-swing", {{kParamMode, float(kModeSync)}, {kParamTimeMode, 1.f}, {kParamSwing, 40.f}}},
        {"spectral-multi", {{kParamMode, float(kModeSpectral)}, {kParamMulti, 1.f}, {kParamEnvLink, 0.f}}},
        {"ring-multi", {{kParamMode, float(kModeRing)}, {kParamMulti, 1.f}, {kParamTrigSource, float(kTrigAudio)}}},
    };
    for (const Cfg& c : cfgs) {
        Stereo ref, var, big;
        Engine e;
        setup(e, c.params);
        kt::Driver d{e, kSr};
        d.run(main, &kp.sc, ref, 256);
        setup(e, c.params);
        kt::Rng rng;
        d.run(main, &kp.sc, var, [&](int) { return rng.range(1, 1000); });
        setup(e, c.params);
        d.run(main, &kp.sc, big, 10000); // > maxBlockSize
        float worst = 0.f;
        for (size_t i = 0; i < n; ++i) {
            worst = std::max(worst, std::fabs(ref.l[i] - var.l[i]) + std::fabs(ref.r[i] - var.r[i]));
            worst = std::max(worst, std::fabs(ref.l[i] - big.l[i]) + std::fabs(ref.r[i] - big.r[i]));
        }
        CHECK_MSG(worst < 1e-6f, "%s: block-size dependence %g", c.name, worst);
    }
    // Zero-length and null-output calls are harmless.
    Engine e;
    const float* in[2] = {nullptr, nullptr};
    float* outp[2] = {nullptr, nullptr};
    e.process(in, in, outp, 0, TransportInfo{}, nullptr, 0);
    e.process(in, in, nullptr, 128, TransportInfo{}, nullptr, 0);
    std::vector<float> o(64, 1.f);
    float* o2[2] = {o.data(), o.data()};
    e.process(nullptr, nullptr, o2, 64, TransportInfo{}, nullptr, 5); // null inputs, notes without array
    CHECK(o[10] == 0.f);
}

TEST_CASE(engine_silence_nan_denormals)
{
    for (float mode : {float(kModeSync), float(kModeSpectral), float(kModeRing), float(kModeAudio)}) {
        Engine e;
        setup(e, {{kParamMode, mode}, {kParamMulti, 1.f}, {kParamTrigSource, float(kTrigContinuous)},
                  {kParamSpecRelease, 2000.f}});
        const size_t n = size_t(31.0 * kSr);
        Stereo in(n), sc(n);
        kt::Rng rng;
        for (size_t i = 0; i < size_t(kSr); ++i) {
            in.l[i] = in.r[i] = 0.5f * rng.bipolar();
            sc.l[i] = sc.r[i] = 0.8f * rng.bipolar();
        }
        Stereo out;
        kt::Driver d{e, kSr};
        d.run(in, &sc, out, 512);
        int bad = 0, subnormal = 0;
        for (size_t i = 0; i < n; ++i) {
            for (float v : {out.l[i], out.r[i]}) {
                bad += std::isfinite(v) ? 0 : 1;
                subnormal += (v != 0.f && std::fabs(v) < FLT_MIN) ? 1 : 0;
            }
        }
        CHECK_MSG(bad == 0 && subnormal == 0, "mode %.0f: %d non-finite, %d subnormal", mode, bad, subnormal);
        CHECK(std::fabs(out.l[n - 1]) < 1e-6f);

        // Garbage input: NaN / Inf / huge values never leak out, processing recovers.
        Stereo g = noise(size_t(kSr), 0.5f, 21), gs = noise(size_t(kSr), 0.5f, 22);
        for (size_t i = 0; i < g.size(); i += 97) {
            g.l[i]  = std::numeric_limits<float>::quiet_NaN();
            gs.r[i] = std::numeric_limits<float>::infinity();
            if (i % 3 == 0)
                g.r[i] = 1e30f;
        }
        d.run(g, &gs, out, 256);
        for (size_t i = 0; i < g.size(); ++i)
            bad += (std::isfinite(out.l[i]) && std::isfinite(out.r[i])) ? 0 : 1;
        const Stereo clean = noise(size_t(kSr), 0.5f, 23);
        d.run(clean, &gs, out, 256);
        CHECK(bad == 0);
        CHECK(kt::peakAbs(out.l, size_t(0.5 * kSr)) < 4.f);
    }
}

TEST_CASE(engine_sample_rates)
{
    for (double sr : {44100.0, 88200.0, 96000.0, 192000.0}) {
        const KickPattern kp = kickPattern(sr, 6, 120.0, 0.01f, size_t(0.1 * sr));
        const size_t n = kp.sc.size();
        Stereo main(n);
        std::vector<float> bass(n, 0.f);
        kt::addBass(bass, sr, 0, n, 55.0, 0.6f);
        main.addMono(bass);
        for (int mode = 0; mode < kModeCount; ++mode) {
            Engine e;
            setup(e, {{kParamMode, float(mode)}, {kParamMulti, mode >= kModeSpectral ? 1.f : 0.f},
                      {kParamTrigSource, float(kTrigAudio)}},
                  sr);
            Stereo out;
            kt::Driver d{e, sr};
            d.transportValid = d.transportPlaying = true;
            d.run(main, &kp.sc, out, 512);
            float peak = 0.f;
            int   bad  = 0;
            for (size_t i = 0; i < n; ++i) {
                bad += std::isfinite(out.l[i]) ? 0 : 1;
                peak = std::max(peak, std::fabs(out.l[i]));
            }
            CHECK_MSG(bad == 0 && peak < 2.f, "sr %.0f mode %d: peak %.3f bad %d", sr, mode, peak, bad);
            if (mode == kModeAudio || mode >= kModeSpectral)
                CHECK_MSG(e.bridge().triggerCount.load() == kp.onsets.size(), "sr %.0f mode %d: %u triggers", sr,
                          mode, e.bridge().triggerCount.load());
        }
    }
}

TEST_CASE(engine_set_envelope_while_processing)
{
    Engine e;
    setup(e, {});
    std::atomic<bool> stop {false};
    std::thread ui([&] {
        kt::Rng rng;
        int k = 0;
        while (!stop.load()) {
            Envelope env = Envelope::flat();
            for (int i = 0; i < 20; ++i)
                env.insert(EnvNode{rng.uniform(), rng.uniform(), rng.bipolar(), 0});
            e.setEnvelope(k++ & 1, env);
        }
    });
    const Stereo in = noise(size_t(3.0 * kSr), 0.5f, 77);
    Stereo out;
    kt::Driver d{e, kSr};
    d.transportValid = d.transportPlaying = true;
    d.run(in, nullptr, out, 64);
    stop.store(true);
    ui.join();
    float peak = 0.f;
    for (size_t i = 0; i < in.size(); ++i)
        peak = std::max(peak, std::fabs(out.l[i]));
    CHECK(std::isfinite(peak) && peak <= 0.5001f);
}

TEST_CASE(engine_bridge_activity_values)
{
    // MIDI: every note-on counts (before the note filter), note-offs don't; only matches trigger.
    Engine e;
    setup(e, {{kParamMode, float(kModeMidi)}, {kParamMidiNote, 36.f}});
    const std::vector<kt::Driver::Note> notes = {
        {1000, 36, 100}, {2000, 38, 90}, {2500, 38, 0} /* note-off */, {3000, 40, 77},
    };
    Stereo out;
    kt::Driver d{e, kSr};
    d.timeSigNum = 7;
    d.timeSigDen = 8;
    d.run(dc(4800, 0.5f), nullptr, out, 256, notes);
    const Bridge& b = e.bridge();
    CHECK(b.noteCount.load() == 3);
    CHECK(b.triggerCount.load() == 1);
    CHECK(b.lastNote.load() == 40);
    CHECK(b.lastVelocity.load() == 77);
    CHECK(b.timeSigNum.load() == 7 && b.timeSigDen.load() == 8);
    // Notes are counted in non-MIDI modes too (MIDI Learn works from any mode).
    setup(e, {{kParamMode, float(kModeSync)}});
    d.run(dc(4800, 0.5f), nullptr, out, 256, notes);
    CHECK(b.noteCount.load() == 6);
    d.timeSigNum = 0; // nonsense from a host is clamped
    d.run(dc(512, 0.5f), nullptr, out, 256);
    CHECK(b.timeSigNum.load() == 1);

    // scLevelDb follows a steady tone through the trigger filter (the level the threshold sees).
    auto scLevel = [&](double hz, bool filter) {
        setup(e, {{kParamMode, float(kModeAudio)}, {kParamTrigFilter, filter ? 1.f : 0.f},
                  {kParamTrigLowCut, 20.f}, {kParamTrigHighCut, 150.f}});
        const size_t n = size_t(0.5 * kSr);
        Stereo sc(n);
        std::vector<float> t(n, 0.f);
        kt::addSine(t, kSr, 0, n, hz, 0.25f); // -12 dBFS
        sc.addMono(t);
        kt::Driver dd{e, kSr};
        dd.run(dc(n, 0.f), &sc, out, 512);
        return b.scLevelDb.load();
    };
    const float lowOpen = scLevel(60.0, false), lowFiltered = scLevel(60.0, true);
    const float highOpen = scLevel(3000.0, false), highFiltered = scLevel(3000.0, true);
    kt::note("scLevelDb for a -12 dBFS tone: 60 Hz %.2f / %.2f dB, 3 kHz %.2f / %.2f dB (filter off / on)",
             lowOpen, lowFiltered, highOpen, highFiltered);
    CHECK(lowOpen > -13.5f && lowOpen < -11.9f);
    CHECK(lowFiltered > -13.5f && lowFiltered < -11.9f);
    CHECK(highOpen > -12.5f && highOpen < -11.9f);
    CHECK(highFiltered < -45.f);

    // outPeakDb: holds the output peak and decays (~300 ms time constant) once the signal stops.
    setup(e, {});
    e.setEnvelope(0, Envelope::flat());
    Stereo burst(static_cast<size_t>(kSr));
    for (size_t i = 0; i < 4800; ++i)
        burst.l[i] = burst.r[i] = 0.5f;
    kt::Driver d3{e, kSr};
    std::vector<float> peaks;
    d3.run(burst, nullptr, out, [&](int blk) {
        peaks.push_back(b.outPeakDb.load());
        return blk == 0 ? 4800 : 9600;
    });
    peaks.push_back(b.outPeakDb.load());
    CHECK_NEAR(peaks[1], -6.02, 0.01);  // right after the burst
    CHECK_NEAR(peaks.back(), -6.02 - 8.686 * (43200.0 / kSr) / 0.3, 0.5); // 0.9 s later
}

TEST_CASE(engine_bridge_ring_mod_wave)
{
    // Ring mode: the per-bin modulation amount shows the kick at the start of each cycle.
    Engine e;
    setup(e, {{kParamMode, float(kModeRing)}, {kParamTrigSource, float(kTrigSync)}, {kParamSmooth, 0.f}});
    Envelope full = Envelope::flat();
    full.node(0).y = full.node(1).y = 0.f; // y = 0: full amount everywhere
    e.setEnvelope(0, full);
    const size_t n = size_t(1.5 * kSr);
    std::vector<float> k(n, 0.f);
    for (size_t on = 0; on < n; on += 24000)
        kt::addKick(k, kSr, on, 0.9f, false);
    Stereo sc(n), main = dc(n, 0.3f), out;
    sc.addMono(k);
    kt::Driver d{e, kSr};
    d.transportValid = d.transportPlaying = true;
    d.run(main, &sc, out, 512);
    const Bridge& b = e.bridge();
    float early = 0.f, late = 0.f;
    for (int i = 2; i < 20; ++i)
        early = std::max(early, b.ringModWave[i].load());
    for (int i = 400; i < 500; ++i)
        late = std::max(late, b.ringModWave[i].load());
    kt::note("ringModWave: %.3f at the kick, %.3f late in the cycle", early, late);
    CHECK(early > 0.8f && early <= 1.f);
    CHECK(late < 0.05f);

    // Other modes leave it untouched.
    Engine v;
    setup(v, {});
    kt::Driver dv{v, kSr};
    dv.transportValid = dv.transportPlaying = true;
    dv.run(main, &sc, out, 512);
    float any = 0.f;
    for (int i = 0; i < Bridge::kWaveBins; ++i)
        any = std::max(any, v.bridge().ringModWave[i].load());
    CHECK(any == 0.f);
}
