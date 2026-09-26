// Kickarse — envelope model implementation (see Envelope.h for the contract).
//
// Segment curve
//   shape(u, t) = (e^{k u} - 1) / (e^{k} - 1), k = t * kShapeK.
//   t > 0 holds the start value longer (the segment "eases in"), t < 0 moves away from it quickly.
//   The family satisfies shape(u, -t) = 1 - shape(1 - u, t) exactly, and near k = 0 a second-order
//   series is used so t = 0 is exactly linear and small tensions stay smooth.
//
// Evaluation
//   evaluate(): right-continuous: at a vertical step (several nodes with equal x) the value is that
//   of the last node at that x. Phases wrap into [0,1).
//   evaluateLeft(): left-continuous (value before a step), phase clamped to [0,1], 1 → last node.
//
// Serialisation format, version 1 (ASCII, locale independent):
//   "KE1 " followed by the nodes separated by ';', each node "x,y,t,f":
//     x, y, t  shortest round-trip decimal floats (std::to_chars, '.' decimal point, optional exponent)
//     f        EnvNodeFlags as an unsigned decimal integer (may be omitted when parsing → 0)
//   Example: "KE1 0,0,0.3,0;0.5,1,0,0;1,1,0,0"
//   Parsing ignores ASCII whitespace around tokens and accepts one trailing ';'. It rejects a missing
//   or unknown version tag, non-numeric or non-finite numbers, fewer than 2 or more than kMaxNodes
//   nodes, anything after the last node and inputs longer than kMaxTextLength. Accepted values are
//   clamped to their ranges and the node list is normalised.
#include "Envelope.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <vector>

namespace kick {

namespace {

constexpr float  kShapeK        = 6.f;      // |t| = 1 → k = 6: f(0.5) ≈ 0.047, strong but usable
constexpr float  kSeriesLimit   = 1e-3f;    // below this |k| the series is exact to float precision
constexpr size_t kMaxTextLength = 64 * 1024;

inline float clampf(float v, float lo, float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// NaN-safe: non-finite values become fallback, finite values are clamped.
inline float sanitise(float v, float lo, float hi, float fallback) noexcept
{
    if (!std::isfinite(v))
        return fallback;
    return clampf(v, lo, hi);
}

inline EnvNode sanitisedNode(const EnvNode& n) noexcept
{
    EnvNode s;
    s.x       = sanitise(n.x, 0.f, 1.f, 0.f);
    s.y       = sanitise(n.y, 0.f, 1.f, 1.f);
    s.tension = sanitise(n.tension, -1.f, 1.f, 0.f);
    s.flags   = n.flags;
    return s;
}

inline float segmentValue(const EnvNode& a, const EnvNode& b, float p) noexcept
{
    const float dx = b.x - a.x;
    if (!(dx > 0.f))
        return b.y;
    return a.y + (b.y - a.y) * Envelope::shape((p - a.x) / dx, a.tension);
}

// ---- parsing helpers ------------------------------------------------------------------------

struct Cursor {
    const char* p;
    const char* end;

    void skipSpace() noexcept
    {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
            ++p;
    }
    bool atEnd() noexcept
    {
        skipSpace();
        return p >= end;
    }
    bool expect(char c) noexcept
    {
        skipSpace();
        if (p < end && *p == c) {
            ++p;
            return true;
        }
        return false;
    }
    bool peek(char c) noexcept
    {
        skipSpace();
        return p < end && *p == c;
    }
    bool readFloat(float& out) noexcept
    {
        skipSpace();
        float v = 0.f;
        const auto r = std::from_chars(p, end, v, std::chars_format::general);
        if (r.ec != std::errc() || r.ptr == p || !std::isfinite(v))
            return false;
        p   = r.ptr;
        out = v;
        return true;
    }
    bool readUint(uint32_t& out) noexcept
    {
        skipSpace();
        uint32_t v = 0;
        const auto r = std::from_chars(p, end, v, 10);
        if (r.ec != std::errc() || r.ptr == p)
            return false;
        p   = r.ptr;
        out = v;
        return true;
    }
};

void appendFloat(std::string& s, float v)
{
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof(buf), v);
    s.append(buf, r.ptr);
}

void appendUint(std::string& s, uint32_t v)
{
    char buf[16];
    const auto r = std::to_chars(buf, buf + sizeof(buf), v);
    s.append(buf, r.ptr);
}

// ---- fitting helpers ------------------------------------------------------------------------

struct FitData {
    const std::vector<float>& x;
    const std::vector<float>& y;
};

// Max |error| over the interior points of [a,b] for a segment with the given tension.
float segmentError(const FitData& d, int a, int b, float tension) noexcept
{
    EnvNode na{d.x[size_t(a)], d.y[size_t(a)], tension, 0};
    EnvNode nb{d.x[size_t(b)], d.y[size_t(b)], 0.f, 0};
    float err = 0.f;
    for (int i = a + 1; i < b; ++i)
        err = std::max(err, std::fabs(segmentValue(na, nb, d.x[size_t(i)]) - d.y[size_t(i)]));
    return err;
}

// Minimax tension for one segment: coarse scan, then golden-section refinement around the best cell.
float fitTension(const FitData& d, int a, int b, float& bestErr) noexcept
{
    bestErr = segmentError(d, a, b, 0.f);
    float best = 0.f;
    if (b - a < 2 || std::fabs(d.y[size_t(b)] - d.y[size_t(a)]) < 1e-6f)
        return best;

    constexpr int kScan = 16;
    int bestCell = kScan / 2;
    for (int i = 0; i <= kScan; ++i) {
        const float t = -1.f + 2.f * float(i) / kScan;
        const float e = segmentError(d, a, b, t);
        if (e < bestErr) {
            bestErr  = e;
            best     = t;
            bestCell = i;
        }
    }

    float lo = -1.f + 2.f * float(std::max(bestCell - 1, 0)) / kScan;
    float hi = -1.f + 2.f * float(std::min(bestCell + 1, kScan)) / kScan;
    constexpr float kInvPhi = 0.6180339887f;
    float c  = hi - kInvPhi * (hi - lo);
    float dd = lo + kInvPhi * (hi - lo);
    float ec = segmentError(d, a, b, c);
    float ed = segmentError(d, a, b, dd);
    for (int it = 0; it < 24; ++it) {
        if (ec < ed) {
            hi = dd;
            dd = c;
            ed = ec;
            c  = hi - kInvPhi * (hi - lo);
            ec = segmentError(d, a, b, c);
        } else {
            lo = c;
            c  = dd;
            ec = ed;
            dd = lo + kInvPhi * (hi - lo);
            ed = segmentError(d, a, b, dd);
        }
    }
    const float t = 0.5f * (lo + hi);
    const float e = segmentError(d, a, b, t);
    if (e < bestErr) {
        bestErr = e;
        best    = t;
    }
    return best;
}

struct FitSegment {
    int   a;
    int   b;
    float tension;
};

// Curve-aware Ramer–Douglas–Peucker: a span is accepted when a single tensioned segment fits it
// within tolerance, otherwise it is split at the point of maximum deviation from its chord.
std::vector<FitSegment> fitSpans(const FitData& d, float tolerance)
{
    std::vector<FitSegment> out;
    std::vector<std::pair<int, int>> stack;
    stack.emplace_back(0, int(d.x.size()) - 1);
    while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        if (b - a <= 1) {
            out.push_back({a, b, 0.f});
            continue;
        }
        float err = 0.f;
        const float t = fitTension(d, a, b, err);
        if (err <= tolerance) {
            out.push_back({a, b, t});
            continue;
        }
        const float xa = d.x[size_t(a)], ya = d.y[size_t(a)];
        const float slope = (d.y[size_t(b)] - ya) / (d.x[size_t(b)] - xa);
        int   split = a + 1;
        float worst = -1.f;
        for (int i = a + 1; i < b; ++i) {
            const float dev = std::fabs(d.y[size_t(i)] - (ya + slope * (d.x[size_t(i)] - xa)));
            if (dev > worst) {
                worst = dev;
                split = i;
            }
        }
        stack.emplace_back(split, b);
        stack.emplace_back(a, split);
    }
    std::sort(out.begin(), out.end(), [](const FitSegment& l, const FitSegment& r) { return l.a < r.a; });
    return out;
}

} // namespace

// ---- construction -------------------------------------------------------------------------------

Envelope::Envelope()
{
    // Classic kick duck: silent at the downbeat, eased recovery back to unity by half the cycle.
    nodes_[0] = EnvNode{0.f, 0.f, 0.3f, 0};
    nodes_[1] = EnvNode{0.5f, 1.f, 0.f, 0};
    nodes_[2] = EnvNode{1.f, 1.f, 0.f, 0};
    count_    = 3;
}

Envelope Envelope::flat()
{
    Envelope e;
    e.nodes_[0] = EnvNode{0.f, 1.f, 0.f, 0};
    e.nodes_[1] = EnvNode{1.f, 1.f, 0.f, 0};
    e.count_    = 2;
    return e;
}

// ---- access & editing ---------------------------------------------------------------------------

int Envelope::size() const noexcept
{
    return count_;
}

const EnvNode& Envelope::node(int i) const noexcept
{
    return nodes_[std::clamp(i, 0, std::max(count_ - 1, 0))];
}

EnvNode& Envelope::node(int i) noexcept
{
    return nodes_[std::clamp(i, 0, std::max(count_ - 1, 0))];
}

int Envelope::insert(const EnvNode& n)
{
    if (count_ >= kMaxNodes)
        return -1;
    if (count_ < 2)
        normalise();

    const EnvNode s = sanitisedNode(n);
    int idx = 0;
    while (idx < count_ && nodes_[idx].x <= s.x)
        ++idx;
    idx = std::clamp(idx, 1, count_ - 1); // endpoints stay first and last

    for (int i = count_; i > idx; --i)
        nodes_[i] = nodes_[i - 1];
    nodes_[idx] = s;
    ++count_;
    return idx;
}

void Envelope::remove(int i)
{
    if (i <= 0 || i >= count_ - 1)
        return;
    for (int k = i; k < count_ - 1; ++k)
        nodes_[k] = nodes_[k + 1];
    --count_;
}

void Envelope::normalise()
{
    if (count_ <= 0) {
        *this = flat();
        return;
    }
    for (int i = 0; i < count_; ++i)
        nodes_[i] = sanitisedNode(nodes_[i]);
    if (count_ == 1) {
        nodes_[1]   = nodes_[0];
        nodes_[1].x = 1.f;
        count_      = 2;
    }
    // Insertion sort: stable, so the order of nodes sharing an x (vertical steps) is preserved.
    for (int i = 1; i < count_; ++i) {
        const EnvNode v = nodes_[i];
        int j = i - 1;
        while (j >= 0 && nodes_[j].x > v.x) {
            nodes_[j + 1] = nodes_[j];
            --j;
        }
        nodes_[j + 1] = v;
    }
    nodes_[0].x          = 0.f;
    nodes_[count_ - 1].x = 1.f;
}

// ---- evaluation ---------------------------------------------------------------------------------

float Envelope::shape(float u, float tension) noexcept
{
    if (!(u > 0.f))
        return 0.f;
    if (u >= 1.f)
        return 1.f;
    const float k = sanitise(tension, -1.f, 1.f, 0.f) * kShapeK;
    if (std::fabs(k) < kSeriesLimit)
        return u + 0.5f * k * u * (u - 1.f);
    return clampf(std::expm1(k * u) / std::expm1(k), 0.f, 1.f);
}

float Envelope::evaluate(float phase) const noexcept
{
    if (count_ < 2)
        return count_ == 1 ? nodes_[0].y : 1.f;
    float p = phase - std::floor(phase);
    if (!(p >= 0.f && p < 1.f))
        p = 0.f; // NaN / Inf, or 1.0 produced by rounding a tiny negative phase

    // First node with x > p (upper bound); the segment starts at the node before it.
    int lo = 0, hi = count_;
    while (lo < hi) {
        const int mid = (lo + hi) >> 1;
        if (nodes_[mid].x <= p)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= count_)
        return nodes_[count_ - 1].y;
    if (lo == 0)
        return nodes_[0].y;
    return segmentValue(nodes_[lo - 1], nodes_[lo], p);
}

float Envelope::evaluateLeft(float phase) const noexcept
{
    if (count_ < 2)
        return count_ == 1 ? nodes_[0].y : 1.f;
    if (!(phase > 0.f))
        return nodes_[0].y; // also NaN
    if (phase >= 1.f)
        return nodes_[count_ - 1].y;

    // First node with x >= phase (lower bound): at a step that is the first node of the step, so
    // the segment arriving there yields the value before the jump.
    int lo = 0, hi = count_;
    while (lo < hi) {
        const int mid = (lo + hi) >> 1;
        if (nodes_[mid].x < phase)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= count_)
        return nodes_[count_ - 1].y;
    if (lo == 0)
        return nodes_[0].y;
    return segmentValue(nodes_[lo - 1], nodes_[lo], phase);
}

void Envelope::render(float* out, int n) const noexcept
{
    if (!out || n <= 0)
        return;
    if (count_ < 2) {
        for (int i = 0; i < n; ++i)
            out[i] = evaluate(0.f);
        return;
    }
    int j = 0; // first node with x > p
    for (int i = 0; i < n; ++i) {
        const float p = float(i) / float(n);
        while (j < count_ && nodes_[j].x <= p)
            ++j;
        if (j >= count_)
            out[i] = nodes_[count_ - 1].y;
        else if (j == 0)
            out[i] = nodes_[0].y;
        else
            out[i] = segmentValue(nodes_[j - 1], nodes_[j], p);
    }
}

// ---- serialisation ------------------------------------------------------------------------------

std::string Envelope::serialize() const
{
    std::string s = "KE1 ";
    s.reserve(size_t(4 + count_ * 28));
    for (int i = 0; i < count_; ++i) {
        const EnvNode& n = nodes_[i];
        appendFloat(s, n.x);
        s += ',';
        appendFloat(s, n.y);
        s += ',';
        appendFloat(s, n.tension);
        s += ',';
        appendUint(s, n.flags);
        if (i + 1 < count_)
            s += ';';
    }
    return s;
}

bool Envelope::deserialize(std::string_view text)
{
    if (text.size() > kMaxTextLength)
        return false;

    Cursor c{text.data(), text.data() + text.size()};
    c.skipSpace();
    if (size_t(c.end - c.p) < 3 || c.p[0] != 'K' || c.p[1] != 'E' || c.p[2] != '1')
        return false;
    c.p += 3;
    if (c.p < c.end && !(*c.p == ' ' || *c.p == '\t' || *c.p == '\r' || *c.p == '\n'))
        return false; // e.g. "KE12": a different (future) version

    Envelope tmp;
    tmp.count_ = 0;
    while (!c.atEnd()) {
        if (tmp.count_ >= kMaxNodes)
            return false;
        EnvNode n;
        if (!c.readFloat(n.x) || !c.expect(',') || !c.readFloat(n.y) || !c.expect(',')
            || !c.readFloat(n.tension))
            return false;
        if (c.expect(',') && !c.readUint(n.flags))
            return false;
        tmp.nodes_[tmp.count_++] = n;
        if (c.atEnd())
            break;
        if (!c.expect(';'))
            return false;
    }
    if (tmp.count_ < 2)
        return false;

    tmp.normalise();
    *this = tmp;
    return true;
}

// ---- fitting ------------------------------------------------------------------------------------

Envelope Envelope::fromSamples(const float* y, int n, float tolerance)
{
    if (!y || n <= 0)
        return flat();

    // Sample i sits at x = i / n (the render() convention); the curve is held to x = 1.
    std::vector<float> xs(size_t(n) + 1), ys(size_t(n) + 1);
    for (int i = 0; i < n; ++i) {
        xs[size_t(i)] = float(i) / float(n);
        ys[size_t(i)] = sanitise(y[i], 0.f, 1.f, 1.f);
    }
    xs[size_t(n)] = 1.f;
    ys[size_t(n)] = ys[size_t(n) - 1];

    float tol = std::isfinite(tolerance) ? clampf(tolerance, 1e-4f, 0.5f) : 0.01f;
    const FitData data{xs, ys};
    for (;;) {
        const std::vector<FitSegment> spans = fitSpans(data, tol);
        if (int(spans.size()) + 1 <= kMaxNodes || tol >= 1.f) {
            Envelope e;
            e.count_ = 0;
            for (const FitSegment& s : spans) {
                if (e.count_ >= kMaxNodes - 1)
                    break;
                e.nodes_[e.count_++] = EnvNode{xs[size_t(s.a)], ys[size_t(s.a)], s.tension, 0};
            }
            e.nodes_[e.count_++] = EnvNode{1.f, ys[size_t(n)], 0.f, 0};
            e.normalise();
            return e;
        }
        tol *= 1.5f;
    }
}

// ---- PhaseMap -----------------------------------------------------------------------------------
// Swing warps node space → timeline over pairs of grid cells: within each complete pair the first
// cell stretches from 1/2 to m = 1/2 + swing/4 of the pair (m = 3/4 at swing 1), the second cell
// shrinks accordingly. Pair boundaries are fixed points, so the warp is continuous and monotonic.
// A trailing partial pair (divisions not a multiple of 2) is left straight so the map stays a
// bijection of [0,1). Computed in double so toNode(toTimeline(q)) == q to float precision.

namespace {

struct SwingGeometry {
    double pair;     // width of a pair of cells, in cycle units
    double mid;      // warped position of the pair midpoint, as a fraction of the pair
    double fullEnd;  // end of the last complete pair
    bool   active;
};

SwingGeometry swingGeometry(const PhaseMap& m) noexcept
{
    SwingGeometry g{1.0, 0.5, 0.0, false};
    const double div   = std::isfinite(m.divisions) ? std::max(1.0, double(m.divisions)) : 1.0;
    const double swing = std::isfinite(m.swing) ? std::clamp(double(m.swing), 0.0, 1.0) : 0.0;
    g.pair = 2.0 / div;
    g.mid  = 0.5 + 0.25 * swing;
    const double pairs = std::floor(div * 0.5 + 1e-9);
    g.fullEnd = pairs * g.pair;
    g.active  = swing > 0.0 && pairs >= 1.0;
    return g;
}

inline double wrap01(double v) noexcept
{
    v -= std::floor(v);
    return (v >= 0.0 && v < 1.0) ? v : 0.0;
}

inline float toUnitFloat(double v) noexcept
{
    const float f = float(v);
    return f < 1.f ? f : 0.f; // v just below 1 may round up to 1.0f; that is phase 0 again
}

inline double rotation(const PhaseMap& m) noexcept
{
    return std::isfinite(m.rotate01) ? double(m.rotate01) : 0.0;
}

} // namespace

float PhaseMap::toNode(float timelinePhase) const noexcept
{
    const double p = wrap01(double(std::isfinite(timelinePhase) ? timelinePhase : 0.f) - rotation(*this));
    const SwingGeometry g = swingGeometry(*this);
    if (!g.active || p >= g.fullEnd)
        return toUnitFloat(p);
    const double k = std::floor(p / g.pair);
    const double v = (p - k * g.pair) / g.pair; // warped position inside the pair
    const double u = v < g.mid ? v * (0.5 / g.mid) : 0.5 + (v - g.mid) * (0.5 / (1.0 - g.mid));
    return toUnitFloat((k + u) * g.pair);
}

float PhaseMap::toTimeline(float nodePhase) const noexcept
{
    double q = wrap01(double(std::isfinite(nodePhase) ? nodePhase : 0.f));
    const SwingGeometry g = swingGeometry(*this);
    if (g.active && q < g.fullEnd) {
        const double k = std::floor(q / g.pair);
        const double u = (q - k * g.pair) / g.pair;
        const double v = u < 0.5 ? u * (g.mid / 0.5) : g.mid + (u - 0.5) * ((1.0 - g.mid) / 0.5);
        q = (k + v) * g.pair;
    }
    return toUnitFloat(wrap01(q + rotation(*this)));
}

} // namespace kick
