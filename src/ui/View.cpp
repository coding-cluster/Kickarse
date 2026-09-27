// Kickarse UI — root view (see View.h): layout of every panel (DESIGN.md §2.2), input routing,
// overlays, Bridge polling and the host plumbing for presets, shapes and capture.
#include "View.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "shared/Bridge.h"
#include "shared/UserData.h"

#include "Displays.h"
#include "EditorLocal.h"
#include "EditorView.h"
#include "Format.h"
#include "Library.h"
#include "Overlays.h"
#include "PresetBrowser.h"
#include "WordmarkData.h"

namespace kick { namespace ui {

using DGL_NAMESPACE::MouseCursor;
using NVG = DGL_NAMESPACE::NanoVG;

namespace {

constexpr float kLX = layout::leftX, kLW = layout::leftW, kRX = layout::rightX, kRW = layout::rightW;

// A widget that is just a clickable area with a paint function.
class Clickable : public Widget {
public:
    Clickable(Services& s, RectF rect, std::function<void(Gfx&, Clickable&)> paintFn, std::function<void(const Pointer&)> click,
              std::string hint = {}, MouseCursor cur = DGL_NAMESPACE::kMouseCursorHand)
        : Widget(s), paint_(std::move(paintFn)), click_(std::move(click)), hint_(std::move(hint)), cur_(cur)
    {
        r = rect;
    }
    void paint(Gfx& g) override { if (paint_) paint_(g, *this); }
    void down(const Pointer& p) override { if (click_) click_(p); }
    void dbl(const Pointer& p) override { if (click_) click_(p); }
    MouseCursor cursor(const Pointer&) const override { return cur_; }
    std::string hint() const override { return hintFn ? hintFn() : hint_; }
    std::function<std::string()> hintFn;
    std::function<void(const Pointer&, float, float)> onDrag;
    void drag(const Pointer& p, float dx, float dy) override { if (onDrag) onDrag(p, dx, dy); }
private:
    std::function<void(Gfx&, Clickable&)> paint_;
    std::function<void(const Pointer&)> click_;
    std::string hint_;
    MouseCursor cur_;
};

void title(Gfx& g, const char* s, float x, float y)
{
    g.text(s, x, y, {11.5f, Font::ScSemi, col::textMute, Align::Left});
}

void caption(Gfx& g, const char* s, float x, float y, Align a = Align::Left, float size = 11.f)
{
    g.text(s, x, y, {size, Font::Sc, col::textDim, a});
}

std::string fmtMs(float ms)
{
    char buf[32];
    if (ms < 1000.f) std::snprintf(buf, sizeof(buf), "%.0f ms", double(ms));
    else std::snprintf(buf, sizeof(buf), "%.2f s", double(ms / 1000.f));
    return buf;
}

// Text-entry caret movement over UTF-8: by code point, and by word the way Windows edit fields do
// it (Ctrl+Right lands at the start of the next word, Ctrl+Left at the start of this/previous one).
bool utf8Cont(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

std::size_t prevCp(const std::string& s, std::size_t i)
{
    if (i == 0) return 0;
    do --i; while (i > 0 && utf8Cont(s[i]));
    return i;
}

std::size_t nextCp(const std::string& s, std::size_t i)
{
    if (i >= s.size()) return s.size();
    do ++i; while (i < s.size() && utf8Cont(s[i]));
    return i;
}

int charClass(char c)   // 0 space, 1 word (letters, digits, anything non-ASCII), 2 punctuation
{
    const unsigned char u = static_cast<unsigned char>(c);
    if (u == ' ' || u == '\t') return 0;
    return (u >= 0x80 || std::isalnum(u)) ? 1 : 2;
}

std::size_t nextWord(const std::string& s, std::size_t i)
{
    if (i < s.size()) {
        const int c = charClass(s[i]);
        while (i < s.size() && charClass(s[i]) == c) ++i;   // continuation bytes are class 1 too
    }
    while (i < s.size() && charClass(s[i]) == 0) ++i;
    return i;
}

std::size_t prevWord(const std::string& s, std::size_t i)
{
    while (i > 0 && charClass(s[i - 1]) == 0) --i;
    if (i > 0) {
        const int c = charClass(s[i - 1]);
        while (i > 0 && charClass(s[i - 1]) == c) --i;
    }
    return i;
}

} // namespace

// ---------------------------------------------------------------------------------------------

View::View(HostIO& host, WindowHost& win, NVG& vg)
    : host_(host), win_(win), gfx_(vg), model_(host)
{
    settings_.load();
    demoMode_ = std::getenv("KICKARSE_UI_DEMO") != nullptr;
    presets_.rescan();
    shapes_.rescan();
}

View::~View() = default;

void View::setEditorBackend(EditorBackend* b)
{
    externalBackend_ = b;
}

const DGL_NAMESPACE::NanoImage& View::image(Img i) const
{
    return i == Img::KnobHero ? res_.knobHero : i == Img::KnobSmall ? res_.knobSmall : res_.grain;
}

void View::init()
{
    gfx_.setFonts(res_.sc, res_.scSemi, res_.exp);
    build();
    if (demoMode_) {
        for (int i = 0; i < 160; ++i)
            demo_.step(model_, live_, 1.0 / 60.0, -1.f);
    }
}

// ---------------------------------------------------------------------------------------------
// layout

void View::build()
{
    buildHeader();
    buildLeft();
    buildModePanels();
    buildCentre();
    buildRight();
    buildFooter();
    buildOverlays();
    model_.onBulkChange = [this] { repaint(); };
}

void View::buildHeader()
{
    // wordmark (paths) — the logo opens the About menu
    add<Clickable>(*this, RectF {12.f, 8.f, 116.f, 30.f},
        [](Gfx& g, Clickable&) {
            auto& vg = g.vg();
            const float size = 21.f;
            vg.save();
            vg.translate(16.f - wordmark::kBBoxMinX * size, 27.f);
            vg.scale(size, size);
            // One path for every contour: NanoVG only cuts a hole (the counters of 'a' and 'e') out of
            // the sub-paths filled together with it.
            vg.beginPath();
            for (int c = 0; c < wordmark::kNumContours; ++c) {
                const auto& ct = wordmark::kContours[c];
                const float* p = nullptr;
                int coord = 0;
                for (int k = 0; k < ct.firstOp; ++k)
                    coord += wordmark::kOps[k] == 0 || wordmark::kOps[k] == 1 ? 2 : wordmark::kOps[k] == 2 ? 4 : 0;
                p = wordmark::kCoords + coord;
                for (int k = ct.firstOp; k < ct.firstOp + ct.opCount; ++k) {
                    switch (wordmark::kOps[k]) {
                    case 0: vg.moveTo(p[0], p[1]); p += 2; break;
                    case 1: vg.lineTo(p[0], p[1]); p += 2; break;
                    case 2: vg.quadTo(p[0], p[1], p[2], p[3]); p += 4; break;
                    default: vg.closePath(); break;
                    }
                }
                vg.pathWinding(ct.hole ? NVG::CW : NVG::CCW);
            }
            vg.fillColor(Gfx::c(col::textHi));
            vg.fill();
            vg.restore();
        },
        [this](const Pointer&) {
            std::vector<MenuItem> items;
            items.push_back(MenuItem::head("Kickarse 1.0"));
            items.push_back(MenuItem::head("Sidechain, drawn by hand."));
            items.push_back(MenuItem::sep());
            MenuItem f; f.label = "Open user folder\xE2\x80\xA6";
            f.action = [] {
#ifdef _WIN32
                std::system(("explorer \"" + UserData::root() + "\"").c_str());
#endif
            };
            items.push_back(std::move(f));
            openMenu(16.f, 40.f, std::move(items));
        },
        "About Kickarse");

    // preset bar
    add<Decor>(*this, [](Gfx& g) {
        g.well(256.f, 8.f, 460.f, 28.f, 3.f);
        g.vline(290.f, 14.f, 30.f, col::ink5);
        g.vline(646.f, 14.f, 30.f, col::ink5);
    });
    auto* browse = add<IconButton>(*this, Icon::Browse, RectF {258.f, 10.f, 30.f, 24.f}, [this] { openPresetBrowser(!presetBrowserOpen()); }, "Browse presets");
    browse->isOn = [this] { return presetBrowserOpen(); };
    add<IconButton>(*this, Icon::Prev, RectF {292.f, 10.f, 24.f, 24.f}, [this] {
        const std::string id = browser_->neighbour(model_.presetId, -1);
        if (!id.empty()) loadPreset(id);
    }, "Previous preset");
    add<Clickable>(*this, RectF {318.f, 8.f, 300.f, 28.f},
        [this](Gfx& g, Clickable& self) {
            const std::string cat = model_.presetCategory.empty() ? std::string() : model_.presetCategory + "  /  ";
            const float catW = g.measure(cat.c_str(), 11.5f);
            const float nameW = g.measure(model_.presetName.c_str(), 12.5f, Font::ScSemi) + (model_.dirty ? 9.f : 0.f);
            const float sx = 468.f - (catW + nameW) * 0.5f;
            if (!cat.empty())
                g.text(cat.c_str(), sx, 22.5f, {11.5f, Font::Sc, col::textDim, Align::Left});
            g.text(model_.presetName.c_str(), sx + catW, 22.5f, {12.5f, Font::ScSemi, mix(col::text, col::textHi, 0.5f + self.hot * 0.5f), Align::Left});
            if (model_.dirty)
                g.text("*", sx + catW + nameW - 6.f, 21.f, {13.f, Font::ScSemi, col::duck, Align::Left});
        },
        [this](const Pointer&) { openPresetBrowser(true); }, "Click to browse \xC2\xB7 \xE2\x86\x91/\xE2\x86\x93 or \xE2\x80\xB9 \xE2\x80\xBA to step");
    add<IconButton>(*this, Icon::Next, RectF {620.f, 10.f, 24.f, 24.f}, [this] {
        const std::string id = browser_->neighbour(model_.presetId, 1);
        if (!id.empty()) loadPreset(id);
    }, "Next preset");
    auto* star = add<IconButton>(*this, Icon::Star, RectF {648.f, 10.f, 32.f, 24.f}, [this] {
        if (!model_.presetId.empty())
            presets_.setFavourite(model_.presetId, !presets_.isFavourite(model_.presetId));
    });
    star->iconFn = [this] { return presets_.isFavourite(model_.presetId) ? Icon::StarOn : Icon::Star; };
    star->isOn = [this] { return presets_.isFavourite(model_.presetId); };
    star->isDisabled = [this] { return model_.presetId.empty(); };
    star->hintFn = [this] { return std::string(presets_.isFavourite(model_.presetId) ? "Remove from favourites" : "Add to favourites"); };
    add<IconButton>(*this, Icon::Save, RectF {682.f, 10.f, 32.f, 24.f}, [this] { savePreset(false); }, "Save preset (Ctrl+S)");
    add<Decor>(*this, [this](Gfx& g) {
        if (savedFlash_ > 0.f)
            g.text(saveMsg_.c_str(), 726.f, 22.f, {11.f, Font::Sc, (saveMsg_ == "Saved" ? col::duck : col::kick).withAlpha(std::min(1.f, savedFlash_ * 2.f)), Align::Left});
    });
    auto* undo = add<IconButton>(*this, Icon::Undo, RectF {736.f, 10.f, 28.f, 24.f}, [this] { model_.undo(); }, "Undo (Ctrl+Z)");
    undo->isDisabled = [this] { return !model_.canUndo(); };
    auto* redo = add<IconButton>(*this, Icon::Redo, RectF {766.f, 10.f, 28.f, 24.f}, [this] { model_.redo(); }, "Redo (Ctrl+Shift+Z)");
    redo->isDisabled = [this] { return !model_.canRedo(); };
    auto* more = add<IconButton>(*this, Icon::More, RectF {986.f, 10.f, 28.f, 24.f}, [this] {
        openMenu(986.f, 38.f, settingsMenu(false), 1064.f);
    }, "Settings: UI size, note names, tooltips");
    more->isOn = [this] { return menu_ && menu_->isOpen(); };
    // bypass key: lamp lit = engaged
    add<Clickable>(*this, RectF {1020.f, 8.f, 48.f, 28.f},
        [this](Gfx& g, Clickable& self) {
            const bool byp = model_.on(kParamBypass);
            g.raised(1020.f, 8.f, 48.f, 28.f, 3.f, col::ink4, self.hot);
            g.lamp(1032.f, 22.f, byp ? 0.f : 1.f, col::duck, 5.f);
            g.icon(Icon::Power, 1052.f, 22.f, byp ? col::textMute : col::textHi, 0.95f);
        },
        [this](const Pointer&) { model_.toggle(kParamBypass); })->hintFn = [this] {
            return std::string(model_.on(kParamBypass) ? "Bypassed \xE2\x80\x94 click to engage" : "Engaged \xE2\x80\x94 click to bypass");
        };
}

void View::buildLeft()
{
    add<Decor>(*this, [](Gfx& g) { title(g, "Time", kLX, 64.f); });
    add<Segmented>(*this, RectF {140.f, 54.f, 88.f, 20.f}, std::vector<std::string> {"Note", "ms"},
                   [this] { return model_.ivalue(kParamTimeMode); }, [this](int i) { model_.setOnce(kParamTimeMode, float(i)); }, true)
        ->hints = {"Cycle length as a note value, locked to the tempo", "Cycle length in milliseconds"};
    auto* rate = add<RateStepper>(*this, RectF {kLX, 76.f, kLW, 38.f});
    rate->visibleIf = [this] { return model_.ivalue(kParamTimeMode) == kTimeSync; };
    auto* len = add<ValueField>(*this, kParamLengthMs, RectF {kLX, 76.f, kLW, 38.f}, "Length");
    len->visibleIf = [this] { return model_.ivalue(kParamTimeMode) == kTimeFree; };
    add<Decor>(*this, [this](Gfx& g) {
        if (model_.ivalue(kParamTimeMode) == kTimeFree) {
            caption(g, "Free time: the grid divides one cycle.", kLX, 126.f);
            return;
        }
        const int ri = std::clamp(model_.ivalue(kParamRate), 0, kNumRates - 1);
        const float ms = float(kRates[ri].beats * 60.0 / double(std::max(1.f, live_.bpm)) * 1000.0);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s at %.0f bpm = %s", kRateLabels[ri], double(live_.bpm), fmtMs(ms).c_str());
        caption(g, buf, kLX, 126.f);
    });
    auto* play = add<Segmented>(*this, RectF {kLX, 140.f, kLW, 26.f}, std::vector<std::string> {"Loop", "One-shot"},
                                [this] { return model_.ivalue(kParamPlayMode); }, [this](int i) { model_.setOnce(kParamPlayMode, float(i)); }, true);
    play->icons = {Icon::Loop, Icon::OneShot};
    play->hints = {"Loop: repeats every cycle", "One-shot: plays once per trigger, then holds"};
    add<Decor>(*this, [this](Gfx& g) {
        g.groove(12.f, 182.f, 232.f, 182.f);
        title(g, "Trigger", kLX, 200.f);
        g.lamp(kLX + kLW - 4.f, 200.f, live_.trigFlash > 0.02f ? live_.trigFlash : 0.f, col::kick, 5.f);
        g.text("hits", kLX + kLW - 14.f, 200.5f, {10.f, Font::Sc, col::textDim, Align::Right});
    });
    auto* mode = add<Segmented>(*this, RectF {kLX, 210.f, kLW, 26.f}, std::vector<std::string> {"Sync", "MIDI", "Audio", "Spectral", "Ring"},
                                [this] { return model_.ivalue(kParamMode); }, [this](int i) { model_.setOnce(kParamMode, float(i)); });
    mode->hints = {"Sync: the cycle follows the host transport", "MIDI: each note restarts the cycle",
                   "Audio: sidechain transients restart the cycle", "Spectral: cut only the frequencies the kick occupies",
                   "Ring: the kick modulates the bass at audio rate"};
}

void View::buildModePanels()
{
    const float x = kLX, w = kLW, y = layout::modePanelY;
    auto modeIs = [this](int m) { return [this, m] { return model_.ivalue(kParamMode) == m; }; };

    // ---- Sync
    add<Decor>(*this, [this, x, w, y](Gfx& g) {
        if (model_.ivalue(kParamMode) != kModeSync) return;
        g.text("Locked to the host transport.", x, y + 6.f, {11.f, Font::Sc, col::textMute, Align::Left});
        g.lamp(x + 4.f, y + 26.f, live_.playing ? 1.f : 0.f, col::duck, 5.f);
        g.text(live_.playing ? "Playing" : "Stopped", x + 13.f, y + 26.5f, {11.f, Font::ScSemi, live_.playing ? col::text : col::textMute, Align::Left});
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%.1f bpm  \xC2\xB7  %d/%d", double(live_.bpm), live_.timeSigNum, live_.timeSigDen);
        g.text(buf, x + w, y + 26.5f, {11.f, Font::Sc, col::textDim, Align::Right});
        g.text("One bar", x, y + 52.f, {11.f, Font::Sc, col::textMute, Align::Left});
        const bool free = model_.ivalue(kParamTimeMode) == kTimeFree;
        const float cyc = free ? model_.value(kParamLengthMs) / 1000.f * live_.bpm / 60.f
                               : float(kRates[std::clamp(model_.ivalue(kParamRate), 0, kNumRates - 1)].beats);
        const float bar = float(std::max(1, live_.timeSigNum)) * 4.f / float(std::max(1, live_.timeSigDen));
        const float reps = bar / std::max(1e-3f, cyc);
        if (reps >= 1.f) std::snprintf(buf, sizeof(buf), "%g cycle%s", std::round(double(reps) * 100.0) / 100.0, std::fabs(reps - 1.f) < 1e-3f ? "" : "s");
        else std::snprintf(buf, sizeof(buf), "%g bars per cycle", std::round(1.0 / double(reps) * 100.0) / 100.0);
        g.text(buf, x + w, y + 52.f, {11.f, Font::Sc, col::textDim, Align::Right});
        caption(g, "Rate, under Time, sets how many fit in a bar.", x, y + 150.f);
    })->visibleIf = modeIs(kModeSync);
    add<BarView>(*this, RectF {x, y + 62.f, w, 70.f})->visibleIf = modeIs(kModeSync);

    // ---- MIDI
    add<Decor>(*this, [this, x, y](Gfx& g) {
        g.text("Note", x, y + 8.f, {11.f, Font::Sc, col::textMute, Align::Left});
        const int nv = model_.ivalue(kParamMidiNote);
        std::string cap = nv < 0 ? "Every note triggers. Pick a key to filter." : "Only " + noteName(nv, settings_.noteC3) + " triggers.";
        caption(g, cap.c_str(), x, y + 116.f);
        caption(g, "Note velocity scales", x + 62.f, y + 156.f);
        caption(g, "the depth of each duck.", x + 62.f, y + 171.f);
        g.lamp(x + 4.f, y + 230.f, live_.noteFlash > 0.02f ? live_.noteFlash : 0.f, col::kick, 5.f);
        std::string last = live_.lastNote < 0 ? std::string("No MIDI received yet")
                         : "Last note  " + noteName(live_.lastNote, settings_.noteC3) + " \xC2\xB7 " + std::to_string(live_.lastNote) + " \xC2\xB7 vel " + std::to_string(live_.lastVelocity);
        g.text(last.c_str(), x + 13.f, y + 230.5f, {11.f, Font::Sc, col::textMute, Align::Left});
    })->visibleIf = modeIs(kModeMidi);
    add<NoteStepper>(*this, RectF {x, y + 18.f, 142.f, 30.f})->visibleIf = modeIs(kModeMidi);
    add<LampButton>(*this, "Learn", RectF {x + 150.f, y + 18.f, 62.f, 30.f}, [this] { return learn_; }, [this] { learn_ = !learn_; lastNotes_ = live_.noteCount; },
                    col::textHi, "Learn: the next incoming note becomes the trigger note")->visibleIf = modeIs(kModeMidi);
    add<Keyboard>(*this, RectF {x, y + 58.f, w, 44.f})->visibleIf = modeIs(kModeMidi);
    add<Knob>(*this, kParamVelocity, x + 26.f, y + 164.f, KnobSize::Small, col::text, "Velocity")->visibleIf = modeIs(kModeMidi);

    // ---- Audio
    add<Decor>(*this, [x, y, w](Gfx& g) {
        g.text("Threshold", x, y + 6.f, {11.f, Font::Sc, col::textMute, Align::Left});
        caption(g, "Ignores new hits for", x + 62.f, y + 92.f);
        caption(g, "this long after a trigger.", x + 62.f, y + 107.f);
        g.text("Detection filter", x, y + 169.f, {11.f, Font::Sc, col::textMute, Align::Left});
        (void)w;
    })->visibleIf = modeIs(kModeAudio);
    add<ValueText>(*this, kParamThreshold, x + w, y + 6.f, Align::Right)->visibleIf = modeIs(kModeAudio);
    add<ThresholdMeter>(*this, RectF {x, y + 16.f, w, 44.f}, false)->visibleIf = modeIs(kModeAudio);
    add<Knob>(*this, kParamRetrigMs, x + 26.f, y + 100.f, KnobSize::Small, col::text, "Hold")->visibleIf = modeIs(kModeAudio);
    add<LampButton>(*this, "Filter", RectF {x + w - 128.f, y + 158.f, 56.f, 22.f}, [this] { return model_.on(kParamTrigFilter); },
                    [this] { model_.toggle(kParamTrigFilter); }, col::kick, "Filter the sidechain before detection (not the audio you hear)")
        ->visibleIf = modeIs(kModeAudio);
    add<LampButton>(*this, "Listen", RectF {x + w - 68.f, y + 158.f, 68.f, 22.f}, [this] { return model_.on(kParamTrigListen); },
                    [this] { model_.toggle(kParamTrigListen); }, col::kick, "Listen to the filtered detection signal", true, Icon::Phones)
        ->visibleIf = modeIs(kModeAudio);
    add<FilterGraph>(*this, RectF {x, y + 188.f, w, 64.f})->visibleIf = modeIs(kModeAudio);
    add<Decor>(*this, [this, x, y, w](Gfx& g) {
        const std::string rng = formatParam(kParamTrigLowCut, model_.value(kParamTrigLowCut)) + " \xE2\x80\x93 " + formatParam(kParamTrigHighCut, model_.value(kParamTrigHighCut));
        caption(g, rng.c_str(), x, y + 268.f);
        caption(g, "isolates the kick in a full drum bus", x + w, y + 268.f, Align::Right, 10.f);
    })->visibleIf = modeIs(kModeAudio);

    // ---- Spectral & Ring share the source row (the threshold slot is always reserved so the
    // panel never jumps when the source changes)
    auto specOrRing = [this] { const int m = model_.ivalue(kParamMode); return m == kModeSpectral || m == kModeRing; };
    auto srcAudio = [this, specOrRing] { return specOrRing() && model_.ivalue(kParamTrigSource) == kTrigAudio; };
    add<Decor>(*this, [this, x, y](Gfx& g) {
        g.text("Driven by", x, y + 6.f, {11.f, Font::Sc, col::textMute, Align::Left});
        const int src = model_.ivalue(kParamTrigSource);
        if (src != kTrigAudio)
            caption(g, src == kTrigSync ? "Cycles with the host transport." : src == kTrigMidi ? "Restarts on every MIDI note." : "Runs continuously, free of any trigger.",
                    x, y + 57.f);
    })->visibleIf = specOrRing;
    auto* src = add<Segmented>(*this, RectF {x, y + 16.f, w, 24.f}, std::vector<std::string> {"Audio", "Sync", "MIDI", "Free"},
                               [this] { return model_.ivalue(kParamTrigSource); }, [this](int i) { model_.setOnce(kParamTrigSource, float(i)); });
    src->visibleIf = specOrRing;
    src->hints = {"Transients in the sidechain restart the cycle", "The host transport drives the cycle", "MIDI notes restart the cycle",
                  "Continuous: no trigger, the cycle free-runs"};
    add<ThresholdMeter>(*this, RectF {x, y + 46.f, w - 64.f, 22.f}, true)->visibleIf = srcAudio;
    add<ValueText>(*this, kParamThreshold, x + w - 30.f, y + 57.f, Align::Center)->visibleIf = srcAudio;

    // Spectral body
    auto specOnly = modeIs(kModeSpectral);
    add<Decor>(*this, [this, x, y, w](Gfx& g) {
        g.text("Cut per band", x, y + 88.f, {11.f, Font::Sc, col::textMute, Align::Left});
        char buf[32];
        std::snprintf(buf, sizeof(buf), "up to \xE2\x88\x92%.0f dB", double(model_.value(kParamSpecRange)));
        g.text(buf, x + w, y + 88.f, {11.f, Font::Sc, col::duck.withAlpha(0.85f), Align::Right});
        g.text("Envelope shapes", x, y + 294.f, {11.f, Font::Sc, col::textMute, Align::Left});
    })->visibleIf = specOnly;
    add<SpectrumView>(*this, RectF {x, y + 96.f, w, 84.f})->visibleIf = specOnly;
    const int specIds[] = {kParamSpecAttack, kParamSpecRelease, kParamSpecRange, kParamSpecSens};
    const char* specLabs[] = {"Attack", "Release", "Range", "Sens"};
    for (int i = 0; i < 4; ++i)
        add<Knob>(*this, specIds[i], x + 26.f + float(i) * 53.3f, y + 220.f, KnobSize::Small, col::text, specLabs[i])->visibleIf = specOnly;
    auto* tgt = add<Segmented>(*this, RectF {x + 94.f, y + 282.f, w - 94.f, 24.f}, std::vector<std::string> {"Depth", "Volume"},
                               [this] { return model_.ivalue(kParamSpecTarget); }, [this](int i) { model_.setOnce(kParamSpecTarget, float(i)); }, true);
    tgt->visibleIf = specOnly;
    tgt->hints = {"The envelope scales the spectral cut over time", "The spectral stage runs fully; the envelope ducks volume after it"};

    // Ring body
    auto ringOnly = modeIs(kModeRing);
    add<Decor>(*this, [x, y](Gfx& g) {
        caption(g, "The sidechain waveform multiplies the", x, y + 88.f);
        caption(g, "main signal: the kick carves the bass", x, y + 103.f);
        caption(g, "at audio rate instead of pumping it.", x, y + 118.f);
        caption(g, "faster", x + 50.f, y + 240.f, Align::Center, 10.f);
        caption(g, "smoother", x + kLW - 50.f, y + 240.f, Align::Center, 10.f);
    })->visibleIf = ringOnly;
    add<Knob>(*this, kParamRingAttack, x + 50.f, y + 174.f, KnobSize::Small, col::text, "Attack")->visibleIf = ringOnly;
    add<Knob>(*this, kParamRingRelease, x + w - 50.f, y + 174.f, KnobSize::Small, col::text, "Release")->visibleIf = ringOnly;
}

void View::buildCentre()
{
    // toolbar: tools
    add<Clickable>(*this, RectF {256.f, 52.f, 88.f, 28.f},
        [this](Gfx& g, Clickable& self) {
            g.well(256.f, 52.f, 88.f, 28.f, 3.f);
            const Icon icons[] = {Icon::Select, Icon::Line, Icon::Pencil};
            for (int i = 0; i < 3; ++i) {
                const float x = 258.f + float(i) * 28.f;
                const bool sel = model_.view.tool == i;
                const float hv = (self.hot > 0.f && lastPtr_.x >= x && lastPtr_.x < x + 28.f && lastPtr_.y >= 52.f && lastPtr_.y < 80.f) ? self.hot : 0.f;
                if (sel) g.raised(x, 54.f, 28.f, 24.f, 2.f, col::ink5);
                g.icon(icons[i], x + 14.f, 66.f, sel ? col::textHi : mix(col::textMute, col::text, hv));
            }
        },
        [this](const Pointer& p) {
            const int i = std::clamp(int((p.x - 258.f) / 28.f), 0, 2);
            model_.view.tool = i;
            if (editorBackend_) editorBackend_->setTool(i);
            model_.pushViewState();
        })->hintFn = [this] {
            const int i = std::clamp(int((lastPtr_.x - 258.f) / 28.f), 0, 2);
            static const char* const h[] = {"Select & edit: drag nodes, drag a segment to bend, double-click to add/remove",
                                            "Line: drag to draw straight segments", "Pencil: draw freehand, simplified to nodes"};
            return std::string(h[i]);
        };
    add<GridDropdown>(*this, RectF {352.f, 52.f, 88.f, 28.f});
    add<LampButton>(*this, "Snap", RectF {446.f, 52.f, 62.f, 28.f}, [this] { return model_.view.snap; },
                    [this] { model_.view.snap = !model_.view.snap; if (onSnapChanged) onSnapChanged(); model_.pushViewState(); },
                    col::duck, "Snap nodes to the grid (hold Ctrl while dragging to invert)");
    add<ValueField>(*this, kParamSwing, RectF {514.f, 52.f, 86.f, 28.f}, "Swing");
    add<ValueField>(*this, kParamSmooth, RectF {606.f, 52.f, 96.f, 28.f}, "Smooth");
    add<Decor>(*this, [](Gfx& g) { g.text("Show", 712.f, 66.5f, {11.f, Font::Sc, col::textDim, Align::Left}); });
    {
        struct T { const char* lab; bool ViewState::*flag; Rgba col; const char* hint; };
        const T togs[] = {{"In", &ViewState::showIn, col::textHi, "Main input before processing"},
                          {"Side", &ViewState::showSide, col::kick, "Sidechain (after the detection filter)"},
                          {"Out", &ViewState::showOut, col::textHi, "Output after ducking (the removed signal while Delta is on)"}};
        float tx = 744.f;
        for (const T& t : togs) {
            const float tw = gfx_.measure(t.lab, 11.f) + 28.f;
            auto flag = t.flag;
            add<LampButton>(*this, t.lab, RectF {tx, 52.f, tw, 28.f}, [this, flag] { return model_.view.*flag; },
                            [this, flag] { model_.view.*flag = !(model_.view.*flag); model_.pushViewState(); }, t.col, t.hint);
            tx += tw + 4.f;
        }
    }

    // the editor itself
    if (externalBackend_ != nullptr) {
        editorBackend_ = externalBackend_;
    } else {
        localBackend_ = std::make_unique<LocalEditorBackend>(model_);
        editorBackend_ = localBackend_.get();
    }
    editor_ = add<EnvelopeEditor>(*this, *editorBackend_);

    // editor footer: band tabs (Split only) · Quick shift · Rotate · Capture
    auto* tabs = add<Clickable>(*this, RectF {256.f, 424.f, 150.f, 24.f},
        [this](Gfx& g, Clickable& self) {
            const bool linked = model_.on(kParamEnvLink);
            g.well(256.f, 424.f, 150.f, 24.f, 3.f);
            const struct { const char* lab; int band; Rgba col; float x; } tb[] = {{"Low", 0, col::duck, 258.f}, {"High", 1, col::high, 350.f}};
            for (const auto& t : tb) {
                const bool sel = linked || model_.view.editBand == t.band;
                const float hv = (self.hot > 0.f && lastPtr_.x >= t.x && lastPtr_.x < t.x + 54.f) ? self.hot : 0.f;
                if (sel && !linked) g.raised(t.x, 426.f, 54.f, 20.f, 2.f, col::ink5);
                g.fillRR(t.x + 8.f, 433.f, 6.f, 6.f, 1.5f, sel ? t.col : t.col.withAlpha(0.35f));
                g.text(t.lab, t.x + 20.f, 436.5f, {11.f, sel ? Font::ScSemi : Font::Sc, sel ? col::textHi : mix(col::textMute, col::text, hv), Align::Left});
            }
            const float lh = (self.hot > 0.f && lastPtr_.x >= 314.f && lastPtr_.x < 348.f) ? self.hot : 0.f;
            if (lh > 0.01f) g.fillRR(314.f, 426.f, 34.f, 20.f, 3.f, col::warm.withAlpha(0.05f * lh));
            g.icon(linked ? Icon::Link : Icon::Unlink, 331.f, 436.f, linked ? col::textHi : mix(col::textMute, col::textHi, lh));
        },
        [this](const Pointer& p) {
            if (p.x >= 314.f && p.x < 348.f) {
                const bool linked = model_.on(kParamEnvLink);
                if (onLinkToggle) onLinkToggle(!linked);
                else {
                    if (linked) model_.setEnvelope(1, model_.env(0));   // unlinking copies A so nothing jumps
                    model_.setOnce(kParamEnvLink, linked ? 0.f : 1.f);
                }
                if (!linked) model_.view.editBand = 0;
            } else {
                model_.view.editBand = p.x < 314.f ? 0 : 1;
                if (onBandChanged) onBandChanged();
            }
            model_.pushViewState();
        });
    tabs->visibleIf = [this] { return model_.on(kParamMulti); };
    tabs->hintFn = [this] {
        if (lastPtr_.x >= 314.f && lastPtr_.x < 348.f)
            return std::string(model_.on(kParamEnvLink) ? "Linked: the high band follows the low envelope. Click to unlink"
                                                         : "Unlinked: each band has its own envelope. Click to link");
        return std::string(model_.on(kParamEnvLink) ? "Envelopes linked: both bands follow the low envelope" : "Choose which band's envelope to edit");
    };
    auto* qs = add<LampButton>(*this, "Quick shift", RectF {256.f, 424.f, 98.f, 24.f}, [this] { return model_.view.qsOn; },
                               [this] { model_.view.qsOn = !model_.view.qsOn; model_.pushViewState(); }, col::duck,
                               "Show the quick-shift bar: move the dip + recovery as one");
    qs->onLayout = [this, qs] { qs->r.x = model_.on(kParamMulti) ? 414.f : 256.f; };
    auto* rot = add<ValueField>(*this, kParamRotate, RectF {360.f, 424.f, 96.f, 24.f}, "Rotate");
    rot->onLayout = [this, rot] { rot->r.x = (model_.on(kParamMulti) ? 414.f : 256.f) + 104.f; };
    add<Clickable>(*this, RectF {776.f, 424.f, 92.f, 24.f},
        [this](Gfx& g, Clickable& self) {
            const int rs = live_.recState;
            g.raised(776.f, 424.f, 92.f, 24.f, 3.f, col::ink4, self.hot);
            if (rs == 2) {
                g.vg().save();
                g.vg().scissor(776.f, 424.f, 92.f * live_.recProgress, 24.f);
                g.fillRR(776.f, 424.f, 92.f, 24.f, 3.f, col::kick.withAlpha(0.22f));
                g.vg().restore();
            }
            const float blink = rs == 1 ? (std::sin(clock_ * 1000.0 / 180.0) > 0.0 ? 1.f : 0.28f) : 1.f;
            g.icon(rs ? Icon::Stop : Icon::Rec, 790.f, 436.f, col::kick.withAlpha(blink), rs ? 0.75f : 0.9f);
            g.text(rs == 1 ? "Armed" : rs == 2 ? "Capturing" : "Capture", 802.f, 436.5f, {11.f, Font::Sc, rs ? col::textHi : mix(col::textMute, col::text, self.hot), Align::Left});
        },
        [this](const Pointer&) { capture(); })->hintFn = [this] {
            return std::string(live_.recState ? "Cancel capture" : "Capture: record one cycle of the sidechain and turn it into a shape");
        };

    // shape library
    bank_ = add<ShapeBank>(*this, *editor_);
    add<LibraryHeader>(*this, *bank_);
}

void View::buildRight()
{
    add<Decor>(*this, [](Gfx& g) {
        title(g, "Depth", kRX, 64.f);
        g.text("0", 980.f - 44.f, 150.f + 52.f, {9.5f, Font::Sc, col::textDim, Align::Center});
        g.text("100", 980.f + 44.f, 150.f + 52.f, {9.5f, Font::Sc, col::textDim, Align::Center});
        g.groove(892.f, 244.f, 1068.f, 244.f);
        title(g, "Bands", kRX, 262.f);
    });
    auto* depth = add<Knob>(*this, kParamDepth, 980.f, 150.f, KnobSize::Hero, col::duck);
    depth->liveAmount = [this] {
        if (!live_.playing && model_.ivalue(kParamMode) == kModeSync) return 0.f;
        return model_.value(kParamDepth) / 100.f * (1.f - live_.valueA);
    };
    auto* dv = add<ValueText>(*this, kParamDepth, 980.f, 222.f, Align::Center, 17.f, Font::Exp);
    dv->color = col::textHi;
    add<LampButton>(*this, "Split", RectF {1000.f, 251.f, 64.f, 22.f}, [this] { return model_.on(kParamMulti); },
                    [this] { model_.toggle(kParamMulti); if (!model_.on(kParamMulti)) model_.view.editBand = 0; },
                    col::duck, "Multiband: duck low and high bands separately");
    add<CrossoverGraph>(*this, RectF {kRX, 282.f, kRW, 66.f});
    auto* slope = add<Segmented>(*this, RectF {kRX, 356.f, 92.f, 22.f}, std::vector<std::string> {"12", "24 dB"},
                                 [this] { return model_.ivalue(kParamSlope); }, [this](int i) { model_.setOnce(kParamSlope, float(i)); }, true);
    slope->disabledIf = [this] { return !model_.on(kParamMulti); };
    slope->hints = {"12 dB/oct crossover", "24 dB/oct crossover"};
    for (int j = 0; j < 2; ++j) {
        const int soloVal = j == 0 ? kSoloLow : kSoloHigh;
        const float bx = kRX + 100.f + float(j) * 35.f;
        const Rgba bcol = j == 0 ? col::duck : col::high;
        const char* lab = j == 0 ? "L" : "H";
        add<Clickable>(*this, RectF {bx, 356.f, 33.f, 22.f},
            [this, bx, bcol, lab, soloVal](Gfx& g, Clickable& self) {
                const bool on = model_.on(kParamMulti), s = model_.ivalue(kParamBandSolo) == soloVal;
                g.raised(bx, 356.f, 33.f, 22.f, 3.f, s ? col::ink5 : col::ink4, on ? self.hot : 0.f);
                const Rgba c = !on ? col::textDim : s ? bcol : col::textMute;
                g.icon(Icon::Phones, bx + 11.f, 366.5f, c, 0.85f);
                g.text(lab, bx + 24.f, 367.5f, {11.f, Font::ScSemi, c, Align::Center});
            },
            [this, soloVal](const Pointer&) {
                if (!model_.on(kParamMulti)) return;
                model_.setOnce(kParamBandSolo, model_.ivalue(kParamBandSolo) == soloVal ? float(kSoloOff) : float(soloVal));
            }, j == 0 ? "Solo the low band" : "Solo the high band");
    }
    auto* lo = add<Knob>(*this, kParamLoMix, kRX + 38.f, 412.f, KnobSize::Small, col::duck, "Low mix");
    lo->dimIf = [this] { return !model_.on(kParamMulti); };
    auto* hi = add<Knob>(*this, kParamHiMix, kRX + kRW - 38.f, 412.f, KnobSize::Small, col::high, "High mix");
    hi->dimIf = [this] { return !model_.on(kParamMulti); };
    add<Decor>(*this, [this](Gfx& g) {
        g.groove(892.f, 470.f, 1068.f, 470.f);
        title(g, "Stereo", kRX, 488.f);
        const std::string s = formatParam(kParamMidSide, model_.value(kParamMidSide));
        g.text(s.c_str(), kRX + kRW, 488.5f, {11.f, Font::ScSemi, col::text, Align::Right});
        g.groove(892.f, 548.f, 1068.f, 548.f);
        title(g, "Output", kRX, 566.f);
        const std::string og = formatParam(kParamOutGain, model_.value(kParamOutGain));
        g.text(og.c_str(), kRX + 50.f, 592.f, {11.f, Font::ScSemi, col::text, Align::Left});
        g.text("gain", kRX + 50.f, 606.f, {11.f, Font::Sc, col::textDim, Align::Left});
    });
    add<MsSlider>(*this, RectF {kRX, 500.f, kRW, 36.f});
    auto* og = add<Knob>(*this, kParamOutGain, kRX + 22.f, 598.f, KnobSize::Small, col::text, nullptr, true);
    (void)og;
    add<LampButton>(*this, "Delta", RectF {kRX + kRW - 66.f, 556.f, 66.f, 22.f}, [this] { return model_.on(kParamDelta); },
                    [this] { model_.toggle(kParamDelta); }, col::duck, "Delta: hear only what is being removed");
    add<GrMeter>(*this, RectF {kRX + kRW - 66.f, 586.f, 66.f, 40.f});
}

void View::buildFooter()
{
    add<Decor>(*this, [this](Gfx& g) {
        const bool has = !hint_.empty();
        const char* idle = "Hover anything for help. Ctrl+Z undo \xC2\xB7 Ctrl+A select all nodes \xC2\xB7 Delete removes selected nodes";
        g.text(has ? hint_.c_str() : idle, 16.f, 646.5f, {10.5f, Font::Sc, has ? col::textMute : col::textDim, Align::Left});
    });
    add<Clickable>(*this, RectF {994.f, 636.f, 60.f, 20.f},
        [this](Gfx& g, Clickable& self) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.0f %%", double(settings_.scale * 100.f));
            g.text(buf, 1040.f, 646.5f, {10.5f, Font::Sc, mix(col::textDim, col::text, self.hot), Align::Right});
            g.icon(Icon::Caret, 1047.f, 646.5f, col::textDim, 0.7f);
        },
        [this](const Pointer&) { openMenu(994.f, 632.f, settingsMenu(true), -1.f, true); }, "UI size");
    auto* grip = add<Clickable>(*this, RectF {1058.f, 636.f, 22.f, 24.f},
        [](Gfx& g, Clickable& self) { g.icon(Icon::Grip, 1070.f, 650.f, self.hot > 0.3f ? col::textMute : col::textDim, 0.9f); },
        nullptr, "Drag to resize (keeps the aspect ratio, 100\xE2\x80\x93" "200 %)", DGL_NAMESPACE::kMouseCursorUpLeftDownRight);
    grip->onDrag = [this](const Pointer& p, float, float) {
        // p.x is in logical units at the current scale: the window's right edge follows the pointer
        const float ns = std::clamp(std::round((p.x + 6.f) * settings_.scale / kBaseW * 20.f) / 20.f, 1.f, 2.f);
        if (std::fabs(ns - settings_.scale) > 1e-3f)
            setScale(ns);
    };
}

void View::buildOverlays()
{
    menu_    = add<MenuOverlay>(*this);
    rate_    = add<RateGridOverlay>(*this);
    browser_ = add<PresetBrowser>(*this);
    menu_->visibleIf    = [this] { return menu_->isOpen(); };
    rate_->visibleIf    = [this] { return rate_->isOpen(); };
    browser_->visibleIf = [this] { return browser_->isOpen(); };
    // the rate grid and menus sit above the browser: move the browser before them
    auto itB = std::find_if(widgets_.begin(), widgets_.end(), [this](const auto& w) { return w.get() == browser_; });
    auto itM = std::find_if(widgets_.begin(), widgets_.end(), [this](const auto& w) { return w.get() == menu_; });
    if (itB != widgets_.end() && itM != widgets_.end())
        std::iter_swap(itB, itM);   // order: …, browser, rate, menu
    auto itR = std::find_if(widgets_.begin(), widgets_.end(), [this](const auto& w) { return w.get() == rate_; });
    auto itM2 = std::find_if(widgets_.begin(), widgets_.end(), [this](const auto& w) { return w.get() == menu_; });
    if (itR != widgets_.end() && itM2 != widgets_.end() && itR > itM2)
        std::iter_swap(itR, itM2);
}

std::vector<MenuItem> View::settingsMenu(bool sizesOnly)
{
    std::vector<MenuItem> items;
    items.push_back(MenuItem::head("UI size"));
    for (float s : {1.f, 1.25f, 1.5f, 1.75f, 2.f}) {
        MenuItem it;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.0f %%", double(s * 100.f));
        it.label = buf;
        it.check = std::fabs(settings_.scale - s) < 1e-3f;
        it.action = [this, s] { setScale(s); };
        items.push_back(std::move(it));
    }
    if (sizesOnly)
        return items;
    items.push_back(MenuItem::sep());
    items.push_back(MenuItem::head("MIDI note names"));
    MenuItem c3; c3.label = "C3 = 60 (Ableton, Bitwig)"; c3.check = settings_.noteC3; c3.action = [this] { settings_.noteC3 = true; settings_.save(); };
    MenuItem c5; c5.label = "C5 = 60 (FL Studio)"; c5.check = !settings_.noteC3; c5.action = [this] { settings_.noteC3 = false; settings_.save(); };
    items.push_back(std::move(c3));
    items.push_back(std::move(c5));
    items.push_back(MenuItem::sep());
    MenuItem tt; tt.label = "Pop-up tooltips"; tt.check = settings_.tooltips; tt.action = [this] { settings_.tooltips = !settings_.tooltips; settings_.save(); };
    MenuItem an; an.label = "Animate shape changes"; an.check = settings_.animate; an.action = [this] { settings_.animate = !settings_.animate; settings_.save(); };
    items.push_back(std::move(tt));
    items.push_back(std::move(an));
    items.push_back(MenuItem::sep());
    MenuItem rs; rs.label = "Rescan presets and shapes"; rs.action = [this] { presets_.rescan(); shapes_.rescan(); if (bank_) bank_->refresh(); };
    items.push_back(std::move(rs));
    return items;
}

// ---------------------------------------------------------------------------------------------
// services

void View::openMenu(float x, float y, std::vector<MenuItem> items, float rightEdge, bool above)
{
    commitEntry();
    menu_->open(x, y, std::move(items), rightEdge, above);
    repaint();
}

void View::openRateGrid(float x, float y)
{
    commitEntry();
    rate_->open(x, y);
    repaint();
}

void View::openPresetBrowser(bool open)
{
    if (open) browser_->open(); else browser_->close();
    repaint();
}

bool View::presetBrowserOpen() const { return browser_ != nullptr && browser_->isOpen(); }

void View::openEntry(int paramId)
{
    commitEntry();
    entry_ = {};
    entry_.open = true;
    entry_.param = paramId;
    const float v = model_.value(paramId);
    char buf[32];
    if (paramId == kParamCrossover || paramId == kParamMidiNote || isStepped(paramId))
        std::snprintf(buf, sizeof(buf), "%.0f", double(v));
    else
        std::snprintf(buf, sizeof(buf), "%g", std::round(double(v) * 100.0) / 100.0);
    entry_.text = buf;
    entry_.anchor = 0;
    entry_.caret = entry_.text.size();
    entry_.blinkFrom = clock_;
    win_.winGrabKeyboard();
    repaint();
}

void View::openTextEntry(const std::string& initial, float cx, float cy, float width, std::function<void(const std::string&)> commit)
{
    commitEntry();
    entry_ = {};
    entry_.open = true;
    entry_.param = -1;
    entry_.text = initial;
    entry_.anchor = 0;
    entry_.caret = entry_.text.size();
    entry_.blinkFrom = clock_;
    entry_.cx = cx;
    entry_.cy = cy;
    entry_.width = width;
    entry_.commit = std::move(commit);
    win_.winGrabKeyboard();
    repaint();
}

void View::drawEntry(Gfx& g, float cx, float cy, float size)
{
    const std::string& s = entry_.text;
    const float tw = s.empty() ? 0.f : g.measure(s.c_str(), size, Font::ScSemi);
    const float w = std::max(entry_.param < 0 ? entry_.width : 56.f, tw + 18.f);
    entry_.box = {cx - w * 0.5f, cy - 10.f, w, 20.f};
    g.fillRR(cx - w * 0.5f, cy - 10.f, w, 20.f, 3.f, col::ink0);
    g.strokeRR(cx - w * 0.5f + 0.5f, cy - 9.5f, w - 1.f, 19.f, 3.f, col::duck);
    // caret stops (every code point boundary) and their x, for drawing and for mouse hits
    const float x0 = cx - tw * 0.5f;
    entry_.stops.clear();
    entry_.stopX.clear();
    for (std::size_t i = 0;; i = nextCp(s, i)) {
        entry_.stops.push_back(i);
        entry_.stopX.push_back(i == 0 ? x0 : i >= s.size() ? x0 + tw : x0 + g.measure(s.substr(0, i).c_str(), size, Font::ScSemi));
        if (i >= s.size())
            break;
    }
    auto xAt = [&](std::size_t b) {
        const auto it = std::lower_bound(entry_.stops.begin(), entry_.stops.end(), b);
        return entry_.stopX[std::size_t(std::min(it - entry_.stops.begin(), std::ptrdiff_t(entry_.stopX.size()) - 1))];
    };
    if (entry_.caret != entry_.anchor) {
        const float a = xAt(std::min(entry_.caret, entry_.anchor)), b = xAt(std::max(entry_.caret, entry_.anchor));
        g.fillRR(a - 1.f, cy - 7.f, b - a + 2.f, 14.f, 2.f, col::duck.withAlpha(0.3f));
    }
    if (!s.empty())
        g.text(s.c_str(), cx, cy, {size, Font::ScSemi, col::textHi, Align::Center});
    if (entry_.caret == entry_.anchor && caretPhaseOn()) {
        const float x = xAt(entry_.caret) + 0.5f;
        g.line(x, cy - 6.f, x, cy + 6.f, col::duck, 1.2f);
    }
}

bool View::caretPhaseOn() const
{
    return std::fmod(clock_ - (entry_.open ? entry_.blinkFrom : 0.0), 1.0) < 0.56;
}

std::size_t View::entryStopAt(float x) const
{
    std::size_t best = entry_.text.size();
    float bestD = 1e9f;
    for (std::size_t i = 0; i < entry_.stops.size(); ++i) {
        const float d = std::fabs(entry_.stopX[i] - x);
        if (d < bestD) { bestD = d; best = entry_.stops[i]; }
    }
    return best;
}

void View::entryErase(std::size_t from, std::size_t to)
{
    if (to > from)
        entry_.text.erase(from, to - from);
    entry_.caret = entry_.anchor = from;
}

void View::entryInsert(const std::string& s)
{
    const std::size_t lo = std::min(entry_.caret, entry_.anchor), hi = std::max(entry_.caret, entry_.anchor);
    if (entry_.text.size() - (hi - lo) + s.size() > 64)
        return;
    entryErase(lo, hi);
    entry_.text.insert(lo, s);
    entry_.caret = entry_.anchor = lo + s.size();
}

void View::entryKey(unsigned key, const Pointer& p)
{
    using namespace DGL_NAMESPACE;
    Entry& e = entry_;
    const std::string& s = e.text;
    const std::size_t lo = std::min(e.caret, e.anchor), hi = std::max(e.caret, e.anchor);
    const bool sel = lo != hi;
    e.blinkFrom = clock_;
    auto moveTo = [&](std::size_t to) {   // Shift extends the selection, otherwise it collapses
        e.caret = to;
        if (!p.shift)
            e.anchor = to;
    };
    switch (key) {
    case kKeyEnter: case kKeyPadEnter: commitEntry(); return;
    case kKeyEscape: cancelEntry(); return;
    case kKeyLeft:
        if (sel && !p.shift && !p.ctrl) moveTo(lo);
        else moveTo(p.ctrl ? prevWord(s, e.caret) : prevCp(s, e.caret));
        return;
    case kKeyRight:
        if (sel && !p.shift && !p.ctrl) moveTo(hi);
        else moveTo(p.ctrl ? nextWord(s, e.caret) : nextCp(s, e.caret));
        return;
    case kKeyHome: moveTo(0); return;
    case kKeyEnd: moveTo(s.size()); return;
    case kKeyBackspace:
        if (sel) entryErase(lo, hi);
        else entryErase(p.ctrl ? prevWord(s, e.caret) : prevCp(s, e.caret), e.caret);
        return;
    case kKeyDelete:
        if (sel) entryErase(lo, hi);
        else entryErase(e.caret, p.ctrl ? nextWord(s, e.caret) : nextCp(s, e.caret));
        return;
    default:
        if (p.ctrl && !p.alt && (key == 'a' || key == 'A')) {   // not AltGr (Ctrl+Alt): that types
            e.anchor = 0;
            e.caret = s.size();
        }
        return;   // the field owns the keyboard while it is open
    }
}

void View::drawTextEntry(Gfx& g)
{
    if (textEntryOpen())
        drawEntry(g, entry_.cx, entry_.cy, 11.5f);
}

void View::commitEntry()
{
    if (!entry_.open)
        return;
    Entry e = std::move(entry_);
    entry_ = {};
    win_.winReleaseKeyboard();   // before commit: it may open a new entry ("Name taken")
    if (e.param < 0) {
        if (e.commit) {
            std::string t = e.text;
            while (!t.empty() && t.back() == ' ') t.pop_back();
            while (!t.empty() && t.front() == ' ') t.erase(t.begin());
            e.commit(t);
        }
    } else {
        float v = 0.f;
        if (parseValue(e.text, v))
            model_.setOnce(e.param, clampParam(e.param, v));
    }
    repaint();
}

void View::cancelEntry()
{
    entry_ = {};
    win_.winReleaseKeyboard();
    repaint();
}

void View::paramMenu(int paramId, float x, float y)
{
    std::vector<MenuItem> items;
    MenuItem r;
    r.label = "Reset to default  (" + formatParam(paramId, kParams[paramId].def, settings_.noteC3) + ")";
    r.action = [this, paramId] { model_.setOnce(paramId, kParams[paramId].def); };
    items.push_back(std::move(r));
    MenuItem t;
    t.label = "Enter value\xE2\x80\xA6";
    t.action = [this, paramId] { openEntry(paramId); };
    items.push_back(std::move(t));
    items.push_back(MenuItem::sep());
    MenuItem c;
    c.label = "Copy value";
    c.action = [this, paramId] { clipValue_ = model_.value(paramId); hasClip_ = true; };
    items.push_back(std::move(c));
    MenuItem p;
    p.label = "Paste value";
    p.dim = !hasClip_;
    p.action = [this, paramId] { if (hasClip_) model_.setOnce(paramId, clampParam(paramId, clipValue_)); };
    items.push_back(std::move(p));
    openMenu(x, y, std::move(items));
}

void View::applyShape(const Envelope& e)
{
    const int band = model_.editedBand();
    const Envelope before = model_.env(band);
    if (onApplyShape) {
        onApplyShape(e);
    } else {
        model_.setEnvelope(band, e, true, true);
    }
    editor_->startMorph(before, model_.env(band));
    repaint();
}

void View::loadPreset(const std::string& id)
{
    PresetData d;
    if (id.empty() || !presets_.load(id, d))
        return;
    const Envelope beforeA = model_.env(0);
    const int band = model_.editedBand();
    const Envelope before = model_.env(band);
    if (onLoadPreset) {
        onLoadPreset(d);
    } else {
        History& h = model_.history();
        h.beginTransaction("Load preset");
        for (int i = 0; i < kParamCount; ++i) {
            if (i == kParamBypass)
                continue;
            const float old = model_.value(i);
            model_.setNoUndo(i, d.params[i]);
            h.recordParam(i, old, model_.value(i));
        }
        Envelope a, b;
        if (!a.deserialize(d.envA)) a = Envelope();
        if (!b.deserialize(d.envB)) b = Envelope::flat();
        model_.setEnvelope(0, a, true, true);
        model_.setEnvelope(1, b, true, true);
        h.commitTransaction();
    }
    model_.presetId = id;
    model_.presetName = d.name;
    model_.presetCategory = d.category;
    model_.dirty = false;
    model_.pushPresetState();
    editor_->startMorph(before, model_.env(model_.editedBand()));
    (void)beforeA;
    repaint();
}

void View::savePreset(bool saveAs)
{
    const bool isUser = model_.presetId.rfind("user:", 0) == 0;
    if (!saveAs && isUser) {
        saveUserPreset(model_.presetName, true);
        return;
    }
    const std::string suggestion = isUser ? model_.presetName : model_.presetName + " (mine)";
    openTextEntry(suggestion, 468.f, 22.f, 220.f, [this](const std::string& n) { saveUserPreset(n, false); });
}

void View::saveUserPreset(const std::string& name, bool overwrite)
{
    if (name.empty())
        return;
    PresetData d = PresetData::defaults();
    d.name = name;
    d.category = model_.presetCategory.empty() ? "Sidechain" : model_.presetCategory;
    d.author = "User";
    for (int i = 0; i < kParamCount; ++i) {
        d.params[i] = model_.value(i);
        d.has[i] = i != kParamBypass;
    }
    d.envA = model_.env(0).serialize();
    d.envB = model_.env(1).serialize();
    std::string err;
    if (!presets_.saveUser(d, overwrite, &err)) {
        savedFlash_ = 1.f;
        if (!overwrite && err.find("already exists") != std::string::npos) {
            // name taken by another user preset: say so and ask again, caret at the end of the name
            saveMsg_ = "Name taken";
            openTextEntry(name, 468.f, 22.f, 220.f, [this](const std::string& n) { saveUserPreset(n, false); });
            entry_.anchor = entry_.caret;
        } else {
            saveMsg_ = "Save failed";
        }
        repaint();
        return;
    }
    for (const PresetEntry& e : presets_.entries())
        if (!e.isFactory && e.name == d.name && e.category == d.category)
            model_.presetId = e.id;
    model_.presetName = d.name;
    model_.presetCategory = d.category;
    model_.dirty = false;
    model_.pushPresetState();
    saveMsg_ = "Saved";
    savedFlash_ = 1.f;
    repaint();
}

void View::setScale(float s)
{
    settings_.scale = std::clamp(s, 1.f, 2.f);
    settings_.save();
    win_.winSetUserScale(settings_.scale);
    repaint();
}

void View::capture()
{
    if (bridge_ == nullptr && !demoMode_)
        return;
    if (live_.recState != 0) {
        if (bridge_) bridge_->recState.store(Bridge::kRecIdle, std::memory_order_release);
        demoRec_ = 0;
        live_.recState = 0;
    } else {
        if (bridge_) bridge_->recState.store(Bridge::kRecArmed, std::memory_order_release);
        demoRec_ = 1;
        live_.recState = 1;
    }
    repaint();
}

// ---------------------------------------------------------------------------------------------
// debug states for snapshots (mirrors applyShot() in the prototype)

void View::applyDebugState(const std::string& shot)
{
    demoMode_ = true;
    demoFreeze_ = 0.34f;
    auto set = [this](int id, float v) { model_.setNoUndo(id, v); };
    model_.presetName = "Classic Pump";
    model_.presetCategory = "Sidechain";
    model_.dirty = true;
    if (shot == "midi") set(kParamMode, float(kModeMidi));
    else if (shot == "audio") { set(kParamMode, float(kModeAudio)); set(kParamTrigFilter, 1.f); demoFreeze_ = 0.2f; }
    else if (shot == "spectral") { set(kParamMode, float(kModeSpectral)); set(kParamTrigSource, float(kTrigAudio)); demoFreeze_ = 0.08f; }
    else if (shot == "ring") { set(kParamMode, float(kModeRing)); set(kParamTrigSource, float(kTrigSync)); demoFreeze_ = 0.12f; }
    else if (shot == "multi") { set(kParamMulti, 1.f); set(kParamEnvLink, 0.f); set(kParamCrossover, 180.f); model_.view.editBand = 1; if (onBandChanged) onBandChanged(); }
    else if (shot == "depth60") set(kParamDepth, 62.f);
    else if (shot == "oneshot") { set(kParamMode, float(kModeAudio)); set(kParamPlayMode, float(kPlayOneShot)); }
    else if (shot == "presets") openPresetBrowser(true);
    else if (shot == "rate") openRateGrid(16.f, 118.f);
    else if (shot == "menu") openMenu(986.f, 38.f, settingsMenu(false), 1064.f);
    else if (shot == "entry") openEntry(kParamDepth);
    else if (shot == "ms") { set(kParamTimeMode, float(kTimeFree)); set(kParamLengthMs, 180.f); set(kParamGrid, 2.f); set(kParamSwing, 40.f); }
    else if (shot == "record") { demoRec_ = 2; live_.recState = 2; live_.recProgress = 0.46f; demoFreeze_ = 0.46f; }
    for (int i = 0; i < 60; ++i)
        demo_.step(model_, live_, 1.0 / 60.0, demoFreeze_);
}

// ---------------------------------------------------------------------------------------------
// host → UI

void View::parameterChanged(int id, float value)
{
    model_.hostParameterChanged(id, value);
    repaint();
}

void View::stateChanged(const char* key, const char* value)
{
    model_.hostStateChanged(key, value);
    repaint();
}

// ---------------------------------------------------------------------------------------------
// polling

void View::pollBridge(Bridge* br, double dt)
{
    Live& l = live_;
    const float fdt = float(dt);
    if (demoMode_) {
        demo_.step(model_, l, dt, demoFreeze_);
    } else if (br != nullptr) {
        l.phase = br->phase.load(std::memory_order_relaxed);
        l.valueA = br->valueA.load(std::memory_order_relaxed);
        l.valueB = br->valueB.load(std::memory_order_relaxed);
        l.grDb = br->gainReductionDb.load(std::memory_order_relaxed);
        l.triggerCount = br->triggerCount.load(std::memory_order_relaxed);
        l.active = br->envelopeActive.load(std::memory_order_relaxed) != 0;
        l.bpm = br->bpm.load(std::memory_order_relaxed);
        l.playing = br->hostPlaying.load(std::memory_order_relaxed) != 0;
        l.cycleSeconds = br->cycleSeconds.load(std::memory_order_relaxed);
        l.timeSigNum = br->timeSigNum.load(std::memory_order_relaxed);
        l.timeSigDen = br->timeSigDen.load(std::memory_order_relaxed);
        l.scLevelDb = br->scLevelDb.load(std::memory_order_relaxed);
        l.outPeakDb = br->outPeakDb.load(std::memory_order_relaxed);
        l.lastNote = br->lastNote.load(std::memory_order_relaxed);
        l.lastVelocity = br->lastVelocity.load(std::memory_order_relaxed);
        l.noteCount = br->noteCount.load(std::memory_order_relaxed);
        // waveforms: peak over a ~±6 ms window (so a bass's half-cycles merge into its envelope
        // at any rate), then a per-bin release (DESIGN.md §7.3)
        const float rel = std::pow(10.f, -30.f * fdt / 20.f);
        const float msPerBin = std::max(1e-3f, l.cycleSeconds * 1000.f / float(Live::kBins));
        const int half = std::clamp(int(std::lround(6.f / msPerBin)), 1, 24);
        auto smooth = [&](const std::atomic<float>* src, float* dst) {
            float raw[Live::kBins];
            for (int i = 0; i < Live::kBins; ++i)
                raw[i] = src[i].load(std::memory_order_relaxed);
            float peak[Live::kBins];
            for (int i = 0; i < Live::kBins; ++i) {
                float v = 0.f;
                for (int j = std::max(0, i - half); j <= std::min(Live::kBins - 1, i + half); ++j)
                    v = std::max(v, raw[j]);
                peak[i] = v;
            }
            for (int i = 0; i < Live::kBins; ++i) {   // box blur removes the max filter's steps
                float sum = 0.f;
                int n = 0;
                for (int j = std::max(0, i - half); j <= std::min(Live::kBins - 1, i + half); ++j, ++n)
                    sum += peak[j];
                const float v = sum / float(n);
                dst[i] = v >= dst[i] ? v : std::max(v, dst[i] * rel);
            }
        };
        smooth(br->mainWave, l.mainWave);
        smooth(br->extWave, l.extWave);
        smooth(br->outWave, l.outWave);
        smooth(br->ringModWave, l.ringWave);
        // spectral: instant attack, 250 ms release (display ballistics)
        const float relDb = 60.f * fdt / 0.25f;
        for (int i = 0; i < kSpecBands; ++i) {
            const float cut = br->specCutDb[i].load(std::memory_order_relaxed);
            l.specCutDb[i] = cut <= l.specCutDb[i] ? cut : std::min(cut, l.specCutDb[i] + relDb * 0.5f);
            const float sc = br->specScDb[i].load(std::memory_order_relaxed);
            l.specScDb[i] = sc >= l.specScDb[i] ? sc : std::max(sc, l.specScDb[i] - relDb);
            l.specHz[i] = br->specCenterHz[i].load(std::memory_order_relaxed);
        }
        l.recState = br->recState.load(std::memory_order_acquire);
        l.recProgress = br->recProgress.load(std::memory_order_relaxed);
        // Stopped in Sync mode the envelope free-runs (edits stay audible) but the editor shows it
        // at rest (no playhead, no depth ring); show no gain reduction either unless something is
        // audible, so an open editor on a stopped, silent project does not repaint at all.
        if (envelopeAtRest() && l.outPeakDb < -90.f)
            l.grDb = 0.f;
    }
    // threshold meter: peak hold with a 40 dB/s fall
    l.scPeakHoldDb = std::max(l.scLevelDb, l.scPeakHoldDb - 40.f * fdt);
    // flashes
    if (firstPoll_) {
        lastTrig_ = l.triggerCount;
        lastNotes_ = l.noteCount;
        firstPoll_ = false;
    }
    if (l.triggerCount != lastTrig_) { l.trigFlash = 1.f; lastTrig_ = l.triggerCount; }
    else l.trigFlash = std::max(0.f, l.trigFlash - fdt * 5.f);
    if (l.noteCount != lastNotes_) {
        l.noteFlash = 1.f;
        lastNotes_ = l.noteCount;
        if (learn_ && l.lastNote >= 0) {
            model_.setOnce(kParamMidiNote, float(l.lastNote));
            learn_ = false;
        }
    } else {
        l.noteFlash = std::max(0.f, l.noteFlash - fdt * 5.f);
    }
}

void View::handleCapture(Bridge* br)
{
    if (demoMode_) {
        // simulate the handshake with the demo kick
        if (demoRec_ == 1 && live_.phase < demoPrevPhase_) demoRec_ = 2;
        if (demoRec_ == 2) {
            live_.recProgress = live_.phase;
            if (live_.phase < demoPrevPhase_ && demoPrevPhase_ > 0.9f) demoRec_ = 3;
        }
        demoPrevPhase_ = live_.phase;
        live_.recState = demoRec_ == 3 ? 0 : demoRec_;
        if (demoRec_ != 3)
            return;
        demoRec_ = 0;
        float buf[Bridge::kRecBins];
        for (int i = 0; i < Bridge::kRecBins; ++i) {
            const float t = float(i) / float(Bridge::kRecBins) * live_.cycleSeconds;
            buf[i] = 0.92f * std::exp(-t / 0.07f);
        }
        finishCapture(buf, Bridge::kRecBins);
        return;
    }
    if (br == nullptr || live_.recState != Bridge::kRecDone)
        return;
    float buf[Bridge::kRecBins];
    for (int i = 0; i < Bridge::kRecBins; ++i)
        buf[i] = br->recBuf[i].load(std::memory_order_relaxed);
    br->recState.store(Bridge::kRecIdle, std::memory_order_release);
    live_.recState = 0;
    finishCapture(buf, Bridge::kRecBins);
}

void View::finishCapture(const float* buf, int bins)
{
    Envelope e;
    bool ok = false;
    if (onCapture) {
        ok = onCapture(buf, bins, e);
    } else {
        float mx = 1e-6f;
        for (int i = 0; i < bins; ++i) mx = std::max(mx, std::fabs(buf[i]));
        if (mx > 1e-4f) {
            std::vector<float> ys(static_cast<std::size_t>(bins), 0.f);
            for (int i = 0; i < bins; ++i) ys[std::size_t(i)] = 1.f - std::clamp(std::fabs(buf[i]) / mx, 0.f, 1.f);
            e = Envelope::fromSamples(ys.data(), bins, 0.02f);
            const int band = model_.editedBand();
            const Envelope before = model_.env(band);
            model_.setEnvelope(band, e, true, true);
            editor_->startMorph(before, e);
            ok = true;
        }
    }
    if (!ok)
        return;
    const int n = int(shapes_.entries("User").size()) + 1;
    char name[32];
    std::snprintf(name, sizeof(name), "Captured %d", n);
    if (shapes_.saveUser(name, model_.env(model_.editedBand()))) {
        shapes_.rescan();
        if (bank_) bank_->refresh();
    }
    editor_->flashCaptured();
    repaint();
}

bool View::idle(Bridge* bridge, double dt)
{
    bridge_ = bridge;
    clock_ += dt;
    pollBridge(bridge, dt);
    handleCapture(bridge);
    savedFlash_ = std::max(0.f, savedFlash_ - float(dt) * 0.8f);
    if (editor_) editor_->tick(float(dt));
    bool anim = false;
    for (auto& w : widgets_) {
        const float target = (w.get() == hot_ || w.get() == active_) ? 1.f : 0.f;
        const float d = target - w->hot;
        if (std::fabs(d) > 0.002f) {
            w->hot += d * std::min(1.f, 12.f * float(dt));
            anim = true;
        } else {
            w->hot = target;
        }
        if (w->visible && w->animating())
            anim = true;
    }
    const bool changed = model_.revision() != lastRevision_;
    lastRevision_ = model_.revision();
    const bool tooltipDue = settings_.tooltips && hot_ != nullptr && clock_ - hoverSince_ > 0.7 && clock_ - hoverSince_ < 0.7 + dt * 1.5;
    // Interaction (hover fades, edits, explicit repaints) paints right away.
    if (anim || changed || dirty_ || tooltipDue) {
        dirty_ = false;
        return true;
    }
    // Everything else is ambient and painted at most kAmbientFps: live displays only when what
    // they show actually moved since the last paint (a stopped transport or a silent sidechain
    // costs nothing), text carets only when they blink.
    const bool caretOn = caretPhaseOn();
    const bool caretDue = (entry_.open || browser_->isOpen()) && caretOn != paintedCaretOn_;
    const bool ambient = demoMode_ || savedFlash_ > 0.f || caretDue || liveMoved(live_, painted_, envelopeAtRest());
    return ambient && clock_ - paintedAt_ >= 1.0 / kAmbientFps - 1e-3;
}

bool View::envelopeAtRest() const
{
    return !live_.playing && model_.ivalue(kParamMode) == kModeSync;
}

bool View::liveMoved(const Live& a, const Live& b, bool atRest)
{
    // tolerances sit below what a paint could show at 2x (a tenth of a pixel, 0.05 dB)
    auto near = [](float x, float y, float eps) { return std::fabs(x - y) <= eps; };
    auto nearAll = [&](const float* x, const float* y, int n, float eps) {
        for (int i = 0; i < n; ++i)
            if (!near(x[i], y[i], eps))
                return false;
        return true;
    };
    if (a.active != b.active || a.playing != b.playing || a.timeSigNum != b.timeSigNum || a.timeSigDen != b.timeSigDen
        || a.lastNote != b.lastNote || a.lastVelocity != b.lastVelocity || a.recState != b.recState)
        return true;
    // at rest the playhead and the envelope value are not drawn
    if (!atRest && (!near(a.phase, b.phase, 1e-4f) || !near(a.valueA, b.valueA, 1e-3f) || !near(a.valueB, b.valueB, 1e-3f)))
        return true;
    if (!near(a.grDb, b.grDb, 0.05f) || !near(a.bpm, b.bpm, 0.05f) || !near(a.cycleSeconds, b.cycleSeconds, 1e-4f)
        || !near(a.scLevelDb, b.scLevelDb, 0.05f) || !near(a.scPeakHoldDb, b.scPeakHoldDb, 0.05f)
        || !near(a.outPeakDb, b.outPeakDb, 0.05f) || !near(a.recProgress, b.recProgress, 1e-3f)
        || !near(a.trigFlash, b.trigFlash, 1e-3f) || !near(a.noteFlash, b.noteFlash, 1e-3f))
        return true;
    return !nearAll(a.mainWave, b.mainWave, Live::kBins, 2e-3f) || !nearAll(a.extWave, b.extWave, Live::kBins, 2e-3f)
        || !nearAll(a.outWave, b.outWave, Live::kBins, 2e-3f) || !nearAll(a.ringWave, b.ringWave, Live::kBins, 2e-3f)
        || !nearAll(a.specCutDb, b.specCutDb, kSpecBands, 0.1f) || !nearAll(a.specScDb, b.specScDb, kSpecBands, 0.1f)
        || !nearAll(a.specHz, b.specHz, kSpecBands, 0.5f);
}

// ---------------------------------------------------------------------------------------------
// paint

void View::paintChassis(Gfx& g)
{
    auto& vg = g.vg();
    vg.beginPath();
    vg.rect(0.f, 0.f, kBaseW, kBaseH);
    vg.fillPaint(vg.linearGradient(0.f, 0.f, 0.f, kBaseH, Gfx::c(hex(0x22211F)), Gfx::c(hex(0x1B1A18))));
    vg.fill();
    // device-pixel grain: one texel per device pixel at every scale
    g.tiledImage(res_.grain, 0.f, 0.f, kBaseW, kBaseH, 256.f / g.scale(), 0.16f);
    vg.beginPath();
    vg.rect(0.f, 0.f, kBaseW, 44.f);
    vg.fillPaint(vg.linearGradient(0.f, 0.f, 0.f, 44.f, Gfx::c(hex(0x1A1A18)), Gfx::c(hex(0x171715))));
    vg.fill();
    g.groove(0.f, 44.f, kBaseW, 44.f);
    g.groove(244.f, 46.f, 244.f, 632.f);
    g.groove(880.f, 46.f, 880.f, 632.f);
    g.fillRect(0.f, 633.f, kBaseW, 27.f, hex(0x171715));
    g.groove(0.f, 632.f, kBaseW, 632.f);
}

void View::paintTooltip(Gfx& g)
{
    if (!settings_.tooltips || hot_ == nullptr || active_ != nullptr || clock_ - hoverSince_ < 0.7)
        return;
    const std::string h = hot_->hint();
    if (h.empty())
        return;
    const float w = std::min(360.f, g.measure(h.c_str(), 11.f) + 16.f);
    float x = std::clamp(lastPtr_.x + 12.f, 4.f, kBaseW - w - 4.f), y = lastPtr_.y + 20.f;
    if (y + 24.f > kBaseH) y = lastPtr_.y - 30.f;
    g.dropShadow(x, y, w, 22.f, 3.f, 10.f, 0.45f, 3.f);
    g.fillRR(x, y, w, 22.f, 3.f, col::ink3);
    g.strokeRR(x + 0.5f, y + 0.5f, w - 1.f, 21.f, 3.f, col::ink6);
    auto& vg = g.vg();
    vg.save();
    vg.scissor(x + 4.f, y, w - 8.f, 22.f);
    g.text(h.c_str(), x + 8.f, y + 11.5f, {11.f, Font::Sc, col::text, Align::Left});
    vg.restore();
}

void View::paint(float scale)
{
    gfx_.setScale(scale);
    auto& vg = gfx_.vg();
    vg.save();
    vg.scale(scale, scale);
    paintChassis(gfx_);
    for (auto& w : widgets_) {
        if (w->onLayout)
            w->onLayout();
        w->visible = !w->visibleIf || w->visibleIf();
        if (w->visible)
            w->paint(gfx_);
    }
    drawTextEntry(gfx_);
    paintTooltip(gfx_);
    vg.restore();
    painted_ = live_;
    paintedAt_ = clock_;
    paintedCaretOn_ = caretPhaseOn();
}

// ---------------------------------------------------------------------------------------------
// input

Pointer View::pointer(float x, float y, unsigned mods) const
{
    Pointer p;
    p.x = x;
    p.y = y;
    p.shift = (mods & DGL_NAMESPACE::kModifierShift) != 0;
    p.ctrl = (mods & (DGL_NAMESPACE::kModifierControl | DGL_NAMESPACE::kModifierSuper)) != 0;
    p.alt = (mods & DGL_NAMESPACE::kModifierAlt) != 0;
    return p;
}

Widget* View::hitTest(float x, float y) const
{
    for (auto it = widgets_.rbegin(); it != widgets_.rend(); ++it) {
        Widget* w = it->get();
        if (!w->visible || !w->interactive())
            continue;
        if (w->hits(x, y) || w->modal())
            if (w->hits(x, y))
                return w;
    }
    return nullptr;
}

void View::setHot(Widget* w, const Pointer& p)
{
    if (w != hot_) {
        if (hot_ != nullptr)
            hot_->leave();
        hot_ = w;
        hoverSince_ = clock_;
    }
    if (hot_ != nullptr)
        hot_->move(p);
}

void View::updateCursorAndHint(const Pointer& p)
{
    Widget* w = active_ != nullptr ? active_ : hot_;
    win_.winSetCursor(w != nullptr ? w->cursor(p) : DGL_NAMESPACE::kMouseCursorArrow);
    hint_ = w != nullptr ? w->hint() : std::string();
}

bool View::motion(float x, float y, unsigned mods)
{
    const Pointer p = pointer(x, y, mods);
    const float dx = x - lastPtr_.x, dy = y - lastPtr_.y;
    lastPtr_ = p;
    if (entry_.open && entry_.dragging) {
        entry_.caret = entryStopAt(x);
        entry_.blinkFrom = clock_;
        repaint();
        return true;
    }
    Widget* const wasHot = hot_;
    const std::string wasHint = hint_;
    if (active_ != nullptr) {
        active_->drag(p, dx, dy);
    } else {
        setHot(hitTest(x, y), p);
    }
    updateCursorAndHint(p);
    // Hovering must not cost a full frame per mouse event: repaint only for a drag, a new hot
    // widget or hint, a tooltip (it follows the pointer), or a widget that draws its hover from
    // the pointer position without reporting it.
    const bool tooltipShown = settings_.tooltips && hot_ != nullptr && clock_ - hoverSince_ >= 0.7;
    if (active_ != nullptr || hot_ != wasHot || hint_ != wasHint || tooltipShown || (hot_ != nullptr && hot_->repaintsOnMove()))
        repaint();
    return true;
}

bool View::mouse(int button, bool press, float x, float y, unsigned mods, unsigned timeMs)
{
    const Pointer p = pointer(x, y, mods);
    lastPtr_ = p;
    if (!press) {
        if (entry_.dragging) {
            entry_.dragging = false;
            return true;
        }
        if (active_ != nullptr && button == lastButton_) {
            Widget* a = active_;
            active_ = nullptr;
            a->up(p);
            setHot(hitTest(x, y), p);
            updateCursorAndHint(p);
            repaint();
        }
        return true;
    }
    // a click inside the entry field starts editing the text (drops the select-all, caret at the
    // end; a double-click selects everything again); a click anywhere else commits it
    if (entry_.open && button == DGL_NAMESPACE::kMouseButtonLeft && entry_.box.contains(x, y)) {
        const bool dbl = lastClickW_ == nullptr && timeMs - lastClickT_ < 320;
        if (dbl) {                    // double-click selects everything
            entry_.anchor = 0;
            entry_.caret = entry_.text.size();
        } else {                      // click places the caret, Shift+click extends, drag selects
            entry_.caret = entryStopAt(x);
            if (!p.shift)
                entry_.anchor = entry_.caret;
            entry_.dragging = true;
        }
        entry_.blinkFrom = clock_;
        lastClickT_ = timeMs;
        lastClickW_ = nullptr;
        win_.winGrabKeyboard();
        repaint();
        return true;
    }
    Widget* w = hitTest(x, y);
    if (entry_.open)
        commitEntry();
    if (w == nullptr)
        return false;
    setHot(w, p);
    if (button == DGL_NAMESPACE::kMouseButtonRight) {
        w->context(p);
        repaint();
        return true;
    }
    if (button != DGL_NAMESPACE::kMouseButtonLeft)
        return true;
    const bool dbl = w == lastClickW_ && timeMs - lastClickT_ < 320;
    lastClickW_ = dbl ? nullptr : w;
    lastClickT_ = timeMs;
    lastButton_ = button;
    active_ = w;
    if (dbl) w->dbl(p); else w->down(p);
    updateCursorAndHint(p);
    repaint();
    return true;
}

bool View::scroll(float x, float y, float dy, unsigned mods)
{
    const Pointer p = pointer(x, y, mods);
    Widget* w = hitTest(x, y);
    if (w == nullptr || std::fabs(dy) < 1e-4f)
        return false;
    w->wheel(p, dy > 0.f ? 1.f : -1.f);
    updateCursorAndHint(p);
    repaint();
    return true;
}

bool View::keyboard(unsigned key, bool press, unsigned mods)
{
    using namespace DGL_NAMESPACE;
    if (!press)
        return false;
    const Pointer p = pointer(lastPtr_.x, lastPtr_.y, mods);
    if (entry_.open) {
        entryKey(key, p);
        repaint();
        return true;
    }
    const bool ctrl = p.ctrl;
    const unsigned lk = (key >= 'A' && key <= 'Z') ? key + 32 : key;
    if (menu_->isOpen() || rate_->isOpen()) {
        if (key == kKeyEscape) { menu_->close(); rate_->close(); repaint(); return true; }
    }
    if (browser_->isOpen() && browser_->key(key, p)) {
        repaint();
        return true;
    }
    if (ctrl && lk == 'z') { if (p.shift) model_.redo(); else model_.undo(); repaint(); return true; }
    if (ctrl && lk == 'y') { model_.redo(); repaint(); return true; }
    if (ctrl && lk == 's') { savePreset(false); return true; }
    if (key == kKeyEscape && (menu_->isOpen() || rate_->isOpen() || browser_->isOpen())) {
        menu_->close(); rate_->close(); browser_->close(); repaint(); return true;
    }
    if (editor_ != nullptr && editor_->key(key, p))
        return true;
    return false;
}

bool View::character(unsigned cp, unsigned mods)
{
    if (entry_.open) {
        if (cp < 32 || cp == 127)
            return true;
        if (entry_.param >= 0) {
            const bool ok = (cp >= '0' && cp <= '9') || cp == '.' || cp == '-' || cp == 'k' || cp == 'K' || cp == ',';
            if (!ok)
                return true;
        }
        char buf[5] = {};
        if (cp < 0x80) buf[0] = char(cp == ',' && entry_.param >= 0 ? '.' : cp);
        else if (cp < 0x800) { buf[0] = char(0xC0 | (cp >> 6)); buf[1] = char(0x80 | (cp & 0x3F)); }
        else { buf[0] = char(0xE0 | (cp >> 12)); buf[1] = char(0x80 | ((cp >> 6) & 0x3F)); buf[2] = char(0x80 | (cp & 0x3F)); }
        entryInsert(buf);
        entry_.blinkFrom = clock_;
        repaint();
        return true;
    }
    if ((mods & (DGL_NAMESPACE::kModifierControl | DGL_NAMESPACE::kModifierSuper)) == 0 && browser_->isOpen() && browser_->character(cp)) {
        repaint();
        return true;
    }
    return false;
}

}} // namespace kick::ui
