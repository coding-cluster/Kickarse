// Kickarse — editor view mapping (see TimelineMap.h).
#include "TimelineMap.h"

#include <algorithm>
#include <cmath>

namespace kick::editor {

namespace {

constexpr int kMaxGridLines = 4096;

inline float clamp01(float v) noexcept
{
    return v > 0.f ? (v < 1.f ? v : 1.f) : 0.f; // NaN → 0
}

inline int roundIndex(float v, int lo, int hi) noexcept
{
    if (!std::isfinite(v))
        return lo;
    return std::clamp(int(std::lround(v)), lo, hi);
}

} // namespace

TimingParams TimingParams::fromParamValues(const float* values) noexcept
{
    TimingParams t;
    if (!values)
        return t;
    t.rotateDeg    = std::isfinite(values[kParamRotate]) ? values[kParamRotate] : 0.f;
    t.gridIndex    = roundIndex(values[kParamGrid], 0, kNumGrids - 1);
    t.swingPercent = std::isfinite(values[kParamSwing]) ? values[kParamSwing] : 0.f;
    t.timeMode     = roundIndex(values[kParamTimeMode], 0, 1);
    t.rateIndex    = roundIndex(values[kParamRate], 0, kNumRates - 1);
    t.lengthMs     = std::isfinite(values[kParamLengthMs]) ? values[kParamLengthMs] : 250.f;
    return t;
}

double cycleBeats(const TimingParams& t) noexcept
{
    if (t.timeMode == kTimeFree)
        return 1.0;
    return kRates[std::clamp(t.rateIndex, 0, kNumRates - 1)].beats;
}

PhaseMap makePhaseMap(const TimingParams& t) noexcept
{
    PhaseMap m;
    m.rotate01  = (std::isfinite(t.rotateDeg) ? t.rotateDeg : 0.f) / 360.f;
    m.divisions = float(std::max(1.0, cycleBeats(t) / kGrids[std::clamp(t.gridIndex, 0, kNumGrids - 1)].beats));
    m.swing     = (std::isfinite(t.swingPercent) ? t.swingPercent : 0.f) * 0.01f;
    return m;
}

// ---- TimelineMap --------------------------------------------------------------------------------

TimelineMap::TimelineMap() noexcept : TimelineMap(PhaseMap{}) {}

TimelineMap::TimelineMap(const PhaseMap& map) noexcept : map_(map), unrotated_(map)
{
    unrotated_.rotate01 = 0.f;

    // Mirror PhaseMap's own sanitising so both agree on edge cases.
    const double rot = std::isfinite(map.rotate01) ? double(map.rotate01) : 0.0;
    double r = rot - std::floor(rot);
    if (!(r >= 0.0 && r < 1.0))
        r = 0.0;
    rot_ = float(r);
    if (rot_ >= 1.f)
        rot_ = 0.f;

    const double div   = std::isfinite(map.divisions) ? std::max(1.0, double(map.divisions)) : 1.0;
    const double swing = std::isfinite(map.swing) ? std::clamp(double(map.swing), 0.0, 1.0) : 0.0;
    div_ = float(div);
    const double pairs = std::floor(div * 0.5 + 1e-9);
    swingActive_ = swing > 0.0 && pairs >= 1.0;
    swingEnd_    = swingActive_ ? float(std::min(1.0, pairs * 2.0 / div)) : 0.f;
}

float TimelineMap::toSeam(float nodeX) const noexcept
{
    if (!(nodeX > 0.f))
        return 0.f;
    if (nodeX >= 1.f)
        return 1.f;
    if (!swingActive_)
        return nodeX;
    const float s = unrotated_.toTimeline(nodeX);
    // PhaseMap folds a result that rounds up to 1.0 back to 0: swing moves points by < 0.5.
    return s < nodeX - 0.5f ? 1.f : s;
}

float TimelineMap::fromSeam(float seam) const noexcept
{
    if (!(seam > 0.f))
        return 0.f;
    if (seam >= 1.f)
        return 1.f;
    if (!swingActive_)
        return seam;
    const float q = unrotated_.toNode(seam);
    return q < seam - 0.5f ? 1.f : q;
}

float TimelineMap::seamToTimeline(float seam) const noexcept
{
    const float s = clamp01(seam);
    double p = double(s) + double(rot_);
    if (p > 1.0 || (p >= 1.0 && s < 1.f))
        p -= 1.0;
    return clamp01(float(p));
}

float TimelineMap::timelineToSeam(float timeline) const noexcept
{
    double v = double(std::isfinite(timeline) ? timeline : 0.f) - double(rot_);
    v -= std::floor(v);
    if (!(v >= 0.0 && v < 1.0))
        v = 0.0;
    const float f = float(v);
    return f < 1.f ? f : 0.f;
}

float TimelineMap::toTimeline(float nodeX) const noexcept
{
    return seamToTimeline(toSeam(nodeX));
}

float TimelineMap::toNode(float timeline) const noexcept
{
    return map_.toNode(std::isfinite(timeline) ? timeline : 0.f);
}

int TimelineMap::gridCount() const noexcept
{
    const double n = std::ceil(double(div_) - 1e-4);
    return std::clamp(int(n), 1, kMaxGridLines);
}

float TimelineMap::gridNode(int k) const noexcept
{
    if (k <= 0)
        return 0.f;
    const double x = double(k) / double(div_);
    return x < 1.0 ? float(x) : 1.f;
}

float TimelineMap::snapNode(float nodeX) const noexcept
{
    const float x = clamp01(nodeX);
    const float s = toSeam(x);
    const int   count = gridCount();
    const int   k = std::clamp(int(std::floor(double(x) * double(div_))), 0, count);
    float best = x, bestD = 2.f;
    for (int c = k - 1; c <= k + 2; ++c) {
        if (c < 0 || c > count)
            continue;
        const float g = c >= count ? 1.f : gridNode(c);
        const float d = std::fabs(toSeam(g) - s);
        if (d < bestD) {
            bestD = d;
            best  = g;
        }
    }
    return best;
}

float TimelineMap::snapDelta(float delta, float step) noexcept
{
    if (!std::isfinite(delta))
        return 0.f;
    if (!(step > 0.f) || !std::isfinite(step))
        return delta;
    return float(std::round(double(delta) / double(step)) * double(step));
}

void TimelineMap::kinksBetween(float x0, float x1, std::vector<float>& out) const
{
    out.clear();
    if (!swingActive_ || !(x1 > x0))
        return;
    const double lo = std::max(0.0, double(x0));
    const double hi = std::min(double(swingEnd_), double(x1));
    const int    k0 = std::max(1, int(std::floor(lo * double(div_))));
    const int    k1 = int(std::ceil(hi * double(div_)));
    for (int k = k0; k <= k1 && k <= kMaxGridLines; ++k) {
        const float x = gridNode(k);
        if (x > x0 + 1e-6f && x < x1 - 1e-6f && x <= swingEnd_ + 1e-6f)
            out.push_back(x);
    }
}

} // namespace kick::editor
