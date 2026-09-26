// Kickarse UI — model (see Model.h) and the local fallback history.
#include "Model.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "Format.h"

namespace kick { namespace ui {

// ---------------------------------------------------------------------------------------------
// ViewState: "key=value;key=value" (opaque to the DSP). The editor's own view state is appended
// verbatim after "|" because it uses ';' itself.

std::string ViewState::serialize() const
{
    std::ostringstream o;
    o << "band=" << editBand << ";tool=" << tool << ";snap=" << (snap ? 1 : 0) << ";qs=" << (qsOn ? 1 : 0)
      << ";in=" << (showIn ? 1 : 0) << ";side=" << (showSide ? 1 : 0) << ";out=" << (showOut ? 1 : 0)
      << ";lib=" << libTab << ";page=" << libPage;
    if (!editor.empty())
        o << "|" << editor;
    return o.str();
}

void ViewState::parse(const std::string& full)
{
    const auto bar = full.find('|');
    const std::string s = full.substr(0, bar);
    editor = bar == std::string::npos ? std::string() : full.substr(bar + 1);
    std::size_t pos = 0;
    while (pos < s.size()) {
        std::size_t end = s.find(';', pos);
        if (end == std::string::npos)
            end = s.size();
        const std::string kv = s.substr(pos, end - pos);
        pos = end + 1;
        const auto eq = kv.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
        const int iv = std::atoi(v.c_str());
        if (k == "band") editBand = iv ? 1 : 0;
        else if (k == "tool") tool = std::clamp(iv, 0, 2);
        else if (k == "snap") snap = iv != 0;
        else if (k == "qs") qsOn = iv != 0;
        else if (k == "in") showIn = iv != 0;
        else if (k == "side") showSide = iv != 0;
        else if (k == "out") showOut = iv != 0;
        else if (k == "lib" && !v.empty()) libTab = v;
        else if (k == "page") libPage = std::max(0, iv);
    }
}

// ---------------------------------------------------------------------------------------------
// LocalHistory: used until/unless the headless editor supplies the real one.

namespace {

class LocalHistory final : public History {
public:
    explicit LocalHistory(Model& m) : m_(m) {}

    void beginParamGesture(int id) override
    {
        gestureId_ = id;
        gestureOpen_ = false;
    }

    void recordParam(int id, float oldValue, float newValue) override
    {
        if (oldValue == newValue)
            return;
        const double t = now();
        if (gestureId_ == id && gestureOpen_ && !undo_.empty()) {
            undo_.back().ops.back().newF = newValue;   // the running drag keeps one entry
            return;
        }
        if (gestureId_ != id && depth_ == 0 && !undo_.empty() && t - lastT_ < 0.5 && undo_.back().ops.size() == 1
            && undo_.back().ops[0].kind == Op::Param && undo_.back().ops[0].id == id) {
            undo_.back().ops[0].newF = newValue;      // wheel / typed repeats coalesce
            lastT_ = t;
            return;
        }
        push({Op::Param, id, oldValue, newValue, {}, {}});
        lastT_ = t;
        if (gestureId_ == id)
            gestureOpen_ = true;
    }

    void endParamGesture(int id) override
    {
        if (gestureId_ == id) {
            gestureId_ = -1;
            gestureOpen_ = false;
            lastT_ = 0.0;
        }
    }

    void recordEnvelope(int band, const Envelope& before, const Envelope& after) override
    {
        const std::string a = before.serialize(), b = after.serialize();
        if (a != b)
            push({Op::Env, band, 0.f, 0.f, a, b});
    }

    void beginTransaction(const char*) override
    {
        if (depth_++ == 0) {
            redo_.clear();
            undo_.push_back({});
            txnOpen_ = true;
        }
    }

    void commitTransaction() override
    {
        if (depth_ == 0 || --depth_ > 0)
            return;
        txnOpen_ = false;
        if (!undo_.empty() && undo_.back().ops.empty())
            undo_.pop_back();
    }

    bool canUndo() const override { return !undo_.empty(); }
    bool canRedo() const override { return !redo_.empty(); }

    void undo() override
    {
        if (undo_.empty())
            return;
        Entry e = std::move(undo_.back());
        undo_.pop_back();
        for (auto it = e.ops.rbegin(); it != e.ops.rend(); ++it)
            apply(*it, true);
        redo_.push_back(std::move(e));
    }

    void redo() override
    {
        if (redo_.empty())
            return;
        Entry e = std::move(redo_.back());
        redo_.pop_back();
        for (const Op& op : e.ops)
            apply(op, false);
        undo_.push_back(std::move(e));
    }

private:
    struct Op {
        enum Kind { Param, Env } kind;
        int id;
        float oldF, newF;
        std::string oldS, newS;
    };
    struct Entry { std::vector<Op> ops; };

    static double now()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void push(Op op)
    {
        if (txnOpen_ && !undo_.empty()) {
            undo_.back().ops.push_back(std::move(op));
            return;
        }
        redo_.clear();
        Entry e;
        e.ops.push_back(std::move(op));
        undo_.push_back(std::move(e));
        if (undo_.size() > 256)
            undo_.erase(undo_.begin());
    }

    void apply(const Op& op, bool backwards)
    {
        if (op.kind == Op::Param) {
            m_.applyFromHistory(op.id, backwards ? op.oldF : op.newF);
        } else {
            Envelope e;
            if (e.deserialize(backwards ? op.oldS : op.newS))
                m_.setEnvelope(op.id, e, false, true);
        }
    }

    Model& m_;
    std::vector<Entry> undo_, redo_;
    int    gestureId_ = -1;
    bool   gestureOpen_ = false;
    int    depth_ = 0;
    bool   txnOpen_ = false;
    double lastT_ = 0.0;
};

} // namespace

// ---------------------------------------------------------------------------------------------

Model::Model(HostIO& host) : host_(host), envB_(Envelope::flat())
{
    for (int i = 0; i < kParamCount; ++i)
        params_[i] = kParams[i].def;
    local_   = std::make_unique<LocalHistory>(*this);
    history_ = local_.get();
}

Model::~Model() = default;

int Model::ivalue(int id) const noexcept
{
    return int(std::lround(params_[id]));
}

void Model::markDirty()
{
    if (!dirty) {
        dirty = true;
        pushPresetState();
    }
}

void Model::beginGesture(int id)
{
    if (gestureParam_ >= 0)
        endGesture(gestureParam_);
    gestureParam_ = id;
    host_.hostEditParameter(std::uint32_t(id), true);
    history_->beginParamGesture(id);
}

void Model::gestureSet(int id, float plain)
{
    plain = clampParam(id, plain);
    if (isStepped(id))
        plain = std::round(plain);
    const float old = params_[id];
    if (old == plain)
        return;
    params_[id] = plain;
    host_.hostSetParameter(std::uint32_t(id), plain);
    history_->recordParam(id, old, plain);
    if (id != kParamBypass)
        markDirty();
    touch();
}

void Model::endGesture(int id)
{
    if (gestureParam_ != id)
        return;
    host_.hostEditParameter(std::uint32_t(id), false);
    history_->endParamGesture(id);
    gestureParam_ = -1;
    touch();
}

void Model::setOnce(int id, float plain)
{
    plain = clampParam(id, plain);
    if (isStepped(id))
        plain = std::round(plain);
    const float old = params_[id];
    if (old == plain)
        return;
    params_[id] = plain;
    host_.hostEditParameter(std::uint32_t(id), true);
    host_.hostSetParameter(std::uint32_t(id), plain);
    host_.hostEditParameter(std::uint32_t(id), false);
    history_->recordParam(id, old, plain);
    if (id != kParamBypass)
        markDirty();
    touch();
}

void Model::setNoUndo(int id, float plain)
{
    plain = clampParam(id, plain);
    if (params_[id] == plain)
        return;
    params_[id] = plain;
    host_.hostEditParameter(std::uint32_t(id), true);
    host_.hostSetParameter(std::uint32_t(id), plain);
    host_.hostEditParameter(std::uint32_t(id), false);
    touch();
}

void Model::hostParameterChanged(int id, float plain)
{
    if (id < 0 || id >= kParamCount || params_[id] == plain)
        return;
    params_[id] = plain;
    touch();
}

// ---------------------------------------------------------------------------------------------
// envelopes

int Model::editedBand() const noexcept
{
    return (on(kParamMulti) && !on(kParamEnvLink) && view.editBand == 1) ? 1 : 0;
}

void Model::setEnvelope(int band, const Envelope& e, bool recordHistory, bool pushToHost)
{
    Envelope& dst = band ? envB_ : envA_;
    if (recordHistory)
        history_->recordEnvelope(band, dst, e);
    dst = e;
    if (pushToHost) {
        pushEnvelope(band);
        markDirty();
    }
    touch();
}

void Model::mirrorEnvelope(int band, const Envelope& e)
{
    (band ? envB_ : envA_) = e;
    touch();
}

void Model::pushEnvelope(int band)
{
    const std::string s = env(band).serialize();
    host_.hostSetState(band ? kStateEnvB : kStateEnvA, s.c_str());
}

void Model::hostStateChanged(const char* key, const char* value)
{
    if (key == nullptr || value == nullptr)
        return;
    if (std::strcmp(key, kStateEnvA) == 0 || std::strcmp(key, kStateEnvB) == 0) {
        Envelope e;
        if (e.deserialize(value)) {
            const int band = std::strcmp(key, kStateEnvA) == 0 ? 0 : 1;
            (band ? envB_ : envA_) = e;
            if (onHostEnvelope)
                onHostEnvelope(band, e);
        }
    } else if (std::strcmp(key, kStateUi) == 0) {
        view.parse(value);
        if (onHostEditorView)
            onHostEditorView(view.editor);
    } else if (std::strcmp(key, kStatePreset) == 0) {
        std::istringstream in(value);
        std::string id, name, cat, d;
        std::getline(in, id);
        std::getline(in, name);
        std::getline(in, cat);
        std::getline(in, d);
        presetId = id;
        if (!name.empty())
            presetName = name;
        presetCategory = cat;
        dirty = d == "1";
    }
    touch();
}

void Model::pushPresetState()
{
    const std::string s = presetId + "\n" + presetName + "\n" + presetCategory + "\n" + (dirty ? "1" : "0");
    host_.hostSetState(kStatePreset, s.c_str());
}

void Model::pushViewState()
{
    if (editorViewState)
        view.editor = editorViewState();
    const std::string s = view.serialize();
    host_.hostSetState(kStateUi, s.c_str());
}

}} // namespace kick::ui
