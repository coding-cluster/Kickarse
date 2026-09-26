// Kickarse UI — pop-up overlays: list menu (DESIGN.md §6.7), rate grid (§6.8).
#pragma once

#include "Widget.h"

namespace kick { namespace ui {

class MenuOverlay : public Widget {
public:
    explicit MenuOverlay(Services& s) : Widget(s) { r = {0.f, 0.f, kBaseW, kBaseH}; }
    void open(float x, float y, std::vector<MenuItem> items, float rightEdge, bool above);
    void close() { open_ = false; }
    bool isOpen() const noexcept { return open_; }

    void paint(Gfx& g) override;
    bool hits(float, float) const override { return open_; }
    bool modal() const override { return true; }
    void down(const Pointer& p) override;
    void dbl(const Pointer& p) override { down(p); }
    void move(const Pointer& p) override { hover_ = rowAt(p.x, p.y); }
    void leave() override { hover_ = -1; }
    DGL_NAMESPACE::MouseCursor cursor(const Pointer& p) const override;

private:
    void layout(Gfx& g);
    int  rowAt(float x, float y) const;
    bool open_ = false;
    std::vector<MenuItem> items_;
    float ax_ = 0.f, ay_ = 0.f, rightEdge_ = -1.f;
    bool  above_ = false;
    RectF box_;
    std::vector<float> rowY_;   // top of each item row
    int hover_ = -1;
};

class RateGridOverlay : public Widget {
public:
    explicit RateGridOverlay(Services& s) : Widget(s) { r = {0.f, 0.f, kBaseW, kBaseH}; }
    void open(float x, float y) { x_ = x; y_ = y; open_ = true; hover_ = -1; }
    void close() { open_ = false; }
    bool isOpen() const noexcept { return open_; }

    void paint(Gfx& g) override;
    bool hits(float, float) const override { return open_; }
    bool modal() const override { return true; }
    void down(const Pointer& p) override;
    void dbl(const Pointer& p) override { down(p); }
    void move(const Pointer& p) override { hover_ = cellAt(p.x, p.y); }
    void leave() override { hover_ = -1; }
    DGL_NAMESPACE::MouseCursor cursor(const Pointer& p) const override;
    std::string hint() const override;

private:
    int cellAt(float x, float y) const;   // rate index or -1
    bool open_ = false;
    float x_ = 0.f, y_ = 0.f;
    int hover_ = -1;
};

}} // namespace kick::ui
