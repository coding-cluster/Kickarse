// Kickarse — editor view mapping: node x ↔ timeline, grid and snapping.
//
// Built on the shared PhaseMap (Envelope.h) so the editor draws exactly what the engine plays.
// PhaseMap is  timeline = frac(warp(x) + rotate),  x = unwarp(frac(timeline − rotate)).
// This class adds the *seam-relative* position  s = warp(x) ∈ [0,1]  (swing applied, rotation not),
// which is continuous over the whole node range: s(0) = 0, s(1) = 1. The rotation "seam" is where
// the envelope's endpoints x = 0 and x = 1 meet on screen (timeline = rotate). Drags and spans are
// computed in s or node x so nothing ever wraps across that seam.
//
// Grid: lines at node x = k / divisions (k = 0 … gridCount()−1), shown at toTimeline(k / divisions),
// so swing visibly shifts every second line (DESIGN.md §7.2). The cycle end x = 1 is also a snap
// target. Snapping picks the grid line nearest in *timeline* distance, i.e. what the user sees.
// Swing is piecewise linear in node space with its kinks on grid lines, which is why the editor's
// rigid moves (drag, nudge, Quick Shift) shift nodes in node x: grid-aligned nodes stay aligned.
#pragma once

#include <vector>

#include "EditorTypes.h"

namespace kick::editor {

// The engine's mapping for these parameters (mirrors Engine::Impl::configure):
// rotate01 = rotateDeg / 360, divisions = max(1, cycleBeats / gridBeats) with cycleBeats = rate beats
// (Note mode) or 1 (ms mode), swing = swingPercent / 100.
PhaseMap makePhaseMap(const TimingParams& t) noexcept;
double   cycleBeats(const TimingParams& t) noexcept;

class TimelineMap {
public:
    TimelineMap() noexcept;
    explicit TimelineMap(const PhaseMap& map) noexcept;

    const PhaseMap& phaseMap() const noexcept { return map_; }
    float rotation() const noexcept { return rot_; }         // [0,1)
    float divisions() const noexcept { return div_; }        // >= 1
    bool  swingActive() const noexcept { return swingActive_; }

    // node x ↔ seam-relative s. Both monotonic, 0 ↔ 0 and 1 ↔ 1, exact when swing is off.
    float toSeam(float nodeX) const noexcept;
    float fromSeam(float seam) const noexcept;

    // seam-relative s ↔ timeline. seamToTimeline(1) is 1 when rotation is 0 (the last node is drawn
    // at the right edge, DESIGN.md §7.1), otherwise it equals seamToTimeline(0) = rotation.
    float seamToTimeline(float seam) const noexcept;
    float timelineToSeam(float timeline) const noexcept;  // [0,1); the seam itself maps to 0

    // Where a node is drawn: PhaseMap::toTimeline, except x = 1 lands on the right edge when the
    // rotation is 0. toNode is exactly PhaseMap::toNode (the engine's lookup), in [0,1).
    float toTimeline(float nodeX) const noexcept;
    float toNode(float timeline) const noexcept;

    // Grid lines at node x = k / divisions for k = 0 … gridCount()−1 (all < 1).
    int   gridCount() const noexcept;
    float gridNode(int k) const noexcept;
    float cellWidth() const noexcept { return 1.f / div_; }   // in node x

    // Nearest grid line (or the cycle end, x = 1) to nodeX, measured in timeline distance.
    float snapNode(float nodeX) const noexcept;
    // Nearest multiple of `step` (node x) to delta: used for Quick Shift's ¼-cell snapping.
    static float snapDelta(float delta, float step) noexcept;

    // Node x of the swing kinks (grid lines inside complete swing pairs) strictly inside (x0, x1).
    // Empty when swing is off. A straight line in the timeline stays straight in node space only
    // between kinks, so the line tool inserts nodes there.
    void kinksBetween(float x0, float x1, std::vector<float>& out) const;

private:
    PhaseMap map_;
    PhaseMap unrotated_;  // same map with rotate01 = 0: its toTimeline is the swing warp
    float    rot_         = 0.f;
    float    div_         = 4.f;
    float    swingEnd_    = 0.f;  // node x where the last complete swing pair ends
    bool     swingActive_ = false;
};

} // namespace kick::editor
