// Kickarse — default gesture mapping for the envelope editor (DESIGN.md §7.6, §7.8, §9.2).
//
// Optional convenience on top of EditorModel: feed it the view's raw pointer/key events in pixels
// and it drives the model, keeping the transient UI state the view draws (hover, marquee, cursor,
// readout). A view with other conventions can skip it and call EditorModel directly.
//
// Mapping (Select tool)
//   hover                     hover() = hitTest(); cursor(): Move on nodes, Bend on segments/handles
//   drag node                 move the selection; Ctrl inverts snap, Shift ×0.2 fine, Alt axis lock
//   click / Shift-click node  select only it / toggle it
//   Ctrl-click node (no drag) toggle Quick Shift membership
//   drag segment or handle    bend: 90 px = 1.0 tension, Shift ×0.25
//   drag empty space          marquee (Shift adds to the selection)
//   double-click              empty: add node at the pointer (snapped); segment: add node on the curve;
//                             node: delete it; handle: straighten the segment
//   Delete / Backspace        delete selection      Ctrl+A    select all     Esc  cancel / select none
//   arrows                    nudge (Shift = fine)  Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z  undo / redo
//   Ctrl+C / Ctrl+V           copy / paste (Ctrl+Shift+V pastes into the selection span)
// Line tool: drag A → B (Ctrl inverts snap; < verticalLinePx wide = vertical step).
// Pencil tool: drag to draw.
// Quick Shift lane: drag the bar (snap ¼ cell, Ctrl inverts, Shift ×0.25), drag an end handle
// (auto-groups), drag in the empty lane to create a range, double-click the bar to select the group.
#pragma once

#include "EditorModel.h"

namespace kick::editor {

enum class Tool { Select, Line, Pencil };

struct PointerMods {
    bool shift = false;
    bool ctrl  = false;   // Cmd on macOS
    bool alt   = false;
};

enum class Key { Delete, Backspace, Escape, Left, Right, Up, Down, A, C, V, Y, Z };

enum class CursorHint { Default, Move, Bend, ResizeHorizontal, Grab, Crosshair };

enum class ReadoutKind { None, Node, Tension, QuickShift, Line };

// What the drag tag shows (DESIGN.md §7.8). The view formats: ms = timeline · cycle length,
// dB = 20·log10(1 − depth·(1 − value)), curve = tension · 100, shift ms = shift · cycle length.
struct Readout {
    ReadoutKind kind = ReadoutKind::None;
    float px = 0.f, py = 0.f;   // pointer, pixels
    float timeline = 0.f;
    float value    = 0.f;
    float tension  = 0.f;
    float shift    = 0.f;       // Quick Shift Δ, timeline phase
};

struct ControllerSettings {
    HitTolerances hit;
    float dragThresholdPx     = 3.f;    // movement before a press on a node becomes a drag
    float fineNodeScale       = 0.2f;
    float fineTensionScale    = 0.25f;
    float fineQuickShiftScale = 0.25f;
    float tensionPxPerUnit    = 90.f;
    float verticalLinePx      = 2.f;
};

class EditorController {
public:
    explicit EditorController(EditorModel& model);

    void setTool(Tool tool);
    Tool tool() const noexcept { return tool_; }
    void setPlot(const ViewTransform& plot) noexcept { plot_ = plot; }
    void setLane(const ViewTransform& lane, bool visible = true) noexcept
    {
        lane_        = lane;
        laneVisible_ = visible;
    }
    void setSettings(const ControllerSettings& s) noexcept { settings_ = s; }
    const ControllerSettings& settings() const noexcept { return settings_; }

    // Return true when the event was consumed (repaint). clickCount = 2 for a double-click press.
    bool mouseDown(float px, float py, PointerMods mods, int clickCount = 1);
    bool mouseMove(float px, float py, PointerMods mods);   // drag while pressed, hover otherwise
    bool mouseUp(float px, float py, PointerMods mods);
    bool keyDown(Key key, PointerMods mods);
    void cancel();   // focus lost / Esc during a drag: cancel the running gesture

    // ---- Drawing state ------------------------------------------------------------------------------
    const Hit& hover() const noexcept { return hover_; }
    bool       pressed() const noexcept { return mode_ != Mode::Idle; }
    bool       marquee(float& t0, float& v0, float& t1, float& v1) const noexcept;   // timeline/value
    CursorHint cursor() const noexcept;
    Readout    readout() const;

private:
    enum class Mode { Idle, NodePress, NodeDrag, Tension, Marquee, Line, Pencil, QuickShift };

    void  beginNodeDragIfMoved(float px, float py, PointerMods mods);
    void  updateFine(float px, float py, PointerMods mods, float fineScale);
    float effDx() const noexcept;
    float effDy() const noexcept;
    bool  inLane(float px, float py) const noexcept;
    bool  doubleClick(float px, float py);
    AxisLock axisFor(PointerMods mods);

    EditorModel&       model_;
    Tool               tool_ = Tool::Select;
    ViewTransform      plot_;
    ViewTransform      lane_;
    bool               laneVisible_ = false;
    ControllerSettings settings_;

    Mode     mode_ = Mode::Idle;
    Hit      hover_;
    Hit      pressHit_;
    float    downPx_ = 0.f, downPy_ = 0.f;   // press position
    float    lastPx_ = 0.f, lastPy_ = 0.f;   // previous raw pointer
    float    effPx_ = 0.f, effPy_ = 0.f;     // pointer with fine scaling accumulated
    bool     ctrlClickPending_ = false;
    AxisLock axis_ = AxisLock::None;
    NodeMask marqueeBase_;
    float    marqueeT0_ = 0.f, marqueeV0_ = 0.f, marqueeT1_ = 0.f, marqueeV1_ = 0.f;
};

} // namespace kick::editor
