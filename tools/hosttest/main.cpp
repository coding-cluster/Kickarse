// Kickarse -- tiny headless smoke-test host for the built VST2 and CLAP binaries.
//
// Not a general-purpose host: just enough of each ABI (loaded straight from the vendored,
// clean-room headers under external/DPF/distrho/src/) to load Kickarse, activate it, push a
// synthetic kick+bass signal through its 4 inputs for a couple of seconds, round-trip
// parameters and saved state, and confirm the output never goes non-finite.
//
// VST2 host callback opcodes handled below are exactly the ones DPF's own VST2 wrapper
// (external/DPF/distrho/src/DistrhoPluginVST2.cpp) needs answered: audioMasterVersion (its
// VSTPluginMain() refuses to create the plugin at all if this returns 0), audioMasterWantMidi,
// audioMasterGetTime, audioMasterGetSampleRate, audioMasterGetBlockSize and audioMasterAutomate
// -- see the grep-able call sites there for the exact opcode numbers used.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "xaymar-vst2/vst.h"

#include "clap/entry.h"
#include "clap/plugin-factory.h"
#include "clap/ext/audio-ports.h"
#include "clap/ext/state.h"

namespace {

constexpr double   kSampleRate  = 44100.0;
constexpr uint32_t kBlockSize   = 256;
constexpr double   kTestSeconds = 2.0;
constexpr double   kPi          = 3.14159265358979323846;

// ---- synthetic kick+bass test signal, generated on the fly ---------------------------------

float kickSample(double t)
{
    const double beat = std::fmod(t, 0.5); // 120 bpm quarter notes
    const double env  = std::exp(-beat * 25.0);
    const double freq = 30.0 + 80.0 * std::exp(-beat * 18.0);
    return float(env * std::sin(2.0 * kPi * freq * beat));
}

float bassSample(double t)
{
    return float(0.35 * std::sin(2.0 * kPi * 55.0 * t));
}

bool allFinite(const float* buf, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        if (!std::isfinite(buf[i]))
            return false;
    return true;
}

std::string exeDirectory()
{
    char path[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return ".";
    const std::string s(path, n);
    const std::size_t slash = s.find_last_of("\\/");
    return slash == std::string::npos ? "." : s.substr(0, slash);
}

bool fileExists(const std::string& path)
{
    const DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

void logStep(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::printf("\n");
    std::fflush(stdout);
}

// =============================================================================================
// VST2
// =============================================================================================

// The classic, publicly-documented Steinberg VstTimeInfo layout (also reproduced verbatim in
// DPF's own DistrhoPluginVST2.cpp, which is what we're driving here).
struct VstTimeInfo
{
    double  samplePos, sampleRate, nanoSeconds, ppqPos, tempo;
    double  barStartPos, cycleStartPos, cycleEndPos;
    int32_t timeSigNumerator, timeSigDenominator;
    int32_t smpteOffset, smpteFrameRate, samplesToNextClock;
    int32_t flags;
};

enum : int32_t
{
    kVstTransportPlaying = 1 << 1,
    kVstPpqPosValid      = 1 << 9,
    kVstTempoValid       = 1 << 10,
    kVstTimeSigValid     = 1 << 13,
};

struct Vst2Host
{
    VstTimeInfo time{};
    uint64_t    samplesProcessed = 0;
} g_vst2Host;

intptr_t VST_FUNCTION_INTERFACE vst2HostCallback(vst_effect*, VST_HOST_OPCODE opcode, int32_t /*index*/,
                                                  int64_t value, void* /*ptr*/, float opt)
{
    switch (int(opcode))
    {
    case 0x10: // audioMasterGetSampleRate -- DPF reads this back as static_cast<double>(result)
        return intptr_t(kSampleRate);
    case 0x11: // audioMasterGetBlockSize
        return intptr_t(kBlockSize);
    case 0x07: // audioMasterGetTime
    {
        g_vst2Host.time.sampleRate         = kSampleRate;
        g_vst2Host.time.samplePos          = double(g_vst2Host.samplesProcessed);
        g_vst2Host.time.tempo              = 120.0;
        g_vst2Host.time.timeSigNumerator   = 4;
        g_vst2Host.time.timeSigDenominator = 4;
        g_vst2Host.time.ppqPos             = (double(g_vst2Host.samplesProcessed) / kSampleRate) * (120.0 / 60.0);
        g_vst2Host.time.flags = kVstTransportPlaying | kVstPpqPosValid | kVstTempoValid | kVstTimeSigValid;
        return reinterpret_cast<intptr_t>(&g_vst2Host.time);
    }
    case 0x01: // audioMasterVersion -- DPF's VSTPluginMain() refuses to create the plugin at all
               // if this returns 0 (distrho/src/DistrhoPluginVST2.cpp), so this one is mandatory.
        return 2400;
    case 0x00: // audioMasterAutomate (parameter changed notification) -- nothing to do
    case 0x06: // audioMasterWantMidi
    case 0x0F: // audioMasterSizeWindow -- only relevant with an open editor, which we never open
        return 0;
    default:
        (void)value; (void)opt;
        return 0;
    }
}

bool runVst2Test(const std::string& dllPath)
{
    logStep("[VST2] loading %s", dllPath.c_str());

    const HMODULE lib = LoadLibraryA(dllPath.c_str());
    if (lib == nullptr)
    {
        logStep("[VST2] FAIL: LoadLibrary failed (error %lu)", GetLastError());
        return false;
    }

    using VstMainFn = vst_effect* (VST_FUNCTION_INTERFACE*)(vst_host_callback);
    const auto mainFn = reinterpret_cast<VstMainFn>(GetProcAddress(lib, "VSTPluginMain"));
    if (mainFn == nullptr)
    {
        logStep("[VST2] FAIL: VSTPluginMain not exported");
        FreeLibrary(lib);
        return false;
    }

    vst_effect* const fx = mainFn(vst2HostCallback);
    if (fx == nullptr || fx->magic_number != VST_MAGICNUMBER)
    {
        logStep("[VST2] FAIL: VSTPluginMain returned no/invalid effect");
        FreeLibrary(lib);
        return false;
    }

    bool ok = true;

    fx->control(fx, VST_EFFECT_OPCODE_CREATE, 0, 0, nullptr, 0.f); // effOpen
    fx->control(fx, VST_EFFECT_OPCODE_SETSAMPLERATE, 0, 0, nullptr, float(kSampleRate));
    fx->control(fx, VST_EFFECT_OPCODE_SETBLOCKSIZE, 0, intptr_t(kBlockSize), nullptr, 0.f);
    fx->control(fx, VST_EFFECT_OPCODE_SUSPEND, 0, 1, nullptr, 0.f); // resume/activate

    logStep("[VST2] %d params, %d in, %d out, flags=0x%x",
            fx->num_params, fx->num_inputs, fx->num_outputs, fx->flags);

    if (fx->num_inputs != 4 || fx->num_outputs != 2)
    {
        logStep("[VST2] FAIL: expected 4 inputs / 2 outputs, got %d / %d", fx->num_inputs, fx->num_outputs);
        ok = false;
    }

    // ---- process a couple of seconds of synthetic kick+bass ---------------------------------
    std::vector<float> mainL(kBlockSize), mainR(kBlockSize), scL(kBlockSize), scR(kBlockSize);
    std::vector<float> outL(kBlockSize), outR(kBlockSize);
    const float* inputs[4]  = { mainL.data(), mainR.data(), scL.data(), scR.data() };
    float*       outputs[2] = { outL.data(), outR.data() };

    const uint32_t totalBlocks = uint32_t(kTestSeconds * kSampleRate / kBlockSize);
    bool sawFiniteBreak = false;

    for (uint32_t b = 0; b < totalBlocks; ++b)
    {
        for (uint32_t i = 0; i < kBlockSize; ++i)
        {
            const double t = double(g_vst2Host.samplesProcessed + i) / kSampleRate;
            const float  kick = kickSample(t);
            const float  bass = bassSample(t);
            mainL[i] = mainR[i] = bass * 0.8f + kick * 0.1f;
            scL[i]   = scR[i]   = kick;
        }

        fx->process(fx, inputs, outputs, int32_t(kBlockSize));
        g_vst2Host.samplesProcessed += kBlockSize;

        if (!allFinite(outL.data(), kBlockSize) || !allFinite(outR.data(), kBlockSize))
        {
            sawFiniteBreak = true;
            break;
        }
    }

    if (sawFiniteBreak)
    {
        logStep("[VST2] FAIL: non-finite sample in output");
        ok = false;
    }
    else
    {
        logStep("[VST2] processed %u blocks (%.1fs), output stayed finite", totalBlocks, kTestSeconds);
    }

    // ---- parameter round-trip ----------------------------------------------------------------
    int roundTripFailures = 0;
    for (int32_t i = 0; i < fx->num_params; ++i)
    {
        const float original = fx->get_parameter(fx, uint32_t(i));
        const float target   = (original < 0.5f) ? 1.0f : 0.0f;
        fx->set_parameter(fx, uint32_t(i), target);
        const float readback = fx->get_parameter(fx, uint32_t(i));

        if (!std::isfinite(readback) || readback < 0.f || readback > 1.f)
            ++roundTripFailures;

        fx->set_parameter(fx, uint32_t(i), original); // restore
    }
    if (roundTripFailures > 0)
    {
        logStep("[VST2] FAIL: %d/%d parameters returned a non-finite/out-of-range value", roundTripFailures,
                fx->num_params);
        ok = false;
    }
    else
    {
        logStep("[VST2] %d parameters round-tripped through get/setParameter", fx->num_params);
    }

    // ---- explicit bypass (designated parameter, index 0 per Params.h) round-trip ------------
    if (fx->num_params > 0)
    {
        fx->set_parameter(fx, 0, 1.0f);
        const float on = fx->get_parameter(fx, 0);
        fx->set_parameter(fx, 0, 0.0f);
        const float off = fx->get_parameter(fx, 0);
        if (on < 0.5f || off >= 0.5f)
        {
            logStep("[VST2] FAIL: bypass parameter (index 0) did not round-trip (on=%.2f off=%.2f)", on, off);
            ok = false;
        }
        else
        {
            logStep("[VST2] bypass parameter round-tripped");
        }
    }

    // ---- save/load chunk ----------------------------------------------------------------------
    void* chunkPtr = nullptr;
    const intptr_t chunkSize = fx->control(fx, VST_EFFECT_OPCODE_17, 0, 0, &chunkPtr, 0.f); // get chunk
    if (chunkSize <= 0 || chunkPtr == nullptr)
    {
        logStep("[VST2] FAIL: get chunk returned size=%td ptr=%p", chunkSize, chunkPtr);
        ok = false;
    }
    else
    {
        const std::size_t chunkSizeBytes = static_cast<std::size_t>(chunkSize);
        std::vector<char> saved(chunkSizeBytes);
        std::memcpy(saved.data(), chunkPtr, chunkSizeBytes);

        fx->control(fx, VST_EFFECT_OPCODE_18, 0, chunkSize, saved.data(), 0.f); // set chunk (load it back)
        logStep("[VST2] saved and reloaded a %td-byte chunk", chunkSize);
    }

    fx->control(fx, VST_EFFECT_OPCODE_SUSPEND, 0, 0, nullptr, 0.f); // deactivate
    fx->control(fx, VST_EFFECT_OPCODE_DESTROY, 0, 0, nullptr, 0.f); // effClose (frees the effect)

    FreeLibrary(lib);
    return ok;
}

// =============================================================================================
// CLAP
// =============================================================================================

uint32_t CLAP_ABI clapInEventsSize(const clap_input_events_t*) { return 0; }
const clap_event_header_t* CLAP_ABI clapInEventsGet(const clap_input_events_t*, uint32_t) { return nullptr; }
bool CLAP_ABI clapOutEventsTryPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }

clap_input_events_t  g_clapInEvents  = { nullptr, clapInEventsSize, clapInEventsGet };
clap_output_events_t g_clapOutEvents = { nullptr, clapOutEventsTryPush };

const void* CLAP_ABI clapHostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void CLAP_ABI clapHostRequestRestart(const clap_host_t*) {}
void CLAP_ABI clapHostRequestProcess(const clap_host_t*) {}
void CLAP_ABI clapHostRequestCallback(const clap_host_t*) {}

clap_host_t g_clapHost = {
    CLAP_VERSION,
    nullptr,
    "Kickarse Hosttest", "Kickarse", "", "0.2.0",
    clapHostGetExtension, clapHostRequestRestart, clapHostRequestProcess, clapHostRequestCallback,
};

struct MemStream
{
    std::vector<uint8_t> data;
    std::size_t          readPos = 0;
};

int64_t CLAP_ABI clapStreamWrite(const clap_ostream_t* stream, const void* buffer, uint64_t size)
{
    MemStream* const ms = static_cast<MemStream*>(stream->ctx);
    const uint8_t* const p = static_cast<const uint8_t*>(buffer);
    ms->data.insert(ms->data.end(), p, p + size);
    return int64_t(size);
}

int64_t CLAP_ABI clapStreamRead(const clap_istream_t* stream, void* buffer, uint64_t size)
{
    MemStream* const ms = static_cast<MemStream*>(stream->ctx);
    const uint64_t remain = ms->data.size() - ms->readPos;
    const uint64_t n = std::min<uint64_t>(size, remain);
    if (n > 0)
        std::memcpy(buffer, ms->data.data() + ms->readPos, std::size_t(n));
    ms->readPos += n;
    return int64_t(n);
}

bool runClapTest(const std::string& clapPath)
{
    logStep("[CLAP] loading %s", clapPath.c_str());

    const HMODULE lib = LoadLibraryA(clapPath.c_str());
    if (lib == nullptr)
    {
        logStep("[CLAP] FAIL: LoadLibrary failed (error %lu)", GetLastError());
        return false;
    }

    const auto* entry = reinterpret_cast<const clap_plugin_entry_t*>(GetProcAddress(lib, "clap_entry"));
    if (entry == nullptr)
    {
        logStep("[CLAP] FAIL: clap_entry not exported");
        FreeLibrary(lib);
        return false;
    }

    if (!entry->init(clapPath.c_str()))
    {
        logStep("[CLAP] FAIL: entry->init() returned false");
        FreeLibrary(lib);
        return false;
    }

    bool ok = true;
    {
        const auto* factory =
            static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        if (factory == nullptr || factory->get_plugin_count(factory) == 0)
        {
            logStep("[CLAP] FAIL: no plugin factory / zero plugins");
            entry->deinit();
            FreeLibrary(lib);
            return false;
        }

        const clap_plugin_descriptor_t* const desc = factory->get_plugin_descriptor(factory, 0);
        logStep("[CLAP] plugin id=%s name=%s", desc->id, desc->name);

        const clap_plugin_t* const plugin = factory->create_plugin(factory, &g_clapHost, desc->id);
        if (plugin == nullptr || !plugin->init(plugin))
        {
            logStep("[CLAP] FAIL: create_plugin/init failed");
            entry->deinit();
            FreeLibrary(lib);
            return false;
        }

        const auto* audioPorts =
            static_cast<const clap_plugin_audio_ports_t*>(plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
        const uint32_t numInPorts  = audioPorts != nullptr ? audioPorts->count(plugin, true)  : 0;
        const uint32_t numOutPorts = audioPorts != nullptr ? audioPorts->count(plugin, false) : 0;
        logStep("[CLAP] %u input port(s), %u output port(s)", numInPorts, numOutPorts);

        if (!plugin->activate(plugin, kSampleRate, 1, kBlockSize))
        {
            logStep("[CLAP] FAIL: activate() failed");
            plugin->destroy(plugin);
            entry->deinit();
            FreeLibrary(lib);
            return false;
        }
        plugin->start_processing(plugin);

        // Build one clap_audio_buffer_t per input port (main stereo + sidechain stereo) and one
        // for the (single, stereo) output port, using whatever channel counts the plugin reports.
        std::vector<clap_audio_port_info_t> inInfo(numInPorts), outInfo(numOutPorts);
        std::vector<std::vector<float>> inChannelData; // flattened storage, 2 per port assumed
        std::vector<clap_audio_buffer_t> inBuffers(numInPorts), outBuffers(numOutPorts);
        std::vector<std::vector<float*>> inChannelPtrs(numInPorts);
        std::vector<std::vector<float>> outChannelData;
        std::vector<std::vector<float*>> outChannelPtrs(numOutPorts);

        for (uint32_t p = 0; p < numInPorts; ++p)
        {
            audioPorts->get(plugin, p, true, &inInfo[p]);
            for (uint32_t c = 0; c < inInfo[p].channel_count; ++c)
            {
                inChannelData.emplace_back(kBlockSize, 0.f);
                inChannelPtrs[p].push_back(inChannelData.back().data());
            }
            inBuffers[p] = { inChannelPtrs[p].data(), nullptr, inInfo[p].channel_count, 0, 0 };
        }
        for (uint32_t p = 0; p < numOutPorts; ++p)
        {
            audioPorts->get(plugin, p, false, &outInfo[p]);
            for (uint32_t c = 0; c < outInfo[p].channel_count; ++c)
            {
                outChannelData.emplace_back(kBlockSize, 0.f);
                outChannelPtrs[p].push_back(outChannelData.back().data());
            }
            outBuffers[p] = { outChannelPtrs[p].data(), nullptr, outInfo[p].channel_count, 0, 0 };
        }

        uint64_t samplesProcessed = 0;
        const uint32_t totalBlocks = uint32_t(kTestSeconds * kSampleRate / kBlockSize);
        bool sawFiniteBreak = false;

        for (uint32_t b = 0; b < totalBlocks && !sawFiniteBreak; ++b)
        {
            uint32_t ptrIndex = 0;
            for (uint32_t p = 0; p < numInPorts; ++p)
            {
                for (uint32_t c = 0; c < inInfo[p].channel_count; ++c)
                {
                    float* const dst = inChannelPtrs[p][c];
                    for (uint32_t i = 0; i < kBlockSize; ++i)
                    {
                        const double t = double(samplesProcessed + i) / kSampleRate;
                        // port 0 = main (bass-ish), any further port = sidechain (kick)
                        dst[i] = (p == 0) ? (bassSample(t) * 0.8f + kickSample(t) * 0.1f) : kickSample(t);
                    }
                }
                (void)ptrIndex;
            }

            clap_process_t process{};
            process.steady_time          = int64_t(samplesProcessed);
            process.frames_count         = kBlockSize;
            process.transport            = nullptr;
            process.audio_inputs         = inBuffers.data();
            process.audio_outputs        = outBuffers.data();
            process.audio_inputs_count   = numInPorts;
            process.audio_outputs_count  = numOutPorts;
            process.in_events            = &g_clapInEvents;
            process.out_events           = &g_clapOutEvents;

            const clap_process_status status = plugin->process(plugin, &process);
            samplesProcessed += kBlockSize;

            if (status == CLAP_PROCESS_ERROR)
            {
                logStep("[CLAP] FAIL: process() returned CLAP_PROCESS_ERROR");
                ok = false;
                break;
            }

            for (uint32_t p = 0; p < numOutPorts && !sawFiniteBreak; ++p)
                for (uint32_t c = 0; c < outInfo[p].channel_count; ++c)
                    if (!allFinite(outChannelPtrs[p][c], kBlockSize))
                        sawFiniteBreak = true;
        }

        if (sawFiniteBreak)
        {
            logStep("[CLAP] FAIL: non-finite sample in output");
            ok = false;
        }
        else
        {
            logStep("[CLAP] processed %u blocks (%.1fs), output stayed finite", totalBlocks, kTestSeconds);
        }

        // ---- state save/load --------------------------------------------------------------
        const auto* state =
            static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
        if (state != nullptr)
        {
            MemStream mem;
            const clap_ostream_t ostream{ &mem, clapStreamWrite };
            if (!state->save(plugin, &ostream))
            {
                logStep("[CLAP] FAIL: state->save() returned false");
                ok = false;
            }
            else
            {
                const clap_istream_t istream{ &mem, clapStreamRead };
                if (!state->load(plugin, &istream))
                {
                    logStep("[CLAP] FAIL: state->load() returned false");
                    ok = false;
                }
                else
                {
                    logStep("[CLAP] saved and reloaded a %zu-byte state blob", mem.data.size());
                }
            }
        }
        else
        {
            logStep("[CLAP] (no clap.state extension exposed, skipping save/load check)");
        }

        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }

    entry->deinit();
    FreeLibrary(lib);
    return ok;
}

} // namespace

int main()
{
    const std::string dir      = exeDirectory();
    const std::string vst2Path = dir + "\\Kickarse-vst2.dll";
    const std::string clapPath = dir + "\\Kickarse.clap";

    bool vst2Ok = false;
    bool clapOk = false;

    if (fileExists(vst2Path))
        vst2Ok = runVst2Test(vst2Path);
    else
        logStep("[VST2] SKIP: %s not found", vst2Path.c_str());

    if (fileExists(clapPath))
        clapOk = runClapTest(clapPath);
    else
        logStep("[CLAP] SKIP: %s not found", clapPath.c_str());

    logStep("----------------------------------------");
    logStep("VST2: %s", fileExists(vst2Path) ? (vst2Ok ? "PASS" : "FAIL") : "SKIPPED");
    logStep("CLAP: %s", fileExists(clapPath) ? (clapOk ? "PASS" : "FAIL") : "SKIPPED");

    const bool allOk = (!fileExists(vst2Path) || vst2Ok) && (!fileExists(clapPath) || clapOk);
    return allOk ? 0 : 1;
}
