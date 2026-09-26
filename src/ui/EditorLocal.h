// Kickarse UI — display-only EditorBackend over the Model's envelopes. Used when the headless
// editor library (src/editor) is not linked into this build; it draws everything but does not
// edit. The real backend is EditorGlue.
#pragma once

#include "EditorView.h"

namespace kick { namespace ui {

class LocalEditorBackend final : public EditorBackend {
public:
    explicit LocalEditorBackend(Model& m) : m_(m) {}

    void  curve(int band, float plotWidthPx, std::vector<EditorPoint>& out) const override;
    int   nodeCount(int band) const override { return m_.env(band).size(); }
    EditorPoint nodePos(int band, int i) const override;
    bool  isSelected(int) const override { return false; }
    bool  isQsMember(int band, int i) const override { return (m_.env(band).node(i).flags & kNodeQuickShift) != 0; }
    bool  segmentHasHandle(int seg) const override;
    EditorPoint handlePos(int seg) const override;
    void  gridLines(std::vector<std::pair<float, bool>>& out, std::vector<int>& index) const override;
    int   quickShiftSpans(float out[4]) const override;
    bool  quickShiftEdges(float& t0, float& t1) const override;
    float valueAt(float timeline) const override;
    float divisions() const override { return map().divisions; }

    int   hoverKind() const override { return 0; }
    int   hoverIndex() const override { return -1; }
    int   gestureKind() const override { return 0; }
    int   gestureIndex() const override { return -1; }
    bool  marquee(float&, float&, float&, float&) const override { return false; }
    bool  line(float&, float&, float&, float&) const override { return false; }
    const std::vector<EditorPoint>& pencil() const override { return pen_; }
    bool  readout(std::string&, float&, float&) const override { return false; }
    DGL_NAMESPACE::MouseCursor cursor() const override { return DGL_NAMESPACE::kMouseCursorArrow; }

    bool mouseDown(float, float, const Pointer&, int) override { return false; }
    bool mouseMove(float, float, const Pointer&) override { return false; }
    bool mouseUp(float, float, const Pointer&) override { return false; }
    bool key(unsigned, const Pointer&) override { return false; }
    void setGeometry(const RectF&, const RectF&, bool) override {}
    void setTool(int) override {}
    void contextMenu(float, float, float, float) override {}

private:
    PhaseMap map() const;
    float    timelineOf(float x) const;
    Model& m_;
    std::vector<EditorPoint> pen_;
};

}} // namespace kick::ui
