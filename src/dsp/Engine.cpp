// Kickarse — audio engine. Signal flow per internal chunk (<= 256 samples, fixed scratch buffers):
//
//   1. inputs      sanitise, main → Mid/Side, sidechain → mono → trigger filter = detection signal
//   2. clock       triggers (audio detector / MIDI) → cycle clock → PhaseMap → envelope tables →
//                  smoothed y per band; recording handshake
//   3. gains       per-sample band gains (volume, ring) or spectral depth scales
//   4. processing  [spectral EQ] → [LR crossover, per-band gains, solo] → wet + phase-matched reference
//   5. output      Mid/Side amounts, delta (reference − wet), output gain, listen, bypass crossfade,
//                  waveform bins
//
// Everything is processed in the Mid/Side domain: all stages are linear in the main signal with the
// same time-varying coefficients on both channels, so processing M and S is identical to processing
// L and R, and the Mid/Side slider becomes a simple per-component wet/dry amount.
#include "Engine.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <initializer_list>

#include "Crossover.h"
#include "CycleClock.h"
#include "DspCommon.h"
#include "EnvelopeTable.h"
#include "RingModulator.h"
#include "SpectralDucker.h"
#include "Trigger.h"

namespace kick {

using namespace dsp;

namespace {

constexpr int   kChunk          = SpectralDucker::kMaxChunk;
constexpr float kParamSmoothMs  = 15.f;
constexpr float kFreqSmoothMs   = 20.f;
constexpr float kBypassFadeMs   = 10.f;
constexpr float kMaxEnvSmoothMs = 20.f;
constexpr int   kXoverCtlPeriod = 16;
constexpr float kOutPeakDecayMs = 300.f;

enum class Kind { Volume, Spectral, Ring };

struct Snapshot {
    bool  bypass;
    int   mode;
    float depth;
    bool  freeTime;
    int   rateIndex;
    float lengthMs;
    bool  oneShotParam;
    float rotate01;
    int   gridIndex;
    float swing01;
    float smooth01;
    float midSide;
    float outGainDb;
    bool  delta;
    bool  multi;
    float crossoverHz;
    bool  slope24;
    float loMix;
    float hiMix;
    bool  link;
    int   solo;
    int   trigSource;
    float thresholdDb;
    float retrigMs;
    bool  trigFilter;
    float lowCutHz;
    float highCutHz;
    bool  listen;
    int   midiNote;
    float velocity01;
    float specAttackMs;
    float specReleaseMs;
    float specRangeDb;
    float specSens01;
    bool  specVolume;
    float ringAttackMs;
    float ringReleaseMs;

    // derived
    Kind               kind;
    CycleClock::Source source;
    bool               audioTriggered;
    bool               midiTriggered;
    bool               continuous;
};

inline int toIndex(float v, int lo, int hi) noexcept
{
    return std::clamp(int(std::lround(v)), lo, hi);
}

inline float log2f(float v) noexcept
{
    return std::log2(std::max(v, 1.f));
}

} // namespace

struct Engine::Impl {
    Impl();

    void     prepare(double sampleRate, uint32_t maxBlockSize);
    void     reset();
    Snapshot snapshot() const noexcept;
    void     configure(const Snapshot& s, const TransportInfo& transport, uint32_t frames) noexcept;
    void     processChunk(const float* const* mainIn, const float* const* scIn, float* const* out,
                          uint32_t offset, int n, uint32_t blockFrames, const NoteEvent* notes,
                          uint32_t numNotes, uint32_t& noteIndex) noexcept;
    void     publishBlock() noexcept;

    void recordSample(double phase, float level) noexcept;
    void finishRecording(double phase) noexcept;
    void storeWaveBin(int bin, float main, float ext, float outv, float ringv) noexcept;

    std::atomic<float>  params[kParamCount];
    Bridge              bridge;
    EnvelopeTableBuffer tables[2];

    double   sampleRate   = 48000.0;
    uint32_t maxBlockSize = 512;

    CycleClock      clock;
    TriggerDetector detector;
    SidechainFilter scFilter;
    Crossover       xover;
    Crossover       xoverRef; // reference path when the spectral EQ precedes the split
    SpectralDucker  spectral;
    RingModulator   ring;

    // Per-block configuration
    Snapshot                          cfg {};
    PhaseMap                          phaseMap;
    const EnvelopeTableBuffer::Table* tableA = nullptr;
    const EnvelopeTableBuffer::Table* tableB = nullptr;
    float                             envCoef = 1.f;
    bool                              configured = false;

    // Smoothed controls
    Smoothed depth, loMix, hiMix, outGain, midAmount, sideAmount;
    float    bypassMix  = 0.f;
    float    bypassStep = 0.f;
    float    yA = 1.f, yB = 1.f;
    float    velFactor = 1.f;
    float    xoverLog2 = 0.f, xoverCoef = 1.f;
    int      xoverCtlLeft = 0;
    float    trigLowLog2 = 0.f, trigHighLog2 = 0.f;
    float    lastThresholdDb = 1e9f, lastRetrigMs = -1.f;

    // Display / handshake state
    CycleClock::State lastState = CycleClock::State::Running;
    double            lastPhase = 0.0;
    float             blockMinGain = 1.f;
    int               waveBin = -1;
    float             peakMain = 0.f, peakExt = 0.f, peakOut = 0.f, peakRing = 0.f;
    float             outPeak = 0.f, outPeakDecay = 0.f;
    int               timeSigNum = 4, timeSigDen = 4;
    bool              recording = false;
    int               recBin = 0;
    float             recPeak = 0.f;
    bool              spectralShown = false;

    // Scratch (one chunk)
    float inL[kChunk];
    float inR[kChunk];
    float mid[kChunk];
    float side[kChunk];
    float det[kChunk];
    float wetM[kChunk];
    float wetS[kChunk];
    float refM[kChunk];
    float refS[kChunk];
    float envA[kChunk];
    float envB[kChunk];
    float vel[kChunk];
    float gainLo[kChunk];
    float gainHi[kChunk];
    float ringAmt[kChunk]; // ring mode: effective modulation amount per sample (display)
    float scaleLo[kChunk];
    float scaleHi[kChunk];
    float phaseArr[kChunk];
    uint8_t running[kChunk];
};

// -------------------------------------------------------------------------------------------------

Engine::Impl::Impl()
{
    for (uint32_t i = 0; i < kParamCount; ++i)
        params[i].store(kParams[i].def, std::memory_order_relaxed);
    prepare(48000.0, 512);
}

void Engine::Impl::prepare(double sr, uint32_t maxBlock)
{
    sampleRate   = (std::isfinite(sr) && sr > 0.0) ? std::clamp(sr, 8000.0, 768000.0) : 48000.0;
    maxBlockSize = maxBlock; // informational: processing is chunked internally, any size works

    clock.prepare(sampleRate);
    detector.prepare(sampleRate);
    scFilter.prepare(sampleRate);
    xover.prepare(sampleRate);
    xoverRef.prepare(sampleRate);
    spectral.prepare(sampleRate);
    ring.prepare(sampleRate);

    for (Smoothed* sm : {&depth, &loMix, &hiMix, &outGain, &midAmount, &sideAmount})
        sm->setTime(kParamSmoothMs, sampleRate);
    bypassStep   = float(1.0 / (kBypassFadeMs * 0.001 * sampleRate));
    outPeakDecay = float(std::exp(-1.0 / (kOutPeakDecayMs * 0.001 * sampleRate)));
    xoverCoef  = float(1.0 - std::exp(-double(kXoverCtlPeriod) / (kFreqSmoothMs * 0.001 * sampleRate)));

    for (int b = 0; b < kSpecBands; ++b)
        bridge.specCenterHz[b].store(spectral.centreHz(b), std::memory_order_relaxed);

    lastThresholdDb = 1e9f;
    lastRetrigMs    = -1.f;
    reset();
}

void Engine::Impl::reset()
{
    detector.reset();
    scFilter.reset();
    xover.reset();
    xoverRef.reset();
    spectral.reset();
    ring.reset();
    clock.reset();

    const Snapshot s = snapshot();
    depth.snap(s.depth);
    loMix.snap(s.loMix);
    hiMix.snap(s.hiMix);
    outGain.snap(dbToGain(s.outGainDb));
    midAmount.snap(s.midSide <= 0.f ? 1.f : 1.f - s.midSide * 0.01f);
    sideAmount.snap(s.midSide >= 0.f ? 1.f : 1.f + s.midSide * 0.01f);
    bypassMix = s.bypass ? 1.f : 0.f;
    yA = yB   = 1.f;
    velFactor = 1.f;

    xoverLog2 = log2f(s.crossoverHz);
    xover.setFrequency(s.crossoverHz);
    xoverRef.setFrequency(s.crossoverHz);
    xover.setSlope24(s.slope24);
    xoverRef.setSlope24(s.slope24);
    xoverCtlLeft = 0;
    trigLowLog2  = log2f(s.lowCutHz);
    trigHighLog2 = log2f(s.highCutHz);
    scFilter.setCutoffs(s.lowCutHz, s.highCutHz);

    cfg        = s;
    configured = false;
    waveBin    = -1;
    outPeak    = 0.f;
    lastState  = clock.state();
    lastPhase  = 0.0;
    if (recording) {
        int expected = Bridge::kRecRecording;
        bridge.recState.compare_exchange_strong(expected, Bridge::kRecIdle, std::memory_order_acq_rel);
        recording = false;
    }
}

Snapshot Engine::Impl::snapshot() const noexcept
{
    auto p = [this](ParamId id) { return params[id].load(std::memory_order_relaxed); };
    Snapshot s {};
    s.bypass        = p(kParamBypass) >= 0.5f;
    s.mode          = toIndex(p(kParamMode), 0, kModeCount - 1);
    s.depth         = p(kParamDepth) * 0.01f;
    s.freeTime      = toIndex(p(kParamTimeMode), 0, 1) == kTimeFree;
    s.rateIndex     = toIndex(p(kParamRate), 0, kNumRates - 1);
    s.lengthMs      = p(kParamLengthMs);
    s.oneShotParam  = toIndex(p(kParamPlayMode), 0, 1) == kPlayOneShot;
    s.rotate01      = p(kParamRotate) / 360.f;
    s.gridIndex     = toIndex(p(kParamGrid), 0, kNumGrids - 1);
    s.swing01       = p(kParamSwing) * 0.01f;
    s.smooth01      = p(kParamSmooth) * 0.01f;
    s.midSide       = p(kParamMidSide);
    s.outGainDb     = p(kParamOutGain);
    s.delta         = p(kParamDelta) >= 0.5f;
    s.multi         = p(kParamMulti) >= 0.5f;
    s.crossoverHz   = p(kParamCrossover);
    s.slope24       = toIndex(p(kParamSlope), 0, 1) == kSlope24;
    s.loMix         = p(kParamLoMix) * 0.01f;
    s.hiMix         = p(kParamHiMix) * 0.01f;
    s.link          = p(kParamEnvLink) >= 0.5f;
    s.solo          = toIndex(p(kParamBandSolo), 0, 2);
    s.trigSource    = toIndex(p(kParamTrigSource), 0, 3);
    s.thresholdDb   = p(kParamThreshold);
    s.retrigMs      = p(kParamRetrigMs);
    s.trigFilter    = p(kParamTrigFilter) >= 0.5f;
    s.lowCutHz      = p(kParamTrigLowCut);
    s.highCutHz     = p(kParamTrigHighCut);
    s.listen        = p(kParamTrigListen) >= 0.5f;
    s.midiNote      = toIndex(p(kParamMidiNote), -1, 127);
    s.velocity01    = p(kParamVelocity) * 0.01f;
    s.specAttackMs  = p(kParamSpecAttack);
    s.specReleaseMs = p(kParamSpecRelease);
    s.specRangeDb   = p(kParamSpecRange);
    s.specSens01    = p(kParamSpecSens) * 0.01f;
    s.specVolume    = toIndex(p(kParamSpecTarget), 0, 1) == kSpecTargetVolume;
    s.ringAttackMs  = p(kParamRingAttack);
    s.ringReleaseMs = p(kParamRingRelease);

    int trig = kTrigSync;
    switch (s.mode) {
    case kModeSync:     s.kind = Kind::Volume; trig = kTrigSync; break;
    case kModeMidi:     s.kind = Kind::Volume; trig = kTrigMidi; break;
    case kModeAudio:    s.kind = Kind::Volume; trig = kTrigAudio; break;
    case kModeSpectral: s.kind = Kind::Spectral; trig = s.trigSource; break;
    default:            s.kind = Kind::Ring; trig = s.trigSource; break;
    }
    s.audioTriggered = trig == kTrigAudio;
    s.midiTriggered  = trig == kTrigMidi;
    s.continuous     = trig == kTrigContinuous;
    s.source = (s.audioTriggered || s.midiTriggered) ? CycleClock::Source::Triggered : CycleClock::Source::Sync;
    return s;
}

void Engine::Impl::configure(const Snapshot& s, const TransportInfo& transport, uint32_t frames) noexcept
{
    const Snapshot prev = cfg;
    const bool     first = !configured;
    cfg        = s;
    configured = true;

    CycleClock::Config cc;
    cc.source    = s.source;
    cc.freeTime  = s.freeTime;
    cc.rateBeats = kRates[s.rateIndex].beats;
    cc.lengthMs  = s.lengthMs;
    cc.oneShot   = s.source == CycleClock::Source::Triggered && s.oneShotParam;
    clock.beginBlock(transport, cc);
    timeSigNum = std::clamp(transport.timeSigNum, 1, 64);
    timeSigDen = std::clamp(transport.timeSigDen, 1, 64);

    const double cycleBeats = s.freeTime ? 1.0 : kRates[s.rateIndex].beats;
    phaseMap.rotate01  = s.rotate01;
    phaseMap.divisions = float(std::max(1.0, cycleBeats / kGrids[s.gridIndex].beats));
    phaseMap.swing     = s.swing01;

    tableA = &tables[0].acquire();
    tableB = &tables[1].acquire();

    const double envMs = kMaxEnvSmoothMs * double(s.smooth01) * double(s.smooth01);
    envCoef            = onePoleCoef(envMs, sampleRate);

    depth.target      = s.depth;
    loMix.target      = s.loMix;
    hiMix.target      = s.hiMix;
    outGain.target    = dbToGain(s.outGainDb);
    midAmount.target  = s.midSide <= 0.f ? 1.f : 1.f - s.midSide * 0.01f;
    sideAmount.target = s.midSide >= 0.f ? 1.f : 1.f + s.midSide * 0.01f;

    if (s.thresholdDb != lastThresholdDb) {
        lastThresholdDb = s.thresholdDb;
        detector.setThresholdDb(s.thresholdDb);
    }
    if (s.retrigMs != lastRetrigMs) {
        lastRetrigMs = s.retrigMs;
        detector.setHoldMs(s.retrigMs);
    }

    // Trigger-filter cutoffs glide in log frequency (block rate is fine on the detection path).
    const float fc = 1.f - std::exp(-float(frames) / float(kFreqSmoothMs * 0.001 * sampleRate));
    trigLowLog2 += fc * (log2f(s.lowCutHz) - trigLowLog2);
    trigHighLog2 += fc * (log2f(s.highCutHz) - trigHighLog2);
    scFilter.setCutoffs(std::exp2(trigLowLog2), std::exp2(trigHighLog2));

    xover.setSlope24(s.slope24);
    xoverRef.setSlope24(s.slope24);

    SpectralDucker::Settings ss;
    ss.attackMs    = s.specAttackMs;
    ss.releaseMs   = s.specReleaseMs;
    ss.rangeDb     = s.specRangeDb;
    ss.sensitivity = s.specSens01;
    ss.multi       = s.multi;
    ss.crossoverHz = xover.frequency();
    ss.slope24     = s.slope24;
    spectral.setSettings(ss);

    ring.setTimes(s.ringAttackMs, s.ringReleaseMs);

    // Modules that were idle hold stale state: start them clean when they come back into use.
    if (!first) {
        if (s.kind != prev.kind) {
            if (s.kind == Kind::Spectral) {
                spectral.reset();
                xoverRef.reset();
            }
            if (s.kind == Kind::Ring)
                ring.reset();
        }
        if (s.multi && !prev.multi) {
            xover.reset();
            xoverRef.reset();
        }
        if (s.audioTriggered && !prev.audioTriggered)
            detector.reset();
    }
    blockMinGain = 1.f;
}

// -------------------------------------------------------------------------------------------------

void Engine::Impl::storeWaveBin(int bin, float main, float ext, float outv, float ringv) noexcept
{
    bridge.mainWave[bin].store(main, std::memory_order_relaxed);
    bridge.extWave[bin].store(ext, std::memory_order_relaxed);
    bridge.outWave[bin].store(outv, std::memory_order_relaxed);
    if (cfg.kind == Kind::Ring)
        bridge.ringModWave[bin].store(ringv, std::memory_order_relaxed);
}

void Engine::Impl::recordSample(double phase, float level) noexcept
{
    const int bin = std::min(int(phase * Bridge::kRecBins), Bridge::kRecBins - 1);
    if (bin == recBin) {
        recPeak = std::max(recPeak, level);
        return;
    }
    bridge.recBuf[recBin].store(recPeak, std::memory_order_relaxed);
    for (int b = recBin + 1; b < bin; ++b)
        bridge.recBuf[b].store(level, std::memory_order_relaxed); // bins crossed within one sample
    recBin  = std::max(bin, recBin);
    recPeak = level;
    bridge.recProgress.store(float(phase), std::memory_order_relaxed);
    if (bridge.recState.load(std::memory_order_relaxed) != Bridge::kRecRecording)
        recording = false; // cancelled by the UI
}

void Engine::Impl::finishRecording(double phase) noexcept
{
    // A cycle that ran to its end fills the last bins with the final peak; a cycle cut short by a
    // new trigger leaves the unvisited part silent (nothing was heard there).
    const float tail = phase >= 0.98 ? recPeak : 0.f;
    bridge.recBuf[recBin].store(recPeak, std::memory_order_relaxed);
    for (int b = recBin + 1; b < Bridge::kRecBins; ++b)
        bridge.recBuf[b].store(tail, std::memory_order_relaxed);
    bridge.recProgress.store(1.f, std::memory_order_relaxed);
    int expected = Bridge::kRecRecording;
    bridge.recState.compare_exchange_strong(expected, Bridge::kRecDone, std::memory_order_release,
                                            std::memory_order_relaxed);
    recording = false;
}

void Engine::Impl::processChunk(const float* const* mainIn, const float* const* scIn, float* const* out,
                                uint32_t offset, int n, uint32_t blockFrames, const NoteEvent* notes,
                                uint32_t numNotes, uint32_t& noteIndex) noexcept
{
    const Snapshot& s = cfg;

    // ---- 1. inputs --------------------------------------------------------------------------
    const float* mL = mainIn ? mainIn[0] : nullptr;
    const float* mR = mainIn ? mainIn[1] : nullptr;
    const float* sL = scIn ? scIn[0] : nullptr;
    const float* sR = scIn ? scIn[1] : nullptr;
    for (int i = 0; i < n; ++i) {
        const uint32_t k = offset + uint32_t(i);
        const float l = mL ? sanitise(mL[k]) : 0.f;
        const float r = mR ? sanitise(mR[k]) : l;
        inL[i]  = l;
        inR[i]  = r;
        mid[i]  = 0.5f * (l + r);
        side[i] = 0.5f * (l - r);
        const float a = sL ? sanitise(sL[k]) : 0.f;
        const float b = sR ? sanitise(sR[k]) : a;
        const float mono     = 0.5f * (a + b);
        const float filtered = scFilter.process(mono); // always run: toggling the filter never clicks
        det[i] = s.trigFilter ? filtered : mono;
    }

    // ---- 2. clock, triggers, envelope values, recording -------------------------------------
    const int recState   = bridge.recState.load(std::memory_order_relaxed);
    bool      armed      = recState == Bridge::kRecArmed;
    if (recording && recState != Bridge::kRecRecording)
        recording = false;
    const bool syncSource = s.source == CycleClock::Source::Sync;

    for (int i = 0; i < n; ++i) {
        const uint32_t k = offset + uint32_t(i);
        bool trigger = detector.tick(det[i]) && s.audioTriggered;
        int  noteVel = -1;
        while (noteIndex < numNotes && std::min(notes[noteIndex].frame, blockFrames - 1) <= k) {
            const NoteEvent& ev = notes[noteIndex++];
            if (ev.on && ev.velocity > 0) { // activity / MIDI Learn: every note-on, any mode, unfiltered
                bridge.lastNote.store(int(ev.note), std::memory_order_relaxed);
                bridge.lastVelocity.store(int(ev.velocity), std::memory_order_relaxed);
                bridge.noteCount.fetch_add(1, std::memory_order_relaxed);
            }
            if (s.midiTriggered && ev.on && ev.velocity > 0 && (s.midiNote < 0 || ev.note == s.midiNote))
                noteVel = std::max(noteVel, int(ev.velocity));
        }
        if (noteVel > 0)
            trigger = true;
        if (trigger) {
            velFactor = noteVel > 0 ? 1.f - s.velocity01 * (1.f - float(noteVel) / 127.f) : 1.f;
            bridge.triggerCount.fetch_add(1, std::memory_order_relaxed);
        }

        const CycleClock::Tick tk = clock.advance(trigger);
        float ra, rb;
        if (s.continuous) {
            ra = rb = 0.f;
        } else if (tk.state == CycleClock::State::Waiting) {
            ra = rb = 1.f;
        } else if (tk.state == CycleClock::State::Holding) {
            ra = EnvelopeTableBuffer::endValue(*tableA);
            rb = s.link ? ra : EnvelopeTableBuffer::endValue(*tableB);
        } else {
            const float q = phaseMap.toNode(float(tk.phase));
            ra = EnvelopeTableBuffer::lookup(*tableA, q);
            rb = s.link ? ra : EnvelopeTableBuffer::lookup(*tableB, q);
        }
        yA += envCoef * (ra - yA);
        yB += envCoef * (rb - yB);
        envA[i]     = yA;
        envB[i]     = yB;
        vel[i]      = velFactor;
        phaseArr[i] = float(tk.phase);
        running[i]  = tk.state == CycleClock::State::Running ? 1 : 0;

        const float level = std::fabs(det[i]);
        if (recording) {
            if (tk.wrapped || tk.triggered || tk.ended)
                finishRecording(lastPhase);
            else
                recordSample(tk.phase, level);
        }
        const bool cycleStart = syncSource ? tk.wrapped : tk.triggered;
        if (armed && !recording && cycleStart) {
            armed = false;
            int expected = Bridge::kRecArmed;
            if (bridge.recState.compare_exchange_strong(expected, Bridge::kRecRecording,
                                                        std::memory_order_acq_rel)) {
                recording = true;
                recBin    = 0;
                recPeak   = level;
                bridge.recProgress.store(0.f, std::memory_order_relaxed);
            }
        }
        lastPhase = tk.phase;
        lastState = tk.state;
    }

    // ---- 3. per-sample gains / spectral scales ----------------------------------------------
    const bool multi = s.multi;
    float      minGain = 1.f;
    switch (s.kind) {
    case Kind::Volume:
        for (int i = 0; i < n; ++i) {
            const float d  = depth.next() * vel[i];
            const float lm = loMix.next();
            const float hm = hiMix.next();
            if (multi) {
                gainLo[i] = 1.f - d * lm * (1.f - envA[i]);
                gainHi[i] = 1.f - d * hm * (1.f - envB[i]);
                minGain   = std::min(minGain, std::min(gainLo[i], gainHi[i]));
            } else {
                gainLo[i] = gainHi[i] = 1.f - d * (1.f - envA[i]);
                minGain   = std::min(minGain, gainLo[i]);
            }
        }
        break;
    case Kind::Ring:
        for (int i = 0; i < n; ++i) {
            const float m  = ring.tick(det[i]);
            const float d  = depth.next() * vel[i] * m;
            const float lm = loMix.next();
            const float hm = hiMix.next();
            if (multi) {
                gainLo[i] = 1.f - d * lm * (1.f - envA[i]);
                gainHi[i] = 1.f - d * hm * (1.f - envB[i]);
            } else {
                gainLo[i] = gainHi[i] = 1.f - d * (1.f - envA[i]);
            }
            const float g = std::min(gainLo[i], gainHi[i]);
            ringAmt[i] = 1.f - g;
            minGain    = std::min(minGain, g);
        }
        break;
    case Kind::Spectral: {
        const bool volumeStage = s.specVolume && !s.continuous;
        for (int i = 0; i < n; ++i) {
            const float d   = depth.next() * vel[i];
            const float lmS = loMix.next();
            const float hmS = hiMix.next();
            const float lm  = multi ? lmS : 1.f; // lo_mix / hi_mix only exist in multiband
            const float hm  = multi ? hmS : 1.f;
            if (s.specVolume) {
                scaleLo[i] = d * lm;
                scaleHi[i] = d * hm;
            } else {
                scaleLo[i] = d * lm * (1.f - envA[i]);
                scaleHi[i] = d * hm * (1.f - (multi ? envB[i] : envA[i]));
            }
            if (volumeStage) {
                gainLo[i] = 1.f - d * lm * (1.f - envA[i]);
                gainHi[i] = multi ? 1.f - d * hm * (1.f - envB[i]) : gainLo[i];
                minGain   = std::min(minGain, std::min(gainLo[i], gainHi[i]));
            } else {
                gainLo[i] = gainHi[i] = 1.f;
            }
        }
        break;
    }
    }
    blockMinGain = std::min(blockMinGain, minGain);

    // ---- 4. processing ------------------------------------------------------------------------
    for (int i = 0; i < n; ++i) {
        wetM[i] = mid[i];
        wetS[i] = side[i];
    }
    if (s.kind == Kind::Spectral)
        spectral.process(det, mid, scaleLo, scaleHi, wetM, wetS, n);

    if (multi) {
        const bool  refFromDry = s.kind == Kind::Spectral;
        const float target     = log2f(s.crossoverHz);
        for (int i = 0; i < n; ++i) {
            if (--xoverCtlLeft <= 0) {
                xoverCtlLeft = kXoverCtlPeriod;
                if (xoverLog2 != target) {
                    xoverLog2 += xoverCoef * (target - xoverLog2);
                    if (std::fabs(target - xoverLog2) < 1e-4f)
                        xoverLog2 = target;
                    const float hz = std::exp2(xoverLog2);
                    xover.setFrequency(hz);
                    xoverRef.setFrequency(hz);
                }
            }
            float* wet[2] = {wetM, wetS};
            float* ref[2] = {refM, refS};
            const float* dry[2] = {mid, side};
            for (int ch = 0; ch < 2; ++ch) {
                float lo, hi;
                xover.process(ch, wet[ch][i], lo, hi);
                float rlo = lo, rhi = hi;
                if (refFromDry)
                    xoverRef.process(ch, dry[ch][i], rlo, rhi);
                switch (s.solo) {
                case kSoloLow:  wet[ch][i] = gainLo[i] * lo; ref[ch][i] = rlo; break;
                case kSoloHigh: wet[ch][i] = gainHi[i] * hi; ref[ch][i] = rhi; break;
                default:        wet[ch][i] = gainLo[i] * lo + gainHi[i] * hi; ref[ch][i] = rlo + rhi; break;
                }
            }
        }
    } else {
        for (int i = 0; i < n; ++i) {
            wetM[i] *= gainLo[i];
            wetS[i] *= gainLo[i];
            refM[i] = mid[i];
            refS[i] = side[i];
        }
    }

    // ---- 5. output ------------------------------------------------------------------------------
    float* oL = out[0];
    float* oR = out[1];
    const float bypassTarget = s.bypass ? 1.f : 0.f;
    for (int i = 0; i < n; ++i) {
        const float aM = midAmount.next();
        const float aS = sideAmount.next();
        const float og = outGain.next();
        const float mo = refM[i] + aM * (wetM[i] - refM[i]);
        const float so = refS[i] + aS * (wetS[i] - refS[i]);
        float l = mo + so;
        float r = mo - so;
        if (s.delta) {
            l = (refM[i] + refS[i]) - l;
            r = (refM[i] - refS[i]) - r;
        }
        l *= og;
        r *= og;
        if (s.listen)
            l = r = det[i];

        if (bypassMix != bypassTarget) {
            bypassMix = bypassTarget > bypassMix ? std::min(bypassMix + bypassStep, 1.f)
                                                 : std::max(bypassMix - bypassStep, 0.f);
        }
        if (bypassMix >= 1.f) {
            l = inL[i]; // bit-exact dry once the fade is complete
            r = inR[i];
        } else if (bypassMix > 0.f) {
            l += bypassMix * (inL[i] - l);
            r += bypassMix * (inR[i] - r);
        }

        const uint32_t k = offset + uint32_t(i);
        if (oL)
            oL[k] = l;
        if (oR)
            oR[k] = r;

        const float ao = std::max(std::fabs(l), std::fabs(r));
        outPeak = std::max(ao, outPeak * outPeakDecay);

        // Waveform bins, indexed by timeline phase while the playhead moves.
        if (!running[i]) {
            if (waveBin >= 0)
                storeWaveBin(waveBin, peakMain, peakExt, peakOut, peakRing);
            waveBin = -1;
            continue;
        }
        const float am  = std::max(std::fabs(inL[i]), std::fabs(inR[i]));
        const float ae  = std::fabs(det[i]);
        const float ar  = s.kind == Kind::Ring ? ringAmt[i] : 0.f;
        const int   bin = std::min(int(phaseArr[i] * float(Bridge::kWaveBins)), Bridge::kWaveBins - 1);
        if (bin != waveBin) {
            if (waveBin >= 0) {
                storeWaveBin(waveBin, peakMain, peakExt, peakOut, peakRing);
                for (int b = waveBin + 1; b < bin; ++b)
                    storeWaveBin(b, am, ae, ao, ar); // bins crossed within one sample (very short cycles)
            }
            waveBin  = bin;
            peakMain = am;
            peakExt  = ae;
            peakOut  = ao;
            peakRing = ar;
        } else {
            peakMain = std::max(peakMain, am);
            peakExt  = std::max(peakExt, ae);
            peakOut  = std::max(peakOut, ao);
            peakRing = std::max(peakRing, ar);
        }
    }
    if (waveBin >= 0)
        storeWaveBin(waveBin, peakMain, peakExt, peakOut, peakRing);
}

void Engine::Impl::publishBlock() noexcept
{
    const Snapshot& s = cfg;
    const bool waiting = lastState == CycleClock::State::Waiting;
    bridge.phase.store(waiting ? 0.f : float(lastPhase), std::memory_order_relaxed);
    bridge.valueA.store(yA, std::memory_order_relaxed);
    bridge.valueB.store(s.link ? yA : yB, std::memory_order_relaxed);
    bridge.envelopeActive.store(lastState == CycleClock::State::Running ? 1 : 0, std::memory_order_relaxed);
    bridge.bpm.store(float(clock.bpm()), std::memory_order_relaxed);
    bridge.hostPlaying.store(clock.hostPlaying() ? 1 : 0, std::memory_order_relaxed);
    bridge.cycleSeconds.store(float(clock.cycleSeconds()), std::memory_order_relaxed);
    bridge.timeSigNum.store(timeSigNum, std::memory_order_relaxed);
    bridge.timeSigDen.store(timeSigDen, std::memory_order_relaxed);
    bridge.scLevelDb.store(gainToDb(detector.level(), -120.f), std::memory_order_relaxed);
    bridge.outPeakDb.store(gainToDb(outPeak, -120.f), std::memory_order_relaxed);

    float grDb = gainToDb(std::max(blockMinGain, 0.f), -96.f);
    if (s.kind == Kind::Spectral)
        grDb += spectral.deepestCutDb();
    if (bypassMix >= 1.f)
        grDb = 0.f;
    bridge.gainReductionDb.store(std::clamp(grDb, -96.f, 0.f), std::memory_order_relaxed);

    if (s.kind == Kind::Spectral) {
        for (int b = 0; b < kSpecBands; ++b) {
            bridge.specCutDb[b].store(spectral.cutDb(b), std::memory_order_relaxed);
            bridge.specScDb[b].store(spectral.sidechainDb(b), std::memory_order_relaxed);
        }
        spectralShown = true;
    } else if (spectralShown) {
        for (int b = 0; b < kSpecBands; ++b) {
            bridge.specCutDb[b].store(0.f, std::memory_order_relaxed);
            bridge.specScDb[b].store(-120.f, std::memory_order_relaxed);
        }
        spectralShown = false;
    }
}

// -------------------------------------------------------------------------------------------------

Engine::Engine()
    : impl_(std::make_unique<Impl>())
{
}

Engine::~Engine() = default;

void Engine::prepare(double sampleRate, uint32_t maxBlockSize)
{
    impl_->prepare(sampleRate, maxBlockSize);
}

void Engine::reset()
{
    impl_->reset();
}

void Engine::setParameter(uint32_t id, float value) noexcept
{
    if (id >= kParamCount || !std::isfinite(value))
        return;
    const ParamInfo& info = kParams[id];
    impl_->params[id].store(std::clamp(value, info.min, info.max), std::memory_order_relaxed);
}

float Engine::getParameter(uint32_t id) const noexcept
{
    if (id >= kParamCount)
        return 0.f;
    return impl_->params[id].load(std::memory_order_relaxed);
}

void Engine::setEnvelope(int band, const Envelope& env)
{
    if (band == 0 || band == 1)
        impl_->tables[band].publish(env);
}

void Engine::process(const float* const* mainIn, const float* const* scIn, float* const* out,
                     uint32_t frames, const TransportInfo& transport, const NoteEvent* notes,
                     uint32_t numNotes) noexcept
{
    if (!out || frames == 0)
        return;
    ScopedDenormalGuard denormalGuard;
    Impl& d = *impl_;
    if (!notes)
        numNotes = 0;

    d.configure(d.snapshot(), transport, frames);
    uint32_t noteIndex = 0;
    for (uint32_t offset = 0; offset < frames;) {
        const int n = int(std::min<uint32_t>(frames - offset, kChunk));
        d.processChunk(mainIn, scIn, out, offset, n, frames, notes, numNotes, noteIndex);
        offset += uint32_t(n);
    }
    d.publishBlock();
}

Bridge& Engine::bridge() noexcept
{
    return impl_->bridge;
}

const Bridge& Engine::bridge() const noexcept
{
    return impl_->bridge;
}

} // namespace kick
