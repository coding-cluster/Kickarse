#include "SpectralDucker.h"

#include <initializer_list>

namespace kick::dsp {

namespace {

constexpr float  kAnalysisQ      = 2.34f;   // per stage: the 4th-order bands cross at -3 dB
constexpr float  kBellQ          = 2.4f;    // chosen with the matrix below for the least interaction error
constexpr double kDesignGainDb   = -18.0;   // reference gain at which the interaction is measured
constexpr double kRidge          = 0.05;    // regularisation: bounds bell boosts next to deep cuts
constexpr float  kMaxBellCutDb   = -72.f;
constexpr float  kMaxBellBoostDb = 12.f;
constexpr float  kMainAttackMs   = 10.f;
constexpr float  kMainReleaseMs  = 250.f;
constexpr float  kLevelFloor     = 1e-6f;   // -120 dBFS

// Magnitude (dB) of a TPT/bilinear RBJ-style peaking filter centred at f0, evaluated at f.
double bellDb(double f, double f0, double gainDb, double q, double sampleRate) noexcept
{
    const double a  = std::pow(10.0, gainDb / 40.0);
    const double w  = std::tan(kPiD * f / sampleRate) / std::tan(kPiD * f0 / sampleRate);
    const double d  = (1.0 - w * w) * (1.0 - w * w);
    const double nu = d + (w * a / q) * (w * a / q);
    const double de = d + (w / (q * a)) * (w / (q * a));
    return 10.0 * std::log10(nu / de);
}

} // namespace

void SpectralDucker::Bank::reset() noexcept
{
    for (int b = 0; b < kBands; ++b)
        s1a[b] = s1b[b] = s2a[b] = s2b[b] = env[b] = 0.f;
}

void SpectralDucker::Bank::tick(float x, float attack, float release) noexcept
{
#ifdef KICK_HAS_SSE
    const __m128 vx   = _mm_set1_ps(x);
    const __m128 two  = _mm_set1_ps(2.f);
    const __m128 att  = _mm_set1_ps(attack);
    const __m128 rel  = _mm_set1_ps(release);
    const __m128 absM = _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));
    for (int b = 0; b < kBands; b += 4) {
        const __m128 A1 = _mm_loadu_ps(a1 + b);
        const __m128 A2 = _mm_loadu_ps(a2 + b);
        const __m128 A3 = _mm_loadu_ps(a3 + b);
        const __m128 K  = _mm_loadu_ps(k + b);
        __m128 S1a = _mm_loadu_ps(s1a + b);
        __m128 S1b = _mm_loadu_ps(s1b + b);
        __m128 S2a = _mm_loadu_ps(s2a + b);
        __m128 S2b = _mm_loadu_ps(s2b + b);

        const __m128 v3 = _mm_sub_ps(vx, S1b);
        const __m128 v1 = _mm_add_ps(_mm_mul_ps(A1, S1a), _mm_mul_ps(A2, v3));
        const __m128 v2 = _mm_add_ps(_mm_add_ps(S1b, _mm_mul_ps(A2, S1a)), _mm_mul_ps(A3, v3));
        S1a = _mm_sub_ps(_mm_mul_ps(two, v1), S1a);
        S1b = _mm_sub_ps(_mm_mul_ps(two, v2), S1b);

        const __m128 w3 = _mm_sub_ps(_mm_mul_ps(K, v1), S2b);
        const __m128 w1 = _mm_add_ps(_mm_mul_ps(A1, S2a), _mm_mul_ps(A2, w3));
        const __m128 w2 = _mm_add_ps(_mm_add_ps(S2b, _mm_mul_ps(A2, S2a)), _mm_mul_ps(A3, w3));
        S2a = _mm_sub_ps(_mm_mul_ps(two, w1), S2a);
        S2b = _mm_sub_ps(_mm_mul_ps(two, w2), S2b);

        const __m128 y    = _mm_and_ps(_mm_mul_ps(K, w1), absM);
        const __m128 e    = _mm_loadu_ps(env + b);
        const __m128 rise = _mm_cmpgt_ps(y, e);
        const __m128 c    = _mm_or_ps(_mm_and_ps(rise, att), _mm_andnot_ps(rise, rel));
        _mm_storeu_ps(env + b, _mm_add_ps(e, _mm_mul_ps(c, _mm_sub_ps(y, e))));
        _mm_storeu_ps(s1a + b, S1a);
        _mm_storeu_ps(s1b + b, S1b);
        _mm_storeu_ps(s2a + b, S2a);
        _mm_storeu_ps(s2b + b, S2b);
    }
#else
    for (int b = 0; b < kBands; ++b) {
        const float v3 = x - s1b[b];
        const float v1 = a1[b] * s1a[b] + a2[b] * v3;
        const float v2 = s1b[b] + a2[b] * s1a[b] + a3[b] * v3;
        s1a[b] = 2.f * v1 - s1a[b];
        s1b[b] = 2.f * v2 - s1b[b];

        const float w3 = k[b] * v1 - s2b[b];
        const float w1 = a1[b] * s2a[b] + a2[b] * w3;
        const float w2 = s2b[b] + a2[b] * s2a[b] + a3[b] * w3;
        s2a[b] = 2.f * w1 - s2a[b];
        s2b[b] = 2.f * w2 - s2b[b];

        const float y = std::fabs(k[b] * w1);
        const float e = env[b];
        env[b] = e + (y > e ? attack : release) * (y - e);
    }
#endif
}

void SpectralDucker::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    ctlPeriod_  = kMinPeriod * std::max(1, int(sampleRate_ / 48000.0 + 0.5));
    ctlPeriod_  = std::min(ctlPeriod_, kMaxChunk);

    const double ratio = double(kHighestHz) / double(kLowestHz);
    for (int b = 0; b < kBands; ++b) {
        const double hz = kLowestHz * std::pow(ratio, double(b) / double(kBands - 1));
        centres_[b] = float(std::min(hz, 0.45 * sampleRate_));

        const float g = svfG(centres_[b], sampleRate_);
        const SvfCoefs an = makeSvf(g, 1.f / kAnalysisQ);
        for (Bank* bank : {&scBank_, &mainBank_}) {
            bank->a1[b] = an.a1;
            bank->a2[b] = an.a2;
            bank->a3[b] = an.a3;
            bank->k[b]  = an.k;
        }
        bellG_[b]   = g;
        unityA1_[b] = makeSvf(g, 1.f / kBellQ).a1;
    }
    mainAttack_  = onePoleCoef(kMainAttackMs, sampleRate_);
    mainRelease_ = onePoleCoef(kMainReleaseMs, sampleRate_);

    computeInteractionMatrix();
    applySettings(settings_, true);
    reset();
}

void SpectralDucker::reset() noexcept
{
    scBank_.reset();
    mainBank_.reset();
    for (int b = 0; b < kBands; ++b) {
        bellIc1_[0][b] = bellIc1_[1][b] = 0.f;
        bellIc2_[0][b] = bellIc2_[1][b] = 0.f;
        a1Cur_[b] = a1To_[b] = unityA1_[b];
        m1Cur_[b] = m1To_[b] = 0.f;
        a1Inc_[b] = m1Inc_[b] = 0.f;
        targetDb_[b] = 0.f;
    }
    ctlLeft_   = 1;
    tickCount_ = 0;
}

void SpectralDucker::setSettings(const Settings& s) noexcept
{
    applySettings(s, false);
}

void SpectralDucker::applySettings(const Settings& s, bool force) noexcept
{
    const bool xoverChanged = s.crossoverHz != settings_.crossoverHz || s.slope24 != settings_.slope24;
    if (force || s.attackMs != settings_.attackMs)
        scAttack_ = onePoleCoef(s.attackMs, sampleRate_);
    if (force || s.releaseMs != settings_.releaseMs)
        scRelease_ = onePoleCoef(s.releaseMs, sampleRate_);
    if (force || s.sensitivity != settings_.sensitivity) {
        // Sensitivity 0 → the sidechain band must reach -6 dBFS to start cutting, 1 → -60 dBFS.
        const float thresholdDb = -6.f - 54.f * clampf(s.sensitivity, 0.f, 1.f);
        invThreshold_ = dbToGain(-thresholdDb);
    }
    if (force || xoverChanged) {
        const float fc = std::max(s.crossoverHz, 1.f);
        for (int b = 0; b < kBands; ++b) {
            const float r  = centres_[b] / fc;
            const float r2 = r * r;
            const float rn = s.slope24 ? r2 * r2 : r2;
            weightLow_[b]  = 1.f / (1.f + rn); // |LP| of the LR crossover; |LP| + |HP| = 1
        }
    }
    settings_ = s;
}

float SpectralDucker::sidechainDb(int band) const noexcept
{
    return 20.f * std::log10(std::max(scBank_.env[band], kLevelFloor));
}

float SpectralDucker::deepestCutDb() const noexcept
{
    float m = 0.f;
    for (int b = 0; b < kBands; ++b)
        m = std::min(m, targetDb_[b]);
    return m;
}

void SpectralDucker::computeInteractionMatrix() noexcept
{
    // Design points: every centre plus the geometric midpoints between neighbours, where the
    // target is the mean of the two neighbouring targets (a smooth curve in log frequency).
    constexpr int P = 2 * kBands - 1;
    double fd[P];
    double interp[P][kBands] = {};
    for (int b = 0; b < kBands; ++b) {
        fd[2 * b]            = centres_[b];
        interp[2 * b][b]     = 1.0;
        if (b + 1 < kBands) {
            fd[2 * b + 1]             = std::sqrt(double(centres_[b]) * double(centres_[b + 1]));
            interp[2 * b + 1][b]      = 0.5;
            interp[2 * b + 1][b + 1]  = 0.5;
        }
    }
    // B[i][j]: response of bell j (normalised by its gain) at design point i.
    double B[P][kBands];
    for (int i = 0; i < P; ++i)
        for (int j = 0; j < kBands; ++j)
            B[i][j] = bellDb(fd[i], centres_[j], kDesignGainDb, kBellQ, sampleRate_) / kDesignGainDb;

    // Normal equations (BᵀB + λI) X = Bᵀ·interp, solved by Gauss–Jordan with partial pivoting.
    double aug[kBands][2 * kBands];
    for (int r = 0; r < kBands; ++r) {
        for (int c = 0; c < kBands; ++c) {
            double n = 0.0, rhs = 0.0;
            for (int i = 0; i < P; ++i) {
                n   += B[i][r] * B[i][c];
                rhs += B[i][r] * interp[i][c];
            }
            aug[r][c]          = n + (r == c ? kRidge : 0.0);
            aug[r][kBands + c] = rhs;
        }
    }
    for (int col = 0; col < kBands; ++col) {
        int pivot = col;
        for (int r = col + 1; r < kBands; ++r)
            if (std::fabs(aug[r][col]) > std::fabs(aug[pivot][col]))
                pivot = r;
        if (pivot != col)
            for (int c = 0; c < 2 * kBands; ++c)
                std::swap(aug[col][c], aug[pivot][c]);
        const double inv = 1.0 / aug[col][col]; // SPD + ridge: never zero
        for (int c = 0; c < 2 * kBands; ++c)
            aug[col][c] *= inv;
        for (int r = 0; r < kBands; ++r) {
            if (r == col)
                continue;
            const double f = aug[r][col];
            if (f != 0.0)
                for (int c = 0; c < 2 * kBands; ++c)
                    aug[r][c] -= f * aug[col][c];
        }
    }
    for (int r = 0; r < kBands; ++r)
        for (int c = 0; c < kBands; ++c)
            matrixT_[c][r] = float(aug[r][kBands + c]);
}

void SpectralDucker::updateTargets(float scaleLow, float scaleHigh, float* a1Out, float* m1Out) noexcept
{
    constexpr float kDbPerLog2 = 6.0205999f; // 20 log10(2)
    float mainDb[kBands];
    float mainMax = -200.f;
    for (int b = 0; b < kBands; ++b) {
        mainDb[b] = kDbPerLog2 * fastLog2(std::max(mainBank_.env[b], kLevelFloor));
        mainMax   = std::max(mainMax, mainDb[b]);
    }

    bool anyCut = false;
    for (int b = 0; b < kBands; ++b) {
        const float presence = smoothstep01((mainDb[b] - mainMax + 30.f) * (1.f / 24.f))
                             * smoothstep01((mainDb[b] + 80.f) * (1.f / 20.f));
        const float x     = std::min(scBank_.env[b] * invThreshold_, 1e6f);
        const float cut   = std::min(0.5f * kDbPerLog2 * fastLog2(1.f + x * x), settings_.rangeDb);
        const float scale = settings_.multi ? weightLow_[b] * scaleLow + (1.f - weightLow_[b]) * scaleHigh
                                            : scaleLow;
        targetDb_[b] = -cut * presence * scale;
        anyCut       = anyCut || targetDb_[b] < -1e-3f;
    }

    if (!anyCut) {
        for (int b = 0; b < kBands; ++b) {
            targetDb_[b] = 0.f;
            a1Out[b]     = unityA1_[b];
            m1Out[b]     = 0.f;
        }
        return;
    }

    float gainDb[kBands] = {};
    for (int j = 0; j < kBands; ++j) {
        const float t = targetDb_[j];
        if (t == 0.f)
            continue;
        for (int b = 0; b < kBands; ++b)
            gainDb[b] += matrixT_[j][b] * t;
    }
    for (int b = 0; b < kBands; ++b) {
        const float gdb = clampf(gainDb[b], kMaxBellCutDb, kMaxBellBoostDb);
        const float a   = fastExp2(gdb * (3.3219281f / 40.f)); // 10^(dB/40)
        const float k = 1.f / (kBellQ * a);
        const float g = bellG_[b];
        a1Out[b] = 1.f / (1.f + g * (g + k));
        m1Out[b] = k * (a * a - 1.f);
    }
}

void SpectralDucker::process(const float* det, const float* analysis, const float* scaleLow,
                             const float* scaleHigh, float* ch0, float* ch1, int n) noexcept
{
    n = std::clamp(n, 0, kMaxChunk);

    // 1. Analysis over the whole chunk (it only reads inputs), collecting control ticks.
    tickCount_ = 0;
    for (int i = 0; i < n; ++i) {
        scBank_.tick(det[i], scAttack_, scRelease_);
        mainBank_.tick(analysis[i], mainAttack_, mainRelease_);
        if (--ctlLeft_ <= 0) {
            ctlLeft_ = ctlPeriod_;
            updateTargets(scaleLow[i], scaleHigh[i], tickA1_[tickCount_], tickM1_[tickCount_]);
            tickPos_[tickCount_++] = i;
        }
    }

    // 2. Bell cascade.
    runCascade(ch0, ch1, n);
}

// Each band's coefficients ramp linearly from one control tick to the next: a ramp starts at its
// tick sample and lands on the target exactly one control period later (the next tick snaps it).

#ifdef KICK_HAS_SSE

namespace {

// Four consecutive bells of the cascade in one register ("wavefront"): lane k runs k samples
// behind lane k-1 and takes its output from the previous step, so four serial bells advance per
// step instead of one. Lanes outside the chunk (pipeline fill/drain) are masked, so the cascade
// still finishes every sample inside the chunk: no latency.
struct Wavefront {
    __m128 a1, da1, m1, dm1, g;
    __m128 p1, p2, y0; // channel 0: integrator states, outputs of the previous step
    __m128 q1, q2, y1; // channel 1
};

template <bool Masked>
inline void wavefrontStep(Wavefront& w, __m128 mask, float x0, float x1) noexcept
{
    const __m128 two = _mm_set1_ps(2.f);
    if constexpr (Masked) {
        w.a1 = _mm_add_ps(w.a1, _mm_and_ps(mask, w.da1));
        w.m1 = _mm_add_ps(w.m1, _mm_and_ps(mask, w.dm1));
    } else {
        w.a1 = _mm_add_ps(w.a1, w.da1);
        w.m1 = _mm_add_ps(w.m1, w.dm1);
    }
    const __m128 a2 = _mm_mul_ps(w.g, w.a1);
    const __m128 a3 = _mm_mul_ps(w.g, a2);

    auto channel = [&](__m128& s1, __m128& s2, __m128& y, float x) {
        const __m128 in = _mm_move_ss(_mm_shuffle_ps(y, y, _MM_SHUFFLE(2, 1, 0, 0)), _mm_set_ss(x));
        const __m128 v3 = _mm_sub_ps(in, s2);
        const __m128 v1 = _mm_add_ps(_mm_mul_ps(w.a1, s1), _mm_mul_ps(a2, v3));
        const __m128 v2 = _mm_add_ps(_mm_add_ps(s2, _mm_mul_ps(a2, s1)), _mm_mul_ps(a3, v3));
        const __m128 n1 = _mm_sub_ps(_mm_mul_ps(two, v1), s1);
        const __m128 n2 = _mm_sub_ps(_mm_mul_ps(two, v2), s2);
        y = _mm_add_ps(in, _mm_mul_ps(w.m1, v1));
        if constexpr (Masked) {
            s1 = _mm_or_ps(_mm_and_ps(mask, n1), _mm_andnot_ps(mask, s1));
            s2 = _mm_or_ps(_mm_and_ps(mask, n2), _mm_andnot_ps(mask, s2));
        } else {
            s1 = n1;
            s2 = n2;
        }
    };
    channel(w.p1, w.p2, w.y0, x0);
    channel(w.q1, w.q2, w.y1, x1);
}

inline float lane3(__m128 v) noexcept
{
    return _mm_cvtss_f32(_mm_shuffle_ps(v, v, _MM_SHUFFLE(3, 3, 3, 3)));
}

} // namespace

void SpectralDucker::runCascade(float* ch0, float* ch1, int n) noexcept
{
    constexpr int kLanes = 4;
    if (n <= 0)
        return;

    // Tick index per sample, and per step the lanes that reach a tick sample on that step.
    int8_t  tickAt[kMaxChunk];
    uint8_t events[kMaxChunk + kLanes] = {};
    for (int i = 0; i < n; ++i)
        tickAt[i] = -1;
    for (int t = 0; t < tickCount_; ++t) {
        tickAt[tickPos_[t]] = int8_t(t);
        for (int k = 0; k < kLanes; ++k)
            events[tickPos_[t] + k] |= uint8_t(1u << k);
    }

    const float  invPeriod = 1.f / float(ctlPeriod_);
    const __m128 laneIdx   = _mm_setr_ps(0.f, 1.f, 2.f, 3.f);
    const __m128 allLanes  = _mm_castsi128_ps(_mm_set1_epi32(-1));
    const int    steps     = n + kLanes - 1;

    for (int g = 0; g < kBands; g += kLanes) {
        Wavefront w;
        w.a1  = _mm_loadu_ps(a1Cur_ + g);
        w.da1 = _mm_loadu_ps(a1Inc_ + g);
        w.m1  = _mm_loadu_ps(m1Cur_ + g);
        w.dm1 = _mm_loadu_ps(m1Inc_ + g);
        w.g   = _mm_loadu_ps(bellG_ + g);
        w.p1  = _mm_loadu_ps(bellIc1_[0] + g);
        w.p2  = _mm_loadu_ps(bellIc2_[0] + g);
        w.q1  = _mm_loadu_ps(bellIc1_[1] + g);
        w.q2  = _mm_loadu_ps(bellIc2_[1] + g);
        w.y0 = w.y1 = _mm_setzero_ps();

        for (int i = 0; i < steps; ++i) {
            if (events[i]) {
                float a1[kLanes], da1[kLanes], m1[kLanes], dm1[kLanes];
                _mm_storeu_ps(a1, w.a1);
                _mm_storeu_ps(da1, w.da1);
                _mm_storeu_ps(m1, w.m1);
                _mm_storeu_ps(dm1, w.dm1);
                for (int k = 0; k < kLanes; ++k) {
                    if (!(events[i] & (1u << k)))
                        continue;
                    const int t = tickAt[i - k];
                    const int b = g + k;
                    a1[k]  = a1To_[b];
                    m1[k]  = m1To_[b];
                    a1To_[b] = tickA1_[t][b];
                    m1To_[b] = tickM1_[t][b];
                    da1[k] = (a1To_[b] - a1[k]) * invPeriod;
                    dm1[k] = (m1To_[b] - m1[k]) * invPeriod;
                }
                w.a1  = _mm_loadu_ps(a1);
                w.da1 = _mm_loadu_ps(da1);
                w.m1  = _mm_loadu_ps(m1);
                w.dm1 = _mm_loadu_ps(dm1);
            }
            const float x0 = i < n ? ch0[i] : 0.f;
            const float x1 = i < n ? ch1[i] : 0.f;
            if (i >= kLanes - 1 && i < n) {
                wavefrontStep<false>(w, allLanes, x0, x1);
            } else {
                // lane k holds sample i - k: valid while 0 <= i - k < n
                const __m128 mask = _mm_and_ps(_mm_cmple_ps(laneIdx, _mm_set1_ps(float(i))),
                                               _mm_cmpgt_ps(laneIdx, _mm_set1_ps(float(i - n))));
                wavefrontStep<true>(w, mask, x0, x1);
            }
            if (i >= kLanes - 1) {
                ch0[i - (kLanes - 1)] = lane3(w.y0);
                ch1[i - (kLanes - 1)] = lane3(w.y1);
            }
        }

        _mm_storeu_ps(a1Cur_ + g, w.a1);
        _mm_storeu_ps(a1Inc_ + g, w.da1);
        _mm_storeu_ps(m1Cur_ + g, w.m1);
        _mm_storeu_ps(m1Inc_ + g, w.dm1);
        _mm_storeu_ps(bellIc1_[0] + g, w.p1);
        _mm_storeu_ps(bellIc2_[0] + g, w.p2);
        _mm_storeu_ps(bellIc1_[1] + g, w.q1);
        _mm_storeu_ps(bellIc2_[1] + g, w.q2);
    }
}

#else

void SpectralDucker::runCascade(float* ch0, float* ch1, int n) noexcept
{
    const float invPeriod = 1.f / float(ctlPeriod_);
    for (int b = 0; b < kBands; ++b) {
        float a1 = a1Cur_[b], da1 = a1Inc_[b];
        float m1 = m1Cur_[b], dm1 = m1Inc_[b];
        const float g = bellG_[b];
        float* chans[2] = {ch0, ch1};
        int t        = 0;
        int nextTick = tickCount_ > 0 ? tickPos_[0] : n;
        for (int i = 0; i < n; ++i) {
            if (i == nextTick) {
                a1       = a1To_[b];
                m1       = m1To_[b];
                a1To_[b] = tickA1_[t][b];
                m1To_[b] = tickM1_[t][b];
                da1      = (a1To_[b] - a1) * invPeriod;
                dm1      = (m1To_[b] - m1) * invPeriod;
                ++t;
                nextTick = t < tickCount_ ? tickPos_[t] : n;
            }
            a1 += da1;
            m1 += dm1;
            const float a2 = g * a1;
            const float a3 = g * a2;
            for (int c = 0; c < 2; ++c) {
                float&      s1 = bellIc1_[c][b];
                float&      s2 = bellIc2_[c][b];
                const float x  = chans[c][i];
                const float v3 = x - s2;
                const float v1 = a1 * s1 + a2 * v3;
                const float v2 = s2 + a2 * s1 + a3 * v3;
                s1 = 2.f * v1 - s1;
                s2 = 2.f * v2 - s2;
                chans[c][i] = x + m1 * v1;
            }
        }
        a1Cur_[b] = a1;
        a1Inc_[b] = da1;
        m1Cur_[b] = m1;
        m1Inc_[b] = dm1;
    }
}

#endif

} // namespace kick::dsp
