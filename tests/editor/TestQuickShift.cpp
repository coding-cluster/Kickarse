// Kickarse — editor tests: Quick Shift (group, range, shift clamping, snapping, lane drags).
#include "EditorTest.h"

using namespace et;

namespace {

constexpr uint32_t QS = kick::kNodeQuickShift;

// A dip (step down at 0.2) recovering by 0.45; the dip and recovery are the group.
const Envelope kDip = makeEnv({{0, 1, 0, 0}, {0.2f, 1, 0, QS}, {0.2f, 0, 0.4f, QS}, {0.45f, 1, 0, QS}, {1, 1, 0, 0}});

void load(Fixture& f, const Envelope& e)
{
    f.model.load(e, e, LoadHistory::Clear);
    f.listener.clear();
}

float x(const Fixture& f, int i)
{
    return f.model.editEnvelope().node(i).x;
}

} // namespace

TEST_CASE(qs_group_membership)
{
    Fixture f;
    load(f, Envelope());
    EditorModel& m = f.model;
    CHECK(m.quickShiftGroupSize() == 0);
    CHECK(!m.quickShiftRange().valid);
    CHECK(!m.toggleQuickShiftMember(0));   // endpoints can't be members
    CHECK(!m.toggleQuickShiftMember(2));
    CHECK(!m.toggleQuickShiftMember(9));
    CHECK(m.toggleQuickShiftMember(1));
    CHECK(m.isQuickShiftMember(1) && m.quickShiftGroupSize() == 1);
    CHECK(m.undoLabel() == "Quick Shift Group");
    const QuickShiftRange r = m.quickShiftRange();
    CHECK(r.valid && !r.explicitRange && r.start == 0.5f && r.end == 0.5f);
    CHECK(m.toggleQuickShiftMember(1));
    CHECK(m.quickShiftGroupSize() == 0);

    load(f, kDip);
    CHECK(m.clearQuickShift());
    m.selectAll();
    CHECK(m.setQuickShiftGroupFromSelection());
    CHECK(m.quickShiftGroupSize() == 3);          // endpoints skipped
    CHECK(!m.isQuickShiftMember(0) && !m.isQuickShiftMember(4));
    m.selectNone();
    m.selectQuickShiftGroup();
    CHECK(m.selectionCount() == 3);
    CHECK(m.clearQuickShift());
    CHECK(m.quickShiftGroupSize() == 0);
    CHECK(!m.clearQuickShift());                  // nothing to clear
    // Membership is saved with the envelope.
    CHECK(m.undo());
    CHECK(m.quickShiftGroupSize() == 3);
    CHECK(m.editEnvelope().serialize().find(",1;") != std::string::npos);
}

TEST_CASE(qs_shift_clamps_and_preserves)
{
    Fixture f;
    load(f, kDip);
    EditorModel& m = f.model;
    const QuickShiftRange r = m.quickShiftRange();
    CHECK(r.valid && r.start == 0.2f && r.end == 0.45f);
    CHECK(m.shiftGroup(0.1f));
    CHECK_NEAR(x(f, 1), 0.3f, 1e-6);
    CHECK_NEAR(x(f, 2), 0.3f, 1e-6);
    CHECK_NEAR(x(f, 3), 0.55f, 1e-6);
    CHECK(m.editEnvelope().node(2).tension == 0.4f);   // tensions and values travel unchanged
    CHECK(m.editEnvelope().node(2).y == 0.f);
    CHECK(m.undoLabel() == "Quick Shift");
    // Clamped between the non-members (minus the 0.001 margin).
    CHECK(m.shiftGroup(5.f));
    CHECK_NEAR(x(f, 3), 0.999f, 1e-6);
    CHECK_NEAR(x(f, 3) - x(f, 1), 0.25f, 1e-6);          // spacing kept
    CHECK(m.shiftGroup(-5.f));
    CHECK_NEAR(x(f, 1), 0.001f, 1e-6);
    CHECK(!m.shiftGroup(-0.1f));                          // already at the limit: no step
    CHECK(!m.shiftGroup(std::nanf("")));
    CHECK(validEnvelope(m.editEnvelope(), "qs clamp"));
    // Undo returns everything.
    while (m.canUndo())
        m.undo();
    CHECK(ops::sameNodes(m.editEnvelope(), kDip));

    // A non-contiguous group is clamped by the non-member in between.
    const Envelope gap = makeEnv({{0, 1, 0, 0}, {0.2f, 0, 0, QS}, {0.3f, 0.5f, 0, 0}, {0.5f, 0.2f, 0, QS}, {1, 1, 0, 0}});
    load(f, gap);
    CHECK(m.shiftGroup(0.5f));
    CHECK_NEAR(x(f, 1), 0.299f, 1e-6);
    CHECK_NEAR(x(f, 3), 0.599f, 1e-6);
    CHECK(x(f, 2) == 0.3f);
    // No group: nothing moves.
    load(f, Envelope());
    CHECK(!m.shiftGroup(0.1f));
    CHECK(!m.beginQuickShiftDrag(QuickShiftPart::Bar));
    CHECK(!m.beginQuickShiftDrag(QuickShiftPart::Start));
}

TEST_CASE(qs_shift_snapping)
{
    Fixture f;
    load(f, kDip);
    EditorModel& m = f.model;
    f.setSnap(true);   // 4 cells → ¼-cell steps of 0.0625
    CHECK(m.shiftGroup(0.1f));
    CHECK_NEAR(x(f, 1), 0.325f, 1e-6);
    CHECK(m.shiftGroup(0.02f, true));   // Ctrl: free
    CHECK_NEAR(x(f, 1), 0.345f, 1e-6);
    CHECK(!m.shiftGroup(0.02f));        // rounds to 0 steps
    // Swing: the first member follows the pointer in the timeline, the delta snaps in node x so
    // grid-aligned members stay grid-aligned.
    const Envelope grid = makeEnv({{0, 1, 0, 0}, {0.25f, 0, 0, QS}, {0.5f, 1, 0, QS}, {1, 1, 0, 0}});
    load(f, grid);
    f.setTiming(0.f, 100.f);
    CHECK(m.shiftGroup(0.1f));         // timeline 0.375 + 0.1 → node 0.45: Δ 0.2 → 3 quarter cells
    CHECK_NEAR(x(f, 1), 0.4375f, 1e-6);
    CHECK_NEAR(x(f, 2), 0.6875f, 1e-6);
}

TEST_CASE(qs_explicit_range_and_autogroup)
{
    Fixture f;
    const Envelope plain = makeEnv({{0, 1, 0, 0}, {0.1f, 0.5f, 0, 0}, {0.3f, 0, 0, 0}, {0.5f, 1, 0, 0}, {1, 1, 0, 0}});
    load(f, plain);
    EditorModel& m = f.model;
    CHECK(m.setQuickShiftRangeNode(0.2f, 0.6f));
    QuickShiftRange r = m.quickShiftRange();
    CHECK(r.valid && r.explicitRange && r.start == 0.2f && r.end == 0.6f);
    CHECK(m.quickShiftGroupSize() == 2 && m.isQuickShiftMember(2) && m.isQuickShiftMember(3));
    CHECK(m.undoLabel() == "Quick Shift Range");
    // The explicit range moves with the group and limits it to the cycle.
    CHECK(m.shiftGroup(0.2f));
    r = m.quickShiftRange();
    CHECK_NEAR(r.start, 0.4f, 1e-6);
    CHECK_NEAR(r.end, 0.8f, 1e-6);
    CHECK(m.shiftGroup(0.5f));
    r = m.quickShiftRange();
    CHECK_NEAR(r.end, 1.f, 1e-6);
    CHECK_NEAR(x(f, 3), 0.9f, 1e-6);
    // Timeline setter with rotation: the range is stored in node x.
    load(f, plain);
    f.setTiming(90.f, 0.f);
    CHECK(m.setQuickShiftRange(0.45f, 0.85f));   // node 0.2 … 0.6
    r = m.quickShiftRange();
    CHECK_NEAR(r.start, 0.2f, 1e-6);
    CHECK_NEAR(r.end, 0.6f, 1e-6);
    CHECK(m.setQuickShiftRange(0.85f, 0.45f, false));   // swapped corners
    CHECK(!m.setQuickShiftRange(std::nanf(""), 0.4f));
    // A range ending past the seam stops at the cycle end.
    CHECK(m.setQuickShiftRange(0.9f, 0.3f));
    r = m.quickShiftRange();
    CHECK_NEAR(r.start, 0.65f, 1e-6);
    CHECK(r.end == 1.f);
    // Toggling a member drops the explicit range (back to the group's extent).
    CHECK(m.toggleQuickShiftMember(1));
    CHECK(!m.quickShiftRange().explicitRange);
    // autoGroup re-derives membership from the range.
    load(f, plain);
    f.setTiming(0.f, 0.f);
    m.setQuickShiftRangeNode(0.05f, 0.35f, false);
    CHECK(m.quickShiftGroupSize() == 0);
    CHECK(m.autoGroupQuickShift());
    CHECK(m.quickShiftGroupSize() == 2 && m.isQuickShiftMember(1) && m.isQuickShiftMember(2));
}

TEST_CASE(qs_lane_drags_one_undo_step)
{
    Fixture f;
    load(f, kDip);
    EditorModel& m = f.model;
    CHECK(m.beginQuickShiftDrag(QuickShiftPart::Bar));
    m.updateQuickShiftDrag(0.05f);
    m.updateQuickShiftDrag(0.12f);
    m.updateQuickShiftDrag(0.08f);
    CHECK_NEAR(x(f, 1), 0.28f, 1e-6);
    CHECK_NEAR(m.gestureInfo().shift, 0.08f, 1e-6);
    CHECK(f.listener.lives() >= 1 && f.listener.finals() == 0);
    CHECK(m.endGesture());
    CHECK(f.listener.finals() == 1);
    CHECK(m.undoCount() == 1 && m.undoLabel() == "Quick Shift");
    m.undo();
    CHECK(ops::sameNodes(m.editEnvelope(), kDip));

    // End handle: grows the range and regroups.
    const Envelope plain = makeEnv({{0, 1, 0, 0}, {0.2f, 0, 0, QS}, {0.4f, 0.5f, 0, 0}, {0.6f, 1, 0, 0}, {1, 1, 0, 0}});
    load(f, plain);
    CHECK(m.beginQuickShiftDrag(QuickShiftPart::End));
    m.updateQuickShiftDrag(0.25f);   // 0.2 → 0.45
    CHECK(m.quickShiftGroupSize() == 2);
    CHECK_NEAR(m.quickShiftRange().end, 0.45f, 1e-6);
    m.updateQuickShiftDrag(-0.5f);   // cannot pass the start
    CHECK_NEAR(m.quickShiftRange().end, 0.2f, 1e-6);
    m.endGesture();
    CHECK(m.undoCount() == 1);       // same members, but the range is now explicit
    // Start handle with snapping.
    f.setSnap(true);
    CHECK(m.beginQuickShiftDrag(QuickShiftPart::Start));
    m.updateQuickShiftDrag(-0.12f);  // 0.08 → snaps to 0
    CHECK(m.quickShiftRange().start == 0.f);
    m.cancelGesture();
    f.setSnap(false);
    // New range dragged in an empty lane.
    load(f, Envelope());
    CHECK(m.beginQuickShiftDrag(QuickShiftPart::NewRange, 0.3f));
    m.updateQuickShiftDrag(0.3f);
    CHECK_NEAR(m.quickShiftRange().start, 0.3f, 1e-6);
    CHECK_NEAR(m.quickShiftRange().end, 0.6f, 1e-6);
    CHECK(m.isQuickShiftMember(1));
    m.updateQuickShiftDrag(-0.2f);   // dragging left of the anchor
    CHECK_NEAR(m.quickShiftRange().start, 0.1f, 1e-6);
    CHECK_NEAR(m.quickShiftRange().end, 0.3f, 1e-6);
    CHECK(!m.isQuickShiftMember(1));
    CHECK(m.endGesture());
    CHECK(m.undoLabel() == "Quick Shift Range");
}

TEST_CASE(qs_spans_and_lane_hits)
{
    Fixture f;
    load(f, kDip);
    EditorModel& m = f.model;
    Vec2 spans[2];
    CHECK(m.quickShiftSpans(spans) == 1);
    CHECK_NEAR(spans[0].x, 0.2f, 1e-6);
    CHECK_NEAR(spans[0].y, 0.45f, 1e-6);
    const ViewTransform lane{0.f, 100.f, 1000.f, 16.f};
    CHECK(m.hitTestQuickShift(lane, 300.f, 108.f).kind == HitKind::QuickShiftBar);
    CHECK(m.hitTestQuickShift(lane, 202.f, 108.f).kind == HitKind::QuickShiftStart);
    CHECK(m.hitTestQuickShift(lane, 449.f, 108.f).kind == HitKind::QuickShiftEnd);
    CHECK(m.hitTestQuickShift(lane, 700.f, 108.f).kind == HitKind::QuickShiftLane);
    CHECK(m.hitTestQuickShift(lane, 300.f, 50.f).kind == HitKind::None);
    CHECK(m.hitTestQuickShift(lane, std::nanf(""), 108.f).kind == HitKind::None);
    // Rotated so the range wraps around the plot edge: two pieces.
    f.setTiming(270.f, 0.f);   // rotate 0.75
    CHECK(m.quickShiftSpans(spans) == 2);
    CHECK_NEAR(spans[0].x, 0.95f, 1e-6);
    CHECK_NEAR(spans[0].y, 1.f, 1e-6);
    CHECK_NEAR(spans[1].x, 0.f, 1e-6);
    CHECK_NEAR(spans[1].y, 0.2f, 1e-6);
    const Vec2 edges = m.quickShiftEdges();
    CHECK_NEAR(edges.x, 0.95f, 1e-6);
    CHECK_NEAR(edges.y, 0.2f, 1e-6);
    CHECK(m.hitTestQuickShift(lane, 100.f, 108.f).kind == HitKind::QuickShiftBar);
    CHECK(m.hitTestQuickShift(lane, 975.f, 108.f).kind == HitKind::QuickShiftBar);
    CHECK(m.hitTestQuickShift(lane, 500.f, 108.f).kind == HitKind::QuickShiftLane);
    load(f, Envelope());
    CHECK(m.quickShiftSpans(spans) == 0);
    CHECK(m.hitTestQuickShift(lane, 500.f, 108.f).kind == HitKind::QuickShiftLane);
}

TEST_CASE(qs_view_state_roundtrip)
{
    Fixture f;
    load(f, kDip);
    EditorModel& m = f.model;
    m.setQuickShiftRangeNode(0.15f, 0.5f);
    m.setActiveBand(Band::B);
    const std::string s = m.saveViewState();
    Fixture g;
    g.model.load(kDip, kDip, LoadHistory::Clear);
    CHECK(g.model.restoreViewState(s));
    CHECK(g.model.activeBand() == Band::B);
    g.model.setActiveBand(Band::A);
    const QuickShiftRange r = g.model.quickShiftRange();
    CHECK(r.explicitRange && r.start == 0.15f && r.end == 0.5f);
    CHECK(!g.model.restoreViewState("band=7;qsA=0.5,0.1;junk"));
    CHECK(g.model.restoreViewState(""));
    // Loading a different envelope whose group lies outside the range drops the explicit range;
    // restoring the same project keeps it.
    g.model.load(kDip, kDip);
    CHECK(g.model.quickShiftRange().explicitRange);
    g.model.load(makeEnv({{0, 1, 0, 0}, {0.8f, 0, 0, QS}, {1, 1, 0, 0}}), kDip);
    CHECK(!g.model.quickShiftRange().explicitRange);
}
