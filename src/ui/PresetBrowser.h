// Kickarse UI — preset browser overlay (DESIGN.md §10.2): search, filter list (All, Favourites,
// User, then the store's categories), result list with favourite stars, Save as…, Open folder.
#pragma once

#include <map>
#include <vector>

#include "shared/Presets.h"

#include "Widget.h"

namespace kick { namespace ui {

class PresetBrowser : public Widget {
public:
    explicit PresetBrowser(Services& s);

    void open();
    void close() { open_ = false; }
    bool isOpen() const noexcept { return open_; }
    // Filter used by the header's ‹ › (the browser's current filter).
    std::string neighbour(const std::string& id, int step) const;

    void paint(Gfx& g) override;
    bool hits(float, float y) const override { return open_ && y >= 44.f && y < 632.f; }
    bool modal() const override { return true; }
    void down(const Pointer& p) override;
    void dbl(const Pointer& p) override;
    void move(const Pointer& p) override { mx_ = p.x; my_ = p.y; }
    void wheel(const Pointer& p, float n) override;
    bool key(unsigned key, const Pointer& p) override;
    bool character(unsigned cp);
    DGL_NAMESPACE::MouseCursor cursor(const Pointer& p) const override;
    std::string hint() const override;

private:
    struct Row { const PresetEntry* e; };
    void rebuild();                         // filtered list for the current filter + query
    std::string modeOf(const PresetEntry& e);
    int  rowAt(float x, float y) const;
    int  filterAt(float x, float y) const;
    std::vector<std::string> filters() const;

    bool open_ = false;
    std::string filter_ = "All presets";
    std::string query_;
    std::vector<const PresetEntry*> rows_;
    std::map<std::string, std::string> modeCache_;
    int scroll_ = 0;
    float mx_ = -1.f, my_ = -1.f;
};

}} // namespace kick::ui
