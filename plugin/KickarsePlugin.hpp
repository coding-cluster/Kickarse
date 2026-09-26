// Kickarse -- DPF glue for kick::Engine. See docs/ARCHITECTURE.md for the thread/data contract.
#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "DistrhoPlugin.hpp"

#include "dsp/Engine.h"
#include "shared/Params.h"

START_NAMESPACE_DISTRHO

// Custom audio port group id for the sidechain pair. Must start at 0 and be sequential among our
// own (non-predefined) groups -- we only ever declare this one, so 0 it is.
static constexpr uint32_t kPortGroupSidechain = 0;

class KickarsePlugin : public Plugin
{
public:
    KickarsePlugin();

    // Direct-access hook for the UI (DISTRHO_PLUGIN_WANT_DIRECT_ACCESS): KickarseUI reaches the
    // Bridge through here. Only ever touched from the UI/idle thread, never from run().
    kick::Engine& engine() noexcept { return engine_; }

protected:
    // -- Information ---------------------------------------------------------------------------
    const char* getDescription() const override;
    const char* getMaker() const override;
    const char* getLicense() const override;
    uint32_t    getVersion() const override;

    // -- Init -------------------------------------------------------------------------------
    void initAudioPort(bool input, uint32_t index, AudioPort& port) override;
    void initParameter(uint32_t index, Parameter& parameter) override;
    void initPortGroup(uint32_t groupId, PortGroup& group) override;
    void initState(uint32_t index, State& state) override;

    // -- Internal data ------------------------------------------------------------------------
    float getParameterValue(uint32_t index) const override;
    void  setParameterValue(uint32_t index, float value) override;
    String getState(const char* key) const override;
    void   setState(const char* key, const char* value) override;

    // -- Audio/MIDI processing -----------------------------------------------------------------
    void activate() override;
    void run(const float** inputs, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override;

    // -- Callbacks ------------------------------------------------------------------------------
    void bufferSizeChanged(uint32_t newBufferSize) override;
    void sampleRateChanged(double newSampleRate) override;

private:
    kick::Engine engine_;

    // Silences substituted for any audio bus the host leaves disconnected (main should always be
    // connected, but we guard it too -- cheap insurance, never touched on the fast path when the
    // host does connect its buses).
    std::vector<float> silence_;

    // Last-known-good state strings, for getState() -- see docs/ARCHITECTURE.md ("KickarsePlugin
    // keeps the last state strings behind a mutex used only by non-RT threads"). Engine has no
    // getEnvelope(), so these caches are the only source of truth when the host asks to save.
    mutable std::mutex stateMutex_;
    std::string envAState_;
    std::string envBState_;
    std::string uiState_;
    std::string presetState_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KickarsePlugin)
};

END_NAMESPACE_DISTRHO
