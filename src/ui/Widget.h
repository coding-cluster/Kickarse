// Kickarse UI — widget base, shared services and the value-control kit (DESIGN.md §6, §9.1).
// Widgets live in one flat list owned by View, painted in order and hit-tested in reverse, so
// later widgets (overlays) win. All coordinates are 1x logical pixels.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Base.hpp"
#include "NanoVG.hpp"

#include "Gfx.h"
#include "Live.h"
#include "Model.h"
#include "Settings.h"

namespace kick { class PresetStore; class ShapeLibrary; }

namespace kick { namespace ui {

struct Pointer {
    float x = 0.f, y = 0.f;
    bool  shift = false, ctrl = false, alt = false;
};

struct MenuItem {
    std::string label;
    std::function<void()> action;
    bool check = false, dim = false, header = false, separator = false;

    static MenuItem sep() { MenuItem m; m.separator = true; return m; }
    static MenuItem head(std::string s) { MenuItem m; m.label = std::move(s); m.header = true; return m; }
};

enum class Img { KnobHero, KnobSmall, Grain };

// What widgets can ask of the view.
class Services {
public:
    virtual ~Services() = default;
    virtual Model&       model() = 0;
    virtual const Live&  live() const = 0;
    virtual Settings&    settings() = 0;
    virtual Gfx&         gfx() = 0;
    virtual PresetStore* presets() = 0;
    virtual ShapeLibrary* shapes() = 0;
    virtual const DGL_NAMESPACE::NanoImage& image(Img i) const = 0;
    virtual double now() const = 0;           // seconds, monotonic
    virtual void   repaint() = 0;

    virtual void openMenu(float x, float y, std::vector<MenuItem> items, float rightEdge = -1.f, bool above = false) = 0;
    virtual void openRateGrid(float x, float y) = 0;
    virtual void openPresetBrowser(bool open) = 0;
    virtual bool presetBrowserOpen() const = 0;
    virtual void openEntry(int paramId) = 0;
    virtual bool entryOpenFor(int paramId) const = 0;
    virtual void drawEntry(Gfx& g, float cx, float cy, float size) = 0;  // the value-entry field
    virtual void paramMenu(int paramId, float x, float y) = 0;          // Reset / Enter value… / copy / paste
    virtual void applyShape(const Envelope& e) = 0;                      // one undo step + morph
    virtual void loadPreset(const std::string& id) = 0;
    virtual void savePreset(bool saveAs) = 0;
    virtual void setScale(float s) = 0;
    virtual void capture() = 0;                                          // arm / cancel recording
    // Free-text entry (names): a field centred at (cx, cy); commit receives the text.
    virtual void openTextEntry(const std::string& initial, float cx, float cy, float width,
                               std::function<void(const std::string&)> commit) = 0;
    virtual bool textEntryOpen() const = 0;
    virtual void drawTextEntry(Gfx& g) = 0;
};

class Widget {
public:
    explicit Widget(Services& s) : sv(s) {}
    virtual ~Widget() = default;
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    RectF r;
    std::function<bool()> visibleIf;   // evaluated by View every frame; null = always visible
    std::function<void()> onLayout;    // optional per-frame geometry update (e.g. rows that shift)
    bool  visible = true;
    float hot = 0.f;                   // hover animation 0…1 (View drives it)

    virtual void paint(Gfx& g) = 0;
    virtual bool hits(float x, float y) const { return r.contains(x, y); }
    virtual bool interactive() const { return true; }
    virtual bool modal() const { return false; }      // swallows every click (menus, browser)

    virtual void down(const Pointer&) {}
    virtual void drag(const Pointer&, float /*dx*/, float /*dy*/) {}
    virtual void up(const Pointer&) {}
    virtual void dbl(const Pointer& p) { down(p); }
    virtual void wheel(const Pointer&, float /*notches*/) {}
    virtual void context(const Pointer&) {}
    virtual void move(const Pointer&) {}              // hover motion
    // false: move() calls sv.repaint() itself when the hover state it draws changes
    virtual bool repaintsOnMove() const { return true; }
    virtual void leave() {}
    virtual bool key(unsigned /*key*/, const Pointer&) { return false; }
    virtual DGL_NAMESPACE::MouseCursor cursor(const Pointer&) const { return DGL_NAMESPACE::kMouseCursorArrow; }
    virtual std::string hint() const { return {}; }
    virtual bool animating() const { return false; }  // keeps the view repainting

protected:
    Services& sv;
};

// ---------------------------------------------------------------------------------------------
// Paint-only decoration (titles, captions, grooves).
class Decor : public Widget {
public:
    Decor(Services& s, std::function<void(Gfx&)> fn) : Widget(s), fn_(std::move(fn)) {}
    void paint(Gfx& g) override { fn_(g); }
    bool interactive() const override { return false; }
    bool hits(float, float) const override { return false; }
private:
    std::function<void(Gfx&)> fn_;
};

// ---------------------------------------------------------------------------------------------
// Shared value-control behaviour: vertical (or horizontal) relative drag over pxPerRange,
// Shift x0.1, double-click / Ctrl-click reset, Alt-click type-in, wheel 2 % (0.2 % with Shift),
// right-click menu. Enum/integer parameters step by one per wheel notch.
class ParamGesture {
public:
    ParamGesture(Services& s, int id, float pxPerRange, bool horizontal)
        : sv_(s), id_(id), pxPerRange_(pxPerRange), horizontal_(horizontal) {}

    int  id() const noexcept { return id_; }
    void down(const Pointer& p);
    void drag(const Pointer& p, float dx, float dy);
    void up();
    void reset();
    void wheel(const Pointer& p, float notches);
    void context(const Pointer& p) { sv_.paramMenu(id_, p.x, p.y); }
    std::string hint() const;
    bool dragging() const noexcept { return active_; }

private:
    Services& sv_;
    int   id_;
    float pxPerRange_;
    bool  horizontal_;
    bool  active_ = false;
    float startNorm_ = 0.f, acc_ = 0.f;
};

enum class KnobSize { Small, Hero };

class Knob : public Widget {
public:
    Knob(Services& s, int id, float cx, float cy, KnobSize size, Rgba arc, const char* label = nullptr, bool bipolar = false);

    std::function<bool()> dimIf;        // multiband section when Split is off
    std::function<float()> liveAmount;  // hero only: inner "live duck" arc (0…1)
    bool showValue = true;

    void paint(Gfx& g) override;
    bool hits(float x, float y) const override;
    void down(const Pointer& p) override;
    void drag(const Pointer& p, float dx, float dy) override { g_.drag(p, dx, dy); }
    void up(const Pointer&) override { g_.up(); }
    void dbl(const Pointer& p) override;
    void wheel(const Pointer& p, float n) override { g_.wheel(p, n); }
    void context(const Pointer& p) override { g_.context(p); }
    DGL_NAMESPACE::MouseCursor cursor(const Pointer& p) const override;
    std::string hint() const override;

private:
    bool onValueText(float x, float y) const;
    ParamGesture g_;
    float cx_, cy_;
    KnobSize size_;
    Rgba arc_;
    const char* label_;
    bool bipolar_;
};

// Label + draggable number in a recessed box (Swing, Smooth, Rotate, Length, Hold …).
class ValueField : public Widget {
public:
    ValueField(Services& s, int id, RectF rect, const char* label);
    void paint(Gfx& g) override;
    void down(const Pointer& p) override { g_.down(p); }
    void drag(const Pointer& p, float dx, float dy) override { g_.drag(p, dx, dy); }
    void up(const Pointer&) override { g_.up(); }
    void dbl(const Pointer&) override { g_.reset(); }
    void wheel(const Pointer& p, float n) override { g_.wheel(p, n); }
    void context(const Pointer& p) override { g_.context(p); }
    DGL_NAMESPACE::MouseCursor cursor(const Pointer&) const override { return DGL_NAMESPACE::kMouseCursorUpDown; }
    std::string hint() const override { return g_.hint(); }
private:
    ParamGesture g_;
    const char* label_;
};

// A printed value that opens the type-in field when clicked (e.g. the threshold readout).
class ValueText : public Widget {
public:
    ValueText(Services& s, int id, float x, float y, Align align, float size = 11.5f, Font font = Font::ScSemi);
    void paint(Gfx& g) override;
    void down(const Pointer&) override { sv.openEntry(id_); }
    void context(const Pointer& p) override { sv.paramMenu(id_, p.x, p.y); }
    DGL_NAMESPACE::MouseCursor cursor(const Pointer&) const override { return DGL_NAMESPACE::kMouseCursorCaret; }
    std::string hint() const override;
    Rgba color = col::text;
private:
    int id_; float x_, y_; Align align_; float size_; Font font_;
};

// Raised key with an LED lamp (DESIGN.md §6.3).
class LampButton : public Widget {
public:
    LampButton(Services& s, const char* label, RectF rect, std::function<bool()> isOn, std::function<void()> onClick,
               Rgba hue = col::duck, std::string hint = {}, bool hasIcon = false, Icon icon = Icon::Phones);
    void paint(Gfx& g) override;
    void down(const Pointer&) override { if (click_) click_(); }
    void dbl(const Pointer&) override { if (click_) click_(); }
    DGL_NAMESPACE::MouseCursor cursor(const Pointer&) const override { return DGL_NAMESPACE::kMouseCursorHand; }
    std::string hint() const override { return hint_; }
    std::function<bool()> dimIf;
private:
    std::string label_;
    std::function<bool()> on_;
    std::function<void()> click_;
    Rgba hue_;
    std::string hint_;
    bool hasIcon_;
    Icon icon_;
};

class IconButton : public Widget {
public:
    IconButton(Services& s, Icon icon, RectF rect, std::function<void()> onClick, std::string hint = {});
    std::function<bool()> isOn, isDisabled;
    std::function<Icon()> iconFn;
    Rgba activeCol = col::duck;
    void paint(Gfx& g) override;
    void down(const Pointer&) override;
    void dbl(const Pointer& p) override { down(p); }
    DGL_NAMESPACE::MouseCursor cursor(const Pointer&) const override { return DGL_NAMESPACE::kMouseCursorHand; }
    std::string hint() const override { return hint_; }
    std::function<std::string()> hintFn;
private:
    Icon icon_;
    std::function<void()> click_;
    std::string hint_;
};

// Recessed track with a raised key on the selected segment (DESIGN.md §6.4).
class Segmented : public Widget {
public:
    Segmented(Services& s, RectF rect, std::vector<std::string> labels, std::function<int()> get,
              std::function<void(int)> set, bool fixed = false);
    std::vector<Icon> icons;                 // optional, one per label
    std::vector<Rgba> selectedColors;        // optional per-segment selected text colour
    std::function<bool()> disabledIf;
    std::vector<std::string> hints;
    void paint(Gfx& g) override;
    void down(const Pointer& p) override;
    void move(const Pointer& p) override { hoverSeg_ = segmentAt(p.x); }
    void leave() override { hoverSeg_ = -1; }
    DGL_NAMESPACE::MouseCursor cursor(const Pointer&) const override { return DGL_NAMESPACE::kMouseCursorHand; }
    std::string hint() const override;
private:
    void layoutSegments(Gfx& g);
    int  segmentAt(float x) const;
    std::vector<std::string> labels_;
    std::function<int()> get_;
    std::function<void(int)> set_;
    bool fixed_;
    std::vector<float> xs_;   // segment start x (+ end)
    int hoverSeg_ = -1;
};

}} // namespace kick::ui
