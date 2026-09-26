// Kickarse UI — the UI-side model: cached parameter values, mirrors of the two envelopes, view
// state (serialised into the "uiState" DPF state), preset identity, and the host plumbing rules:
//   parameters  begin → set … → end  (one host gesture, one undo step)
//   envelopes   setState("envA"/"envB", serialize())
//   host → UI   parameterChanged / stateChanged update the cache without creating undo entries.
// Undo is delegated to a History: the headless editor's EditorModel when it is linked (it owns
// envelope edits and records knob edits), otherwise a small local snapshot history.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "shared/Envelope.h"
#include "shared/Params.h"

namespace kick { namespace ui {

// Implemented by the DPF UI (plugin/KickarseUI.cpp).
class HostIO {
public:
    virtual ~HostIO() = default;
    virtual void hostEditParameter(std::uint32_t id, bool started) = 0;
    virtual void hostSetParameter(std::uint32_t id, float value) = 0;
    virtual void hostSetState(const char* key, const char* value) = 0;
};

class History {
public:
    virtual ~History() = default;
    virtual void beginParamGesture(int id) = 0;
    virtual void recordParam(int id, float oldValue, float newValue) = 0;
    virtual void endParamGesture(int id) = 0;
    virtual void recordEnvelope(int band, const Envelope& before, const Envelope& after) = 0;
    virtual void beginTransaction(const char* label) = 0;
    virtual void commitTransaction() = 0;
    virtual bool canUndo() const = 0;
    virtual bool canRedo() const = 0;
    virtual void undo() = 0;
    virtual void redo() = 0;
};

struct ViewState {
    int  editBand = 0;       // 0 = A (low / main), 1 = B (high)
    int  tool     = 0;       // 0 select, 1 line, 2 pencil
    bool snap     = true;
    bool qsOn     = true;
    bool showIn   = true, showSide = true, showOut = true;
    std::string libTab = "Sidechain";
    int  libPage  = 0;
    std::string editor;      // EditorModel::saveViewState() (Quick Shift ranges)

    std::string serialize() const;
    void        parse(const std::string& s);
};

class Model {
public:
    explicit Model(HostIO& host);
    ~Model();

    // ---- parameters ----------------------------------------------------------------------
    float value(int id) const noexcept { return params_[id]; }
    int   ivalue(int id) const noexcept;
    bool  on(int id) const noexcept { return params_[id] > 0.5f; }
    const float* values() const noexcept { return params_; }

    void beginGesture(int id);                 // host edit start; knob drags merge into one step
    void gestureSet(int id, float plain);      // during a gesture
    void endGesture(int id);                   // host edit end
    void setOnce(int id, float plain);         // begin + set + end (click, wheel notch, reset)
    void toggle(int id) { setOnce(id, on(id) ? 0.f : 1.f); }
    void setNoUndo(int id, float plain);       // host begin/set/end, no history entry
    void applyFromHistory(int id, float plain) { setNoUndo(id, plain); }

    void hostParameterChanged(int id, float plain);  // from DPF: no undo entry

    // ---- envelopes (mirrors; the editor owns editing when linked) ----------------------------
    const Envelope& env(int band) const noexcept { return band ? envB_ : envA_; }
    int  editedBand() const noexcept;
    // Replace an envelope (records history unless recordHistory is false) and push it to the host.
    void setEnvelope(int band, const Envelope& e, bool recordHistory = true, bool pushToHost = true);
    void mirrorEnvelope(int band, const Envelope& e);   // editor echo: cache only
    void pushEnvelope(int band);
    void hostStateChanged(const char* key, const char* value);
    std::function<void(int band, const Envelope&)> onHostEnvelope;   // host → editor (no history)
    std::function<void(const std::string&)> onHostEditorView;         // host uiState → editor

    // ---- undo ----------------------------------------------------------------------------------
    void setHistory(History* h) noexcept { history_ = h ? h : local_.get(); }
    History& history() noexcept { return *history_; }
    bool canUndo() const { return history_->canUndo(); }
    bool canRedo() const { return history_->canRedo(); }
    void undo() { history_->undo(); touch(); if (onBulkChange) onBulkChange(); }
    void redo() { history_->redo(); touch(); if (onBulkChange) onBulkChange(); }

    // ---- presets ---------------------------------------------------------------------------
    std::string presetId;       // PresetStore id of the last loaded / saved preset ("" = none)
    std::string presetName = "Init";
    std::string presetCategory;
    bool        dirty = false;
    void markDirty();
    void pushPresetState();     // "presetName" DPF state: id \n name \n category \n dirty

    // ---- view state (persisted in "uiState") -----------------------------------------------
    ViewState view;
    void pushViewState();
    std::function<std::string()> editorViewState;   // collects EditorModel::saveViewState()

    std::uint32_t revision() const noexcept { return revision_; }
    void touch() noexcept { ++revision_; }
    std::function<void()> onBulkChange;   // after undo/redo/preset load: reset transient view state

    HostIO& host() noexcept { return host_; }

private:
    HostIO&  host_;
    float    params_[kParamCount];
    Envelope envA_, envB_;
    int      gestureParam_ = -1;
    std::unique_ptr<History> local_;
    History* history_ = nullptr;
    std::uint32_t revision_ = 1;
};

}} // namespace kick::ui
