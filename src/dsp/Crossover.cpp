#include "Crossover.h"

namespace kick::dsp {

void Crossover::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    updateCoefs();
    reset();
}

void Crossover::reset() noexcept
{
    for (int ch = 0; ch < kChannels; ++ch) {
        s1_[ch].reset();
        s2Low_[ch].reset();
        s2High_[ch].reset();
    }
}

void Crossover::setFrequency(float hz) noexcept
{
    if (hz == hz_)
        return;
    hz_ = hz;
    updateCoefs();
}

void Crossover::setSlope24(bool slope24) noexcept
{
    if (slope24 == slope24_)
        return;
    slope24_ = slope24;
    // The shared first stage keeps its integrator states (a damping change is benign for a TPT
    // SVF); the second stages are idle in LR2 mode, so their stale states are cleared.
    for (int ch = 0; ch < kChannels; ++ch) {
        s2Low_[ch].reset();
        s2High_[ch].reset();
    }
}

void Crossover::updateCoefs() noexcept
{
    const float g = svfG(hz_, sampleRate_);
    lr2_ = makeSvf(g, 2.f);
    bw_  = makeSvf(g, 1.41421356f);
}

} // namespace kick::dsp
