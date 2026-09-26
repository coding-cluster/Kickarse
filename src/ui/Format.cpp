// Kickarse UI — value formatting (see Format.h). Mirrors fmt() in design/prototype/index.html.
#include "Format.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace kick { namespace ui {

namespace {

const char* const kMinus = "\xE2\x88\x92";  // U+2212

std::string printf1(const char* f, double v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), f, v);
    return buf;
}

} // namespace

std::string niceMinus(std::string s)
{
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '-') {
            s.replace(i, 1, kMinus);
            i += 2;
        }
    }
    return s;
}

float clampParam(int id, float v) noexcept
{
    const ParamInfo& p = kParams[id];
    return std::clamp(v, p.min, p.max);
}

bool isStepped(int id) noexcept
{
    return (kParams[id].flags & (kPFEnum | kPFInteger | kPFBool)) != 0;
}

float toNorm(int id, float plain) noexcept
{
    const ParamInfo& p = kParams[id];
    plain = std::clamp(plain, p.min, p.max);
    if ((p.flags & kPFLog) && p.min > 0.f)
        return std::log(plain / p.min) / std::log(p.max / p.min);
    return p.max > p.min ? (plain - p.min) / (p.max - p.min) : 0.f;
}

float fromNorm(int id, float n) noexcept
{
    const ParamInfo& p = kParams[id];
    n = std::clamp(n, 0.f, 1.f);
    float v = ((p.flags & kPFLog) && p.min > 0.f) ? p.min * std::pow(p.max / p.min, n) : p.min + n * (p.max - p.min);
    if (isStepped(id))
        v = std::round(v);
    return std::clamp(v, p.min, p.max);
}

std::string noteName(int note, bool noteC3)
{
    static const char* const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    note = std::clamp(note, 0, 127);
    const int octave = note / 12 - (noteC3 ? 2 : 0);
    return std::string(names[note % 12]) + (octave < 0 ? kMinus + std::to_string(-octave) : std::to_string(octave));
}

static std::string ms(float v)
{
    if (v < 1.f)    return printf1("%.2f ms", v);
    if (v < 10.f)   return printf1("%.1f ms", v);
    if (v >= 1000.f) return printf1("%.2f s", v / 1000.f);
    return printf1("%.0f ms", v);
}

static std::string hz(float v)
{
    if (v >= 1000.f)
        return printf1(v >= 10000.f ? "%.1f kHz" : "%.2f kHz", v / 1000.f);
    return printf1("%.0f Hz", v);
}

std::string formatParam(int id, float v, bool noteC3)
{
    switch (id) {
    case kParamDepth: case kParamLoMix: case kParamHiMix: case kParamSwing: case kParamSmooth:
    case kParamVelocity: case kParamSpecSens:
        return printf1("%.0f %%", v);
    case kParamRotate:
        return printf1("%.0f", v) + "\xC2\xB0";
    case kParamOutGain:
        if (std::fabs(v) < 0.05f) return "0.0 dB";
        return niceMinus(printf1(v > 0.f ? "+%.1f dB" : "%.1f dB", v));
    case kParamThreshold:
        return niceMinus(printf1("%.1f dB", v));
    case kParamSpecRange:
        return printf1("%.1f dB", v);
    case kParamCrossover: case kParamTrigLowCut: case kParamTrigHighCut:
        return hz(v);
    case kParamLengthMs: case kParamRetrigMs: case kParamSpecAttack: case kParamSpecRelease:
    case kParamRingAttack: case kParamRingRelease:
        return ms(v);
    case kParamMidSide:
        if (std::fabs(v) < 0.5f) return "Mid + Side";
        return v < 0.f ? printf1("Mid %.0f %%", -v) : printf1("Side %.0f %%", v);
    case kParamMidiNote: {
        const int n = int(std::lround(v));
        if (n < 0) return "Any note";
        return noteName(n, noteC3) + "  \xC2\xB7  " + std::to_string(n);
    }
    case kParamRate:
        return kRateLabels[std::clamp(int(std::lround(v)), 0, kNumRates - 1)];
    case kParamGrid:
        return kGridLabels[std::clamp(int(std::lround(v)), 0, kNumGrids - 1)];
    default:
        break;
    }
    const ParamInfo& p = kParams[id];
    if ((p.flags & kPFEnum) && p.labels != nullptr)
        return p.labels[std::clamp(int(std::lround(v - p.min)), 0, int(p.max - p.min))];
    if (p.flags & kPFBool)
        return v > 0.5f ? "On" : "Off";
    return printf1("%.2f", v);
}

const char* paramLabel(int id)
{
    switch (id) {
    case kParamDepth:       return "Depth";
    case kParamLengthMs:    return "Length";
    case kParamRotate:      return "Rotate";
    case kParamSwing:       return "Swing";
    case kParamSmooth:      return "Smooth";
    case kParamMidSide:     return "Mid/Side";
    case kParamOutGain:     return "Output";
    case kParamCrossover:   return "Crossover";
    case kParamLoMix:       return "Low mix";
    case kParamHiMix:       return "High mix";
    case kParamThreshold:   return "Threshold";
    case kParamRetrigMs:    return "Hold";
    case kParamTrigLowCut:  return "Trigger low cut";
    case kParamTrigHighCut: return "Trigger high cut";
    case kParamVelocity:    return "Velocity";
    case kParamSpecAttack:  return "Attack";
    case kParamSpecRelease: return "Release";
    case kParamSpecRange:   return "Range";
    case kParamSpecSens:    return "Sensitivity";
    case kParamRingAttack:  return "Attack";
    case kParamRingRelease: return "Release";
    case kParamMidiNote:    return "Note";
    default:                return kParams[id].name;
    }
}

bool parseValue(const std::string& text, float& out) noexcept
{
    std::string t;
    for (char ch : text)
        if (ch != ' ')
            t.push_back(ch);
    // accept the typographic minus too
    for (std::size_t i = 0; i + 2 < t.size() + 0; ++i)
        if (t.compare(i, 3, kMinus) == 0)
            t.replace(i, 3, "-");
    if (t.empty())
        return false;
    double mult = 1.0;
    if (t.back() == 'k' || t.back() == 'K') {
        mult = 1000.0;
        t.pop_back();
    }
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str())
        return false;
    out = float(v * mult);
    return std::isfinite(out);
}

}} // namespace kick::ui
