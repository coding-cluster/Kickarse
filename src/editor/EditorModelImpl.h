// Kickarse — EditorModel internals shared by EditorModel*.cpp. Not part of the public API.
#pragma once

#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "EditorModel.h"
#include "EnvelopeOps.h"

namespace kick::editor {

struct BandDoc {
    Envelope env;
    NodeMask sel;
    bool     qsExplicit = false;
    float    qsStart    = 0.f;   // node x
    float    qsEnd      = 0.f;
};

// Everything an undo step restores.
struct DocState {
    BandDoc bands[kNumBands];
    Band    active = Band::A;
};

struct ParamChange {
    uint32_t id     = 0;
    float    before = 0.f;
    float    after  = 0.f;
};

struct HistoryEntry {
    std::string              label;
    bool                     hasDoc = false;
    DocState                 doc;          // the state on the other side of this step (swap on undo/redo)
    std::vector<ParamChange> params;
    uint32_t                 coalesceKey  = 0;
    int                      paramGesture = 0;
    double                   time         = 0.0;
};

struct Transaction {
    std::string              label;
    DocState                 before;
    std::vector<ParamChange> params;
    uint32_t                 coalesceKey = 0;
};

struct GestureState {
    GestureKind       kind = GestureKind::None;
    size_t            txn  = 0;          // index of the gesture's transaction in Impl::txns
    Band              band = Band::A;
    int               index = -1;        // grabbed node / segment
    bool              allSelected = false;
    QuickShiftPart    qsPart = QuickShiftPart::Bar;
    StretchEdge       edge   = StretchEdge::Start;
    float             anchor = 0.f;      // NewRange: node x where the drag started
    Vec2              lineStart;         // raw start of a line drag
    std::vector<Vec2> stroke;            // pencil
    GestureInfo       info;
    std::vector<Envelope> sent[kNumBands];  // live envelopes pushed during this gesture (echo guard)
};

enum CoalesceKey : uint32_t { kCoalesceNone = 0, kCoalesceNudge = 1 };

inline constexpr float kEdgeGap       = 1e-4f;   // new nodes keep this distance from x = 0 / 1
inline constexpr int   kPencilSamples = 97;      // DESIGN.md §7.8
inline constexpr float kPencilTolerance = 0.012f;

inline float clamp01(float v) noexcept
{
    return v > 0.f ? (v < 1.f ? v : 1.f) : 0.f; // NaN → 0
}

bool sameContent(const DocState& a, const DocState& b) noexcept;   // envelopes + Quick Shift ranges

struct EditorModel::Impl {
    DocState     doc;
    bool         linked = true;   // env_link defaults to on
    TimingParams timing;
    TimelineMap  map;
    SnapSettings snap;
    EditorListener* listener = nullptr;

    std::deque<HistoryEntry>  undo;
    std::deque<HistoryEntry>  redo;
    int                       limit = 256;
    double                    coalesceWindow = 0.5;
    std::function<double()>   clock;
    std::vector<Transaction>  txns;
    std::map<uint32_t, int>   paramGestures;
    int                       paramGestureSerial = 0;

    GestureState g;
    std::string  clipboard;

    Envelope published[kNumBands];
    Envelope lastSent[kNumBands];
    bool     liveSent[kNumBands] = {false, false};

    Impl();

    // ---- access
    BandDoc&        bd(Band b) noexcept { return doc.bands[bandIndex(b)]; }
    const BandDoc&  bd(Band b) const noexcept { return doc.bands[bandIndex(b)]; }
    const Envelope& env(Band b) const noexcept { return bd(b).env; }
    Band            editBand() const noexcept { return linked ? Band::A : doc.active; }
    bool            snapActive(bool invert) const noexcept { return snap.enabled != invert; }
    double          now() const;

    // ---- node lists with the selection carried as kTagSelected
    ops::NodeList tagged(Band b) const;
    bool          store(Band b, const ops::NodeList& list);
    bool          selectionSpan(Band b, int& i0, int& i1) const;   // true for a real span
    float         applyY(float y, bool invertSnap) const noexcept; // magnet / y snap / clamp

    // ---- transactions & history
    void begin(std::string label, uint32_t key = kCoalesceNone);
    bool commit();
    void cancel();
    template <class F>
    bool edit(std::string label, F&& fn, uint32_t key = kCoalesceNone)
    {
        endGesture();
        begin(std::move(label), key);
        if (!fn()) {
            cancel();
            return false;
        }
        return commit();
    }
    void pushEntry(std::string label, bool hasDoc, DocState&& before, std::vector<ParamChange>&& params,
                   uint32_t key, int paramGesture);
    void recordParam(uint32_t id, float before, float after);
    void emitParam(uint32_t id, float value);
    void mirrorParam(uint32_t id, float value);
    bool undoStep();
    bool redoStep();
    static void mergeParam(std::vector<ParamChange>& list, const ParamChange& p);

    // ---- notifications
    void live();
    void finalize();
    void stateChanged();
    void historyChanged();

    // ---- gestures
    void            startGesture(GestureKind kind, std::string label, Band band);
    const DocState& origin() const noexcept { return txns[g.txn].before; }
    void            restoreOrigin() { doc = origin(); }
    bool            endGesture();
    void            cancelGesture();
    bool            applyLine(ops::NodeList& list, Vec2 a, Vec2 b) const;
    bool            applyPencil(Band band);
    float           shiftGroupImpl(Band band, float dxTimeline, bool invertSnap);
    void            setQsRange(Band band, float start, float end, bool autoGroup);
    QuickShiftRange qsRange(Band band) const noexcept;
    int             groupSize(Band band) const noexcept;

    // ---- shared editing helpers
    bool spanTransform(std::string label, const std::function<bool(ops::NodeList&)>& fn,
                       bool keepQsRange = false);
    bool applyShapeImpl(const Envelope& shape, bool intoSelection, std::string label);
    bool rotateExact(ops::NodeList& list, float amount) const;
    bool load(const Envelope* a, const Envelope* b, LoadHistory history, bool notify);
};

} // namespace kick::editor
