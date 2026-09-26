// Kickarse UI — drawing helpers over DGL's NanoVG wrapper: the panel treatments of DESIGN.md §5
// (well, raised key, groove, lamp, drop shadow), text roles, crisp hairlines and the icon set.
// Everything takes 1x logical coordinates; the caller has already applied the global scale.
#pragma once

#include "NanoVG.hpp"

#include "Theme.h"

namespace kick { namespace ui {

enum class Align { Left, Center, Right };

struct TextStyle {
    float size   = 11.f;
    Font  font   = Font::Sc;
    Rgba  color  = col::text;
    Align align  = Align::Left;
};

enum class Icon {
    Select, Line, Pencil, Prev, Next, Caret, Browse, Save, Star, StarOn, Undo, Redo, More, Power,
    Link, Unlink, Phones, Rec, Stop, Loop, OneShot, Delta, Plus, Close, Grip, Shift, Folder, Search,
};

class Gfx {
public:
    using NanoVG = DGL_NAMESPACE::NanoVG;

    explicit Gfx(NanoVG& vg) noexcept : vg_(vg) {}

    NanoVG& vg() noexcept { return vg_; }

    // Device pixels per logical pixel (the global transform). Used to snap hairlines.
    void  setScale(float k) noexcept { k_ = k; }
    float scale() const noexcept { return k_; }

    void setFonts(NanoVG::FontId sc, NanoVG::FontId scSemi, NanoVG::FontId exp) noexcept
    {
        fontSc_ = sc; fontScSemi_ = scSemi; fontExp_ = exp;
    }

    static DGL_NAMESPACE::Color c(const Rgba& v) noexcept { return DGL_NAMESPACE::Color(v.r, v.g, v.b, v.a); }

    // -- primitives -------------------------------------------------------------------------
    void fillRect(float x, float y, float w, float h, const Rgba& col);
    void fillRR(float x, float y, float w, float h, float r, const Rgba& col);
    void strokeRR(float x, float y, float w, float h, float r, const Rgba& col, float lw = 1.f);
    void line(float x0, float y0, float x1, float y1, const Rgba& col, float lw = 1.f, bool round = false);
    // 1-logical-px rules snapped to the device pixel grid (DESIGN.md §13.2)
    void hline(float x0, float x1, float y, const Rgba& col);
    void vline(float x, float y0, float y1, const Rgba& col);
    float snap(float v) const noexcept;
    void arcStroke(float cx, float cy, float r, float a0, float a1, const Rgba& col, float lw, bool roundCap);
    void circle(float cx, float cy, float r, const Rgba& fillCol);

    // -- panel treatments (DESIGN.md §5) ---------------------------------------------------
    void well(float x, float y, float w, float h, float r = 3.f);
    void raised(float x, float y, float w, float h, float r, const Rgba& fill, float hover = 0.f);
    void groove(float x0, float y0, float x1, float y1);
    void lamp(float cx, float cy, float level, const Rgba& hue, float s = 5.f);
    void dropShadow(float x, float y, float w, float h, float r, float feather, float alpha, float dy);

    // -- text ------------------------------------------------------------------------------
    // y is the vertical centre of the line (Canvas 'middle' semantics used by the prototype).
    float text(const char* s, float x, float y, const TextStyle& st);
    float measure(const char* s, float size, Font f = Font::Sc);

    // -- images ------------------------------------------------------------------------------
    void image(const DGL_NAMESPACE::NanoImage& img, float x, float y, float w, float h, float alpha = 1.f);
    void tiledImage(const DGL_NAMESPACE::NanoImage& img, float x, float y, float w, float h, float tile, float alpha);

    // -- icons (DESIGN.md §6.10) ---------------------------------------------------------------
    void icon(Icon i, float cx, float cy, const Rgba& col, float s = 1.f);

    // -- misc ---------------------------------------------------------------------------------
    // Polyline with a dash pattern (NanoVG has none). pts = x0,y0,x1,y1,...
    void dashedPolyline(const float* pts, int n, float dash, float gap, const Rgba& col, float lw);

private:
    void setFont(Font f, float size);

    NanoVG& vg_;
    float   k_ = 1.f;
    NanoVG::FontId fontSc_ = -1, fontScSemi_ = -1, fontExp_ = -1;
};

}} // namespace kick::ui
