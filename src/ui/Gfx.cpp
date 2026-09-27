// Kickarse UI — drawing helpers (see Gfx.h). Ported 1:1 from design/prototype/index.html §6–§8.
#include "Gfx.h"

#include <algorithm>
#include <cmath>

namespace kick { namespace ui {

using NanoVG = DGL_NAMESPACE::NanoVG;

// ---------------------------------------------------------------------------------------------
// primitives

void Gfx::fillRect(float x, float y, float w, float h, const Rgba& col)
{
    vg_.beginPath();
    vg_.rect(x, y, w, h);
    vg_.fillColor(c(col));
    vg_.fill();
}

void Gfx::fillRR(float x, float y, float w, float h, float r, const Rgba& col)
{
    vg_.beginPath();
    if (r > 0.f)
        vg_.roundedRect(x, y, w, h, r);
    else
        vg_.rect(x, y, w, h);
    vg_.fillColor(c(col));
    vg_.fill();
}

void Gfx::strokeRR(float x, float y, float w, float h, float r, const Rgba& col, float lw)
{
    vg_.beginPath();
    if (r > 0.f)
        vg_.roundedRect(x, y, w, h, r);
    else
        vg_.rect(x, y, w, h);
    vg_.strokeColor(c(col));
    vg_.strokeWidth(lw);
    vg_.stroke();
}

void Gfx::line(float x0, float y0, float x1, float y1, const Rgba& col, float lw, bool round)
{
    vg_.beginPath();
    vg_.moveTo(x0, y0);
    vg_.lineTo(x1, y1);
    vg_.lineCap(round ? NanoVG::ROUND : NanoVG::BUTT);
    vg_.strokeColor(c(col));
    vg_.strokeWidth(lw);
    vg_.stroke();
    vg_.lineCap(NanoVG::BUTT);
}

float Gfx::snap(float v) const noexcept
{
    const float dev = std::floor(v * k_ + 0.001f);
    const float lw  = std::max(1.f, std::round(k_));
    return (dev + lw * 0.5f) / k_;
}

void Gfx::hline(float x0, float x1, float y, const Rgba& col)
{
    const float lw = std::max(1.f, std::round(k_)) / k_;
    const float yy = snap(y);
    vg_.beginPath();
    vg_.moveTo(x0, yy);
    vg_.lineTo(x1, yy);
    vg_.strokeColor(c(col));
    vg_.strokeWidth(lw);
    vg_.stroke();
}

void Gfx::vline(float x, float y0, float y1, const Rgba& col)
{
    const float lw = std::max(1.f, std::round(k_)) / k_;
    const float xx = snap(x);
    vg_.beginPath();
    vg_.moveTo(xx, y0);
    vg_.lineTo(xx, y1);
    vg_.strokeColor(c(col));
    vg_.strokeWidth(lw);
    vg_.stroke();
}

void Gfx::arcStroke(float cx, float cy, float r, float a0, float a1, const Rgba& col, float lw, bool roundCap)
{
    vg_.beginPath();
    vg_.arc(cx, cy, r, a0, a1, a1 >= a0 ? NanoVG::CW : NanoVG::CCW);
    vg_.lineCap(roundCap ? NanoVG::ROUND : NanoVG::BUTT);
    vg_.strokeColor(c(col));
    vg_.strokeWidth(lw);
    vg_.stroke();
    vg_.lineCap(NanoVG::BUTT);
}

void Gfx::circle(float cx, float cy, float r, const Rgba& fillCol)
{
    vg_.beginPath();
    vg_.circle(cx, cy, r);
    vg_.fillColor(c(fillCol));
    vg_.fill();
}

// ---------------------------------------------------------------------------------------------
// panel treatments

void Gfx::well(float x, float y, float w, float h, float r)
{
    fillRR(x, y, w, h, r, col::ink1);
    // inner top shadow: the gradient clamps outside its 6 px span, so filling the same rounded
    // path needs no scissor (DESIGN.md §5.3)
    vg_.beginPath();
    vg_.roundedRect(x, y, w, h, r);
    vg_.fillPaint(vg_.linearGradient(x, y, x, y + 6.f, c(col::black.withAlpha(0.55f)), c(col::black.withAlpha(0.f))));
    vg_.fill();
    hline(x + r, x + w - r, y + h - 1.f, col::warm.withAlpha(0.055f));
}

void Gfx::raised(float x, float y, float w, float h, float r, const Rgba& fill, float hover)
{
    fillRR(x, y + 1.f, w, h, r, col::black.withAlpha(0.35f));
    fillRR(x, y, w, h, r, hover > 0.f ? mix(fill, col::ink6, hover * 0.45f) : fill);
    hline(x + r - 1.f, x + w - r + 1.f, y, col::warm.withAlpha(0.075f));
}

void Gfx::groove(float x0, float y0, float x1, float y1)
{
    if (y0 == y1) {
        hline(x0, x1, y0, col::black.withAlpha(0.55f));
        hline(x0, x1, y0 + 1.f, col::warm.withAlpha(0.045f));
    } else {
        vline(x0, y0, y1, col::black.withAlpha(0.55f));
        vline(x0 + 1.f, y0, y1, col::warm.withAlpha(0.045f));
    }
}

void Gfx::lamp(float cx, float cy, float level, const Rgba& hue, float s)
{
    level = std::clamp(level, 0.f, 1.f);
    if (level > 0.f) {
        vg_.beginPath();
        vg_.rect(cx - s * 2.f, cy - s * 2.f, s * 4.f, s * 4.f);
        vg_.fillPaint(vg_.radialGradient(cx, cy, 0.f, s * 1.8f, c(hue.withAlpha(0.35f * level)), c(hue.withAlpha(0.f))));
        vg_.fill();
        fillRR(cx - s * 0.5f, cy - s * 0.5f, s, s, 1.5f, mix(col::ink0, hue, level));
        line(cx - s * 0.5f + 1.5f, cy - s * 0.5f + 1.f, cx + s * 0.5f - 1.5f, cy - s * 0.5f + 1.f,
             col::white.withAlpha(0.55f * level), 1.f);
    } else {
        fillRR(cx - s * 0.5f, cy - s * 0.5f, s, s, 1.5f, col::ink0);
        strokeRR(cx - s * 0.5f + 0.5f, cy - s * 0.5f + 0.5f, s - 1.f, s - 1.f, 1.f, col::warm.withAlpha(0.08f));
    }
}

void Gfx::dropShadow(float x, float y, float w, float h, float r, float feather, float alpha, float dy)
{
    vg_.beginPath();
    vg_.rect(x - feather, y - feather + dy, w + feather * 2.f, h + feather * 2.f);
    vg_.roundedRect(x, y, w, h, r);
    vg_.pathWinding(NanoVG::CW);
    vg_.fillPaint(vg_.boxGradient(x, y + dy, w, h, r, feather, c(col::black.withAlpha(alpha)), c(col::black.withAlpha(0.f))));
    vg_.fill();
}

// ---------------------------------------------------------------------------------------------
// text

void Gfx::setFont(Font f, float size)
{
    vg_.fontFaceId(f == Font::Sc ? fontSc_ : f == Font::ScSemi ? fontScSemi_ : fontExp_);
    vg_.fontSize(size);
    vg_.textLetterSpacing(0.f);
}

float Gfx::text(const char* s, float x, float y, const TextStyle& st)
{
    if (s == nullptr || *s == '\0')
        return 0.f;
    setFont(st.font, st.size);
    const int h = st.align == Align::Center ? NanoVG::ALIGN_CENTER : st.align == Align::Right ? NanoVG::ALIGN_RIGHT : NanoVG::ALIGN_LEFT;
    vg_.textAlign(h | NanoVG::ALIGN_MIDDLE);
    vg_.fillColor(c(st.color));
    return vg_.text(x, y, s, nullptr) - x;
}

float Gfx::measure(const char* s, float size, Font f)
{
    if (s == nullptr || *s == '\0')
        return 0.f;
    setFont(f, size);
    vg_.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE);
    DGL_NAMESPACE::Rectangle<float> b;
    return vg_.textBounds(0.f, 0.f, s, nullptr, b);
}

// ---------------------------------------------------------------------------------------------
// images

void Gfx::image(const DGL_NAMESPACE::NanoImage& img, float x, float y, float w, float h, float alpha)
{
    if (!img.isValid())
        return;
    vg_.beginPath();
    vg_.rect(x, y, w, h);
    vg_.fillPaint(vg_.imagePattern(x, y, w, h, 0.f, img, alpha));
    vg_.fill();
}

void Gfx::imageFrame(const DGL_NAMESPACE::NanoImage& img, int index, int cols, float x, float y, float w, float h)
{
    if (!img.isValid() || cols < 1)
        return;
    const auto size = img.getSize();
    const float fs = float(size.getWidth()) / float(cols);   // frame size in texels
    const int rows = std::max(1, int(float(size.getHeight()) / fs + 0.5f));
    const int col = index % cols, row = index / cols;
    vg_.beginPath();
    vg_.rect(x, y, w, h);
    vg_.fillPaint(vg_.imagePattern(x - float(col) * w, y - float(row) * h, w * float(cols), h * float(rows), 0.f, img, 1.f));
    vg_.fill();
}

void Gfx::tiledImage(const DGL_NAMESPACE::NanoImage& img, float x, float y, float w, float h, float tile, float alpha)
{
    if (!img.isValid())
        return;
    vg_.beginPath();
    vg_.rect(x, y, w, h);
    vg_.fillPaint(vg_.imagePattern(0.f, 0.f, tile, tile, 0.f, img, alpha));
    vg_.fill();
}

// ---------------------------------------------------------------------------------------------
// dashes

void Gfx::dashedPolyline(const float* pts, int n, float dash, float gap, const Rgba& col, float lw)
{
    if (n < 2)
        return;
    vg_.beginPath();
    const float period = dash + gap;
    float phase = 0.f;  // position within the dash period
    for (int i = 0; i + 1 < n; ++i) {
        const float x0 = pts[i * 2], y0 = pts[i * 2 + 1], x1 = pts[i * 2 + 2], y1 = pts[i * 2 + 3];
        const float len = std::hypot(x1 - x0, y1 - y0);
        if (len <= 1e-4f)
            continue;
        float t = 0.f;
        while (t < len) {
            const bool on = phase < dash;
            const float left = on ? dash - phase : period - phase;
            const float step = std::min(left, len - t);
            if (on) {
                const float a = t / len, b = (t + step) / len;
                vg_.moveTo(x0 + (x1 - x0) * a, y0 + (y1 - y0) * a);
                vg_.lineTo(x0 + (x1 - x0) * b, y0 + (y1 - y0) * b);
            }
            t += step;
            phase += step;
            if (phase >= period - 1e-5f)
                phase = 0.f;
        }
    }
    vg_.strokeColor(c(col));
    vg_.strokeWidth(lw);
    vg_.lineCap(NanoVG::BUTT);
    vg_.stroke();
}

// ---------------------------------------------------------------------------------------------
// icons: 14 px grid centred on (cx, cy), 1.5 px strokes (DESIGN.md §6.10). The geometry is the
// prototype's icon() switch, verbatim.

void Gfx::icon(Icon i, float cx, float cy, const Rgba& colour, float s)
{
    NanoVG& v = vg_;
    const auto C = c(colour);
    v.save();
    v.translate(cx, cy);
    v.scale(s, s);
    v.strokeColor(C);
    v.fillColor(C);
    v.strokeWidth(1.5f);
    v.lineCap(NanoVG::SQUARE);
    v.lineJoin(NanoVG::MITER);
    v.beginPath();
    auto rectF = [&](float x, float y, float w, float h) { v.beginPath(); v.rect(x, y, w, h); v.fill(); };
    switch (i) {
    case Icon::Select:
        v.moveTo(-3.5f, -6.f); v.lineTo(-3.5f, 5.f); v.lineTo(-0.8f, 2.4f); v.lineTo(1.3f, 6.5f);
        v.lineTo(3.f, 5.6f); v.lineTo(1.f, 1.6f); v.lineTo(4.5f, 1.6f); v.closePath(); v.fill();
        break;
    case Icon::Line:
        v.moveTo(-4.5f, 4.5f); v.lineTo(4.5f, -4.5f); v.stroke();
        rectF(-6.5f, 2.5f, 4.f, 4.f); rectF(2.5f, -6.5f, 4.f, 4.f);
        break;
    case Icon::Pencil:
        v.lineCap(NanoVG::BUTT); v.lineJoin(NanoVG::ROUND);
        v.moveTo(-5.5f, 5.5f); v.lineTo(-5.f, 2.5f); v.lineTo(3.f, -5.5f); v.lineTo(5.5f, -3.f); v.lineTo(-2.5f, 5.f);
        v.closePath(); v.stroke();
        v.beginPath(); v.moveTo(1.f, -3.5f); v.lineTo(3.5f, -1.f); v.stroke();
        break;
    case Icon::Prev:
        v.lineCap(NanoVG::ROUND); v.lineJoin(NanoVG::ROUND);
        v.moveTo(2.f, -4.5f); v.lineTo(-2.5f, 0.f); v.lineTo(2.f, 4.5f); v.stroke();
        break;
    case Icon::Next:
        v.lineCap(NanoVG::ROUND); v.lineJoin(NanoVG::ROUND);
        v.moveTo(-2.f, -4.5f); v.lineTo(2.5f, 0.f); v.lineTo(-2.f, 4.5f); v.stroke();
        break;
    case Icon::Caret:
        v.moveTo(-3.5f, -1.5f); v.lineTo(3.5f, -1.5f); v.lineTo(0.f, 2.5f); v.closePath(); v.fill();
        break;
    case Icon::Browse:
        for (float yy : {-4.f, 0.f, 4.f}) { rectF(-5.5f, yy - 0.75f, 2.f, 1.5f); rectF(-2.f, yy - 0.75f, 7.5f, 1.5f); }
        break;
    case Icon::Save:
        v.lineCap(NanoVG::BUTT);
        v.moveTo(0.f, -6.f); v.lineTo(0.f, 1.5f);
        v.moveTo(-3.2f, -1.5f); v.lineTo(0.f, 1.7f); v.lineTo(3.2f, -1.5f);
        v.moveTo(-5.5f, 1.5f); v.lineTo(-5.5f, 5.5f); v.lineTo(5.5f, 5.5f); v.lineTo(5.5f, 1.5f);
        v.stroke();
        break;
    case Icon::Star:
    case Icon::StarOn:
        v.lineJoin(NanoVG::ROUND);
        for (int k = 0; k < 10; ++k) {
            const float a = -kPi * 0.5f + float(k) * kPi / 5.f, r = (k % 2) ? 2.6f : 6.f;
            if (k == 0) v.moveTo(std::cos(a) * r, std::sin(a) * r + 0.4f);
            else        v.lineTo(std::cos(a) * r, std::sin(a) * r + 0.4f);
        }
        v.closePath();
        if (i == Icon::StarOn) v.fill();
        else { v.strokeWidth(1.3f); v.stroke(); }
        break;
    case Icon::Undo:
    case Icon::Redo:
        if (i == Icon::Redo) v.scale(-1.f, 1.f);
        v.lineCap(NanoVG::BUTT);
        v.moveTo(-5.f, -2.f); v.lineTo(2.f, -2.f); v.quadTo(5.5f, -2.f, 5.5f, 1.5f); v.quadTo(5.5f, 5.f, 2.f, 5.f); v.lineTo(-1.f, 5.f);
        v.stroke();
        v.beginPath(); v.moveTo(-2.f, -5.5f); v.lineTo(-5.5f, -2.f); v.lineTo(-2.f, 1.5f); v.stroke();
        break;
    case Icon::More:
        for (float xx : {-5.f, 0.f, 5.f}) v.circle(xx, 0.f, 1.3f);
        v.fill();
        break;
    case Icon::Power:
        // open at the top: the gap is centred on -pi/2
        v.lineCap(NanoVG::ROUND);
        v.arc(0.f, 0.8f, 5.f, -kPi * 0.5f + kPi * 0.28f, -kPi * 0.5f - kPi * 0.28f + kPi * 2.f, NanoVG::CW);
        v.stroke();
        v.beginPath(); v.moveTo(0.f, -6.f); v.lineTo(0.f, -0.5f); v.stroke();
        break;
    case Icon::Link:
        v.strokeWidth(1.4f); v.rotate(-kPi / 4.f);
        v.roundedRect(-7.f, -2.6f, 8.4f, 5.2f, 2.6f); v.roundedRect(-1.4f, -2.6f, 8.4f, 5.2f, 2.6f);
        v.stroke();
        break;
    case Icon::Unlink:
        v.strokeWidth(1.4f); v.rotate(-kPi / 4.f);
        v.roundedRect(-8.2f, -2.6f, 6.6f, 5.2f, 2.6f); v.roundedRect(1.6f, -2.6f, 6.6f, 5.2f, 2.6f);
        v.stroke();
        v.beginPath(); v.rotate(kPi / 4.f);
        v.moveTo(-3.5f, -5.5f); v.lineTo(-2.5f, -3.8f); v.moveTo(3.5f, 5.5f); v.lineTo(2.5f, 3.8f);
        v.strokeWidth(1.2f); v.stroke();
        break;
    case Icon::Phones:
        v.lineCap(NanoVG::BUTT);
        v.arc(0.f, 1.5f, 5.2f, kPi * 1.08f, kPi * 1.92f, NanoVG::CW);
        v.stroke();
        v.beginPath(); v.roundedRect(-6.6f, 0.2f, 3.4f, 5.8f, 1.2f); v.roundedRect(3.2f, 0.2f, 3.4f, 5.8f, 1.2f); v.fill();
        break;
    case Icon::Rec:
        v.circle(0.f, 0.f, 4.f); v.fill();
        break;
    case Icon::Stop:
        v.rect(-3.5f, -3.5f, 7.f, 7.f); v.fill();
        break;
    case Icon::Loop:
        v.strokeWidth(1.4f); v.lineCap(NanoVG::BUTT);
        v.moveTo(-5.f, 1.f); v.lineTo(-5.f, -1.f); v.quadTo(-5.f, -4.f, -2.f, -4.f); v.lineTo(4.f, -4.f);
        v.moveTo(5.f, -1.f); v.lineTo(5.f, 1.f); v.quadTo(5.f, 4.f, 2.f, 4.f); v.lineTo(-4.f, 4.f);
        v.stroke();
        v.beginPath();
        v.moveTo(2.f, -6.3f); v.lineTo(4.6f, -4.f); v.lineTo(2.f, -1.7f);
        v.moveTo(-2.f, 1.7f); v.lineTo(-4.6f, 4.f); v.lineTo(-2.f, 6.3f);
        v.stroke();
        break;
    case Icon::OneShot:
        v.strokeWidth(1.4f); v.lineCap(NanoVG::BUTT);
        v.moveTo(-6.f, 0.f); v.lineTo(3.f, 0.f); v.stroke();
        v.beginPath(); v.moveTo(0.f, -3.f); v.lineTo(3.2f, 0.f); v.lineTo(0.f, 3.f); v.stroke();
        rectF(4.5f, -5.f, 1.6f, 10.f);
        break;
    case Icon::Delta:
        v.strokeWidth(1.4f);
        v.moveTo(0.f, -5.5f); v.lineTo(5.8f, 4.8f); v.lineTo(-5.8f, 4.8f); v.closePath(); v.stroke();
        break;
    case Icon::Plus:
        v.moveTo(-5.f, 0.f); v.lineTo(5.f, 0.f); v.moveTo(0.f, -5.f); v.lineTo(0.f, 5.f); v.stroke();
        break;
    case Icon::Close:
        v.moveTo(-4.5f, -4.5f); v.lineTo(4.5f, 4.5f); v.moveTo(4.5f, -4.5f); v.lineTo(-4.5f, 4.5f); v.stroke();
        break;
    case Icon::Grip:
        v.strokeWidth(1.f);
        for (float o : {0.f, 3.5f, 7.f}) { v.moveTo(5.f - o, 5.f); v.lineTo(5.f, 5.f - o); }
        v.stroke();
        break;
    case Icon::Shift:
        v.strokeWidth(1.4f); v.lineCap(NanoVG::BUTT);
        v.moveTo(-6.f, 0.f); v.lineTo(6.f, 0.f);
        v.moveTo(-3.5f, -2.5f); v.lineTo(-6.f, 0.f); v.lineTo(-3.5f, 2.5f);
        v.moveTo(3.5f, -2.5f); v.lineTo(6.f, 0.f); v.lineTo(3.5f, 2.5f);
        v.stroke();
        break;
    case Icon::Folder:
        v.strokeWidth(1.4f);
        v.moveTo(-6.f, -4.f); v.lineTo(-2.f, -4.f); v.lineTo(-0.5f, -2.5f); v.lineTo(6.f, -2.5f); v.lineTo(6.f, 4.5f);
        v.lineTo(-6.f, 4.5f); v.closePath(); v.stroke();
        break;
    case Icon::Search:
        v.circle(-1.f, -1.f, 4.f); v.moveTo(2.f, 2.f); v.lineTo(5.5f, 5.5f); v.stroke();
        break;
    }
    v.restore();
}

}} // namespace kick::ui
