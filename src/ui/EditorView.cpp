// Kickarse UI — envelope editor view (see EditorView.h). Drawing ported from drawEditor() and
// friends in design/prototype/index.html; geometry comes from the EditorBackend.
#include "EditorView.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Format.h"

namespace kick { namespace ui {

using DGL_NAMESPACE::MouseCursor;
using NVG = DGL_NAMESPACE::NanoVG;

namespace {
constexpr RectF kWell = layout::editorWell;
constexpr RectF kPlot = layout::plot;
constexpr RectF kLab  = layout::labelStrip;
constexpr RectF kLane = layout::qsLane;

float easeOut(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    return 1.f - (1.f - t) * (1.f - t) * (1.f - t);
}
} // namespace

EnvelopeEditor::EnvelopeEditor(Services& s, EditorBackend& backend) : Widget(s), be_(backend), plot_(kPlot)
{
    r = kWell;
}

PhaseMap EnvelopeEditor::phaseMap() const
{
    const Model& m = sv.model();
    const bool free = m.ivalue(kParamTimeMode) == kTimeFree;
    const double cyc = free ? 1.0 : kRates[std::clamp(m.ivalue(kParamRate), 0, kNumRates - 1)].beats;
    const double grid = kGrids[std::clamp(m.ivalue(kParamGrid), 0, kNumGrids - 1)].beats;
    return PhaseMap {m.value(kParamRotate) / 360.f, std::max(1.f, float(cyc / grid)), m.value(kParamSwing) / 100.f};
}

int EnvelopeEditor::band() const { return sv.model().editedBand(); }

Rgba EnvelopeEditor::bandColour() const { return band() == 1 ? col::high : col::duck; }

float EnvelopeEditor::effectiveDepth() const
{
    const Model& m = sv.model();
    float d = m.value(kParamDepth) / 100.f;
    if (m.on(kParamMulti))
        d *= (band() == 1 ? m.value(kParamHiMix) : m.value(kParamLoMix)) / 100.f;
    return d;
}

void EnvelopeEditor::startMorph(const Envelope& from, const Envelope& to)
{
    if (!sv.settings().animate)
        return;
    constexpr int N = 97;
    morphFrom_.resize(N);
    morphTo_.resize(N);
    for (int i = 0; i < N; ++i) {
        const float q = float(i) / float(N - 1);
        morphFrom_[std::size_t(i)] = from.evaluateLeft(q);
        morphTo_[std::size_t(i)] = to.evaluateLeft(q);
    }
    morphT_ = 0.f;
    morphing_ = true;
}

bool EnvelopeEditor::animating() const
{
    return morphing_ || capturedFlash_ > 0.f || sv.live().recState == 1;
}

void EnvelopeEditor::tick(float dt)
{
    if (morphing_) {
        morphT_ += dt / 0.18f;
        if (morphT_ >= 1.f)
            morphing_ = false;
    }
    capturedFlash_ = std::max(0.f, capturedFlash_ - dt * 0.35f);
}

// ---------------------------------------------------------------------------------------------
// drawing helpers

void EnvelopeEditor::samplePolyline(const Envelope& e, const PhaseMap& map, std::vector<EditorPoint>& out) const
{
    const int N = int(plot_.w);
    out.resize(std::size_t(N) + 1);
    for (int i = 0; i <= N; ++i) {
        const float p = float(i) / float(N);
        float q = map.toNode(p);
        if (i == N && map.rotate01 == 0.f)
            q = 1.f;
        out[std::size_t(i)] = {p, e.evaluateLeft(q)};
    }
}

void EnvelopeEditor::drawPolyline(Gfx& g, const std::vector<EditorPoint>& pts, const Rgba& c, float width, bool fill,
                                  float alpha, bool dashed)
{
    if (pts.size() < 2)
        return;
    auto& vg = g.vg();
    if (fill) {
        vg.beginPath();
        vg.moveTo(X(pts[0].t), Y(pts[0].v));
        for (std::size_t i = 1; i < pts.size(); ++i)
            vg.lineTo(X(pts[i].t), Y(pts[i].v));
        vg.lineTo(X(1.f), Y(0.f));
        vg.lineTo(X(0.f), Y(0.f));
        vg.closePath();
        vg.fillPaint(vg.linearGradient(0.f, plot_.y, 0.f, plot_.bottom(), Gfx::c(c.withAlpha(0.085f * alpha)), Gfx::c(c.withAlpha(0.f))));
        vg.fill();
    }
    if (dashed) {
        std::vector<float> xy;
        xy.reserve(pts.size() * 2);
        for (const auto& p : pts) { xy.push_back(X(p.t)); xy.push_back(Y(p.v)); }
        g.dashedPolyline(xy.data(), int(pts.size()), 4.f, 3.f, c.withAlpha(alpha), width);
        return;
    }
    vg.beginPath();
    vg.moveTo(X(pts[0].t), Y(pts[0].v));
    for (std::size_t i = 1; i < pts.size(); ++i)
        vg.lineTo(X(pts[i].t), Y(pts[i].v));
    vg.strokeColor(Gfx::c(c.withAlpha(alpha)));
    vg.strokeWidth(width);
    vg.lineJoin(NVG::ROUND);
    vg.lineCap(NVG::ROUND);
    vg.stroke();
    vg.lineCap(NVG::BUTT);
}

void EnvelopeEditor::drawGrid(Gfx& g, bool labels)
{
    const Model& m = sv.model();
    const float div = be_.divisions();
    be_.gridLines(grid_, gridIndex_);
    const bool free = m.ivalue(kParamTimeMode) == kTimeFree;
    if (!labels) {
        for (float v : {0.25f, 0.5f, 0.75f})
            g.hline(plot_.x, plot_.right(), Y(v), col::warm.withAlpha(0.035f));
        g.hline(plot_.x, plot_.right(), Y(1.f), col::warm.withAlpha(0.07f));
        g.hline(plot_.x, plot_.right(), Y(0.f), col::warm.withAlpha(0.07f));
    }
    const int every = std::max(1, int(std::ceil(div / 16.f)));
    const int gi = std::clamp(m.ivalue(kParamGrid), 0, kNumGrids - 1);
    const double cell = kGrids[gi].beats;
    const double cycBeats = free ? 0.0 : kRates[std::clamp(m.ivalue(kParamRate), 0, kNumRates - 1)].beats;
    const char* gridLab = kGridLabels[gi];
    const int den = std::atoi(std::strchr(gridLab, '/') ? std::strchr(gridLab, '/') + 1 : "16");
    const bool trip = gridLab[std::strlen(gridLab) - 1] == 'T';
    for (std::size_t n = 0; n < grid_.size(); ++n) {
        const int k = gridIndex_[n];
        const float x = std::round(X(grid_[n].first));
        const bool beat = grid_[n].second;
        if (!labels) {
            g.vline(x, plot_.y, plot_.bottom(), col::warm.withAlpha(k == 0 ? 0.10f : beat ? 0.075f : 0.035f));
            continue;
        }
        if (k % every != 0 || k == 0)
            continue;
        char buf[32];
        if (free) {
            std::snprintf(buf, sizeof(buf), "%.0f ms", double(float(k) / div * m.value(kParamLengthMs)));
        } else {
            const double beats = double(k) * cell;
            if (std::fabs(beats - std::round(beats)) < 1e-6 && cycBeats > 1.0) {
                const int b = int(std::lround(beats));
                std::snprintf(buf, sizeof(buf), "%d.%d", b / 4 + 1, b % 4 + 1);
            } else {
                std::snprintf(buf, sizeof(buf), "%d/%d%s", k, den, trip ? "T" : "");
            }
        }
        g.text(buf, x, kLab.y + 7.f, {10.f, Font::Sc, beat ? col::textMute : col::textDim, Align::Center});
    }
}

void EnvelopeEditor::drawWaves(Gfx& g)
{
    // Magnitude view on the gain axis: Out = In x gain, so the output silhouette takes the shape of
    // the curve; the gap between In (outline) and Out (fill) is what the duck removed.
    auto& vg = g.vg();
    const Live& l = sv.live();
    const ViewState& vs = sv.model().view;
    const int n = Live::kBins;
    auto path = [&](const float* a) {
        vg.beginPath();
        vg.moveTo(X(0.f), Y(0.f));
        for (int i = 0; i <= n; ++i)
            vg.lineTo(X(float(i) / float(n)), Y(std::min(1.f, a[std::min(i, n - 1)])));
        vg.lineTo(X(1.f), Y(0.f));
        vg.closePath();
    };
    auto edge = [&](const float* a) {
        vg.beginPath();
        for (int i = 0; i <= n; ++i) {
            const float x = X(float(i) / float(n)), y = Y(std::min(1.f, a[std::min(i, n - 1)]));
            if (i == 0) vg.moveTo(x, y); else vg.lineTo(x, y);
        }
    };
    const bool ring = sv.model().ivalue(kParamMode) == kModeRing;
    const bool delta = sv.model().on(kParamDelta);
    if (vs.showOut) {
        path(l.outWave);
        vg.fillColor(Gfx::c(delta ? col::duck.withAlpha(0.10f) : col::bone.withAlpha(0.075f)));
        vg.fill();
    }
    if (vs.showIn) {
        if (vs.showOut) {
            edge(l.mainWave);
            vg.strokeColor(Gfx::c(col::bone.withAlpha(0.20f)));
            vg.strokeWidth(1.f);
            vg.stroke();
        } else {
            path(l.mainWave);
            vg.fillColor(Gfx::c(col::bone.withAlpha(0.07f)));
            vg.fill();
        }
    }
    if (vs.showSide) {
        const float* a = ring ? l.ringWave : l.extWave;
        path(a);
        vg.fillColor(Gfx::c(col::kick.withAlpha(0.10f)));
        vg.fill();
        edge(a);
        vg.strokeColor(Gfx::c(col::kick.withAlpha(0.55f)));
        vg.strokeWidth(1.f);
        vg.stroke();
    }
}

void EnvelopeEditor::drawNodes(Gfx& g, const Rgba& c)
{
    auto& vg = g.vg();
    const int b = band();
    const int nn = be_.nodeCount(b);
    const int hk = be_.hoverKind(), hi = be_.hoverIndex();
    const int gk = be_.gestureKind(), gi = be_.gestureIndex();
    // tension handles: hovered segment, segments touching a selected node, the bent segment
    for (int s = 0; s + 1 < nn; ++s) {
        if (!be_.segmentHasHandle(s))
            continue;
        const bool bent = gk == 2 && gi == s;
        const bool show = ((hk == 3 || hk == 4) && hi == s) || be_.isSelected(s) || be_.isSelected(s + 1) || bent;
        if (!show)
            continue;
        const EditorPoint hp = be_.handlePos(s);
        const bool hotH = (hk == 4 && hi == s) || bent;
        g.circle(X(hp.t), Y(hp.v), hotH ? 3.5f : 2.5f, hotH ? c : c.withAlpha(0.75f));
        if (hotH) {
            vg.beginPath();
            vg.circle(X(hp.t), Y(hp.v), 3.5f);
            vg.strokeColor(Gfx::c(col::ink1));
            vg.strokeWidth(1.5f);
            vg.stroke();
        }
    }
    const bool qs = sv.model().view.qsOn;
    for (int i = 0; i < nn; ++i) {
        const EditorPoint p = be_.nodePos(b, i);
        const float x = X(p.t), y = Y(p.v);
        const bool sel = be_.isSelected(i);
        const bool hotN = (hk == 2 && hi == i) || (gk == 1 && sel);
        const float rad = hotN ? 5.f : sel ? 4.5f : 4.f;
        if (hotN) {
            vg.beginPath();
            vg.rect(x - 12.f, y - 12.f, 24.f, 24.f);
            vg.fillPaint(vg.radialGradient(x, y, 0.f, 12.f, Gfx::c(c.withAlpha(0.28f)), Gfx::c(c.withAlpha(0.f))));
            vg.fill();
        }
        vg.beginPath();
        if (qs && be_.isQsMember(b, i)) {
            const float d = rad + 0.8f;
            vg.moveTo(x, y - d); vg.lineTo(x + d, y); vg.lineTo(x, y + d); vg.lineTo(x - d, y); vg.closePath();
        } else {
            vg.circle(x, y, rad);
        }
        vg.fillColor(Gfx::c(sel ? col::textHi : hotN ? c : col::ink1));
        vg.fill();
        vg.strokeColor(Gfx::c(c));
        vg.strokeWidth(2.f);
        vg.stroke();
    }
}

void EnvelopeEditor::drawPreviews(Gfx& g, const Rgba& c)
{
    auto& vg = g.vg();
    float t0, v0, t1, v1;
    if (be_.marquee(t0, v0, t1, v1)) {
        const float x = std::min(X(t0), X(t1)), y = std::min(Y(v0), Y(v1));
        const float w = std::fabs(X(t1) - X(t0)), h = std::fabs(Y(v1) - Y(v0));
        g.fillRect(x, y, w, h, col::bone.withAlpha(0.05f));
        g.strokeRR(std::round(x) + 0.5f, std::round(y) + 0.5f, std::round(w), std::round(h), 0.f, col::bone.withAlpha(0.4f));
    }
    if (be_.line(t0, v0, t1, v1)) {
        g.line(X(t0), Y(v0), X(t1), Y(v1), col::textHi, 1.5f);
        g.circle(X(t0), Y(v0), 3.5f, c);
        g.circle(X(t1), Y(v1), 3.5f, c);
    }
    const auto& pen = be_.pencil();
    if (pen.size() > 1) {
        vg.beginPath();
        vg.moveTo(X(pen[0].t), Y(pen[0].v));
        for (std::size_t i = 1; i < pen.size(); ++i)
            vg.lineTo(X(pen[i].t), Y(pen[i].v));
        vg.strokeColor(Gfx::c(col::textHi));
        vg.strokeWidth(1.5f);
        vg.lineJoin(NVG::ROUND);
        vg.stroke();
    }
}

void EnvelopeEditor::drawQuickShift(Gfx& g, const Rgba& c)
{
    if (!sv.model().view.qsOn)
        return;
    g.fillRR(kLane.x, kLane.y, kLane.w, kLane.h, 2.f, col::ink0);
    float spans[4];
    const int pieces = be_.quickShiftSpans(spans);
    float e0 = 0.f, e1 = 0.f;
    if (pieces == 0 || !be_.quickShiftEdges(e0, e1)) {
        g.text("Quick shift: Ctrl-click nodes to group them", kLane.cx(), kLane.cy() + 0.5f, {10.f, Font::Sc, col::textDim, Align::Center});
        return;
    }
    const int hk = be_.hoverKind();
    const bool active = be_.gestureKind() == 5;
    const float barHot = (hk == 6 && hot > 0.f) || active ? 1.f : 0.f;
    for (int p = 0; p < pieces; ++p) {
        const float x0 = X(spans[p * 2]), x1 = X(spans[p * 2 + 1]);
        if (barHot > 0.f) {
            g.fillRect(x0, plot_.y, x1 - x0, plot_.h, c.withAlpha(0.05f));
            g.vline(x0, plot_.y, plot_.bottom(), c.withAlpha(0.3f));
            g.vline(x1 - 1.f, plot_.y, plot_.bottom(), c.withAlpha(0.3f));
        }
        g.fillRR(x0, kLane.y + 2.f, std::max(8.f, x1 - x0), kLane.h - 4.f, 2.f, c.withAlpha(0.2f + 0.12f * barHot));
        g.hline(x0 + 2.f, x1 - 2.f, kLane.y + 2.f, c.withAlpha(0.6f));
    }
    // grip at the centre of the whole range, handles at its edges
    float gx0 = X(e0), gx1 = X(e1);
    if (gx1 < gx0) gx1 += plot_.w;
    float cx = (gx0 + gx1) * 0.5f;
    if (cx > plot_.right()) cx -= plot_.w;
    for (float o : {-3.f, 0.f, 3.f})
        g.vline(cx + o, kLane.y + 5.f, kLane.bottom() - 5.f, col::textHi.withAlpha(0.55f));
    const float hs = hk == 7 ? 1.f : 0.f, he = hk == 8 ? 1.f : 0.f;
    g.fillRR(X(e0) - 1.f, kLane.y + 1.f, 3.f, kLane.h - 2.f, 1.f, c.withAlpha(0.6f + 0.4f * hs));
    g.fillRR(X(e1) - 2.f, kLane.y + 1.f, 3.f, kLane.h - 2.f, 1.f, c.withAlpha(0.6f + 0.4f * he));
}

// ---------------------------------------------------------------------------------------------

void EnvelopeEditor::paint(Gfx& g)
{
    auto& vg = g.vg();
    const Model& m = sv.model();
    const Live& l = sv.live();
    const int b = band();
    const Rgba bc = bandColour();
    const PhaseMap map = phaseMap();
    be_.setGeometry(plot_, kLane, m.view.qsOn);

    g.well(kWell.x, kWell.y, kWell.w, kWell.h, 3.f);
    vg.beginPath();
    vg.rect(kWell.x + 1.f, kWell.y + 6.f, kWell.w - 2.f, kWell.h - 7.f);
    vg.fillPaint(vg.linearGradient(0.f, kWell.y, 0.f, kWell.bottom(), Gfx::c(col::warm.withAlpha(0.012f)), Gfx::c(col::black.withAlpha(0.18f))));
    vg.fill();

    vg.save();
    vg.scissor(plot_.x - 8.f, plot_.y - 8.f, plot_.w + 16.f, plot_.h + 16.f);
    drawGrid(g, false);
    drawWaves(g);
    if (l.recState == 2) {
        const float rx = X(l.recProgress);
        g.fillRect(plot_.x, plot_.y, rx - plot_.x, plot_.h, col::kick.withAlpha(0.07f));
        g.line(rx, plot_.y, rx, plot_.bottom(), col::kick.withAlpha(0.8f), 1.5f);
    }
    if (m.on(kParamMulti) && !m.on(kParamEnvLink)) {
        be_.curve(1 - b, plot_.w, other_);
        drawPolyline(g, other_, b == 1 ? col::duck : col::high, 1.25f, false, 0.42f);
    }
    if (preview_ != nullptr) {
        samplePolyline(*preview_, map, ghost_);
        drawPolyline(g, ghost_, col::textHi, 1.25f, false, 0.55f, true);
    }
    if (morphing_) {
        const float t = easeOut(morphT_);
        const int N = int(plot_.w);
        pts_.resize(std::size_t(N) + 1);
        const int M = int(morphFrom_.size()) - 1;
        for (int i = 0; i <= N; ++i) {
            const float p = float(i) / float(N);
            float q = map.toNode(p);
            if (i == N && map.rotate01 == 0.f) q = 1.f;
            const float fi = q * float(M);
            const int i0 = std::clamp(int(fi), 0, M), i1 = std::min(M, i0 + 1);
            const float u = fi - float(i0);
            const float a = morphFrom_[std::size_t(i0)] + (morphFrom_[std::size_t(i1)] - morphFrom_[std::size_t(i0)]) * u;
            const float z = morphTo_[std::size_t(i0)] + (morphTo_[std::size_t(i1)] - morphTo_[std::size_t(i0)]) * u;
            pts_[std::size_t(i)] = {p, a + (z - a) * t};
        }
    } else {
        be_.curve(b, plot_.w, pts_);
    }
    const bool bypass = m.on(kParamBypass);
    if (bypass)
        vg.globalAlpha(0.5f);
    drawPolyline(g, pts_, bc, 2.f, true, 1.f);
    const float eff = effectiveDepth();
    if (eff < 0.995f && pts_.size() > 1) {
        std::vector<float> xy;
        xy.reserve(pts_.size() * 2);
        for (const auto& p : pts_) { xy.push_back(X(p.t)); xy.push_back(Y(1.f - eff * (1.f - p.v))); }
        g.dashedPolyline(xy.data(), int(pts_.size()), 3.f, 3.f, bc.withAlpha(0.55f), 1.f);
    }
    if (l.trigFlash > 0.02f && m.ivalue(kParamMode) != kModeSync) {
        float t0 = map.toTimeline(0.f);
        g.fillRect(X(t0) - 1.f, plot_.y, 2.f, plot_.h, col::kick.withAlpha(0.5f * l.trigFlash));
    }
    if (l.playing || m.ivalue(kParamMode) != kModeSync) {
        const float x = X(l.phase);
        vg.beginPath();
        vg.rect(x - 18.f, plot_.y, 18.f, plot_.h);
        vg.fillPaint(vg.linearGradient(x - 18.f, 0.f, x, 0.f, Gfx::c(col::bone.withAlpha(0.f)), Gfx::c(col::bone.withAlpha(l.active ? 0.045f : 0.015f))));
        vg.fill();
        g.vline(x, plot_.y, plot_.bottom(), col::bone.withAlpha(l.active ? 0.5f : 0.18f));
        if (!morphing_) {
            const float y = Y(be_.valueAt(l.phase));
            vg.beginPath();
            vg.circle(x, y, 3.5f);
            vg.fillColor(Gfx::c(col::textHi));
            vg.fill();
            vg.strokeColor(Gfx::c(col::ink1));
            vg.strokeWidth(2.f);
            vg.stroke();
        }
    }
    vg.globalAlpha(1.f);
    vg.restore();

    // axis labels + y caption
    vg.save();
    vg.scissor(kLab.x - 20.f, kLab.y, kLab.w + 40.f, kLab.h);
    drawGrid(g, true);
    vg.restore();
    const int mode = m.ivalue(kParamMode);
    const char* cap = mode == kModeSpectral ? (m.ivalue(kParamSpecTarget) == kSpecTargetVolume ? "Gain after spectral" : "Spectral depth")
                    : mode == kModeRing ? "Ring depth" : "Gain";
    g.text(cap, plot_.x, kLab.y + 7.f, {10.f, Font::Sc, col::textDim, Align::Left});

    if (bypass)
        vg.globalAlpha(0.5f);
    if (!morphing_)
        drawNodes(g, bc);
    vg.globalAlpha(1.f);
    drawPreviews(g, bc);

    // corner status
    const bool oneShot = m.ivalue(kParamPlayMode) == kPlayOneShot;
    g.icon(oneShot ? Icon::OneShot : Icon::Loop, plot_.right() - 10.f, plot_.y + 10.f, col::textDim, 0.8f);
    if (!l.active && mode != kModeSync)
        g.text(oneShot ? "Held \xE2\x80\x94 waiting for the next trigger" : "Waiting for a trigger", plot_.right() - 24.f, plot_.y + 10.5f,
               {11.f, Font::Sc, col::textDim, Align::Right});
    if (l.recState == 1) {
        const float a = 0.5f + 0.5f * float(std::sin(sv.now() * 1000.0 / 220.0));
        g.strokeRR(kWell.x + 1.5f, kWell.y + 1.5f, kWell.w - 3.f, kWell.h - 3.f, 3.f, col::kick.withAlpha(0.35f + 0.4f * a), 1.5f);
        g.text(mode == kModeSync ? "Armed \xE2\x80\x94 capture starts on the next cycle" : "Armed \xE2\x80\x94 capture starts on the next trigger",
               plot_.x + 8.f, plot_.y + 10.5f, {11.f, Font::Sc, col::kick, Align::Left});
    } else if (l.recState == 2) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Capturing sidechain\xE2\x80\xA6 %d %%", int(l.recProgress * 100.f));
        g.text(buf, plot_.x + 8.f, plot_.y + 10.5f, {11.f, Font::Sc, col::kick, Align::Left});
    } else if (capturedFlash_ > 0.f) {
        g.text("Captured from the sidechain \xE2\x80\x94 saved to User shapes", plot_.x + 8.f, plot_.y + 10.5f,
               {11.f, Font::Sc, col::textHi.withAlpha(std::min(1.f, capturedFlash_ * 2.f)), Align::Left});
    }

    drawQuickShift(g, bc);

    std::string ro;
    float rx = 0.f, ry = 0.f;
    if (be_.readout(ro, rx, ry) && !ro.empty()) {
        const float w = g.measure(ro.c_str(), 10.5f, Font::ScSemi) + 12.f;
        const float bx = std::clamp(rx + 10.f, plot_.x, plot_.right() - w), by = std::clamp(ry - 26.f, plot_.y, plot_.bottom() - 18.f);
        g.fillRR(bx, by, w, 18.f, 3.f, Rgba{12.f / 255.f, 12.f / 255.f, 11.f / 255.f, 0.92f});
        g.strokeRR(bx + 0.5f, by + 0.5f, w - 1.f, 17.f, 3.f, col::ink6);
        g.text(ro.c_str(), bx + 6.f, by + 9.5f, {10.5f, Font::ScSemi, col::textHi, Align::Left});
    }
}

// ---------------------------------------------------------------------------------------------
// input: forwarded to the backend (EditorController)

void EnvelopeEditor::down(const Pointer& p) { be_.mouseDown(p.x, p.y, p, 1); sv.repaint(); }
void EnvelopeEditor::dbl(const Pointer& p) { be_.mouseDown(p.x, p.y, p, 2); sv.repaint(); }
void EnvelopeEditor::drag(const Pointer& p, float, float) { be_.mouseMove(p.x, p.y, p); sv.repaint(); }
void EnvelopeEditor::up(const Pointer& p) { be_.mouseUp(p.x, p.y, p); sv.repaint(); }
void EnvelopeEditor::move(const Pointer& p) { if (be_.mouseMove(p.x, p.y, p)) sv.repaint(); }
void EnvelopeEditor::leave() { be_.mouseMove(-1000.f, -1000.f, Pointer{}); sv.repaint(); }
void EnvelopeEditor::context(const Pointer& p) { be_.contextMenu(p.x, p.y, p.x, p.y); }
bool EnvelopeEditor::key(unsigned k, const Pointer& p) { const bool used = be_.key(k, p); if (used) sv.repaint(); return used; }
MouseCursor EnvelopeEditor::cursor(const Pointer&) const { return be_.cursor(); }

std::string EnvelopeEditor::hint() const
{
    switch (sv.model().view.tool) {
    case 1: return "Line: drag from one point to another (Ctrl inverts snap)";
    case 2: return "Pencil: draw; the stroke becomes nodes";
    default: break;
    }
    const int hk = be_.hoverKind();
    if (hk >= 5)
        return "Quick shift: drag the bar to move the dip and recovery together (Shift fine) \xC2\xB7 drag an end to change the range";
    return "Drag nodes \xC2\xB7 drag a segment to bend \xC2\xB7 double-click to add/delete \xC2\xB7 drag empty space to select \xC2\xB7 Ctrl-click = quick-shift group";
}

}} // namespace kick::ui
