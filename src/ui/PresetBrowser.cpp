// Kickarse UI — preset browser overlay (see PresetBrowser.h); ports drawPresetBrowser().
#include "PresetBrowser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include "shared/UserData.h"

namespace kick { namespace ui {

using DGL_NAMESPACE::MouseCursor;

namespace {
constexpr float kX = 248.f, kY = 44.f, kW = 828.f, kH = 588.f;
constexpr float kLx = kX + 220.f, kLw = kW - 236.f, kRowH = 26.f, kListY = kY + 80.f;
constexpr int   kVisibleRows = 17;

std::string lower(std::string s)
{
    for (char& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void openFolder(const std::string& utf8)
{
#ifdef _WIN32
    UserData::ensureDir(utf8);
    const std::wstring w = UserData::toPath(utf8).wstring();
    ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    (void)utf8;
#endif
}
} // namespace

PresetBrowser::PresetBrowser(Services& s) : Widget(s)
{
    r = {0.f, 44.f, kBaseW, 588.f};
}

void PresetBrowser::open()
{
    open_ = true;
    query_.clear();
    scroll_ = 0;
    if (sv.presets() != nullptr)
        sv.presets()->rescan();
    modeCache_.clear();
    rebuild();
    // scroll the loaded preset into view
    const std::string& cur = sv.model().presetId;
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i]->id == cur)
            scroll_ = std::max(0, int(i) - kVisibleRows / 2);
}

std::vector<std::string> PresetBrowser::filters() const
{
    std::vector<std::string> f {"All presets", "Favourites", "User", ""};
    if (sv.presets() != nullptr)
        for (const std::string& c : sv.presets()->categories())
            f.push_back(c);
    return f;
}

void PresetBrowser::rebuild()
{
    rows_.clear();
    PresetStore* st = sv.presets();
    if (st == nullptr)
        return;
    const std::string q = lower(query_);
    for (const PresetEntry& e : st->entries()) {
        bool ok = filter_ == "All presets" || (filter_ == "Favourites" && e.isFavourite) || (filter_ == "User" && !e.isFactory)
               || e.category == filter_;
        if (ok && !q.empty())
            ok = lower(e.name).find(q) != std::string::npos || lower(e.category).find(q) != std::string::npos;
        if (ok)
            rows_.push_back(&e);
    }
    scroll_ = std::clamp(scroll_, 0, std::max(0, int(rows_.size()) - kVisibleRows));
}

std::string PresetBrowser::neighbour(const std::string& id, int step) const
{
    std::vector<const PresetEntry*> list;
    if (open_) {
        list = rows_;
    } else if (sv.presets() != nullptr) {
        for (const PresetEntry& e : sv.presets()->entries())
            list.push_back(&e);
    }
    if (list.empty())
        return id;
    int idx = -1;
    for (std::size_t i = 0; i < list.size(); ++i)
        if (list[i]->id == id)
            idx = int(i);
    if (idx < 0)
        return list[step > 0 ? 0 : list.size() - 1]->id;
    const int n = int(list.size());
    return list[std::size_t(((idx + step) % n + n) % n)]->id;
}

std::string PresetBrowser::modeOf(const PresetEntry& e)
{
    auto it = modeCache_.find(e.id);
    if (it != modeCache_.end())
        return it->second;
    std::string s;
    PresetData d;
    if (sv.presets() != nullptr && sv.presets()->load(e.id, d)) {
        const int mode = std::clamp(int(std::lround(d.params[kParamMode])), 0, kModeCount - 1);
        static const char* const names[] = {"Sync", "MIDI", "Audio", "Spectral", "Ring"};
        s = names[mode];
        if (d.params[kParamMulti] > 0.5f)
            s += " \xC2\xB7 Split";
    }
    modeCache_[e.id] = s;
    return s;
}

int PresetBrowser::rowAt(float x, float y) const
{
    if (x < kLx || x > kLx + kLw || y < kListY)
        return -1;
    const int j = int((y - kListY) / kRowH);
    if (j >= kVisibleRows)
        return -1;
    const int i = scroll_ + j;
    return i < int(rows_.size()) ? i : -1;
}

int PresetBrowser::filterAt(float x, float y) const
{
    if (x < kX + 16.f || x > kX + 188.f)
        return -1;
    float cy = kY + 60.f;
    const auto f = filters();
    for (std::size_t i = 0; i < f.size(); ++i) {
        if (f[i].empty()) { cy += 14.f; continue; }
        if (y >= cy && y < cy + 24.f)
            return int(i);
        cy += 26.f;
    }
    return -1;
}

MouseCursor PresetBrowser::cursor(const Pointer& p) const
{
    return rowAt(p.x, p.y) >= 0 || filterAt(p.x, p.y) >= 0 ? DGL_NAMESPACE::kMouseCursorHand : DGL_NAMESPACE::kMouseCursorArrow;
}

std::string PresetBrowser::hint() const
{
    return "Click to load \xC2\xB7 double-click to load and close \xC2\xB7 star to favourite \xC2\xB7 type to search \xC2\xB7 \xE2\x86\x91/\xE2\x86\x93 to step";
}

void PresetBrowser::down(const Pointer& p)
{
    // panel chrome
    if (p.x < kX || p.x > kX + kW) { close(); sv.repaint(); return; }
    if (p.x >= kX + kW - 40.f && p.x < kX + kW - 12.f && p.y >= kY + 16.f && p.y < kY + 44.f) { close(); sv.repaint(); return; }
    if (p.y >= kY + kH - 44.f && p.y < kY + kH - 16.f) {
        if (p.x >= kX + kW - 236.f && p.x < kX + kW - 136.f) { sv.savePreset(true); return; }
        if (p.x >= kX + kW - 128.f && p.x < kX + kW - 16.f) { openFolder(UserData::presetsDir()); return; }
    }
    const int f = filterAt(p.x, p.y);
    if (f >= 0) {
        filter_ = filters()[std::size_t(f)];
        scroll_ = 0;
        rebuild();
        sv.repaint();
        return;
    }
    const int i = rowAt(p.x, p.y);
    if (i < 0)
        return;
    const PresetEntry* e = rows_[std::size_t(i)];
    if (p.x < kLx + 28.f) {
        sv.presets()->setFavourite(e->id, !e->isFavourite);
        const std::string keep = e->id;
        rebuild();
        (void)keep;
        sv.repaint();
        return;
    }
    sv.loadPreset(e->id);
    rebuild();
}

void PresetBrowser::dbl(const Pointer& p)
{
    const int i = rowAt(p.x, p.y);
    if (i >= 0 && p.x >= kLx + 28.f) {
        close();
        sv.repaint();
        return;
    }
    down(p);
}

void PresetBrowser::wheel(const Pointer&, float n)
{
    scroll_ = std::clamp(scroll_ - int(n > 0.f ? 3 : -3), 0, std::max(0, int(rows_.size()) - kVisibleRows));
    sv.repaint();
}

bool PresetBrowser::key(unsigned key, const Pointer&)
{
    using namespace DGL_NAMESPACE;
    if (key == kKeyUp || key == kKeyDown) {
        if (rows_.empty())
            return true;
        const std::string next = neighbour(sv.model().presetId, key == kKeyDown ? 1 : -1);
        sv.loadPreset(next);
        rebuild();
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i]->id == next) {
                if (int(i) < scroll_) scroll_ = int(i);
                if (int(i) >= scroll_ + kVisibleRows) scroll_ = int(i) - kVisibleRows + 1;
            }
        return true;
    }
    if (key == kKeyEnter || key == kKeyEscape) {
        close();
        return true;
    }
    if (key == kKeyBackspace) {
        if (!query_.empty()) {
            query_.pop_back();
            rebuild();
        }
        return true;
    }
    return false;
}

bool PresetBrowser::character(unsigned cp)
{
    if (cp < 32 || cp == 127 || cp > 126)
        return false;
    query_.push_back(char(cp));
    scroll_ = 0;
    rebuild();
    return true;
}

void PresetBrowser::paint(Gfx& g)
{
    if (!open_)
        return;
    rebuild();
    const Model& m = sv.model();
    g.dropShadow(kX, kY + 2.f, kW, kH - 6.f, 4.f, 24.f, 0.55f, 8.f);
    g.fillRR(kX, kY + 2.f, kW, kH - 6.f, 4.f, col::ink2);
    g.strokeRR(kX + 0.5f, kY + 2.5f, kW - 1.f, kH - 7.f, 4.f, col::ink6);
    // search
    g.well(kX + 16.f, kY + 16.f, 300.f, 28.f, 3.f);
    g.icon(Icon::Search, kX + 32.f, kY + 30.f, col::textDim, 0.85f);
    const bool hasQ = !query_.empty();
    const float qw = g.text(hasQ ? query_.c_str() : "Search presets", kX + 46.f, kY + 30.5f, {11.f, Font::Sc, hasQ ? col::textHi : col::textDim, Align::Left});
    if (std::fmod(sv.now(), 1.0) < 0.56)
        g.line(kX + 46.f + (hasQ ? qw + 1.5f : 0.f), kY + 24.f, kX + 46.f + (hasQ ? qw + 1.5f : 0.f), kY + 37.f, col::duck, 1.2f);
    const bool closeHot = mx_ >= kX + kW - 40.f && mx_ < kX + kW - 12.f && my_ >= kY + 16.f && my_ < kY + 44.f;
    if (closeHot) g.fillRR(kX + kW - 40.f, kY + 16.f, 28.f, 28.f, 3.f, col::warm.withAlpha(0.05f));
    g.icon(Icon::Close, kX + kW - 26.f, kY + 30.f, closeHot ? col::textHi : col::textMute);
    // filters
    PresetStore* st = sv.presets();
    float cy = kY + 60.f;
    const auto fl = filters();
    for (std::size_t i = 0; i < fl.size(); ++i) {
        const std::string& c = fl[i];
        if (c.empty()) {
            g.hline(kX + 24.f, kX + 180.f, cy + 6.f, col::ink5);
            cy += 14.f;
            continue;
        }
        const bool sel = filter_ == c;
        const bool hv = filterAt(mx_, my_) == int(i);
        if (sel) g.fillRR(kX + 16.f, cy, 172.f, 24.f, 3.f, col::ink4);
        const bool fav = c == "Favourites";
        if (fav) g.icon(Icon::StarOn, kX + 30.f, cy + 12.f, sel ? col::duck : col::textDim, 0.7f);
        g.text(c.c_str(), kX + (fav ? 42.f : 28.f), cy + 12.5f, {11.f, sel ? Font::ScSemi : Font::Sc, sel ? col::textHi : (hv ? col::text : col::textMute), Align::Left});
        int cnt = 0;
        if (st != nullptr)
            for (const PresetEntry& e : st->entries())
                if (c == "All presets" || (c == "Favourites" && e.isFavourite) || (c == "User" && !e.isFactory) || e.category == c)
                    ++cnt;
        const std::string cs = std::to_string(cnt);
        g.text(cs.c_str(), kX + 180.f, cy + 12.5f, {10.5f, Font::Sc, col::textDim, Align::Right});
        cy += 26.f;
    }
    g.vline(kX + 204.f, kY + 56.f, kY + kH - 60.f, col::ink5);
    // list
    g.text("Name", kLx + 36.f, kY + 66.f, {10.5f, Font::Sc, col::textDim, Align::Left});
    g.text("Category", kLx + 300.f, kY + 66.f, {10.5f, Font::Sc, col::textDim, Align::Left});
    g.text("Mode", kLx + 440.f, kY + 66.f, {10.5f, Font::Sc, col::textDim, Align::Left});
    const int hoverRow = rowAt(mx_, my_);
    for (int j = 0; j < kVisibleRows && scroll_ + j < int(rows_.size()); ++j) {
        const PresetEntry& e = *rows_[std::size_t(scroll_ + j)];
        const float ry = kListY + float(j) * kRowH;
        const bool sel = e.id == m.presetId;
        const bool hv = hoverRow == scroll_ + j;
        if (sel) {
            g.fillRR(kLx, ry, kLw, kRowH, 3.f, col::ink4);
            g.fillRR(kLx, ry + 5.f, 2.f, 16.f, 1.f, col::duck);
        } else if (hv) {
            g.fillRR(kLx, ry, kLw, kRowH, 3.f, col::warm.withAlpha(0.04f));
        }
        g.icon(e.isFavourite ? Icon::StarOn : Icon::Star, kLx + 16.f, ry + 13.f, e.isFavourite ? col::duck : hv ? col::textMute : col::ink7, 0.75f);
        g.text(e.name.c_str(), kLx + 36.f, ry + 13.5f, {12.f, sel ? Font::ScSemi : Font::Sc, sel ? col::textHi : col::text, Align::Left});
        g.text(e.category.c_str(), kLx + 300.f, ry + 13.5f, {11.f, Font::Sc, col::textMute, Align::Left});
        const std::string md = modeOf(e);
        g.text(md.c_str(), kLx + 440.f, ry + 13.5f, {11.f, Font::Sc, col::textDim, Align::Left});
    }
    if (rows_.empty())
        g.text(filter_ == "Favourites" ? "Star a preset to find it here." : filter_ == "User" ? "Your saved presets appear here." : "No presets match.",
               kLx + kLw * 0.5f, kListY + 60.f, {11.f, Font::Sc, col::textDim, Align::Center});
    if (int(rows_.size()) > kVisibleRows) {   // scroll indicator
        const float trackH = kVisibleRows * kRowH, frac = float(kVisibleRows) / float(rows_.size());
        const float y0 = kListY + trackH * float(scroll_) / float(rows_.size());
        g.fillRR(kLx + kLw + 4.f, y0, 3.f, trackH * frac, 1.5f, col::ink6);
    }
    // footer
    g.hline(kX + 16.f, kX + kW - 16.f, kY + kH - 57.f, col::ink5);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%d presets \xC2\xB7 \xE2\x86\x91/\xE2\x86\x93 to step \xC2\xB7 Enter to load", int(rows_.size()));
    g.text(buf, kX + 24.f, kY + kH - 30.f, {11.f, Font::Sc, col::textDim, Align::Left});
    const bool saveHot = my_ >= kY + kH - 44.f && my_ < kY + kH - 16.f && mx_ >= kX + kW - 236.f && mx_ < kX + kW - 136.f;
    const bool foldHot = my_ >= kY + kH - 44.f && my_ < kY + kH - 16.f && mx_ >= kX + kW - 128.f && mx_ < kX + kW - 16.f;
    g.raised(kX + kW - 236.f, kY + kH - 44.f, 100.f, 28.f, 3.f, col::ink4, saveHot ? 1.f : 0.f);
    g.text("Save as\xE2\x80\xA6", kX + kW - 186.f, kY + kH - 29.5f, {11.f, Font::Sc, saveHot ? col::textHi : col::text, Align::Center});
    g.raised(kX + kW - 128.f, kY + kH - 44.f, 112.f, 28.f, 3.f, col::ink4, foldHot ? 1.f : 0.f);
    g.icon(Icon::Folder, kX + kW - 106.f, kY + kH - 30.f, foldHot ? col::textHi : col::textMute, 0.85f);
    g.text("Open folder", kX + kW - 94.f, kY + kH - 29.5f, {11.f, Font::Sc, foldHot ? col::textHi : col::text, Align::Left});
}

}} // namespace kick::ui
