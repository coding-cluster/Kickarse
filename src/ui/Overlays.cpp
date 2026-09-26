// Kickarse UI — menu and rate-grid overlays (see Overlays.h); ports drawMenu / drawRateMenu.
#include "Overlays.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Format.h"

namespace kick { namespace ui {

using DGL_NAMESPACE::MouseCursor;

// ---------------------------------------------------------------------------------------------
// MenuOverlay

void MenuOverlay::open(float x, float y, std::vector<MenuItem> items, float rightEdge, bool above)
{
    items_ = std::move(items);
    ax_ = x;
    ay_ = y;
    rightEdge_ = rightEdge;
    above_ = above;
    open_ = true;
    hover_ = -1;
    layout(sv.gfx());
}

void MenuOverlay::layout(Gfx& g)
{
    float w = 150.f;
    float h = 8.f;
    rowY_.assign(items_.size(), 0.f);
    for (const MenuItem& it : items_) {
        if (!it.separator)
            w = std::max(w, g.measure(it.label.c_str(), 11.5f) + 44.f);
        h += it.separator ? 9.f : 22.f;
    }
    float mx = rightEdge_ > 0.f ? rightEdge_ - w : ax_;
    float my = above_ ? ay_ - h - 4.f : ay_;
    mx = std::clamp(mx, 4.f, kBaseW - w - 4.f);
    my = std::clamp(my, 4.f, kBaseH - h - 4.f);
    box_ = {mx, my, w, h};
    float yy = my + 4.f;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        rowY_[i] = yy;
        yy += items_[i].separator ? 9.f : 22.f;
    }
}

int MenuOverlay::rowAt(float x, float y) const
{
    if (!open_ || x < box_.x + 4.f || x > box_.right() - 4.f)
        return -1;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const MenuItem& it = items_[i];
        if (it.separator || it.header)
            continue;
        if (y >= rowY_[i] && y < rowY_[i] + 22.f)
            return int(i);
    }
    return -1;
}

MouseCursor MenuOverlay::cursor(const Pointer& p) const
{
    const int i = rowAt(p.x, p.y);
    return i >= 0 && !items_[std::size_t(i)].dim ? DGL_NAMESPACE::kMouseCursorHand : DGL_NAMESPACE::kMouseCursorArrow;
}

void MenuOverlay::down(const Pointer& p)
{
    const int i = rowAt(p.x, p.y);
    if (i >= 0 && items_[std::size_t(i)].dim)
        return;
    open_ = false;
    if (i >= 0 && items_[std::size_t(i)].action) {
        auto act = items_[std::size_t(i)].action;  // copy: the action may reopen a menu
        act();
    }
    sv.repaint();
}

void MenuOverlay::paint(Gfx& g)
{
    if (!open_)
        return;
    layout(g);
    const RectF& b = box_;
    g.dropShadow(b.x, b.y, b.w, b.h, 4.f, 18.f, 0.5f, 6.f);
    g.fillRR(b.x, b.y, b.w, b.h, 4.f, col::ink3);
    g.strokeRR(b.x + 0.5f, b.y + 0.5f, b.w - 1.f, b.h - 1.f, 4.f, col::ink6);
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const MenuItem& it = items_[i];
        const float yy = rowY_[i];
        if (it.separator) {
            g.hline(b.x + 8.f, b.right() - 8.f, yy + 4.f, col::ink5);
            continue;
        }
        const float hv = (int(i) == hover_ && !it.dim && !it.header) ? 1.f : 0.f;
        if (hv > 0.f)
            g.fillRR(b.x + 4.f, yy, b.w - 8.f, 22.f, 3.f, col::warm.withAlpha(0.07f));
        if (it.check) {
            auto& vg = g.vg();
            vg.beginPath();
            vg.moveTo(b.x + 12.f, yy + 11.f);
            vg.lineTo(b.x + 15.f, yy + 14.f);
            vg.lineTo(b.x + 20.f, yy + 7.5f);
            vg.strokeColor(Gfx::c(col::duck));
            vg.strokeWidth(1.5f);
            vg.lineCap(DGL_NAMESPACE::NanoVG::ROUND);
            vg.lineJoin(DGL_NAMESPACE::NanoVG::ROUND);
            vg.stroke();
            vg.lineCap(DGL_NAMESPACE::NanoVG::BUTT);
        }
        const Rgba c = it.header || it.dim ? col::textDim : mix(col::text, col::textHi, hv);
        g.text(it.label.c_str(), b.x + 28.f, yy + 11.5f, {it.header ? 10.5f : 11.5f, Font::Sc, c, Align::Left});
    }
}

// ---------------------------------------------------------------------------------------------
// RateGridOverlay: rows straight / dotted / triplet, columns = note length

namespace {
const char* const kCols[] = {"4/1", "2/1", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64"};
const char* const kRowNames[] = {"Straight", "Dotted", "Triplet"};
const char* const kRowSuffix[] = {"", ".", "T"};
constexpr float kCw = 42.f, kCh = 26.f, kLw = 60.f;

int rateIndex(int row, int col)
{
    char lab[16];
    std::snprintf(lab, sizeof(lab), "%s%s", kCols[col], kRowSuffix[row]);
    for (int i = 0; i < kNumRates; ++i)
        if (std::strcmp(kRateLabels[i], lab) == 0)
            return i;
    return -1;
}
} // namespace

int RateGridOverlay::cellAt(float x, float y) const
{
    const float mx = x_, my = y_;
    for (int rw = 0; rw < 3; ++rw)
        for (int c = 0; c < 9; ++c) {
            const float xx = mx + kLw + float(c) * kCw, yy = my + 26.f + float(rw) * kCh;
            if (x >= xx + 1.f && x < xx + kCw - 1.f && y >= yy + 1.f && y < yy + kCh - 1.f)
                return rateIndex(rw, c);
        }
    return -1;
}

MouseCursor RateGridOverlay::cursor(const Pointer& p) const
{
    return cellAt(p.x, p.y) >= 0 ? DGL_NAMESPACE::kMouseCursorHand : DGL_NAMESPACE::kMouseCursorArrow;
}

std::string RateGridOverlay::hint() const
{
    if (hover_ < 0)
        return "Pick a cycle length";
    const float msv = float(kRates[hover_].beats * 60.0 / double(std::max(1.f, sv.live().bpm)) * 1000.0);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s = %.0f ms at %.0f bpm", kRateLabels[hover_], double(msv), double(sv.live().bpm));
    return buf;
}

void RateGridOverlay::down(const Pointer& p)
{
    const int i = cellAt(p.x, p.y);
    open_ = false;
    if (i >= 0)
        sv.model().setOnce(kParamRate, float(i));
    sv.repaint();
}

void RateGridOverlay::paint(Gfx& g)
{
    if (!open_)
        return;
    const float mw = kLw + 9.f * kCw + 12.f, mh = 3.f * kCh + 34.f, mx = x_, my = y_;
    g.dropShadow(mx, my, mw, mh, 4.f, 18.f, 0.5f, 6.f);
    g.fillRR(mx, my, mw, mh, 4.f, col::ink3);
    g.strokeRR(mx + 0.5f, my + 0.5f, mw - 1.f, mh - 1.f, 4.f, col::ink6);
    g.text("Cycle length", mx + 10.f, my + 14.f, {10.5f, Font::Sc, col::textDim, Align::Left});
    g.text("bars", mx + kLw + 1.5f * kCw, my + 14.f, {10.f, Font::Sc, col::textDim, Align::Center});
    g.text("beats and below", mx + kLw + 6.f * kCw, my + 14.f, {10.f, Font::Sc, col::textDim, Align::Center});
    const int cur = sv.model().ivalue(kParamRate);
    for (int rw = 0; rw < 3; ++rw) {
        const float yy = my + 26.f + float(rw) * kCh;
        g.text(kRowNames[rw], mx + 10.f, yy + kCh * 0.5f, {11.f, Font::Sc, col::textMute, Align::Left});
        for (int c = 0; c < 9; ++c) {
            const float xx = mx + kLw + float(c) * kCw;
            const int idx = rateIndex(rw, c);
            if (idx < 0) {
                g.fillRect(xx + 20.f, yy + kCh * 0.5f, 2.f, 1.f, col::ink6);
                continue;
            }
            const bool sel = idx == cur;
            const float hv = hover_ == idx ? 1.f : 0.f;
            if (sel)
                g.fillRR(xx + 2.f, yy + 2.f, kCw - 4.f, kCh - 4.f, 3.f, col::ink5);
            else if (hv > 0.f)
                g.fillRR(xx + 2.f, yy + 2.f, kCw - 4.f, kCh - 4.f, 3.f, col::warm.withAlpha(0.06f));
            g.text(kRateLabels[idx], xx + kCw * 0.5f, yy + kCh * 0.5f + 0.5f,
                   {11.f, sel ? Font::ScSemi : Font::Sc, sel ? col::duck : mix(col::text, col::textHi, hv), Align::Center});
        }
    }
}

}} // namespace kick::ui
