// Kickarse — editor tests: EditorController gesture mapping (pixels → model calls).
#include "EditorTest.h"

using namespace et;

namespace {

const ViewTransform kPlot{100.f, 50.f, 800.f, 200.f};
const ViewTransform kLane{100.f, 270.f, 800.f, 16.f};
const Envelope      kBase = makeEnv({{0, 0, 0.3f, 0}, {0.3f, 0.5f, 0, 0}, {0.6f, 1, 0, 0}, {1, 1, 0, 0}});

struct Rig {
    Fixture          f;
    EditorController c{f.model};
    Rig()
    {
        f.model.load(kBase, kBase, LoadHistory::Clear);
        c.setPlot(kPlot);
        c.setLane(kLane, true);
        f.listener.clear();
    }
    EditorModel& m() { return f.model; }
    float px(float t) const { return kPlot.toPixelX(t); }
    float py(float v) const { return kPlot.toPixelY(v); }
    void  drag(float x0, float y0, float x1, float y1, PointerMods mods = {}, int steps = 8)
    {
        c.mouseDown(x0, y0, mods);
        for (int i = 1; i <= steps; ++i) {
            const float u = float(i) / float(steps);
            c.mouseMove(x0 + (x1 - x0) * u, y0 + (y1 - y0) * u, mods);
        }
        c.mouseUp(x1, y1, mods);
    }
    void click(float x, float y, PointerMods mods = {}, int count = 1)
    {
        c.mouseDown(x, y, mods, count);
        c.mouseUp(x, y, mods);
    }
    void doubleClick(float x, float y, PointerMods mods = {})
    {
        click(x, y, mods, 1);
        click(x, y, mods, 2);
    }
};

PointerMods shift()
{
    return PointerMods{true, false, false};
}
PointerMods ctrl()
{
    return PointerMods{false, true, false};
}
PointerMods alt()
{
    return PointerMods{false, false, true};
}

} // namespace

TEST_CASE(ctl_hover_and_cursor)
{
    Rig r;
    CHECK(r.c.mouseMove(r.px(0.3f), r.py(0.5f), {}));
    CHECK(r.c.hover().kind == HitKind::Node && r.c.cursor() == CursorHint::Move);
    r.c.mouseMove(r.px(0.8f), r.py(1.f) + 3.f, {});
    CHECK(r.c.hover().kind == HitKind::TensionHandle || r.c.hover().kind == HitKind::Segment);
    CHECK(r.c.cursor() == CursorHint::Bend);
    r.c.mouseMove(r.px(0.5f), r.py(0.05f), {});
    CHECK(r.c.hover().kind == HitKind::Empty && r.c.cursor() == CursorHint::Default);
    r.c.setTool(Tool::Line);
    CHECK(r.c.cursor() == CursorHint::Crosshair);
    r.c.setTool(Tool::Select);
    CHECK(!r.c.pressed());
}

TEST_CASE(ctl_node_drag_fine_axis_threshold)
{
    Rig r;
    r.drag(r.px(0.3f), r.py(0.5f), r.px(0.3f) + 80.f, r.py(0.5f));
    CHECK_NEAR(r.m().editEnvelope().node(1).x, 0.4f, 1e-5);
    CHECK(r.m().undoCount() == 1 && r.m().undoLabel() == "Move Nodes");
    // Shift held while moving: ×0.2 (the press itself is plain; Shift-press toggles selection).
    r.c.mouseDown(r.px(0.4f), r.py(0.5f), {});
    r.c.mouseMove(r.px(0.4f) + 10.f, r.py(0.5f), {});
    r.c.mouseMove(r.px(0.4f) + 110.f, r.py(0.5f), shift());
    r.c.mouseUp(r.px(0.4f) + 110.f, r.py(0.5f), shift());
    CHECK_NEAR(r.m().editEnvelope().node(1).x, 0.4375f, 1e-5);   // 10 px + 100 px × 0.2
    // Alt: axis lock to the dominant direction.
    r.drag(r.px(0.4375f), r.py(0.5f), r.px(0.4375f) + 50.f, r.py(0.5f) - 20.f, alt());
    CHECK_NEAR(r.m().editEnvelope().node(1).y, 0.5f, 1e-6);
    CHECK_NEAR(r.m().editEnvelope().node(1).x, 0.5f, 1e-5);   // 0.4375 + 50 px
    // Below the drag threshold: a click, no step.
    const int steps = r.m().undoCount();
    r.drag(r.px(0.5f), r.py(0.5f), r.px(0.5f) + 2.f, r.py(0.5f));
    CHECK(r.m().undoCount() == steps);
    // Readout while dragging.
    r.c.mouseDown(r.px(0.5f), r.py(0.5f), {});
    r.c.mouseMove(r.px(0.5f) + 40.f, r.py(0.5f) - 40.f, {});
    const Readout ro = r.c.readout();
    CHECK(ro.kind == ReadoutKind::Node);
    CHECK_NEAR(ro.timeline, 0.55f, 1e-5);
    CHECK_NEAR(ro.value, 0.7f, 1e-5);
    // Esc cancels the drag.
    CHECK(r.c.keyDown(Key::Escape, {}));
    CHECK_NEAR(r.m().editEnvelope().node(1).x, 0.5f, 1e-5);
    r.c.mouseUp(r.px(0.55f), r.py(0.7f), {});
    CHECK(r.m().undoCount() == steps);
    // Ctrl while dragging inverts snapping.
    r.f.setSnap(true);
    r.drag(r.px(0.5f), r.py(0.5f), r.px(0.5f) - 36.f, r.py(0.5f), ctrl());
    CHECK_NEAR(r.m().editEnvelope().node(1).x, 0.455f, 1e-5);
    r.drag(r.px(0.455f), r.py(0.5f), r.px(0.455f) - 36.f, r.py(0.5f));
    CHECK(r.m().editEnvelope().node(1).x == 0.5f);   // 0.41 snaps to 0.5
}

TEST_CASE(ctl_clicks_selection_and_quick_shift)
{
    Rig r;
    r.click(r.px(0.3f), r.py(0.5f));
    CHECK(r.m().selectionCount() == 1 && r.m().isSelected(1));
    r.click(r.px(0.6f), r.py(1.f), shift());
    CHECK(r.m().selectionCount() == 2);
    r.click(r.px(0.6f), r.py(1.f), shift());
    CHECK(r.m().selectionCount() == 1);
    // A plain click on a node of a multi-selection selects just it.
    r.m().selectAll();
    r.click(r.px(0.6f), r.py(1.f));
    CHECK(r.m().selectionCount() == 1 && r.m().isSelected(2));
    // Clicking empty space clears the selection.
    r.click(r.px(0.5f), r.py(0.05f));
    CHECK(r.m().selectionCount() == 0);
    // Ctrl-click toggles Quick Shift membership; endpoints refuse.
    r.click(r.px(0.3f), r.py(0.5f), ctrl());
    CHECK(r.m().isQuickShiftMember(1));
    r.click(r.px(0.f), r.py(0.f), ctrl());
    CHECK(r.m().quickShiftGroupSize() == 1);
    r.click(r.px(0.3f), r.py(0.5f), ctrl());
    CHECK(!r.m().isQuickShiftMember(1));
}

TEST_CASE(ctl_double_clicks)
{
    Rig r;
    // Empty: add at the pointer.
    r.doubleClick(r.px(0.45f), r.py(0.2f));
    CHECK(r.m().editEnvelope().size() == 5);
    CHECK_NEAR(r.m().editEnvelope().node(2).x, 0.45f, 1e-5);
    CHECK_NEAR(r.m().editEnvelope().node(2).y, 0.2f, 1e-5);
    CHECK(r.m().isSelected(2));
    // Node: delete.
    r.doubleClick(r.px(0.45f), r.py(0.2f));
    CHECK(ops::sameNodes(r.m().editEnvelope(), kBase));
    // Endpoint: nothing.
    r.doubleClick(r.px(0.f), r.py(0.f));
    CHECK(r.m().editEnvelope().size() == 4);
    // Handle: straighten.
    const Vec2 h = r.m().tensionHandlePosition(0);
    r.doubleClick(r.px(h.x), r.py(h.y));
    CHECK(r.m().editEnvelope().node(0).tension == 0.f);
    // Segment: add on the curve (curve unchanged).
    const Envelope before = r.m().editEnvelope();
    const float    t      = 0.4f;
    r.doubleClick(r.px(t), r.py(r.m().valueAt(t)) - 2.f);
    CHECK(r.m().editEnvelope().size() == before.size() + 1);
    CHECK(ops::curveDistance(before, r.m().editEnvelope()) < 2e-5f);
}

TEST_CASE(ctl_marquee_and_tension)
{
    Rig r;
    r.c.mouseDown(r.px(0.2f), r.py(1.f) - 4.f, {});
    r.c.mouseMove(r.px(0.65f), r.py(0.3f), {});
    float t0 = 0, v0 = 0, t1 = 0, v1 = 0;
    CHECK(r.c.marquee(t0, v0, t1, v1));
    CHECK(r.m().selectionCount() == 2);
    r.c.mouseUp(r.px(0.65f), r.py(0.3f), {});
    CHECK(!r.c.marquee(t0, v0, t1, v1));
    CHECK(r.m().isSelected(1) && r.m().isSelected(2));
    // Shift adds.
    r.drag(r.px(0.95f), r.py(0.9f), r.px(1.f) + 4.f, r.py(1.f) - 4.f, shift());
    CHECK(r.m().selectionCount() == 3);
    // Bend: 90 px up = 1.0 more curve (segment 1 rises 0.5 → 1, so its tension falls).
    const float t = 0.45f;
    r.c.mouseDown(r.px(t), r.py(r.m().valueAt(t)), {});
    r.c.mouseMove(r.px(t), r.py(r.m().valueAt(t)) - 45.f, {});
    CHECK(r.c.readout().kind == ReadoutKind::Tension);
    r.c.mouseUp(r.px(t), 0.f, {});
    CHECK_NEAR(r.m().editEnvelope().node(1).tension, -0.5f, 1e-5);
    CHECK(r.m().undoLabel() == "Bend Segment");
}

TEST_CASE(ctl_tools_and_lane)
{
    Rig r;
    r.c.setTool(Tool::Line);
    r.drag(r.px(0.1f), r.py(0.2f), r.px(0.25f), r.py(0.2f));
    CHECK_NEAR(r.m().valueAt(0.2f), 0.2f, 1e-4);
    CHECK(r.m().undoLabel() == "Draw Line");
    // A near-vertical drag becomes a step.
    r.drag(r.px(0.8f), r.py(0.2f), r.px(0.8f) + 1.f, r.py(0.9f));
    CHECK_NEAR(r.m().editEnvelope().evaluateLeft(0.8f), 0.2f, 1e-4);
    CHECK_NEAR(r.m().editEnvelope().evaluate(0.8f), 0.9f, 1e-4);
    r.c.setTool(Tool::Pencil);
    r.drag(r.px(0.4f), r.py(0.5f), r.px(0.7f), r.py(0.5f), {}, 40);
    CHECK_NEAR(r.m().valueAt(0.55f), 0.5f, 0.02);
    CHECK(r.m().undoLabel() == "Draw");
    r.c.setTool(Tool::Select);
    // Quick Shift lane: create a range by dragging the empty lane, then drag the bar.
    r.f.model.load(kBase, kBase, LoadHistory::Clear);
    r.drag(kLane.toPixelX(0.25f), 278.f, kLane.toPixelX(0.65f), 278.f);
    CHECK(r.m().quickShiftGroupSize() == 2);
    const float mid = kLane.toPixelX(0.45f);
    r.c.mouseMove(mid, 278.f, {});
    CHECK(r.c.hover().kind == HitKind::QuickShiftBar && r.c.cursor() == CursorHint::Grab);
    r.c.mouseDown(mid, 278.f, {});
    r.c.mouseMove(mid + 40.f, 278.f, {});
    CHECK(r.c.readout().kind == ReadoutKind::QuickShift);
    CHECK_NEAR(r.c.readout().shift, 0.05f, 1e-5);
    r.c.mouseUp(mid + 40.f, 278.f, {});
    CHECK_NEAR(r.m().editEnvelope().node(1).x, 0.35f, 1e-5);
    CHECK(r.m().undoLabel() == "Quick Shift");
    // Double-click the bar selects the group.
    r.m().selectNone();
    r.click(kLane.toPixelX(0.5f), 278.f, {}, 2);
    CHECK(r.m().selectionCount() == 2);
    // Hidden lane: presses there fall through to nothing.
    r.c.setLane(kLane, false);
    CHECK(!r.c.mouseDown(kLane.toPixelX(0.5f), 300.f, {}));
}

TEST_CASE(ctl_keys)
{
    Rig r;
    CHECK(r.c.keyDown(Key::A, ctrl()));
    CHECK(r.m().selectionCount() == 4);
    CHECK(!r.c.keyDown(Key::A, {}));
    CHECK(r.c.keyDown(Key::Delete, {}));
    CHECK(r.m().editEnvelope().size() == 2);
    CHECK(r.c.keyDown(Key::Z, ctrl()));
    CHECK(r.m().editEnvelope().size() == 4);
    CHECK(r.c.keyDown(Key::Z, PointerMods{true, true, false}));
    CHECK(r.m().editEnvelope().size() == 2);
    CHECK(r.c.keyDown(Key::Z, ctrl()));
    CHECK(r.c.keyDown(Key::Y, ctrl()));
    CHECK(r.m().editEnvelope().size() == 2);
    r.c.keyDown(Key::Z, ctrl());
    r.m().select(1);
    CHECK(r.c.keyDown(Key::Right, {}));
    CHECK_NEAR(r.m().editEnvelope().node(1).x, 0.55f, 1e-6);
    CHECK(r.c.keyDown(Key::Up, shift()));
    CHECK_NEAR(r.m().editEnvelope().node(1).y, 0.501f, 1e-6);
    CHECK(r.c.keyDown(Key::Escape, {}));
    CHECK(r.m().selectionCount() == 0);
    CHECK(!r.c.keyDown(Key::Escape, {}));
    CHECK(!r.c.keyDown(Key::Left, {}));
    CHECK(!r.c.keyDown(Key::Delete, {}));
    CHECK(r.c.keyDown(Key::C, ctrl()));
    CHECK(!r.m().clipboard().empty());
    r.m().setActiveBand(Band::B);
    CHECK(r.c.keyDown(Key::V, ctrl()));
    CHECK(ops::sameNodes(r.m().envelope(Band::B), r.m().envelope(Band::A)));
    // A key edit during a drag ends the drag; the controller follows.
    r.m().setActiveBand(Band::A);
    const Vec2 p = r.m().nodePosition(1);
    r.c.mouseDown(r.px(p.x), r.py(p.y), {});
    r.c.mouseMove(r.px(p.x) + 30.f, r.py(p.y), {});
    CHECK(r.c.pressed() && r.m().gesture() == GestureKind::MoveNodes);
    r.c.keyDown(Key::Delete, {});
    CHECK(!r.c.pressed());
    r.c.mouseUp(r.px(p.x) + 30.f, r.py(p.y), {});
    CHECK(validEnvelope(r.m().editEnvelope(), "key during drag"));
}
