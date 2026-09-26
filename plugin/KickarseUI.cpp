// Kickarse -- DPF UI adapter. All drawing, layout and interaction live in src/ui (kick::ui::View,
// see docs/design/DESIGN.md); this class only bridges DPF: parameters/state in and out, the Bridge
// via direct access, mouse/keyboard events (window pixels -> 1x logical units), window size and
// cursor, fonts/images from the embedded resources, and a debug snapshot hook:
//   KICKARSE_UI_SNAPSHOT=<file.bmp>  write the rendered UI after ~20 frames
//   KICKARSE_UI_SHOT=<state>         deterministic demo state (sync, midi, audio, spectral, ...)
//   KICKARSE_UI_SCALE=<1..2>         UI size for the snapshot
//   KICKARSE_UI_SNAPSHOT_EXIT=1      exit the (standalone) process after writing the snapshot
//   KICKARSE_UI_DEMO=1               synthetic Bridge data (implied by KICKARSE_UI_SHOT)
//   KICKARSE_UI_SELFTEST=<file.txt>  drive the real input path with synthetic events, write a report
#include "DistrhoUI.hpp"
#include "KickarsePlugin.hpp"
#include "KickarseResources.hpp"

#include "OpenGL-include.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "ui/EditorGlue.h"
#include "ui/View.h"

START_NAMESPACE_DISTRHO

namespace {

const kick::res::Resource* findResource(const kick::res::Resource* table, std::size_t count, const char* name)
{
    for (std::size_t i = 0; i < count; ++i)
        if (table[i].name != nullptr && std::strcmp(table[i].name, name) == 0)
            return &table[i];
    return nullptr;
}

double nowSeconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool writeBmp(const char* path, int w, int h, const std::vector<unsigned char>& rgba)
{
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr)
        return false;
    const int rowBytes = (w * 3 + 3) & ~3;
    const unsigned fileSize = 54u + unsigned(rowBytes * h);
    unsigned char hdr[54] = {'B', 'M'};
    auto put32 = [&](int off, unsigned v) { hdr[off] = (unsigned char)(v); hdr[off + 1] = (unsigned char)(v >> 8); hdr[off + 2] = (unsigned char)(v >> 16); hdr[off + 3] = (unsigned char)(v >> 24); };
    put32(2, fileSize);
    put32(10, 54);
    put32(14, 40);
    put32(18, unsigned(w));
    put32(22, unsigned(h));
    hdr[26] = 1;
    hdr[28] = 24;
    put32(34, unsigned(rowBytes * h));
    std::fwrite(hdr, 1, 54, f);
    std::vector<unsigned char> row(std::size_t(rowBytes), 0);
    for (int y = 0; y < h; ++y) {   // GL rows are bottom-up, like BMP
        for (int x = 0; x < w; ++x) {
            const unsigned char* p = &rgba[(std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4];
            row[std::size_t(x) * 3 + 0] = p[2];
            row[std::size_t(x) * 3 + 1] = p[1];
            row[std::size_t(x) * 3 + 2] = p[0];
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    return true;
}

} // namespace

// -----------------------------------------------------------------------------------------------

class KickarseUI : public UI, public kick::ui::HostIO, public kick::ui::WindowHost
{
public:
    KickarseUI()
        : UI(uint(kick::ui::kBaseW), uint(kick::ui::kBaseH))
    {
        const char* shot = std::getenv("KICKARSE_UI_SHOT");
        if (shot != nullptr && std::getenv("KICKARSE_UI_DEMO") == nullptr)
            _putenv_s("KICKARSE_UI_DEMO", "1");

        fView = std::make_unique<kick::ui::View>(*this, *this, *this);
        fGlue = std::make_unique<kick::ui::EditorGlue>(*fView);
        fView->setEditorBackend(fGlue.get());

        kick::ui::Resources& r = fView->resources();
        auto font = [&](const char* resName, const char* face) -> FontId {
            const kick::res::Resource* res = findResource(kick::res::kFonts, kick::res::kFontCount, resName);
            return res != nullptr ? createFontFromMemory(face, res->data, uint(res->size), false) : FontId(-1);
        };
        r.sc = font("ArchivoSC-Medium", "sc");
        r.scSemi = font("ArchivoSC-SemiBold", "sc-semi");
        r.exp = font("ArchivoExp-SemiBold", "exp");
        if (r.sc < 0) {   // resources missing: fall back to DPF's DejaVu so text still renders
            loadSharedResources();
            r.sc = r.scSemi = r.exp = findFont(NANOVG_DEJAVU_SANS_TTF);
        }
        if (r.scSemi < 0) r.scSemi = r.sc;
        if (r.exp < 0) r.exp = r.sc;
        auto img = [&](NanoImage& dst, const char* resName, int flags) {
            if (const kick::res::Resource* res = findResource(kick::res::kImages, kick::res::kImageCount, resName))
                dst = createImageFromMemory(res->data, uint(res->size), flags);
        };
        img(r.knobHero, "knob_hero@2x", IMAGE_GENERATE_MIPMAPS);
        img(r.knobSmall, "knob_small@2x", IMAGE_GENERATE_MIPMAPS);
        img(r.grain, "grain_256", IMAGE_REPEAT_X | IMAGE_REPEAT_Y);

        fView->init();

        if (const char* s = std::getenv("KICKARSE_UI_SNAPSHOT"))
            fSnapshotPath = s;
        float userScale = fView->settings().scale;
        if (const char* s = std::getenv("KICKARSE_UI_SCALE"))
            userScale = float(std::atof(s));
        if (shot != nullptr)
            fView->applyDebugState(shot);

        fUserScale = std::fmin(2.f, std::fmax(1.f, userScale));
        applySize();
        fLastIdle = nowSeconds();
    }

    // -- HostIO ------------------------------------------------------------------------------
    void hostEditParameter(uint32_t id, bool started) override { editParameter(id, started); }
    void hostSetParameter(uint32_t id, float value) override { setParameterValue(id, value); }
    void hostSetState(const char* key, const char* value) override { setState(key, value); }

    // -- WindowHost --------------------------------------------------------------------------
    void winRepaint() override { repaint(); }
    void winSetCursor(DGL_NAMESPACE::MouseCursor c) override
    {
        if (c != fCursor) {
            fCursor = c;
            setCursor(c);
        }
    }
    void winSetUserScale(float s) override
    {
        fUserScale = s;
        applySize();
    }
    float winUserScale() const override { return fUserScale; }
    // Embedded windows only get keys once they hold the focus; without this, typing into a text
    // field (e.g. a new preset's name) went to the host.
    void winGrabKeyboard() override { getWindow().focus(); }

protected:
    // -- DSP/Plugin callbacks -------------------------------------------------------------------
    void parameterChanged(uint32_t index, float value) override { fView->parameterChanged(int(index), value); }
    void stateChanged(const char* key, const char* value) override { fView->stateChanged(key, value); }

    void uiScaleFactorChanged(double) override { applySize(); }

    // -- UI callbacks ---------------------------------------------------------------------------
    void uiIdle() override
    {
        const double now = nowSeconds();
        const double dt = std::fmin(0.1, std::fmax(0.0, now - fLastIdle));
        fLastIdle = now;
        kick::Bridge* bridge = nullptr;
        if (auto* plugin = static_cast<KickarsePlugin*>(getPluginInstancePointer()))
            bridge = &plugin->engine().bridge();
        if (fSizeChecks > 0 && fFrames > 0)
            enforceUserSize();
        if (fView->idle(bridge, dt) || !fSnapshotPath.empty() || fFrames < 12)
            repaint();
    }

    void onNanoDisplay() override
    {
        const float k = scale();
        fView->paint(k);
        fLastPaint = nowSeconds();
        ++fFrames;
        if (fFrames == 10) {
            if (const char* path = std::getenv("KICKARSE_UI_SELFTEST")) {
                const std::string rep = fView->runSelfTest(k);
                if (std::FILE* f = std::fopen(path, "wb")) {
                    std::fwrite(rep.data(), 1, rep.size(), f);
                    std::fclose(f);
                }
#ifdef _WIN32
                if (fSnapshotPath.empty())
                    ExitProcess(0);
#endif
            }
        }
        if (!fSnapshotPath.empty() && fFrames == 20)
            snapshot();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        const float k = scale();
        return fView->mouse(int(ev.button), ev.press, float(ev.pos.getX()) / k, float(ev.pos.getY()) / k, ev.mod, ev.time);
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float k = scale();
        if (const char* log = std::getenv("KICKARSE_UI_DEBUGLOG")) {
            if (std::FILE* f = std::fopen(log, "a")) {
                std::fprintf(f, "motion pos=%.1f,%.1f widget=%ux%u window=%ux%u host=%.3f user=%.2f k=%.3f\n",
                             ev.pos.getX(), ev.pos.getY(), getWidth(), getHeight(), getWindow().getWidth(),
                             getWindow().getHeight(), getScaleFactor(), fUserScale, k);
                std::fclose(f);
            }
        }
        const bool handled = fView->motion(float(ev.pos.getX()) / k, float(ev.pos.getY()) / k, ev.mod);
        keepAnimatingDuringInput();
        return handled;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float k = scale();
        return fView->scroll(float(ev.pos.getX()) / k, float(ev.pos.getY()) / k, float(ev.delta.getY()), ev.mod);
    }

    bool onKeyboard(const KeyboardEvent& ev) override { return fView->keyboard(ev.key, ev.press, ev.mod); }

    bool onCharacterInput(const CharacterInputEvent& ev) override { return fView->character(ev.character, ev.mod); }

private:
    float scale() const { return float(getWidth()) / kick::ui::kBaseW; }

    // Windows hands out WM_TIMER (our uiIdle) and WM_PAINT only when no input is queued, so a moving
    // mouse starved both and the live displays froze while hovering. Drive the idle tick from input
    // and paint synchronously, at most ~60 times per second.
    void keepAnimatingDuringInput()
    {
        const double now = nowSeconds();
        if (now - fLastIdle >= 1.0 / 60.0)
            uiIdle();
#ifdef _WIN32
        if (now - fLastPaint >= 1.0 / 60.0)
            if (HWND hwnd = reinterpret_cast<HWND>(getWindow().getNativeWindowHandle()))
                UpdateWindow(hwnd);
#endif
    }

    uint wantedWidth() const { return uint(std::lround(kick::ui::kBaseW * getScaleFactor() * double(fUserScale))); }

    // Hosts size the editor frame before the UI exists, from DISTRHO_UI_DEFAULT_WIDTH/HEIGHT and
    // (often) without the display scale factor, then apply that size after we asked for ours, so
    // the editor opened smaller than the same "100 %" picked later from the menu. Once the window
    // is up, check the size a few times and ask again when it does not match the saved UI size.
    void enforceUserSize()
    {
        --fSizeChecks;
        const uint w = wantedWidth();
        if (getWindow().getWidth() + 1 < w || getWindow().getWidth() > w + 1)
            applySize();
    }

    void applySize()
    {
        const double host = getScaleFactor();
        // the minimum follows the display scale factor, which some hosts only announce after opening
        setGeometryConstraints(uint(std::lround(kick::ui::kBaseW * host)), uint(std::lround(kick::ui::kBaseH * host)), true);
        const double f = host * double(fUserScale);
        const uint w = wantedWidth(), h = uint(std::lround(kick::ui::kBaseH * f));
        setSize(w, h);
        // When the host announces a scale factor, DPF creates the window already scaled; setSize then
        // changes nothing, no configure event arrives and the widget keeps its unscaled size. NanoVG and
        // our pointer mapping use the widget size, so the UI rendered stretched and every click landed
        // 25 % off at 125 % display scaling. Keep the widget in sync with the real window.
        if (getWidth() != getWindow().getWidth() || getHeight() != getWindow().getHeight())
            Widget::setSize(getWindow().getWidth(), getWindow().getHeight());
        if (const char* log = std::getenv("KICKARSE_UI_DEBUGLOG")) {
            if (std::FILE* fl = std::fopen(log, "a")) {
                std::fprintf(fl, "applySize host=%.3f user=%.2f want=%ux%u -> widget=%ux%u window=%ux%u\n", getScaleFactor(),
                             fUserScale, w, h, getWidth(), getHeight(), getWindow().getWidth(), getWindow().getHeight());
                std::fclose(fl);
            }
        }
        repaint();
    }

    void snapshot()
    {
        NanoVG::endFrame();   // flush NanoVG now; DGL's own endFrame afterwards is a harmless no-op
        glFinish();
        const int w = int(getWidth()), h = int(getHeight());
        std::vector<unsigned char> px(std::size_t(w) * std::size_t(h) * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        const bool ok = writeBmp(fSnapshotPath.c_str(), w, h, px);
        std::fprintf(stderr, "Kickarse UI snapshot %s (%dx%d): %s\n", fSnapshotPath.c_str(), w, h, ok ? "ok" : "failed");
#ifdef _WIN32
        if (std::getenv("KICKARSE_UI_SNAPSHOT_EXIT") != nullptr)
            ExitProcess(ok ? 0u : 1u);
#endif
        fSnapshotPath.clear();
    }

    std::unique_ptr<kick::ui::View> fView;
    std::unique_ptr<kick::ui::EditorGlue> fGlue;
    float  fUserScale = 1.f;
    double fLastIdle = 0.0;
    double fLastPaint = 0.0;
    DGL_NAMESPACE::MouseCursor fCursor = DGL_NAMESPACE::kMouseCursorArrow;
    std::string fSnapshotPath;
    int    fFrames = 0;
    int    fSizeChecks = 8;   // idle ticks after the first paint that re-assert the saved size

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KickarseUI)
};

// -----------------------------------------------------------------------------------------------

UI* createUI()
{
    return new KickarseUI();
}

END_NAMESPACE_DISTRHO
