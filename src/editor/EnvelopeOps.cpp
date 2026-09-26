// Kickarse — pure node-list surgery (see EnvelopeOps.h).
#include "EnvelopeOps.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kick::editor::ops {

namespace {

constexpr int kMax = Envelope::kMaxNodes;

inline float clamp01(float v) noexcept
{
    return v > 0.f ? (v < 1.f ? v : 1.f) : 0.f; // NaN → 0
}

inline float clampTension(float t) noexcept
{
    return std::isfinite(t) ? std::clamp(t, -1.f, 1.f) : 0.f;
}

inline EnvNode sanitised(const EnvNode& n) noexcept
{
    EnvNode s = n;
    s.x       = std::isfinite(n.x) ? clamp01(n.x) : 0.f;
    s.y       = std::isfinite(n.y) ? clamp01(n.y) : 1.f;
    s.tension = clampTension(n.tension);
    return s;
}

// x clamped to [0,1] and made non-decreasing; first.x = 0, last.x = 1.
void enforceOrder(NodeList& v) noexcept
{
    if (v.empty())
        return;
    float prev = 0.f;
    for (EnvNode& n : v) {
        n.x = clamp01(n.x);
        if (n.x < prev)
            n.x = prev;
        prev = n.x;
    }
    v.front().x = 0.f;
    v.back().x  = 1.f;
}

inline float segmentAt(const EnvNode& a, const EnvNode& b, float tension, float x) noexcept
{
    const float dx = b.x - a.x;
    if (!(dx > 0.f))
        return b.y;
    const float u = clamp01((x - a.x) / dx);
    return a.y + (b.y - a.y) * Envelope::shape(u, tension);
}

// ---- greedy decimation ------------------------------------------------------------------------

struct RemovalFit {
    float cost    = std::numeric_limits<float>::max();
    float tension = 0.f;
};

// Max error of the merged segment a→b (tension t) against the reference curve over (xa, xb).
float mergedError(const NodeList& ref, const EnvNode& a, const EnvNode& b, float t,
                  const std::vector<float>& probes) noexcept
{
    float err = 0.f;
    for (float x : probes) {
        const float s  = segmentAt(a, b, t, x);
        const float eR = std::fabs(s - valueRight(ref, x));
        const float eL = std::fabs(s - valueLeft(ref, x));
        err = std::max(err, std::max(eR, eL));
    }
    return err;
}

RemovalFit removalFit(const NodeList& v, int i, const NodeList& ref)
{
    RemovalFit fit;
    const EnvNode& a = v[size_t(i) - 1];
    const EnvNode& b = v[size_t(i) + 1];
    if (!(b.x > a.x)) {
        // A node inside a vertical stack: removing it changes nothing audible.
        fit.cost    = 0.f;
        fit.tension = a.tension;
        return fit;
    }
    std::vector<float> probes;
    constexpr int kUniform = 24;
    probes.reserve(kUniform + 8);
    for (int k = 1; k < kUniform; ++k)
        probes.push_back(a.x + (b.x - a.x) * float(k) / float(kUniform));
    for (const EnvNode& r : ref)
        if (r.x > a.x && r.x < b.x)
            probes.push_back(r.x);
    probes.push_back(v[size_t(i)].x);

    if (std::fabs(b.y - a.y) < 1e-7f) {
        fit.tension = 0.f;
        fit.cost    = mergedError(ref, a, b, 0.f, probes);
        return fit;
    }
    constexpr int kScan = 16;
    int bestCell = kScan / 2;
    for (int k = 0; k <= kScan; ++k) {
        const float t = -1.f + 2.f * float(k) / float(kScan);
        const float e = mergedError(ref, a, b, t, probes);
        if (e < fit.cost) {
            fit.cost    = e;
            fit.tension = t;
            bestCell    = k;
        }
    }
    float lo = -1.f + 2.f * float(std::max(bestCell - 1, 0)) / float(kScan);
    float hi = -1.f + 2.f * float(std::min(bestCell + 1, kScan)) / float(kScan);
    constexpr float kInvPhi = 0.6180339887f;
    float c  = hi - kInvPhi * (hi - lo);
    float d  = lo + kInvPhi * (hi - lo);
    float ec = mergedError(ref, a, b, c, probes);
    float ed = mergedError(ref, a, b, d, probes);
    for (int it = 0; it < 20; ++it) {
        if (ec < ed) {
            hi = d;
            d  = c;
            ed = ec;
            c  = hi - kInvPhi * (hi - lo);
            ec = mergedError(ref, a, b, c, probes);
        } else {
            lo = c;
            c  = d;
            ec = ed;
            d  = lo + kInvPhi * (hi - lo);
            ed = mergedError(ref, a, b, d, probes);
        }
    }
    const float t = 0.5f * (lo + hi);
    const float e = mergedError(ref, a, b, t, probes);
    if (e < fit.cost) {
        fit.cost    = e;
        fit.tension = t;
    }
    return fit;
}

// Removes interior nodes greedily (cheapest first) while the list is longer than maxNodes or the
// cheapest removal costs at most tolerance (tolerance < 0: only enforce maxNodes).
void greedyDecimate(NodeList& v, int maxNodes, float tolerance)
{
    maxNodes = std::clamp(maxNodes, 2, kMax);
    if (v.size() <= 2)
        return;
    const NodeList ref = v;
    std::vector<RemovalFit> fits(v.size());
    auto refresh = [&](int i) {
        if (i >= 1 && i + 1 < int(v.size()))
            fits[size_t(i)] = removalFit(v, i, ref);
    };
    for (int i = 1; i + 1 < int(v.size()); ++i)
        refresh(i);

    while (v.size() > 2) {
        int best = -1;
        for (int i = 1; i + 1 < int(v.size()); ++i)
            if (best < 0 || fits[size_t(i)].cost < fits[size_t(best)].cost)
                best = i;
        if (best < 0)
            break;
        const bool mustRemove = int(v.size()) > maxNodes;
        if (!mustRemove && !(tolerance >= 0.f && fits[size_t(best)].cost <= tolerance))
            break;
        v[size_t(best) - 1].tension = fits[size_t(best)].tension;
        v.erase(v.begin() + best);
        fits.erase(fits.begin() + best);
        refresh(best - 1);
        refresh(best);
    }
}

} // namespace

// ---- conversion ---------------------------------------------------------------------------------

NodeList toList(const Envelope& env)
{
    NodeList v;
    v.reserve(size_t(env.size()));
    for (int i = 0; i < env.size(); ++i)
        v.push_back(env.node(i));
    return v;
}

bool fromList(const NodeList& nodes, Envelope& out)
{
    if (nodes.size() < 2 || nodes.size() > size_t(kMax))
        return false;
    Envelope e = Envelope::flat();
    e.node(0)   = sanitised(nodes.front());
    e.node(0).x = 0.f;
    e.node(1)   = sanitised(nodes.back());
    e.node(1).x = 1.f;
    for (size_t i = 1; i + 1 < nodes.size(); ++i)
        if (e.insert(sanitised(nodes[i])) < 0)
            return false;
    e.normalise();
    e.node(e.size() - 1).tension = 0.f;
    out = e;
    return true;
}

void strip(Envelope& env, uint32_t bits) noexcept
{
    for (int i = 0; i < env.size(); ++i)
        env.node(i).flags &= ~bits;
}

// ---- inspection ---------------------------------------------------------------------------------

bool isValid(const Envelope& env, std::string* why)
{
    auto bad = [why](const char* msg) {
        if (why)
            *why = msg;
        return false;
    };
    const int n = env.size();
    if (n < 2 || n > kMax)
        return bad("node count out of range");
    float prev = 0.f;
    for (int i = 0; i < n; ++i) {
        const EnvNode& e = env.node(i);
        if (!std::isfinite(e.x) || !std::isfinite(e.y) || !std::isfinite(e.tension))
            return bad("non-finite value");
        if (e.x < 0.f || e.x > 1.f)
            return bad("x out of range");
        if (e.y < 0.f || e.y > 1.f)
            return bad("y out of range");
        if (e.tension < -1.f || e.tension > 1.f)
            return bad("tension out of range");
        if (e.x < prev)
            return bad("x not sorted");
        prev = e.x;
    }
    if (env.node(0).x != 0.f)
        return bad("first node not at x = 0");
    if (env.node(n - 1).x != 1.f)
        return bad("last node not at x = 1");
    return true;
}

bool sameNodes(const Envelope& a, const Envelope& b, float tol) noexcept
{
    if (a.size() != b.size())
        return false;
    const int n = a.size();
    for (int i = 0; i < n; ++i) {
        const EnvNode& p = a.node(i);
        const EnvNode& q = b.node(i);
        if (!(std::fabs(p.x - q.x) <= tol) || !(std::fabs(p.y - q.y) <= tol) || p.flags != q.flags)
            return false;
        if (i + 1 < n && !(std::fabs(p.tension - q.tension) <= tol))
            return false;
    }
    return true;
}

float curveDistance(const Envelope& a, const Envelope& b, int samples)
{
    samples = std::max(samples, 1);
    float d = 0.f;
    for (int i = 0; i < samples; ++i) {
        const float p = float(i) / float(samples);
        d = std::max(d, std::fabs(a.evaluate(p) - b.evaluate(p)));
    }
    auto probe = [&](float x) {
        d = std::max(d, std::fabs(a.evaluateLeft(x) - b.evaluateLeft(x)));
        if (x < 1.f)
            d = std::max(d, std::fabs(a.evaluate(x) - b.evaluate(x)));
    };
    for (int i = 0; i < a.size(); ++i)
        probe(a.node(i).x);
    for (int i = 0; i < b.size(); ++i)
        probe(b.node(i).x);
    return d;
}

float segmentValue(const NodeList& nodes, int i, float x) noexcept
{
    if (i < 0 || size_t(i) + 1 >= nodes.size())
        return nodes.empty() ? 1.f : nodes.back().y;
    const EnvNode& a = nodes[size_t(i)];
    return segmentAt(a, nodes[size_t(i) + 1], a.tension, x);
}

float valueLeft(const NodeList& nodes, float x) noexcept
{
    if (nodes.empty())
        return 1.f;
    // first node with node.x >= x: the segment arriving there ends at the value before any step.
    const auto it = std::lower_bound(nodes.begin(), nodes.end(), x,
                                     [](const EnvNode& n, float v) { return n.x < v; });
    if (it == nodes.begin())
        return nodes.front().y;
    if (it == nodes.end())
        return nodes.back().y;
    const int j = int(it - nodes.begin());
    return segmentValue(nodes, j - 1, x);
}

float valueRight(const NodeList& nodes, float x) noexcept
{
    if (nodes.empty())
        return 1.f;
    // first node with node.x > x: the segment leaving x starts at the last node at or before x.
    const auto it = std::upper_bound(nodes.begin(), nodes.end(), x,
                                     [](float v, const EnvNode& n) { return v < n.x; });
    if (it == nodes.begin())
        return nodes.front().y;
    if (it == nodes.end())
        return nodes.back().y;
    const int j = int(it - nodes.begin());
    return segmentValue(nodes, j - 1, x);
}

// ---- surgery ------------------------------------------------------------------------------------

int cut(NodeList& nodes, float x, bool lastAtX)
{
    if (nodes.size() < 2 || !std::isfinite(x))
        return -1;
    x = clamp01(x);
    int first = -1, last = -1;
    for (int i = 0; i < int(nodes.size()); ++i) {
        if (std::fabs(nodes[size_t(i)].x - x) <= kSameX) {
            if (first < 0)
                first = i;
            last = i;
        }
    }
    if (first >= 0)
        return lastAtX ? last : first;

    const auto it = std::upper_bound(nodes.begin(), nodes.end(), x,
                                     [](float v, const EnvNode& n) { return v < n.x; });
    if (it == nodes.begin() || it == nodes.end())
        return -1;
    const int j  = int(it - nodes.begin());
    EnvNode&  a  = nodes[size_t(j) - 1];
    const EnvNode& b = nodes[size_t(j)];
    const float dx = b.x - a.x;
    if (!(dx > 0.f))
        return -1;
    const float c = clamp01((x - a.x) / dx);
    EnvNode m;
    m.x       = x;
    m.y       = clamp01(a.y + (b.y - a.y) * Envelope::shape(c, a.tension));
    m.tension = clampTension(a.tension * (1.f - c));
    m.flags   = 0;
    a.tension = clampTension(a.tension * c);
    nodes.insert(nodes.begin() + j, m);
    return j;
}

bool replaceSpan(NodeList& nodes, float x0, float x1, const NodeList& piece)
{
    if (nodes.size() < 2 || piece.size() < 2 || !std::isfinite(x0) || !std::isfinite(x1))
        return false;
    x0 = clamp01(x0);
    x1 = clamp01(x1);
    if (x1 < x0)
        return false;

    NodeList v = nodes;
    const int iL = cut(v, x0, false);
    if (iL < 0)
        return false;
    int iR = cut(v, x1, true);
    if (iR < 0)
        return false;
    if (iR <= iL) {
        v.insert(v.begin() + iL + 1, v[size_t(iL)]);
        iR = iL + 1;
    }

    const EnvNode& pf = piece.front();
    const EnvNode& pb = piece.back();
    EnvNode L = v[size_t(iL)];
    EnvNode R = v[size_t(iR)];
    L.y       = std::isfinite(pf.y) ? clamp01(pf.y) : L.y;
    L.tension = clampTension(pf.tension);
    L.flags  |= pf.flags;
    R.y       = std::isfinite(pb.y) ? clamp01(pb.y) : R.y;
    R.flags  |= pb.flags;

    NodeList out;
    out.reserve(v.size() + piece.size());
    out.insert(out.end(), v.begin(), v.begin() + iL);
    out.push_back(L);
    for (size_t k = 1; k + 1 < piece.size(); ++k) {
        EnvNode m = sanitised(piece[k]);
        m.x       = std::clamp(m.x, L.x, R.x);
        out.push_back(m);
    }
    out.push_back(R);
    out.insert(out.end(), v.begin() + iR + 1, v.end());
    if (out.size() > size_t(kMax))
        return false;
    enforceOrder(out);
    nodes.swap(out);
    return true;
}

bool transformSpan(NodeList& nodes, int i0, int i1, const std::function<bool(NodeList&)>& fn)
{
    const int n = int(nodes.size());
    if (!fn || n < 2 || i0 < 0 || i1 >= n || i1 <= i0)
        return false;
    const float a = nodes[size_t(i0)].x;
    const float b = nodes[size_t(i1)].x;
    const float w = b - a;
    if (!(w > 0.f))
        return false;

    NodeList content(nodes.begin() + i0, nodes.begin() + i1 + 1);
    for (EnvNode& c : content)
        c.x = clamp01((c.x - a) / w);
    content.front().x = 0.f;
    content.back().x  = 1.f;
    const float outgoing = nodes[size_t(i1)].tension;

    if (!fn(content) || content.size() < 2)
        return false;
    for (EnvNode& c : content) {
        c   = sanitised(c);
        c.x = std::clamp(a + c.x * w, a, b);
    }
    content.front().x       = a;
    content.back().x        = b;
    content.back().tension  = outgoing;

    NodeList out;
    out.reserve(size_t(n) + content.size());
    out.insert(out.end(), nodes.begin(), nodes.begin() + i0);
    out.insert(out.end(), content.begin(), content.end());
    out.insert(out.end(), nodes.begin() + i1 + 1, nodes.end());
    if (out.size() > size_t(kMax))
        return false;
    enforceOrder(out);
    nodes.swap(out);
    return true;
}

// ---- transforms ---------------------------------------------------------------------------------

void invertY(NodeList& nodes, uint32_t onlyWithFlag) noexcept
{
    for (EnvNode& n : nodes)
        if (onlyWithFlag == 0 || (n.flags & onlyWithFlag))
            n.y = clamp01(1.f - n.y);
}

void scaleY(NodeList& nodes, float factor, uint32_t onlyWithFlag) noexcept
{
    if (!std::isfinite(factor))
        return;
    factor = std::max(factor, 0.f);
    for (EnvNode& n : nodes)
        if (onlyWithFlag == 0 || (n.flags & onlyWithFlag))
            n.y = clamp01(1.f - factor * (1.f - n.y));
}

void reverse(NodeList& nodes)
{
    const size_t n = nodes.size();
    if (n < 2)
        return;
    NodeList r(n);
    for (size_t j = 0; j < n; ++j) {
        const EnvNode& src = nodes[n - 1 - j];
        r[j]         = src;
        r[j].x       = 1.f - src.x;
        r[j].tension = j + 1 < n ? -nodes[n - 2 - j].tension : 0.f;
    }
    enforceOrder(r);
    nodes.swap(r);
}

bool rotate(NodeList& nodes, float amount)
{
    if (nodes.size() < 2 || !std::isfinite(amount))
        return false;
    double dd = double(amount) - std::floor(double(amount));
    if (!(dd >= 0.0 && dd < 1.0))
        dd = 0.0;
    const float d = float(dd);
    if (d <= 1e-7f || d >= 1.f - 1e-7f)
        return true; // a whole-cycle shift: nothing to do

    const float c = 1.f - d;  // old x that becomes the new start
    NodeList v = nodes;
    const int iL = cut(v, c, false);
    if (iL < 0)
        return false;
    int iR = cut(v, c, true);
    if (iR < 0)
        return false;
    if (iR <= iL) {
        v.insert(v.begin() + iL + 1, v[size_t(iL)]);
        iR = iL + 1;
    }
    NodeList r;
    r.reserve(v.size());
    for (size_t k = size_t(iR); k < v.size(); ++k) {  // [c, 1] → [0, d]
        EnvNode m = v[k];
        m.x       = std::min(v[k].x - c, d);
        r.push_back(m);
    }
    r.back().tension = 0.f;  // old last → old first: a zero-width step at d
    for (size_t k = 0; k <= size_t(iL); ++k) {       // [0, c] → [d, 1]
        EnvNode m = v[k];
        m.x       = std::max(v[k].x + d, d);
        r.push_back(m);
    }
    r.back().tension = 0.f;
    if (r.size() > size_t(kMax))
        return false;
    enforceOrder(r);
    nodes.swap(r);
    return true;
}

bool duplicate(NodeList& nodes)
{
    const size_t n = nodes.size();
    if (n < 2)
        return false;
    const bool merge = nodes.back().y == nodes.front().y && nodes.back().flags == nodes.front().flags;
    if ((merge ? 2 * n - 1 : 2 * n) > size_t(kMax))
        return false;
    NodeList r;
    r.reserve(2 * n);
    for (const EnvNode& src : nodes) {
        EnvNode m = src;
        m.x       = src.x * 0.5f;
        r.push_back(m);
    }
    r.back().tension = 0.f;
    if (merge)
        r.pop_back();
    for (const EnvNode& src : nodes) {
        EnvNode m = src;
        m.x       = 0.5f + src.x * 0.5f;
        r.push_back(m);
    }
    enforceOrder(r);
    nodes.swap(r);
    return true;
}

bool halve(NodeList& nodes)
{
    if (nodes.size() < 2)
        return false;
    NodeList v = nodes;
    const int i = cut(v, 0.5f, false);
    if (i < 1)
        return false;
    NodeList r(v.begin(), v.begin() + i + 1);
    for (EnvNode& m : r)
        m.x = std::min(m.x * 2.f, 1.f);
    r.back().tension = 0.f;
    if (r.size() > size_t(kMax))
        return false;
    enforceOrder(r);
    nodes.swap(r);
    return true;
}

bool mirrorLeftToRight(NodeList& nodes)
{
    if (nodes.size() < 2)
        return false;
    NodeList v = nodes;
    const int i = cut(v, 0.5f, false);
    if (i < 1)
        return false;
    const size_t m = size_t(i) + 1;  // left half: v[0 .. i], v[i].x = 0.5
    if (2 * m - 1 > size_t(kMax))
        return false;
    NodeList r(v.begin(), v.begin() + i + 1);
    r.back().tension = -v[m - 2].tension;  // the mirrored image of the segment arriving at 0.5
    for (size_t j = 1; j < m; ++j) {
        const EnvNode& src = v[m - 1 - j];
        EnvNode c          = src;
        c.x                = 1.f - src.x;
        c.tension          = j + 1 < m ? -v[m - 2 - j].tension : 0.f;
        c.flags            = src.flags & ~uint32_t(kNodeQuickShift);
        r.push_back(c);
    }
    enforceOrder(r);
    nodes.swap(r);
    return true;
}

bool simplify(NodeList& nodes, int maxNodes, float tolerance)
{
    if (nodes.size() < 2)
        return false;
    greedyDecimate(nodes, maxNodes, std::isfinite(tolerance) ? std::max(tolerance, 0.f) : 0.f);
    enforceOrder(nodes);
    return true;
}

void decimate(Envelope& env, int maxNodes)
{
    if (env.size() <= std::clamp(maxNodes, 2, kMax))
        return;
    NodeList v = toList(env);
    greedyDecimate(v, maxNodes, -1.f);
    enforceOrder(v);
    Envelope out;
    if (fromList(v, out))
        env = out;
}

} // namespace kick::editor::ops
