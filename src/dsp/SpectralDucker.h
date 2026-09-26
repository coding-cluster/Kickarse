// Kickarse — spectral ducking: a zero-latency dynamic "reverse EQ" keyed by the sidechain.
//
//  analysis   24 log-spaced 4th-order band-passes (30 Hz – 16 kHz) on the sidechain (level follower
//             with spec attack/release) and on the main signal (fixed 10 / 250 ms follower).
//  target     per band, every control period (~3 kHz):
//               drive    = sidechain level against a sensitivity threshold, soft knee 1:1
//                          cut = 10 log10(1 + (S/T)^2), capped at spec_range
//               presence = how much the main signal occupies the band (relative to its loudest band,
//                          -30 … -6 dB, and an absolute gate at -80 … -60 dBFS)
//               target   = -cut * presence * scale(envelope, depth, band mix)
//  EQ         cascade of 24 TPT-SVF peaking filters at the analysis centres. Neighbouring bells
//             interact strongly, so the bell gains are solved from the targets through a
//             pre-computed, regularised least-squares interaction matrix (Välimäki & Liski's
//             "accurate cascade graphic equaliser" approach): the cascade then reproduces the
//             per-band targets instead of piling up cuts. Coefficients ramp linearly per sample
//             between control ticks, so there is no zipper noise and no latency.
#pragma once

#include <cstdint>

#include "../shared/Params.h"
#include "Svf.h"

namespace kick::dsp {

class SpectralDucker {
public:
    static constexpr int   kBands     = kSpecBands;
    static constexpr int   kMaxChunk  = 256;
    static_assert(kBands % 4 == 0, "the analysis bank processes four bands per SSE register");
    static constexpr int   kMinPeriod = 16;    // control period in samples (scaled up with the rate)
    static constexpr float kLowestHz  = 30.f;
    static constexpr float kHighestHz = 16000.f;

    struct Settings {
        float attackMs    = 5.f;
        float releaseMs   = 120.f;
        float rangeDb     = 18.f;
        float sensitivity = 0.6f;    // 0..1
        bool  multi       = false;   // weight bands between scaleLow / scaleHigh at the crossover
        float crossoverHz = 150.f;
        bool  slope24     = true;
    };

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setSettings(const Settings& s) noexcept;

    // Processes n <= kMaxChunk samples of the two channels in place (the engine passes Mid / Side).
    // det: sidechain detection signal. analysis: main signal used for the presence estimate.
    // scaleLow / scaleHigh: per-sample 0..1 amount for bands below / above the crossover
    // (identical arrays when multiband is off).
    void process(const float* det, const float* analysis, const float* scaleLow, const float* scaleHigh,
                 float* ch0, float* ch1, int n) noexcept;

    float centreHz(int band) const noexcept { return centres_[band]; }
    float cutDb(int band) const noexcept { return targetDb_[band]; }
    float sidechainDb(int band) const noexcept; // follower level, dBFS (for display)
    float deepestCutDb() const noexcept;

private:
    // Two cascaded band-pass SVFs + a peak follower per band, structure-of-arrays so one SSE
    // register carries four bands (kBands is a multiple of 4).
    struct Bank {
        float a1[kBands];
        float a2[kBands];
        float a3[kBands];
        float k[kBands];
        float s1a[kBands];
        float s1b[kBands];
        float s2a[kBands];
        float s2b[kBands];
        float env[kBands];

        void reset() noexcept;
        void tick(float x, float attack, float release) noexcept;
    };

    void applySettings(const Settings& s, bool force) noexcept;
    void computeInteractionMatrix() noexcept;
    void updateTargets(float scaleLow, float scaleHigh, float* a1Out, float* m1Out) noexcept;
    void runCascade(float* ch0, float* ch1, int n) noexcept;

    double sampleRate_ = 48000.0;
    int    ctlPeriod_  = 16;
    int    ctlLeft_    = 1;
    Settings settings_;
    float  scAttack_ = 1.f, scRelease_ = 1.f;
    float  mainAttack_ = 1.f, mainRelease_ = 1.f;
    float  invThreshold_ = 1.f;
    float  weightLow_[kBands] {};       // share of each band that follows scaleLow (LR magnitude)

    Bank scBank_;
    Bank mainBank_;

    float centres_[kBands] {};
    float bellG_[kBands] {};
    float unityA1_[kBands] {};          // bell a1 at 0 dB
    float matrixT_[kBands][kBands] {};  // bell gain dB[b] = sum_j matrixT_[j][b] * target dB[j]
    float targetDb_[kBands] {};

    // Bell coefficient ramps (a1 and the band-pass mix m1; a2 = g a1, a3 = g a2 stay consistent).
    float a1Cur_[kBands] {}, a1Inc_[kBands] {}, a1To_[kBands] {};
    float m1Cur_[kBands] {}, m1Inc_[kBands] {}, m1To_[kBands] {};
    float bellIc1_[2][kBands] {};       // bell integrator states per channel (structure of arrays)
    float bellIc2_[2][kBands] {};

    // Ticks inside the current chunk: sample index and the new bell targets.
    static constexpr int kMaxTicks = kMaxChunk / kMinPeriod + 1;
    int   tickCount_ = 0;
    int   tickPos_[kMaxTicks] {};
    float tickA1_[kMaxTicks][kBands] {};
    float tickM1_[kMaxTicks][kBands] {};
};

} // namespace kick::dsp
