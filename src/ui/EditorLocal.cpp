// Kickarse UI — display-only editor backend (see EditorLocal.h).
#include "EditorLocal.h"

#include <algorithm>
#include <cmath>

namespace kick { namespace ui {

PhaseMap LocalEditorBackend::map() const
{
    const bool free = m_.ivalue(kParamTimeMode) == kTimeFree;
    const double cyc = free ? 1.0 : kRates[std::clamp(m_.ivalue(kParamRate), 0, kNumRates - 1)].beats;
    const double grid = kGrids[std::clamp(m_.ivalue(kParamGrid), 0, kNumGrids - 1)].beats;
    return PhaseMap {m_.value(kParamRotate) / 360.f, std::max(1.f, float(cyc / grid)), m_.value(kParamSwing) / 100.f};
}

float LocalEditorBackend::timelineOf(float x) const
{
    const PhaseMap pm = map();
    const float t = pm.toTimeline(x);
    return (t < 1e-6f && x > 0.5f && pm.rotate01 == 0.f) ? 1.f : t;
}

void LocalEditorBackend::curve(int band, float plotWidthPx, std::vector<EditorPoint>& out) const
{
    const PhaseMap pm = map();
    const Envelope& e = m_.env(band);
    const int N = std::max(2, int(plotWidthPx * 0.5f));
    out.resize(std::size_t(N) + 1);
    for (int i = 0; i <= N; ++i) {
        const float p = float(i) / float(N);
        float q = pm.toNode(p);
        if (i == N && pm.rotate01 == 0.f)
            q = 1.f;
        out[std::size_t(i)] = {p, e.evaluateLeft(q)};
    }
}

EditorPoint LocalEditorBackend::nodePos(int band, int i) const
{
    const EnvNode& n = m_.env(band).node(i);
    return {timelineOf(n.x), n.y};
}

bool LocalEditorBackend::segmentHasHandle(int seg) const
{
    const Envelope& e = m_.env(m_.editedBand());
    return seg >= 0 && seg + 1 < e.size() && e.node(seg + 1).x - e.node(seg).x > 0.01f;
}

EditorPoint LocalEditorBackend::handlePos(int seg) const
{
    const Envelope& e = m_.env(m_.editedBand());
    const float qm = (e.node(seg).x + e.node(seg + 1).x) * 0.5f;
    return {timelineOf(qm), e.evaluateLeft(qm)};
}

void LocalEditorBackend::gridLines(std::vector<std::pair<float, bool>>& out, std::vector<int>& index) const
{
    const PhaseMap pm = map();
    const bool free = m_.ivalue(kParamTimeMode) == kTimeFree;
    const double cell = kGrids[std::clamp(m_.ivalue(kParamGrid), 0, kNumGrids - 1)].beats;
    const int n = int(std::lround(pm.divisions));
    out.clear();
    index.clear();
    for (int k = 0; k < n; ++k) {
        const double beats = double(k) * cell;
        out.emplace_back(pm.toTimeline(float(k) / pm.divisions), !free && std::fabs(beats - std::round(beats)) < 1e-6);
        index.push_back(k);
    }
}

int LocalEditorBackend::quickShiftSpans(float out[4]) const
{
    float t0, t1;
    if (!quickShiftEdges(t0, t1))
        return 0;
    if (t1 >= t0) { out[0] = t0; out[1] = t1; return 1; }
    out[0] = t0; out[1] = 1.f; out[2] = 0.f; out[3] = t1;
    return 2;
}

bool LocalEditorBackend::quickShiftEdges(float& t0, float& t1) const
{
    const Envelope& e = m_.env(m_.editedBand());
    float lo = 2.f, hi = -1.f;
    for (int i = 0; i < e.size(); ++i)
        if (e.node(i).flags & kNodeQuickShift) {
            lo = std::min(lo, e.node(i).x);
            hi = std::max(hi, e.node(i).x);
        }
    if (hi < 0.f)
        return false;
    t0 = timelineOf(lo);
    t1 = timelineOf(hi);
    return true;
}

float LocalEditorBackend::valueAt(float timeline) const
{
    return m_.env(m_.editedBand()).evaluate(map().toNode(timeline));
}

}} // namespace kick::ui
