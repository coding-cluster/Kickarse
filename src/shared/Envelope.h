// Kickarse — envelope model shared by the DSP (rendering lookup tables) and the UI (drawing/editing).
// Contract header: the API below is fixed; implementations live in Envelope.cpp.
// Additive changes are allowed (new methods), signature changes are not.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kick {

enum EnvNodeFlags : uint32_t {
    kNodeQuickShift = 1u << 0,  // member of the Quick Shift group
};

struct EnvNode {
    float    x = 0.f;        // [0,1] position in the cycle (node space, i.e. before swing/rotate)
    float    y = 1.f;        // [0,1] gain value: 1 = unity, 0 = full duck
    float    tension = 0.f;  // [-1,1] curvature of the segment from this node to the next; 0 = linear
    uint32_t flags = 0;      // EnvNodeFlags
};

class Envelope {
public:
    static constexpr int kMaxNodes = 128;

    Envelope();                         // default shape: a classic sidechain duck (0 → recover)
    static Envelope flat();             // y = 1 everywhere (no ducking)

    int            size() const noexcept;
    const EnvNode& node(int i) const noexcept;
    EnvNode&       node(int i) noexcept;          // after direct edits call normalise()
    int            insert(const EnvNode& n);      // keeps order, returns new index or -1 when full
    void           remove(int i);                 // first and last node cannot be removed
    void           normalise();                   // stable-sort by x, clamp ranges, first.x = 0, last.x = 1

    // Evaluation in node space. phase wraps into [0,1].
    // Two nodes with equal x form a vertical step (value jumps at that x).
    float evaluate(float phase) const noexcept;
    // Left-continuous, non-wrapping variant for drawing: phase clamped to [0,1], evaluateLeft(1) is
    // the last node's value, and at a vertical step it returns the value *before* the step.
    float evaluateLeft(float phase) const noexcept;
    void  render(float* out, int n) const noexcept;   // out[i] = evaluate(i / float(n))

    // Segment curve shared by DSP & UI. u in [0,1], tension in [-1,1] → [0,1], shape(0)=0, shape(1)=1,
    // monotonic, shape(u, 0) = u, shape(u, -t) mirrors shape(u, t).
    static float shape(float u, float tension) noexcept;

    // ASCII, locale-independent. Round-trips node data to at least 1e-5.
    std::string serialize() const;
    bool        deserialize(std::string_view text);   // false and *this untouched on malformed input

    // Fit a compact node list to n samples of y in [0,1] spread uniformly over one cycle
    // (used for audio→envelope recording and the pencil tool). tolerance is max |error| in y.
    static Envelope fromSamples(const float* y, int n, float tolerance = 0.01f);

private:
    EnvNode nodes_[kMaxNodes];
    int     count_ = 0;
};

// Maps between the *timeline* (what is heard, what the editor displays, Bridge::phase) and
// *node space* (where nodes are stored). Shared so DSP playback and UI drawing always agree.
//   timeline → node:  q = swingUnwarp(frac(p - rotate01))
//   node → timeline:  p = frac(swingWarp(q) + rotate01)
// divisions = grid cells per cycle (>= 1; = cycleBeats / gridBeats, with cycleBeats = 1 in free-time mode).
// swing in [0,1]: 0 = straight, 1 = MPC 75 % (first cell of each pair lasts 3/4 of the pair).
struct PhaseMap {
    float rotate01  = 0.f;   // Rotate parameter / 360
    float divisions = 4.f;
    float swing     = 0.f;

    float toNode(float timelinePhase) const noexcept;
    float toTimeline(float nodePhase) const noexcept;
};

} // namespace kick
