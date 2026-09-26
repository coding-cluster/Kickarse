// Kickarse UI — in-plugin interaction self-test (debug only, KICKARSE_UI_SELFTEST=<report file>).
// Drives View's real input entry points with synthetic events inside a paint frame and checks the
// model / editor state: the C++ counterpart of the prototype's ?test=1 (DESIGN.md §12).
#include <cmath>
#include <cstdio>
#include <string>

#include "Library.h"
#include "Overlays.h"
#include "PresetBrowser.h"
#include "View.h"

#include "shared/Params.h"

namespace kick { namespace ui {

std::string View::runSelfTest(float scale)
{
    std::string rep;
    int pass = 0, fail = 0;
    auto check = [&](const char* name, bool ok, const std::string& extra = {}) {
        (ok ? pass : fail)++;
        rep += std::string(ok ? "PASS  " : "FAIL  ") + name + (extra.empty() ? "" : "  (" + extra + ")") + "\n";
    };
    unsigned t = 100000;
    auto refresh = [&] { paint(scale); };
    auto move = [&](float x, float y, unsigned m = 0) { motion(x, y, m); };
    auto click = [&](float x, float y, unsigned m = 0) {
        refresh(); move(x, y, m); mouse(1, true, x, y, m, t); mouse(1, false, x, y, m, t); t += 1000; refresh();
    };
    auto dbl = [&](float x, float y) {
        refresh(); move(x, y); mouse(1, true, x, y, 0, t); mouse(1, false, x, y, 0, t); t += 50;
        mouse(1, true, x, y, 0, t); mouse(1, false, x, y, 0, t); t += 1000; refresh();
    };
    auto drag = [&](float x0, float y0, float x1, float y1, unsigned m = 0) {
        refresh(); move(x0, y0, m); mouse(1, true, x0, y0, m, t);
        for (int i = 1; i <= 8; ++i) move(x0 + (x1 - x0) * float(i) / 8.f, y0 + (y1 - y0) * float(i) / 8.f, m);
        mouse(1, false, x1, y1, m, t); t += 1000; refresh();
    };
    auto fmt = [](float v) { char b[32]; std::snprintf(b, sizeof(b), "%.2f", double(v)); return std::string(b); };
    const unsigned kShift = DGL_NAMESPACE::kModifierShift, kCtrl = DGL_NAMESPACE::kModifierControl, kAlt = DGL_NAMESPACE::kModifierAlt;
    const RectF P = layout::plot;
    auto X = [&](float tl) { return P.x + tl * P.w; };
    auto Y = [&](float v) { return P.y + (1.f - v) * P.h; };

    // ---- knob
    model_.setNoUndo(kParamDepth, 50.f);
    drag(980.f, 150.f, 980.f, 100.f);
    check("knob: vertical drag raises Depth", std::fabs(model_.value(kParamDepth) - (50.f + 50.f / 260.f * 100.f)) < 0.8f, fmt(model_.value(kParamDepth)));
    const float d1 = model_.value(kParamDepth);
    drag(980.f, 150.f, 980.f, 140.f, kShift);
    check("knob: Shift = fine", std::fabs(model_.value(kParamDepth) - (d1 + 10.f / 260.f * 10.f)) < 0.3f, fmt(model_.value(kParamDepth)));
    dbl(980.f, 150.f);
    check("knob: double-click resets", model_.value(kParamDepth) == 100.f);
    model_.setNoUndo(kParamDepth, 30.f);
    click(980.f, 150.f, kCtrl);
    check("knob: Ctrl-click resets", model_.value(kParamDepth) == 100.f);
    click(980.f, 150.f, kAlt);
    character('4', 0); character('2', 0); keyboard(DGL_NAMESPACE::kKeyEnter, true, 0);
    check("knob: Alt-click type-in + Enter", model_.value(kParamDepth) == 42.f, fmt(model_.value(kParamDepth)));
    check("undo available after knob edits", model_.canUndo());
    model_.undo();
    check("undo reverts the typed value", model_.value(kParamDepth) != 42.f, fmt(model_.value(kParamDepth)));
    model_.redo();
    check("redo re-applies it", model_.value(kParamDepth) == 42.f, fmt(model_.value(kParamDepth)));

    // ---- segmented / lamps / stepper / rate grid
    click(160.f, 223.f);   // "Spectral" in the mode segmented
    check("mode segmented selects Spectral", model_.ivalue(kParamMode) == kModeSpectral, std::to_string(model_.ivalue(kParamMode)));
    click(36.f, 223.f);
    check("mode segmented selects Sync", model_.ivalue(kParamMode) == kModeSync);
    click(1030.f, 262.f);
    check("Split lamp toggles multiband", model_.on(kParamMulti));
    click(1030.f, 262.f);
    const int r0 = model_.ivalue(kParamRate);
    click(210.f, 95.f);
    check("rate stepper: next = shorter", model_.ivalue(kParamRate) == r0 + 1);
    click(122.f, 95.f);
    check("rate value opens the grid", rate_->isOpen());
    click(16.f + 60.f + 5.f * 42.f + 21.f, 118.f + 26.f + 13.f);   // Straight · 1/8
    check("rate grid picks 1/8", model_.ivalue(kParamRate) == 9 && !rate_->isOpen(), std::to_string(model_.ivalue(kParamRate)));

    // ---- editor (through the headless EditorModel)
    const int n0 = model_.env(0).size();
    dbl(X(0.85f), Y(0.45f));
    check("editor: double-click adds a node", model_.env(0).size() == n0 + 1, std::to_string(n0) + " -> " + std::to_string(model_.env(0).size()));
    model_.undo();
    check("editor: undo removes it", model_.env(0).size() == n0);
    // bend the first segment
    const Envelope e0 = model_.env(0);
    const float q = e0.node(1).x * 0.5f;
    drag(X(q), Y(e0.evaluateLeft(q)), X(q), Y(e0.evaluateLeft(q)) - 40.f);
    check("editor: dragging a segment bends it", model_.env(0).node(0).tension != e0.node(0).tension,
          fmt(e0.node(0).tension) + " -> " + fmt(model_.env(0).node(0).tension));
    // move the middle node with Ctrl (free)
    const float mx = e0.node(1).x;
    drag(X(mx), Y(e0.node(1).y), X(mx) - 30.f, Y(e0.node(1).y) + 20.f, kCtrl);
    check("editor: Ctrl-drag moves a node off-grid", std::fabs(model_.env(0).node(1).x - mx) > 0.02f, fmt(mx) + " -> " + fmt(model_.env(0).node(1).x));
    // marquee + delete
    drag(X(0.02f), P.y + 4.f, X(0.98f), P.bottom() - 4.f);
    keyboard(DGL_NAMESPACE::kKeyDelete, true, 0);
    check("editor: marquee + Delete keeps only the endpoints", model_.env(0).size() == 2, std::to_string(model_.env(0).size()));
    model_.undo();
    check("editor: undo restores the nodes", model_.env(0).size() >= 3, std::to_string(model_.env(0).size()));

    // ---- library
    if (bank_ != nullptr && !bank_->items().empty()) {
        click(256.f + 76.5f * 2.f + 38.f, 494.f + 32.f);
        const Envelope& want = bank_->items()[2].env;
        const Envelope& got = model_.env(0);
        bool same = want.size() == got.size();
        for (int i = 0; same && i < got.size(); ++i)
            same = std::fabs(want.node(i).x - got.node(i).x) < 1e-3f && std::fabs(want.node(i).y - got.node(i).y) < 1e-3f;
        check("library click applies the shape", same);
    } else {
        check("library has shapes", false);
    }

    // ---- threshold, crossover, presets, capture
    click(160.f, 223.f - 0.f);   // Spectral
    click(115.f, 223.f);         // Audio
    const float th0 = model_.value(kParamThreshold);
    drag(16.f + 106.f, 248.f + 38.f, 16.f + 136.f, 248.f + 38.f);
    check("threshold meter drag", model_.value(kParamThreshold) > th0, fmt(th0) + " -> " + fmt(model_.value(kParamThreshold)));
    click(36.f, 223.f);
    model_.setNoUndo(kParamMulti, 1.f);
    const float xo = model_.value(kParamCrossover);
    drag(980.f, 315.f, 1010.f, 315.f);
    check("crossover drag is relative", model_.value(kParamCrossover) > xo * 1.5f, fmt(xo) + " -> " + fmt(model_.value(kParamCrossover)));
    model_.setNoUndo(kParamMulti, 0.f);
    click(273.f, 22.f);
    check("preset browser opens", browser_->isOpen());
    keyboard(DGL_NAMESPACE::kKeyDown, true, 0);
    check("Down loads a preset", !model_.presetId.empty(), model_.presetId);
    keyboard(DGL_NAMESPACE::kKeyEscape, true, 0);
    check("Esc closes the browser", !browser_->isOpen());
    click(822.f, 436.f);
    check("capture arms", live_.recState == 1);
    click(822.f, 436.f);
    check("capture cancels", live_.recState == 0);

    rep = "SELFTEST " + std::string(fail ? "FAILED " : "OK ") + std::to_string(pass) + "/" + std::to_string(pass + fail) + "\n" + rep;
    return rep;
}

}} // namespace kick::ui
