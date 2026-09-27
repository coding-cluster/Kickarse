// Kickarse UI — the root view: owns the widgets, overlays, preset/shape stores and the Live copy
// of the Bridge; routes DGL input (already converted to 1x logical coordinates) and paints the
// whole surface under one uniform scale. plugin/KickarseUI.cpp is a thin DPF adapter around it.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "shared/Presets.h"
#include "shared/Shapes.h"

#include "DemoFeed.h"
#include "EditorView.h"
#include "Widget.h"

namespace kick { struct Bridge; }

namespace kick { namespace ui {

// Window-level services the DPF UI provides.
class WindowHost {
public:
    virtual ~WindowHost() = default;
    virtual void winRepaint() = 0;
    virtual void winSetCursor(DGL_NAMESPACE::MouseCursor c) = 0;
    virtual void winSetUserScale(float s) = 0;   // resize the window to 1080·s·host × 660·s·host
    virtual float winUserScale() const = 0;
    virtual void winGrabKeyboard() {}   // take keyboard focus from the host (text entry)
};

struct Resources {
    DGL_NAMESPACE::NanoVG::FontId sc = -1, scSemi = -1, exp = -1;
    DGL_NAMESPACE::NanoImage knobHero, knobSmall, grain;
};

class MenuOverlay;
class RateGridOverlay;
class PresetBrowser;
class EnvelopeEditor;
class ShapeBank;
class LocalEditorBackend;
class EditorGlue;

class View final : public Services {
public:
    View(HostIO& host, WindowHost& win, DGL_NAMESPACE::NanoVG& vg);
    ~View() override;

    Resources& resources() noexcept { return res_; }
    void init();   // after fonts/images are loaded: builds the widget tree

    // ---- DPF entry points (coordinates in 1x logical pixels) -------------------------------
    void paint(float scale);
    bool mouse(int button, bool press, float x, float y, unsigned mods, unsigned timeMs);
    bool motion(float x, float y, unsigned mods);
    bool scroll(float x, float y, float dy, unsigned mods);
    bool keyboard(unsigned key, bool press, unsigned mods);
    bool character(unsigned codepoint, unsigned mods);
    // Poll the Bridge (nullptr = no DSP, e.g. demo); returns true when something needs painting.
    bool idle(Bridge* bridge, double dt);
    void parameterChanged(int id, float value);
    void stateChanged(const char* key, const char* value);

    // ---- Services ------------------------------------------------------------------------------
    Model&        model() override { return model_; }
    const Live&   live() const override { return live_; }
    Settings&     settings() override { return settings_; }
    Gfx&          gfx() override { return gfx_; }
    PresetStore*  presets() override { return &presets_; }
    ShapeLibrary* shapes() override { return &shapes_; }
    const DGL_NAMESPACE::NanoImage& image(Img i) const override;
    double now() const override { return clock_; }
    void   repaint() override { dirty_ = true; win_.winRepaint(); }

    void openMenu(float x, float y, std::vector<MenuItem> items, float rightEdge = -1.f, bool above = false) override;
    void openRateGrid(float x, float y) override;
    void openPresetBrowser(bool open) override;
    bool presetBrowserOpen() const override;
    void openEntry(int paramId) override;
    bool entryOpenFor(int paramId) const override { return entry_.open && entry_.param == paramId; }
    void drawEntry(Gfx& g, float cx, float cy, float size) override;
    void paramMenu(int paramId, float x, float y) override;
    void applyShape(const Envelope& e) override;
    void loadPreset(const std::string& id) override;
    void savePreset(bool saveAs) override;
    void setScale(float s) override;
    void capture() override;

    void openTextEntry(const std::string& initial, float cx, float cy, float width,
                       std::function<void(const std::string&)> commit) override;
    bool textEntryOpen() const override { return entry_.open && entry_.param < 0; }
    void drawTextEntry(Gfx& g) override;

    std::vector<MenuItem> settingsMenu(bool sizesOnly);

    // Hooks installed by EditorGlue when the headless editor is available.
    std::function<void()> onSnapChanged, onBandChanged;
    std::function<void(bool linked)> onLinkToggle;
    std::function<void(const Envelope&)> onApplyShape;
    std::function<void(const PresetData&)> onLoadPreset;
    std::function<bool(const float* buf, int bins, Envelope& out)> onCapture;
    EnvelopeEditor* editorWidget() noexcept { return editor_; }
    void setEditorBackend(EditorBackend* b);   // before init()
    // Debug: pin the playhead in demo mode (snapshots)
    void setDemoFreeze(float phase) { demoFreeze_ = phase; }
    bool demoMode() const noexcept { return demoMode_; }
    void applyDebugState(const std::string& shot);
    std::string runSelfTest(float scale);            // KICKARSE_UI_SELFTEST (see SelfTest.cpp)   // KICKARSE_UI_SHOT states (snapshots)

private:
    template <class T, class... A> T* add(A&&... args)
    {
        auto w = std::make_unique<T>(std::forward<A>(args)...);
        T* raw = w.get();
        widgets_.push_back(std::move(w));
        return raw;
    }
    void build();
    void buildHeader();
    void buildLeft();
    void buildModePanels();
    void buildCentre();
    void buildRight();
    void buildFooter();
    void buildOverlays();

    Widget* hitTest(float x, float y) const;
    Pointer pointer(float x, float y, unsigned mods) const;
    void    setHot(Widget* w, const Pointer& p);
    void    updateCursorAndHint(const Pointer& p);
    void    commitEntry();
    void    cancelEntry();
    void    entryKey(unsigned key, const Pointer& p);   // editing keys while an entry is open
    void    entryInsert(const std::string& s);
    void    entryErase(std::size_t from, std::size_t to);
    std::size_t entryStopAt(float x) const;              // nearest caret position to x
    bool    caretPhaseOn() const;
    void    paintChassis(Gfx& g);
    void    paintTooltip(Gfx& g);
    void    pollBridge(Bridge* bridge, double dt);
    void    handleCapture(Bridge* bridge);
    void    finishCapture(const float* buf, int bins);
    void    saveUserPreset(const std::string& name, bool overwrite);
    static bool liveMoved(const Live& a, const Live& b, bool atRest);
    bool    envelopeAtRest() const;   // Sync mode, host stopped: the editor shows no playhead

    HostIO&      host_;
    WindowHost&  win_;
    Gfx          gfx_;
    Resources    res_;
    Model        model_;
    Settings     settings_;
    Live         live_;
    PresetStore  presets_;
    ShapeLibrary shapes_;
    DemoFeed     demo_;
    bool         demoMode_ = false;

    std::vector<std::unique_ptr<Widget>> widgets_;
    Widget* hot_    = nullptr;
    Widget* active_ = nullptr;
    Widget* lastClickW_ = nullptr;
    unsigned lastClickT_ = 0;
    int     lastButton_ = 0;
    Pointer lastPtr_;
    double  hoverSince_ = 0.0;
    std::string hint_;

    MenuOverlay*     menu_    = nullptr;
    RateGridOverlay* rate_    = nullptr;
    PresetBrowser*   browser_ = nullptr;
    EnvelopeEditor*  editor_  = nullptr;
    ShapeBank*       bank_    = nullptr;
    std::unique_ptr<LocalEditorBackend> localBackend_;
    EditorBackend*   editorBackend_ = nullptr;
    EditorBackend*   externalBackend_ = nullptr;
    Bridge*          bridge_ = nullptr;
    bool             learn_ = false;
    int              demoRec_ = 0;
    float            demoPrevPhase_ = 0.f;
    float            demoFreeze_ = -1.f;

    struct Entry {
        bool open = false;
        int  param = -1;          // -1 = free text entry
        std::string text;
        // caret and selection as UTF-8 byte offsets on code point boundaries; the selection is
        // [min(anchor, caret), max(anchor, caret)), empty when they are equal
        std::size_t caret = 0, anchor = 0;
        bool dragging = false;    // mouse-selecting inside the field
        double blinkFrom = 0.0;   // caret blink restarts (visible) on every edit or move
        float cx = 0.f, cy = 0.f, width = 0.f;
        RectF box;                // where the field was last drawn: clicks inside it edit, not commit
        std::vector<std::size_t> stops;   // caret positions (byte offsets) as last drawn ...
        std::vector<float> stopX;         // ... and their x
        std::function<void(const std::string&)> commit;
    } entry_;

    // Live displays, blinking carets and fades repaint at most this often; interaction is immediate.
    static constexpr double kAmbientFps = 30.0;

    double clock_ = 0.0;
    bool   dirty_ = true;
    Live   painted_;              // live_ as of the last paint, to skip repaints nothing would change
    double paintedAt_ = -1.0;
    bool   paintedCaretOn_ = false;
    std::uint32_t lastRevision_ = 0;
    std::uint32_t lastTrig_ = 0, lastNotes_ = 0;
    bool   firstPoll_ = true;
    float  savedFlash_ = 0.f;
    std::string saveMsg_ = "Saved";   // header status next to Save: "Saved", "Name taken", ...
    std::string clipboard_;
    bool   hasClip_ = false;
    float  clipValue_ = 0.f;

    friend class MenuOverlay;
    friend class RateGridOverlay;
    friend class PresetBrowser;
};

}} // namespace kick::ui
