/*
 * Kickarse -- DPF plugin configuration.
 * See docs/ARCHITECTURE.md for the ownership/threading contract this configuration implies.
 */

#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND    "Kickarse"
#define DISTRHO_PLUGIN_NAME     "Kickarse"
#define DISTRHO_PLUGIN_URI      "urn:kickarse:kickarse"
#define DISTRHO_PLUGIN_CLAP_ID  "com.kickarse.kickarse"
#define DISTRHO_PLUGIN_LABEL    "Kickarse"

// 4-char codes: brand id must be unique per vendor, unique id must be unique per plugin.
#define DISTRHO_PLUGIN_BRAND_ID  KkAr
#define DISTRHO_PLUGIN_UNIQUE_ID Kick

// 2 main in + 2 sidechain in, 2 main out. See KickarsePlugin::initAudioPort/initPortGroup for the
// sidechain aux-bus grouping (kAudioPortIsSidechain + a dedicated "Sidechain" port group).
#define DISTRHO_PLUGIN_NUM_INPUTS  4
#define DISTRHO_PLUGIN_NUM_OUTPUTS 2

#define DISTRHO_PLUGIN_HAS_UI              1
#define DISTRHO_PLUGIN_IS_RT_SAFE          1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS  1
#define DISTRHO_PLUGIN_WANT_STATE          1
#define DISTRHO_PLUGIN_WANT_FULL_STATE     1
#define DISTRHO_PLUGIN_WANT_TIMEPOS        1
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT     1
#define DISTRHO_PLUGIN_WANT_LATENCY        0

#define DISTRHO_UI_USE_NANOVG       1
#define DISTRHO_UI_USER_RESIZABLE   1
// Base size at 100 % (docs/design/DESIGN.md §2.1); the UI scales it by the host DPI factor and the
// user's UI size (100–200 %), keeping the aspect ratio.
#define DISTRHO_UI_DEFAULT_WIDTH   1080
#define DISTRHO_UI_DEFAULT_HEIGHT   660

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Dynamics"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "compressor", "stereo"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
