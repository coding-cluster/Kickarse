// Kickarse — sidechain detection path: optional HP/LP trigger filter and the transient detector.
#pragma once

#include "Svf.h"

namespace kick::dsp {

// 12 dB/oct Butterworth high-pass followed by a 12 dB/oct Butterworth low-pass (mono).
class SidechainFilter {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setCutoffs(float lowCutHz, float highCutHz) noexcept; // control rate

    inline float process(float x) noexcept { return lp_.lowpass(lpC_, hp_.highpass(hpC_, x)); }

private:
    double   sampleRate_ = 48000.0;
    float    lowHz_      = -1.f;
    float    highHz_     = -1.f;
    SvfCoefs hpC_, lpC_;
    SvfState hp_, lp_;
};

// Peak follower (instant attack, 60 ms release) → threshold with 3 dB hysteresis → hold-off.
// The release keeps the follower within the hysteresis band between the half-cycles of a 30 Hz
// kick body, so one kick can never re-arm the detector on its own; the hold-off additionally
// swallows flams and click/body pairs. An event that crosses the threshold during the hold-off
// is consumed (not delayed), so triggers are never late.
class TriggerDetector {
public:
    static constexpr float kHysteresisDb = 3.f;
    static constexpr float kReleaseMs    = 60.f;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setThresholdDb(float db) noexcept;
    void setHoldMs(float ms) noexcept;

    // True on the sample at which a trigger fires.
    inline bool tick(float x) noexcept
    {
        const float a = std::fabs(x);
        env_ = a > env_ ? a : env_ * decay_;
        if (holdLeft_ > 0)
            --holdLeft_;
        if (armed_) {
            if (env_ >= thrOn_) {
                armed_ = false;
                if (holdLeft_ == 0) {
                    holdLeft_ = holdSamples_;
                    return true;
                }
            }
        } else if (env_ < thrOff_) {
            armed_ = true;
        }
        return false;
    }

    float level() const noexcept { return env_; } // follower level the threshold is compared with

private:
    double  sampleRate_  = 48000.0;
    float   decay_       = 0.f;
    float   thrOn_       = 0.063f;
    float   thrOff_      = 0.045f;
    int32_t holdSamples_ = 0;
    int32_t holdLeft_    = 0;
    float   env_         = 0.f;
    bool    armed_       = true;
};

} // namespace kick::dsp
