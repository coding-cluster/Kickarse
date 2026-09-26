// Kickarse — editor tests: audio → envelope capture on a synthetic kick contour.
#include "EditorTest.h"

using namespace et;

namespace {

constexpr int   kBins  = 1024;   // Bridge::kRecBins
constexpr float kCycle = 0.5f;   // a quarter note at 120 bpm
constexpr float kTau   = 0.07f;  // body decay time constant, seconds

float kickAmp(float t)
{
    return std::exp(-t / kTau);
}

// Peak |x| per bin of a pitched, decaying kick with a click and a little noise, like the engine's
// recBuf (raw per-bin peaks that ripple at the kick's frequency). hitAt = timeline phase of the hit.
std::vector<float> synthKick(float hitAt = 0.f)
{
    constexpr float fs = 48000.f;
    const int       n  = int(kCycle * fs);
    std::vector<float> bins(size_t(kBins), 0.f);
    Rng   rng(99);
    float phase = 0.f;
    for (int i = 0; i < n; ++i) {
        float t = float(i) / fs - hitAt * kCycle;
        if (t < 0.f)
            t += kCycle;
        const float freq = 50.f + 100.f * std::exp(-t / 0.03f);   // pitch sweep 150 → 50 Hz
        phase += 6.2831853f * freq / fs;
        const float x = kickAmp(t) * std::sin(phase) + 0.3f * std::exp(-t / 0.002f) + 0.002f * rng.range(-1.f, 1.f);
        const int b = std::min(kBins - 1, int(float(i) / float(n) * float(kBins)));
        bins[size_t(b)] = std::max(bins[size_t(b)], std::fabs(x));
    }
    return bins;
}

} // namespace

TEST_CASE(capture_contour_basics)
{
    const std::vector<float> rec = synthKick();
    CaptureOptions opt;
    opt.cycleSeconds = kCycle;
    const std::vector<float> c = captureContour(rec.data(), kBins, opt);
    CHECK(int(c.size()) == kBins);
    float lo = 1.f;
    for (float v : c) {
        CHECK(v >= 0.f && v <= 1.f);
        lo = std::min(lo, v);
    }
    CHECK(lo == 0.f);          // normalised: the loudest point is a full duck
    CHECK(c[0] < 0.05f);       // the hit is at the start
    CHECK(c[size_t(kBins) - 1] > 0.95f);
    // The hold removes the waveform ripple: the contour recovers smoothly (no deep notches).
    int notches = 0;
    for (size_t i = 1; i < c.size(); ++i)
        if (c[i] < c[i - 1] - 0.05f)
            ++notches;
    CHECK(notches == 0);
    // Rejections.
    CHECK(captureContour(nullptr, kBins, opt).empty());
    CHECK(captureContour(rec.data(), 0, opt).empty());
    const std::vector<float> silence(size_t(kBins), 1e-6f);
    CHECK(captureContour(silence.data(), kBins, opt).empty());
    std::vector<float> nan = rec;
    nan[10] = std::nanf("");
    nan[11] = std::numeric_limits<float>::infinity();
    CHECK(captureContour(nan.data(), kBins, opt).size() == size_t(kBins));
}

TEST_CASE(capture_kick_becomes_duck)
{
    const std::vector<float> rec = synthKick();
    CaptureOptions opt;
    opt.cycleSeconds = kCycle;
    opt.maxNodes     = 32;
    Envelope     env;
    CaptureStats stats;
    TimelineMap  identity;
    CHECK(envelopeFromCapture(rec.data(), kBins, identity.phaseMap(), opt, env, &stats));
    CHECK(validEnvelope(env, "capture"));
    CHECK(env.size() <= 32 && env.size() == stats.nodes);
    CHECK(stats.peak > 0.9f);
    CHECK(stats.usedTolerance >= opt.tolerance);
    CHECK_MSG(stats.maxError <= stats.usedTolerance + 1e-3f, "fit error %g > tolerance %g", double(stats.maxError),
              double(stats.usedTolerance));
    et::note("capture: %d nodes, tolerance %.4f, max error %.4f", stats.nodes, double(stats.usedTolerance),
             double(stats.maxError));
    // A duck: low at the start, recovering, back to unity by the end.
    CHECK(env.evaluate(0.f) < 0.05f);
    CHECK(env.evaluate(0.1f) > env.evaluate(0.02f));
    CHECK(env.evaluate(0.3f) > env.evaluate(0.1f));
    CHECK(env.evaluate(0.9f) > 0.95f);
    // Close to the ideal duck 1 − A(t): the hold delays the recovery by up to holdMs (15 ms ≈ 0.19 here).
    float worst = 0.f;
    for (int i = 0; i < 200; ++i) {
        const float x = float(i) / 200.f;
        worst = std::max(worst, std::fabs(env.evaluate(x) - (1.f - kickAmp(x * kCycle))));
    }
    CHECK_MSG(worst < 0.25f, "deviation from the ideal duck %g", double(worst));
    for (int i = 0; i < env.size(); ++i)
        CHECK(env.node(i).flags == 0);
    // Fewer nodes on request.
    opt.maxNodes = 6;
    CHECK(envelopeFromCapture(rec.data(), kBins, identity.phaseMap(), opt, env, &stats));
    CHECK(env.size() <= 6);
    // Silence leaves the output untouched.
    const std::vector<float> silence(size_t(kBins), 0.f);
    Envelope keep = Envelope::flat();
    CHECK(!envelopeFromCapture(silence.data(), kBins, identity.phaseMap(), opt, keep));
    CHECK(ops::sameNodes(keep, Envelope::flat()));
}

TEST_CASE(capture_model_operation_and_rotation)
{
    Fixture f;
    EditorModel& m = f.model;
    m.load(makeEnv({{0, 1, 0, 0}, {0.5f, 0, 0, kick::kNodeQuickShift}, {1, 1, 0, 0}}), Envelope(), LoadHistory::Clear);
    m.select(1);
    CaptureOptions opt;
    opt.cycleSeconds = kCycle;
    // Rotated display: the kick recorded at timeline 0 must play (and show) at timeline 0.
    f.setTiming(90.f, 0.f);
    const std::vector<float> rec = synthKick();
    f.listener.clear();
    CHECK(m.applyCapture(rec.data(), kBins, opt));
    CHECK(m.undoCount() == 1 && m.undoLabel() == "Capture");
    CHECK(f.listener.finals() == 1);
    CHECK(m.selectionCount() == 0 && m.quickShiftGroupSize() == 0);
    CHECK(validEnvelope(m.editEnvelope(), "capture rotated"));
    CHECK(m.valueAt(0.005f) < 0.1f);
    CHECK(m.valueAt(0.9f) > 0.95f);
    CHECK(m.valueAt(0.3f) > m.valueAt(0.05f));
    // Swing too: compare against the contour at the same timeline positions.
    f.setTiming(30.f, 70.f);
    CHECK(m.applyCapture(rec.data(), kBins, opt));
    const std::vector<float> c = captureContour(rec.data(), kBins, opt);
    float worst = 0.f;
    for (int i = 2; i < 100; ++i) {
        const float t = float(i) / 100.f;
        worst = std::max(worst, std::fabs(m.valueAt(t) - c[size_t(t * float(kBins))]));
    }
    CHECK_MSG(worst < 0.08f, "swung capture deviates by %g", double(worst));
    // Bad input: no step.
    const int steps = m.undoCount();
    CHECK(!m.applyCapture(nullptr, kBins, opt));
    CHECK(m.undoCount() == steps);
    m.undo();
    m.undo();
    CHECK(m.quickShiftGroupSize() == 1);
}
