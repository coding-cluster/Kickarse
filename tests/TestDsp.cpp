// Kickarse — unit tests of the DSP building blocks (crossover, detector, table hand-over).
#include <atomic>
#include <complex>
#include <thread>

#include "TestHarness.h"
#include "TestSignals.h"
#include "dsp/Crossover.h"
#include "dsp/EnvelopeTable.h"
#include "dsp/Trigger.h"

using namespace kick;

namespace {

// Magnitude response (dB) of an impulse response at frequency hz (direct DFT).
double responseDb(const std::vector<double>& h, double hz, double sr)
{
    std::complex<double> acc(0.0, 0.0);
    const double w = 2.0 * kt::kPi * hz / sr;
    for (size_t i = 0; i < h.size(); ++i)
        acc += h[i] * std::polar(1.0, -w * double(i));
    return 20.0 * std::log10(std::abs(acc));
}

} // namespace

TEST_CASE(crossover_lr2_lr4_sum_flat)
{
    const double sr = 48000.0;
    const size_t len = 1u << 17;
    double worst = 0.0;
    for (bool slope24 : {false, true}) {
        for (float fc : {40.f, 150.f, 1000.f, 5000.f, 15000.f}) {
            dsp::Crossover x;
            x.prepare(sr);
            x.setSlope24(slope24);
            x.setFrequency(fc);
            std::vector<double> sum(len), low(len), high(len);
            for (size_t i = 0; i < len; ++i) {
                float lo, hi;
                x.process(0, i == 0 ? 1.f : 0.f, lo, hi);
                sum[i]  = double(lo) + double(hi);
                low[i]  = lo;
                high[i] = hi;
            }
            for (int k = 0; k <= 60; ++k) {
                const double hz = 20.0 * std::pow(1000.0, double(k) / 60.0);
                const double db = responseDb(sum, hz, sr);
                worst = std::max(worst, std::fabs(db));
                CHECK_MSG(std::fabs(db) < 0.05, "%s fc %.0f: sum %.4f dB at %.0f Hz", slope24 ? "LR4" : "LR2", fc, db, hz);
            }
            // Each band is -6.02 dB at the crossover frequency (Linkwitz–Riley).
            CHECK_NEAR(responseDb(low, fc, sr), -6.02, 0.05);
            CHECK_NEAR(responseDb(high, fc, sr), -6.02, 0.05);
            // Slopes: an octave away the stop band is about -12 / -24 dB (per octave, asymptotically).
            if (fc >= 150.f && fc <= 1000.f) { // 8 fc stays below Nyquist
                const double stop = responseDb(low, fc * 8.0, sr);
                CHECK(stop < (slope24 ? -65.0 : -33.0));
            }
        }
    }
    kt::note("worst |low + high| deviation 20 Hz - 20 kHz: %.5f dB", worst);
}

TEST_CASE(crossover_modulation_stable)
{
    dsp::Crossover x;
    x.prepare(44100.0);
    kt::Rng rng;
    float peak = 0.f;
    for (int i = 0; i < 44100 * 2; ++i) {
        if (i % 16 == 0)
            x.setFrequency(20.f * std::pow(1000.f, 0.5f + 0.5f * std::sin(float(i) * 0.0005f)));
        float lo, hi;
        x.process(0, rng.bipolar(), lo, hi);
        peak = std::max(peak, std::fabs(lo + hi));
        CHECK(std::isfinite(lo) && std::isfinite(hi));
    }
    CHECK(peak < 4.f);
}

TEST_CASE(trigger_detector_hysteresis_holdoff)
{
    const double sr = 48000.0;
    dsp::TriggerDetector d;
    d.prepare(sr);
    d.setThresholdDb(-20.f);
    d.setHoldMs(50.f);

    // One sustained 40 Hz burst = one trigger (hysteresis holds across half-cycles).
    int count = 0;
    for (int i = 0; i < int(sr); ++i) {
        const float x = i < int(0.5 * sr) ? 0.5f * std::sin(2.f * 3.14159265f * 40.f * float(i) / float(sr)) : 0.f;
        count += d.tick(x) ? 1 : 0;
    }
    CHECK(count == 1);

    // A second hit 45 ms after the first (re-armed by the hysteresis, but inside the 50 ms hold-off)
    // is swallowed, and not fired late when the hold-off expires while it is still sounding.
    d.reset();
    count = 0;
    int firstAt = -1;
    for (int i = 0; i < int(0.4 * sr); ++i) {
        float x = 0.f;
        if (i < 480)
            x = 0.12f; // -18.4 dB: decays below -23 dB about 32 ms after it stops
        else if (i >= 2160 && i < 4800)
            x = 0.12f;
        if (d.tick(x)) {
            ++count;
            if (firstAt < 0)
                firstAt = i;
        }
    }
    CHECK(count == 1);
    CHECK(firstAt == 0);

    // Below threshold: nothing.
    d.reset();
    count = 0;
    for (int i = 0; i < 48000; ++i)
        count += d.tick(0.05f * std::sin(float(i) * 0.01f)) ? 1 : 0;
    CHECK(count == 0);
}

TEST_CASE(envelope_table_handover_concurrent)
{
    dsp::EnvelopeTableBuffer buf;
    // The default table matches the default envelope, including the end value.
    const Envelope def;
    const auto& t0 = buf.acquire();
    CHECK_NEAR(dsp::EnvelopeTableBuffer::lookup(t0, 0.f), def.evaluate(0.f), 1e-6);
    CHECK_NEAR(dsp::EnvelopeTableBuffer::lookup(t0, 0.3f), def.evaluate(0.3f), 1e-3);
    CHECK_NEAR(dsp::EnvelopeTableBuffer::endValue(t0), 1.0, 0.0);

    // Steps stay exactly vertical, on a cell boundary (0.5) and inside a cell (0.30001).
    Envelope step = Envelope::flat();
    step.insert(EnvNode{0.5f, 1.f, 0.f, 0});
    step.insert(EnvNode{0.5f, 0.f, 0.f, 0});   // 1 → 0 at 0.5, then linear back to 1
    step.insert(EnvNode{0.30001f, 1.f, 0.f, 0});
    step.insert(EnvNode{0.30001f, 0.25f, 0.f, 0});
    step.insert(EnvNode{0.30001f, 1.f, 0.f, 0}); // a spike: 1 → 0.25 → 1 collapses to no step
    buf.publish(step);
    const auto& t1 = buf.acquire();
    int wrong = 0;
    for (int i = 0; i < 20000; ++i) {
        const float q = float(i) / 20000.f;
        wrong += std::fabs(dsp::EnvelopeTableBuffer::lookup(t1, q) - step.evaluate(q)) > 1e-4f ? 1 : 0;
    }
    CHECK(wrong == 0);
    CHECK_NEAR(dsp::EnvelopeTableBuffer::lookup(t1, 0.49999997f), 1.0, 1e-6);
    CHECK_NEAR(dsp::EnvelopeTableBuffer::lookup(t1, 0.5f), 0.0, 1e-6);
    CHECK_NEAR(dsp::EnvelopeTableBuffer::endValue(t1), 1.0, 0.0);

    Envelope inCell = Envelope::flat();
    inCell.insert(EnvNode{0.30001f, 1.f, 0.f, 0});
    inCell.insert(EnvNode{0.30001f, 0.f, 0.f, 0});
    buf.publish(inCell);
    const auto& t2 = buf.acquire();
    CHECK_NEAR(dsp::EnvelopeTableBuffer::lookup(t2, 0.30000f), 1.0, 1e-6);
    CHECK_NEAR(dsp::EnvelopeTableBuffer::lookup(t2, 0.30002f), inCell.evaluate(0.30002f), 1e-4);
    CHECK(dsp::EnvelopeTableBuffer::lookup(t2, 0.30002f) < 0.01f);

    // A writer thread hammers publish() while the "audio thread" reads: every table seen must be
    // one of the published constant envelopes, never a torn mix.
    Envelope constant = Envelope::flat();
    buf.publish(constant);
    std::atomic<bool> stop {false};
    std::thread writer([&] {
        for (int k = 0; k < 4000; ++k) {
            Envelope e = Envelope::flat();
            e.node(0).y = e.node(1).y = float(k % 10) / 10.f;
            buf.publish(e);
        }
        stop.store(true);
    });
    int torn = 0, reads = 0;
    while (!stop.load() || reads < 1000) {
        const auto& t = buf.acquire();
        const float v0 = t.v[0];
        for (int i = 0; i <= dsp::EnvelopeTableBuffer::kSize; i += 97)
            torn += t.v[i] != v0 ? 1 : 0;
        ++reads;
    }
    writer.join();
    CHECK(torn == 0);
    kt::note("%d reads during 4000 concurrent publishes, torn tables: %d", reads, torn);
}
