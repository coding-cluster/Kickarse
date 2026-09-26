// Kickarse — lock-free, allocation-free data channel between the audio thread and the UI.
// The UI reaches it through DPF direct access: Engine::bridge() on the plugin instance.
// Each field has exactly one writer (marked DSP→UI or UI→DSP). All loads/stores use
// std::memory_order_relaxed unless a handshake says otherwise: a torn *frame* of display data is
// acceptable, a blocked audio thread is not.
#pragma once

#include <atomic>
#include <cstdint>

#include "Params.h"

namespace kick {

struct Bridge {
    static constexpr int kWaveBins = 512;   // one envelope cycle split into bins (timeline phase)
    static constexpr int kRecBins  = 1024;  // resolution of a recorded sidechain contour

    // ---- DSP → UI : playhead / transport ------------------------------------------------------
    std::atomic<float>    phase {0.f};          // current timeline phase [0,1) (what is heard, incl. rotate/swing)
    std::atomic<float>    valueA {1.f};         // current envelope value y of band A [0,1] (pre-depth)
    std::atomic<float>    valueB {1.f};         // current envelope value y of band B [0,1] (pre-depth)
    std::atomic<float>    gainReductionDb {0.f};// current overall gain reduction, dB (<= 0), for a meter
    std::atomic<uint32_t> triggerCount {0};     // incremented on every trigger (Audio/MIDI/one-shot restart)
    std::atomic<int>      envelopeActive {1};   // 0 while a one-shot has finished / waiting for a trigger
    std::atomic<float>    bpm {120.f};
    std::atomic<int>      hostPlaying {0};
    std::atomic<float>    cycleSeconds {0.5f};  // current cycle length in seconds
    std::atomic<int>      timeSigNum {4};       // host time signature (4/4 when the host gives none)
    std::atomic<int>      timeSigDen {4};

    // ---- DSP → UI : detection & activity ------------------------------------------------------
    std::atomic<float>    scLevelDb {-120.f};   // sidechain detector level after the trigger filter, dBFS (threshold meter)
    std::atomic<float>    outPeakDb {-120.f};   // output peak with ~300 ms decay, dBFS
    std::atomic<int>      lastNote {-1};        // last MIDI note-on received (any note, before the note filter), -1 = none yet
    std::atomic<int>      lastVelocity {0};
    std::atomic<uint32_t> noteCount {0};        // incremented on every MIDI note-on (activity LED, MIDI Learn)

    // ---- DSP → UI : waveforms indexed by timeline phase bin -----------------------------------
    // Peak |x| (linear, max of L/R) while the playhead was inside that bin on the most recent pass.
    std::atomic<float> mainWave[kWaveBins] {};  // main input, before processing
    std::atomic<float> extWave[kWaveBins] {};   // sidechain input (after the trigger filter when enabled)
    std::atomic<float> outWave[kWaveBins] {};   // output, after processing (the removed signal while Delta is on)
    std::atomic<float> ringModWave[kWaveBins] {}; // Ring mode: effective modulation depth·(1−y)·m in [0,1] per bin

    // ---- DSP → UI : spectral mode --------------------------------------------------------------
    std::atomic<float> specCutDb[kSpecBands] {};    // current cut per band, dB (<= 0)
    std::atomic<float> specScDb[kSpecBands] {};     // sidechain level per band, dBFS
    std::atomic<float> specCenterHz[kSpecBands] {}; // band centre frequencies (written once in prepare)

    // ---- Audio → envelope recording (handshake) -----------------------------------------------
    //  1. UI stores recState = kRecArmed.
    //  2. DSP, at the next cycle start (Sync) or next trigger (Audio/MIDI), stores kRecRecording and
    //     fills recBuf over exactly one cycle with peak |sidechain| per bin (linear, unnormalised),
    //     updating recProgress in [0,1].
    //  3. DSP stores kRecDone with memory_order_release after the last recBuf write.
    //  4. UI loads kRecDone with memory_order_acquire, reads recBuf, builds an Envelope and stores kRecIdle.
    //  The UI may cancel at any time by storing kRecIdle; the DSP then stops writing.
    enum RecState : int { kRecIdle = 0, kRecArmed, kRecRecording, kRecDone };
    std::atomic<int>   recState {kRecIdle};
    std::atomic<float> recProgress {0.f};
    std::atomic<float> recBuf[kRecBins] {};
};

} // namespace kick
