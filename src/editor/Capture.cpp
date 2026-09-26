// Kickarse — audio → envelope conversion (see Capture.h).
#include "Capture.h"

#include <algorithm>
#include <cmath>

#include "EnvelopeOps.h"

namespace kick::editor {

namespace {

inline float positiveOr(float v, float fallback) noexcept
{
    return std::isfinite(v) && v > 0.f ? v : fallback;
}

// Linear interpolation of bin samples placed at i / n; the last bin is held to the cycle end.
float sampleContour(const std::vector<float>& c, float timeline) noexcept
{
    const int   n = int(c.size());
    const float f = std::clamp(timeline, 0.f, 1.f) * float(n);
    const int   i = int(std::floor(f));
    if (i >= n - 1)
        return c.back();
    if (i < 0)
        return c.front();
    const float u = f - float(i);
    return c[size_t(i)] + (c[size_t(i) + 1] - c[size_t(i)]) * u;
}

} // namespace

std::vector<float> captureContour(const float* recBuf, int bins, const CaptureOptions& opt)
{
    if (!recBuf || bins <= 0)
        return {};
    const size_t n = size_t(bins);
    std::vector<float> x(n);
    float peak = 0.f;
    for (size_t i = 0; i < n; ++i) {
        const float v = recBuf[i];
        x[i] = std::isfinite(v) ? std::fabs(v) : 0.f;
        peak = std::max(peak, x[i]);
    }
    const float silence = std::isfinite(opt.silenceThreshold) ? std::max(opt.silenceThreshold, 0.f) : 1e-4f;
    if (!(peak > silence))
        return {};

    const double cycleSec = double(positiveOr(opt.cycleSeconds, 0.5f));
    const double binSec   = cycleSec / double(n);

    // Hold: max over the previous holdMs (wrapping: the data is one period of a loop) and the next
    // lookaheadMs (not wrapping, so the cycle's end never anticipates the next hit).
    auto bins_ = [&](float ms) {
        const double sec = std::isfinite(ms) ? std::max(0.0, double(ms)) * 1e-3 : 0.0;
        return std::min(n - 1, size_t(std::lround(sec / binSec)));
    };
    const size_t hold  = bins_(opt.holdMs);
    const size_t ahead = bins_(opt.lookaheadMs);
    std::vector<float> held(n);
    for (size_t i = 0; i < n; ++i) {
        float m = x[i];
        for (size_t k = 1; k <= hold; ++k)
            m = std::max(m, x[(i + n - k) % n]);
        for (size_t k = 1; k <= ahead && i + k < n; ++k)
            m = std::max(m, x[i + k]);
        held[i] = m;
    }

    // Instant attack, exponential release; two passes so the state entering bin 0 is the tail's.
    const double relSec = std::isfinite(opt.releaseMs) ? std::max(0.0, double(opt.releaseMs)) * 1e-3 : 0.0;
    const float  decay  = relSec > 0.0 ? float(std::exp(-binSec / relSec)) : 0.f;
    std::vector<float> env(n);
    float state = 0.f;
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < n; ++i) {
            state  = std::max(held[i], state * decay);
            env[i] = state;
        }
    }

    const float top = *std::max_element(env.begin(), env.end());
    if (!(top > 0.f))
        return {};
    for (float& v : env)
        v = std::clamp(1.f - v / top, 0.f, 1.f);
    return env;
}

bool envelopeFromCapture(const float* recBuf, int bins, const PhaseMap& map, const CaptureOptions& opt,
                         Envelope& out, CaptureStats* stats)
{
    const std::vector<float> contour = captureContour(recBuf, bins, opt);
    if (contour.empty())
        return false;

    // Uniform node-space samples (fromSamples' convention), each read at its timeline position.
    const int m = std::clamp(bins, 64, 4096);
    std::vector<float> ys(static_cast<size_t>(m));
    for (int j = 0; j < m; ++j) {
        const float q = float(j) / float(m);
        ys[size_t(j)] = sampleContour(contour, map.toTimeline(q));
    }

    const int maxNodes = std::clamp(opt.maxNodes, 2, Envelope::kMaxNodes);
    float tol = std::isfinite(opt.tolerance) ? std::clamp(opt.tolerance, 1e-4f, 0.5f) : 0.01f;
    Envelope env = Envelope::fromSamples(ys.data(), m, tol);
    for (int it = 0; it < 40 && env.size() > maxNodes && tol < 0.5f; ++it) {
        tol = std::min(tol * 1.25f, 0.5f);
        env = Envelope::fromSamples(ys.data(), m, tol);
    }
    if (env.size() > maxNodes)
        ops::decimate(env, maxNodes);
    ops::strip(env, ~0u);

    if (stats) {
        float err = 0.f;
        for (int j = 0; j < m; ++j)
            err = std::max(err, std::fabs(env.evaluate(float(j) / float(m)) - ys[size_t(j)]));
        float peak = 0.f;
        for (int i = 0; i < bins; ++i)
            if (std::isfinite(recBuf[i]))
                peak = std::max(peak, std::fabs(recBuf[i]));
        stats->peak          = peak;
        stats->usedTolerance = tol;
        stats->maxError      = err;
        stats->nodes         = env.size();
    }
    out = env;
    return true;
}

} // namespace kick::editor
