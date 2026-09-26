// Kickarse — cycle clock: produces the timeline phase of the envelope cycle, sample by sample.
//
//  Sync source, tempo time : phase = frac(ppq / rateBeats), ppq in double from the host while it
//                            plays; while stopped (or without host info) the clock free-runs at the
//                            last known tempo from the last position, so edits stay audible.
//  Sync source, free time  : free-running loop of lengthMs; re-aligned to the song position when the
//                            transport starts, continuous afterwards (tempo changes never jump).
//  Triggered source        : a trigger restarts at phase 0. Loop keeps cycling, one-shot plays a
//                            single cycle then holds. Before the first trigger the clock waits.
#pragma once

#include <cstdint>

#include "Engine.h"

namespace kick::dsp {

class CycleClock {
public:
    enum class Source { Sync, Triggered };
    enum class State : uint8_t { Waiting, Running, Holding };

    struct Config {
        Source source    = Source::Sync;
        bool   freeTime  = false;   // time_mode = ms
        double rateBeats = 1.0;     // cycle length in quarter notes (tempo time)
        double lengthMs  = 250.0;   // cycle length (free time)
        bool   oneShot   = false;   // triggered sources only
    };

    struct Tick {
        double phase;       // timeline phase [0,1)
        State  state;
        bool   triggered;   // a trigger restarted the cycle on this sample
        bool   wrapped;     // the cycle wrapped around on this sample (loop / sync)
        bool   ended;       // a one-shot finished on this sample
    };

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Once per host block, before advancing. Source changes reset the clock.
    void beginBlock(const TransportInfo& transport, const Config& config) noexcept;

    Tick advance(bool trigger) noexcept;

    double bpm() const noexcept { return bpm_; }
    bool   hostPlaying() const noexcept { return playing_; }
    double cycleSeconds() const noexcept;
    State  state() const noexcept { return state_; }

private:
    double sampleRate_ = 48000.0;
    Config config_;
    bool   configured_ = false;
    double bpm_        = 120.0;
    bool   playing_    = false;
    double ppq_        = 0.0;     // sync position of the next sample
    double dppq_       = 0.0;     // quarter notes per sample
    double phase_      = 0.0;
    double lastPhase_  = 0.0;
    double dphase_     = 0.0;     // phase per sample (free time / triggered)
    State  state_      = State::Running;
};

} // namespace kick::dsp
