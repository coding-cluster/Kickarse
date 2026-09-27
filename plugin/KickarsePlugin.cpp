// Kickarse -- DPF glue for kick::Engine. See docs/ARCHITECTURE.md for the thread/data contract.
#include "KickarsePlugin.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "shared/Envelope.h"

START_NAMESPACE_DISTRHO

// -----------------------------------------------------------------------------------------------

KickarsePlugin::KickarsePlugin()
    : Plugin(kick::kParamCount, 0, 4) // 0 programs, 4 states (envA, envB, uiState, presetName)
{
    // The Plugin constructor contract requires every parameter to already read back its default
    // (ParameterRanges::def) value. Engine owns the actual storage, so push the table's defaults
    // into it here rather than duplicating them.
    for (uint32_t i = 0; i < kick::kParamCount; ++i)
        engine_.setParameter(i, kick::kParams[i].def);

    // Seed the state caches so a host asking for envA/envB before ever calling setState() (e.g.
    // a very first project save) gets a valid, round-trippable envelope instead of an empty string.
    envAState_ = kick::Envelope().serialize();
    envBState_ = kick::Envelope().serialize();
}

// -- Information ----------------------------------------------------------------------------

const char* KickarsePlugin::getDescription() const
{
    return "Envelope-based sidechain / ducking plugin.";
}

const char* KickarsePlugin::getMaker() const
{
    return "Kickarse";
}

const char* KickarsePlugin::getLicense() const
{
    return "GPL-3.0";
}

uint32_t KickarsePlugin::getVersion() const
{
    return d_version(0, 2, 0);
}

// -- Init -------------------------------------------------------------------------------------

void KickarsePlugin::initAudioPort(bool input, uint32_t index, AudioPort& port)
{
    // Inputs: 0,1 = main stereo; 2,3 = sidechain stereo (kAudioPortIsSidechain, its own port
    // group so hosts expose it as a separate aux/sidechain bus instead of folding it into Main).
    // Outputs: 0,1 = main stereo.
    if (input && index >= 2)
    {
        port.hints   = kAudioPortIsSidechain;
        port.groupId = kPortGroupSidechain;
        port.name    = index == 2 ? "Sidechain Left" : "Sidechain Right";
        port.symbol  = index == 2 ? "sidechain_l" : "sidechain_r";
        return;
    }

    port.groupId = kPortGroupStereo;
    if (input)
    {
        port.name   = index == 0 ? "Main Left" : "Main Right";
        port.symbol = index == 0 ? "main_in_l" : "main_in_r";
    }
    else
    {
        port.name   = index == 0 ? "Out Left" : "Out Right";
        port.symbol = index == 0 ? "main_out_l" : "main_out_r";
    }
}

void KickarsePlugin::initParameter(uint32_t index, Parameter& parameter)
{
    if (index >= kick::kParamCount)
        return;

    const kick::ParamInfo& info = kick::kParams[index];

    parameter.hints = kParameterIsAutomatable;
    if (info.flags & kick::kPFBool)
        parameter.hints |= kParameterIsBoolean;
    if (info.flags & (kick::kPFInteger | kick::kPFEnum))
        parameter.hints |= kParameterIsInteger;
    if (info.flags & kick::kPFLog)
        parameter.hints |= kParameterIsLogarithmic;

    parameter.name      = info.name;
    parameter.shortName = info.shortName;
    parameter.symbol    = info.symbol;
    parameter.unit      = info.unit;

    parameter.ranges.def = info.def;
    parameter.ranges.min = info.min;
    parameter.ranges.max = info.max;

    if ((info.flags & kick::kPFEnum) && info.labels != nullptr)
    {
        const int count = int(info.max - info.min) + 1;

        if (count > 0 && count <= 255)
        {
            ParameterEnumerationValue* const values = new ParameterEnumerationValue[count];

            for (int i = 0; i < count; ++i)
            {
                values[i].value = info.min + float(i);
                values[i].label = info.labels[i];
            }

            parameter.enumValues.count          = uint8_t(count);
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values         = values; // deleteLater defaults to true
        }
    }

    // Designation is independent of name/symbol (DPF's format wrappers key off `designation`,
    // not the symbol string), so we can keep our own stable "bypass" symbol from Params.h here.
    if (info.flags & kick::kPFBypass)
        parameter.designation = kParameterDesignationBypass;
}

void KickarsePlugin::initPortGroup(uint32_t groupId, PortGroup& group)
{
    if (groupId == kPortGroupSidechain)
    {
        group.name   = "Sidechain";
        group.symbol = "sidechain";
        return;
    }

    Plugin::initPortGroup(groupId, group);
}

void KickarsePlugin::initState(uint32_t index, State& state)
{
    switch (index)
    {
    case 0:
        state.key          = kick::kStateEnvA;
        state.label        = "Envelope A";
        state.defaultValue = envAState_.c_str();
        break;
    case 1:
        state.key          = kick::kStateEnvB;
        state.label        = "Envelope B";
        state.defaultValue = envBState_.c_str();
        break;
    case 2:
        state.key          = kick::kStateUi;
        state.label        = "UI State";
        state.defaultValue = "";
        // Opaque to the DSP; only meaningful to (and saved on behalf of) the UI.
        state.hints        = kStateIsOnlyForUI;
        break;
    case 3:
        state.key          = kick::kStatePreset;
        state.label        = "Preset Name";
        state.defaultValue = "";
        break;
    }
}

// -- Internal data --------------------------------------------------------------------------

float KickarsePlugin::getParameterValue(uint32_t index) const
{
    return engine_.getParameter(index);
}

void KickarsePlugin::setParameterValue(uint32_t index, float value)
{
    engine_.setParameter(index, value);
}

String KickarsePlugin::getState(const char* key) const
{
    // Called from a non-RT thread only (host save, or the UI reading back). Engine has no
    // getEnvelope(), so envA/envB/uiState/presetName are served from our own cache, kept in sync
    // by setState() below -- see docs/ARCHITECTURE.md.
    const std::lock_guard<std::mutex> lock(stateMutex_);

    if (std::strcmp(key, kick::kStateEnvA) == 0)
        return String(envAState_.c_str());
    if (std::strcmp(key, kick::kStateEnvB) == 0)
        return String(envBState_.c_str());
    if (std::strcmp(key, kick::kStateUi) == 0)
        return String(uiState_.c_str());
    if (std::strcmp(key, kick::kStatePreset) == 0)
        return String(presetState_.c_str());

    return String();
}

void KickarsePlugin::setState(const char* key, const char* value)
{
    // Every DPF format wrapper we checked (distrho/src/DistrhoPluginVST2.cpp "set chunk" opcode,
    // DistrhoPluginVST3.cpp IComponent::setState plus the UI->DSP IMessage path, DistrhoPluginCLAP.cpp
    // state.load plus its UI->DSP path, DistrhoPluginJACK.cpp's UI setState callback) calls this from
    // the host's main/session-load thread or the UI/editor thread -- never from inside process()/run().
    // We still avoid anything that could stall the audio thread: Envelope::deserialize only touches
    // its own stack-local Envelope, and Engine::setEnvelope() is documented as a lock-free, non-
    // blocking publish, so even a mis-behaving host calling this concurrently with run() would not
    // block audio -- only the (non-RT) caller thread does any allocation/parsing.
    const std::lock_guard<std::mutex> lock(stateMutex_);

    if (std::strcmp(key, kick::kStateEnvA) == 0)
    {
        kick::Envelope env;
        if (env.deserialize(value))
        {
            envAState_ = value;
            engine_.setEnvelope(0, env);
        }
    }
    else if (std::strcmp(key, kick::kStateEnvB) == 0)
    {
        kick::Envelope env;
        if (env.deserialize(value))
        {
            envBState_ = value;
            engine_.setEnvelope(1, env);
        }
    }
    else if (std::strcmp(key, kick::kStateUi) == 0)
    {
        uiState_ = value;
    }
    else if (std::strcmp(key, kick::kStatePreset) == 0)
    {
        presetState_ = value;
    }
}

// -- Audio/MIDI processing -------------------------------------------------------------------

void KickarsePlugin::activate()
{
    silence_.assign(getBufferSize(), 0.f);
    engine_.prepare(getSampleRate(), getBufferSize());
    engine_.reset();
}

void KickarsePlugin::run(const float** inputs, float** outputs, uint32_t frames,
                          const MidiEvent* midiEvents, uint32_t midiEventCount)
{
    const float* const zero = (silence_.size() >= frames) ? silence_.data() : nullptr;

    const float* mainIn[2] = {
        inputs[0] != nullptr ? inputs[0] : zero,
        inputs[1] != nullptr ? inputs[1] : zero,
    };
    const float* scIn[2] = {
        inputs[2] != nullptr ? inputs[2] : zero,
        inputs[3] != nullptr ? inputs[3] : zero,
    };

    // ---- Transport --------------------------------------------------------------------------
    // DPF's TimePosition::BarBeatTick has no ready-made "quarter notes since song start" (ppq)
    // field -- every format wrapper (distrho/src/DistrhoPluginVST2.cpp, DistrhoPluginVST3.cpp,
    // DistrhoPluginCLAP.cpp, DistrhoPluginJACK.cpp) reduces the host's native position down to
    // bar/beat/tick + barStartTick instead. We reconstruct ppq from those fields:
    //
    //   ticksSinceStart     = barStartTick + (beat - 1) * ticksPerBeat + tick
    //   beatUnitsSinceStart = ticksSinceStart / ticksPerBeat           -- in "beatType" units:
    //                                                                     1 unit = a (1/beatType)
    //                                                                     note
    //   ppq                 = beatUnitsSinceStart * (4.0 / beatType)   -- convert to quarter notes
    //
    // barStartTick already folds in every bar before the current one; DPF's own VST2/VST3/CLAP
    // wrappers compute it as `ticksPerBeat * beatsPerBar * (bar - 1)` (constant-meter assumption
    // -- the same one they used when deriving bar/beat/tick from the host's raw position). That
    // makes the formula above an exact inverse for VST2 and VST3: with ppqPerBar = beatsPerBar *
    // 4 / beatType, the floor/frac split those wrappers do to get bar/beat/tick cancels out
    // algebraically to beatUnitsSinceStart == ppqPos * beatType / 4, so ppq == ppqPos here.
    // For CLAP, DPF derives bar/beat/tick directly from the host's raw song_pos_beats fixed-point
    // value with no intermediate "ppqPos" of its own, so what we recover is that raw beat count
    // rescaled by 4/beatType -- consistent with DPF's own CLAP bar/beat encoding, but (like that
    // encoding) only equal to true quarter notes when beatType == 4. That's an inherited DPF
    // limitation for non-4 CLAP time signatures, not something fixable from the Plugin side: we
    // only ever see the reduced TimePosition, never CLAP's raw clap_event_transport_t.
    kick::TransportInfo transport;
    const TimePosition& pos = getTimePosition();
    transport.playing = pos.playing;

    // Time signature is display-only (Bridge); keep the 4/4 default when the host gives none.
    if (pos.bbt.valid && pos.bbt.beatsPerBar > 0.0f && pos.bbt.beatType > 0.0f)
    {
        transport.timeSigNum = std::clamp(int(std::lround(pos.bbt.beatsPerBar)), 1, 32);
        transport.timeSigDen = std::clamp(int(std::lround(pos.bbt.beatType)), 1, 32);
    }

    if (pos.bbt.valid && pos.bbt.ticksPerBeat > 0.0 && pos.bbt.beatType > 0.0f)
    {
        transport.valid = true;
        transport.bpm   = pos.bbt.beatsPerMinute > 0.0 ? pos.bbt.beatsPerMinute : 120.0;

        const double ticksSinceStart = pos.bbt.barStartTick
                                      + (pos.bbt.beat - 1) * pos.bbt.ticksPerBeat
                                      + pos.bbt.tick;
        const double beatUnitsSinceStart = ticksSinceStart / pos.bbt.ticksPerBeat;
        transport.ppq = beatUnitsSinceStart * (4.0 / double(pos.bbt.beatType));
    }
    else
    {
        transport.valid = false;
        transport.bpm   = 120.0;
        transport.ppq   = 0.0;
    }

    // ---- MIDI ---------------------------------------------------------------------------------
    // Fixed-size stack array: no allocation on the audio thread. Channel is intentionally dropped
    // (kick::NoteEvent is channel-agnostic per src/dsp/Engine.h).
    static constexpr uint32_t kMaxNoteEvents = 256;
    kick::NoteEvent notes[kMaxNoteEvents];
    uint32_t numNotes = 0;

    for (uint32_t i = 0; i < midiEventCount && numNotes < kMaxNoteEvents; ++i)
    {
        const MidiEvent& ev = midiEvents[i];
        if (ev.size < 3)
            continue;

        const uint8_t status = ev.data[0] & 0xF0;
        if (status != 0x90 && status != 0x80)
            continue;

        kick::NoteEvent& note = notes[numNotes++];
        note.frame    = ev.frame;
        note.note     = ev.data[1];
        note.velocity = (status == 0x80) ? 0 : ev.data[2];
        note.on       = (status == 0x90) && (ev.data[2] > 0);
    }

    engine_.process(mainIn, scIn, outputs, frames, transport, notes, numNotes);
}

void KickarsePlugin::bufferSizeChanged(uint32_t newBufferSize)
{
    silence_.assign(newBufferSize, 0.f);
    engine_.prepare(getSampleRate(), newBufferSize);
}

void KickarsePlugin::sampleRateChanged(double newSampleRate)
{
    engine_.prepare(newSampleRate, getBufferSize());
}

// -----------------------------------------------------------------------------------------------

Plugin* createPlugin()
{
    return new KickarsePlugin();
}

END_NAMESPACE_DISTRHO
