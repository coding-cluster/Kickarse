// Kickarse — zero-latency, minimum-phase Linkwitz–Riley crossover built from TPT SVFs.
//   LR2 (12 dB/oct): one SVF with Q = 0.5; low = LP, high = -HP. low + high = 1st-order allpass.
//   LR4 (24 dB/oct): Butterworth SVF (Q = 1/sqrt 2) whose LP and HP outputs each go through a second
//                    Butterworth SVF; low + high = 2nd-order allpass.
// Both sums are flat in magnitude, so the (low + high) signal is the phase-matched dry reference.
#pragma once

#include "Svf.h"

namespace kick::dsp {

class Crossover {
public:
    static constexpr int kChannels = 2;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setFrequency(float hz) noexcept;   // cheap enough for control-rate modulation
    void setSlope24(bool slope24) noexcept;
    float frequency() const noexcept { return hz_; }

    inline void process(int ch, float x, float& low, float& high) noexcept
    {
        float bp, lp;
        if (!slope24_) {
            s1_[ch].tick(lr2_, x, bp, lp);
            low  = lp;
            high = -(x - lr2_.k * bp - lp);
            return;
        }
        s1_[ch].tick(bw_, x, bp, lp);
        const float hp = x - bw_.k * bp - lp;
        low  = s2Low_[ch].lowpass(bw_, lp);
        high = s2High_[ch].highpass(bw_, hp);
    }

private:
    void updateCoefs() noexcept;

    double   sampleRate_ = 48000.0;
    float    hz_         = 150.f;
    bool     slope24_    = true;
    SvfCoefs lr2_;
    SvfCoefs bw_;
    SvfState s1_[kChannels];
    SvfState s2Low_[kChannels];
    SvfState s2High_[kChannels];
};

} // namespace kick::dsp
