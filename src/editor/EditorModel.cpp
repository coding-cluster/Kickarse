// Kickarse — EditorModel: document, selection, history, notifications, load, clipboard.
// Editing operations live in EditorEdit.cpp, geometry and hit testing in EditorGeometry.cpp.
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>

#include "EditorModelImpl.h"

namespace kick::editor {

namespace {

std::string paramLabel(uint32_t id)
{
    if (id < kParamCount)
        return std::string("Change ") + kParams[id].name;
    return "Change Parameter";
}

void appendFloat(std::string& s, float v)
{
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof(buf), v);
    s.append(buf, r.ptr);
}

bool parseFloat(std::string_view t, float& out)
{
    while (!t.empty() && t.front() == ' ')
        t.remove_prefix(1);
    float v = 0.f;
    const auto r = std::from_chars(t.data(), t.data() + t.size(), v);
    if (r.ec != std::errc() || r.ptr == t.data() || !std::isfinite(v))
        return false;
    out = v;
    return true;
}

bool anyMemberInside(const BandDoc& d) noexcept
{
    const int n = d.env.size();
    for (int i = 1; i + 1 < n; ++i) {
        const EnvNode& e = d.env.node(i);
        if ((e.flags & kNodeQuickShift) && e.x >= d.qsStart - ops::kSameX && e.x <= d.qsEnd + ops::kSameX)
            return true;
    }
    return false;
}

} // namespace

bool sameContent(const DocState& a, const DocState& b) noexcept
{
    for (int k = 0; k < kNumBands; ++k) {
        const BandDoc& p = a.bands[k];
        const BandDoc& q = b.bands[k];
        if (!ops::sameNodes(p.env, q.env) || p.qsExplicit != q.qsExplicit)
            return false;
        if (p.qsExplicit && (p.qsStart != q.qsStart || p.qsEnd != q.qsEnd))
            return false;
    }
    return true;
}

// ---- Impl basics ------------------------------------------------------------------------------------

EditorModel::Impl::Impl()
{
    map = TimelineMap(makePhaseMap(timing));
    for (int k = 0; k < kNumBands; ++k) {
        published[k] = doc.bands[k].env;
        lastSent[k]  = doc.bands[k].env;
    }
}

double EditorModel::Impl::now() const
{
    if (clock)
        return clock();
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

ops::NodeList EditorModel::Impl::tagged(Band b) const
{
    const BandDoc& d = bd(b);
    ops::NodeList v = ops::toList(d.env);
    for (size_t i = 0; i < v.size(); ++i) {
        v[i].flags &= ~ops::kTagMask;
        if (d.sel.test(i))
            v[i].flags |= ops::kTagSelected;
    }
    return v;
}

bool EditorModel::Impl::store(Band b, const ops::NodeList& list)
{
    Envelope e;
    if (!ops::fromList(list, e))
        return false;
    NodeMask sel;
    for (int i = 0; i < e.size(); ++i) {
        if (e.node(i).flags & ops::kTagSelected)
            sel.set(size_t(i));
        e.node(i).flags &= ~ops::kTagMask;
    }
    BandDoc& d = bd(b);
    d.env = e;
    d.sel = sel;
    return true;
}

bool EditorModel::Impl::selectionSpan(Band b, int& i0, int& i1) const
{
    const BandDoc& d = bd(b);
    const int n = d.env.size();
    i0 = 0;
    i1 = n - 1;
    int first = -1, last = -1;
    for (int i = 0; i < n; ++i) {
        if (d.sel.test(size_t(i))) {
            if (first < 0)
                first = i;
            last = i;
        }
    }
    if (first < 0 || last <= first || !(d.env.node(last).x > d.env.node(first).x))
        return false;
    i0 = first;
    i1 = last;
    return true;
}

float EditorModel::Impl::applyY(float y, bool invertSnap) const noexcept
{
    y = clamp01(y);
    if (!invertSnap && snap.yMagnet > 0.f) {
        if (y <= snap.yMagnet)
            y = 0.f;
        else if (y >= 1.f - snap.yMagnet)
            y = 1.f;
    }
    if (snapActive(invertSnap) && snap.snapY)
        y = std::round(y * 4.f) * 0.25f;
    return y;
}

// ---- notifications -----------------------------------------------------------------------------------

void EditorModel::Impl::live()
{
    if (txns.empty()) {
        finalize();
        return;
    }
    for (int k = 0; k < kNumBands; ++k) {
        const Envelope& e = doc.bands[k].env;
        if (ops::sameNodes(e, lastSent[k]))
            continue;
        lastSent[k] = e;
        liveSent[k] = true;
        if (g.kind != GestureKind::None && g.sent[k].size() < 4096)
            g.sent[k].push_back(e);
        if (listener)
            listener->envelopeChanged(Band(k), e, false);
    }
}

void EditorModel::Impl::finalize()
{
    for (int k = 0; k < kNumBands; ++k) {
        const Envelope& e = doc.bands[k].env;
        if (!liveSent[k] && ops::sameNodes(e, published[k]))
            continue;
        published[k] = e;
        lastSent[k]  = e;
        liveSent[k]  = false;
        if (listener)
            listener->envelopeChanged(Band(k), e, true);
    }
}

void EditorModel::Impl::stateChanged()
{
    if (listener)
        listener->editorStateChanged();
}

void EditorModel::Impl::historyChanged()
{
    if (listener)
        listener->historyChanged();
}

// ---- transactions & history ------------------------------------------------------------------------

void EditorModel::Impl::begin(std::string label, uint32_t key)
{
    Transaction t;
    t.label       = std::move(label);
    t.before      = doc;
    t.coalesceKey = key;
    txns.push_back(std::move(t));
}

void EditorModel::Impl::mergeParam(std::vector<ParamChange>& list, const ParamChange& p)
{
    for (ParamChange& q : list) {
        if (q.id == p.id) {
            q.after = p.after;
            return;
        }
    }
    list.push_back(p);
}

bool EditorModel::Impl::commit()
{
    if (txns.empty())
        return false;
    Transaction t = std::move(txns.back());
    txns.pop_back();
    t.params.erase(std::remove_if(t.params.begin(), t.params.end(),
                                  [](const ParamChange& p) { return p.before == p.after; }),
                   t.params.end());
    const bool changed = !sameContent(t.before, doc);
    const bool any     = changed || !t.params.empty();
    if (!txns.empty()) {
        for (const ParamChange& p : t.params)
            mergeParam(txns.back().params, p);
        live();
        return any;
    }
    if (any)
        pushEntry(std::move(t.label), changed, std::move(t.before), std::move(t.params), t.coalesceKey, 0);
    finalize();
    stateChanged();
    return any;
}

void EditorModel::Impl::cancel()
{
    if (txns.empty())
        return;
    Transaction t = std::move(txns.back());
    txns.pop_back();
    doc = t.before;
    for (auto it = t.params.rbegin(); it != t.params.rend(); ++it)
        if (it->before != it->after)
            emitParam(it->id, it->before);
    if (txns.empty())
        finalize();
    else
        live();
    stateChanged();
}

void EditorModel::Impl::pushEntry(std::string label, bool hasDoc, DocState&& before,
                                  std::vector<ParamChange>&& params, uint32_t key, int paramGesture)
{
    const double t = now();
    if (key != kCoalesceNone && !undo.empty() && redo.empty()) {
        HistoryEntry& top = undo.back();
        if (top.coalesceKey == key && top.hasDoc == hasDoc && t - top.time <= coalesceWindow) {
            for (const ParamChange& p : params)
                mergeParam(top.params, p);
            top.time = t;
            historyChanged();
            return;
        }
    }
    HistoryEntry e;
    e.label        = std::move(label);
    e.hasDoc       = hasDoc;
    e.doc          = std::move(before);
    e.params       = std::move(params);
    e.coalesceKey  = key;
    e.paramGesture = paramGesture;
    e.time         = t;
    redo.clear();
    undo.push_back(std::move(e));
    while (int(undo.size()) > limit)
        undo.pop_front();
    historyChanged();
}

void EditorModel::Impl::recordParam(uint32_t id, float before, float after)
{
    if (!std::isfinite(before) || !std::isfinite(after) || before == after)
        return;
    if (!txns.empty()) {
        std::vector<ParamChange>& list = txns.back().params;
        for (ParamChange& q : list) {
            if (q.id == id) {
                q.after = after;
                return;
            }
        }
        list.push_back({id, before, after});
        return;
    }
    const double t      = now();
    const auto   gi     = paramGestures.find(id);
    const int    serial = gi == paramGestures.end() ? 0 : gi->second;
    if (!undo.empty() && redo.empty()) {
        HistoryEntry& top = undo.back();
        bool merge = !top.hasDoc && top.params.size() == 1 && top.params[0].id == id;
        if (merge)
            merge = serial != 0 ? top.paramGesture == serial
                                : top.paramGesture == 0 && t - top.time <= coalesceWindow;
        if (merge) {
            top.params[0].after = after;
            top.time            = t;
            if (top.params[0].after == top.params[0].before)
                undo.pop_back();
            historyChanged();
            return;
        }
    }
    pushEntry(paramLabel(id), false, DocState(doc), {ParamChange{id, before, after}}, kCoalesceNone, serial);
}

void EditorModel::Impl::mirrorParam(uint32_t id, float value)
{
    if (!std::isfinite(value))
        return;
    bool timingChanged = true;
    switch (id) {
    case kParamEnvLink:  linked = value >= 0.5f; timingChanged = false; break;
    case kParamRotate:   timing.rotateDeg = value; break;
    case kParamSwing:    timing.swingPercent = value; break;
    case kParamGrid:     timing.gridIndex = std::clamp(int(std::lround(value)), 0, kNumGrids - 1); break;
    case kParamRate:     timing.rateIndex = std::clamp(int(std::lround(value)), 0, kNumRates - 1); break;
    case kParamTimeMode: timing.timeMode = std::clamp(int(std::lround(value)), 0, 1); break;
    case kParamLengthMs: timing.lengthMs = value; break;
    default:             timingChanged = false; break;
    }
    if (timingChanged)
        map = TimelineMap(makePhaseMap(timing));
}

void EditorModel::Impl::emitParam(uint32_t id, float value)
{
    mirrorParam(id, value);
    if (listener)
        listener->parameterChanged(id, value);
}

bool EditorModel::Impl::undoStep()
{
    endGesture();
    while (!txns.empty())
        commit();
    if (undo.empty())
        return false;
    HistoryEntry e = std::move(undo.back());
    undo.pop_back();
    if (e.hasDoc)
        std::swap(doc, e.doc);
    for (auto it = e.params.rbegin(); it != e.params.rend(); ++it)
        emitParam(it->id, it->before);
    redo.push_back(std::move(e));
    finalize();
    historyChanged();
    stateChanged();
    return true;
}

bool EditorModel::Impl::redoStep()
{
    endGesture();
    while (!txns.empty())
        commit();
    if (redo.empty())
        return false;
    HistoryEntry e = std::move(redo.back());
    redo.pop_back();
    if (e.hasDoc)
        std::swap(doc, e.doc);
    for (const ParamChange& p : e.params)
        emitParam(p.id, p.after);
    undo.push_back(std::move(e));
    while (int(undo.size()) > limit)
        undo.pop_front();
    finalize();
    historyChanged();
    stateChanged();
    return true;
}

// ---- load ---------------------------------------------------------------------------------------------

bool EditorModel::Impl::load(const Envelope* a, const Envelope* b, LoadHistory history, bool notify)
{
    const Envelope* in[kNumBands] = {a, b};
    Envelope        clean[kNumBands];
    bool            change[kNumBands] = {false, false};
    for (int k = 0; k < kNumBands; ++k) {
        if (!in[k])
            continue;
        clean[k] = *in[k];
        clean[k].normalise();
        ops::strip(clean[k]);
        clean[k].node(clean[k].size() - 1).tension = 0.f;
        if (ops::sameNodes(clean[k], doc.bands[k].env))
            continue;
        // Our own live pushes echoed back by a host while a drag runs are not new state.
        bool echo = false;
        for (const Envelope& s : g.sent[k])
            echo = echo || ops::sameNodes(s, clean[k]);
        change[k] = !echo;
    }
    if (!change[0] && !change[1])
        return false;

    cancelGesture();
    while (!txns.empty())
        commit();
    if (history == LoadHistory::Record)
        begin("Load");
    for (int k = 0; k < kNumBands; ++k) {
        if (!change[k])
            continue;
        BandDoc& d = doc.bands[k];
        d.env = clean[k];
        d.sel.reset();
        if (d.qsExplicit && !anyMemberInside(d))
            d.qsExplicit = false;
        if (!notify) {
            published[k] = d.env;
            lastSent[k]  = d.env;
            liveSent[k]  = false;
        }
    }
    if (history == LoadHistory::Record)
        commit();
    else
        finalize();
    if (history == LoadHistory::Clear) {
        undo.clear();
        redo.clear();
        historyChanged();
    }
    stateChanged();
    return true;
}

// ---- EditorModel: construction, listener, document --------------------------------------------------

EditorModel::EditorModel() : d_(std::make_unique<Impl>()) {}
EditorModel::~EditorModel() = default;

void EditorModel::setListener(EditorListener* listener) noexcept
{
    d_->listener = listener;
}

const Envelope& EditorModel::envelope(Band band) const noexcept
{
    return d_->env(band);
}

Band EditorModel::activeBand() const noexcept
{
    return d_->doc.active;
}

Band EditorModel::editBand() const noexcept
{
    return d_->editBand();
}

void EditorModel::setActiveBand(Band band)
{
    if (band != Band::A && band != Band::B)
        return;
    if (d_->doc.active == band)
        return;
    d_->endGesture();
    d_->doc.active = band;
    d_->stateChanged();
}

bool EditorModel::linked() const noexcept
{
    return d_->linked;
}

void EditorModel::setLinked(bool linked)
{
    if (d_->linked == linked)
        return;
    d_->endGesture();
    d_->linked = linked;
    d_->stateChanged();
}

bool EditorModel::setLinkedByUser(bool linked)
{
    Impl& m = *d_;
    if (m.linked == linked)
        return false;
    return m.edit(linked ? "Link Envelopes" : "Unlink Envelopes", [&] {
        if (!linked) {
            BandDoc&       dst = m.bd(Band::B);
            const BandDoc& src = m.bd(Band::A);
            dst.env        = src.env;
            dst.sel.reset();
            dst.qsExplicit = src.qsExplicit;
            dst.qsStart    = src.qsStart;
            dst.qsEnd      = src.qsEnd;
        }
        m.recordParam(kParamEnvLink, m.linked ? 1.f : 0.f, linked ? 1.f : 0.f);
        m.emitParam(kParamEnvLink, linked ? 1.f : 0.f);
        return true;
    });
}

bool EditorModel::load(const Envelope& a, const Envelope& b, LoadHistory history, bool notify)
{
    return d_->load(&a, &b, history, notify);
}

bool EditorModel::loadBand(Band band, const Envelope& env, LoadHistory history, bool notify)
{
    return band == Band::B ? d_->load(nullptr, &env, history, notify)
                           : d_->load(&env, nullptr, history, notify);
}

bool EditorModel::loadState(Band band, std::string_view ke1, LoadHistory history, bool notify)
{
    Envelope e;
    if (!e.deserialize(ke1))
        return false;
    loadBand(band, e, history, notify);
    return true;
}

// ---- timing & snap ------------------------------------------------------------------------------------

void EditorModel::setTiming(const TimingParams& timing)
{
    d_->timing = timing;
    d_->map    = TimelineMap(makePhaseMap(timing));
    d_->stateChanged();
}

const TimingParams& EditorModel::timing() const noexcept
{
    return d_->timing;
}

const TimelineMap& EditorModel::map() const noexcept
{
    return d_->map;
}

void EditorModel::setSnap(const SnapSettings& snap)
{
    SnapSettings s = snap;
    s.yMagnet                = std::isfinite(s.yMagnet) ? std::clamp(s.yMagnet, 0.f, 0.25f) : 0.f;
    s.quickShiftSubdivisions = std::clamp(s.quickShiftSubdivisions, 1, 64);
    s.quickShiftMargin       = std::isfinite(s.quickShiftMargin) ? std::clamp(s.quickShiftMargin, 0.f, 0.1f) : 0.f;
    d_->snap                 = s;
    d_->stateChanged();
}

const SnapSettings& EditorModel::snap() const noexcept
{
    return d_->snap;
}

// ---- selection ------------------------------------------------------------------------------------------

const NodeMask& EditorModel::selection() const noexcept
{
    return d_->bd(d_->editBand()).sel;
}

bool EditorModel::isSelected(int index) const noexcept
{
    return index >= 0 && index < Envelope::kMaxNodes && selection().test(size_t(index));
}

int EditorModel::selectionCount() const noexcept
{
    return int(selection().count());
}

void EditorModel::select(int index, SelectMode mode)
{
    Impl&    m = *d_;
    BandDoc& d = m.bd(m.editBand());
    if (index < 0 || index >= d.env.size())
        return;
    const size_t i = size_t(index);
    switch (mode) {
    case SelectMode::Replace: d.sel.reset(); d.sel.set(i); break;
    case SelectMode::Add:     d.sel.set(i); break;
    case SelectMode::Toggle:  d.sel.flip(i); break;
    case SelectMode::Remove:  d.sel.reset(i); break;
    }
    m.stateChanged();
}

void EditorModel::selectRect(float t0, float v0, float t1, float v1, SelectMode mode, const NodeMask* base)
{
    Impl&          m     = *d_;
    const NodeMask inRect = nodesInRect(t0, v0, t1, v1);
    NodeMask       result = base ? *base : m.bd(m.editBand()).sel;
    switch (mode) {
    case SelectMode::Replace: result = inRect; break;
    case SelectMode::Add:     result |= inRect; break;
    case SelectMode::Toggle:  result ^= inRect; break;
    case SelectMode::Remove:  result &= ~inRect; break;
    }
    setSelection(result);
}

void EditorModel::setSelection(const NodeMask& mask)
{
    Impl&    m = *d_;
    BandDoc& d = m.bd(m.editBand());
    NodeMask valid;
    for (int i = 0; i < d.env.size(); ++i)
        valid.set(size_t(i));
    const NodeMask next = mask & valid;
    if (next == d.sel)
        return;
    d.sel = next;
    m.stateChanged();
}

void EditorModel::selectAll()
{
    NodeMask all;
    all.set();
    setSelection(all);
}

void EditorModel::selectNone()
{
    setSelection(NodeMask{});
}

// ---- clipboard --------------------------------------------------------------------------------------------

std::string EditorModel::copy()
{
    Impl&          m = *d_;
    const Band     b = m.editBand();
    int            i0 = 0, i1 = 0;
    ops::NodeList  list = m.tagged(b);
    Envelope       out  = m.env(b);
    if (m.selectionSpan(b, i0, i1)) {
        ops::NodeList content;
        const float a = list[size_t(i0)].x, w = list[size_t(i1)].x - a;
        for (int i = i0; i <= i1; ++i) {
            EnvNode n = list[size_t(i)];
            n.x       = clamp01((n.x - a) / w);
            content.push_back(n);
        }
        if (!ops::fromList(content, out))
            out = m.env(b);
    }
    ops::strip(out);
    m.clipboard = out.serialize();
    return m.clipboard;
}

const std::string& EditorModel::clipboard() const noexcept
{
    return d_->clipboard;
}

bool EditorModel::setClipboard(std::string_view ke1)
{
    Envelope e;
    if (!e.deserialize(ke1))
        return false;
    ops::strip(e);
    d_->clipboard = e.serialize();
    return true;
}

bool EditorModel::paste(bool intoSelection)
{
    Envelope e;
    if (d_->clipboard.empty() || !e.deserialize(d_->clipboard))
        return false;
    return d_->applyShapeImpl(e, intoSelection, "Paste");
}

// ---- history (public) ----------------------------------------------------------------------------------

bool EditorModel::canUndo() const noexcept
{
    return !d_->undo.empty() || !d_->txns.empty();
}

bool EditorModel::canRedo() const noexcept
{
    return !d_->redo.empty();
}

std::string EditorModel::undoLabel() const
{
    if (!d_->txns.empty())
        return d_->txns.front().label;
    return d_->undo.empty() ? std::string() : d_->undo.back().label;
}

std::string EditorModel::redoLabel() const
{
    return d_->redo.empty() ? std::string() : d_->redo.back().label;
}

int EditorModel::undoCount() const noexcept
{
    return int(d_->undo.size());
}

int EditorModel::redoCount() const noexcept
{
    return int(d_->redo.size());
}

bool EditorModel::undo()
{
    return d_->undoStep();
}

bool EditorModel::redo()
{
    return d_->redoStep();
}

void EditorModel::clearHistory()
{
    d_->undo.clear();
    d_->redo.clear();
    d_->historyChanged();
}

void EditorModel::setHistoryLimit(int entries)
{
    d_->limit = std::max(entries, 1);
    bool trimmed = false;
    while (int(d_->undo.size()) > d_->limit) {
        d_->undo.pop_front();
        trimmed = true;
    }
    if (trimmed)
        d_->historyChanged();
}

int EditorModel::historyLimit() const noexcept
{
    return d_->limit;
}

void EditorModel::setCoalesceWindow(double seconds)
{
    d_->coalesceWindow = std::isfinite(seconds) ? std::max(0.0, seconds) : 0.5;
}

void EditorModel::setClock(std::function<double()> nowSeconds)
{
    d_->clock = std::move(nowSeconds);
}

void EditorModel::beginTransaction(std::string label)
{
    d_->begin(label.empty() ? std::string("Edit") : std::move(label));
}

bool EditorModel::commitTransaction()
{
    Impl& m = *d_;
    if (m.txns.empty())
        return false;
    if (m.g.kind != GestureKind::None && m.g.txn + 1 >= m.txns.size())
        m.endGesture();   // the transaction being closed is (or contains) the gesture's
    if (m.txns.empty())
        return false;
    const bool outermost = m.txns.size() == 1;
    const bool any       = m.commit();
    return outermost && any;
}

void EditorModel::cancelTransaction()
{
    Impl& m = *d_;
    if (m.txns.empty())
        return;
    if (m.g.kind != GestureKind::None && m.g.txn + 1 >= m.txns.size())
        m.cancelGesture();
    else
        m.cancel();
}

void EditorModel::revertTransaction()
{
    Impl& m = *d_;
    if (m.txns.empty())
        return;
    m.doc = m.txns.back().before;
    for (auto it = m.txns.back().params.rbegin(); it != m.txns.back().params.rend(); ++it)
        m.emitParam(it->id, it->before);
    m.txns.back().params.clear();
    m.live();
    m.stateChanged();
}

bool EditorModel::inTransaction() const noexcept
{
    return !d_->txns.empty();
}

void EditorModel::recordParameterChange(uint32_t paramId, float oldValue, float newValue)
{
    d_->mirrorParam(paramId, newValue);
    d_->recordParam(paramId, oldValue, newValue);
}

void EditorModel::beginParameterGesture(uint32_t paramId)
{
    d_->paramGestures[paramId] = ++d_->paramGestureSerial;
}

void EditorModel::endParameterGesture(uint32_t paramId)
{
    d_->paramGestures.erase(paramId);
}

void EditorModel::changeParameter(uint32_t paramId, float oldValue, float newValue)
{
    if (!std::isfinite(newValue))
        return;
    d_->recordParam(paramId, oldValue, newValue);
    d_->emitParam(paramId, newValue);
    d_->stateChanged();
}

// ---- view state ------------------------------------------------------------------------------------------

std::string EditorModel::saveViewState() const
{
    std::string s = "band=";
    s += d_->doc.active == Band::B ? '1' : '0';
    for (int k = 0; k < kNumBands; ++k) {
        const BandDoc& d = d_->doc.bands[k];
        s += k == 0 ? ";qsA=" : ";qsB=";
        if (!d.qsExplicit) {
            s += '-';
            continue;
        }
        appendFloat(s, d.qsStart);
        s += ',';
        appendFloat(s, d.qsEnd);
    }
    return s;
}

bool EditorModel::restoreViewState(std::string_view text)
{
    Impl& m  = *d_;
    bool  ok = true;
    while (!text.empty()) {
        const size_t     semi = text.find(';');
        std::string_view item = text.substr(0, semi);
        text = semi == std::string_view::npos ? std::string_view() : text.substr(semi + 1);
        const size_t eq = item.find('=');
        if (eq == std::string_view::npos) {
            ok = ok && item.empty();
            continue;
        }
        const std::string_view key = item.substr(0, eq), value = item.substr(eq + 1);
        if (key == "band") {
            if (value == "0" || value == "1")
                m.doc.active = value == "1" ? Band::B : Band::A;
            else
                ok = false;
        } else if (key == "qsA" || key == "qsB") {
            BandDoc& d = m.doc.bands[key == "qsB" ? 1 : 0];
            if (value == "-") {
                d.qsExplicit = false;
                continue;
            }
            const size_t comma = value.find(',');
            float a = 0.f, b = 0.f;
            if (comma == std::string_view::npos || !parseFloat(value.substr(0, comma), a)
                || !parseFloat(value.substr(comma + 1), b) || a < 0.f || b > 1.f || b < a) {
                ok = false;
                continue;
            }
            d.qsExplicit = true;
            d.qsStart    = a;
            d.qsEnd      = b;
        }
    }
    m.stateChanged();
    return ok;
}

} // namespace kick::editor
