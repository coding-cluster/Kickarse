// Kickarse — editor tests: undo/redo, transactions, parameter coalescing, notifications, load.
#include "EditorTest.h"

using namespace et;

namespace {

using kick::kParamDepth;
using kick::kParamSwing;

void load(Fixture& f, const Envelope& e)
{
    f.model.load(e, e, LoadHistory::Clear);
    f.listener.clear();
}

} // namespace

TEST_CASE(history_undo_redo_sequence)
{
    Fixture f;
    load(f, Envelope());
    EditorModel& m = f.model;
    std::vector<Envelope> states{m.editEnvelope()};
    CHECK(m.addNode(0.2f, 0.3f) >= 0);
    states.push_back(m.editEnvelope());
    CHECK(m.setTension(0, -0.5f));
    states.push_back(m.editEnvelope());
    CHECK(m.invert());
    states.push_back(m.editEnvelope());
    CHECK(m.deleteNode(1));
    states.push_back(m.editEnvelope());
    CHECK(m.undoCount() == 4 && !m.canRedo());
    CHECK(m.undoLabel() == "Delete Node");
    for (int i = 3; i >= 1; --i) {
        CHECK(m.undo());
        CHECK(ops::sameNodes(m.editEnvelope(), states[size_t(i)]));
    }
    CHECK(m.redoLabel() == "Bend Segment");
    CHECK(m.redo());
    CHECK(ops::sameNodes(m.editEnvelope(), states[2]));
    CHECK(m.undo() && m.undo());
    CHECK(ops::sameNodes(m.editEnvelope(), states[0]));
    CHECK(!m.undo());
    CHECK(m.canRedo() && m.redoCount() == 4);
    // A new edit clears the redo stack.
    CHECK(m.addNode(0.7f, 0.1f) >= 0);
    CHECK(!m.canRedo());
    // Undo restores the selection and the active band together with the envelope.
    load(f, Envelope());
    m.select(1);
    m.beginNodeDrag(1);
    m.updateNodeDrag(0.1f, 0.f);
    m.endGesture();
    m.selectNone();
    m.setActiveBand(Band::B);
    CHECK(m.undo());
    CHECK(m.activeBand() == Band::A);
    CHECK(m.isSelected(1));
    CHECK(m.redo());
    CHECK(m.activeBand() == Band::B);
}

TEST_CASE(history_transactions)
{
    Fixture f;
    load(f, Envelope());
    EditorModel& m = f.model;
    m.beginTransaction("Make Wobble");
    CHECK(m.inTransaction() && m.gesture() == GestureKind::Custom);
    m.addNode(0.2f, 0.3f);
    m.addNode(0.7f, 0.6f);
    m.beginTransaction("Inner");
    m.invert();
    CHECK(m.commitTransaction() == false);   // inner commit records nothing on its own
    CHECK(m.undoLabel() == "Make Wobble");
    CHECK(m.commitTransaction());
    CHECK(m.undoCount() == 1 && m.undoLabel() == "Make Wobble");
    CHECK(m.undo());
    CHECK(ops::sameNodes(m.editEnvelope(), Envelope()));
    // Cancel restores and records nothing.
    m.beginTransaction("Oops");
    m.addNode(0.2f, 0.3f);
    m.cancelTransaction();
    CHECK(ops::sameNodes(m.editEnvelope(), Envelope()));
    CHECK(m.undoCount() == 0 && m.redoCount() == 1);
    // Revert keeps the transaction open (live "from origin" editing of any operation).
    m.beginTransaction("Scale drag");
    for (float k : {0.9f, 0.7f, 0.5f}) {
        m.revertTransaction();
        m.scaleY(k);
    }
    CHECK(m.commitTransaction());
    CHECK_NEAR(m.editEnvelope().node(0).y, 0.5f, 1e-6);   // 1 − 0.5·(1 − 0), not compounded
    // Empty transaction: nothing.
    const int n = m.undoCount();
    m.beginTransaction("Nothing");
    CHECK(!m.commitTransaction());
    CHECK(m.undoCount() == n);
    CHECK(!m.commitTransaction());   // none open
    m.cancelTransaction();           // none open: harmless
}

TEST_CASE(history_gestures_and_undo_mid_drag)
{
    Fixture f;
    load(f, Envelope());
    EditorModel& m = f.model;
    m.select(1);
    m.beginNodeDrag(1);
    m.updateNodeDrag(0.1f, -0.2f);
    CHECK(m.canUndo() && m.undoLabel() == "Move Nodes");
    CHECK(m.undo());   // ends the drag (one step) and undoes it
    CHECK(m.gesture() == GestureKind::None);
    CHECK(ops::sameNodes(m.editEnvelope(), Envelope()));
    CHECK(m.canRedo());
    // Another edit while dragging ends the drag first.
    m.beginNodeDrag(1);
    m.updateNodeDrag(0.1f, 0.f);
    m.invert();
    CHECK(m.gesture() == GestureKind::None);
    CHECK(m.undoCount() == 2);
    // Band switch ends it as well.
    m.beginNodeDrag(1);
    m.updateNodeDrag(-0.1f, 0.f);
    m.setActiveBand(Band::B);
    CHECK(m.gesture() == GestureKind::None && m.undoCount() == 3);
    // updates without a matching gesture are ignored.
    m.updateNodeDrag(0.3f, 0.3f);
    m.updateTensionDrag(1.f);
    m.updateQuickShiftDrag(0.2f);
    m.updateStretch(0.2f);
    m.updateLine(0.5f, 0.5f);
    CHECK(m.undoCount() == 3);
    CHECK(!m.endGesture());
}

TEST_CASE(history_bound)
{
    Fixture f;
    load(f, Envelope());
    EditorModel& m = f.model;
    CHECK(m.historyLimit() >= 200);
    m.setHistoryLimit(200);
    for (int i = 0; i < 260; ++i)
        m.setTension(0, (i % 2) ? 0.5f : -0.5f);
    CHECK(m.undoCount() == 200);
    int undone = 0;
    while (m.undo())
        ++undone;
    CHECK(undone == 200);
    CHECK(validEnvelope(m.editEnvelope(), "bounded"));
    m.setHistoryLimit(10);
    CHECK(m.undoCount() == 0 && m.redoCount() == 200);
    while (m.redo()) {
    }
    m.setHistoryLimit(10);
    CHECK(m.undoCount() == 10);
    m.setHistoryLimit(-3);
    CHECK(m.historyLimit() == 1 && m.undoCount() == 1);
    m.clearHistory();
    CHECK(!m.canUndo() && !m.canRedo());
}

TEST_CASE(history_parameter_entries_and_coalescing)
{
    Fixture f;
    load(f, Envelope());
    EditorModel& m = f.model;
    m.recordParameterChange(kParamDepth, 100.f, 90.f);
    CHECK(m.undoCount() == 1 && m.undoLabel() == "Change Depth");
    f.advance(0.1);
    m.recordParameterChange(kParamDepth, 90.f, 80.f);   // wheel steps merge
    f.advance(0.1);
    m.recordParameterChange(kParamDepth, 80.f, 70.f);
    CHECK(m.undoCount() == 1);
    CHECK(m.undo());
    CHECK(f.listener.params.back().first == kParamDepth && f.listener.params.back().second == 100.f);
    CHECK(m.redo());
    CHECK(f.listener.params.back().second == 70.f);
    // After the window a new step starts; another parameter never merges.
    f.advance(1.0);
    m.recordParameterChange(kParamDepth, 70.f, 60.f);
    m.recordParameterChange(kParamSwing, 0.f, 20.f);
    CHECK(m.undoCount() == 3);
    // A knob gesture merges however slow it is, and ends the merge when released.
    m.beginParameterGesture(kParamSwing);
    f.advance(2.0);
    m.recordParameterChange(kParamSwing, 20.f, 30.f);
    f.advance(2.0);
    m.recordParameterChange(kParamSwing, 30.f, 40.f);
    m.endParameterGesture(kParamSwing);
    CHECK(m.undoCount() == 4);
    m.recordParameterChange(kParamSwing, 40.f, 50.f);
    CHECK(m.undoCount() == 5);
    // Returning to the starting value cancels the merged step.
    f.advance(0.1);
    m.recordParameterChange(kParamSwing, 50.f, 40.f);
    CHECK(m.undoCount() == 4);
    m.recordParameterChange(kParamSwing, 40.f, 40.f);     // no change
    CHECK(m.timing().swingPercent == 40.f);                // swing edits keep the model's mapping in sync
    m.recordParameterChange(kParamSwing, std::nanf(""), 1.f);
    CHECK(m.undoCount() == 4);
    // Parameters and envelope edits in one transaction undo together.
    f.listener.clear();
    m.beginTransaction("Compound");
    m.addNode(0.2f, 0.2f);
    m.changeParameter(kParamDepth, 60.f, 50.f);
    CHECK(f.listener.params.size() == 1 && f.listener.params[0].second == 50.f);
    m.commitTransaction();
    CHECK(m.undoLabel() == "Compound");
    CHECK(m.undo());
    CHECK(f.listener.params.back().second == 60.f);
    CHECK(ops::sameNodes(m.editEnvelope(), Envelope()));
    // Cancelling a transaction reverts its parameters.
    m.beginTransaction("Cancel me");
    m.changeParameter(kParamDepth, 60.f, 10.f);
    m.cancelTransaction();
    CHECK(f.listener.params.back().first == kParamDepth && f.listener.params.back().second == 60.f);
}

TEST_CASE(history_notifications)
{
    Fixture f;
    load(f, Envelope());
    EditorModel& m = f.model;
    // One operation: exactly one final notification, no live ones, for the changed band only.
    CHECK(m.addNode(0.3f, 0.3f) >= 0);
    CHECK(f.listener.finals() == 1 && f.listener.lives() == 0);
    CHECK(f.listener.envEvents[0].band == Band::A);
    CHECK(ops::sameNodes(f.listener.envEvents[0].env, m.envelope(Band::A)));
    CHECK(f.listener.historyEvents >= 1 && f.listener.stateEvents >= 1);
    // A gesture: live updates while dragging, one final at the end.
    f.listener.clear();
    m.select(1);
    m.beginNodeDrag(1);
    for (int i = 1; i <= 5; ++i)
        m.updateNodeDrag(0.02f * float(i), 0.f);
    CHECK(f.listener.lives() == 5 && f.listener.finals() == 0);
    m.endGesture();
    CHECK(f.listener.finals() == 1);
    // Cancelled after live updates: a final with the restored envelope.
    const Envelope before = m.editEnvelope();
    f.listener.clear();
    m.beginNodeDrag(1);
    m.updateNodeDrag(0.1f, 0.f);
    m.cancelGesture();
    CHECK(f.listener.finals() == 1);
    CHECK(ops::sameNodes(f.listener.envEvents.back().env, before));
    // Undo/redo notify.
    f.listener.clear();
    m.undo();
    CHECK(f.listener.finals() == 1);
    // A no-op edit sends nothing.
    f.listener.clear();
    CHECK(!m.setTension(0, m.editEnvelope().node(0).tension));
    CHECK(f.listener.envEvents.empty());
    m.setListener(nullptr);
    CHECK(m.addNode(0.8f, 0.5f) >= 0);   // no listener: fine
}

TEST_CASE(history_load_modes)
{
    Fixture f;
    EditorModel& m = f.model;
    const Envelope a = makeEnv({{0, 0.2f, 0, 0}, {0.4f, 1, 0, 0}, {1, 1, 0, 0}});
    // Keep (default): no undo step, no notification.
    CHECK(m.load(a, a));
    CHECK(!m.canUndo());
    CHECK(f.listener.envEvents.empty());
    CHECK(!m.load(a, a));   // identical: ignored
    // An edit then an undo-able load.
    m.addNode(0.7f, 0.5f);
    f.listener.clear();
    CHECK(m.load(Envelope(), Envelope(), LoadHistory::Record, true));
    CHECK(m.undoLabel() == "Load");
    CHECK(f.listener.finals() == 2);
    CHECK(m.undo());
    CHECK(m.envelope(Band::A).size() == 4);
    CHECK(m.envelope(Band::B).size() == 3);
    // Clear drops the history.
    CHECK(m.load(Envelope::flat(), Envelope::flat(), LoadHistory::Clear));
    CHECK(!m.canUndo() && !m.canRedo());
    // From state strings.
    CHECK(m.loadState(Band::B, "KE1 0,0,0,0;1,1,0,0"));
    CHECK(m.envelope(Band::B).size() == 2 && m.envelope(Band::B).node(0).y == 0.f);
    CHECK(!m.loadState(Band::B, "KE9 garbage"));
    // Selection on a reloaded band is cleared; a running gesture is cancelled.
    m.setActiveBand(Band::A);
    m.selectAll();
    m.beginNodeDrag(0);
    m.updateNodeDrag(0.f, -0.5f);
    CHECK(m.loadBand(Band::A, a));
    CHECK(m.gesture() == GestureKind::None);
    CHECK(m.selectionCount() == 0);
    CHECK(ops::sameNodes(m.envelope(Band::A), a));
    // Echoes of our own live pushes during a drag are ignored.
    m.select(1);
    m.beginNodeDrag(1);
    m.updateNodeDrag(0.1f, 0.f);
    const Envelope live1 = m.editEnvelope();
    m.updateNodeDrag(0.2f, 0.f);
    CHECK(!m.loadBand(Band::A, live1));
    CHECK(m.gesture() == GestureKind::MoveNodes);
    m.endGesture();
    // Invalid envelopes are normalised on load.
    Envelope junk = Envelope::flat();
    junk.node(0).y = 7.f;
    junk.node(1).x = 0.3f;
    CHECK(m.loadBand(Band::A, junk));
    CHECK(validEnvelope(m.envelope(Band::A), "junk load"));
}
