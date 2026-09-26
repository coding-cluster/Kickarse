// Kickarse UI — shape library (see Library.h); ports drawLibrary() from the prototype.
#include "Library.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "EditorView.h"

namespace kick { namespace ui {

using DGL_NAMESPACE::MouseCursor;

namespace {
constexpr float kX0 = 256.f, kY0 = 464.f, kW = 612.f;
constexpr float kGy = kY0 + 30.f, kBh = 128.f, kCw = kW / 8.f, kCh = kBh / 2.f;
constexpr int   kPer = 16;
const char* const kFavTab = "Favourites";

bool sameEnvelope(const Envelope& a, const Envelope& b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i) {
        const EnvNode &x = a.node(i), &y = b.node(i);
        if (std::fabs(x.x - y.x) > 1e-3f || std::fabs(x.y - y.y) > 1e-3f || std::fabs(x.tension - y.tension) > 1e-3f)
            return false;
    }
    return true;
}

void thumbCurve(Gfx& g, const Envelope& e, float x, float y, float w, float h, const Rgba& c, float fillA)
{
    auto& vg = g.vg();
    constexpr int N = 48;
    auto pt = [&](int i) { const float q = float(i) / float(N); return std::pair<float, float>(x + q * w, y + (1.f - e.evaluateLeft(q)) * h); };
    vg.beginPath();
    for (int i = 0; i <= N; ++i) {
        const auto [px, py] = pt(i);
        if (i == 0) vg.moveTo(px, py); else vg.lineTo(px, py);
    }
    vg.lineTo(x + w, y + h);
    vg.lineTo(x, y + h);
    vg.closePath();
    vg.fillColor(Gfx::c(c.withAlpha(fillA)));
    vg.fill();
    vg.beginPath();
    for (int i = 0; i <= N; ++i) {
        const auto [px, py] = pt(i);
        if (i == 0) vg.moveTo(px, py); else vg.lineTo(px, py);
    }
    vg.strokeColor(Gfx::c(c));
    vg.strokeWidth(1.5f);
    vg.lineJoin(DGL_NAMESPACE::NanoVG::ROUND);
    vg.stroke();
}
} // namespace

// ---------------------------------------------------------------------------------------------
// ShapeBank

ShapeBank::ShapeBank(Services& s, EnvelopeEditor& editor) : Widget(s), editor_(editor)
{
    r = {kX0, kGy, kW, kBh};
}

void ShapeBank::refresh()
{
    ShapeLibrary* lib = sv.shapes();
    const std::string& tab = sv.model().view.libTab;
    items_.clear();
    if (lib != nullptr) {
        if (tab == kFavTab) {
            for (ShapeEntry& e : lib->entries())
                if (sv.settings().favouriteShapes.count(e.id))
                    items_.push_back(std::move(e));
        } else {
            items_ = lib->entries(tab);
        }
    }
    loadedTab_ = tab;
    hover_ = -1;
    editor_.setPreview(nullptr);
}

int ShapeBank::pageCount() const
{
    return std::max(1, int((items_.size() + kPer - 1) / kPer));
}

int ShapeBank::cellAt(float x, float y) const
{
    if (!r.contains(x, y))
        return -1;
    const int c = std::clamp(int((x - kX0) / kCw), 0, 7), rw = std::clamp(int((y - kGy) / kCh), 0, 1);
    const int idx = sv.model().view.libPage * kPer + rw * 8 + c;
    return idx < int(items_.size()) ? idx : -1;
}

void ShapeBank::move(const Pointer& p)
{
    const int h = cellAt(p.x, p.y);
    if (h != hover_) {
        hover_ = h;
        editor_.setPreview(h >= 0 ? &items_[std::size_t(h)].env : nullptr);
        sv.repaint();
    }
}

void ShapeBank::leave()
{
    hover_ = -1;
    editor_.setPreview(nullptr);
}

void ShapeBank::down(const Pointer& p)
{
    const int i = cellAt(p.x, p.y);
    if (i < 0)
        return;
    editor_.setPreview(nullptr);
    sv.applyShape(items_[std::size_t(i)].env);
}

void ShapeBank::wheel(const Pointer&, float n)
{
    ViewState& v = sv.model().view;
    v.libPage = std::clamp(v.libPage - (n > 0.f ? 1 : -1), 0, pageCount() - 1);
    sv.model().pushViewState();
    sv.repaint();
}

void ShapeBank::context(const Pointer& p)
{
    const int i = cellAt(p.x, p.y);
    if (i < 0)
        return;
    const ShapeEntry e = items_[std::size_t(i)];
    Settings& st = sv.settings();
    const bool fav = st.favouriteShapes.count(e.id) != 0;
    std::vector<MenuItem> items;
    MenuItem f;
    f.label = fav ? "Remove from favourites" : "Add to favourites";
    f.action = [this, id = e.id, fav] {
        Settings& s2 = sv.settings();
        if (fav) s2.favouriteShapes.erase(id); else s2.favouriteShapes.insert(id);
        s2.save();
        refresh();
    };
    items.push_back(std::move(f));
    MenuItem rn;
    rn.label = "Rename\xE2\x80\xA6";
    rn.dim = e.isFactory;
    rn.action = [this, e] {
        const int idx = int(std::find_if(items_.begin(), items_.end(), [&](const ShapeEntry& x) { return x.id == e.id; }) - items_.begin());
        const int slot = idx - sv.model().view.libPage * kPer;
        const float cx = kX0 + float(slot % 8) * kCw + kCw * 0.5f, cy = kGy + float(slot / 8) * kCh + kCh * 0.5f;
        sv.openTextEntry(e.name, cx, cy, 140.f, [this, e](const std::string& name) {
            if (name.empty() || name == e.name || sv.shapes() == nullptr)
                return;
            if (sv.shapes()->saveUser(name, e.env))
                sv.shapes()->removeUser(e.id);
            sv.shapes()->rescan();
            refresh();
        });
    };
    items.push_back(std::move(rn));
    MenuItem del;
    del.label = "Delete";
    del.dim = e.isFactory;
    del.action = [this, id = e.id] {
        if (sv.shapes() != nullptr && sv.shapes()->removeUser(id)) {
            sv.shapes()->rescan();
            refresh();
        }
    };
    items.push_back(std::move(del));
    sv.openMenu(p.x, p.y, std::move(items));
}

MouseCursor ShapeBank::cursor(const Pointer& p) const
{
    return cellAt(p.x, p.y) >= 0 ? DGL_NAMESPACE::kMouseCursorHand : DGL_NAMESPACE::kMouseCursorArrow;
}

std::string ShapeBank::hint() const
{
    if (hover_ < 0 || hover_ >= int(items_.size()))
        return {};
    return items_[std::size_t(hover_)].name + " \xE2\x80\x94 hover previews in the editor, click to apply, right-click for favourite";
}

void ShapeBank::paint(Gfx& g)
{
    Model& m = sv.model();
    if (loadedTab_ != m.view.libTab)
        refresh();
    m.view.libPage = std::clamp(m.view.libPage, 0, pageCount() - 1);
    g.well(kX0, kGy, kW, kBh, 3.f);
    for (int c = 1; c < 8; ++c)
        g.vline(std::round(kX0 + float(c) * kCw), kGy + 6.f, kGy + kBh - 6.f, col::warm.withAlpha(0.045f));
    g.hline(kX0 + 6.f, kX0 + kW - 6.f, kGy + kCh, col::warm.withAlpha(0.045f));
    const Envelope& cur = m.env(m.editedBand());
    const int first = m.view.libPage * kPer;
    for (int j = 0; j < kPer && first + j < int(items_.size()); ++j) {
        const ShapeEntry& e = items_[std::size_t(first + j)];
        const float cx = kX0 + float(j % 8) * kCw, cy = kGy + float(j / 8) * kCh;
        const float hv = hover_ == first + j ? hot : 0.f;
        const bool isCur = sameEnvelope(e.env, cur);
        if (hv > 0.01f)
            g.fillRR(cx + 3.f, cy + 3.f, kCw - 6.f, kCh - 6.f, 2.f, col::warm.withAlpha(0.04f * hv));
        const Rgba c = isCur ? col::duck : mix(col::textMute, col::textHi, hv);
        thumbCurve(g, e.env, cx + 10.f, cy + 10.f, kCw - 20.f, kCh - 20.f, c, isCur ? 0.10f : 0.035f + 0.04f * hv);
        if (isCur)
            g.fillRR(cx + 10.f, cy + kCh - 5.f, kCw - 20.f, 2.f, 1.f, col::duck);
        if (sv.settings().favouriteShapes.count(e.id))
            g.icon(Icon::StarOn, cx + kCw - 9.f, cy + 9.f, isCur ? col::duck : col::duck.withAlpha(0.7f), 0.5f);
    }
    if (items_.empty()) {
        const char* msg = m.view.libTab == kFavTab ? "Right-click a shape to add it to your favourites."
                        : m.view.libTab == "User" ? "Save shapes here with + Save shape, or capture one from the sidechain."
                                                  : "No shapes in this category.";
        g.text(msg, kX0 + kW * 0.5f, kGy + kBh * 0.5f, {11.f, Font::Sc, col::textDim, Align::Center});
    }
}

// ---------------------------------------------------------------------------------------------
// LibraryHeader: tabs (categories from the API + Favourites), pager, Save shape

LibraryHeader::LibraryHeader(Services& s, ShapeBank& bank) : Widget(s), bank_(bank)
{
    r = {kX0, kY0 - 4.f, kW, 26.f};
}

std::vector<std::string> LibraryHeader::tabs() const
{
    std::vector<std::string> t(kShapeCategories, kShapeCategories + kNumShapeCategories);
    t.emplace_back(kFavTab);
    return t;
}

int LibraryHeader::zoneAt(float x, float y) const
{
    if (y < kY0 - 4.f || y > kY0 + 22.f)
        return -1;
    for (std::size_t i = 0; i < tabX_.size(); ++i)
        if (x >= tabX_[i] - 4.f && x < tabX_[i] + tabW_[i] + 4.f)
            return int(i);
    if (x >= kX0 + kW - 150.f && x < kX0 + kW - 128.f) return 100;
    if (x >= kX0 + kW - 104.f && x < kX0 + kW - 82.f) return 101;
    if (x >= kX0 + kW - 76.f && x <= kX0 + kW) return 102;
    return -1;
}

MouseCursor LibraryHeader::cursor(const Pointer& p) const
{
    return zoneAt(p.x, p.y) >= 0 ? DGL_NAMESPACE::kMouseCursorHand : DGL_NAMESPACE::kMouseCursorArrow;
}

std::string LibraryHeader::hint() const
{
    if (hoverZone_ == 100) return "Previous page";
    if (hoverZone_ == 101) return "Next page";
    if (hoverZone_ == 102) return "Save the current envelope as a User shape";
    const auto t = tabs();
    if (hoverZone_ >= 0 && hoverZone_ < int(t.size())) return t[std::size_t(hoverZone_)] + " shapes";
    return {};
}

void LibraryHeader::down(const Pointer& p)
{
    Model& m = sv.model();
    const int z = zoneAt(p.x, p.y);
    const auto t = tabs();
    if (z >= 0 && z < int(t.size())) {
        m.view.libTab = t[std::size_t(z)];
        m.view.libPage = 0;
        bank_.refresh();
    } else if (z == 100) {
        m.view.libPage = std::max(0, m.view.libPage - 1);
    } else if (z == 101) {
        m.view.libPage = std::min(bank_.pageCount() - 1, m.view.libPage + 1);
    } else if (z == 102) {
        const int n = sv.shapes() ? int(sv.shapes()->entries("User").size()) + 1 : 1;
        char name[32];
        std::snprintf(name, sizeof(name), "Shape %d", n);
        sv.openTextEntry(name, kX0 + kW - 60.f, kY0 + 10.f, 150.f, [this](const std::string& nm) {
            ShapeLibrary* lib = sv.shapes();
            if (lib == nullptr || nm.empty())
                return;
            Model& mm = sv.model();
            if (lib->saveUser(nm, mm.env(mm.editedBand()))) {
                lib->rescan();
                mm.view.libTab = "User";
                mm.view.libPage = 0;
                bank_.refresh();
                mm.pushViewState();
            }
        });
        return;
    } else {
        return;
    }
    m.pushViewState();
    sv.repaint();
}

void LibraryHeader::paint(Gfx& g)
{
    const Model& m = sv.model();
    const auto t = tabs();
    tabX_.assign(t.size(), 0.f);
    tabW_.assign(t.size(), 0.f);
    float tx = kX0;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const bool fav = t[i] == kFavTab, sel = m.view.libTab == t[i];
        const float tw = g.measure(t[i].c_str(), 11.5f, Font::ScSemi) + (fav ? 16.f : 0.f);
        tabX_[i] = tx;
        tabW_[i] = tw;
        const float hv = hoverZone_ == int(i) ? hot : 0.f;
        if (fav)
            g.icon(Icon::StarOn, tx + 5.f, kY0 + 10.f, sel ? col::duck : col::textDim, 0.75f);
        g.text(t[i].c_str(), tx + (fav ? 14.f : 0.f), kY0 + 10.5f, {11.5f, Font::ScSemi, sel ? col::textHi : mix(col::textDim, col::textMute, hv), Align::Left});
        if (sel)
            g.fillRR(tx, kY0 + 20.f, tw, 2.f, 1.f, col::duck);
        tx += tw + 20.f;
    }
    const int pages = bank_.pageCount(), page = std::clamp(m.view.libPage, 0, pages - 1);
    const float hp = hoverZone_ == 100 ? hot : 0.f, hn = hoverZone_ == 101 ? hot : 0.f, hs = hoverZone_ == 102 ? hot : 0.f;
    g.icon(Icon::Prev, kX0 + kW - 139.f, kY0 + 10.f, page == 0 ? col::textDim : mix(col::textMute, col::textHi, hp));
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%d / %d", page + 1, pages);
    g.text(buf, kX0 + kW - 116.f, kY0 + 10.5f, {11.f, Font::Sc, col::textDim, Align::Center});
    g.icon(Icon::Next, kX0 + kW - 93.f, kY0 + 10.f, page >= pages - 1 ? col::textDim : mix(col::textMute, col::textHi, hn));
    const Rgba sc = mix(col::textMute, col::textHi, hs);
    g.icon(Icon::Plus, kX0 + kW - 66.f, kY0 + 10.f, sc, 0.75f);
    g.text("Save shape", kX0 + kW, kY0 + 10.5f, {11.f, Font::Sc, sc, Align::Right});
}

}} // namespace kick::ui
