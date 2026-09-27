// Kickarse UI — bespoke controls and live displays: rate stepper, grid dropdown, M/S slider,
// threshold meter, detection-filter graph, crossover graph, spectral view, bar view, MIDI
// keyboard and note stepper, GR meter (DESIGN.md §6.5–§6.14, §8).
#pragma once

#include "Widget.h"

namespace kick { namespace ui {

// Rate as a stepped rotary switch: an amber "chicken head" lever with one detent per note value
// (pre-rendered per detent, see tools/assets/knobs.py), a few labelled positions around it and
// the value with its length in ms beside it. Drag, scroll or click a label to switch; the value
// opens the note-value grid.
class RateSwitch : public Widget {
public:
    RateSwitch(Services& s, RectF rect);
    void paint(Gfx& g) override;
    void down(const Pointer& p) override;
    void drag(const Pointer& p, float dx, float dy) override { g_.drag(p, dx, dy); }
    void up(const Pointer&) override { g_.up(); }
    void dbl(const Pointer& p) override;
    void wheel(const Pointer& p, float n) override { g_.wheel(p, n); }
    void context(const Pointer& p) override { g_.context(p); }
    void move(const Pointer& p) override { zone_ = zoneAt(p.x, p.y); }
    void leave() override { zone_ = kNone; }
    DGL_NAMESPACE::MouseCursor cursor(const Pointer& p) const override;
    std::string hint() const override;
private:
    enum { kNone = -3, kKnob = -2, kValue = -1 };   // or >= 0: the rate index of a label
    int   zoneAt(float x, float y) const;
    float cx() const { return r.x + 54.f; }
    float cy() const { return r.y + 50.f; }
    ParamGesture g_;
    int zone_ = kNone;
};

class GridDropdown : public Widget {
public:
    GridDropdown(Services& s, RectF rect);
    void paint(Gfx& g) override;
    void down(const Pointer& p) override;
    void wheel(const Pointer&, float n) override;
    DGL_NAMESPACE::MouseCursor cursor(const Pointer&) const override { return DGL_NAMESPACE::kMouseCursorHand; }
    std::string hint() const override { return "Grid resolution \xC2\xB7 wheel to step"; }
};

class MsSlider : public Widget {
public:
    MsSlider(Services& s, RectF rect);
    void paint(Gfx& g) override;
    void down(const Pointer& p) override;
    void drag(const Pointer& p, float dx, float dy) override;
    void up(const Pointer&) override;
    void dbl(const Pointer&) override;
    void wheel(const Pointer& p, float n) override { g_.wheel(p, n); }
    void context(const Pointer& p) override { g_.context(p); }
    DGL_NAMESPACE::MouseCursor cursor(const Pointer&) const override { return DGL_NAMESPACE::kMouseCursorLeftRight; }
    std::string hint() const override { return g_.hint(); }
private:
    ParamGesture g_;
    bool active_ = false;
};

class ThresholdMeter : public Widget {
public:
    ThresholdMeter(Services& s, RectF rect, bool compact);
    void paint(Gfx& g) override;
    void down(const Pointer& p) override;
    void drag(const Pointer& p, float dx, float dy) override;
    void up(const Pointer&) override;
    void dbl(const Pointer&) override;
    void wheel(const Pointer& p, float n) override;
    void context(const Pointer& p) override { sv.paramMenu(kParamThreshold, p.x, p.y); }
    DGL_NAMESPACE::MouseCursor cursor(const Pointer&) const override { return DGL_NAMESPACE::kMouseCursorLeftRight; }
    std::string hint() const override;
private:
    float dbToX(float db) const;
    bool compact_;
    bool active_ = false;
};

class FilterGraph : public Widget {
public:
    FilterGraph(Services& s, RectF rect);
    void paint(Gfx& g) override;
    void down(const Pointer& p) override;
    void drag(const Pointer& p, float dx, float dy) override;
    void up(const Pointer&) override;
    void dbl(const Pointer& p) override;
    void move(const Pointer& p) override { hoverHandle_ = handleAt(p.x); }
    void leave() override { hoverHandle_ = -1; }
    DGL_NAMESPACE::MouseCursor cursor(const Pointer& p) const override;
    std::string hint() const override;
private:
    float f2x(float f) const;
    float x2f(float x) const;
    int   handleAt(float x) const;
    int   hoverHandle_ = -1, dragHandle_ = -1;
};

class CrossoverGraph : public Widget {
public:
    CrossoverGraph(Services& s, RectF rect);
    void paint(Gfx& g) override;
    void down(const Pointer& p) override;
    void drag(const Pointer& p, float dx, float dy) override;
    void up(const Pointer&) override;
    void dbl(const Pointer&) override;
    void wheel(const Pointer& p, float n) override;
    void context(const Pointer& p) override { sv.paramMenu(kParamCrossover, p.x, p.y); }
    DGL_NAMESPACE::MouseCursor cursor(const Pointer&) const override { return DGL_NAMESPACE::kMouseCursorLeftRight; }
    std::string hint() const override;
private:
    float f2x(float f) const;
    bool active_ = false;
    // band responses per 1.5 px column, rebuilt only when the crossover or slope changes
    std::vector<float> curveY_[2];
    float curveFc_ = -1.f, curveOrd_ = -1.f;
};

class SpectrumView : public Widget {
public:
    SpectrumView(Services& s, RectF rect);
    void paint(Gfx& g) override;
    bool interactive() const override { return true; }
    std::string hint() const override
    {
        return "Yellow bars: cut per band (a full bar = Range). Red line: the sidechain spectrum that causes it.";
    }
};

class BarView : public Widget {   // Sync mode: one host bar with the cycle tiled across it
public:
    BarView(Services& s, RectF rect);
    void paint(Gfx& g) override;
    bool interactive() const override { return false; }
    bool hits(float, float) const override { return false; }
};

class NoteStepper : public Widget {   // MIDI trigger note: ‹ C1 · 36 ›
public:
    NoteStepper(Services& s, RectF rect);
    void paint(Gfx& g) override;
    void down(const Pointer& p) override;
    void dbl(const Pointer& p) override;
    void wheel(const Pointer&, float n) override;
    void move(const Pointer& p) override { zone_ = p.x < r.x + 26.f ? 0 : p.x > r.right() - 26.f ? 2 : 1; }
    void leave() override { zone_ = -1; }
    void context(const Pointer& p) override { sv.paramMenu(kParamMidiNote, p.x, p.y); }
    std::string hint() const override { return "Trigger note: wheel or arrows to step \xC2\xB7 double-click = any note \xC2\xB7 or click a key below"; }
private:
    int zone_ = -1;
};

class Keyboard : public Widget {       // two octaves around the trigger note
public:
    Keyboard(Services& s, RectF rect);
    void paint(Gfx& g) override;
    void down(const Pointer& p) override;
    void move(const Pointer& p) override { hoverNote_ = noteAt(p.x, p.y); }
    void leave() override { hoverNote_ = -1; }
    DGL_NAMESPACE::MouseCursor cursor(const Pointer&) const override { return DGL_NAMESPACE::kMouseCursorHand; }
    std::string hint() const override;
private:
    int baseNote() const;
    int noteAt(float x, float y) const;
    int hoverNote_ = -1;
};

class GrMeter : public Widget {
public:
    GrMeter(Services& s, RectF rect);
    void paint(Gfx& g) override;
    std::string hint() const override { return "Gain reduction right now"; }
};

}} // namespace kick::ui
