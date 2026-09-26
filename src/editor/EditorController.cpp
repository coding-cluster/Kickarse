// Kickarse — default gesture mapping (see EditorController.h).
#include "EditorController.h"

#include <algorithm>
#include <cmath>

namespace kick::editor {

namespace {

inline float clamp01(float v) noexcept
{
    return v > 0.f ? (v < 1.f ? v : 1.f) : 0.f;
}

} // namespace

EditorController::EditorController(EditorModel& model) : model_(model) {}

void EditorController::setTool(Tool tool)
{
    if (tool == tool_)
        return;
    cancel();
    tool_ = tool;
}

bool EditorController::inLane(float px, float py) const noexcept
{
    return laneVisible_ && lane_.width > 0.f && lane_.contains(px, py);
}

void EditorController::updateFine(float px, float py, PointerMods mods, float fineScale)
{
    const float f = mods.shift ? fineScale : 1.f;
    effPx_ += (px - lastPx_) * f;
    effPy_ += (py - lastPy_) * f;
    lastPx_ = px;
    lastPy_ = py;
}

float EditorController::effDx() const noexcept
{
    return plot_.width > 0.f ? (effPx_ - downPx_) / plot_.width : 0.f;
}

float EditorController::effDy() const noexcept
{
    return plot_.height > 0.f ? -(effPy_ - downPy_) / plot_.height : 0.f;
}

AxisLock EditorController::axisFor(PointerMods mods)
{
    if (!mods.alt) {
        axis_ = AxisLock::None;
        return axis_;
    }
    if (axis_ == AxisLock::None) {
        const float dx = effPx_ - downPx_, dy = effPy_ - downPy_;
        if (std::hypot(dx, dy) >= settings_.dragThresholdPx)
            axis_ = std::fabs(dx) >= std::fabs(dy) ? AxisLock::X : AxisLock::Y;
    }
    return axis_;
}

bool EditorController::mouseDown(float px, float py, PointerMods mods, int clickCount)
{
    if (!std::isfinite(px) || !std::isfinite(py))
        return false;
    if (mode_ != Mode::Idle)
        cancel();
    downPx_ = lastPx_ = effPx_ = px;
    downPy_ = lastPy_ = effPy_ = py;
    axis_             = AxisLock::None;
    ctrlClickPending_ = false;

    if (inLane(px, py)) {
        const Hit h = model_.hitTestQuickShift(lane_, px, py, settings_.hit);
        pressHit_   = h;
        if (clickCount >= 2 && h.kind == HitKind::QuickShiftBar) {
            model_.selectQuickShiftGroup();
            return true;
        }
        QuickShiftPart part = QuickShiftPart::NewRange;
        if (h.kind == HitKind::QuickShiftBar)
            part = QuickShiftPart::Bar;
        else if (h.kind == HitKind::QuickShiftStart)
            part = QuickShiftPart::Start;
        else if (h.kind == HitKind::QuickShiftEnd)
            part = QuickShiftPart::End;
        if (model_.beginQuickShiftDrag(part, clamp01(lane_.toTimeline(px))))
            mode_ = Mode::QuickShift;
        return true;
    }

    if (!plot_.contains(px, py, settings_.hit.nodePx))
        return false;
    const float t = clamp01(plot_.toTimeline(px));
    const float v = clamp01(plot_.toValue(py));

    if (tool_ == Tool::Line) {
        if (model_.beginLine(t, v, DragOptions{mods.ctrl, AxisLock::None}))
            mode_ = Mode::Line;
        return true;
    }
    if (tool_ == Tool::Pencil) {
        if (model_.beginPencil(t, v))
            mode_ = Mode::Pencil;
        return true;
    }

    const Hit h = model_.hitTest(plot_, px, py, settings_.hit);
    pressHit_   = h;
    if (clickCount >= 2)
        return doubleClick(px, py);

    switch (h.kind) {
    case HitKind::Node:
        ctrlClickPending_ = mods.ctrl;
        if (mods.shift) {
            model_.select(h.index, SelectMode::Toggle);
            if (!model_.isSelected(h.index))
                return true;   // deselected: nothing to drag
        } else if (!model_.isSelected(h.index)) {
            model_.select(h.index, SelectMode::Replace);
        }
        mode_ = Mode::NodePress;
        return true;
    case HitKind::TensionHandle:
    case HitKind::Segment:
        if (model_.beginTensionDrag(h.index, false))
            mode_ = Mode::Tension;
        return true;
    default:
        marqueeBase_ = mods.shift ? model_.selection() : NodeMask{};
        if (!mods.shift)
            model_.selectNone();
        marqueeT0_ = marqueeT1_ = t;
        marqueeV0_ = marqueeV1_ = v;
        mode_      = Mode::Marquee;
        return true;
    }
}

bool EditorController::doubleClick(float px, float py)
{
    const float t = clamp01(plot_.toTimeline(px));
    const float v = clamp01(plot_.toValue(py));
    switch (pressHit_.kind) {
    case HitKind::Node:          model_.deleteNode(pressHit_.index); break;
    case HitKind::TensionHandle: model_.straightenSegment(pressHit_.index); break;
    case HitKind::Segment:       model_.addNodeOnCurve(t); break;
    case HitKind::Empty:         model_.addNode(t, v); break;
    default:                     return false;
    }
    mode_ = Mode::Idle;
    return true;
}

void EditorController::beginNodeDragIfMoved(float px, float py, PointerMods mods)
{
    if (std::hypot(px - downPx_, py - downPy_) < settings_.dragThresholdPx)
        return;
    if (!model_.beginNodeDrag(pressHit_.index)) {
        mode_ = Mode::Idle;
        return;
    }
    mode_             = Mode::NodeDrag;
    ctrlClickPending_ = false;
    (void)mods;
}

bool EditorController::mouseMove(float px, float py, PointerMods mods)
{
    if (!std::isfinite(px) || !std::isfinite(py))
        return false;
    switch (mode_) {
    case Mode::Idle: {
        Hit h;
        if (inLane(px, py))
            h = model_.hitTestQuickShift(lane_, px, py, settings_.hit);
        else
            h = model_.hitTest(plot_, px, py, settings_.hit);
        const bool changed = h.kind != hover_.kind || h.index != hover_.index;
        hover_ = h;
        return changed;
    }
    case Mode::NodePress:
        beginNodeDragIfMoved(px, py, mods);
        if (mode_ != Mode::NodeDrag) {
            lastPx_ = effPx_ = px;
            lastPy_ = effPy_ = py;
            return false;
        }
        [[fallthrough]];
    case Mode::NodeDrag:
        updateFine(px, py, mods, settings_.fineNodeScale);
        model_.updateNodeDrag(effDx(), effDy(), DragOptions{mods.ctrl, axisFor(mods)});
        return true;
    case Mode::Tension:
        updateFine(px, py, mods, settings_.fineTensionScale);
        model_.updateTensionDrag((downPy_ - effPy_) / std::max(settings_.tensionPxPerUnit, 1.f));
        return true;
    case Mode::Marquee:
        marqueeT1_ = plot_.toTimeline(px);
        marqueeV1_ = plot_.toValue(py);
        model_.selectRect(marqueeT0_, marqueeV0_, marqueeT1_, marqueeV1_, SelectMode::Add, &marqueeBase_);
        return true;
    case Mode::Line: {
        float t = clamp01(plot_.toTimeline(px));
        if (std::fabs(px - downPx_) < settings_.verticalLinePx)
            t = clamp01(plot_.toTimeline(downPx_));
        model_.updateLine(t, clamp01(plot_.toValue(py)), DragOptions{mods.ctrl, AxisLock::None});
        return true;
    }
    case Mode::Pencil:
        model_.addPencilPoint(clamp01(plot_.toTimeline(px)), clamp01(plot_.toValue(py)));
        return true;
    case Mode::QuickShift:
        updateFine(px, py, mods, settings_.fineQuickShiftScale);
        model_.updateQuickShiftDrag(lane_.width > 0.f ? (effPx_ - downPx_) / lane_.width : 0.f,
                                    DragOptions{mods.ctrl, AxisLock::None});
        return true;
    }
    return false;
}

bool EditorController::mouseUp(float px, float py, PointerMods mods)
{
    const Mode mode = mode_;
    mode_ = Mode::Idle;
    switch (mode) {
    case Mode::Idle:
        return false;
    case Mode::NodePress:
        if (ctrlClickPending_)
            model_.toggleQuickShiftMember(pressHit_.index);
        else if (!mods.shift && model_.selectionCount() > 1)
            model_.select(pressHit_.index, SelectMode::Replace);
        break;
    case Mode::Marquee:
        break;
    case Mode::NodeDrag:
    case Mode::Tension:
    case Mode::Line:
    case Mode::Pencil:
    case Mode::QuickShift:
        model_.endGesture();
        break;
    }
    ctrlClickPending_ = false;
    axis_             = AxisLock::None;
    if (std::isfinite(px) && std::isfinite(py))
        hover_ = inLane(px, py) ? model_.hitTestQuickShift(lane_, px, py, settings_.hit)
                                : model_.hitTest(plot_, px, py, settings_.hit);
    return true;
}

bool EditorController::keyDown(Key key, PointerMods mods)
{
    bool handled = false;
    switch (key) {
    case Key::Delete:
    case Key::Backspace:
        handled = model_.selectionCount() > 0;
        model_.deleteSelection();
        break;
    case Key::Escape:
        if (mode_ != Mode::Idle) {
            cancel();
            handled = true;
        } else if (model_.selectionCount() > 0) {
            model_.selectNone();
            handled = true;
        }
        break;
    case Key::Left:
    case Key::Right:
    case Key::Up:
    case Key::Down: {
        if (model_.selectionCount() == 0)
            break;
        const int sx = key == Key::Left ? -1 : key == Key::Right ? 1 : 0;
        const int sy = key == Key::Down ? -1 : key == Key::Up ? 1 : 0;
        model_.nudgeSelection(sx, sy, mods.shift);
        handled = true;
        break;
    }
    case Key::A:
        if (mods.ctrl) {
            model_.selectAll();
            handled = true;
        }
        break;
    case Key::C:
        if (mods.ctrl) {
            model_.copy();
            handled = true;
        }
        break;
    case Key::V:
        if (mods.ctrl) {
            model_.paste(mods.shift);
            handled = true;
        }
        break;
    case Key::Z:
        if (mods.ctrl) {
            if (mods.shift)
                model_.redo();
            else
                model_.undo();
            handled = true;
        }
        break;
    case Key::Y:
        if (mods.ctrl) {
            model_.redo();
            handled = true;
        }
        break;
    }
    // An edit made from the keyboard ends a running drag inside the model; follow it.
    if (mode_ != Mode::Idle && mode_ != Mode::Marquee && mode_ != Mode::NodePress
        && model_.gesture() == GestureKind::None)
        mode_ = Mode::Idle;
    return handled;
}

void EditorController::cancel()
{
    switch (mode_) {
    case Mode::NodeDrag:
    case Mode::Tension:
    case Mode::Line:
    case Mode::Pencil:
    case Mode::QuickShift:
        model_.cancelGesture();
        break;
    case Mode::Marquee:
        model_.setSelection(marqueeBase_);
        break;
    default:
        break;
    }
    mode_             = Mode::Idle;
    ctrlClickPending_ = false;
    axis_             = AxisLock::None;
}

bool EditorController::marquee(float& t0, float& v0, float& t1, float& v1) const noexcept
{
    if (mode_ != Mode::Marquee)
        return false;
    t0 = marqueeT0_;
    v0 = marqueeV0_;
    t1 = marqueeT1_;
    v1 = marqueeV1_;
    return true;
}

CursorHint EditorController::cursor() const noexcept
{
    switch (mode_) {
    case Mode::NodePress:
    case Mode::NodeDrag:
        return CursorHint::Move;
    case Mode::Tension:
        return CursorHint::Bend;
    case Mode::Line:
    case Mode::Pencil:
        return CursorHint::Crosshair;
    case Mode::QuickShift:
        return pressHit_.kind == HitKind::QuickShiftBar ? CursorHint::Grab : CursorHint::ResizeHorizontal;
    case Mode::Marquee:
        return CursorHint::Default;
    case Mode::Idle:
        break;
    }
    switch (hover_.kind) {
    case HitKind::QuickShiftBar:   return CursorHint::Grab;
    case HitKind::QuickShiftStart:
    case HitKind::QuickShiftEnd:   return CursorHint::ResizeHorizontal;
    case HitKind::QuickShiftLane:  return CursorHint::Default;
    case HitKind::None:            return CursorHint::Default;
    default:                       break;
    }
    if (tool_ != Tool::Select)
        return CursorHint::Crosshair;
    switch (hover_.kind) {
    case HitKind::Node:          return CursorHint::Move;
    case HitKind::Segment:
    case HitKind::TensionHandle: return CursorHint::Bend;
    default:                     return CursorHint::Default;
    }
}

Readout EditorController::readout() const
{
    Readout r;
    r.px = lastPx_;
    r.py = lastPy_;
    const GestureInfo g = model_.gestureInfo();
    switch (mode_) {
    case Mode::NodeDrag:
        r.kind     = ReadoutKind::Node;
        r.timeline = g.position.x;
        r.value    = g.position.y;
        break;
    case Mode::Tension:
        r.kind    = ReadoutKind::Tension;
        r.tension = g.tension;
        break;
    case Mode::QuickShift:
        r.kind  = ReadoutKind::QuickShift;
        r.shift = g.shift;
        break;
    case Mode::Line:
        r.kind     = ReadoutKind::Line;
        r.timeline = g.lineEnd.x;
        r.value    = g.lineEnd.y;
        break;
    default:
        break;
    }
    return r;
}

} // namespace kick::editor
