// Kickarse — editor tests: drawing geometry (curve polyline, grid, handles) and hit testing.
#include "EditorTest.h"

using namespace et;

namespace {

const Envelope kShape = makeEnv({{0, 0, 0.5f, 0}, {0.3f, 0.6f, -0.3f, 0}, {0.3f, 0.2f, 0.f, 0}, {0.7f, 1, 0, 0}, {1, 1, 0, 0}});
const ViewTransform kPlot{100.f, 50.f, 800.f, 200.f};

// Linear interpolation of the polyline at timeline t (last point wins at vertical jumps).
float polyAt(const std::vector<Vec2>& pts, float t)
{
    for (size_t i = 1; i < pts.size(); ++i) {
        const Vec2& a = pts[i - 1];
        const Vec2& b = pts[i];
        if (t >= a.x && t < b.x)
            return b.x > a.x ? a.y + (b.y - a.y) * (t - a.x) / (b.x - a.x) : b.y;
    }
    return pts.back().y;
}

} // namespace

TEST_CASE(geometry_curve_polyline_matches_value)
{
    for (float rot : {0.f, 45.f, 180.f, 300.f}) {
        for (float swing : {0.f, 80.f}) {
            Fixture f;
            f.model.load(kShape, kShape, LoadHistory::Clear);
            f.setTiming(rot, swing);
            std::vector<Vec2> pts;
            f.model.buildCurve(Band::A, 800.f, pts, 1.f);
            CHECK(pts.size() > 10);
            CHECK(pts.front().x == 0.f && pts.back().x == 1.f);
            bool sorted = true;
            for (size_t i = 1; i < pts.size(); ++i)
                sorted = sorted && pts[i].x >= pts[i - 1].x;
            CHECK(sorted);
            float worst = 0.f;
            for (int i = 0; i < 997; ++i) {
                const float t = (float(i) + 0.5f) / 997.f;
                worst = std::max(worst, std::fabs(polyAt(pts, t) - f.model.valueAt(t)));
            }
            CHECK_MSG(worst < 0.02f, "rot %g swing %g: polyline off by %g", double(rot), double(swing), double(worst));
        }
    }
    // Rotation 0: the last node at the right edge; rotated: the seam jump at timeline = rotation.
    Fixture f;
    f.model.load(Envelope(), Envelope(), LoadHistory::Clear);
    std::vector<Vec2> pts;
    f.model.buildCurve(Band::A, 400.f, pts);
    CHECK(pts.back().x == 1.f && pts.back().y == 1.f && pts.front().y == 0.f);
    f.setTiming(90.f, 0.f);
    f.model.buildCurve(Band::A, 400.f, pts);
    bool jump = false;
    for (size_t i = 1; i < pts.size(); ++i)
        jump = jump || (std::fabs(pts[i].x - 0.25f) < 1e-6f && pts[i].x == pts[i - 1].x && pts[i].y == 0.f
                        && pts[i - 1].y == 1.f);
    CHECK(jump);
    f.model.buildCurve(Band::A, std::nanf(""), pts, -3.f);   // degenerate inputs are clamped
    CHECK(!pts.empty());
}

TEST_CASE(geometry_grid_lines_and_handles)
{
    Fixture f;
    EditorModel& m = f.model;
    m.load(kShape, kShape, LoadHistory::Clear);
    std::vector<GridLine> lines;
    m.gridLines(lines);
    CHECK(lines.size() == 4);
    CHECK(lines[0].beat && !lines[1].beat && lines[1].timeline == 0.25f);
    f.setTiming(0.f, 100.f);
    m.gridLines(lines);
    CHECK_NEAR(lines[1].timeline, 0.375f, 1e-6);   // swing shifts every second line
    CHECK_NEAR(lines[3].timeline, 0.875f, 1e-6);
    f.setTiming(0.f, 0.f, 4, 2);                    // 1/16 grid over a 1/1 cycle: 16 lines, beats every 4
    m.gridLines(lines);
    CHECK(lines.size() == 16 && lines[4].beat && !lines[5].beat);
    CHECK_NEAR(m.snapTimeline(0.1f), 0.125f, 1e-6);
    f.setTiming(0.f, 0.f);
    // Handles sit on the curve at the node-space midpoint; vertical segments have none.
    CHECK(m.segmentHasHandle(0) && !m.segmentHasHandle(1) && !m.segmentHasHandle(9));
    const Vec2 h = m.tensionHandlePosition(0);
    CHECK_NEAR(h.x, 0.15f, 1e-6);
    CHECK_NEAR(h.y, m.valueAt(0.15f), 1e-6);
    CHECK(m.tensionHandlePosition(-1).x == 0.f);
    CHECK(m.nodePosition(99).x == 0.f);
}

TEST_CASE(geometry_hit_testing)
{
    Fixture f;
    EditorModel& m = f.model;
    m.load(kShape, kShape, LoadHistory::Clear);
    auto px = [](float t) { return kPlot.toPixelX(t); };
    auto py = [](float v) { return kPlot.toPixelY(v); };
    // Node.
    Hit h = m.hitTest(kPlot, px(0.7f) + 3.f, py(1.f) + 2.f);
    CHECK(h.kind == HitKind::Node && h.index == 3);
    // A vertical step: the pointer near either end picks that node.
    CHECK(m.hitTest(kPlot, px(0.3f), py(0.6f)).index == 1);
    CHECK(m.hitTest(kPlot, px(0.3f), py(0.2f)).index == 2);
    // Handle of segment 0 (at x 0.15) and segment 3 (0.7 → 1).
    const Vec2 h0 = m.tensionHandlePosition(0);
    h = m.hitTest(kPlot, px(h0.x), py(h0.y) + 1.f);
    CHECK(h.kind == HitKind::TensionHandle && h.index == 0);
    // Segment away from the handle.
    const float t = 0.55f;
    h = m.hitTest(kPlot, px(t), py(m.valueAt(t)) - 4.f);
    CHECK(h.kind == HitKind::Segment && h.index == 2);
    // Near the vertical step line but away from both nodes: nothing to bend.
    h = m.hitTest(kPlot, px(0.3f) + 2.f, py(0.4f));
    CHECK(h.kind == HitKind::Empty);
    // Empty space and outside.
    CHECK(m.hitTest(kPlot, px(0.5f), py(0.05f)).kind == HitKind::Empty);
    CHECK(m.hitTest(kPlot, 5.f, 5.f).kind == HitKind::None);
    CHECK(m.hitTest(kPlot, std::nanf(""), 5.f).kind == HitKind::None);
    CHECK(m.hitTest(ViewTransform{0, 0, 0, 0}, 5.f, 5.f).kind == HitKind::None);
    // An edge node just outside the plot is still grabbable.
    CHECK(m.hitTest(kPlot, px(0.f) - 4.f, py(0.f) + 3.f).kind == HitKind::Node);
    // Rotated: hits follow the drawn positions (node x 0.7 is drawn at 0.95).
    f.setTiming(90.f, 0.f);
    h = m.hitTest(kPlot, px(0.95f), py(1.f));
    CHECK(h.kind == HitKind::Node && h.index == 3);
    // The rotation seam at 0.25 (endpoints y 0 and 1 meet): a vertical line, not a segment.
    h = m.hitTest(kPlot, px(0.25f) + 1.f, py(0.5f));
    CHECK(h.kind == HitKind::Empty);
}
