// Kickarse — headless envelope editor: shared value types.
//
// Coordinate spaces used throughout src/editor (see also TimelineMap.h):
//   node x      where nodes are stored (EnvNode::x), [0,1]. Envelope endpoints sit at 0 and 1.
//   timeline    what is heard and what the editor displays (Bridge::phase), [0,1] left to right.
//               timeline = PhaseMap::toTimeline(node x): swing warps it, rotate shifts it cyclically.
//   value       envelope y, [0,1], 1 = unity gain at the top of the plot.
//   pixels      only in hit testing, through ViewTransform (the view owns the layout).
// Every public position in EditorModel is (timeline, value) unless the name says "node".
#pragma once

#include <bitset>
#include <cstdint>

#include "../shared/Envelope.h"
#include "../shared/Params.h"

namespace kick::editor {

// A = envA (main / low band), B = envB (high band).
enum class Band : int { A = 0, B = 1 };
inline constexpr int kNumBands = 2;
constexpr int  bandIndex(Band b) noexcept { return b == Band::B ? 1 : 0; }
constexpr Band otherBand(Band b) noexcept { return b == Band::B ? Band::A : Band::B; }

// One bit per node index of an envelope (selection, marquee results).
using NodeMask = std::bitset<Envelope::kMaxNodes>;

// (timeline, value) unless documented otherwise.
struct Vec2 {
    float x = 0.f;
    float y = 0.f;
};

// Maps (timeline, value) to pixels for the plot area or the Quick Shift lane. Any unit works as
// long as hit-test tolerances use the same one (DESIGN.md uses 1x logical px under nvgScale).
struct ViewTransform {
    float left   = 0.f;
    float top    = 0.f;
    float width  = 1.f;
    float height = 1.f;

    float toPixelX(float timeline) const noexcept { return left + timeline * width; }
    float toPixelY(float value) const noexcept { return top + (1.f - value) * height; }
    float toTimeline(float px) const noexcept { return width > 0.f ? (px - left) / width : 0.f; }
    float toValue(float py) const noexcept { return height > 0.f ? 1.f - (py - top) / height : 0.f; }
    bool  contains(float px, float py, float margin = 0.f) const noexcept
    {
        return px >= left - margin && px <= left + width + margin && py >= top - margin
            && py <= top + height + margin;
    }
};

// The parameters that shape the editor's view mapping, in plain units (as in Params.h). Build it
// from the host parameter values; EditorModel::setTiming() turns it into the same PhaseMap the
// engine uses (Engine::Impl::configure), so the display equals what is heard.
struct TimingParams {
    float rotateDeg    = 0.f;                // kParamRotate, degrees (any value; wraps)
    int   gridIndex    = kDefaultGridIndex;  // kParamGrid (index into kGrids)
    float swingPercent = 0.f;                // kParamSwing, 0..100
    int   timeMode     = kTimeSync;          // kParamTimeMode
    int   rateIndex    = kDefaultRateIndex;  // kParamRate (index into kRates)
    float lengthMs     = 250.f;              // kParamLengthMs (only used for readout helpers)

    // Reads the five timing parameters from an array of plain values indexed by ParamId.
    static TimingParams fromParamValues(const float* values) noexcept;
};

struct SnapSettings {
    bool  enabled   = true;    // the toolbar "Snap" toggle: x snaps to the (swung) grid
    bool  snapY     = false;   // also snap y to 0 / 0.25 / 0.5 / 0.75 / 1 while snapping
    float yMagnet   = 0.015f;  // y within this distance of 0 or 1 sticks to it (DESIGN.md §9.2); 0 = off
    int   quickShiftSubdivisions = 4;      // Quick Shift moves snap to 1/n grid cell (DESIGN.md §7.6)
    float quickShiftMargin       = 0.001f; // min node-x gap kept to non-group neighbours
};

enum class AxisLock { None, X, Y };

// Per-update options for drag gestures. The view maps modifiers onto these (DESIGN.md: Ctrl =
// invertSnap, Alt = axis lock to the dominant direction). "Fine" (Shift) is the view's job: it
// scales the pointer delta before passing it in, see EditorController.
struct DragOptions {
    bool     invertSnap = false;           // snapping = SnapSettings::enabled XOR invertSnap
    AxisLock axis       = AxisLock::None;  // node drags: keep the other axis at its origin
};

enum class HitKind {
    None,            // outside the plot / lane
    Empty,           // inside the plot, nothing under the pointer
    Node,            // index = node index
    Segment,         // index = segment index (node i → i+1), near the curve
    TensionHandle,   // index = segment index, on the segment's handle dot
    QuickShiftLane,  // inside the lane, not on the bar
    QuickShiftBar,   // the group bar body
    QuickShiftStart, // the bar's left end handle
    QuickShiftEnd,   // the bar's right end handle
};

struct Hit {
    HitKind kind     = HitKind::None;
    int     index    = -1;
    float   distance = 0.f;  // pixels from the pointer to the hit feature
};

struct HitTolerances {
    float nodePx             = 8.f;   // pointer distance that grabs a node
    float handlePx           = 7.f;   // tension handle dot
    float segmentPx          = 7.f;   // distance to the curve that grabs a segment
    float minHandleSegmentPx = 14.f;  // segments narrower than this have no handle
    float laneHandlePx       = 5.f;   // Quick Shift bar end handles
};

enum class SelectMode { Replace, Add, Toggle, Remove };

enum class GestureKind {
    None,
    MoveNodes,
    Tension,
    Line,
    Pencil,
    QuickShiftMove,
    QuickShiftStart,
    QuickShiftEnd,
    Stretch,
    Custom,  // a transaction opened with beginTransaction()
};

enum class QuickShiftPart {
    Bar,       // move the group (and the range) by Δx
    Start,     // move the range's start edge (auto-groups)
    End,       // move the range's end edge (auto-groups)
    NewRange,  // create a range from the pointer (drag in an empty lane)
};

enum class StretchEdge { Start, End };

// What load() does to the undo history.
enum class LoadHistory {
    Keep,    // leave it (default): the load is not an undo step
    Clear,   // drop all undo/redo entries (project restore)
    Record,  // make the load one undo step (e.g. a preset picked in the UI)
};

// Quick Shift range, stored in *node* space so it stays glued to its nodes when rotate, swing or
// the grid change. Use EditorModel::quickShiftSpans() for the timeline pieces to draw.
struct QuickShiftRange {
    bool  valid         = false;  // false: no explicit range and no group
    bool  explicitRange = false;  // false: derived from the group's extent
    float start         = 0.f;    // node x
    float end           = 0.f;    // node x, >= start
};

struct GridLine {
    int   index    = 0;      // k: the line sits at node x = k / divisions
    float nodeX    = 0.f;
    float timeline = 0.f;    // where to draw it
    bool  beat     = false;  // falls on a whole quarter-note beat (Note mode only)
};

} // namespace kick::editor
