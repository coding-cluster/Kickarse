// Kickarse UI — value-control kit (see Widget.h). Geometry and colours: DESIGN.md §6.1–§6.6.
#include "Widget.h"

#include <algorithm>
#include <cmath>

#include "Format.h"

namespace kick { namespace ui {

using DGL_NAMESPACE::MouseCursor;

// ---------------------------------------------------------------------------------------------
// ParamGesture

void ParamGesture::down(const Pointer& p)
{
    Model& m = sv_.model();
    if (p.ctrl) {                       // FabFilter / Serum muscle memory: Ctrl-click resets
        reset();
        return;
    }
    if (p.alt) {                        // Alt-click types a value
        sv_.openEntry(id_);
        return;
    }
    m.beginGesture(id_);
    active_    = true;
    startNorm_ = toNorm(id_, m.value(id_));
    acc_       = 0.f;
}

void ParamGesture::drag(const Pointer& p, float dx, float dy)
{
    if (!active_)
        return;
    acc_ += (horizontal_ ? dx : -dy) * (p.shift ? 0.1f : 1.f) / pxPerRange_;
    sv_.model().gestureSet(id_, fromNorm(id_, startNorm_ + acc_));
}

void ParamGesture::up()
{
    if (!active_)
        return;
    active_ = false;
    sv_.model().endGesture(id_);
}

void ParamGesture::reset()
{
    active_ = false;
    sv_.model().setOnce(id_, kParams[id_].def);
}

void ParamGesture::wheel(const Pointer& p, float notches)
{
    Model& m = sv_.model();
    const float v = m.value(id_);
    if (isStepped(id_))
        m.setOnce(id_, clampParam(id_, v + (notches > 0.f ? 1.f : -1.f)));
    else
        m.setOnce(id_, fromNorm(id_, toNorm(id_, v) + notches * (p.shift ? 0.002f : 0.02f)));
}

std::string ParamGesture::hint() const
{
    return std::string(paramLabel(id_)) + "  " + formatParam(id_, sv_.model().value(id_), sv_.settings().noteC3)
        + "   \xE2\x80\x94   drag \xC2\xB7 Shift fine \xC2\xB7 double-click reset \xC2\xB7 Alt-click type \xC2\xB7 right-click menu";
}

// ---------------------------------------------------------------------------------------------
// Knob

Knob::Knob(Services& s, int id, float cx, float cy, KnobSize size, Rgba arc, const char* label, bool bipolar)
    : Widget(s), g_(s, id, size == KnobSize::Hero ? 260.f : 200.f, false), cx_(cx), cy_(cy), size_(size), arc_(arc),
      label_(label), bipolar_(bipolar)
{
    const float ringR = size == KnobSize::Hero ? 50.f : 20.f;
    r = {cx - ringR - 4.f, cy - ringR - 4.f, ringR * 2.f + 8.f, ringR * 2.f + 8.f};
    if (label_ != nullptr)
        r.h += 30.f;  // label + value rows are part of the control
}

bool Knob::hits(float x, float y) const
{
    return r.contains(x, y);
}

bool Knob::onValueText(float x, float y) const
{
    if (label_ == nullptr || !showValue)
        return false;
    const float ringR = size_ == KnobSize::Hero ? 50.f : 20.f;
    const float vy = cy_ + ringR + 26.f;
    return y >= vy - 8.f && y <= vy + 8.f && std::fabs(x - cx_) < 30.f;
}

void Knob::down(const Pointer& p)
{
    if (onValueText(p.x, p.y) && !p.ctrl) {
        sv.openEntry(g_.id());
        return;
    }
    g_.down(p);
}

void Knob::dbl(const Pointer& p)
{
    if (onValueText(p.x, p.y))
        return;
    g_.reset();
}

MouseCursor Knob::cursor(const Pointer& p) const
{
    return onValueText(p.x, p.y) ? DGL_NAMESPACE::kMouseCursorCaret : DGL_NAMESPACE::kMouseCursorUpDown;
}

std::string Knob::hint() const
{
    return g_.hint();
}

void Knob::paint(Gfx& g)
{
    auto& vg = g.vg();
    Model& m = sv.model();
    const int id = g_.id();
    const bool hero = size_ == KnobSize::Hero;
    const float ringR = hero ? 50.f : 20.f, ringW = hero ? 3.f : 2.f;
    const float n = toNorm(id, m.value(id));
    const bool dim = dimIf && dimIf();
    const float hv = hot;

    vg.save();
    if (dim)
        vg.globalAlpha(0.38f);
    // track: ink0 underlay, then ink6
    g.arcStroke(cx_, cy_, ringR, kKnobA0, kKnobA0 + kKnobSweep, col::ink0, ringW + 2.f, true);
    g.arcStroke(cx_, cy_, ringR, kKnobA0, kKnobA0 + kKnobSweep, col::ink6, ringW, true);
    const float n0 = bipolar_ ? toNorm(id, kParams[id].def) : 0.f;
    if (std::fabs(n - n0) > 0.002f) {
        const Rgba c = hv > 0.5f || g_.dragging() ? mix(arc_, col::white, 0.25f) : arc_;
        g.arcStroke(cx_, cy_, ringR, kKnobA0 + kKnobSweep * n0, kKnobA0 + kKnobSweep * n, c, ringW, true);
    }
    if (bipolar_) {
        const float a = kKnobA0 + kKnobSweep * n0;
        g.line(cx_ + std::cos(a) * (ringR - 3.f), cy_ + std::sin(a) * (ringR - 3.f), cx_ + std::cos(a) * (ringR + 3.f),
               cy_ + std::sin(a) * (ringR + 3.f), col::textDim, 1.f);
    }
    if (hero) {
        // precision scale: 21 ticks, majors every 25 %
        for (int i = 0; i <= 20; ++i) {
            const float a = kKnobA0 + kKnobSweep * float(i) / 20.f;
            const bool major = i % 5 == 0;
            const float r0 = ringR + 5.f, r1 = ringR + (major ? 10.f : 7.f);
            g.line(cx_ + std::cos(a) * r0, cy_ + std::sin(a) * r0, cx_ + std::cos(a) * r1, cy_ + std::sin(a) * r1,
                   major ? col::textDim : col::ink7, major ? 1.25f : 1.f);
        }
        if (liveAmount) {
            const float ln = std::clamp(liveAmount(), 0.f, 1.f);
            if (ln > 0.004f)
                g.arcStroke(cx_, cy_, ringR - 6.f, kKnobA0, kKnobA0 + kKnobSweep * ln, col::duck.withAlpha(0.8f), 1.5f, true);
        }
    }
    // body (static lighting) + vector pointer
    const float half = hero ? 48.f : 22.f;
    g.image(sv.image(hero ? Img::KnobHero : Img::KnobSmall), cx_ - half, cy_ - half, half * 2.f, half * 2.f);
    const float a = kKnobA0 + kKnobSweep * n, ca = std::cos(a), sa = std::sin(a);
    if (hero) {
        g.line(cx_ + ca * 9.f, cy_ + sa * 9.f, cx_ + ca * 27.f, cy_ + sa * 27.f, Rgba{38.f / 255.f, 34.f / 255.f, 29.f / 255.f, 0.92f}, 3.2f, true);
        g.line(cx_ + ca * 9.f + 0.7f, cy_ + sa * 9.f + 0.9f, cx_ + ca * 27.f + 0.7f, cy_ + sa * 27.f + 0.9f, col::white.withAlpha(0.28f), 0.9f, true);
    } else {
        g.line(cx_ + ca * 4.f, cy_ + sa * 4.f, cx_ + ca * 12.5f, cy_ + sa * 12.5f, col::textHi, 2.f, true);
    }
    vg.restore();

    if (label_ != nullptr) {
        g.text(label_, cx_, cy_ + ringR + 12.f, {11.f, Font::Sc, dim ? col::textDim : col::textMute, Align::Center});
        if (showValue) {
            const float vy = cy_ + ringR + 26.f;
            if (sv.entryOpenFor(id)) {
                sv.drawEntry(g, cx_, vy, 11.5f);
            } else {
                const std::string s = formatParam(id, m.value(id), sv.settings().noteC3);
                const Rgba c = dim ? col::textDim : mix(col::text, col::textHi, hv);
                g.text(s.c_str(), cx_, vy, {11.5f, Font::ScSemi, c, Align::Center});
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// ValueField

ValueField::ValueField(Services& s, int id, RectF rect, const char* label)
    : Widget(s), g_(s, id, 200.f, false), label_(label)
{
    r = rect;
}

void ValueField::paint(Gfx& g)
{
    const float hv = hot;
    g.well(r.x, r.y, r.w, r.h, 3.f);
    if (hv > 0.01f)
        g.strokeRR(r.x + 0.5f, r.y + 0.5f, r.w - 1.f, r.h - 1.f, 3.f, col::warm.withAlpha(0.08f * hv));
    g.text(label_, r.x + 8.f, r.cy() + 0.5f, {11.f, Font::Sc, col::textMute, Align::Left});
    const int id = g_.id();
    if (sv.entryOpenFor(id)) {
        sv.drawEntry(g, r.right() - 30.f, r.cy(), 11.5f);
        return;
    }
    const std::string s = formatParam(id, sv.model().value(id), sv.settings().noteC3);
    g.text(s.c_str(), r.right() - 8.f, r.cy() + 0.5f, {11.5f, Font::ScSemi, mix(col::text, col::textHi, hv), Align::Right});
}

// ---------------------------------------------------------------------------------------------
// ValueText

ValueText::ValueText(Services& s, int id, float x, float y, Align align, float size, Font font)
    : Widget(s), id_(id), x_(x), y_(y), align_(align), size_(size), font_(font)
{
    r = {x - 40.f, y - 9.f, 80.f, 18.f};
    if (align == Align::Right) r.x = x - 80.f;
    if (align == Align::Left) r.x = x;
}

void ValueText::paint(Gfx& g)
{
    const float cx = align_ == Align::Center ? x_ : align_ == Align::Right ? x_ - 30.f : x_ + 30.f;
    if (sv.entryOpenFor(id_)) {
        sv.drawEntry(g, cx, y_, size_);
        return;
    }
    const std::string s = formatParam(id_, sv.model().value(id_), sv.settings().noteC3);
    const float w = g.measure(s.c_str(), size_, font_);
    // hit area follows the text
    r = {align_ == Align::Center ? x_ - w * 0.5f - 4.f : align_ == Align::Right ? x_ - w - 4.f : x_ - 4.f, y_ - 8.f, w + 8.f, 16.f};
    g.text(s.c_str(), x_, y_, {size_, font_, mix(color, col::textHi, hot), align_});
    if (hot > 0.05f) {
        const float x0 = r.x + 4.f;
        g.line(x0, y_ + size_ * 0.5f + 1.5f, x0 + w, y_ + size_ * 0.5f + 1.5f, col::textMute.withAlpha(0.6f * hot), 1.f);
    }
}

std::string ValueText::hint() const
{
    return std::string(paramLabel(id_)) + ": click to type a value";
}

// ---------------------------------------------------------------------------------------------
// LampButton

LampButton::LampButton(Services& s, const char* label, RectF rect, std::function<bool()> isOn,
                       std::function<void()> onClick, Rgba hue, std::string hint, bool hasIcon, Icon icon)
    : Widget(s), label_(label), on_(std::move(isOn)), click_(std::move(onClick)), hue_(hue), hint_(std::move(hint)),
      hasIcon_(hasIcon), icon_(icon)
{
    r = rect;
}

void LampButton::paint(Gfx& g)
{
    const bool on = on_ && on_();
    const float hv = hot;
    auto& vg = g.vg();
    const bool dim = dimIf && dimIf();
    if (dim) { vg.save(); vg.globalAlpha(0.38f); }
    g.raised(r.x, r.y, r.w, r.h, 3.f, col::ink4, hv);
    g.lamp(r.x + 11.f, r.cy(), on ? 1.f : 0.f, hue_, 5.f);
    const float tx = r.x + 20.f;
    const Rgba tc = on ? col::textHi : mix(col::textMute, col::text, hv);
    if (hasIcon_) {
        g.icon(icon_, tx + 6.f, r.cy(), on ? col::textHi : col::textMute, 0.85f);
        g.text(label_.c_str(), tx + 15.f, r.cy() + 0.5f, {11.f, Font::Sc, tc, Align::Left});
    } else {
        g.text(label_.c_str(), tx, r.cy() + 0.5f, {11.f, Font::Sc, tc, Align::Left});
    }
    if (dim) vg.restore();
}

// ---------------------------------------------------------------------------------------------
// IconButton

IconButton::IconButton(Services& s, Icon icon, RectF rect, std::function<void()> onClick, std::string hint)
    : Widget(s), icon_(icon), click_(std::move(onClick)), hint_(std::move(hint))
{
    r = rect;
}

void IconButton::down(const Pointer&)
{
    if (isDisabled && isDisabled())
        return;
    if (click_)
        click_();
}

void IconButton::paint(Gfx& g)
{
    const bool disabled = isDisabled && isDisabled();
    const float hv = disabled ? 0.f : hot;
    if (hv > 0.01f)
        g.fillRR(r.x, r.y, r.w, r.h, 3.f, col::warm.withAlpha(0.05f * hv));
    const bool on = isOn && isOn();
    const Rgba c = disabled ? col::textDim : on ? activeCol : mix(col::textMute, col::textHi, hv);
    g.icon(iconFn ? iconFn() : icon_, r.cx(), r.cy(), c);
}

// ---------------------------------------------------------------------------------------------
// Segmented

Segmented::Segmented(Services& s, RectF rect, std::vector<std::string> labels, std::function<int()> get,
                     std::function<void(int)> set, bool fixed)
    : Widget(s), labels_(std::move(labels)), get_(std::move(get)), set_(std::move(set)), fixed_(fixed)
{
    r = rect;
}

void Segmented::layoutSegments(Gfx& g)
{
    const std::size_t n = labels_.size();
    xs_.assign(n + 1, r.x + 2.f);
    std::vector<float> widths(n);
    float sum = 0.f;
    for (std::size_t i = 0; i < n; ++i) {
        widths[i] = g.measure(labels_[i].c_str(), 11.f) + (icons.empty() ? 0.f : 16.f);
        sum += widths[i];
    }
    const float extra = (r.w - 4.f - sum) / float(n);
    float x = r.x + 2.f;
    for (std::size_t i = 0; i < n; ++i) {
        xs_[i] = x;
        x += fixed_ ? (r.w - 4.f) / float(n) : widths[i] + extra;
    }
    xs_[n] = x;
}

int Segmented::segmentAt(float x) const
{
    for (std::size_t i = 0; i + 1 < xs_.size(); ++i)
        if (x >= xs_[i] && x < xs_[i + 1])
            return int(i);
    return -1;
}

void Segmented::down(const Pointer& p)
{
    if (disabledIf && disabledIf())
        return;
    const int i = segmentAt(p.x);
    if (i >= 0 && set_)
        set_(i);
}

std::string Segmented::hint() const
{
    if (hoverSeg_ >= 0 && hoverSeg_ < int(hints.size()))
        return hints[std::size_t(hoverSeg_)];
    return hoverSeg_ >= 0 && hoverSeg_ < int(labels_.size()) ? labels_[std::size_t(hoverSeg_)] : std::string();
}

void Segmented::paint(Gfx& g)
{
    layoutSegments(g);
    const bool disabled = disabledIf && disabledIf();
    auto& vg = g.vg();
    if (disabled) { vg.save(); vg.globalAlpha(0.45f); }
    g.well(r.x, r.y, r.w, r.h, 3.f);
    const int sel = get_ ? get_() : -1;
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        const float x = xs_[i], sw = xs_[i + 1] - xs_[i];
        const bool isSel = int(i) == sel;
        const float hv = (!disabled && hoverSeg_ == int(i)) ? hot : 0.f;
        if (isSel)
            g.raised(x, r.y + 2.f, sw, r.h - 4.f, 2.f, col::ink5);
        const Rgba tc = disabled ? col::textDim
                      : isSel ? (i < selectedColors.size() ? selectedColors[i] : col::textHi)
                              : mix(col::textMute, col::text, hv);
        const char* l = labels_[i].c_str();
        if (!icons.empty()) {
            g.icon(icons[i], x + sw * 0.5f - g.measure(l, 11.f) * 0.5f - 4.f, r.cy(), tc, 0.8f);
            g.text(l, x + sw * 0.5f + 8.f, r.cy() + 0.5f, {11.f, isSel ? Font::ScSemi : Font::Sc, tc, Align::Center});
        } else {
            g.text(l, x + sw * 0.5f, r.cy() + 0.5f, {11.f, isSel ? Font::ScSemi : Font::Sc, tc, Align::Center});
        }
    }
    if (disabled) vg.restore();
}

}} // namespace kick::ui
