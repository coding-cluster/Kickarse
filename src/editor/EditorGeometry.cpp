// Kickarse — EditorModel geometry for drawing, and hit testing.
#include <algorithm>
#include <cmath>

#include "EditorModelImpl.h"

namespace kick::editor {

namespace {

// Segment index (node i → i+1) whose span contains node x (right-continuous at nodes).
int segmentAt(const Envelope& e, float x) noexcept
{
    const int n = e.size();
    int lo = 0, hi = n;
    while (lo < hi) {
        const int mid = (lo + hi) >> 1;
        if (e.node(mid).x <= x)
            lo = mid + 1;
        else
            hi = mid;
    }
    return std::clamp(lo - 1, 0, n - 2);
}

float segmentY(const Envelope& e, int i, float x) noexcept
{
    const EnvNode& a  = e.node(i);
    const EnvNode& b  = e.node(i + 1);
    const float    dx = b.x - a.x;
    if (!(dx > 0.f))
        return b.y;
    return a.y + (b.y - a.y) * Envelope::shape(clamp01((x - a.x) / dx), a.tension);
}

float pointSegmentDistance(float px, float py, float ax, float ay, float bx, float by) noexcept
{
    const float vx = bx - ax, vy = by - ay;
    const float len2 = vx * vx + vy * vy;
    float t = len2 > 0.f ? ((px - ax) * vx + (py - ay) * vy) / len2 : 0.f;
    t = std::clamp(t, 0.f, 1.f);
    const float dx = ax + vx * t - px, dy = ay + vy * t - py;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

Vec2 EditorModel::nodePosition(int index) const noexcept
{
    return nodePosition(d_->editBand(), index);
}

Vec2 EditorModel::nodePosition(Band band, int index) const noexcept
{
    const Envelope& e = d_->env(band);
    if (index < 0 || index >= e.size())
        return {};
    const EnvNode& n = e.node(index);
    return Vec2{d_->map.toTimeline(n.x), n.y};
}

bool EditorModel::segmentHasHandle(int segment) const noexcept
{
    const Envelope& e = d_->env(d_->editBand());
    return segment >= 0 && segment + 1 < e.size() && e.node(segment + 1).x > e.node(segment).x;
}

Vec2 EditorModel::tensionHandlePosition(int segment) const noexcept
{
    return tensionHandlePosition(d_->editBand(), segment);
}

Vec2 EditorModel::tensionHandlePosition(Band band, int segment) const noexcept
{
    const Envelope& e = d_->env(band);
    if (segment < 0 || segment + 1 >= e.size())
        return {};
    const float xm = 0.5f * (e.node(segment).x + e.node(segment + 1).x);
    return Vec2{d_->map.toTimeline(xm), segmentY(e, segment, xm)};
}

float EditorModel::valueAt(float timeline) const noexcept
{
    return valueAt(d_->editBand(), timeline);
}

float EditorModel::valueAt(Band band, float timeline) const noexcept
{
    return d_->env(band).evaluate(d_->map.toNode(timeline));
}

void EditorModel::buildCurve(Band band, float plotWidthPx, std::vector<Vec2>& out, float samplePx) const
{
    out.clear();
    const Impl&        m = *d_;
    const Envelope&    e = m.env(band);
    const TimelineMap& map = m.map;
    const int          n = e.size();
    const float width = std::isfinite(plotWidthPx) ? std::clamp(plotWidthPx, 1.f, 100000.f) : 600.f;
    const float step  = std::isfinite(samplePx) ? std::clamp(samplePx, 0.25f, 64.f) : 2.f;

    // Polyline in seam space (s, y) from the first node to the last.
    std::vector<Vec2>  pts;
    std::vector<float> interior, kinks;
    pts.reserve(size_t(n) * 4 + 64);
    for (int i = 0; i + 1 < n; ++i) {
        const EnvNode& a  = e.node(i);
        const EnvNode& b  = e.node(i + 1);
        const float    sa = map.toSeam(a.x), sb = map.toSeam(b.x);
        pts.push_back(Vec2{sa, a.y});
        if (!(sb > sa))
            continue;
        interior.clear();
        if (a.tension != 0.f && a.y != b.y) {
            const int count = std::clamp(int(std::ceil((sb - sa) * width / step)), 1, 4096);
            for (int k = 1; k < count; ++k)
                interior.push_back(sa + (sb - sa) * float(k) / float(count));
        }
        map.kinksBetween(a.x, b.x, kinks);
        for (float k : kinks)
            interior.push_back(map.toSeam(k));
        std::sort(interior.begin(), interior.end());
        for (float s : interior)
            if (s > sa && s < sb)
                pts.push_back(Vec2{s, segmentY(e, i, map.fromSeam(s))});
    }
    pts.push_back(Vec2{1.f, e.node(n - 1).y});

    const float r = map.rotation();
    if (r <= 0.f) {
        out = std::move(pts);
        return;
    }
    // Rotate into the timeline: the part after sw = 1 − r is drawn first (timeline 0 … r), then the
    // rest (r … 1). The envelope's own endpoints meet at timeline r (the seam).
    const float sw = 1.f - r;
    const float xw = map.fromSeam(sw);
    out.reserve(pts.size() + 2);
    out.push_back(Vec2{0.f, e.evaluate(xw)});
    for (const Vec2& p : pts)
        if (p.x > sw)
            out.push_back(Vec2{std::clamp(p.x - sw, 0.f, 1.f), p.y});
    for (const Vec2& p : pts)
        if (p.x < sw)
            out.push_back(Vec2{std::clamp(p.x + r, 0.f, 1.f), p.y});
    out.push_back(Vec2{1.f, e.evaluateLeft(xw)});
}

void EditorModel::gridLines(std::vector<GridLine>& out) const
{
    out.clear();
    const Impl&  m     = *d_;
    const int    count = m.map.gridCount();
    const double beats = kGrids[std::clamp(m.timing.gridIndex, 0, kNumGrids - 1)].beats;
    out.reserve(size_t(count));
    for (int k = 0; k < count; ++k) {
        GridLine g;
        g.index    = k;
        g.nodeX    = m.map.gridNode(k);
        g.timeline = m.map.toTimeline(g.nodeX);
        const double pos = double(k) * beats;
        g.beat = m.timing.timeMode == kTimeSync && std::fabs(pos - std::round(pos)) < 1e-6;
        out.push_back(g);
    }
}

float EditorModel::snapTimeline(float timeline) const noexcept
{
    const TimelineMap& map = d_->map;
    return map.toTimeline(map.snapNode(map.toNode(clamp01(timeline))));
}

int EditorModel::quickShiftSpans(Vec2 out[2]) const noexcept
{
    const QuickShiftRange r = quickShiftRange();
    if (!r.valid || !out)
        return 0;
    const TimelineMap& map = d_->map;
    const double rot = map.rotation();
    const double a   = rot + map.toSeam(r.start);
    const double b   = rot + map.toSeam(r.end);
    if (b <= 1.0) {
        out[0] = Vec2{float(a), float(b)};
        return 1;
    }
    if (a >= 1.0) {
        out[0] = Vec2{float(a - 1.0), float(b - 1.0)};
        return 1;
    }
    out[0] = Vec2{float(a), 1.f};
    out[1] = Vec2{0.f, float(b - 1.0)};
    return 2;
}

Vec2 EditorModel::quickShiftEdges() const noexcept
{
    const QuickShiftRange r = quickShiftRange();
    if (!r.valid)
        return {};
    const TimelineMap& map = d_->map;
    const double rot = map.rotation();
    double a = rot + map.toSeam(r.start);
    double b = rot + map.toSeam(r.end);
    if (a >= 1.0)
        a -= 1.0;
    if (b > 1.0)
        b -= 1.0;
    return Vec2{float(a), float(b)};
}

NodeMask EditorModel::nodesInRect(float t0, float v0, float t1, float v1) const
{
    NodeMask mask;
    if (!std::isfinite(t0) || !std::isfinite(t1) || !std::isfinite(v0) || !std::isfinite(v1))
        return mask;
    const float xl = std::min(t0, t1), xh = std::max(t0, t1);
    const float yl = std::min(v0, v1), yh = std::max(v0, v1);
    const Envelope& e = d_->env(d_->editBand());
    for (int i = 0; i < e.size(); ++i) {
        const Vec2 p = nodePosition(i);
        if (p.x >= xl && p.x <= xh && p.y >= yl && p.y <= yh)
            mask.set(size_t(i));
    }
    return mask;
}

Hit EditorModel::hitTest(const ViewTransform& plot, float px, float py, const HitTolerances& tol) const
{
    Hit hit;
    if (!std::isfinite(px) || !std::isfinite(py) || !(plot.width > 0.f) || !(plot.height > 0.f))
        return hit;
    const float margin = std::max(tol.nodePx, tol.segmentPx);
    if (!plot.contains(px, py, margin))
        return hit;

    const Impl&        m   = *d_;
    const Envelope&    e   = m.env(m.editBand());
    const TimelineMap& map = m.map;
    const int          n   = e.size();

    // Nodes (ties go to the later node: pulling a stacked pair apart grabs its right-hand node).
    int   node  = -1;
    float nodeD = tol.nodePx;
    for (int i = 0; i < n; ++i) {
        const Vec2  p = nodePosition(i);
        const float d = std::hypot(plot.toPixelX(p.x) - px, plot.toPixelY(p.y) - py);
        if (d <= nodeD) {
            nodeD = d;
            node  = i;
        }
    }
    // Tension handles on segments wide enough to show one.
    int   handle  = -1;
    float handleD = tol.handlePx;
    for (int i = 0; i + 1 < n; ++i) {
        const float w = (map.toSeam(e.node(i + 1).x) - map.toSeam(e.node(i).x)) * plot.width;
        if (!(w >= tol.minHandleSegmentPx))
            continue;
        const Vec2  p = tensionHandlePosition(i);
        const float d = std::hypot(plot.toPixelX(p.x) - px, plot.toPixelY(p.y) - py);
        if (d <= handleD) {
            handleD = d;
            handle  = i;
        }
    }
    if (node >= 0 && (handle < 0 || nodeD <= handleD + 2.f)) {
        hit.kind     = HitKind::Node;
        hit.index    = node;
        hit.distance = nodeD;
        return hit;
    }
    if (handle >= 0) {
        hit.kind     = HitKind::TensionHandle;
        hit.index    = handle;
        hit.distance = handleD;
        return hit;
    }

    // The curve: a fine polyline around the pointer (0.5 px steps), so steep parts, steps and the
    // rotation seam are measured by true distance.
    const float t0    = plot.toTimeline(px);
    const float stepT = 0.5f / plot.width;
    const int   half  = int(std::ceil(tol.segmentPx / 0.5f));
    float bestD   = tol.segmentPx;
    int   bestSeg = -1;
    bool  prevOk  = false;
    float prevX = 0.f, prevY = 0.f;
    int   prevSeg = 0;
    for (int k = -half; k <= half; ++k) {
        const float t = t0 + float(k) * stepT;
        if (t < 0.f || t > 1.f) {
            prevOk = false;
            continue;
        }
        float q = 0.f, y = 0.f;
        int   seg = 0;
        if (t >= 1.f && map.rotation() == 0.f) {
            q   = 1.f;
            y   = e.node(n - 1).y;
            seg = n - 2;
        } else {
            q   = map.toNode(t);
            seg = segmentAt(e, q);
            y   = e.evaluate(q);
        }
        const float x = plot.toPixelX(t), yp = plot.toPixelY(y);
        if (prevOk) {
            const float d = pointSegmentDistance(px, py, prevX, prevY, x, yp);
            if (d <= bestD) {
                const bool steep = std::fabs(yp - prevY) > 4.f * std::fabs(x - prevX);
                if (seg == prevSeg || !steep) {
                    const bool nearCur = std::hypot(px - x, py - yp) <= std::hypot(px - prevX, py - prevY);
                    bestSeg = seg == prevSeg ? seg : (nearCur ? seg : prevSeg);
                } else {
                    bestSeg = -1;   // a vertical step: nothing to bend
                }
                bestD = d;
            }
        }
        prevOk  = true;
        prevX   = x;
        prevY   = yp;
        prevSeg = seg;
    }
    if (bestSeg >= 0 && segmentHasHandle(bestSeg)) {
        hit.kind     = HitKind::Segment;
        hit.index    = bestSeg;
        hit.distance = bestD;
        return hit;
    }
    if (plot.contains(px, py)) {
        hit.kind = HitKind::Empty;
        return hit;
    }
    return hit;
}

Hit EditorModel::hitTestQuickShift(const ViewTransform& lane, float px, float py, const HitTolerances& tol) const
{
    Hit hit;
    if (!std::isfinite(px) || !std::isfinite(py) || !(lane.width > 0.f) || !lane.contains(px, py))
        return hit;
    hit.kind = HitKind::QuickShiftLane;
    Vec2      spans[2];
    const int count = quickShiftSpans(spans);
    if (count == 0)
        return hit;
    const Vec2  edges = quickShiftEdges();
    const float xs = lane.toPixelX(edges.x), xe = lane.toPixelX(edges.y);
    const float ds = std::fabs(px - xs), de = std::fabs(px - xe);
    if (std::min(ds, de) <= tol.laneHandlePx) {
        bool end = de < ds;
        if (ds == de)
            end = px >= xe;
        hit.kind     = end ? HitKind::QuickShiftEnd : HitKind::QuickShiftStart;
        hit.index    = end ? 1 : 0;
        hit.distance = end ? de : ds;
        return hit;
    }
    for (int i = 0; i < count; ++i) {
        if (px >= lane.toPixelX(spans[i].x) && px <= lane.toPixelX(spans[i].y)) {
            hit.kind = HitKind::QuickShiftBar;
            return hit;
        }
    }
    return hit;
}

} // namespace kick::editor
