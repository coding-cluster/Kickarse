// Kickarse — sidechain "ring" modulator: an audio-rate gain modulator derived from the kick waveform.
//
//   e(n) = attack/release follower of |sidechain|      (ring_attack / ring_release)
//   P(n) = peak of e with instant attack and a slow (2 s) release, floored at -48 dBFS
//   m(n) = e / P  in [0, 1]
//   out  = main * (1 - amount * m)
//
// With fast settings e is essentially the rectified kick waveform, so the bass is multiplied by
// (1 - |kick|/peak): it is carved at the kick's own cycle rate (new sidebands, no slow pump). At full
// amount the sum is bounded: for |bass| <= P, |kick + bass (1 - |kick|/P)| <= P, i.e. the overlap can
// never exceed the kick's peak. Slower attack/release blur m towards a fast ducker.
#pragma once

#include "DspCommon.h"

namespace kick::dsp {

class RingModulator {
public:
    static constexpr float kPeakReleaseMs = 2000.f;
    static constexpr float kFloorDb       = -48.f;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setTimes(float attackMs, float releaseMs) noexcept;

    inline float tick(float sidechain) noexcept
    {
        const float a = std::fabs(sidechain);
        env_ += (a > env_ ? attack_ : release_) * (a - env_);
        peak_ = env_ > peak_ ? env_ : peak_ * peakDecay_;
        const float m = env_ / std::max(peak_, floor_);
        return m < 1.f ? m : 1.f;
    }

private:
    double sampleRate_ = 48000.0;
    float  attackMs_   = -1.f;
    float  releaseMs_  = -1.f;
    float  attack_     = 1.f;
    float  release_    = 1.f;
    float  peakDecay_  = 0.f;
    float  floor_      = 0.004f;
    float  env_        = 0.f;
    float  peak_       = 0.f;
};

} // namespace kick::dsp
