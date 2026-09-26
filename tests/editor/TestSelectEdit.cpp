// Kickarse — editor tests: selection, marquee, node add/delete, drags, snapping, tension, nudge.
#include "EditorTest.h"

using namespace et;

namespace {

const Envelope kBase = makeEnv({{0, 0, 0.3f, 0}, {0.3f, 0.5f, 0, 0}, {0.6f, 1, 0, 0}, {1, 1, 0, 0}});

void loadBase(Fixture& f, const Envelope& e = kBase)
{
    f.model.load(e, e, LoadHistory::Clear);
    f.listener.clear();
}

float x(const Fixture& f, int i)
{
    return f.model.editEnvelope().node(i).x;
}

float y(const Fixture& f, int i)
{
    return f.model.editEnvelope().node(i).y;
}

} // namespace

TEST_CASE(select_basic_and_masks)
{
    Fixture f;
    loadBase(f);
    EditorModel& m = f.model;
    CHECK(m.selectionCount() == 0);
    m.select(1);
    CHECK(m.isSelected(1) && m.selectionCount() == 1);
    m.select(2, SelectMode::Add);
    CHECK(m.selectionCount() == 2);
    m.select(1, SelectMode::Toggle);
    CHECK(!m.isSelected(1) && m.isSelected(2));
    m.select(2, SelectMode::Remove);
    CHECK(m.selectionCount() == 0);
    m.select(99);   // out of range: ignored
    m.select(-1);
    CHECK(m.selectionCount() == 0);
    m.selectAll();
    CHECK(m.selectionCount() == 4);   // bits past the node count are masked off
    m.selectNone();
    NodeMask mask;
    mask.set(1);
    mask.set(100);
    m.setSelection(mask);
    CHECK(m.selectionCount() == 1 && m.isSelected(1));
    CHECK(!m.isSelected(-5) && !m.isSelected(500));
    // Selection is per band.
    m.setActiveBand(Band::B);
    CHECK(m.selectionCount() == 0);
    m.setActiveBand(Band::A);
    CHECK(m.isSelected(1));
    // Selection changes are not undo steps.
    CHECK(!m.canUndo());
}

TEST_CASE(select_marquee_and_across_seam)
{
    Fixture f;
    loadBase(f);
    EditorModel& m = f.model;
    m.selectRect(0.2f, 0.f, 0.7f, 1.f);
    CHECK(m.selectionCount() == 2 && m.isSelected(1) && m.isSelected(2));
    m.selectRect(0.7f, 1.f, 0.2f, 0.f);   // corners in any order
    CHECK(m.selectionCount() == 2);
    m.selectRect(0.2f, 0.4f, 0.7f, 0.6f);   // only the node at y 0.5
    CHECK(m.selectionCount() == 1 && m.isSelected(1));
    NodeMask base;
    base.set(0);
    m.selectRect(0.5f, 0.f, 0.7f, 1.f, SelectMode::Add, &base);
    CHECK(m.selectionCount() == 2 && m.isSelected(0) && m.isSelected(2));
    m.selectRect(0.f, 0.f, 1.f, 1.f, SelectMode::Toggle, &base);
    CHECK(!m.isSelected(0) && m.isSelected(1) && m.isSelected(3));
    m.selectRect(std::nanf(""), 0.f, 1.f, 1.f);
    CHECK(m.selectionCount() == 0);

    // Rotation 90°: node x 0.9 shows at 0.15, x 0 and x 1 at 0.25 (the seam), x 0.1 at 0.35.
    const Envelope e = makeEnv({{0, 0, 0, 0}, {0.1f, 0.3f, 0, 0}, {0.7f, 1, 0, 0}, {0.9f, 0.8f, 0, 0}, {1, 0.5f, 0, 0}});
    loadBase(f, e);
    f.setTiming(90.f, 0.f);
    CHECK_NEAR(m.nodePosition(3).x, 0.15f, 1e-6);
    CHECK_NEAR(m.nodePosition(0).x, 0.25f, 1e-6);
    CHECK_NEAR(m.nodePosition(4).x, 0.25f, 1e-6);
    CHECK_NEAR(m.nodePosition(2).x, 0.95f, 1e-6);
    m.selectRect(0.1f, 0.f, 0.4f, 1.f);
    CHECK(m.selectionCount() == 4);
    CHECK(m.isSelected(0) && m.isSelected(1) && m.isSelected(3) && m.isSelected(4) && !m.isSelected(2));
}

TEST_CASE(edit_add_and_delete_nodes)
{
    Fixture f;
    loadBase(f);
    EditorModel& m = f.model;
    const int i = m.addNode(0.45f, 0.2f);
    CHECK(i == 2);
    CHECK_NEAR(x(f, 2), 0.45f, 1e-6);
    CHECK_NEAR(y(f, 2), 0.2f, 1e-6);
    CHECK(m.selectionCount() == 1 && m.isSelected(2));
    CHECK(m.canUndo() && m.undoLabel() == "Add Node");
    // On the curve: the curve does not change.
    const Envelope before = m.editEnvelope();
    const int j = m.addNodeOnCurve(0.15f);
    CHECK(j == 1);
    CHECK(m.editEnvelope().size() == before.size() + 1);
    CHECK(ops::curveDistance(before, m.editEnvelope()) < 2e-5f);
    // Snap on: x lands on the grid (0.45 → 0.5), y from the pointer.
    f.setSnap(true);
    const int k = m.addNode(0.45f, 0.9f);
    CHECK(k >= 0 && x(f, k) == 0.5f);
    CHECK(m.addNode(0.45f, 0.9f, true) >= 0);   // Ctrl inverts snap: unsnapped
    CHECK(validEnvelope(m.editEnvelope(), "adds"));
    // Adding at the edges keeps the endpoints the endpoints.
    const int e0 = m.addNode(0.f, 0.5f, true);
    CHECK(e0 > 0 && x(f, e0) > 0.f);
    CHECK(m.editEnvelope().node(0).y == 0.f);
    // Delete: endpoints refused, selection follows the nodes.
    loadBase(f);
    m.select(2);
    CHECK(!m.deleteNode(0));
    CHECK(!m.deleteNode(3));
    CHECK(!m.deleteNode(17));
    CHECK(m.deleteNode(1));
    CHECK(m.editEnvelope().size() == 3);
    CHECK(m.isSelected(1) && m.selectionCount() == 1);   // the node formerly at index 2
    CHECK_NEAR(x(f, 1), 0.6f, 1e-7);
    m.selectAll();
    CHECK(m.deleteSelection());
    CHECK(m.editEnvelope().size() == 2);   // endpoints stay
    CHECK(!m.deleteSelection());
    // Full envelope: add fails cleanly.
    ops::NodeList full;
    for (int n = 0; n < Envelope::kMaxNodes; ++n)
        full.push_back(EnvNode{float(n) / float(Envelope::kMaxNodes - 1), 0.5f, 0.f, 0});
    loadBase(f, makeEnv(full));
    const int undoBefore = m.undoCount();
    CHECK(m.addNode(0.37f, 0.5f) == -1);
    CHECK(m.addNodeOnCurve(0.371f) == -1);
    CHECK(m.undoCount() == undoBefore);
    CHECK(m.addNode(std::nanf(""), 0.5f) == -1);
}

TEST_CASE(edit_drag_constraints)
{
    Fixture f;
    loadBase(f);
    EditorModel& m = f.model;
    // A single node never crosses its neighbours; it may meet them (a vertical step).
    CHECK(m.beginNodeDrag(1));
    CHECK(m.isSelected(1));
    m.updateNodeDrag(0.5f, 0.f);
    CHECK(x(f, 1) == 0.6f);
    m.updateNodeDrag(-0.5f, 0.f);
    CHECK(x(f, 1) == 0.f);
    m.updateNodeDrag(0.1f, 0.2f);
    CHECK_NEAR(x(f, 1), 0.4f, 1e-6);
    CHECK_NEAR(y(f, 1), 0.7f, 1e-6);
    m.updateNodeDrag(0.f, 5.f);   // y clamps
    CHECK(y(f, 1) == 1.f);
    CHECK(x(f, 1) == 0.3f);       // dx 0: x exactly unchanged
    CHECK(m.endGesture());
    CHECK(m.undoLabel() == "Move Nodes");
    CHECK(m.undoCount() == 1);

    // Endpoints move only in y.
    loadBase(f);
    m.select(0);
    CHECK(m.beginNodeDrag(0));
    m.updateNodeDrag(0.3f, 0.5f);
    CHECK(x(f, 0) == 0.f && y(f, 0) == 0.5f);
    m.endGesture();

    // A selection moves rigidly and stops at the first unselected neighbour (here the last node).
    loadBase(f);
    m.select(1);
    m.select(2, SelectMode::Add);
    CHECK(m.beginNodeDrag(2));
    m.updateNodeDrag(0.6f, -0.1f);
    CHECK_NEAR(x(f, 1), 0.7f, 1e-6);
    CHECK_NEAR(x(f, 2), 1.f, 1e-6);
    CHECK_NEAR(y(f, 1), 0.4f, 1e-6);
    CHECK_NEAR(y(f, 2), 0.9f, 1e-6);
    CHECK(validEnvelope(m.editEnvelope(), "rigid"));
    m.cancelGesture();
    CHECK(ops::sameNodes(m.editEnvelope(), kBase));
    CHECK(!m.canUndo());

    // Axis lock.
    loadBase(f);
    m.select(1);
    m.beginNodeDrag(1);
    m.updateNodeDrag(0.1f, 0.3f, DragOptions{false, AxisLock::Y});
    CHECK(x(f, 1) == 0.3f && y(f, 1) == 0.8f);
    m.updateNodeDrag(0.1f, 0.3f, DragOptions{false, AxisLock::X});
    CHECK_NEAR(x(f, 1), 0.4f, 1e-6);
    CHECK(y(f, 1) == 0.5f);
    m.updateNodeDrag(std::nanf(""), std::numeric_limits<float>::infinity());
    CHECK(validEnvelope(m.editEnvelope(), "nan drag"));
    m.endGesture();

    // Vertical steps: a stacked pair can only be pulled apart to the right by its right node.
    const Envelope step = makeEnv({{0, 1, 0, 0}, {0.5f, 1, 0, 0}, {0.5f, 0, 0, 0}, {1, 1, 0, 0}});
    loadBase(f, step);
    m.select(2);
    m.beginNodeDrag(2);
    m.updateNodeDrag(-0.2f, 0.f);
    CHECK(x(f, 2) == 0.5f);
    m.updateNodeDrag(0.2f, 0.f);
    CHECK_NEAR(x(f, 2), 0.7f, 1e-6);
    CHECK(x(f, 1) == 0.5f);
    m.endGesture();
    CHECK(!m.beginNodeDrag(-1) && !m.beginNodeDrag(40));
}

TEST_CASE(edit_drag_snapping_swing_rotate)
{
    Fixture f;
    const Envelope e = makeEnv({{0, 0, 0, 0}, {0.1f, 0.5f, 0, 0}, {0.9f, 1, 0, 0}, {1, 1, 0, 0}});
    loadBase(f, e);
    EditorModel& m = f.model;
    f.setSnap(true);
    m.select(1);
    m.beginNodeDrag(1);
    m.updateNodeDrag(0.1f, 0.f);    // 0.2 → nearest grid line 0.25
    CHECK(x(f, 1) == 0.25f);
    m.updateNodeDrag(0.1f, 0.f, DragOptions{true, AxisLock::None});   // Ctrl: free
    CHECK_NEAR(x(f, 1), 0.2f, 1e-6);
    m.updateNodeDrag(0.3f, 0.f);    // 0.4 → 0.5
    CHECK(x(f, 1) == 0.5f);
    m.endGesture();

    // Swing 100 %: lines shown at 0, 0.375, 0.5, 0.875; a drag to timeline 0.37 lands on the swung
    // line (node x 0.25), both straight and rotated.
    for (float rot : {0.f, 90.f, 200.f}) {
        loadBase(f, e);
        f.setTiming(rot, 100.f);
        const float t0 = m.nodePosition(1).x;
        float target = 0.37f + rot / 360.f;
        target -= std::floor(target);
        float dx = target - t0;
        if (dx < -0.5f)
            dx += 1.f;
        if (dx > 0.5f)
            dx -= 1.f;
        m.select(1);
        m.beginNodeDrag(1);
        m.updateNodeDrag(dx, 0.f);
        CHECK_MSG(x(f, 1) == 0.25f, "rot %g: x = %g", double(rot), double(x(f, 1)));
        float shown = 0.375f + rot / 360.f;
        shown -= std::floor(shown);
        CHECK_NEAR(m.nodePosition(1).x, shown, 1e-5);
        m.endGesture();
    }
    // Snap to the cycle end: the node meets the last node.
    loadBase(f, e);
    f.setTiming(0.f, 0.f);
    m.select(2);
    m.beginNodeDrag(2);
    m.updateNodeDrag(0.08f, 0.f);
    CHECK(x(f, 2) == 1.f);
    m.endGesture();
    CHECK(validEnvelope(m.editEnvelope(), "snap end"));
}

TEST_CASE(edit_drag_y_magnet_and_snap)
{
    Fixture f;
    loadBase(f);
    EditorModel& m = f.model;
    f.setSnap(false, false, 0.015f);
    m.select(1);
    m.beginNodeDrag(1);
    m.updateNodeDrag(0.f, 0.49f);
    CHECK(y(f, 1) == 1.f);          // 0.99 sticks to 1
    m.updateNodeDrag(0.f, -0.488f);
    CHECK(y(f, 1) == 0.f);          // 0.012 sticks to 0
    m.updateNodeDrag(0.f, 0.49f, DragOptions{true, AxisLock::None});
    CHECK_NEAR(y(f, 1), 0.99f, 1e-6);   // Ctrl: no magnet
    m.endGesture();
    f.setSnap(true, true, 0.f);
    m.beginNodeDrag(1);
    m.updateNodeDrag(0.f, 0.14f);   // 0.99 → hmm, from 0.99? origin is the committed 0.99
    CHECK(y(f, 1) == 1.f);
    m.updateNodeDrag(0.f, -0.6f);   // 0.39 → 0.5
    CHECK(y(f, 1) == 0.5f);
    m.endGesture();
}

TEST_CASE(edit_tension_drag_direction)
{
    Fixture f;
    loadBase(f);
    EditorModel& m = f.model;
    // Rising segment 0 (0 → 0.5, t = 0.3): raising the curve lowers the tension.
    const float mid0 = m.editEnvelope().evaluate(0.15f);
    CHECK(m.beginTensionDrag(0));
    m.updateTensionDrag(0.5f);
    CHECK_NEAR(m.editEnvelope().node(0).tension, -0.2f, 1e-6);
    CHECK(m.editEnvelope().evaluate(0.15f) > mid0);
    m.updateTensionDrag(10.f);
    CHECK(m.editEnvelope().node(0).tension == -1.f);
    CHECK(m.gestureInfo().tension == -1.f);
    m.endGesture();
    CHECK(m.undoLabel() == "Bend Segment");
    // Falling segment: raising the curve raises the tension.
    const Envelope fall = makeEnv({{0, 1, 0, 0}, {0.5f, 0, 0, 0}, {1, 1, 0, 0}});
    loadBase(f, fall);
    const float midF = m.editEnvelope().evaluate(0.25f);
    CHECK(m.beginTensionDrag(0));
    m.updateTensionDrag(0.4f);
    CHECK_NEAR(m.editEnvelope().node(0).tension, 0.4f, 1e-6);
    CHECK(m.editEnvelope().evaluate(0.25f) > midF);
    m.endGesture();
    // All selected segments bend together.
    const Envelope three = makeEnv({{0, 0, 0, 0}, {0.3f, 1, 0, 0}, {0.6f, 0, 0, 0}, {1, 1, 0, 0}});
    loadBase(f, three);
    m.selectAll();
    CHECK(m.beginTensionDrag(1, true));
    m.updateTensionDrag(0.5f);
    CHECK_NEAR(m.editEnvelope().node(0).tension, -0.5f, 1e-6);   // rising
    CHECK_NEAR(m.editEnvelope().node(1).tension, 0.5f, 1e-6);    // falling
    CHECK_NEAR(m.editEnvelope().node(2).tension, -0.5f, 1e-6);   // rising
    m.endGesture();
    // Vertical segments have nothing to bend.
    const Envelope step = makeEnv({{0, 1, 0, 0}, {0.5f, 1, 0, 0}, {0.5f, 0, 0, 0}, {1, 1, 0, 0}});
    loadBase(f, step);
    CHECK(!m.beginTensionDrag(1));
    CHECK(!m.beginTensionDrag(3));
    CHECK(m.setTension(0, 0.7f));
    CHECK(m.editEnvelope().node(0).tension == 0.7f);
    CHECK(!m.setTension(0, std::nanf("")));
    CHECK(m.setTension(0, 4.f) && m.editEnvelope().node(0).tension == 1.f);
    CHECK(m.straightenSegment(0) && m.editEnvelope().node(0).tension == 0.f);
    CHECK(!m.straightenSegment(0));   // no change, no step
    CHECK(m.undoLabel() == "Straighten Segment");
}

TEST_CASE(edit_nudge_and_typed_position)
{
    Fixture f;
    const Envelope e = makeEnv({{0, 0, 0, 0}, {0.1f, 0.5f, 0, 0}, {0.9f, 1, 0, 0}, {1, 1, 0, 0}});
    loadBase(f, e);
    EditorModel& m = f.model;
    CHECK(!m.nudgeSelection(1, 0, false));   // nothing selected
    m.select(1);
    CHECK(m.nudgeSelection(1, 0, false));
    CHECK_NEAR(x(f, 1), 0.35f, 1e-6);        // one grid cell
    CHECK(m.nudgeSelection(1, 1, true));
    CHECK_NEAR(x(f, 1), 0.38125f, 1e-6);     // 1/8 cell
    CHECK_NEAR(y(f, 1), 0.501f, 1e-6);       // 0.1 %
    CHECK(m.nudgeSelection(0, -1, false));
    CHECK_NEAR(y(f, 1), 0.491f, 1e-6);       // 1 %
    CHECK(m.undoCount() == 1);               // rapid repeats coalesce
    f.advance(2.0);
    CHECK(m.nudgeSelection(-1, 0, false));
    CHECK(m.undoCount() == 2);
    for (int i = 0; i < 10; ++i)
        m.nudgeSelection(-1, 0, false);
    CHECK(x(f, 1) == 0.f);                   // clamped at the first node
    CHECK(validEnvelope(m.editEnvelope(), "nudge"));
    m.undo();
    m.undo();
    CHECK(ops::sameNodes(m.editEnvelope(), e));

    CHECK(m.setNodePosition(1, 0.5f, 0.25f));
    CHECK_NEAR(x(f, 1), 0.5f, 1e-6);
    CHECK_NEAR(y(f, 1), 0.25f, 1e-6);
    CHECK(m.setNodePosition(1, 0.99f, 0.25f));
    CHECK_NEAR(x(f, 1), 0.9f, 1e-6);         // clamped at the neighbour
    CHECK(m.setNodePosition(0, 0.5f, 0.3f));
    CHECK(x(f, 0) == 0.f && y(f, 0) == 0.3f);
    CHECK(!m.setNodePosition(7, 0.5f, 0.3f));
    CHECK(!m.setNodePosition(1, std::nanf(""), 0.3f));
}

TEST_CASE(edit_link_awareness)
{
    Fixture f;
    EditorModel& m = f.model;
    const Envelope b = makeEnv({{0, 0.5f, 0, 0}, {1, 0.5f, 0, 0}});
    m.load(kBase, b, LoadHistory::Clear);
    m.setLinked(true);
    m.setActiveBand(Band::B);
    CHECK(m.activeBand() == Band::B);
    CHECK(m.editBand() == Band::A);             // linked: edits go to A
    CHECK(m.addNode(0.8f, 0.2f) >= 0);
    CHECK(m.envelope(Band::A).size() == 5);
    CHECK(m.envelope(Band::B).size() == 2);
    m.setLinked(false);
    CHECK(m.editBand() == Band::B);
    CHECK(m.addNode(0.8f, 0.2f) >= 0);
    CHECK(m.envelope(Band::B).size() == 3);
    // Unlinking from the editor copies A into B, emits the parameter, one undo step.
    m.setLinked(true);
    f.listener.clear();
    CHECK(m.setLinkedByUser(false));
    CHECK(!m.linked());
    CHECK(ops::sameNodes(m.envelope(Band::B), m.envelope(Band::A)));
    CHECK(f.listener.params.size() == 1 && f.listener.params[0].first == kick::kParamEnvLink
          && f.listener.params[0].second == 0.f);
    CHECK(m.undoLabel() == "Unlink Envelopes");
    CHECK(m.undo());
    CHECK(m.linked());
    CHECK(m.envelope(Band::B).size() == 3);
    CHECK(f.listener.params.back().second == 1.f);
    CHECK(m.redo());
    CHECK(!m.linked());
    CHECK(!m.setLinkedByUser(false));
}
