#include "RingModulator.h"

namespace kick::dsp {

void RingModulator::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    peakDecay_  = float(std::exp(-1.0 / (kPeakReleaseMs * 0.001 * sampleRate_)));
    floor_      = dbToGain(kFloorDb);
    attackMs_ = releaseMs_ = -1.f; // force a coefficient update
    setTimes(0.1f, 5.f);
    reset();
}

void RingModulator::reset() noexcept
{
    env_  = 0.f;
    peak_ = 0.f;
}

void RingModulator::setTimes(float attackMs, float releaseMs) noexcept
{
    if (attackMs != attackMs_) {
        attackMs_ = attackMs;
        attack_   = onePoleCoef(attackMs, sampleRate_);
    }
    if (releaseMs != releaseMs_) {
        releaseMs_ = releaseMs;
        release_   = onePoleCoef(releaseMs, sampleRate_);
    }
}

} // namespace kick::dsp
