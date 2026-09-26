// Kickarse — PresetData::serialize()/parse() tests: round-trip, enum label/number parsing,
// clamping, and robustness against garbage/partial input.
#include <cmath>
#include <cstring>
#include <string>

#include "TestHarness.h"
#include "shared/Envelope.h"
#include "shared/Presets.h"

using kick::Envelope;
using kick::kParamBypass;
using kick::kParamCount;
using kick::kParams;
using kick::kPFBool;
using kick::kPFEnum;
using kick::kPFInteger;
using kick::PresetData;

namespace {

// A value inside [min,max] chosen per parameter kind, distinct from the Params.h default
// whenever the range allows it, so a round-trip test actually exercises a change.
float pickTestValue(int i)
{
    const auto& info = kParams[i];
    if ((info.flags & kPFEnum) != 0) {
        const int count = int(info.max - info.min) + 1;
        const int idx   = count > 1 ? 1 : 0;
        return info.min + float(idx);
    }
    if ((info.flags & kPFBool) != 0)
        return info.def > 0.5f ? 0.f : 1.f;
    if ((info.flags & kPFInteger) != 0) {
        const float mid = std::floor((info.min + info.max) * 0.5f);
        return mid;
    }
    const float frac = 0.6180339887f; // golden ratio fraction: unlikely to coincide with def
    return info.min + frac * (info.max - info.min);
}

} // namespace

TEST_CASE(preset_defaults_are_sane)
{
    const PresetData d = PresetData::defaults();
    CHECK(!d.name.empty());
    CHECK(!d.category.empty());
    CHECK(!d.author.empty());
    for (int i = 0; i < kParamCount; ++i) {
        CHECK(d.has[i] == (i != kParamBypass));
        CHECK(d.params[i] >= kParams[i].min && d.params[i] <= kParams[i].max);
    }
    Envelope probe;
    CHECK(probe.deserialize(d.envA));
    CHECK(probe.deserialize(d.envB));
}

TEST_CASE(preset_roundtrip_every_param)
{
    for (int i = 0; i < kParamCount; ++i) {
        if (i == kParamBypass)
            continue;
        PresetData d  = PresetData::defaults();
        const float v = pickTestValue(i);
        d.params[i]   = v;
        d.has[i]      = true;

        const std::string text = d.serialize();
        const PresetData  back = PresetData::parse(text);
        CHECK_MSG(back.has[i], "param %s: has[] lost across round-trip", kParams[i].symbol);
        CHECK_NEAR(back.params[i], v, 1e-3);
    }
}

TEST_CASE(preset_roundtrip_metadata_and_envelopes)
{
    PresetData d  = PresetData::defaults();
    d.name        = "My Test Preset";
    d.category    = "Rhythmic";
    d.author      = "Someone";
    Envelope envA = Envelope::flat();
    envA.node(0).y = 0.25f;
    d.envA        = envA.serialize();
    d.envB        = Envelope().serialize();

    const PresetData back = PresetData::parse(d.serialize());
    CHECK(back.name == "My Test Preset");
    CHECK(back.category == "Rhythmic");
    CHECK(back.author == "Someone");
    CHECK(back.envA == d.envA);
    CHECK(back.envB == d.envB);
}

TEST_CASE(preset_enum_label_and_number_both_parse)
{
    for (int i = 0; i < kParamCount; ++i) {
        if ((kParams[i].flags & kPFEnum) == 0 || kParams[i].labels == nullptr)
            continue;
        const int count = int(kParams[i].max - kParams[i].min) + 1;
        const int idx   = count > 1 ? 1 : 0;
        const float expected = kParams[i].min + float(idx);

        std::string textLabel = "version=1\nparam.";
        textLabel += kParams[i].symbol;
        textLabel += "=";
        textLabel += kParams[i].labels[idx];
        textLabel += "\n";
        const PresetData byLabel = PresetData::parse(textLabel);
        CHECK_MSG(byLabel.has[i], "%s: label form not recognised", kParams[i].symbol);
        CHECK_NEAR(byLabel.params[i], expected, 1e-4);

        std::string textNumber = "version=1\nparam.";
        textNumber += kParams[i].symbol;
        textNumber += "=";
        textNumber += std::to_string(expected);
        textNumber += "\n";
        const PresetData byNumber = PresetData::parse(textNumber);
        CHECK_MSG(byNumber.has[i], "%s: numeric form not recognised", kParams[i].symbol);
        CHECK_NEAR(byNumber.params[i], expected, 1e-3);
    }
}

TEST_CASE(preset_case_insensitive_enum_label)
{
    const std::string text = "version=1\nparam.mode=audio\n"; // lower-case "Audio"
    const PresetData  d    = PresetData::parse(text);
    CHECK(d.has[kick::kParamMode]);
    CHECK_NEAR(d.params[kick::kParamMode], float(kick::kModeAudio), 1e-4);
}

TEST_CASE(preset_clamps_out_of_range_values)
{
    const std::string text =
        "version=1\n"
        "param.depth=99999\n"
        "param.threshold=-99999\n"
        "param.mode=99999\n";
    const PresetData d = PresetData::parse(text);
    CHECK(d.has[kick::kParamDepth]);
    CHECK_NEAR(d.params[kick::kParamDepth], kParams[kick::kParamDepth].max, 1e-4);
    CHECK(d.has[kick::kParamThreshold]);
    CHECK_NEAR(d.params[kick::kParamThreshold], kParams[kick::kParamThreshold].min, 1e-4);
    // 99999 is out of the enum's label range but IS a parseable float, so it's clamped like any
    // other numeric value rather than falling back to "unset".
    CHECK(d.has[kick::kParamMode]);
    CHECK_NEAR(d.params[kick::kParamMode], kParams[kick::kParamMode].max, 1e-4);
}

TEST_CASE(preset_bypass_never_read_or_written)
{
    const PresetData fromFile = PresetData::parse("version=1\nparam.bypass=1\nparam.depth=50\n");
    CHECK(!fromFile.has[kParamBypass]);
    CHECK(fromFile.has[kick::kParamDepth]);

    PresetData d       = PresetData::defaults();
    d.has[kParamBypass] = true;
    d.params[kParamBypass] = 1.f;
    const std::string text = d.serialize();
    CHECK(text.find("param.bypass") == std::string::npos);
}

TEST_CASE(preset_missing_params_fall_back_to_default)
{
    const PresetData d = PresetData::parse("version=1\nname=Sparse\nparam.depth=42\n");
    CHECK(d.name == "Sparse");
    CHECK(d.has[kick::kParamDepth]);
    CHECK_NEAR(d.params[kick::kParamDepth], 42.0, 1e-4);
    for (int i = 0; i < kParamCount; ++i) {
        if (i == kick::kParamDepth || i == kParamBypass)
            continue;
        CHECK(!d.has[i]);
        CHECK_NEAR(d.params[i], kParams[i].def, 1e-4);
    }
}

TEST_CASE(preset_parse_never_crashes_on_garbage)
{
    // Kept as a named local (not a temporary) so its c_str() below stays valid for the loop.
    const std::string hugeBlob(200000, 'x');
    const char* garbageInputs[] = {
        "",
        "\n\n\n",
        "not a preset file at all, just prose.",
        "====\x01\x02\x03====",
        "param.=nokey\n",
        "=novalue\n",
        "param.depth\n",              // no '='
        "param.depth=\n",             // empty value
        "state.envA=not an envelope\n",
        "state.envA=KE1\n",           // no nodes
        "param.mode=Sync   # a trailing comment\n",
        hugeBlob.c_str(),             // no keys at all, just a very long single "line"
    };
    for (const char* text : garbageInputs) {
        const PresetData d = PresetData::parse(text);
        // Never crashes (we got here), and always leaves the object in a playable state.
        Envelope probe;
        CHECK(probe.deserialize(d.envA));
        CHECK(probe.deserialize(d.envB));
        for (int i = 0; i < kParamCount; ++i)
            CHECK(d.params[i] >= kParams[i].min && d.params[i] <= kParams[i].max);
    }
}

TEST_CASE(preset_invalid_envelope_falls_back_to_default)
{
    PresetData def = PresetData::defaults();
    const PresetData d = PresetData::parse("version=1\nstate.envA=garbage\nstate.envB=KE1 1,1,0,0\n");
    CHECK(d.envA == def.envA); // invalid text: default envA kept
    Envelope probe;
    CHECK(probe.deserialize(d.envB)); // "KE1 1,1,0,0" has only one node -> also rejected -> default
    CHECK(d.envB == def.envB);
}

TEST_CASE(preset_unknown_keys_are_ignored)
{
    const std::string text =
        "version=1\n"
        "totally_unknown_key=123\n"
        "param.not_a_real_symbol=5\n"
        "param.depth=77\n";
    const PresetData d = PresetData::parse(text);
    CHECK(d.has[kick::kParamDepth]);
    CHECK_NEAR(d.params[kick::kParamDepth], 77.0, 1e-4);
}

TEST_CASE(preset_serialize_only_writes_has_true_params)
{
    PresetData d = PresetData::defaults();
    for (int i = 0; i < kParamCount; ++i)
        d.has[i] = false;
    d.has[kick::kParamDepth] = true;
    d.params[kick::kParamDepth] = 55.f;

    const std::string text = d.serialize();
    CHECK(text.find("param.depth=55") != std::string::npos);
    for (int i = 0; i < kParamCount; ++i) {
        if (i == kick::kParamDepth)
            continue;
        std::string key = "param.";
        key += kParams[i].symbol;
        key += "=";
        CHECK_MSG(text.find(key) == std::string::npos, "unexpected key in output: %s", key.c_str());
    }
}
