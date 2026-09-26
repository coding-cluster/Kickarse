// Kickarse UI — binds the headless editor (src/editor: EditorModel + EditorController) to the
// view: it is the EditorBackend the EnvelopeEditor draws from, the History the Model records
// knob edits into, and the EditorListener that mirrors envelope changes to the host.
#pragma once

#include <memory>
#include <vector>

#include "editor/EditorController.h"
#include "editor/EditorModel.h"

#include "EditorView.h"
#include "Model.h"

namespace kick { namespace ui {

class View;

class EditorGlue final : public EditorBackend, public History, public editor::EditorListener {
public:
    explicit EditorGlue(View& view);
    ~EditorGlue() override;

    // ---- EditorBackend ------------------------------------------------------------------------
    void  curve(int band, float plotWidthPx, std::vector<EditorPoint>& out) const override;
    int   nodeCount(int band) const override;
    EditorPoint nodePos(int band, int i) const override;
    bool  isSelected(int i) const override;
    bool  isQsMember(int band, int i) const override;
    bool  segmentHasHandle(int seg) const override;
    EditorPoint handlePos(int seg) const override;
    void  gridLines(std::vector<std::pair<float, bool>>& out, std::vector<int>& index) const override;
    int   quickShiftSpans(float out[4]) const override;
    bool  quickShiftEdges(float& t0, float& t1) const override;
    float valueAt(float timeline) const override;
    float divisions() const override;
    int   hoverKind() const override;
    int   hoverIndex() const override;
    int   gestureKind() const override;
    int   gestureIndex() const override;
    bool  marquee(float& t0, float& v0, float& t1, float& v1) const override;
    bool  line(float& t0, float& v0, float& t1, float& v1) const override;
    const std::vector<EditorPoint>& pencil() const override;
    bool  readout(std::string& text, float& px, float& py) const override;
    DGL_NAMESPACE::MouseCursor cursor() const override;
    bool  mouseDown(float x, float y, const Pointer& p, int clickCount) override;
    bool  mouseMove(float x, float y, const Pointer& p) override;
    bool  mouseUp(float x, float y, const Pointer& p) override;
    bool  key(unsigned key, const Pointer& p) override;
    void  setGeometry(const RectF& plot, const RectF& lane, bool laneVisible) override;
    void  setTool(int tool) override;
    void  contextMenu(float x, float y, float px, float py) override;

    // ---- History ------------------------------------------------------------------------------
    void beginParamGesture(int id) override;
    void recordParam(int id, float oldValue, float newValue) override;
    void endParamGesture(int id) override;
    void recordEnvelope(int band, const Envelope& before, const Envelope& after) override;
    void beginTransaction(const char* label) override;
    void commitTransaction() override;
    bool canUndo() const override;
    bool canRedo() const override;
    void undo() override;
    void redo() override;

    // ---- EditorListener ----------------------------------------------------------------------
    void envelopeChanged(editor::Band band, const Envelope& env, bool final) override;
    void parameterChanged(uint32_t paramId, float value) override;
    void historyChanged() override;
    void editorStateChanged() override;

private:
    void sync() const;   // timing / link / snap / band from the Model into the EditorModel
    editor::PointerMods mods(const Pointer& p) const { return {p.shift, p.ctrl, p.alt}; }

    View&  view_;
    Model& m_;
    mutable editor::EditorModel ed_;
    std::unique_ptr<editor::EditorController> ctl_;
    mutable std::vector<editor::Vec2> tmp_;
    mutable std::vector<editor::GridLine> grid_;
    mutable std::vector<EditorPoint> pen_;
    mutable editor::TimingParams lastTiming_;
    mutable bool timingValid_ = false;
    mutable int  lastLink_ = -1, lastBand_ = -1, lastSnap_ = -1;
    double lastPush_ = -1.0;
    bool   applyingHistory_ = false;
};

}} // namespace kick::ui
