// Kickarse — small DSP helpers shared by the engine modules. Header-only, realtime safe.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#if defined(_M_X64) || defined(__x86_64__) || defined(__SSE2__)
#include <emmintrin.h>
#define KICK_HAS_SSE 1
#endif

namespace kick::dsp {

constexpr double kPiD = 3.14159265358979323846;

// Largest sample magnitude we accept (+80 dBFS). Anything beyond is treated as broken input.
constexpr float kMaxSample = 1.0e4f;

inline float clampf(float v, float lo, float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// Replaces NaN/Inf by 0 and clamps absurd magnitudes so nothing downstream can blow up.
inline float sanitise(float x) noexcept
{
    if (std::fabs(x) <= kMaxSample)
        return x; // common path; false for NaN
    if (std::isfinite(x))
        return x > 0.f ? kMaxSample : -kMaxSample;
    return 0.f;
}

inline float dbToGain(float db) noexcept
{
    return std::pow(10.f, 0.05f * db);
}

inline float gainToDb(float g, float floorDb = -120.f) noexcept
{
    const float floorGain = dbToGain(floorDb);
    return g > floorGain ? 20.f * std::log10(g) : floorDb;
}

// One-pole coefficient for a time constant in ms (reaches 63 % after `ms`). 0 ms → 1 (no smoothing).
inline float onePoleCoef(double ms, double sampleRate) noexcept
{
    if (!(ms > 0.0) || !(sampleRate > 0.0))
        return 1.f;
    return float(1.0 - std::exp(-1.0 / (ms * 0.001 * sampleRate)));
}

// Control-rate approximations (continuous, monotonic). fastLog2: |error| < 1.5e-4 (0.001 dB) for
// positive normal inputs; fastExp2: relative error < 5e-6 for x in [-126, 126].
inline float fastLog2(float x) noexcept
{
    uint32_t bits;
    std::memcpy(&bits, &x, sizeof(bits));
    const float e = float(int((bits >> 23) & 0xffu) - 127);
    bits = (bits & 0x007fffffu) | 0x3f800000u;
    float m;
    std::memcpy(&m, &bits, sizeof(m));
    const float t = m - 1.f;
    return e + t * (1.4380732454f + t * (-0.6747666626f + t * (0.3170007211f + t * -0.0803073039f)));
}

inline float fastExp2(float x) noexcept
{
    x = clampf(x, -126.f, 126.f);
    const float fl = std::floor(x);
    const float f  = x - fl;
    const float p  = 1.f + f * (0.6929956551f + f * (0.2415655976f + f * (0.0517522761f + f * 0.0136864712f)));
    const uint32_t bits = uint32_t(int(fl) + 127) << 23;
    float scale;
    std::memcpy(&scale, &bits, sizeof(scale));
    return p * scale;
}

inline float smoothstep01(float x) noexcept
{
    x = clampf(x, 0.f, 1.f);
    return x * x * (3.f - 2.f * x);
}

// Linear-domain parameter smoother (one pole). Used for depth, mixes, gains: cheap and zipper free.
struct Smoothed {
    float value  = 0.f;
    float target = 0.f;
    float coef   = 1.f;

    void setTime(double ms, double sampleRate) noexcept { coef = onePoleCoef(ms, sampleRate); }
    void snap(float v) noexcept { value = target = v; }
    inline float next() noexcept
    {
        value += coef * (target - value);
        return value;
    }
};

// Sets flush-to-zero / denormals-are-zero for the scope (restores the caller's mode afterwards).
class ScopedDenormalGuard {
public:
    ScopedDenormalGuard() noexcept
    {
#ifdef KICK_HAS_SSE
        saved_ = _mm_getcsr();
        _mm_setcsr(saved_ | 0x8040u); // FTZ (bit 15) | DAZ (bit 6)
#endif
    }
    ~ScopedDenormalGuard()
    {
#ifdef KICK_HAS_SSE
        _mm_setcsr(saved_);
#endif
    }
    ScopedDenormalGuard(const ScopedDenormalGuard&) = delete;
    ScopedDenormalGuard& operator=(const ScopedDenormalGuard&) = delete;

private:
    unsigned int saved_ = 0;
};

} // namespace kick::dsp
