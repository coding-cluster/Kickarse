// Kickarse UI — synthetic Bridge data (see DemoFeed.h); port of engineStep() in the prototype.
#include "DemoFeed.h"

#include <algorithm>
#include <cmath>

#include "Model.h"

namespace kick { namespace ui {

namespace {

float vnoise(float x)
{
    const float i = std::floor(x), f = x - i;
    auto h = [](float n) { const float s = std::sin(n * 127.1f) * 43758.5453f; return s - std::floor(s); };
    const float u = f * f * (3.f - 2.f * f);
    return (h(i) + (h(i + 1.f) - h(i)) * u) * 2.f - 1.f;
}

} // namespace

float DemoFeed::kickAt(float t) const
{
    if (t < 0.f)
        return 0.f;
    return 0.92f * std::exp(-t / 0.07f) * (0.86f + 0.14f * std::cos(t * 2.f * 3.14159265f * (55.f + 90.f * std::exp(-t / 0.02f))));
}

void DemoFeed::step(const Model& m, Live& l, double dt, float freezePhase)
{
    simT_ += dt;
    l.playing = true;
    l.bpm = 124.f;
    l.timeSigNum = 4;
    l.timeSigDen = 4;
    const int mode = m.ivalue(kParamMode);
    const double beatsCycle = m.ivalue(kParamTimeMode) == kTimeFree ? m.value(kParamLengthMs) / 1000.0 * l.bpm / 60.0
                                                                     : kRates[std::clamp(m.ivalue(kParamRate), 0, kNumRates - 1)].beats;
    l.cycleSeconds = float(beatsCycle * 60.0 / l.bpm);
    const double beat = 60.0 / l.bpm;
    const long long beatIdx = (long long)std::floor(simT_ / beat);
    const float tb = float(simT_ - double(beatIdx) * beat);
    const float hat = tb > beat * 0.5 ? 0.18f * std::exp(-(tb - float(beat) * 0.5f) / 0.03f) : 0.f;
    const float sc = kickAt(tb) + hat;
    const float scDb = 20.f * std::log10(std::max(sc, 1e-4f));
    scPeak_ = std::max(scDb, scPeak_ - float(dt) * 40.f);
    l.scLevelDb = scDb;

    const int src = (mode == kModeSpectral || mode == kModeRing) ? m.ivalue(kParamTrigSource) : -1;
    const bool transport = mode == kModeSync || src == kTrigSync || src == kTrigContinuous;
    bool trig = false;
    if (beatIdx != lastBeat_) {
        lastBeat_ = beatIdx;
        ++notes_;
        l.lastNote = 36;
        l.lastVelocity = 104;
        if (mode == kModeMidi || src == kTrigMidi)
            trig = true;
        else if (mode == kModeAudio || src == kTrigAudio)
            trig = -0.7f > m.value(kParamThreshold);
    }
    l.noteCount = notes_;

    float ph;
    if (transport) {
        ph = float(std::fmod(simT_ / l.cycleSeconds, 1.0));
        l.active = true;
        if (ph < prevPhase_ - 0.5f)
            ++trig_;
    } else {
        if (trig) { sinceTrig_ = 0.0; ++trig_; l.active = true; } else sinceTrig_ += dt;
        ph = float(sinceTrig_ / l.cycleSeconds);
        if (ph >= 1.f) {
            if (m.ivalue(kParamPlayMode) == kPlayOneShot) { ph = 0.99999f; l.active = false; }
            else ph = std::fmod(ph, 1.f);
        }
    }
    if (freezePhase >= 0.f)
        ph = freezePhase;
    const float prev = prevPhase_;
    prevPhase_ = ph;
    l.phase = ph;
    l.triggerCount = trig_;

    const PhaseMap map {m.value(kParamRotate) / 360.f,
                        std::max(1.f, float((m.ivalue(kParamTimeMode) == kTimeFree ? 1.0 : beatsCycle) / kGrids[std::clamp(m.ivalue(kParamGrid), 0, kNumGrids - 1)].beats)),
                        m.value(kParamSwing) / 100.f};
    const float depth = m.value(kParamDepth) / 100.f;
    l.valueA = m.env(0).evaluate(map.toNode(ph));
    l.valueB = (m.on(kParamEnvLink) ? m.env(0) : m.env(1)).evaluate(map.toNode(ph));
    const float gcur = 1.f - depth * (1.f - l.valueA);
    l.grDb = 20.f * std::log10(std::max(gcur, 1e-3f));

    // waveform bins the playhead crossed (all of them on the first call)
    constexpr int bins = Live::kBins;
    int a = int(prev * bins), b = int(ph * bins);
    int n = b >= a ? b - a : b + bins - a;
    if (freezePhase >= 0.f || simT_ <= dt * 1.5) {
        n = bins - 1;
        a = 0;
    }
    const bool delta = m.on(kParamDelta);
    for (int k = 0; k <= std::min(n, bins - 1); ++k) {
        const int bin = (a + k) % bins;
        const float p = float(bin) / float(bins);
        const float t = p * l.cycleSeconds;
        const float bass = 0.56f * (0.86f + 0.10f * std::sin(p * 3.14159265f * 6.f + float(simT_) * 0.7f) + 0.025f * vnoise(float(bin) * 0.12f + float(simT_) * 3.f));
        const float kk = kickAt(t) * (0.94f + 0.06f * vnoise(float(bin) * 0.37f + 11.f));
        const float gi = 1.f - depth * (1.f - m.env(0).evaluate(map.toNode(p)));
        l.mainWave[bin] = bass;
        l.extWave[bin] = kk;
        l.outWave[bin] = delta ? bass * (1.f - gi) : bass * gi;
        l.ringWave[bin] = depth * (1.f - m.env(0).evaluate(map.toNode(p))) * kk;
    }
    // spectral bands
    for (int i = 0; i < kSpecBands; ++i) {
        const float f = 30.f * std::pow(16000.f / 30.f, float(i) / float(kSpecBands - 1));
        l.specHz[i] = f;
        const float kickE = kickAt(tb) * std::exp(-std::pow(std::log(f / 60.f), 2.f) / 1.6f) + kickAt(tb) * 0.25f * std::exp(-std::pow(std::log(f / 3000.f), 2.f) / 1.2f);
        const float hatE = (tb > beat * 0.5 ? 0.3f * std::exp(-(tb - float(beat) * 0.5f) / 0.04f) : 0.f) * std::exp(-std::pow(std::log(f / 9000.f), 2.f) / 0.8f);
        const float lvl = 20.f * std::log10(std::max(kickE + hatE + 0.004f, 1e-5f));
        specSmooth_[i] = lvl > specSmooth_[i] ? lvl : specSmooth_[i] - float(dt) * 60.f;
        const float norm = std::clamp((specSmooth_[i] + 48.f) / 48.f, 0.f, 1.f);
        const float range = m.value(kParamSpecRange), sens = m.value(kParamSpecSens) / 100.f;
        const float target = -range * std::pow(norm, 1.4f) * (sens + 0.2f) * (m.ivalue(kParamSpecTarget) ? 1.f : (1.f - l.valueA) * depth + 0.15f);
        specCut_[i] = target < specCut_[i] ? specCut_[i] + (target - specCut_[i]) * 0.6f : specCut_[i] + (target - specCut_[i]) * float(dt) * 6.f;
        l.specScDb[i] = specSmooth_[i];
        l.specCutDb[i] = std::max(-range, specCut_[i]);
    }
    l.outPeakDb = -6.f;
}

}} // namespace kick::ui
