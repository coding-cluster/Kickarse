// Kickarse UI — shape library under the editor (DESIGN.md §10.3): category tabs read from the
// ShapeLibrary API (+ Favourites), pager, "Save shape", and the 8 x 2 shape bank.
#pragma once

#include <vector>

#include "shared/Shapes.h"

#include "Widget.h"

namespace kick { namespace ui {

class EnvelopeEditor;

class ShapeBank : public Widget {
public:
    ShapeBank(Services& s, EnvelopeEditor& editor);

    void refresh();                       // re-read the current tab from the ShapeLibrary
    int  pageCount() const;
    const std::vector<ShapeEntry>& items() const noexcept { return items_; }

    void paint(Gfx& g) override;
    void down(const Pointer& p) override;
    void dbl(const Pointer& p) override { down(p); }
    void move(const Pointer& p) override;
    void leave() override;
    void wheel(const Pointer&, float n) override;
    void context(const Pointer& p) override;
    DGL_NAMESPACE::MouseCursor cursor(const Pointer& p) const override;
    std::string hint() const override;

private:
    int cellAt(float x, float y) const;   // index into items_ or -1
    EnvelopeEditor& editor_;
    std::vector<ShapeEntry> items_;
    std::string loadedTab_;
    std::uint32_t loadedRev_ = 0;
    int hover_ = -1;
};

class LibraryHeader : public Widget {
public:
    LibraryHeader(Services& s, ShapeBank& bank);
    void paint(Gfx& g) override;
    void down(const Pointer& p) override;
    void dbl(const Pointer& p) override { down(p); }
    void move(const Pointer& p) override { hoverZone_ = zoneAt(p.x, p.y); }
    void leave() override { hoverZone_ = -1; }
    DGL_NAMESPACE::MouseCursor cursor(const Pointer& p) const override;
    std::string hint() const override;
    std::vector<std::string> tabs() const;
private:
    int zoneAt(float x, float y) const;   // 0..n-1 tabs, 100 prev, 101 next, 102 save, -1 none
    ShapeBank& bank_;
    std::vector<float> tabX_, tabW_;
    int hoverZone_ = -1;
};

}} // namespace kick::ui
