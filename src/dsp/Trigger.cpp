#include "Trigger.h"

namespace kick::dsp {

void SidechainFilter::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    lowHz_ = highHz_ = -1.f; // force a coefficient update
    setCutoffs(20.f, 20000.f);
    reset();
}

void SidechainFilter::reset() noexcept
{
    hp_.reset();
    lp_.reset();
}

void SidechainFilter::setCutoffs(float lowCutHz, float highCutHz) noexcept
{
    constexpr float kButterworthK = 1.41421356f;
    if (lowCutHz != lowHz_) {
        lowHz_ = lowCutHz;
        hpC_   = makeSvf(svfG(lowCutHz, sampleRate_), kButterworthK);
    }
    if (highCutHz != highHz_) {
        highHz_ = highCutHz;
        lpC_    = makeSvf(svfG(highCutHz, sampleRate_), kButterworthK);
    }
}

void TriggerDetector::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    decay_      = float(std::exp(-1.0 / (kReleaseMs * 0.001 * sampleRate_)));
    reset();
}

void TriggerDetector::reset() noexcept
{
    env_      = 0.f;
    armed_    = true;
    holdLeft_ = 0;
}

void TriggerDetector::setThresholdDb(float db) noexcept
{
    thrOn_  = dbToGain(db);
    thrOff_ = dbToGain(db - kHysteresisDb);
}

void TriggerDetector::setHoldMs(float ms) noexcept
{
    holdSamples_ = int32_t(std::max(0.0, double(ms) * 0.001 * sampleRate_) + 0.5);
}

} // namespace kick::dsp
