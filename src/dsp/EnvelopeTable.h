// Kickarse — envelope lookup tables and their lock-free hand-over to the audio thread.
//
// setEnvelope() renders the envelope (non-RT) into the writer's private table and swaps it into
// the shared "middle" slot of a triple buffer; the audio thread swaps the middle slot into its own
// "front" slot when it sees the dirty bit. All three tables are allocated once in the constructor,
// so nothing is ever allocated or freed while the engine runs, and the audio thread never waits.
// Writers are serialised by a mutex that only non-RT threads touch.
//
// Table format: kSize + 1 points, linearly interpolated, plus the vertical steps of the envelope.
// A table cell that contains a step interpolates up to the step's left value and restarts from its
// right value, so steps stay exactly vertical at any cycle length (plain interpolation would turn
// them into one-cell ramps, i.e. up to 4 ms at a 4-bar cycle).
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "../shared/Envelope.h"

namespace kick::dsp {

class EnvelopeTableBuffer {
public:
    static constexpr int kSize = 4096; // cells per cycle

    struct Step {
        float at;   // position inside its cell, (0, 1]
        float pre;  // value approached from the left
        float post; // value from the step on
    };

    struct Table {
        float   v[kSize + 1];           // v[i] = envelope(i / kSize); v[kSize] = value at the cycle end
        int16_t cellStep[kSize];        // index into steps, or -1
        Step    steps[Envelope::kMaxNodes];
    };

    EnvelopeTableBuffer();

    // Non-RT. Thread-safe with respect to other writers and to the audio thread.
    void publish(const Envelope& env);

    // Audio thread only. Returns the most recently published table.
    const Table& acquire() noexcept;

    // q: node-space phase in [0,1).
    static inline float lookup(const Table& t, float q) noexcept
    {
        const float pos = q * float(kSize);
        if (!(pos >= 0.f))
            return t.v[0];
        if (pos >= float(kSize))
            return t.v[kSize];
        const int   i    = int(pos);
        const float frac = pos - float(i);
        const int   s    = t.cellStep[i];
        if (s < 0)
            return t.v[i] + frac * (t.v[i + 1] - t.v[i]);
        const Step& st = t.steps[s];
        if (frac < st.at)
            return t.v[i] + (st.pre - t.v[i]) * (frac / st.at);
        return st.post + (t.v[i + 1] - st.post) * ((frac - st.at) / (1.f - st.at));
    }

    // Value approached at the end of the cycle: the last node's value (one-shot hold value).
    static inline float endValue(const Table& t) noexcept { return t.v[kSize]; }

    static void render(const Envelope& env, Table& out) noexcept;

private:
    static constexpr uint32_t kDirty = 4u;

    std::unique_ptr<Table[]> tables_;
    std::atomic<uint32_t>    middle_ {1};
    uint32_t                 back_  = 0; // writer-owned
    uint32_t                 front_ = 2; // reader-owned
    std::mutex               writerMutex_;
};

} // namespace kick::dsp
