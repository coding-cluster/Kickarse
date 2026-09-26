// Kickarse — EditorModel editing operations: nodes, gestures, Quick Shift, transforms, capture.
#include <algorithm>
#include <cmath>

#include "EditorModelImpl.h"

namespace kick::editor {

namespace {

inline bool finite(float v) noexcept
{
    return std::isfinite(v);
}

inline bool isMember(const ops::NodeList& v, size_t i) noexcept
{
    return i > 0 && i + 1 < v.size() && (v[i].flags & kNodeQuickShift);
}

void clearTag(ops::NodeList& v, uint32_t tag) noexcept
{
    for (EnvNode& n : v)
        n.flags &= ~tag;
}

// Rigid node-x delta range for the nodes in `moving` so none crosses a fixed neighbour
// (endpoints are always fixed). margin keeps that much distance (Quick Shift). Always contains 0.
void rigidRange(const ops::NodeList& v, const std::vector<bool>& moving, float margin, float& lo, float& hi)
{
    lo = -1.f;
    hi = 1.f;
    const int n = int(v.size());
    for (int i = 0; i < n; ++i) {
        if (!moving[size_t(i)])
            continue;
        int j = i - 1;
        while (j > 0 && moving[size_t(j)])
            --j;
        int k = i + 1;
        while (k < n - 1 && moving[size_t(k)])
            ++k;
        lo = std::max(lo, v[size_t(std::max(j, 0))].x + margin - v[size_t(i)].x);
        hi = std::min(hi, v[size_t(std::min(k, n - 1))].x - margin - v[size_t(i)].x);
    }
    lo = std::min(lo, 0.f);
    hi = std::max(hi, 0.f);
}

// Removes nodes carrying `tag` that merely subdivide one segment (the merged segment reproduces
// the same curve), undoing the cuts made for an exact piecewise-linear remap of node x.
void removeRedundant(ops::NodeList& v, uint32_t tag)
{
    for (size_t i = 1; i + 1 < v.size();) {
        if (!(v[i].flags & tag)) {
            ++i;
            continue;
        }
        EnvNode&       a = v[i - 1];
        const EnvNode& m = v[i];
        const EnvNode& b = v[i + 1];
        bool remove = false;
        float t = a.tension;
        if (b.x > a.x && m.x > a.x && m.x < b.x) {
            const float c = (m.x - a.x) / (b.x - a.x);
            if (std::fabs(b.y - a.y) < 1e-7f && std::fabs(m.y - a.y) < 1e-6f) {
                remove = true;
                t      = 0.f;
            } else if (c > 1e-6f) {
                t = a.tension / c;
                if (std::fabs(t) <= 1.f + 1e-4f) {
                    t = std::clamp(t, -1.f, 1.f);
                    const float ym = a.y + (b.y - a.y) * Envelope::shape(c, t);
                    remove = std::fabs(m.tension - t * (1.f - c)) <= 1e-4f && std::fabs(ym - m.y) <= 1e-5f;
                }
            }
        } else if (a.x == m.x && m.x == b.x) {
            remove = true;   // inside a vertical stack: inaudible
        }
        if (remove) {
            a.tension = t;
            v.erase(v.begin() + ptrdiff_t(i));
        } else {
            ++i;
        }
    }
    clearTag(v, tag);
}

// Cuts `list` at node positions `xs` (tagging new nodes), then maps every node x through f.
// f must be monotonic and affine between consecutive cut positions.
template <class F>
void remapX(ops::NodeList& list, const std::vector<float>& xs, F&& f)
{
    for (float x : xs) {
        const size_t before = list.size();
        const int    idx    = ops::cut(list, x, false);
        if (idx >= 0 && list.size() > before)
            list[size_t(idx)].flags |= ops::kTagNew;
    }
    for (EnvNode& n : list)
        n.x = f(n.x);
    list.front().x = 0.f;
    list.back().x  = 1.f;
}

} // namespace

// ---- Impl helpers ------------------------------------------------------------------------------------

QuickShiftRange EditorModel::Impl::qsRange(Band band) const noexcept
{
    const BandDoc&  d = bd(band);
    QuickShiftRange r;
    if (d.qsExplicit) {
        r.valid         = true;
        r.explicitRange = true;
        r.start         = d.qsStart;
        r.end           = d.qsEnd;
        return r;
    }
    const int n = d.env.size();
    for (int i = 1; i + 1 < n; ++i) {
        const EnvNode& e = d.env.node(i);
        if (!(e.flags & kNodeQuickShift))
            continue;
        if (!r.valid) {
            r.valid = true;
            r.start = r.end = e.x;
        } else {
            r.start = std::min(r.start, e.x);
            r.end   = std::max(r.end, e.x);
        }
    }
    return r;
}

int EditorModel::Impl::groupSize(Band band) const noexcept
{
    const Envelope& e = env(band);
    int count = 0;
    for (int i = 1; i + 1 < e.size(); ++i)
        if (e.node(i).flags & kNodeQuickShift)
            ++count;
    return count;
}

void EditorModel::Impl::setQsRange(Band band, float start, float end, bool autoGroup)
{
    BandDoc& d  = bd(band);
    start       = clamp01(start);
    end         = std::max(clamp01(end), start);
    d.qsExplicit = true;
    d.qsStart    = start;
    d.qsEnd      = end;
    if (!autoGroup)
        return;
    const int n = d.env.size();
    for (int i = 0; i < n; ++i) {
        EnvNode&   e      = d.env.node(i);
        const bool inside = i > 0 && i + 1 < n && e.x >= start - ops::kSameX && e.x <= end + ops::kSameX;
        e.flags = inside ? (e.flags | kNodeQuickShift) : (e.flags & ~uint32_t(kNodeQuickShift));
    }
}

float EditorModel::Impl::shiftGroupImpl(Band band, float dxTimeline, bool invertSnap)
{
    ops::NodeList v = tagged(band);
    const size_t  n = v.size();
    std::vector<bool> moving(n, false);
    int anchor = -1;
    for (size_t i = 0; i < n; ++i) {
        moving[i] = isMember(v, i);
        if (moving[i] && anchor < 0)
            anchor = int(i);
    }
    if (anchor < 0 || !finite(dxTimeline))
        return 0.f;
    const float xa = v[size_t(anchor)].x;
    float dq = map.fromSeam(clamp01(map.toSeam(xa) + dxTimeline)) - xa;
    if (snapActive(invertSnap))
        dq = TimelineMap::snapDelta(dq, map.cellWidth() / float(std::max(1, snap.quickShiftSubdivisions)));
    float lo = 0.f, hi = 0.f;
    rigidRange(v, moving, snap.quickShiftMargin, lo, hi);
    BandDoc& d = bd(band);
    if (d.qsExplicit) {
        lo = std::max(lo, -d.qsStart);
        hi = std::min(hi, 1.f - d.qsEnd);
        lo = std::min(lo, 0.f);
        hi = std::max(hi, 0.f);
    }
    dq = std::clamp(dq, lo, hi);
    if (dq == 0.f)
        return 0.f;
    for (size_t i = 0; i < n; ++i)
        if (moving[i])
            v[i].x += dq;
    if (!store(band, v))
        return 0.f;
    if (d.qsExplicit) {
        d.qsStart = clamp01(d.qsStart + dq);
        d.qsEnd   = std::max(clamp01(d.qsEnd + dq), d.qsStart);
    }
    return map.toSeam(xa + dq) - map.toSeam(xa);
}

bool EditorModel::Impl::applyLine(ops::NodeList& list, Vec2 a, Vec2 b) const
{
    if (a.x == b.x) {
        if (a.y == b.y)
            return false;
        const float q = map.toNode(a.x);
        if (q <= 0.f) {
            // At the rotation seam the arriving side is the cycle end, the leaving side its start.
            list.back().y      = a.y;
            list.front().y     = b.y;
            list.back().flags  |= ops::kTagSelected;
            list.front().flags |= ops::kTagSelected;
            return true;
        }
        const ops::NodeList step = {EnvNode{q, a.y, 0.f, ops::kTagSelected},
                                    EnvNode{q, b.y, 0.f, ops::kTagSelected}};
        return ops::replaceSpan(list, q, q, step);
    }
    if (b.x < a.x)
        std::swap(a, b);

    Vec2        pieces[2][2];
    int         count = 0;
    const float r     = map.rotation();
    if (r > 0.f && a.x < r && b.x > r) {
        const Vec2 mid{r, a.y + (b.y - a.y) * (r - a.x) / (b.x - a.x)};
        pieces[0][0] = a;
        pieces[0][1] = mid;
        pieces[1][0] = mid;
        pieces[1][1] = b;
        count        = 2;
    } else {
        pieces[0][0] = a;
        pieces[0][1] = b;
        count        = 1;
    }

    bool any = false;
    std::vector<float> kinks;
    for (int p = 0; p < count; ++p) {
        const Vec2  pa = pieces[p][0], pb = pieces[p][1];
        const float sa = map.timelineToSeam(pa.x);
        float       sb = sa + (pb.x - pa.x);
        if (sb > 1.f - 1e-6f)
            sb = 1.f;   // the piece ends at the seam: the cycle end exactly
        if (!(sb > sa))
            continue;
        const float qa = map.fromSeam(sa), qb = map.fromSeam(sb);
        if (!(qb > qa))
            continue;
        ops::NodeList piece;
        piece.push_back(EnvNode{qa, pa.y, 0.f, ops::kTagSelected});
        map.kinksBetween(qa, qb, kinks);
        for (float k : kinks) {
            const float u = (map.toSeam(k) - sa) / (sb - sa);
            piece.push_back(EnvNode{k, pa.y + (pb.y - pa.y) * clamp01(u), 0.f, ops::kTagSelected});
        }
        piece.push_back(EnvNode{qb, pb.y, 0.f, ops::kTagSelected});
        ops::NodeList trial = list;
        if (!ops::replaceSpan(trial, qa, qb, piece)) {
            // Out of nodes for the swing kinks: accept a slightly bent line.
            const ops::NodeList ends = {piece.front(), piece.back()};
            trial = list;
            if (!ops::replaceSpan(trial, qa, qb, ends))
                return false;
        }
        list.swap(trial);
        any = true;
    }
    return any;
}

bool EditorModel::Impl::applyPencil(Band band)
{
    const std::vector<Vec2>& s = g.stroke;
    if (s.size() < 2)
        return false;
    float pmin = 1.f, pmax = 0.f;
    for (const Vec2& p : s) {
        pmin = std::min(pmin, p.x);
        pmax = std::max(pmax, p.x);
    }
    if (!(pmax - pmin >= 1e-4f))
        return false;

    // Paint the stroke onto a uniform grid over its x-span; later segments overwrite earlier ones,
    // so drawing back over a region replaces it (the result is a function of x).
    constexpr int kCells = 1024;
    const float   step   = (pmax - pmin) / float(kCells - 1);
    std::vector<float> paint(size_t(kCells), -1.f);
    for (size_t k = 1; k < s.size(); ++k) {
        const Vec2  a = s[k - 1], c = s[k];
        const float lo = std::min(a.x, c.x), hi = std::max(a.x, c.x);
        const int   j0 = std::max(0, int(std::ceil((lo - pmin) / step - 1e-3f)));
        const int   j1 = std::min(kCells - 1, int(std::floor((hi - pmin) / step + 1e-3f)));
        for (int j = j0; j <= j1; ++j) {
            const float gx = pmin + float(j) * step;
            const float t  = c.x == a.x ? 1.f : clamp01((gx - a.x) / (c.x - a.x));
            paint[size_t(j)] = clamp01(a.y + (c.y - a.y) * t);
        }
    }
    for (int j = 1; j < kCells; ++j)
        if (paint[size_t(j)] < 0.f)
            paint[size_t(j)] = paint[size_t(j) - 1];
    for (int j = kCells - 2; j >= 0; --j)
        if (paint[size_t(j)] < 0.f)
            paint[size_t(j)] = paint[size_t(j) + 1];
    if (paint[0] < 0.f)
        return false;
    auto painted = [&](float p) {
        const float f = std::clamp((p - pmin) / step, 0.f, float(kCells - 1));
        const int   i = std::min(int(f), kCells - 2);
        const float u = f - float(i);
        return paint[size_t(i)] + (paint[size_t(i) + 1] - paint[size_t(i)]) * u;
    };

    float bounds[3] = {pmin, pmax, pmax};
    int   pieces    = 1;
    const float r   = map.rotation();
    if (r > 0.f && pmin < r && pmax > r) {
        bounds[1] = r;
        bounds[2] = pmax;
        pieces    = 2;
    }

    restoreOrigin();
    ops::NodeList list = tagged(band);
    clearTag(list, ops::kTagSelected);
    bool any = false;
    for (int p = 0; p < pieces; ++p) {
        const float pa = bounds[p], pb = bounds[p + 1];
        const float sa = map.timelineToSeam(pa);
        float       sb = sa + (pb - pa);
        if (sb > 1.f - 1e-6f)
            sb = 1.f;
        if (!(sb > sa))
            continue;
        const float qa = map.fromSeam(sa), qb = map.fromSeam(sb);
        if (!(qb > qa))
            continue;
        std::vector<float> ys(static_cast<size_t>(kPencilSamples));
        for (int i = 0; i < kPencilSamples; ++i) {
            const float q  = qa + (qb - qa) * float(i) / float(kPencilSamples);
            ys[size_t(i)]  = painted(pa + (map.toSeam(q) - sa));
        }
        const float yEnd = painted(pb);
        float tol    = kPencilTolerance;
        bool  placed = false;
        for (int attempt = 0; attempt < 10 && !placed; ++attempt, tol *= 1.5f) {
            const Envelope fit   = Envelope::fromSamples(ys.data(), kPencilSamples, tol);
            ops::NodeList  piece = ops::toList(fit);
            for (EnvNode& nd : piece) {
                nd.x     = qa + nd.x * (qb - qa);
                nd.flags = 0;
            }
            piece.front().x = qa;
            piece.back().x  = qb;
            piece.back().y  = yEnd;
            ops::NodeList trial = list;
            if (ops::replaceSpan(trial, qa, qb, piece)) {
                list.swap(trial);
                placed = true;
            }
        }
        if (!placed)
            return false;
        any = true;
    }
    return any && store(band, list);
}

void EditorModel::Impl::startGesture(GestureKind kind, std::string label, Band band)
{
    begin(std::move(label));
    g           = GestureState{};
    g.kind      = kind;
    g.txn       = txns.size() - 1;
    g.band      = band;
    g.info.kind = kind;
    g.info.band = band;
}

bool EditorModel::Impl::endGesture()
{
    if (g.kind == GestureKind::None)
        return false;
    if (g.txn >= txns.size()) {
        g = GestureState{};
        return false;
    }
    if (g.kind == GestureKind::Pencil) {
        while (txns.size() > g.txn + 1)
            commit();
        if (!applyPencil(g.band))
            restoreOrigin();
    }
    const size_t txn = g.txn;
    g = GestureState{};
    while (txns.size() > txn + 1)
        commit();
    const bool outermost = txns.size() == 1;
    const bool any       = commit();
    stateChanged();
    return outermost && any;
}

void EditorModel::Impl::cancelGesture()
{
    if (g.kind == GestureKind::None)
        return;
    const size_t txn = g.txn;
    g = GestureState{};
    while (txns.size() > txn + 1)
        commit();
    cancel();
    stateChanged();
}

bool EditorModel::Impl::spanTransform(std::string label, const std::function<bool(ops::NodeList&)>& fn,
                                      bool keepQsRange)
{
    return edit(std::move(label), [&] {
        const Band    b    = editBand();
        ops::NodeList list = tagged(b);
        int  i0 = 0, i1 = int(list.size()) - 1;
        const bool span = selectionSpan(b, i0, i1);
        auto wrapped = [&](ops::NodeList& c) {
            if (!fn(c))
                return false;
            for (EnvNode& nd : c)
                nd.flags = span ? (nd.flags | ops::kTagSelected) : (nd.flags & ~ops::kTagSelected);
            return true;
        };
        if (!ops::transformSpan(list, i0, i1, wrapped))
            return false;
        if (!store(b, list))
            return false;
        if (!keepQsRange)
            bd(b).qsExplicit = false;
        return true;
    });
}

bool EditorModel::Impl::applyShapeImpl(const Envelope& shape, bool intoSelection, std::string label)
{
    ops::NodeList content = ops::toList(shape);
    for (EnvNode& n : content)
        n.flags &= ~ops::kTagMask;
    int i0 = 0, i1 = 0;
    if (intoSelection && selectionSpan(editBand(), i0, i1)) {
        return spanTransform(std::move(label), [&](ops::NodeList& c) {
            c = content;
            return c.size() >= 2;
        });
    }
    return edit(std::move(label), [&] {
        Envelope e;
        if (!ops::fromList(content, e))
            return false;
        BandDoc& d  = bd(editBand());
        d.env        = e;
        d.sel.reset();
        d.qsExplicit = false;
        return true;
    });
}

bool EditorModel::Impl::rotateExact(ops::NodeList& list, float amount) const
{
    if (!map.swingActive())
        return ops::rotate(list, amount);
    // Exact under swing: go to seam space (swing applied), rotate there, come back. Each step is a
    // piecewise-affine remap of x after cutting at its breakpoints, so the curve is preserved.
    ops::NodeList       w = list;
    std::vector<float>  kinks;
    map.kinksBetween(0.f, 1.f, kinks);
    remapX(w, kinks, [&](float x) { return map.toSeam(x); });
    if (!ops::rotate(w, amount))
        return false;
    std::vector<float> seamKinks;
    for (float k : kinks)
        seamKinks.push_back(map.toSeam(k));
    remapX(w, seamKinks, [&](float s) { return map.fromSeam(s); });
    removeRedundant(w, ops::kTagNew);
    if (w.size() > size_t(Envelope::kMaxNodes)) {
        ops::NodeList plain = list;
        if (!ops::rotate(plain, amount))
            return false;
        list.swap(plain);
        return true;
    }
    list.swap(w);
    return true;
}

// ---- node editing ------------------------------------------------------------------------------------

int EditorModel::addNode(float timeline, float value, bool invertSnap)
{
    Impl& m      = *d_;
    int   result = -1;
    m.edit("Add Node", [&] {
        if (!finite(timeline) || !finite(value))
            return false;
        const Band    b    = m.editBand();
        ops::NodeList list = m.tagged(b);
        if (int(list.size()) >= Envelope::kMaxNodes)
            return false;
        const float q  = m.map.toNode(clamp01(timeline));
        float       qs = m.snapActive(invertSnap) ? m.map.snapNode(q) : q;
        if (qs <= 0.f || qs >= 1.f)
            qs = q;
        qs = std::clamp(qs, kEdgeGap, 1.f - kEdgeGap);
        clearTag(list, ops::kTagSelected);
        const auto it = std::upper_bound(list.begin(), list.end() - 1, qs,
                                         [](float v, const EnvNode& n) { return v < n.x; });
        list.insert(it, EnvNode{qs, m.applyY(value, invertSnap), 0.f, ops::kTagSelected});
        if (!m.store(b, list))
            return false;
        const NodeMask& sel = m.bd(b).sel;
        for (int i = 0; i < m.env(b).size(); ++i)
            if (sel.test(size_t(i)))
                result = i;
        return true;
    });
    return result;
}

int EditorModel::addNodeOnCurve(float timeline, bool invertSnap)
{
    Impl& m      = *d_;
    int   result = -1;
    m.edit("Add Node", [&] {
        if (!finite(timeline))
            return false;
        const Band    b    = m.editBand();
        ops::NodeList list = m.tagged(b);
        const float   q    = m.map.toNode(clamp01(timeline));
        float         qs   = m.snapActive(invertSnap) ? m.map.snapNode(q) : q;
        if (qs <= 0.f || qs >= 1.f)
            qs = q;
        qs = std::clamp(qs, kEdgeGap, 1.f - kEdgeGap);
        const size_t before = list.size();
        const int    idx    = ops::cut(list, qs, true);
        if (idx < 0 || (list.size() > before && int(list.size()) > Envelope::kMaxNodes))
            return false;
        clearTag(list, ops::kTagSelected);
        list[size_t(idx)].flags |= ops::kTagSelected;
        if (!m.store(b, list))
            return false;
        result = idx;
        return true;
    });
    return result;
}

bool EditorModel::deleteNode(int index)
{
    Impl& m = *d_;
    return m.edit("Delete Node", [&] {
        const Band    b    = m.editBand();
        ops::NodeList list = m.tagged(b);
        if (index <= 0 || index >= int(list.size()) - 1)
            return false;
        list.erase(list.begin() + index);
        return m.store(b, list);
    });
}

bool EditorModel::deleteSelection()
{
    Impl& m = *d_;
    return m.edit("Delete Nodes", [&] {
        const Band    b    = m.editBand();
        ops::NodeList list = m.tagged(b);
        const size_t  n    = list.size();
        ops::NodeList out;
        for (size_t i = 0; i < n; ++i)
            if (i == 0 || i + 1 == n || !(list[i].flags & ops::kTagSelected))
                out.push_back(list[i]);
        if (out.size() == n)
            return false;
        return m.store(b, out);
    });
}

bool EditorModel::setTension(int segment, float tension)
{
    Impl& m = *d_;
    return m.edit("Bend Segment", [&] {
        const Band    b    = m.editBand();
        ops::NodeList list = m.tagged(b);
        if (segment < 0 || segment + 1 >= int(list.size()) || !finite(tension))
            return false;
        list[size_t(segment)].tension = std::clamp(tension, -1.f, 1.f);
        return m.store(b, list);
    });
}

bool EditorModel::straightenSegment(int segment)
{
    Impl& m = *d_;
    return m.edit("Straighten Segment", [&] {
        const Band    b    = m.editBand();
        ops::NodeList list = m.tagged(b);
        if (segment < 0 || segment + 1 >= int(list.size()))
            return false;
        list[size_t(segment)].tension = 0.f;
        return m.store(b, list);
    });
}

bool EditorModel::setNodePosition(int index, float timeline, float value)
{
    Impl& m = *d_;
    return m.edit("Move Node", [&] {
        const Band    b    = m.editBand();
        ops::NodeList list = m.tagged(b);
        const int     n    = int(list.size());
        if (index < 0 || index >= n || !finite(timeline) || !finite(value))
            return false;
        EnvNode& nd = list[size_t(index)];
        nd.y = clamp01(value);
        if (index > 0 && index < n - 1)
            nd.x = std::clamp(m.map.toNode(clamp01(timeline)), list[size_t(index) - 1].x, list[size_t(index) + 1].x);
        return m.store(b, list);
    });
}

bool EditorModel::nudgeSelection(int stepsX, int stepsY, bool fine)
{
    Impl& m = *d_;
    return m.edit("Nudge", [&] {
        const Band    b    = m.editBand();
        ops::NodeList list = m.tagged(b);
        const size_t  n    = list.size();
        std::vector<bool> movX(n, false);
        bool any = false;
        for (size_t i = 0; i < n; ++i) {
            const bool sel = (list[i].flags & ops::kTagSelected) != 0;
            any            = any || sel;
            movX[i]        = sel && i > 0 && i + 1 < n;
        }
        if (!any || (stepsX == 0 && stepsY == 0))
            return false;
        float dq = float(stepsX) * m.map.cellWidth() * (fine ? 0.125f : 1.f);
        float lo = 0.f, hi = 0.f;
        rigidRange(list, movX, 0.f, lo, hi);
        dq = std::clamp(dq, lo, hi);
        const float dy = float(stepsY) * (fine ? 0.001f : 0.01f);
        for (size_t i = 0; i < n; ++i) {
            if (movX[i])
                list[i].x += dq;
            if (list[i].flags & ops::kTagSelected)
                list[i].y = clamp01(list[i].y + dy);
        }
        return m.store(b, list);
    }, kCoalesceNudge);
}

// ---- gestures ----------------------------------------------------------------------------------------

bool EditorModel::beginNodeDrag(int grabbedIndex)
{
    Impl& m = *d_;
    m.endGesture();
    const Band b = m.editBand();
    BandDoc&   d = m.bd(b);
    if (grabbedIndex < 0 || grabbedIndex >= d.env.size())
        return false;
    if (!d.sel.test(size_t(grabbedIndex))) {
        d.sel.reset();
        d.sel.set(size_t(grabbedIndex));
    }
    m.startGesture(GestureKind::MoveNodes, "Move Nodes", b);
    m.g.index         = grabbedIndex;
    m.g.info.index    = grabbedIndex;
    m.g.info.position = nodePosition(b, grabbedIndex);
    m.stateChanged();
    return true;
}

void EditorModel::updateNodeDrag(float dxTimeline, float dyValue, const DragOptions& opt)
{
    Impl& m = *d_;
    if (m.g.kind != GestureKind::MoveNodes)
        return;
    float dx = finite(dxTimeline) ? dxTimeline : 0.f;
    float dy = finite(dyValue) ? dyValue : 0.f;
    if (opt.axis == AxisLock::X)
        dy = 0.f;
    else if (opt.axis == AxisLock::Y)
        dx = 0.f;

    m.restoreOrigin();
    const Band    b       = m.g.band;
    ops::NodeList list    = m.tagged(b);
    const int     n       = int(list.size());
    const int     grabbed = m.g.index;
    if (grabbed < 0 || grabbed >= n)
        return;
    std::vector<bool> mov(static_cast<size_t>(n)), movX(static_cast<size_t>(n));
    int anchor = -1;
    for (int i = 0; i < n; ++i) {
        mov[size_t(i)]  = (list[size_t(i)].flags & ops::kTagSelected) != 0;
        movX[size_t(i)] = mov[size_t(i)] && i > 0 && i < n - 1;
    }
    if (movX[size_t(grabbed)])
        anchor = grabbed;
    for (int i = 0; i < n && anchor < 0; ++i)
        if (movX[size_t(i)])
            anchor = i;

    float dq = 0.f;
    if (anchor >= 0 && dx != 0.f) {
        const float xa = list[size_t(anchor)].x;
        float       q  = m.map.fromSeam(clamp01(m.map.toSeam(xa) + dx));
        if (m.snapActive(opt.invertSnap))
            q = m.map.snapNode(q);
        float lo = 0.f, hi = 0.f;
        rigidRange(list, movX, 0.f, lo, hi);
        dq = std::clamp(q - xa, lo, hi);
        m.g.info.shift = m.map.toSeam(xa + dq) - m.map.toSeam(xa);
    } else {
        m.g.info.shift = 0.f;
    }
    float dyEff = 0.f;
    if (dy != 0.f) {
        const float y0 = list[size_t(grabbed)].y;
        dyEff          = m.applyY(y0 + dy, opt.invertSnap) - y0;
    }
    for (int i = 0; i < n; ++i) {
        if (movX[size_t(i)])
            list[size_t(i)].x += dq;
        if (mov[size_t(i)])
            list[size_t(i)].y = clamp01(list[size_t(i)].y + dyEff);
    }
    m.store(b, list);
    m.g.info.position = nodePosition(b, grabbed);
    m.live();
    m.stateChanged();
}

bool EditorModel::beginTensionDrag(int segment, bool allSelectedSegments)
{
    Impl& m = *d_;
    m.endGesture();
    const Band      b = m.editBand();
    const Envelope& e = m.env(b);
    if (segment < 0 || segment + 1 >= e.size() || !(e.node(segment + 1).x > e.node(segment).x))
        return false;
    m.startGesture(GestureKind::Tension, "Bend Segment", b);
    m.g.index        = segment;
    m.g.allSelected  = allSelectedSegments;
    m.g.info.index   = segment;
    m.g.info.tension = e.node(segment).tension;
    m.stateChanged();
    return true;
}

void EditorModel::updateTensionDrag(float raise)
{
    Impl& m = *d_;
    if (m.g.kind != GestureKind::Tension)
        return;
    if (!finite(raise))
        raise = 0.f;
    m.restoreOrigin();
    const Band    b    = m.g.band;
    ops::NodeList list = m.tagged(b);
    const int     seg  = m.g.index;
    if (seg < 0 || seg + 1 >= int(list.size()))
        return;
    auto bend = [&](int j) {
        EnvNode&       a = list[size_t(j)];
        const EnvNode& c = list[size_t(j) + 1];
        if (!(c.x > a.x))
            return;
        const float dir = c.y >= a.y ? 1.f : -1.f;   // raising the curve: rising → t down, falling → t up
        a.tension = std::clamp(a.tension - raise * dir, -1.f, 1.f);
    };
    bend(seg);
    const auto selected = [&](int i) { return (list[size_t(i)].flags & ops::kTagSelected) != 0; };
    if (m.g.allSelected && selected(seg) && selected(seg + 1))
        for (int j = 0; j + 1 < int(list.size()); ++j)
            if (j != seg && selected(j) && selected(j + 1))
                bend(j);
    m.store(b, list);
    m.g.info.tension = m.env(b).node(seg).tension;
    m.live();
    m.stateChanged();
}

bool EditorModel::beginLine(float timeline, float value, const DragOptions& opt)
{
    Impl& m = *d_;
    m.endGesture();
    if (!finite(timeline) || !finite(value))
        return false;
    m.startGesture(GestureKind::Line, "Draw Line", m.editBand());
    m.g.lineStart = Vec2{clamp01(timeline), clamp01(value)};
    updateLine(timeline, value, opt);
    return true;
}

void EditorModel::updateLine(float timeline, float value, const DragOptions& opt)
{
    Impl& m = *d_;
    if (m.g.kind != GestureKind::Line || !finite(timeline) || !finite(value))
        return;
    m.restoreOrigin();
    auto snapPoint = [&](Vec2 p) {
        if (m.snapActive(opt.invertSnap))
            p.x = m.map.toTimeline(m.map.snapNode(m.map.toNode(p.x)));
        p.y = m.applyY(p.y, opt.invertSnap);
        return p;
    };
    const Vec2 a = snapPoint(m.g.lineStart);
    const Vec2 b = snapPoint(Vec2{clamp01(timeline), clamp01(value)});
    m.g.info.lineStart = a;
    m.g.info.lineEnd   = b;
    const bool raw = m.g.lineStart.x == clamp01(timeline) && m.g.lineStart.y == clamp01(value);
    if (!raw) {
        ops::NodeList list = m.tagged(m.g.band);
        clearTag(list, ops::kTagSelected);
        if (m.applyLine(list, a, b))
            m.store(m.g.band, list);
    }
    m.live();
    m.stateChanged();
}

bool EditorModel::beginPencil(float timeline, float value)
{
    Impl& m = *d_;
    m.endGesture();
    if (!finite(timeline) || !finite(value))
        return false;
    m.startGesture(GestureKind::Pencil, "Draw", m.editBand());
    m.g.stroke.push_back(Vec2{clamp01(timeline), clamp01(value)});
    m.stateChanged();
    return true;
}

void EditorModel::addPencilPoint(float timeline, float value)
{
    Impl& m = *d_;
    if (m.g.kind != GestureKind::Pencil || !finite(timeline) || !finite(value))
        return;
    if (m.g.stroke.size() >= 65536)
        return;
    m.g.stroke.push_back(Vec2{clamp01(timeline), clamp01(value)});
    m.stateChanged();
}

const std::vector<Vec2>& EditorModel::pencilStroke() const noexcept
{
    return d_->g.stroke;
}

bool EditorModel::beginQuickShiftDrag(QuickShiftPart part, float timeline)
{
    Impl& m = *d_;
    m.endGesture();
    const Band b = m.editBand();
    switch (part) {
    case QuickShiftPart::Bar:
        if (m.groupSize(b) == 0)
            return false;
        m.startGesture(GestureKind::QuickShiftMove, "Quick Shift", b);
        break;
    case QuickShiftPart::Start:
    case QuickShiftPart::End:
        if (!m.qsRange(b).valid)
            return false;
        m.startGesture(part == QuickShiftPart::Start ? GestureKind::QuickShiftStart : GestureKind::QuickShiftEnd,
                       "Quick Shift Range", b);
        break;
    case QuickShiftPart::NewRange:
        if (!finite(timeline))
            return false;
        m.startGesture(GestureKind::QuickShiftEnd, "Quick Shift Range", b);
        m.g.anchor = m.map.toNode(clamp01(timeline));
        break;
    }
    m.g.qsPart = part;
    m.stateChanged();
    return true;
}

void EditorModel::updateQuickShiftDrag(float dxTimeline, const DragOptions& opt)
{
    Impl& m = *d_;
    if (m.g.kind != GestureKind::QuickShiftMove && m.g.kind != GestureKind::QuickShiftStart
        && m.g.kind != GestureKind::QuickShiftEnd)
        return;
    const float dx = finite(dxTimeline) ? dxTimeline : 0.f;
    m.restoreOrigin();
    const Band b    = m.g.band;
    const bool snap = m.snapActive(opt.invertSnap);
    switch (m.g.qsPart) {
    case QuickShiftPart::Bar:
        m.g.info.shift = m.shiftGroupImpl(b, dx, opt.invertSnap);
        break;
    case QuickShiftPart::Start:
    case QuickShiftPart::End: {
        const QuickShiftRange r    = m.qsRange(b);
        const bool            left = m.g.qsPart == QuickShiftPart::Start;
        const float           edge = left ? r.start : r.end;
        float q = m.map.fromSeam(clamp01(m.map.toSeam(edge) + dx));
        if (snap)
            q = m.map.snapNode(q);
        if (left)
            m.setQsRange(b, std::min(q, r.end), r.end, true);
        else
            m.setQsRange(b, r.start, std::max(q, r.start), true);
        m.g.info.shift = m.map.toSeam(q) - m.map.toSeam(edge);
        break;
    }
    case QuickShiftPart::NewRange: {
        float a = m.g.anchor;
        float q = m.map.fromSeam(clamp01(m.map.toSeam(a) + dx));
        if (snap) {
            a = m.map.snapNode(a);
            q = m.map.snapNode(q);
        }
        m.setQsRange(b, std::min(a, q), std::max(a, q), true);
        m.g.info.shift = dx;
        break;
    }
    }
    m.live();
    m.stateChanged();
}

bool EditorModel::beginStretch(StretchEdge edge)
{
    Impl& m = *d_;
    m.endGesture();
    const Band b  = m.editBand();
    int        i0 = 0, i1 = 0;
    if (!m.selectionSpan(b, i0, i1))
        return false;
    if ((edge == StretchEdge::Start && i0 == 0) || (edge == StretchEdge::End && i1 == m.env(b).size() - 1))
        return false;
    m.startGesture(GestureKind::Stretch, "Stretch", b);
    m.g.edge = edge;
    m.stateChanged();
    return true;
}

void EditorModel::updateStretch(float dxTimeline, const DragOptions& opt)
{
    Impl& m = *d_;
    if (m.g.kind != GestureKind::Stretch)
        return;
    const float dx = finite(dxTimeline) ? dxTimeline : 0.f;
    m.restoreOrigin();
    const Band    b    = m.g.band;
    ops::NodeList list = m.tagged(b);
    int           i0 = 0, i1 = 0;
    if (!m.selectionSpan(b, i0, i1))
        return;
    const int   n  = int(list.size());
    const float a  = list[size_t(i0)].x, c = list[size_t(i1)].x;
    const float lo = i0 > 0 ? list[size_t(i0) - 1].x : 0.f;
    const float hi = i1 < n - 1 ? list[size_t(i1) + 1].x : 1.f;
    const bool  start = m.g.edge == StretchEdge::Start;
    const float edge  = start ? a : c;
    float q = m.map.fromSeam(clamp01(m.map.toSeam(edge) + dx));
    if (m.snapActive(opt.invertSnap))
        q = m.map.snapNode(q);
    const float na = start ? std::clamp(q, lo, c) : a;
    const float nc = start ? c : std::clamp(q, a, hi);
    for (int i = i0; i <= i1; ++i)
        list[size_t(i)].x = na + (list[size_t(i)].x - a) * (nc - na) / (c - a);
    list[size_t(i0)].x = na;
    list[size_t(i1)].x = nc;
    m.store(b, list);
    m.g.info.shift = m.map.toSeam(start ? na : nc) - m.map.toSeam(edge);
    m.live();
    m.stateChanged();
}

GestureKind EditorModel::gesture() const noexcept
{
    if (d_->g.kind != GestureKind::None)
        return d_->g.kind;
    return d_->txns.empty() ? GestureKind::None : GestureKind::Custom;
}

GestureInfo EditorModel::gestureInfo() const
{
    GestureInfo info = d_->g.info;
    info.kind        = gesture();
    return info;
}

bool EditorModel::endGesture()
{
    return d_->endGesture();
}

void EditorModel::cancelGesture()
{
    d_->cancelGesture();
}

// ---- Quick Shift ---------------------------------------------------------------------------------------

QuickShiftRange EditorModel::quickShiftRange() const noexcept
{
    return d_->qsRange(d_->editBand());
}

bool EditorModel::isQuickShiftMember(int index) const noexcept
{
    const Envelope& e = d_->env(d_->editBand());
    return index > 0 && index < e.size() - 1 && (e.node(index).flags & kNodeQuickShift);
}

int EditorModel::quickShiftGroupSize() const noexcept
{
    return d_->groupSize(d_->editBand());
}

bool EditorModel::toggleQuickShiftMember(int index)
{
    Impl& m = *d_;
    return m.edit("Quick Shift Group", [&] {
        BandDoc& d = m.bd(m.editBand());
        if (index <= 0 || index >= d.env.size() - 1)
            return false;
        d.env.node(index).flags ^= uint32_t(kNodeQuickShift);
        d.qsExplicit = false;
        return true;
    });
}

bool EditorModel::setQuickShiftGroupFromSelection()
{
    Impl& m = *d_;
    return m.edit("Quick Shift Group", [&] {
        BandDoc& d = m.bd(m.editBand());
        const int n = d.env.size();
        for (int i = 0; i < n; ++i) {
            EnvNode&   e   = d.env.node(i);
            const bool mem = i > 0 && i < n - 1 && d.sel.test(size_t(i));
            e.flags = mem ? (e.flags | kNodeQuickShift) : (e.flags & ~uint32_t(kNodeQuickShift));
        }
        d.qsExplicit = false;
        return true;
    });
}

bool EditorModel::setQuickShiftRange(float startTimeline, float endTimeline, bool autoGroup)
{
    const TimelineMap& map = d_->map;
    if (!finite(startTimeline) || !finite(endTimeline))
        return false;
    const float sa = map.timelineToSeam(clamp01(startTimeline));
    float       sb = map.timelineToSeam(clamp01(endTimeline));
    if (sb < sa || (sb == sa && endTimeline > startTimeline))
        sb = 1.f;   // the end lies on or past the seam: stop at the cycle end
    return setQuickShiftRangeNode(map.fromSeam(sa), map.fromSeam(sb), autoGroup);
}

bool EditorModel::setQuickShiftRangeNode(float startX, float endX, bool autoGroup)
{
    Impl& m = *d_;
    return m.edit("Quick Shift Range", [&] {
        if (!finite(startX) || !finite(endX))
            return false;
        if (endX < startX)
            std::swap(startX, endX);
        m.setQsRange(m.editBand(), startX, endX, autoGroup);
        return true;
    });
}

bool EditorModel::autoGroupQuickShift()
{
    Impl& m = *d_;
    return m.edit("Quick Shift Group", [&] {
        const Band            b = m.editBand();
        const QuickShiftRange r = m.qsRange(b);
        if (!r.valid)
            return false;
        const bool wasExplicit = m.bd(b).qsExplicit;
        m.setQsRange(b, r.start, r.end, true);
        m.bd(b).qsExplicit = wasExplicit;
        return true;
    });
}

bool EditorModel::clearQuickShift()
{
    Impl& m = *d_;
    return m.edit("Clear Quick Shift", [&] {
        BandDoc& d = m.bd(m.editBand());
        for (int i = 0; i < d.env.size(); ++i)
            d.env.node(i).flags &= ~uint32_t(kNodeQuickShift);
        d.qsExplicit = false;
        return true;
    });
}

bool EditorModel::shiftGroup(float dxTimeline, bool invertSnap)
{
    Impl& m = *d_;
    return m.edit("Quick Shift", [&] { return m.shiftGroupImpl(m.editBand(), dxTimeline, invertSnap) != 0.f; });
}

void EditorModel::selectQuickShiftGroup()
{
    const Envelope& e = d_->env(d_->editBand());
    NodeMask        mask;
    for (int i = 1; i + 1 < e.size(); ++i)
        if (e.node(i).flags & kNodeQuickShift)
            mask.set(size_t(i));
    setSelection(mask);
}

// ---- transforms ----------------------------------------------------------------------------------------

bool EditorModel::invert()
{
    Impl& m = *d_;
    return m.edit("Invert", [&] {
        const Band    b    = m.editBand();
        ops::NodeList list = m.tagged(b);
        ops::invertY(list, m.bd(b).sel.any() ? ops::kTagSelected : 0u);
        return m.store(b, list);
    });
}

bool EditorModel::scaleY(float factor)
{
    Impl& m = *d_;
    return m.edit("Scale", [&] {
        if (!finite(factor))
            return false;
        const Band    b    = m.editBand();
        ops::NodeList list = m.tagged(b);
        ops::scaleY(list, factor, m.bd(b).sel.any() ? ops::kTagSelected : 0u);
        return m.store(b, list);
    });
}

bool EditorModel::reverse()
{
    return d_->spanTransform("Reverse", [](ops::NodeList& c) {
        ops::reverse(c);
        return true;
    });
}

bool EditorModel::rotateShape(float amount)
{
    if (!finite(amount))
        return false;
    return d_->spanTransform("Rotate", [amount](ops::NodeList& c) { return ops::rotate(c, amount); });
}

bool EditorModel::bakeRotation()
{
    Impl& m = *d_;
    return m.edit("Bake Rotation", [&] {
        const float r = m.map.rotation();
        if (r == 0.f)
            return false;
        const Band    b    = m.editBand();
        ops::NodeList list = m.tagged(b);
        clearTag(list, ops::kTagSelected);
        if (!m.rotateExact(list, r) || !m.store(b, list))
            return false;
        m.bd(b).qsExplicit = false;
        const float before = m.timing.rotateDeg;
        m.recordParam(kParamRotate, before, 0.f);
        m.emitParam(kParamRotate, 0.f);
        return true;
    });
}

bool EditorModel::duplicate()
{
    return d_->spanTransform("Duplicate", [](ops::NodeList& c) { return ops::duplicate(c); });
}

bool EditorModel::halve()
{
    return d_->spanTransform("Halve", [](ops::NodeList& c) { return ops::halve(c); });
}

bool EditorModel::setFlat(float level)
{
    if (!finite(level))
        return false;
    const float y = clamp01(level);
    return d_->spanTransform("Flatten", [y](ops::NodeList& c) {
        c = {EnvNode{0.f, y, 0.f, 0}, EnvNode{1.f, y, 0.f, 0}};
        return true;
    });
}

bool EditorModel::resetToDefault()
{
    Impl& m = *d_;
    return m.edit("Reset Shape", [&] {
        BandDoc& d  = m.bd(m.editBand());
        d.env        = Envelope();
        d.sel.reset();
        d.qsExplicit = false;
        return true;
    });
}

bool EditorModel::mirrorLeftToRight()
{
    return d_->spanTransform("Mirror", [](ops::NodeList& c) { return ops::mirrorLeftToRight(c); });
}

bool EditorModel::applyShape(const Envelope& shape, bool intoSelection)
{
    return d_->applyShapeImpl(shape, intoSelection, "Apply Shape");
}

bool EditorModel::stretchSelection(float startTimeline, float endTimeline)
{
    Impl& m = *d_;
    return m.edit("Stretch", [&] {
        if (!finite(startTimeline) || !finite(endTimeline))
            return false;
        const Band    b    = m.editBand();
        ops::NodeList list = m.tagged(b);
        int           i0 = 0, i1 = 0;
        if (!m.selectionSpan(b, i0, i1))
            return false;
        const int   n  = int(list.size());
        const float a  = list[size_t(i0)].x, c = list[size_t(i1)].x;
        const float lo = i0 > 0 ? list[size_t(i0) - 1].x : 0.f;
        const float hi = i1 < n - 1 ? list[size_t(i1) + 1].x : 1.f;
        const float sa = m.map.timelineToSeam(clamp01(startTimeline));
        float       sb = m.map.timelineToSeam(clamp01(endTimeline));
        if (sb < sa || (sb == sa && endTimeline > startTimeline))
            sb = 1.f;
        float na = i0 == 0 ? 0.f : std::clamp(m.map.fromSeam(sa), lo, hi);
        float nc = i1 == n - 1 ? 1.f : std::clamp(m.map.fromSeam(sb), lo, hi);
        if (nc < na)
            std::swap(na, nc);
        for (int i = i0; i <= i1; ++i)
            list[size_t(i)].x = na + (list[size_t(i)].x - a) * (nc - na) / (c - a);
        list[size_t(i0)].x = na;
        list[size_t(i1)].x = nc;
        return m.store(b, list);
    });
}

bool EditorModel::simplify(int maxNodes, float tolerance)
{
    if (!finite(tolerance))
        return false;
    return d_->spanTransform("Simplify", [maxNodes, tolerance](ops::NodeList& c) {
        return ops::simplify(c, std::max(maxNodes, 2), tolerance);
    }, true);
}

bool EditorModel::copyToBand(Band target)
{
    Impl& m = *d_;
    const Band src = m.editBand();
    if ((target != Band::A && target != Band::B) || target == src)
        return false;
    return m.edit(target == Band::B ? "Copy to High Band" : "Copy to Low Band", [&] {
        BandDoc&       dst = m.bd(target);
        const BandDoc& s   = m.bd(src);
        dst.env        = s.env;
        dst.sel.reset();
        dst.qsExplicit = s.qsExplicit;
        dst.qsStart    = s.qsStart;
        dst.qsEnd      = s.qsEnd;
        return true;
    });
}

// ---- capture -------------------------------------------------------------------------------------------

bool EditorModel::applyCapture(const float* recBuf, int bins, const CaptureOptions& opt, CaptureStats* stats)
{
    Impl&    m = *d_;
    Envelope e;
    if (!envelopeFromCapture(recBuf, bins, m.map.phaseMap(), opt, e, stats))
        return false;
    return m.edit("Capture", [&] {
        BandDoc& d  = m.bd(m.editBand());
        d.env        = e;
        d.sel.reset();
        d.qsExplicit = false;
        return true;
    });
}

} // namespace kick::editor
