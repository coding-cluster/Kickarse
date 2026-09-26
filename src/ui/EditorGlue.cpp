// Kickarse UI — editor glue (see EditorGlue.h).
#include "EditorGlue.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "shared/Shapes.h"

#include "View.h"

namespace kick { namespace ui {

using editor::Band;
using DGL_NAMESPACE::MouseCursor;

namespace {
Band toBand(int b) { return b ? Band::B : Band::A; }

editor::ViewTransform vt(const RectF& r) { return {r.x, r.y, r.w, r.h}; }
} // namespace

EditorGlue::EditorGlue(View& view) : view_(view), m_(view.model())
{
    ed_.setListener(this);
    ctl_ = std::make_unique<editor::EditorController>(ed_);
    ed_.load(m_.env(0), m_.env(1), editor::LoadHistory::Clear, false);
    m_.setHistory(this);
    m_.onHostEnvelope = [this](int band, const Envelope& e) { ed_.loadBand(toBand(band), e, editor::LoadHistory::Keep, false); };
    m_.onHostEditorView = [this](const std::string& s) { if (!s.empty()) ed_.restoreViewState(s); };
    m_.editorViewState = [this] { return ed_.saveViewState(); };
    view.onSnapChanged = [this] { lastSnap_ = -1; sync(); };
    view.onBandChanged = [this] { lastBand_ = -1; sync(); };
    view.onLinkToggle = [this](bool linked) { ed_.setLinkedByUser(linked); };
    view.onApplyShape = [this](const Envelope& e) { ed_.applyShape(e, false); };
    view.onCapture = [this](const float* buf, int bins, Envelope& out) {
        editor::CaptureOptions opt;
        opt.cycleSeconds = std::max(0.01f, view_.live().cycleSeconds);
        const Envelope before = ed_.editEnvelope();
        if (!ed_.applyCapture(buf, bins, opt))
            return false;
        out = ed_.editEnvelope();
        if (auto* w = view_.editorWidget())
            w->startMorph(before, out);
        return true;
    };
}

EditorGlue::~EditorGlue()
{
    ed_.setListener(nullptr);
    m_.setHistory(nullptr);
    m_.onHostEnvelope = nullptr;
    m_.onHostEditorView = nullptr;
    m_.editorViewState = nullptr;
}

void EditorGlue::sync() const
{
    const editor::TimingParams t = editor::TimingParams::fromParamValues(m_.values());
    if (!timingValid_ || t.rotateDeg != lastTiming_.rotateDeg || t.gridIndex != lastTiming_.gridIndex || t.swingPercent != lastTiming_.swingPercent
        || t.timeMode != lastTiming_.timeMode || t.rateIndex != lastTiming_.rateIndex || t.lengthMs != lastTiming_.lengthMs) {
        ed_.setTiming(t);
        lastTiming_ = t;
        timingValid_ = true;
    }
    const int link = (!m_.on(kParamMulti) || m_.on(kParamEnvLink)) ? 1 : 0;
    if (link != lastLink_) {
        ed_.setLinked(m_.on(kParamEnvLink));
        lastLink_ = link;
    }
    const int band = m_.editedBand();
    if (band != lastBand_) {
        ed_.setActiveBand(toBand(m_.on(kParamMulti) ? m_.view.editBand : 0));
        lastBand_ = band;
    }
    const int snap = m_.view.snap ? 1 : 0;
    if (snap != lastSnap_) {
        editor::SnapSettings s = ed_.snap();
        s.enabled = m_.view.snap;
        ed_.setSnap(s);
        lastSnap_ = snap;
    }
}

// ---------------------------------------------------------------------------------------------
// geometry

void EditorGlue::curve(int band, float plotWidthPx, std::vector<EditorPoint>& out) const
{
    sync();
    ed_.buildCurve(toBand(band), plotWidthPx, tmp_, 2.f);
    out.resize(tmp_.size());
    for (std::size_t i = 0; i < tmp_.size(); ++i)
        out[i] = {tmp_[i].x, tmp_[i].y};
}

int EditorGlue::nodeCount(int band) const { return ed_.envelope(toBand(band)).size(); }

EditorPoint EditorGlue::nodePos(int band, int i) const
{
    const editor::Vec2 v = ed_.nodePosition(toBand(band), i);
    return {v.x, v.y};
}

bool EditorGlue::isSelected(int i) const { return ed_.isSelected(i); }

bool EditorGlue::isQsMember(int band, int i) const
{
    const Envelope& e = ed_.envelope(toBand(band));
    return i >= 0 && i < e.size() && (e.node(i).flags & kNodeQuickShift) != 0;
}

bool EditorGlue::segmentHasHandle(int seg) const { return ed_.segmentHasHandle(seg); }

EditorPoint EditorGlue::handlePos(int seg) const
{
    const editor::Vec2 v = ed_.tensionHandlePosition(seg);
    return {v.x, v.y};
}

void EditorGlue::gridLines(std::vector<std::pair<float, bool>>& out, std::vector<int>& index) const
{
    sync();
    ed_.gridLines(grid_);
    out.clear();
    index.clear();
    for (const editor::GridLine& gl : grid_) {
        out.emplace_back(gl.timeline, gl.beat);
        index.push_back(gl.index);
    }
}

int EditorGlue::quickShiftSpans(float out[4]) const
{
    editor::Vec2 v[2];
    const int n = ed_.quickShiftSpans(v);
    for (int i = 0; i < n; ++i) {
        out[i * 2] = v[i].x;
        out[i * 2 + 1] = v[i].y;
    }
    return n;
}

bool EditorGlue::quickShiftEdges(float& t0, float& t1) const
{
    if (!ed_.quickShiftRange().valid)
        return false;
    const editor::Vec2 e = ed_.quickShiftEdges();
    t0 = e.x;
    t1 = e.y;
    return true;
}

float EditorGlue::valueAt(float timeline) const { return ed_.valueAt(timeline); }
float EditorGlue::divisions() const { sync(); return ed_.map().divisions(); }

int EditorGlue::hoverKind() const
{
    switch (ctl_->hover().kind) {
    case editor::HitKind::None:            return 0;
    case editor::HitKind::Empty:           return 1;
    case editor::HitKind::Node:            return 2;
    case editor::HitKind::Segment:         return 3;
    case editor::HitKind::TensionHandle:   return 4;
    case editor::HitKind::QuickShiftLane:  return 5;
    case editor::HitKind::QuickShiftBar:   return 6;
    case editor::HitKind::QuickShiftStart: return 7;
    case editor::HitKind::QuickShiftEnd:   return 8;
    }
    return 0;
}

int EditorGlue::hoverIndex() const { return ctl_->hover().index; }

int EditorGlue::gestureKind() const
{
    switch (ed_.gesture()) {
    case editor::GestureKind::MoveNodes: return 1;
    case editor::GestureKind::Tension:   return 2;
    case editor::GestureKind::Line:      return 3;
    case editor::GestureKind::Pencil:    return 4;
    case editor::GestureKind::QuickShiftMove:
    case editor::GestureKind::QuickShiftStart:
    case editor::GestureKind::QuickShiftEnd: return 5;
    default: return 0;
    }
}

int EditorGlue::gestureIndex() const { return ed_.gestureInfo().index; }

bool EditorGlue::marquee(float& t0, float& v0, float& t1, float& v1) const { return ctl_->marquee(t0, v0, t1, v1); }

bool EditorGlue::line(float& t0, float& v0, float& t1, float& v1) const
{
    if (ed_.gesture() != editor::GestureKind::Line)
        return false;
    const editor::GestureInfo gi = ed_.gestureInfo();
    t0 = gi.lineStart.x; v0 = gi.lineStart.y; t1 = gi.lineEnd.x; v1 = gi.lineEnd.y;
    return true;
}

const std::vector<EditorPoint>& EditorGlue::pencil() const
{
    pen_.clear();
    if (ed_.gesture() == editor::GestureKind::Pencil)
        for (const editor::Vec2& v : ed_.pencilStroke())
            pen_.push_back({v.x, v.y});
    return pen_;
}

bool EditorGlue::readout(std::string& text, float& px, float& py) const
{
    const editor::Readout r = ctl_->readout();
    if (r.kind == editor::ReadoutKind::None)
        return false;
    px = r.px;
    py = r.py;
    const Live& l = view_.live();
    const float cyc = std::max(1e-3f, l.cycleSeconds) * 1000.f;
    char buf[64];
    auto dbAt = [&](float v) {
        const float depth = m_.value(kParamDepth) / 100.f;
        const float g = 1.f - depth * (1.f - v);
        return g > 1e-5f ? 20.f * std::log10(g) : -1e9f;
    };
    switch (r.kind) {
    case editor::ReadoutKind::Node:
    case editor::ReadoutKind::Line: {
        const float ms = r.timeline * cyc, db = dbAt(r.value);
        char dbs[24];
        if (db < -900.f) std::snprintf(dbs, sizeof(dbs), "\xE2\x88\x92\xE2\x88\x9E dB");
        else if (db < -0.05f) std::snprintf(dbs, sizeof(dbs), "\xE2\x88\x92%.1f dB", double(-db));
        else std::snprintf(dbs, sizeof(dbs), "0.0 dB");
        std::snprintf(buf, sizeof(buf), ms < 100.f ? "%.1f ms   %s" : "%.0f ms   %s", double(ms), dbs);
        break;
    }
    case editor::ReadoutKind::Tension:
        std::snprintf(buf, sizeof(buf), "Curve %s%.0f", r.tension >= 0.f ? "+" : "\xE2\x88\x92", double(std::fabs(r.tension * 100.f)));
        break;
    case editor::ReadoutKind::QuickShift:
        std::snprintf(buf, sizeof(buf), "%s%.1f ms", r.shift >= 0.f ? "+" : "\xE2\x88\x92", double(std::fabs(r.shift * cyc)));
        break;
    default:
        return false;
    }
    text = buf;
    return true;
}

MouseCursor EditorGlue::cursor() const
{
    switch (ctl_->cursor()) {
    case editor::CursorHint::Move:             return DGL_NAMESPACE::kMouseCursorAllScroll;
    case editor::CursorHint::Bend:             return DGL_NAMESPACE::kMouseCursorUpDown;
    case editor::CursorHint::ResizeHorizontal: return DGL_NAMESPACE::kMouseCursorLeftRight;
    case editor::CursorHint::Grab:             return DGL_NAMESPACE::kMouseCursorHand;
    case editor::CursorHint::Crosshair:        return DGL_NAMESPACE::kMouseCursorCrosshair;
    default:                                   return DGL_NAMESPACE::kMouseCursorArrow;
    }
}

// ---------------------------------------------------------------------------------------------
// input

void EditorGlue::setGeometry(const RectF& plot, const RectF& lane, bool laneVisible)
{
    sync();
    ctl_->setPlot(vt(plot));
    ctl_->setLane(vt(lane), laneVisible);
}

void EditorGlue::setTool(int tool)
{
    ctl_->setTool(tool == 1 ? editor::Tool::Line : tool == 2 ? editor::Tool::Pencil : editor::Tool::Select);
}

bool EditorGlue::mouseDown(float x, float y, const Pointer& p, int clickCount)
{
    sync();
    setTool(m_.view.tool);
    return ctl_->mouseDown(x, y, mods(p), clickCount);
}

bool EditorGlue::mouseMove(float x, float y, const Pointer& p)
{
    return ctl_->mouseMove(x, y, mods(p));
}

bool EditorGlue::mouseUp(float x, float y, const Pointer& p)
{
    const bool r = ctl_->mouseUp(x, y, mods(p));
    m_.pushViewState();   // Quick Shift ranges live in uiState
    return r;
}

bool EditorGlue::key(unsigned k, const Pointer& p)
{
    using namespace DGL_NAMESPACE;
    editor::Key ek;
    const unsigned lk = (k >= 'A' && k <= 'Z') ? k + 32 : k;
    switch (k) {
    case kKeyDelete:    ek = editor::Key::Delete; break;
    case kKeyBackspace: ek = editor::Key::Backspace; break;
    case kKeyEscape:    ek = editor::Key::Escape; break;
    case kKeyLeft:      ek = editor::Key::Left; break;
    case kKeyRight:     ek = editor::Key::Right; break;
    case kKeyUp:        ek = editor::Key::Up; break;
    case kKeyDown:      ek = editor::Key::Down; break;
    default:
        if (!p.ctrl) return false;
        if (lk == 'a') ek = editor::Key::A;
        else if (lk == 'c') ek = editor::Key::C;
        else if (lk == 'v') ek = editor::Key::V;
        else return false;
        break;
    }
    return ctl_->keyDown(ek, mods(p));
}

void EditorGlue::contextMenu(float x, float y, float px, float py)
{
    sync();
    const editor::Hit h = ed_.hitTest(vt(layout::plot), px, py);
    std::vector<MenuItem> items;
    auto add = [&](const char* label, std::function<void()> fn, bool dim = false) {
        MenuItem it;
        it.label = label;
        it.action = std::move(fn);
        it.dim = dim;
        items.push_back(std::move(it));
    };
    if (h.kind == editor::HitKind::Node) {
        const int i = h.index;
        const bool end = i == 0 || i == ed_.editEnvelope().size() - 1;
        add("Delete node", [this, i] { ed_.deleteNode(i); }, end);
        add(ed_.isQuickShiftMember(i) ? "Remove from quick shift" : "Add to quick shift", [this, i] { ed_.toggleQuickShiftMember(i); }, end);
        add("Straighten segment", [this, i] { ed_.straightenSegment(i); }, i >= ed_.editEnvelope().size() - 1);
    } else {
        add("Select all", [this] { ed_.selectAll(); });
        add("Flip vertically", [this] { ed_.invert(); });
        add("Mirror horizontally", [this] { ed_.reverse(); });
        add("Duplicate (twice as fast)", [this] { ed_.duplicate(); });
        add("Halve", [this] { ed_.halve(); });
        add("Make selection the quick-shift group", [this] { ed_.setQuickShiftGroupFromSelection(); }, ed_.selectionCount() == 0);
        add("Bake Rotate into the shape", [this] { ed_.bakeRotation(); }, m_.value(kParamRotate) == 0.f);
        if (m_.on(kParamMulti) && !m_.on(kParamEnvLink))
            add(ed_.editBand() == Band::A ? "Copy to High band" : "Copy to Low band", [this] { ed_.copyToBand(editor::otherBand(ed_.editBand())); });
        add("Reset to default duck", [this] { ed_.resetToDefault(); });
        items.push_back(MenuItem::sep());
        add("Save shape to User\xE2\x80\xA6", [this] {
            ShapeLibrary* lib = view_.shapes();
            if (lib == nullptr) return;
            char name[32];
            std::snprintf(name, sizeof(name), "Shape %d", int(lib->entries("User").size()) + 1);
            view_.openTextEntry(name, 562.f, 254.f, 160.f, [this](const std::string& n) {
                ShapeLibrary* l2 = view_.shapes();
                if (l2 != nullptr && !n.empty() && l2->saveUser(n, ed_.editEnvelope())) {
                    l2->rescan();
                    m_.view.libTab = "User";
                    m_.view.libPage = 0;
                    m_.pushViewState();
                }
            });
        });
    }
    view_.openMenu(x, y, std::move(items));
}

// ---------------------------------------------------------------------------------------------
// History (the Model records knob edits and whole-envelope replacements here)

void EditorGlue::beginParamGesture(int id) { ed_.beginParameterGesture(uint32_t(id)); }
void EditorGlue::recordParam(int id, float oldValue, float newValue)
{
    if (!applyingHistory_)
        ed_.recordParameterChange(uint32_t(id), oldValue, newValue);
}
void EditorGlue::endParamGesture(int id) { ed_.endParameterGesture(uint32_t(id)); }
void EditorGlue::recordEnvelope(int band, const Envelope&, const Envelope& after)
{
    ed_.loadBand(toBand(band), after, editor::LoadHistory::Record, false);
}
void EditorGlue::beginTransaction(const char* label) { ed_.beginTransaction(label ? label : ""); }
void EditorGlue::commitTransaction() { ed_.commitTransaction(); }
bool EditorGlue::canUndo() const { return ed_.canUndo(); }
bool EditorGlue::canRedo() const { return ed_.canRedo(); }
void EditorGlue::undo() { ed_.undo(); }
void EditorGlue::redo() { ed_.redo(); }

// ---------------------------------------------------------------------------------------------
// EditorListener

void EditorGlue::envelopeChanged(editor::Band band, const Envelope& env, bool final)
{
    const int b = editor::bandIndex(band);
    m_.mirrorEnvelope(b, env);
    const double now = view_.now();
    if (final || now - lastPush_ > 1.0 / 30.0) {   // throttle live pushes during drags
        m_.pushEnvelope(b);
        lastPush_ = now;
    }
    if (final)
        m_.markDirty();
    view_.repaint();
}

void EditorGlue::parameterChanged(uint32_t paramId, float value)
{
    applyingHistory_ = true;
    m_.setNoUndo(int(paramId), value);
    applyingHistory_ = false;
    m_.markDirty();
    view_.repaint();
}

void EditorGlue::historyChanged() { view_.repaint(); }
void EditorGlue::editorStateChanged() { view_.repaint(); }

}} // namespace kick::ui
