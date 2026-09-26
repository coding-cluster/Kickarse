// Kickarse — topology-preserving-transform state-variable filter (Simper / Zavalishin).
// Chosen everywhere a filter is modulated (crossover, trigger filter, dynamic EQ) because it stays
// well behaved under fast coefficient changes and keeps precision at low frequencies in float.
#pragma once

#include "DspCommon.h"

namespace kick::dsp {

// g = tan(pi f / fs) (pre-warped cutoff), k = 1 / Q (damping).
struct SvfCoefs {
    float g  = 0.f;
    float k  = 2.f;
    float a1 = 1.f;
    float a2 = 0.f;
    float a3 = 0.f;
};

inline float svfG(double hz, double sampleRate) noexcept
{
    const double nyquistSafe = 0.49 * sampleRate;
    const double f = std::clamp(hz, 1.0, nyquistSafe);
    return float(std::tan(kPiD * f / sampleRate));
}

inline SvfCoefs makeSvf(float g, float k) noexcept
{
    SvfCoefs c;
    c.g  = g;
    c.k  = k;
    c.a1 = 1.f / (1.f + g * (g + k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
    return c;
}

struct SvfState {
    float ic1 = 0.f;
    float ic2 = 0.f;

    void reset() noexcept { ic1 = ic2 = 0.f; }

    // bp = band-pass with peak gain 1/k (multiply by k for unity), lp = low-pass.
    // high-pass = x - k * bp - lp.
    inline void tick(const SvfCoefs& c, float x, float& bp, float& lp) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        bp  = v1;
        lp  = v2;
    }

    inline float lowpass(const SvfCoefs& c, float x) noexcept
    {
        float bp, lp;
        tick(c, x, bp, lp);
        return lp;
    }

    inline float highpass(const SvfCoefs& c, float x) noexcept
    {
        float bp, lp;
        tick(c, x, bp, lp);
        return x - c.k * bp - lp;
    }
};

} // namespace kick::dsp
