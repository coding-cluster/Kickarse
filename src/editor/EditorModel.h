// Kickarse — headless envelope editor model (library kickarse_editor, UI thread only).
//
// The view (src/ui, NanoVG) owns layout and drawing; this model owns the edit state and every
// editing operation: selection, snapping, hit testing, node/tension/line/pencil editing, Quick
// Shift, transforms, capture → envelope, clipboard and undo/redo. The view translates gestures
// into calls here (EditorController does a complete default mapping per DESIGN.md §9.2) and draws
// from the accessors. No DPF/NanoVG dependency.
//
// ---- Coordinates -------------------------------------------------------------------------------
// Positions in this API are (timeline, value) — what the editor shows — unless a name says "node".
// See EditorTypes.h / TimelineMap.h. Deltas (dx) are timeline deltas.
//
// ---- Bands and link ----------------------------------------------------------------------------
// Two envelopes: Band::A (envA, main/low) and Band::B (envB, high). setActiveBand() picks the tab;
// editBand() is the band edits apply to: always A while linked (the engine then plays A on both
// bands), otherwise the active band. Selection is kept per band. Every operation without a band
// argument works on editBand().
//
// ---- Undo steps, transactions, gestures -----------------------------------------------------------
// Each public editing call is one undo step, unless it runs inside an open transaction
// (beginTransaction … commitTransaction) or gesture (begin… → update… → endGesture), which make
// one step together. A commit that changed nothing records nothing. Gesture updates always
// recompute from the state at the gesture's start, so they are exact and reversible mid-drag.
// Starting another edit (or undo, band switch, load) while a gesture runs ends it first.
// Selection changes are not undo steps, but undo/redo restore the selection, the Quick Shift range
// and the active band of the moment together with the envelopes.
//
// ---- Notifications -----------------------------------------------------------------------------
// EditorListener::envelopeChanged(band, env, final):
//   final = false  during a gesture/transaction (push throttled, e.g. ≤ 30 Hz, or not at all);
//   final = true   once the change is complete (push now: setState("envA"/"envB", env.serialize())).
//   A cancelled gesture that had sent live updates sends one final update with the restored curve.
// parameterChanged(id, value): the model wants a host parameter set (undo/redo of knob edits,
//   bakeRotation, setLinkedByUser). Call setParameterValue with editParameter begin/end around it.
// historyChanged(): undo/redo availability or labels changed.
// editorStateChanged(): selection, band, Quick Shift range, gesture preview… (repaint).
// load() with notify = false sends nothing (the host already has that state).
//
// ---- Invariants --------------------------------------------------------------------------------
// Stored envelopes are always valid (2…kMaxNodes nodes, sorted, endpoints at 0/1, ranges clamped,
// finite) and never carry the editor's internal tag bits. Every operation is a no-op returning
// false/-1 on bad input (out-of-range index, NaN, full envelope …) instead of misbehaving.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Capture.h"
#include "EditorTypes.h"
#include "TimelineMap.h"

namespace kick::editor {

class EditorListener {
public:
    virtual ~EditorListener() = default;
    virtual void envelopeChanged(Band /*band*/, const Envelope& /*env*/, bool /*final*/) {}
    virtual void parameterChanged(uint32_t /*paramId*/, float /*value*/) {}
    virtual void historyChanged() {}
    virtual void editorStateChanged() {}
};

// Live details of the running gesture, for the drag readout (DESIGN.md §7.8) and previews.
struct GestureInfo {
    GestureKind kind  = GestureKind::None;
    Band        band  = Band::A;
    int         index = -1;        // grabbed node (MoveNodes) or segment (Tension)
    Vec2        position;          // MoveNodes: grabbed node now (timeline, value)
    float       tension = 0.f;     // Tension: the grabbed segment's tension now
    float       shift   = 0.f;     // QuickShift*/MoveNodes/Stretch: applied Δ in timeline phase
    Vec2        lineStart;         // Line: snapped start (timeline, value)
    Vec2        lineEnd;           // Line: snapped end
};

class EditorModel {
public:
    EditorModel();
    ~EditorModel();
    EditorModel(const EditorModel&)            = delete;
    EditorModel& operator=(const EditorModel&) = delete;

    void setListener(EditorListener* listener) noexcept;  // not owned; nullptr detaches

    // ---- Document ---------------------------------------------------------------------------------
    const Envelope& envelope(Band band) const noexcept;
    const Envelope& editEnvelope() const noexcept { return envelope(editBand()); }
    Band activeBand() const noexcept;
    Band editBand() const noexcept;               // A while linked, else activeBand()
    void setActiveBand(Band band);                // the band tab; not an undo step
    bool linked() const noexcept;
    void setLinked(bool linked);                  // mirror of the host's env_link parameter; no undo
    // User toggled the link in the editor: one undo step that emits parameterChanged(kParamEnvLink).
    // Unlinking copies A to B first so nothing jumps (DESIGN.md §7.7).
    bool setLinkedByUser(bool linked);

    // Host / preset state arrived. Invalid input is normalised; an identical envelope is ignored
    // (so echoes of our own setState are free). Clears the band's selection and cancels a gesture.
    // Returns true when anything changed.
    bool load(const Envelope& a, const Envelope& b, LoadHistory history = LoadHistory::Keep,
              bool notify = false);
    bool loadBand(Band band, const Envelope& env, LoadHistory history = LoadHistory::Keep,
                  bool notify = false);
    // Same from a KE1 state string (stateChanged("envA"/"envB", …)); false when malformed.
    bool loadState(Band band, std::string_view ke1, LoadHistory history = LoadHistory::Keep,
                   bool notify = false);

    // ---- Timing, mapping, snap -----------------------------------------------------------------------
    void                setTiming(const TimingParams& timing);   // call when rotate/grid/swing/rate/time mode change
    const TimingParams& timing() const noexcept;
    const TimelineMap&  map() const noexcept;
    void                setSnap(const SnapSettings& snap);
    const SnapSettings& snap() const noexcept;
    bool                snapActive(bool invertSnap) const noexcept { return snap().enabled != invertSnap; }

    // ---- Geometry for drawing (edit band unless a band is given) --------------------------------------
    Vec2  nodePosition(int index) const noexcept;                 // (timeline, value)
    Vec2  nodePosition(Band band, int index) const noexcept;
    bool  segmentHasHandle(int segment) const noexcept;           // non-vertical segment
    Vec2  tensionHandlePosition(int segment) const noexcept;      // curve point at the node-space midpoint
    Vec2  tensionHandlePosition(Band band, int segment) const noexcept;
    float valueAt(float timeline) const noexcept;                 // heard value (right-continuous)
    float valueAt(Band band, float timeline) const noexcept;
    // Polyline of the whole curve from timeline 0 to 1, in drawing order: samples about every
    // `samplePx` pixels over a plot `plotWidthPx` wide, plus every node, vertical step, swing kink and
    // the rotation seam (a vertical jump at timeline = rotation when the endpoints differ).
    void  buildCurve(Band band, float plotWidthPx, std::vector<Vec2>& out, float samplePx = 2.f) const;
    void  gridLines(std::vector<GridLine>& out) const;
    float snapTimeline(float timeline) const noexcept;             // nearest grid line, ignores the toggle
    // Quick Shift range as up to two timeline pieces (it can wrap at the plot edge when rotated).
    // out[i] = (startTimeline, endTimeline); returns the number of pieces (0 = nothing to draw).
    int   quickShiftSpans(Vec2 out[2]) const noexcept;
    Vec2  quickShiftEdges() const noexcept;                        // (start, end) timeline of the handles

    // ---- Hit testing (pixels, via the view's transforms) ---------------------------------------------
    // Priority: node, then tension handle (nodes win near-ties), then segment, else Empty.
    Hit hitTest(const ViewTransform& plot, float px, float py, const HitTolerances& tol = {}) const;
    Hit hitTestQuickShift(const ViewTransform& lane, float px, float py,
                          const HitTolerances& tol = {}) const;
    // Nodes whose drawn position lies in the (timeline, value) rectangle (corners in any order).
    NodeMask nodesInRect(float t0, float v0, float t1, float v1) const;

    // ---- Selection (edit band) ---------------------------------------------------------------------
    const NodeMask& selection() const noexcept;
    bool isSelected(int index) const noexcept;
    int  selectionCount() const noexcept;
    void select(int index, SelectMode mode = SelectMode::Replace);
    void selectRect(float t0, float v0, float t1, float v1, SelectMode mode = SelectMode::Replace,
                    const NodeMask* base = nullptr);   // base: selection the marquee started from
    void setSelection(const NodeMask& mask);
    void selectAll();
    void selectNone();

    // ---- Node editing ------------------------------------------------------------------------------
    // Adds a node at the pointer (x snapped when snapping, y magnet/snap applied) and selects it.
    int  addNode(float timeline, float value, bool invertSnap = false);
    // Adds a node exactly on the curve at that x (curve unchanged: the segment is split with
    // matching tensions) and selects it. Returns the index or -1.
    int  addNodeOnCurve(float timeline, bool invertSnap = false);
    bool deleteNode(int index);          // endpoints are never deleted
    bool deleteSelection();
    bool setTension(int segment, float tension);
    bool straightenSegment(int segment);
    // Typed entry: moves one node, clamped between its neighbours (endpoints only change y).
    bool setNodePosition(int index, float timeline, float value);
    // Arrow keys: x by one grid cell (fine: 1/8 cell), y by 1 % (fine: 0.1 %), rigidly, clamped
    // against unselected neighbours. Repeats within the coalesce window make one undo step.
    bool nudgeSelection(int stepsX, int stepsY, bool fine);

    // ---- Gestures: begin → update* → endGesture()/cancelGesture(); one undo step --------------------
    // Drag the selection (select the grabbed node first). The grabbed node follows the pointer and
    // snaps; the others move rigidly by the same node-x delta and the same y delta. Nodes never cross
    // unselected neighbours (they may meet them, forming a vertical step); endpoints move only in y;
    // y clamps per node to [0,1].
    bool beginNodeDrag(int grabbedIndex);
    void updateNodeDrag(float dxTimeline, float dyValue, const DragOptions& opt = {});
    // Bend a segment. `raise` > 0 lifts the curve whatever the segment's direction (DESIGN.md §7.4:
    // raise = pixels dragged up / 90, × 0.25 with Shift). allSelectedSegments also bends every
    // segment between two selected nodes by the same amount.
    bool beginTensionDrag(int segment, bool allSelectedSegments = false);
    void updateTensionDrag(float raise);
    // Line tool, applied live. The span strictly between the (snapped) ends is replaced by a straight
    // line (in the timeline, i.e. with nodes on swing kinks); the neighbouring segments keep their
    // curvature and stretch to meet its ends. A drag with equal x (after snapping) makes a vertical
    // step at that x: arriving at the start value, leaving at the end value. Spans crossing the
    // rotation seam are split there.
    bool beginLine(float timeline, float value, const DragOptions& opt = {});
    void updateLine(float timeline, float value, const DragOptions& opt = {});
    // Pencil tool: points are collected (clamped, any direction — later strokes overwrite earlier
    // ones at the same x) and applied at endGesture(): resampled over the stroke's x-span, fitted with
    // Envelope::fromSamples and spliced in like the line tool.
    bool beginPencil(float timeline, float value);
    void addPencilPoint(float timeline, float value);
    const std::vector<Vec2>& pencilStroke() const noexcept;   // raw stroke, for drawing the preview
    // Quick Shift lane drags (see the Quick Shift section below).
    bool beginQuickShiftDrag(QuickShiftPart part, float timeline = 0.f);
    void updateQuickShiftDrag(float dxTimeline, const DragOptions& opt = {});
    // Stretch the selection span by dragging one edge; the other edge stays put. The span is
    // clamped between the unselected neighbours; the moving edge snaps.
    bool beginStretch(StretchEdge edge);
    void updateStretch(float dxTimeline, const DragOptions& opt = {});

    GestureKind gesture() const noexcept;
    GestureInfo gestureInfo() const;
    bool        endGesture();      // commit; true when an undo step was recorded
    void        cancelGesture();   // restore the state at the gesture's start

    // ---- Quick Shift ---------------------------------------------------------------------------------
    // Group = nodes flagged kNodeQuickShift (saved with the envelope; endpoints are never members).
    // Range = an explicit node-x interval once the user dragged an end handle, else the group's
    // extent. Toggling members or setting the group resets the range to "derived".
    QuickShiftRange quickShiftRange() const noexcept;
    bool isQuickShiftMember(int index) const noexcept;
    int  quickShiftGroupSize() const noexcept;
    bool toggleQuickShiftMember(int index);
    bool setQuickShiftGroupFromSelection();
    bool setQuickShiftRange(float startTimeline, float endTimeline, bool autoGroup = true);
    bool setQuickShiftRangeNode(float startX, float endX, bool autoGroup = true);
    bool autoGroupQuickShift();          // members = non-endpoint nodes inside the range
    bool clearQuickShift();              // no members, no explicit range
    // Moves every member by the same node-x delta (the first member follows dxTimeline), keeping
    // spacing, values and tensions; clamped between the nearest non-member neighbours minus
    // SnapSettings::quickShiftMargin. Snaps the delta to 1/quickShiftSubdivisions of a grid cell.
    // An explicit range moves along. Bar drags do the same live (one undo step per drag).
    bool shiftGroup(float dxTimeline, bool invertSnap = false);
    void selectQuickShiftGroup();

    // ---- Transforms ----------------------------------------------------------------------------------
    // Target: the selection span (from the first to the last selected node, when those differ in x)
    // or the whole envelope. Span results keep the outside curve; its neighbouring segments stretch
    // to meet the span's new end values, and the span's nodes stay selected. Node-wise transforms
    // (invert, scaleY) act on the selected nodes, or all nodes without a selection.
    bool invert();                        // y → 1 − y
    bool reverse();                       // time-reverse (tensions mirrored exactly)
    bool rotateShape(float amount);       // cyclic shift by `amount` of the target (bakes a rotation)
    bool bakeRotation();                  // rotate the nodes by Rotate, then set Rotate to 0 (one step)
    bool duplicate();                     // the target repeated twice in its length (fails if > kMaxNodes)
    bool halve();                         // first half stretched over the target (undoes duplicate)
    bool scaleY(float factor);            // y → 1 − factor·(1 − y) (depth-like, around unity)
    bool setFlat(float level = 1.f);      // a flat line at `level`
    bool resetToDefault();                // Envelope() — whole envelope
    bool mirrorLeftToRight();             // right half := mirror image of the left half
    bool applyShape(const Envelope& shape, bool intoSelection = false);  // library click: replace
    bool stretchSelection(float startTimeline, float endTimeline);       // map the span onto [start,end]
    bool simplify(int maxNodes, float tolerance = 0.01f);                // refit with fewer nodes
    bool copyToBand(Band target);         // editBand() → target (e.g. "Copy to High")

    // ---- Clipboard (internal; KE1 text so the view can mirror it to the OS clipboard) ---------------
    // copy(): the selection span normalised to [0,1], or the whole envelope. Returns the text.
    std::string        copy();
    const std::string& clipboard() const noexcept;
    bool               setClipboard(std::string_view ke1);   // false when not valid KE1
    bool               paste(bool intoSelection = false);    // replace the envelope / the selection span

    // ---- Capture (audio → envelope), one undo step ---------------------------------------------------
    // Converts Bridge::recBuf (bins raw peaks per timeline bin) with the current PhaseMap; see
    // Capture.h. Replaces the edit band's envelope, clears selection and Quick Shift.
    bool applyCapture(const float* recBuf, int bins, const CaptureOptions& opt, CaptureStats* stats = nullptr);

    // ---- History -------------------------------------------------------------------------------------
    bool        canUndo() const noexcept;
    bool        canRedo() const noexcept;
    std::string undoLabel() const;        // e.g. "Move Nodes" → tooltip "Undo Move Nodes"
    std::string redoLabel() const;
    int         undoCount() const noexcept;
    int         redoCount() const noexcept;
    bool        undo();
    bool        redo();
    void        clearHistory();
    void        setHistoryLimit(int entries);    // default 256, minimum 1
    int         historyLimit() const noexcept;
    // Parameter edits for rapid-change coalescing (wheel, typed values) are merged when they hit the
    // same parameter within this window; default 0.5 s.
    void        setCoalesceWindow(double seconds);
    void        setClock(std::function<double()> nowSeconds);   // tests; default steady_clock

    // Transactions group several calls into one undo step (nestable; the outermost label wins).
    void beginTransaction(std::string label);
    bool commitTransaction();          // true when the outermost commit recorded a step
    void cancelTransaction();          // restore the state at the matching begin (and its parameters)
    void revertTransaction();          // restore the state at the matching begin, keep it open
    bool inTransaction() const noexcept;

    // Knob edits made in the UI (the view already sent them to the host). One undo step, or merged
    // into the open transaction / the running parameter gesture / a recent edit of the same parameter.
    // Undo/redo re-send the values through parameterChanged().
    void recordParameterChange(uint32_t paramId, float oldValue, float newValue);
    void beginParameterGesture(uint32_t paramId);   // mouse down on a knob: all changes merge
    void endParameterGesture(uint32_t paramId);
    // The model sets a parameter itself (menu actions): records it and emits parameterChanged().
    void changeParameter(uint32_t paramId, float oldValue, float newValue);

    // ---- View state persistence (fits in the UI's uiState string) -----------------------------------
    // Explicit Quick Shift ranges and the active band: "qsA=0.1,0.4;qsB=-;band=0".
    std::string saveViewState() const;
    bool        restoreViewState(std::string_view text);

    struct Impl;   // implementation detail (EditorModel.cpp and friends)

private:
    std::unique_ptr<Impl> d_;
};

} // namespace kick::editor
