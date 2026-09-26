// Kickarse UI — bespoke controls and live displays (see Displays.h). Ported from the prototype's
// rateStepper / msSlider / thresholdMeter / filterGraph / crossoverGraph / spectrumDisplay /
// panelSync / panelMidi.
#include "Displays.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Format.h"

namespace kick { namespace ui {

using DGL_NAMESPACE::MouseCursor;

namespace {

inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// cycle length in quarter-note beats (free time converts ms at the host tempo)
float cycleBeatsNow(const Model& m, const Live& l)
{
    if (m.ivalue(kParamTimeMode) == kTimeFree)
        return std::max(1e-3f, m.value(kParamLengthMs) / 1000.f * l.bpm / 60.f);
    return float(kRates[std::clamp(m.ivalue(kParamRate), 0, kNumRates - 1)].beats);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// RateStepper

RateStepper::RateStepper(Services& s, RectF rect) : Widget(s) { r = rect; }

int RateStepper::zoneAt(float x) const
{
    if (x < r.x + 34.f) return 0;
    if (x > r.right() - 34.f) return 2;
    return 1;
}

void RateStepper::step(int d)
{
    Model& m = sv.model();
    m.setOnce(kParamRate, float(std::clamp(m.ivalue(kParamRate) + d, 0, kNumRates - 1)));
}

void RateStepper::down(const Pointer& p)
{
    const int z = zoneAt(p.x);
    if (z == 0) step(-1);
    else if (z == 2) step(1);
    else sv.openRateGrid(r.x, r.bottom() + 4.f);
}

void RateStepper::wheel(const Pointer&, float n)
{
    step(n > 0.f ? -1 : 1);
}

std::string RateStepper::hint() const
{
    if (zone_ == 0) return "Longer cycle";
    if (zone_ == 2) return "Shorter cycle";
    return "Rate: click for the note-value grid \xC2\xB7 wheel to step";
}

void RateStepper::paint(Gfx& g)
{
    g.well(r.x, r.y, r.w, r.h, 3.f);
    const float hp = zone_ == 0 ? hot : 0.f, hn = zone_ == 2 ? hot : 0.f, hvv = zone_ == 1 ? hot : 0.f;
    if (hp > 0.01f) g.fillRR(r.x + 2.f, r.y + 2.f, 32.f, r.h - 4.f, 3.f, col::warm.withAlpha(0.05f * hp));
    if (hn > 0.01f) g.fillRR(r.right() - 34.f, r.y + 2.f, 32.f, r.h - 4.f, 3.f, col::warm.withAlpha(0.05f * hn));
    g.icon(Icon::Prev, r.x + 18.f, r.cy(), mix(col::textMute, col::textHi, hp));
    g.icon(Icon::Next, r.right() - 18.f, r.cy(), mix(col::textMute, col::textHi, hn));
    const char* lab = kRateLabels[std::clamp(sv.model().ivalue(kParamRate), 0, kNumRates - 1)];
    g.text(lab, r.cx(), r.cy() + 1.f, {19.f, Font::Exp, mix(col::text, col::textHi, 0.6f + hvv * 0.4f), Align::Center});
    g.icon(Icon::Caret, r.cx() + g.measure(lab, 19.f, Font::Exp) * 0.5f + 10.f, r.cy() + 1.f,
           hvv > 0.3f ? col::textMute : col::textDim, 0.9f);
}

// ---------------------------------------------------------------------------------------------
// GridDropdown

GridDropdown::GridDropdown(Services& s, RectF rect) : Widget(s) { r = rect; }

void GridDropdown::down(const Pointer&)
{
    std::vector<MenuItem> items;
    Model& m = sv.model();
    for (int i = 0; i < kNumGrids; ++i) {
        MenuItem it;
        it.label  = kGridLabels[i];
        it.check  = m.ivalue(kParamGrid) == i;
        it.action = [this, i] { sv.model().setOnce(kParamGrid, float(i)); };
        items.push_back(std::move(it));
    }
    sv.openMenu(r.x, r.bottom() + 4.f, std::move(items));
}

void GridDropdown::wheel(const Pointer&, float n)
{
    Model& m = sv.model();
    m.setOnce(kParamGrid, float(std::clamp(m.ivalue(kParamGrid) - (n > 0.f ? 1 : -1), 0, kNumGrids - 1)));
}

void GridDropdown::paint(Gfx& g)
{
    g.well(r.x, r.y, r.w, r.h, 3.f);
    g.text("Grid", r.x + 8.f, r.cy() + 0.5f, {11.f, Font::Sc, col::textMute, Align::Left});
    g.text(kGridLabels[std::clamp(sv.model().ivalue(kParamGrid), 0, kNumGrids - 1)], r.x + 70.f, r.cy() + 0.5f,
           {11.f, Font::ScSemi, mix(col::text, col::textHi, hot), Align::Right});
    g.icon(Icon::Caret, r.x + 79.f, r.cy() + 0.5f, col::textMute, 0.85f);
}

// ---------------------------------------------------------------------------------------------
// MsSlider

MsSlider::MsSlider(Services& s, RectF rect) : Widget(s), g_(s, kParamMidSide, rect.w - 16.f, true) { r = rect; }

void MsSlider::down(const Pointer& p)
{
    g_.down(p);
    active_ = g_.dragging();
}

void MsSlider::drag(const Pointer& p, float dx, float dy) { g_.drag(p, dx, dy); }

void MsSlider::up(const Pointer&)
{
    g_.up();
    active_ = false;
}

void MsSlider::dbl(const Pointer&) { g_.reset(); }

void MsSlider::paint(Gfx& g)
{
    const float v = sv.model().value(kParamMidSide);
    const float tx = r.x + 8.f, tw = r.w - 16.f, ty = r.y + 12.f, n = (v + 100.f) / 200.f;
    g.fillRR(tx, ty - 1.5f, tw, 3.f, 1.5f, col::ink0);
    g.line(tx + tw * 0.5f + 0.5f, ty - 6.f, tx + tw * 0.5f + 0.5f, ty + 6.f, col::ink7, 1.f);
    const float kx = tx + n * tw;
    if (std::fabs(v) > 0.5f)
        g.fillRect(std::min(kx, tx + tw * 0.5f), ty - 1.f, std::fabs(kx - tx - tw * 0.5f), 2.f, col::textMute);
    g.dropShadow(kx - 5.f, ty - 8.f, 10.f, 16.f, 2.f, 4.f, 0.5f, 1.5f);
    g.raised(kx - 5.f, ty - 8.f, 10.f, 16.f, 2.f, hot > 0.3f || active_ ? col::ink7 : col::ink6);
    g.line(kx + 0.5f, ty - 4.f, kx + 0.5f, ty + 4.f, col::textHi, 1.f);
    g.text("Mid", tx - 2.f, r.y + 30.f, {11.f, Font::Sc, v < -0.5f ? col::text : col::textDim, Align::Left});
    g.text("Side", tx + tw + 2.f, r.y + 30.f, {11.f, Font::Sc, v > 0.5f ? col::text : col::textDim, Align::Right});
}

// ---------------------------------------------------------------------------------------------
// ThresholdMeter (sidechain level with a draggable threshold line, -60…0 dB)

ThresholdMeter::ThresholdMeter(Services& s, RectF rect, bool compact) : Widget(s), compact_(compact) { r = rect; }

float ThresholdMeter::dbToX(float db) const
{
    const float ix = r.x + 6.f, iw = r.w - 12.f;
    return ix + iw * (db + 60.f) / 60.f;
}

void ThresholdMeter::down(const Pointer& p)
{
    Model& m = sv.model();
    const float ix = r.x + 6.f, iw = r.w - 12.f;
    m.beginGesture(kParamThreshold);
    active_ = true;
    m.gestureSet(kParamThreshold, (std::clamp(p.x, ix, ix + iw) - ix) / iw * 60.f - 60.f);
}

void ThresholdMeter::drag(const Pointer& p, float dx, float)
{
    if (!active_)
        return;
    Model& m = sv.model();
    m.gestureSet(kParamThreshold, m.value(kParamThreshold) + dx * (p.shift ? 0.1f : 1.f) * 60.f / (r.w - 12.f));
}

void ThresholdMeter::up(const Pointer&)
{
    if (active_)
        sv.model().endGesture(kParamThreshold);
    active_ = false;
}

void ThresholdMeter::dbl(const Pointer&)
{
    active_ = false;
    sv.model().setOnce(kParamThreshold, kParams[kParamThreshold].def);
}

void ThresholdMeter::wheel(const Pointer& p, float n)
{
    Model& m = sv.model();
    m.setOnce(kParamThreshold, m.value(kParamThreshold) + (n > 0.f ? 1.f : -1.f) * (p.shift ? 0.1f : 1.f));
}

std::string ThresholdMeter::hint() const
{
    return "Threshold " + formatParam(kParamThreshold, sv.model().value(kParamThreshold))
        + " \xE2\x80\x94 the kick must cross this line to trigger \xC2\xB7 click or drag \xC2\xB7 double-click reset";
}

void ThresholdMeter::paint(Gfx& g)
{
    const Live& l = sv.live();
    auto& vg = g.vg();
    g.well(r.x, r.y, r.w, r.h, 3.f);
    const float ix = r.x + 6.f, iw = r.w - 12.f;
    const float my = r.y + (compact_ ? r.h * 0.5f - 3.f : r.h - 16.f), mh = 6.f;
    g.fillRR(ix, my, iw, mh, 1.f, col::ink0);
    const float pk = std::clamp(l.scPeakHoldDb, -60.f, 0.f);
    if (pk > -60.f) {
        vg.beginPath();
        vg.rect(ix, my, dbToX(pk) - ix, mh);
        vg.fillPaint(vg.linearGradient(ix, 0.f, ix + iw, 0.f, Gfx::c(col::kick.withAlpha(0.35f)), Gfx::c(col::kick)));
        vg.fill();
    }
    if (!compact_) {
        for (int db = -48; db <= 0; db += 12) {
            const float x = dbToX(float(db));
            g.line(x, my + mh + 2.f, x, my + mh + 5.f, col::ink7, 1.f);
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", -db);
            const std::string s = db == 0 ? std::string("0") : std::string("\xE2\x88\x92") + buf;
            g.text(s.c_str(), x, my + mh + 11.f, {9.5f, Font::Sc, col::textDim, db == 0 ? Align::Right : Align::Center});
        }
    }
    const float tx = dbToX(sv.model().value(kParamThreshold));
    g.fillRect(tx, my, ix + iw - tx, mh, col::black.withAlpha(0.45f));
    const Rgba lc = hot > 0.3f || active_ ? col::textHi : col::text;
    g.line(tx, r.y + 4.f, tx, r.y + r.h - (compact_ ? 4.f : 10.f), lc, 1.5f);
    vg.beginPath();
    vg.moveTo(tx - 4.f, r.y + 3.f);
    vg.lineTo(tx + 4.f, r.y + 3.f);
    vg.lineTo(tx, r.y + 8.f);
    vg.closePath();
    vg.fillColor(Gfx::c(lc));
    vg.fill();
    if (l.trigFlash > 0.05f)
        g.fillRR(ix, my, iw, mh, 1.f, col::kick.withAlpha(0.25f * l.trigFlash));
}

// ---------------------------------------------------------------------------------------------
// FilterGraph (detection filter: low cut / high cut on the sidechain path)

FilterGraph::FilterGraph(Services& s, RectF rect) : Widget(s) { r = rect; }

float FilterGraph::f2x(float f) const { return r.x + 6.f + (r.w - 12.f) * std::log(f / 20.f) / std::log(1000.f); }

float FilterGraph::x2f(float x) const
{
    return 20.f * std::pow(1000.f, std::clamp((x - r.x - 6.f) / (r.w - 12.f), 0.f, 1.f));
}

int FilterGraph::handleAt(float x) const
{
    const Model& m = sv.model();
    const float lo = f2x(m.value(kParamTrigLowCut)), hi = f2x(m.value(kParamTrigHighCut));
    const float dl = std::fabs(x - lo), dh = std::fabs(x - hi);
    if (std::min(dl, dh) > 7.f)
        return -1;
    return dl <= dh ? 0 : 1;
}

MouseCursor FilterGraph::cursor(const Pointer& p) const
{
    return handleAt(p.x) >= 0 ? DGL_NAMESPACE::kMouseCursorLeftRight : DGL_NAMESPACE::kMouseCursorArrow;
}

std::string FilterGraph::hint() const
{
    const Model& m = sv.model();
    const int h = dragHandle_ >= 0 ? dragHandle_ : hoverHandle_;
    if (h < 0)
        return "Detection filter: shapes only what the trigger listens to, never the audio you hear";
    const int id = h == 0 ? kParamTrigLowCut : kParamTrigHighCut;
    return std::string(paramLabel(id)) + " " + formatParam(id, m.value(id)) + " \xE2\x80\x94 drag \xC2\xB7 double-click reset";
}

void FilterGraph::down(const Pointer& p)
{
    dragHandle_ = handleAt(p.x);
    if (dragHandle_ < 0)
        return;
    Model& m = sv.model();
    if (!m.on(kParamTrigFilter))
        m.setOnce(kParamTrigFilter, 1.f);
    m.beginGesture(dragHandle_ == 0 ? kParamTrigLowCut : kParamTrigHighCut);
}

void FilterGraph::drag(const Pointer& p, float, float)
{
    if (dragHandle_ < 0)
        return;
    sv.model().gestureSet(dragHandle_ == 0 ? kParamTrigLowCut : kParamTrigHighCut, x2f(p.x));
}

void FilterGraph::up(const Pointer&)
{
    if (dragHandle_ >= 0)
        sv.model().endGesture(dragHandle_ == 0 ? kParamTrigLowCut : kParamTrigHighCut);
    dragHandle_ = -1;
}

void FilterGraph::dbl(const Pointer& p)
{
    const int h = handleAt(p.x);
    if (h < 0)
        return;
    const int id = h == 0 ? kParamTrigLowCut : kParamTrigHighCut;
    sv.model().setOnce(id, kParams[id].def);
}

void FilterGraph::paint(Gfx& g)
{
    auto& vg = g.vg();
    const Model& m = sv.model();
    const bool on = m.on(kParamTrigFilter);
    g.well(r.x, r.y, r.w, r.h, 3.f);
    vg.save();
    vg.scissor(r.x, r.y, r.w, r.h);
    for (float f : {100.f, 1000.f, 10000.f}) {
        g.line(f2x(f), r.y + 4.f, f2x(f), r.bottom() - 4.f, col::warm.withAlpha(0.05f), 1.f);
        g.text(f >= 10000.f ? "10k" : f >= 1000.f ? "1k" : "100", f2x(f) + 3.f, r.bottom() - 8.f, {9.5f, Font::Sc, col::textDim, Align::Left});
    }
    const float top = r.y + 10.f, bot = r.bottom() - 4.f;
    const float lc = m.value(kParamTrigLowCut), hc = m.value(kParamTrigHighCut);
    auto response = [&] {
        vg.beginPath();
        for (float px = r.x; px <= r.right() + 0.01f; px += 2.f) {
            const float f = x2f(px);
            const float hp = 1.f / std::sqrt(1.f + std::pow(lc / f, 4.f)), lp = 1.f / std::sqrt(1.f + std::pow(f / hc, 4.f));
            const float mm = on ? hp * lp : 1.f;
            const float yy = lerpf(bot, top, std::sqrt(mm));
            if (px == r.x) vg.moveTo(px, yy); else vg.lineTo(px, yy);
        }
    };
    response();
    vg.lineTo(r.right(), bot);
    vg.lineTo(r.x, bot);
    vg.closePath();
    vg.fillColor(Gfx::c(col::kick.withAlpha(on ? 0.13f : 0.04f)));
    vg.fill();
    response();
    vg.strokeColor(Gfx::c(on ? col::kick : col::ink7));
    vg.strokeWidth(1.5f);
    vg.stroke();
    vg.restore();
    for (int h = 0; h < 2; ++h) {
        const int id = h == 0 ? kParamTrigLowCut : kParamTrigHighCut;
        const float hx = f2x(m.value(id));
        const bool hl = (hoverHandle_ == h && hot > 0.3f) || dragHandle_ == h;
        g.fillRR(hx - 3.f, r.y + 5.f, 6.f, 10.f, 1.5f, on ? (hl ? col::textHi : col::text) : col::ink7);
        if (hl) {
            const std::string s = formatParam(id, m.value(id));
            g.text(s.c_str(), hx + (h == 0 ? 6.f : -6.f), r.y + 10.5f, {10.f, Font::Sc, col::textHi, h == 0 ? Align::Left : Align::Right});
        }
    }
}

// ---------------------------------------------------------------------------------------------
// CrossoverGraph

CrossoverGraph::CrossoverGraph(Services& s, RectF rect) : Widget(s) { r = rect; }

float CrossoverGraph::f2x(float f) const { return r.x + 4.f + (r.w - 8.f) * std::log(f / 20.f) / std::log(1000.f); }

void CrossoverGraph::down(const Pointer&)
{
    sv.model().beginGesture(kParamCrossover);
    active_ = true;
}

void CrossoverGraph::drag(const Pointer& p, float dx, float)
{
    if (!active_)
        return;
    Model& m = sv.model();  // relative: grabbing never jumps
    m.gestureSet(kParamCrossover, m.value(kParamCrossover) * std::pow(1000.f, dx * (p.shift ? 0.1f : 1.f) / (r.w - 8.f)));
}

void CrossoverGraph::up(const Pointer&)
{
    if (active_)
        sv.model().endGesture(kParamCrossover);
    active_ = false;
}

void CrossoverGraph::dbl(const Pointer&)
{
    active_ = false;
    sv.model().setOnce(kParamCrossover, kParams[kParamCrossover].def);
}

void CrossoverGraph::wheel(const Pointer& p, float n)
{
    Model& m = sv.model();
    m.setOnce(kParamCrossover, m.value(kParamCrossover) * std::pow(1.03f, (n > 0.f ? 1.f : -1.f) * (p.shift ? 0.2f : 1.f)));
}

std::string CrossoverGraph::hint() const
{
    return "Crossover " + formatParam(kParamCrossover, sv.model().value(kParamCrossover)) + " \xE2\x80\x94 drag \xC2\xB7 wheel \xC2\xB7 double-click reset";
}

void CrossoverGraph::paint(Gfx& g)
{
    auto& vg = g.vg();
    const Model& m = sv.model();
    const bool on = m.on(kParamMulti);
    g.well(r.x, r.y, r.w, r.h, 3.f);
    vg.save();
    vg.scissor(r.x, r.y, r.w, r.h);
    if (!on)
        vg.globalAlpha(0.4f);
    for (float f : {100.f, 1000.f, 10000.f})
        g.line(f2x(f) + 0.5f, r.y + 4.f, f2x(f) + 0.5f, r.bottom() - 4.f, col::warm.withAlpha(0.045f), 1.f);
    const float top = r.y + 12.f, bot = r.bottom() - 4.f, fc = m.value(kParamCrossover);
    const float ord = m.ivalue(kParamSlope) == kSlope24 ? 4.f : 2.f;
    const int solo = m.ivalue(kParamBandSolo);
    auto curve = [&](int band) {
        vg.beginPath();
        for (float px = r.x; px <= r.right() + 0.01f; px += 1.5f) {
            const float f = 20.f * std::pow(1000.f, std::clamp((px - r.x - 4.f) / (r.w - 8.f), 0.f, 1.f));
            const float rr = std::pow(f / fc, ord);
            const float mm = band ? rr / (1.f + rr) : 1.f / (1.f + rr);
            const float yy = lerpf(bot, top, std::pow(mm, 0.6f));
            if (px == r.x) vg.moveTo(px, yy); else vg.lineTo(px, yy);
        }
    };
    for (int band = 0; band < 2; ++band) {
        const Rgba c = band ? col::high : col::duck;
        const bool away = solo != kSoloOff && solo != band + 1;
        curve(band);
        vg.lineTo(r.right(), bot);
        vg.lineTo(r.x, bot);
        vg.closePath();
        vg.fillColor(Gfx::c(c.withAlpha(away ? 0.03f : 0.12f)));
        vg.fill();
        curve(band);
        vg.strokeColor(Gfx::c(c.withAlpha(away ? 0.35f : 1.f)));
        vg.strokeWidth(1.5f);
        vg.stroke();
    }
    const float cx = f2x(fc);
    const bool hl = hot > 0.3f || active_;
    g.line(std::round(cx) + 0.5f, r.y + 3.f, std::round(cx) + 0.5f, r.bottom() - 3.f, hl ? col::textHi : col::bone.withAlpha(0.55f), 1.f);
    g.fillRR(cx - 3.f, r.y + 3.f, 6.f, 9.f, 1.5f, hl ? col::textHi : col::text);
    const std::string lab = formatParam(kParamCrossover, fc);
    const float lw = g.measure(lab.c_str(), 10.5f, Font::ScSemi);
    const float lx = cx + 7.f + lw > r.right() - 4.f ? cx - 7.f - lw : cx + 7.f;
    g.text(lab.c_str(), lx, r.y + 8.5f, {10.5f, Font::ScSemi, col::textHi, Align::Left});
    vg.restore();
}

// ---------------------------------------------------------------------------------------------
// SpectrumView

SpectrumView::SpectrumView(Services& s, RectF rect) : Widget(s) { r = rect; }

void SpectrumView::paint(Gfx& g)
{
    auto& vg = g.vg();
    const Live& l = sv.live();
    const Model& m = sv.model();
    g.well(r.x, r.y, r.w, r.h, 3.f);
    constexpr int n = kSpecBands;
    const float gap = 2.f, bw = (r.w - 12.f - gap * float(n - 1)) / float(n), top = r.y + 6.f, bot = r.bottom() - 16.f;
    const float range = std::max(3.f, m.value(kParamSpecRange));
    auto cxOf = [&](int i) { return r.x + 6.f + float(i) * (bw + gap) + bw * 0.5f; };
    auto scY = [&](int i) { return bot - std::clamp((l.specScDb[i] + 60.f) / 60.f, 0.f, 1.f) * (bot - top); };
    vg.save();
    vg.scissor(r.x, r.y, r.w, r.h);
    vg.beginPath();
    vg.moveTo(r.x + 6.f, bot);
    for (int i = 0; i < n; ++i) vg.lineTo(cxOf(i), scY(i));
    vg.lineTo(r.right() - 6.f, bot);
    vg.closePath();
    vg.fillColor(Gfx::c(col::kick.withAlpha(0.10f)));
    vg.fill();
    vg.beginPath();
    for (int i = 0; i < n; ++i) {
        if (i == 0) vg.moveTo(cxOf(i), scY(i)); else vg.lineTo(cxOf(i), scY(i));
    }
    vg.strokeColor(Gfx::c(col::kick.withAlpha(0.7f)));
    vg.strokeWidth(1.25f);
    vg.lineJoin(DGL_NAMESPACE::NanoVG::ROUND);
    vg.stroke();
    for (int i = 0; i < n; ++i) {
        const float bx = r.x + 6.f + float(i) * (bw + gap);
        const float cut = std::clamp(-l.specCutDb[i] / range, 0.f, 1.f), ch = cut * (bot - top);
        g.fillRect(bx, top, bw, 2.f, col::warm.withAlpha(0.08f));
        if (ch > 0.5f)
            g.fillRect(bx, top, bw, ch, col::duck);
    }
    vg.restore();
    g.hline(r.x + 6.f, r.right() - 6.f, bot, col::warm.withAlpha(0.06f));
    const float span = std::log(16000.f / 30.f);
    const struct { float f; const char* s; } labs[] = {{100.f, "100"}, {1000.f, "1k"}, {10000.f, "10k"}};
    for (const auto& lb : labs)
        g.text(lb.s, r.x + 6.f + (r.w - 12.f) * std::log(lb.f / 30.f) / span, r.bottom() - 7.f, {9.5f, Font::Sc, col::textDim, Align::Center});
}

// ---------------------------------------------------------------------------------------------
// BarView: one host bar with the envelope tiled at the current rate (Sync mode)

BarView::BarView(Services& s, RectF rect) : Widget(s) { r = rect; }

void BarView::paint(Gfx& g)
{
    auto& vg = g.vg();
    const Model& m = sv.model();
    const Live& l = sv.live();
    g.well(r.x, r.y, r.w, r.h, 3.f);
    const float ix = r.x + 6.f, iw = r.w - 12.f, iy = r.y + 8.f, ih = r.h - 24.f;
    const int beats = std::clamp(l.timeSigNum, 1, 16);
    for (int b = 0; b <= beats; ++b) {
        const float lx = std::round(ix + iw * float(b) / float(beats));
        g.vline(lx, iy, iy + ih, col::warm.withAlpha(b % beats == 0 ? 0.10f : 0.05f));
        if (b < beats) {
            const std::string s = std::to_string(b + 1);
            g.text(s.c_str(), lx + 3.f, r.bottom() - 8.f, {9.5f, Font::Sc, col::textDim, Align::Left});
        }
    }
    const float barBeats = float(beats) * 4.f / float(std::max(1, l.timeSigDen));
    const float cyc = cycleBeatsNow(m, l);
    const float reps = barBeats / cyc;
    const PhaseMap map {m.value(kParamRotate) / 360.f,
                        std::max(1.f, float((m.ivalue(kParamTimeMode) == kTimeFree ? 1.0 : double(cyc)) / kGrids[std::clamp(m.ivalue(kParamGrid), 0, kNumGrids - 1)].beats)),
                        m.value(kParamSwing) / 100.f};
    const Envelope& env = m.env(0);
    constexpr int N = 180;
    auto yAt = [&](int i) {
        const float barPos = float(i) / float(N) * reps;
        float p = barPos - std::floor(barPos);
        if (i == N && std::fabs(barPos - std::round(barPos)) < 1e-4f)
            p = 1.f;
        return iy + (1.f - env.evaluateLeft(map.toNode(p))) * ih;
    };
    vg.save();
    vg.scissor(ix, iy - 2.f, iw, ih + 4.f);
    vg.beginPath();
    for (int i = 0; i <= N; ++i) {
        const float px = ix + iw * float(i) / float(N);
        if (i == 0) vg.moveTo(px, yAt(i)); else vg.lineTo(px, yAt(i));
    }
    vg.lineTo(ix + iw, iy + ih);
    vg.lineTo(ix, iy + ih);
    vg.closePath();
    vg.fillColor(Gfx::c(col::duck.withAlpha(0.08f)));
    vg.fill();
    vg.beginPath();
    for (int i = 0; i <= N; ++i) {
        const float px = ix + iw * float(i) / float(N);
        if (i == 0) vg.moveTo(px, yAt(i)); else vg.lineTo(px, yAt(i));
    }
    vg.strokeColor(Gfx::c(col::duck));
    vg.strokeWidth(1.25f);
    vg.lineJoin(DGL_NAMESPACE::NanoVG::ROUND);
    vg.stroke();
    if (l.playing && reps >= 1.f) {
        // the Bridge has no bar position yet: place the cycle phase in the first tile
        const float bx = ix + iw * (l.phase / reps);
        g.line(std::round(bx) + 0.5f, iy - 2.f, std::round(bx) + 0.5f, iy + ih + 2.f, col::bone.withAlpha(0.55f), 1.f);
    }
    vg.restore();
}

// ---------------------------------------------------------------------------------------------
// NoteStepper

NoteStepper::NoteStepper(Services& s, RectF rect) : Widget(s) { r = rect; }

void NoteStepper::down(const Pointer& p)
{
    Model& m = sv.model();
    const int nv = m.ivalue(kParamMidiNote);
    if (p.x < r.x + 26.f)
        m.setOnce(kParamMidiNote, float(std::clamp(nv - 1, -1, 127)));
    else if (p.x > r.right() - 26.f)
        m.setOnce(kParamMidiNote, float(std::clamp(nv + 1, -1, 127)));
    else
        sv.openEntry(kParamMidiNote);
}

void NoteStepper::dbl(const Pointer& p)
{
    if (p.x >= r.x + 26.f && p.x <= r.right() - 26.f)
        sv.model().setOnce(kParamMidiNote, -1.f);
    else
        down(p);
}

void NoteStepper::wheel(const Pointer&, float n)
{
    Model& m = sv.model();
    m.setOnce(kParamMidiNote, float(std::clamp(m.ivalue(kParamMidiNote) + (n > 0.f ? 1 : -1), -1, 127)));
}

void NoteStepper::paint(Gfx& g)
{
    g.well(r.x, r.y, r.w, r.h, 3.f);
    const float hp = zone_ == 0 ? hot : 0.f, hn = zone_ == 2 ? hot : 0.f;
    if (hp > 0.01f) g.fillRR(r.x + 2.f, r.y + 2.f, 24.f, r.h - 4.f, 3.f, col::warm.withAlpha(0.05f * hp));
    if (hn > 0.01f) g.fillRR(r.right() - 26.f, r.y + 2.f, 24.f, r.h - 4.f, 3.f, col::warm.withAlpha(0.05f * hn));
    g.icon(Icon::Prev, r.x + 14.f, r.cy(), mix(col::textMute, col::textHi, hp));
    g.icon(Icon::Next, r.right() - 14.f, r.cy(), mix(col::textMute, col::textHi, hn));
    if (sv.entryOpenFor(kParamMidiNote)) {
        sv.drawEntry(g, r.cx(), r.cy(), 12.5f);
        return;
    }
    const std::string s = formatParam(kParamMidiNote, sv.model().value(kParamMidiNote), sv.settings().noteC3);
    g.text(s.c_str(), r.cx(), r.cy() + 0.5f, {12.5f, Font::ScSemi, col::textHi, Align::Center});
}

// ---------------------------------------------------------------------------------------------
// Keyboard

Keyboard::Keyboard(Services& s, RectF rect) : Widget(s) { r = rect; }

int Keyboard::baseNote() const
{
    const int nv = sv.model().ivalue(kParamMidiNote);
    return nv < 0 ? 36 : (nv / 12) * 12;
}

int Keyboard::noteAt(float x, float y) const
{
    const float ww = r.w / 14.f;
    const int base = baseNote();
    if (y < r.y + 2.f + r.h * 0.58f) {
        static const int blackSt[] = {1, 3, 6, 8, 10}, blackPos[] = {1, 2, 4, 5, 6};
        for (int o = 0; o < 2; ++o)
            for (int k = 0; k < 5; ++k) {
                const float bx = r.x + float(o * 7 + blackPos[k]) * ww - ww * 0.32f;
                if (x >= bx && x < bx + ww * 0.64f)
                    return base + o * 12 + blackSt[k];
            }
    }
    static const int whiteSt[] = {0, 2, 4, 5, 7, 9, 11};
    const int j = std::clamp(int((x - r.x) / ww), 0, 13);
    return base + (j / 7) * 12 + whiteSt[j % 7];
}

void Keyboard::down(const Pointer& p)
{
    const int note = noteAt(p.x, p.y);
    if (note >= 0 && note <= 127)
        sv.model().setOnce(kParamMidiNote, float(note));
}

std::string Keyboard::hint() const
{
    if (hoverNote_ < 0)
        return {};
    return noteName(hoverNote_, sv.settings().noteC3) + " (" + std::to_string(hoverNote_) + ") \xE2\x80\x94 click to trigger on this note only";
}

void Keyboard::paint(Gfx& g)
{
    const Model& m = sv.model();
    const Live& l = sv.live();
    const int nv = m.ivalue(kParamMidiNote), base = baseNote();
    const float ww = r.w / 14.f, kh = r.h;
    g.well(r.x, r.y, r.w, r.h, 3.f);
    static const int whiteSt[] = {0, 2, 4, 5, 7, 9, 11};
    const Rgba anyWhite = hex(0x7C7870), offWhite = hex(0x4E4B46), offWhiteHv = hex(0x6A665F), lastBase = hex(0x57544E);
    for (int o = 0; o < 2; ++o)
        for (int j = 0; j < 7; ++j) {
            const int note = base + o * 12 + whiteSt[j];
            const float wx = r.x + float(o * 7 + j) * ww;
            const bool on = nv < 0 || nv == note, last = l.lastNote == note && l.noteFlash > 0.02f;
            const float hv = hoverNote_ == note ? hot : 0.f;
            const Rgba c = last ? mix(nv < 0 ? anyWhite : lastBase, col::kick, l.noteFlash)
                         : on ? (nv < 0 ? anyWhite : col::duck) : mix(offWhite, offWhiteHv, hv);
            g.fillRR(wx + 1.f, r.y + 2.f, ww - 2.f, kh - 4.f, 2.f, c);
            if (whiteSt[j] == 0) {
                const std::string s = noteName(note, sv.settings().noteC3);
                g.text(s.c_str(), wx + ww * 0.5f, r.y + kh - 9.f, {9.f, Font::ScSemi, on && nv >= 0 ? col::ink1 : col::ink3, Align::Center});
            }
        }
    static const int blackSt[] = {1, 3, 6, 8, 10}, blackPos[] = {1, 2, 4, 5, 6};
    for (int o = 0; o < 2; ++o)
        for (int k = 0; k < 5; ++k) {
            const int note = base + o * 12 + blackSt[k];
            const float bx = r.x + float(o * 7 + blackPos[k]) * ww - ww * 0.32f;
            const bool on = nv == note, last = l.lastNote == note && l.noteFlash > 0.02f;
            const float hv = hoverNote_ == note ? hot : 0.f;
            g.fillRR(bx, r.y + 2.f, ww * 0.64f, kh * 0.58f, 1.5f, last ? col::kick : on ? col::duck : mix(col::ink1, col::ink4, hv));
        }
}

// ---------------------------------------------------------------------------------------------
// GrMeter

GrMeter::GrMeter(Services& s, RectF rect) : Widget(s) { r = rect; }

void GrMeter::paint(Gfx& g)
{
    const float gr = std::clamp(-sv.live().grDb, 0.f, 36.f);
    g.fillRR(r.x, r.y, r.w, 5.f, 1.f, col::ink0);
    if (gr > 0.01f)
        g.fillRR(r.x + r.w - r.w * gr / 36.f, r.y, r.w * gr / 36.f, 5.f, 1.f, col::duck);
    char buf[32];
    if (gr < 0.05f)
        std::snprintf(buf, sizeof(buf), "GR 0.0");
    else
        std::snprintf(buf, sizeof(buf), "GR \xE2\x88\x92%.1f", double(gr));
    g.text(buf, r.right(), r.y + 15.f, {10.f, Font::Sc, col::textDim, Align::Right});
}

}} // namespace kick::ui
