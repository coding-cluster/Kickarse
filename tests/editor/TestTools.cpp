// Kickarse — editor tests: line and pencil tools (splicing, steps, seam, swing).
#include "EditorTest.h"

using namespace et;

namespace {

void load(Fixture& f, const Envelope& e)
{
    f.model.load(e, e, LoadHistory::Clear);
    f.listener.clear();
}

bool drawLine(EditorModel& m, float t0, float v0, float t1, float v1, DragOptions opt = {})
{
    if (!m.beginLine(t0, v0, opt))
        return false;
    m.updateLine(0.5f * (t0 + t1), 0.5f * (v0 + v1), opt);
    m.updateLine(t1, v1, opt);
    return m.endGesture();
}

void drawStroke(EditorModel& m, const std::vector<Vec2>& pts)
{
    m.beginPencil(pts.front().x, pts.front().y);
    for (size_t i = 1; i < pts.size(); ++i)
        m.addPencilPoint(pts[i].x, pts[i].y);
    m.endGesture();
}

std::vector<Vec2> segment(float t0, float v0, float t1, float v1, int n = 60)
{
    std::vector<Vec2> pts;
    for (int i = 0; i <= n; ++i) {
        const float u = float(i) / float(n);
        pts.push_back(Vec2{t0 + (t1 - t0) * u, v0 + (v1 - v0) * u});
    }
    return pts;
}

} // namespace

TEST_CASE(line_basic_splice)
{
    Fixture f;
    load(f, Envelope::flat());
    EditorModel& m = f.model;
    CHECK(drawLine(m, 0.2f, 0.2f, 0.6f, 0.8f));
    const Envelope& e = m.editEnvelope();
    CHECK(validEnvelope(e, "line"));
    CHECK(e.size() == 4);
    CHECK_NEAR(m.valueAt(0.4f), 0.5f, 1e-5);
    CHECK_NEAR(e.evaluateLeft(0.2f), 0.2f, 1e-6);   // continuous: neighbours meet the line's ends
    CHECK_NEAR(m.valueAt(0.1f), 0.6f, 1e-5);
    CHECK_NEAR(m.valueAt(0.8f), 0.9f, 1e-5);
    CHECK(m.selectionCount() == 2 && m.isSelected(1) && m.isSelected(2));
    CHECK(m.undoCount() == 1 && m.undoLabel() == "Draw Line");
    // Right to left gives the same line.
    load(f, Envelope::flat());
    CHECK(drawLine(m, 0.6f, 0.8f, 0.2f, 0.2f));
    CHECK_NEAR(m.valueAt(0.4f), 0.5f, 1e-5);
    // Nodes strictly inside the span are replaced; tension of the line is 0.
    const Envelope bumpy = makeEnv({{0, 1, 0, 0}, {0.3f, 0, 0.8f, 0}, {0.35f, 1, -0.5f, 0}, {0.4f, 0, 0, 0}, {1, 1, 0, 0}});
    load(f, bumpy);
    CHECK(drawLine(m, 0.25f, 0.5f, 0.45f, 0.5f));
    for (int i = 0; i < m.editEnvelope().size(); ++i) {
        const EnvNode& n = m.editEnvelope().node(i);
        CHECK(!(n.x > 0.25f && n.x < 0.45f));
        if (n.x == 0.25f)
            CHECK(n.tension == 0.f);
    }
    CHECK_NEAR(m.valueAt(0.33f), 0.5f, 1e-5);
    // Full width: only the endpoints' y change.
    load(f, Envelope());
    CHECK(drawLine(m, 0.f, 0.1f, 1.f, 0.9f));
    CHECK(m.editEnvelope().size() == 2);
    CHECK_NEAR(m.valueAt(0.5f), 0.5f, 1e-5);
}

TEST_CASE(line_vertical_step_and_seam)
{
    Fixture f;
    load(f, Envelope::flat());
    EditorModel& m = f.model;
    CHECK(drawLine(m, 0.5f, 0.2f, 0.5f, 0.9f));
    const Envelope& e = m.editEnvelope();
    CHECK_NEAR(e.evaluateLeft(0.5f), 0.2f, 1e-6);
    CHECK_NEAR(e.evaluate(0.5f), 0.9f, 1e-6);
    CHECK_NEAR(m.valueAt(0.25f), 0.6f, 1e-5);
    CHECK_NEAR(m.valueAt(0.75f), 0.95f, 1e-5);
    // A click without a drag changes nothing and records nothing.
    load(f, Envelope::flat());
    CHECK(m.beginLine(0.3f, 0.3f));
    CHECK(!m.endGesture());
    CHECK(ops::sameNodes(m.editEnvelope(), Envelope::flat()) && !m.canUndo());

    // Rotation 90°: a line from timeline 0.1 to 0.5 crosses the seam at 0.25 and is split there.
    f.setTiming(90.f, 0.f);
    load(f, Envelope::flat());
    CHECK(drawLine(m, 0.1f, 0.2f, 0.5f, 0.6f));
    CHECK(validEnvelope(m.editEnvelope(), "seam line"));
    for (int i = 1; i < 40; ++i) {
        const float t = 0.1f + 0.4f * float(i) / 40.f;
        CHECK_NEAR(m.valueAt(t), 0.2f + (t - 0.1f), 2e-5);
    }
    CHECK_NEAR(m.editEnvelope().node(0).y, 0.35f, 1e-5);
    CHECK_NEAR(m.editEnvelope().node(m.editEnvelope().size() - 1).y, 0.35f, 1e-5);
    // A vertical line exactly on the seam sets the cycle end (arrival) and start (departure).
    load(f, Envelope::flat());
    CHECK(drawLine(m, 0.25f, 0.3f, 0.25f, 0.7f));
    CHECK(m.editEnvelope().size() == 2);
    CHECK_NEAR(m.editEnvelope().node(1).y, 0.3f, 1e-6);
    CHECK_NEAR(m.editEnvelope().node(0).y, 0.7f, 1e-6);
}

TEST_CASE(line_swing_kinks_and_snap)
{
    Fixture f;
    f.setTiming(0.f, 100.f);
    load(f, Envelope::flat());
    EditorModel& m = f.model;
    CHECK(drawLine(m, 0.f, 0.f, 1.f, 1.f));
    // Straight in the timeline despite swing: nodes on the kinks.
    for (int i = 0; i <= 50; ++i) {
        const float t = float(i) / 50.f;
        if (t < 1.f)
            CHECK_NEAR(m.valueAt(t), t, 2e-5);
    }
    CHECK(m.editEnvelope().size() == 5);
    // Snap: the ends land on (swung) grid lines.
    f.setTiming(0.f, 0.f);
    f.setSnap(true);
    load(f, Envelope::flat());
    CHECK(drawLine(m, 0.23f, 0.5f, 0.73f, 0.5f));
    CHECK(m.gestureInfo().kind == GestureKind::None);
    bool at25 = false, at75 = false;
    for (int i = 0; i < m.editEnvelope().size(); ++i) {
        at25 = at25 || m.editEnvelope().node(i).x == 0.25f;
        at75 = at75 || m.editEnvelope().node(i).x == 0.75f;
    }
    CHECK(at25 && at75);
    // Live preview and cancel.
    load(f, Envelope::flat());
    m.beginLine(0.1f, 0.1f);
    m.updateLine(0.6f, 0.1f);
    CHECK(m.gesture() == GestureKind::Line);
    CHECK(m.editEnvelope().size() == 3);
    CHECK(f.listener.lives() >= 1);
    CHECK_NEAR(m.gestureInfo().lineStart.x, 0.f, 1e-6);   // 0.1 snapped to the cycle start
    CHECK_NEAR(m.gestureInfo().lineEnd.x, 0.5f, 1e-6);
    m.cancelGesture();
    CHECK(ops::sameNodes(m.editEnvelope(), Envelope::flat()));
    CHECK(!m.canUndo());
    CHECK(!m.beginLine(std::nanf(""), 0.f));
}

TEST_CASE(pencil_basic_and_overwrite)
{
    Fixture f;
    load(f, Envelope::flat());
    EditorModel& m = f.model;
    drawStroke(m, segment(0.2f, 0.3f, 0.6f, 0.3f));
    CHECK(validEnvelope(m.editEnvelope(), "pencil"));
    CHECK_NEAR(m.valueAt(0.4f), 0.3f, 0.02);
    CHECK_NEAR(m.valueAt(0.8f), 0.65f, 0.02);    // the right neighbour stretches to meet the stroke
    CHECK_NEAR(m.valueAt(0.1f), 0.65f, 0.02);
    CHECK(m.undoCount() == 1 && m.undoLabel() == "Draw");
    // Drawing back over a region replaces it (the later stroke wins).
    load(f, Envelope::flat());
    std::vector<Vec2> pts = segment(0.2f, 0.3f, 0.6f, 0.3f);
    for (const Vec2& p : segment(0.6f, 0.7f, 0.4f, 0.7f))
        pts.push_back(p);
    drawStroke(m, pts);
    CHECK_NEAR(m.valueAt(0.3f), 0.3f, 0.03);
    CHECK_NEAR(m.valueAt(0.5f), 0.7f, 0.03);
    // A smooth freehand curve is fitted within about the tolerance.
    load(f, Envelope::flat());
    std::vector<Vec2> sine;
    for (int i = 0; i <= 300; ++i) {
        const float t = 0.1f + 0.8f * float(i) / 300.f;
        sine.push_back(Vec2{t, 0.5f + 0.4f * std::sin(t * 12.f)});
    }
    drawStroke(m, sine);
    float worst = 0.f;
    for (int i = 1; i < 100; ++i) {
        const float t = 0.1f + 0.8f * float(i) / 100.f;
        worst = std::max(worst, std::fabs(m.valueAt(t) - (0.5f + 0.4f * std::sin(t * 12.f))));
    }
    CHECK_MSG(worst < 0.04f, "pencil error %g", double(worst));
    CHECK(m.editEnvelope().size() < 40);
    // A tap does nothing.
    load(f, Envelope::flat());
    drawStroke(m, {Vec2{0.5f, 0.2f}, Vec2{0.5f, 0.8f}});
    CHECK(ops::sameNodes(m.editEnvelope(), Envelope::flat()) && !m.canUndo());
    CHECK(m.pencilStroke().empty());
}

TEST_CASE(pencil_across_seam_and_swing)
{
    Fixture f;
    f.setTiming(90.f, 50.f);
    load(f, Envelope::flat());
    EditorModel& m = f.model;
    drawStroke(m, segment(0.1f, 0.5f, 0.4f, 0.5f));
    CHECK(validEnvelope(m.editEnvelope(), "pencil seam"));
    for (float t : {0.15f, 0.2f, 0.24f, 0.26f, 0.3f, 0.35f})
        CHECK_NEAR(m.valueAt(t), 0.5f, 0.02);
    // The preview stroke is available while drawing.
    m.beginPencil(0.5f, 0.5f);
    m.addPencilPoint(0.6f, 0.4f);
    m.addPencilPoint(std::nanf(""), 0.4f);   // ignored
    CHECK(m.pencilStroke().size() == 2);
    CHECK(m.gesture() == GestureKind::Pencil);
    m.cancelGesture();
    CHECK(m.pencilStroke().empty());
}
