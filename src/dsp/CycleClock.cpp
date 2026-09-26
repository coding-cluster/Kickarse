#include "CycleClock.h"

#include <algorithm>
#include <cmath>

namespace kick::dsp {

namespace {

constexpr double kHoldPhase = 0.99999994039535522; // largest float below 1: the end of the cycle

inline double frac(double v) noexcept
{
    v -= std::floor(v);
    return (v >= 0.0 && v < 1.0) ? v : 0.0;
}

} // namespace

void CycleClock::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
}

void CycleClock::reset() noexcept
{
    phase_     = 0.0;
    lastPhase_ = 0.0;
    state_     = config_.source == Source::Triggered ? State::Waiting : State::Running;
}

double CycleClock::cycleSeconds() const noexcept
{
    return config_.freeTime ? config_.lengthMs * 0.001 : config_.rateBeats * 60.0 / bpm_;
}

void CycleClock::beginBlock(const TransportInfo& transport, const Config& config) noexcept
{
    const bool sourceChanged = !configured_ || config.source != config_.source;
    const bool wasPlaying    = playing_;

    if (transport.valid && std::isfinite(transport.bpm) && transport.bpm > 0.0)
        bpm_ = std::clamp(transport.bpm, 10.0, 999.0);
    playing_ = transport.valid && transport.playing;
    dppq_    = bpm_ / (60.0 * sampleRate_);

    config_           = config;
    config_.rateBeats = std::isfinite(config.rateBeats) ? std::max(config.rateBeats, 1e-3) : 1.0;
    config_.lengthMs  = std::isfinite(config.lengthMs) ? std::max(config.lengthMs, 1.0) : 250.0;
    configured_       = true;
    if (sourceChanged)
        reset();

    if (playing_ && std::isfinite(transport.ppq)) {
        if (config_.source == Source::Sync && config_.freeTime && (!wasPlaying || sourceChanged))
            phase_ = frac(transport.ppq * 60.0 / bpm_ * 1000.0 / config_.lengthMs);
        ppq_ = transport.ppq;
    }
    dphase_ = config_.freeTime ? 1000.0 / (config_.lengthMs * sampleRate_) : dppq_ / config_.rateBeats;
}

CycleClock::Tick CycleClock::advance(bool trigger) noexcept
{
    Tick tk{0.0, State::Running, false, false, false};

    if (config_.source == Source::Sync) {
        double p;
        if (config_.freeTime) {
            p = phase_;
            phase_ = frac(phase_ + dphase_);
        } else {
            p = frac(ppq_ / config_.rateBeats);
            ppq_ += dppq_;
        }
        tk.wrapped = p < lastPhase_;
        lastPhase_ = p;
        tk.phase   = p;
        return tk;
    }

    if (trigger) {
        phase_       = 0.0;
        state_       = State::Running;
        tk.triggered = true;
    } else if (state_ == State::Running) {
        phase_ += dphase_;
        if (phase_ >= 1.0) {
            if (config_.oneShot) {
                state_   = State::Holding;
                phase_   = kHoldPhase;
                tk.ended = true;
            } else {
                phase_     = frac(phase_);
                tk.wrapped = true;
            }
        }
    }
    tk.phase = phase_;
    tk.state = state_;
    return tk;
}

} // namespace kick::dsp
