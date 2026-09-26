// Kickarse — audio engine. Framework independent (no DPF includes).
// Contract header: public API is fixed. All state lives in Engine::Impl (Engine.cpp).
#pragma once

#include <cstdint>
#include <memory>

#include "../shared/Bridge.h"
#include "../shared/Envelope.h"
#include "../shared/Params.h"

namespace kick {

struct TransportInfo {
    bool   valid   = false;  // host supplied tempo/position for this block
    bool   playing = false;
    double bpm     = 120.0;
    double ppq     = 0.0;    // position of the block's first sample, in quarter notes since song start
    int    timeSigNum = 4;   // host time signature; only used for display (Bridge)
    int    timeSigDen = 4;
};

struct NoteEvent {
    uint32_t frame;          // offset inside the block
    uint8_t  note;
    uint8_t  velocity;       // 0 for note-off
    bool     on;
};

class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Non-realtime; may allocate. Called before processing and on sample-rate / block-size change.
    void prepare(double sampleRate, uint32_t maxBlockSize);
    // Non-realtime. Clears filter state and restarts the envelope.
    void reset();

    // Any thread; realtime-safe and lock-free. Plain units from Params.h; clamped internally.
    void  setParameter(uint32_t id, float value) noexcept;
    float getParameter(uint32_t id) const noexcept;

    // Non-realtime thread (UI / host main thread). Publishes an envelope to the audio thread
    // without locks and without the audio thread ever freeing memory. band 0 = A (main/low), 1 = B (high).
    void setEnvelope(int band, const Envelope& env);

    // Audio thread. mainIn / scIn / out are 2 channels each. scIn is all zeros when no sidechain
    // is connected. out may alias mainIn (in-place). notes are sorted by frame.
    void process(const float* const* mainIn, const float* const* scIn, float* const* out,
                 uint32_t frames, const TransportInfo& transport,
                 const NoteEvent* notes, uint32_t numNotes) noexcept;

    Bridge&       bridge() noexcept;
    const Bridge& bridge() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kick
