// Kickarse — parameter table. SINGLE SOURCE OF TRUTH for every automatable parameter.
// Values are always in plain units (the ones listed here), never normalised.
// Symbols are stable identifiers used in preset files and host automation: never rename one,
// and only ever APPEND new parameters before kParamCount.
#pragma once

#include <cstdint>

namespace kick {

enum Mode : int       { kModeSync = 0, kModeMidi, kModeAudio, kModeSpectral, kModeRing, kModeCount };
enum TimeMode : int   { kTimeSync = 0, kTimeFree };
enum PlayMode : int   { kPlayLoop = 0, kPlayOneShot };
enum TrigSource : int { kTrigAudio = 0, kTrigSync, kTrigMidi, kTrigContinuous };
enum SpecTarget : int { kSpecTargetDepth = 0, kSpecTargetVolume };
enum BandSolo : int   { kSoloOff = 0, kSoloLow, kSoloHigh };
enum Slope : int      { kSlope12 = 0, kSlope24 };

enum ParamId : uint32_t {
    kParamBypass = 0,
    // core
    kParamMode,
    kParamDepth,
    kParamTimeMode,
    kParamRate,
    kParamLengthMs,
    kParamPlayMode,
    kParamRotate,
    kParamGrid,
    kParamSwing,
    kParamSmooth,
    kParamMidSide,
    kParamOutGain,
    kParamDelta,
    // multiband
    kParamMulti,
    kParamCrossover,
    kParamSlope,
    kParamLoMix,
    kParamHiMix,
    kParamEnvLink,
    kParamBandSolo,
    // triggering
    kParamTrigSource,
    kParamThreshold,
    kParamRetrigMs,
    kParamTrigFilter,
    kParamTrigLowCut,
    kParamTrigHighCut,
    kParamTrigListen,
    kParamMidiNote,
    kParamVelocity,
    // spectral
    kParamSpecAttack,
    kParamSpecRelease,
    kParamSpecRange,
    kParamSpecSens,
    kParamSpecTarget,
    // ring mod
    kParamRingAttack,
    kParamRingRelease,

    kParamCount
};

enum ParamFlags : uint32_t {
    kPFNone    = 0,
    kPFBool    = 1u << 0,
    kPFInteger = 1u << 1,
    kPFLog     = 1u << 2,   // logarithmic knob/slider mapping
    kPFEnum    = 1u << 3,   // integer with labels[] (labels[i] for value min + i)
    kPFBypass  = 1u << 4,   // host bypass designation
};

struct ParamInfo {
    const char* name;       // display name
    const char* shortName;  // <= 8 chars, for hosts with narrow displays
    const char* symbol;     // stable id, [a-zA-Z0-9_]
    const char* unit;
    float min, max, def;
    uint32_t flags;
    const char* const* labels; // enum labels, or nullptr
};

// ---------------------------------------------------------------------------------------------
// Tables

struct NoteValue { const char* label; double beats; }; // beats = quarter notes

inline constexpr NoteValue kRates[] = {
    {"4/1", 16.0}, {"2/1", 8.0}, {"1/1", 4.0},
    {"1/2", 2.0},  {"1/2.", 3.0},  {"1/2T", 4.0 / 3.0},
    {"1/4", 1.0},  {"1/4.", 1.5},  {"1/4T", 2.0 / 3.0},
    {"1/8", 0.5},  {"1/8.", 0.75}, {"1/8T", 1.0 / 3.0},
    {"1/16", 0.25}, {"1/16.", 0.375}, {"1/16T", 1.0 / 6.0},
    {"1/32", 0.125}, {"1/32T", 1.0 / 12.0},
    {"1/64", 0.0625},
};
inline constexpr int kNumRates = int(sizeof(kRates) / sizeof(kRates[0]));
inline constexpr const char* kRateLabels[] = {
    "4/1", "2/1", "1/1", "1/2", "1/2.", "1/2T", "1/4", "1/4.", "1/4T",
    "1/8", "1/8.", "1/8T", "1/16", "1/16.", "1/16T", "1/32", "1/32T", "1/64",
};
static_assert(sizeof(kRateLabels) / sizeof(kRateLabels[0]) == kNumRates, "rate tables out of sync");

inline constexpr NoteValue kGrids[] = {
    {"1/2", 2.0}, {"1/4", 1.0}, {"1/8", 0.5}, {"1/8T", 1.0 / 3.0},
    {"1/16", 0.25}, {"1/16T", 1.0 / 6.0}, {"1/32", 0.125}, {"1/64", 0.0625},
};
inline constexpr int kNumGrids = int(sizeof(kGrids) / sizeof(kGrids[0]));
inline constexpr const char* kGridLabels[] = {
    "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/64",
};
static_assert(sizeof(kGridLabels) / sizeof(kGridLabels[0]) == kNumGrids, "grid tables out of sync");

inline constexpr const char* kModeLabels[]       = {"Sync", "MIDI", "Audio", "Spectral", "Ring Mod"};
inline constexpr const char* kTimeModeLabels[]   = {"Sync", "ms"};
inline constexpr const char* kPlayModeLabels[]   = {"Loop", "One-shot"};
inline constexpr const char* kSlopeLabels[]      = {"12 dB/oct", "24 dB/oct"};
inline constexpr const char* kBandSoloLabels[]   = {"Off", "Low", "High"};
inline constexpr const char* kTrigSourceLabels[] = {"Audio", "Sync", "MIDI", "Continuous"};
inline constexpr const char* kSpecTargetLabels[] = {"Spectral Depth", "Volume"};

inline constexpr int kDefaultRateIndex = 6; // 1/4
inline constexpr int kDefaultGridIndex = 4; // 1/16

inline constexpr ParamInfo kParams[kParamCount] = {
    // name                 short      symbol         unit   min      max       def     flags                 labels
    {"Bypass",             "Bypass",  "bypass",       "",    0.f,     1.f,      0.f,    kPFBool | kPFBypass,  nullptr},
    {"Mode",               "Mode",    "mode",         "",    0.f,     4.f,      0.f,    kPFEnum,              kModeLabels},
    {"Depth",              "Depth",   "depth",        "%",   0.f,     100.f,    100.f,  kPFNone,              nullptr},
    {"Time Mode",          "TimeMode","time_mode",    "",    0.f,     1.f,      0.f,    kPFEnum,              kTimeModeLabels},
    {"Rate",               "Rate",    "rate",         "",    0.f,     float(kNumRates - 1), float(kDefaultRateIndex), kPFEnum, kRateLabels},
    {"Length",             "Length",  "length_ms",    "ms",  5.f,     4000.f,   250.f,  kPFLog,               nullptr},
    {"Play Mode",          "PlayMode","play_mode",    "",    0.f,     1.f,      0.f,    kPFEnum,              kPlayModeLabels},
    {"Rotate",             "Rotate",  "rotate",       "deg", 0.f,     360.f,    0.f,    kPFNone,              nullptr},
    {"Grid",               "Grid",    "grid",         "",    0.f,     float(kNumGrids - 1), float(kDefaultGridIndex), kPFEnum, kGridLabels},
    {"Swing",              "Swing",   "swing",        "%",   0.f,     100.f,    0.f,    kPFNone,              nullptr},
    {"Smooth",             "Smooth",  "smooth",       "%",   0.f,     100.f,    10.f,   kPFNone,              nullptr},
    {"Mid/Side",           "M/S",     "mid_side",     "",    -100.f,  100.f,    0.f,    kPFNone,              nullptr},
    {"Output",             "Output",  "out_gain",     "dB",  -24.f,   12.f,     0.f,    kPFNone,              nullptr},
    {"Delta",              "Delta",   "delta",        "",    0.f,     1.f,      0.f,    kPFBool,              nullptr},

    {"Multiband",          "Multi",   "multi",        "",    0.f,     1.f,      0.f,    kPFBool,              nullptr},
    {"Crossover",          "X-Over",  "crossover",    "Hz",  20.f,    20000.f,  150.f,  kPFLog,               nullptr},
    {"Slope",              "Slope",   "slope",        "",    0.f,     1.f,      1.f,    kPFEnum,              kSlopeLabels},
    {"Low Mix",            "Lo Mix",  "lo_mix",       "%",   0.f,     100.f,    100.f,  kPFNone,              nullptr},
    {"High Mix",           "Hi Mix",  "hi_mix",       "%",   0.f,     100.f,    100.f,  kPFNone,              nullptr},
    {"Link Envelopes",     "Link",    "env_link",     "",    0.f,     1.f,      1.f,    kPFBool,              nullptr},
    {"Band Solo",          "Solo",    "band_solo",    "",    0.f,     2.f,      0.f,    kPFEnum,              kBandSoloLabels},

    {"Trigger Source",     "TrigSrc", "trig_source",  "",    0.f,     3.f,      0.f,    kPFEnum,              kTrigSourceLabels},
    {"Threshold",          "Thresh",  "threshold",    "dB",  -60.f,   0.f,      -24.f,  kPFNone,              nullptr},
    {"Retrigger Hold",     "Hold",    "retrig_ms",    "ms",  0.f,     1000.f,   60.f,   kPFNone,              nullptr},
    {"Trigger Filter",     "TrigFilt","trig_filter",  "",    0.f,     1.f,      0.f,    kPFBool,              nullptr},
    {"Trigger Low Cut",    "TrigLoCt","trig_lowcut",  "Hz",  20.f,    20000.f,  20.f,   kPFLog,               nullptr},
    {"Trigger High Cut",   "TrigHiCt","trig_highcut", "Hz",  20.f,    20000.f,  200.f,  kPFLog,               nullptr},
    {"Trigger Listen",     "TrigLstn","trig_listen",  "",    0.f,     1.f,      0.f,    kPFBool,              nullptr},
    {"MIDI Note",          "Note",    "midi_note",    "",    -1.f,    127.f,    -1.f,   kPFInteger,           nullptr},
    {"Velocity",           "Velocity","velocity",     "%",   0.f,     100.f,    0.f,    kPFNone,              nullptr},

    {"Spectral Attack",    "SpAtk",   "spec_attack",  "ms",  0.1f,    200.f,    5.f,    kPFLog,               nullptr},
    {"Spectral Release",   "SpRel",   "spec_release", "ms",  1.f,     2000.f,   120.f,  kPFLog,               nullptr},
    {"Spectral Range",     "SpRange", "spec_range",   "dB",  0.f,     48.f,     18.f,   kPFNone,              nullptr},
    {"Spectral Sensitivity","SpSens", "spec_sens",    "%",   0.f,     100.f,    60.f,   kPFNone,              nullptr},
    {"Envelope Target",    "EnvTgt",  "spec_target",  "",    0.f,     1.f,      0.f,    kPFEnum,              kSpecTargetLabels},

    {"Ring Attack",        "RgAtk",   "ring_attack",  "ms",  0.01f,   50.f,     0.1f,   kPFLog,               nullptr},
    {"Ring Release",       "RgRel",   "ring_release", "ms",  0.1f,    500.f,    5.f,    kPFLog,               nullptr},
};

// ---------------------------------------------------------------------------------------------
// Non-automatable state (DPF "state" key/value strings, saved with the host project)

inline constexpr const char* kStateEnvA      = "envA";      // low / main band envelope (Envelope::serialize)
inline constexpr const char* kStateEnvB      = "envB";      // high band envelope
inline constexpr const char* kStateUi        = "uiState";   // opaque to the DSP, owned by the UI
inline constexpr const char* kStatePreset    = "presetName";

// Spectral filterbank size (shared so the UI can draw one bar per band)
inline constexpr int kSpecBands = 24;

// Smooth: a one-pole low-pass on the envelope value, time constant 0–20 ms, quadratic in
// smooth01 = Smooth % / 100. Shared so the editor's smoothed-curve overlay matches the DSP.
inline constexpr float kMaxEnvSmoothMs = 20.f;
inline constexpr double envSmoothMs(float smooth01) noexcept
{
    return double(kMaxEnvSmoothMs) * double(smooth01) * double(smooth01);
}

} // namespace kick
