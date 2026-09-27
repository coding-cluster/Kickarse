// Kickarse UI — the envelope editor view (DESIGN.md §7): grid, waveform layers, curves, nodes,
// tension handles, playhead, Quick Shift lane, tool previews, capture states. Editing itself is
// delegated to the headless editor (src/editor) through EditorBackend; this class draws and
// forwards pointer/keyboard input.
#pragma once

#include <string>
#include <vector>

#include "Widget.h"

namespace kick { namespace ui {

// What the view needs from the editing engine. Implemented over kick::editor::EditorModel +
// EditorController by View (see EditorGlue.cpp).
struct EditorPoint { float t = 0.f, v = 0.f; };

class EditorBackend {
public:
    virtual ~EditorBackend() = default;
    // geometry for drawing (edit band unless a band is given)
    virtual void  curve(int band, float plotWidthPx, std::vector<EditorPoint>& out) const = 0;
    virtual int   nodeCount(int band) const = 0;
    virtual EditorPoint nodePos(int band, int i) const = 0;
    virtual bool  isSelected(int i) const = 0;
    virtual bool  isQsMember(int band, int i) const = 0;
    virtual bool  segmentHasHandle(int seg) const = 0;
    virtual EditorPoint handlePos(int seg) const = 0;
    virtual void  gridLines(std::vector<std::pair<float, bool>>& out, std::vector<int>& index) const = 0;  // (timeline, beat)
    virtual int   quickShiftSpans(float out[4]) const = 0;       // up to two (start,end) timeline pieces
    virtual bool  quickShiftEdges(float& t0, float& t1) const = 0;
    virtual float valueAt(float timeline) const = 0;             // edit band, heard value
    virtual float divisions() const = 0;

    // interaction state for drawing
    virtual int   hoverKind() const = 0;      // 0 none, 1 empty, 2 node, 3 segment, 4 handle, 5 lane, 6 bar, 7 start, 8 end
    virtual int   hoverIndex() const = 0;
    virtual int   gestureKind() const = 0;    // 0 none, 1 nodes, 2 tension, 3 line, 4 pencil, 5 qs
    virtual int   gestureIndex() const = 0;
    virtual bool  marquee(float& t0, float& v0, float& t1, float& v1) const = 0;
    virtual bool  line(float& t0, float& v0, float& t1, float& v1) const = 0;
    virtual const std::vector<EditorPoint>& pencil() const = 0;
    virtual bool  readout(std::string& text, float& px, float& py) const = 0;
    virtual DGL_NAMESPACE::MouseCursor cursor() const = 0;

    // input (pixels in 1x logical units); clickCount 2 = double-click
    virtual bool mouseDown(float x, float y, const Pointer& p, int clickCount) = 0;
    virtual bool mouseMove(float x, float y, const Pointer& p) = 0;
    virtual bool mouseUp(float x, float y, const Pointer& p) = 0;
    virtual bool key(unsigned key, const Pointer& p) = 0;
    virtual void setGeometry(const RectF& plot, const RectF& lane, bool laneVisible) = 0;
    virtual void setTool(int tool) = 0;
    virtual void contextMenu(float x, float y, float px, float py) = 0;
};

class EnvelopeEditor : public Widget {
public:
    EnvelopeEditor(Services& s, EditorBackend& backend);

    // Shape changes (library click, preset load, capture, reset) cross-fade for 180 ms.
    void startMorph(const Envelope& from, const Envelope& to);
    // Library hover preview (dashed ghost); nullptr hides it.
    void setPreview(const Envelope* e) { preview_ = e; }
    void flashCaptured() { capturedFlash_ = 1.f; }

    void paint(Gfx& g) override;
    bool animating() const override;
    void tick(float dt);

    void down(const Pointer& p) override;
    void drag(const Pointer& p, float dx, float dy) override;
    void up(const Pointer& p) override;
    void dbl(const Pointer& p) override;
    void move(const Pointer& p) override;
    bool repaintsOnMove() const override { return false; }
    void leave() override;
    void context(const Pointer& p) override;
    bool key(unsigned key, const Pointer& p) override;
    DGL_NAMESPACE::MouseCursor cursor(const Pointer& p) const override;
    std::string hint() const override;

private:
    float X(float timeline) const { return plot_.x + timeline * plot_.w; }
    float Y(float v) const { return plot_.y + (1.f - v) * plot_.h; }
    PhaseMap phaseMap() const;
    int   band() const;
    Rgba  bandColour() const;
    float effectiveDepth() const;

    void drawGrid(Gfx& g, bool labels);
    void drawWaves(Gfx& g);
    void samplePolyline(const Envelope& e, const PhaseMap& map, std::vector<EditorPoint>& out) const;
    void drawPolyline(Gfx& g, const std::vector<EditorPoint>& pts, const Rgba& col, float width, bool fill, float alpha, bool dashed = false);
    void drawNodes(Gfx& g, const Rgba& col);
    void drawQuickShift(Gfx& g, const Rgba& col);
    void drawPreviews(Gfx& g, const Rgba& col);
    void drawSmoothed(Gfx& g, const Rgba& col);

    EditorBackend& be_;
    RectF plot_;
    std::vector<EditorPoint> pts_, other_, ghost_, smooth_;
    std::vector<float> smoothIn_;
    std::vector<std::pair<float, bool>> grid_;
    std::vector<int> gridIndex_;
    bool     morphing_ = false;
    float    morphT_ = 0.f;
    std::vector<float> morphFrom_, morphTo_;
    const Envelope* preview_ = nullptr;
    float    capturedFlash_ = 0.f;
};

}} // namespace kick::ui
