// Kickarse — Envelope and PhaseMap tests.
#include <cstring>
#include <limits>

#include "TestHarness.h"
#include "TestSignals.h"
#include "shared/Envelope.h"

using kick::Envelope;
using kick::EnvNode;
using kick::PhaseMap;

namespace {

bool sameNodes(const Envelope& a, const Envelope& b, float tol)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i) {
        const EnvNode& x = a.node(i);
        const EnvNode& y = b.node(i);
        if (std::fabs(x.x - y.x) > tol || std::fabs(x.y - y.y) > tol || std::fabs(x.tension - y.tension) > tol
            || x.flags != y.flags)
            return false;
    }
    return true;
}

Envelope randomEnvelope(kt::Rng& rng)
{
    Envelope e = Envelope::flat();
    const int extra = rng.range(0, 40);
    for (int i = 0; i < extra; ++i) {
        EnvNode n;
        n.x       = rng.uniform();
        n.y       = rng.uniform();
        n.tension = rng.bipolar();
        n.flags   = rng.next() & 1u;
        e.insert(n);
    }
    e.node(0).y       = rng.uniform();
    e.node(0).tension = rng.bipolar();
    e.normalise();
    return e;
}

} // namespace

TEST_CASE(envelope_shape_properties)
{
    for (float t : {-1.f, -0.7f, -0.2f, -1e-5f, 0.f, 1e-5f, 0.3f, 0.8f, 1.f}) {
        CHECK_NEAR(Envelope::shape(0.f, t), 0.0, 0.0);
        CHECK_NEAR(Envelope::shape(1.f, t), 1.0, 0.0);
        float prev = 0.f;
        for (int i = 0; i <= 200; ++i) {
            const float u = float(i) / 200.f;
            const float v = Envelope::shape(u, t);
            CHECK(v >= prev - 1e-7f);      // monotonic
            CHECK(v >= 0.f && v <= 1.f);
            CHECK_NEAR(Envelope::shape(u, -t), 1.f - Envelope::shape(1.f - u, t), 2e-6);
            prev = v;
        }
    }
    for (int i = 0; i <= 100; ++i) {
        const float u = float(i) / 100.f;
        CHECK_NEAR(Envelope::shape(u, 0.f), u, 1e-7);
    }
    // Positive tension holds the start value longer; |t| = 1 is a strong bend.
    CHECK(Envelope::shape(0.5f, 0.5f) < 0.3f);
    CHECK(Envelope::shape(0.5f, 1.f) < 0.06f);
    CHECK(Envelope::shape(0.5f, -1.f) > 0.94f);
    // Garbage in, sane out.
    CHECK(Envelope::shape(std::numeric_limits<float>::quiet_NaN(), 0.5f) == 0.f);
    const float v = Envelope::shape(0.5f, std::numeric_limits<float>::quiet_NaN());
    CHECK_NEAR(v, 0.5, 1e-7);
}

TEST_CASE(envelope_default_and_flat)
{
    const Envelope def;
    CHECK(def.size() >= 2);
    CHECK_NEAR(def.evaluate(0.f), 0.0, 1e-6);          // starts fully ducked
    CHECK_NEAR(def.evaluate(0.6f), 1.0, 1e-6);         // recovered by 60 %
    CHECK(def.evaluate(0.2f) > 0.05f && def.evaluate(0.2f) < 0.7f);
    float prev = -1.f;
    for (int i = 0; i <= 100; ++i) {
        const float v = def.evaluate(0.5f * float(i) / 100.f);
        CHECK(v >= prev);
        prev = v;
    }
    const Envelope flat = Envelope::flat();
    for (int i = 0; i < 50; ++i)
        CHECK_NEAR(flat.evaluate(float(i) / 50.f), 1.0, 0.0);
}

TEST_CASE(envelope_evaluate_steps_and_wrap)
{
    Envelope e = Envelope::flat();
    // 1 → step down to 0 at 0.25, linear up to 1 at 0.75.
    e.insert(EnvNode{0.25f, 1.f, 0.f, 0});
    e.insert(EnvNode{0.25f, 0.f, 0.f, 0});
    e.insert(EnvNode{0.75f, 1.f, 0.f, 0});
    CHECK(e.size() == 5);
    CHECK(e.node(1).y == 1.f && e.node(2).y == 0.f); // insertion order kept at equal x
    CHECK_NEAR(e.evaluate(0.2499f), 1.0, 1e-6);
    CHECK_NEAR(e.evaluate(0.25f), 0.0, 1e-6);          // right-continuous at the step
    CHECK_NEAR(e.evaluate(0.5f), 0.5, 1e-6);
    CHECK_NEAR(e.evaluate(1.5f), 0.5, 1e-6);           // wraps
    CHECK_NEAR(e.evaluate(-0.5f), 0.5, 1e-6);
    CHECK_NEAR(e.evaluate(1.f), e.evaluate(0.f), 0.0);
    CHECK(std::isfinite(e.evaluate(std::numeric_limits<float>::quiet_NaN())));

    // Step at x = 0: value at 0 is the second node.
    Envelope s = Envelope::flat();
    s.insert(EnvNode{0.f, 0.2f, 0.f, 0});
    CHECK_NEAR(s.evaluate(0.f), 0.2, 1e-6);

    // render() == evaluate(i / n)
    kt::Rng rng;
    for (int k = 0; k < 20; ++k) {
        const Envelope r = randomEnvelope(rng);
        std::vector<float> buf(1000);
        r.render(buf.data(), int(buf.size()));
        for (int i = 0; i < 1000; ++i)
            CHECK(buf[size_t(i)] == r.evaluate(float(i) / 1000.f));
    }
}

TEST_CASE(envelope_insert_remove_normalise)
{
    Envelope e = Envelope::flat();
    const int first = e.insert(EnvNode{-3.f, 0.5f, 0.f, 0});
    CHECK(first == 1);                                   // x clamped to 0, inserted after the first node
    const int last = e.insert(EnvNode{7.f, 0.5f, 0.f, 0});
    CHECK(last == e.size() - 2);                         // x clamped to 1, inserted before the last node
    CHECK(e.node(0).x == 0.f && e.node(e.size() - 1).x == 1.f);
    const int before = e.size();
    e.remove(0);
    e.remove(e.size() - 1);
    CHECK(e.size() == before);                          // endpoints are protected
    e.remove(1);
    CHECK(e.size() == before - 1);

    Envelope full = Envelope::flat();
    int added = 0;
    for (int i = 0; i < 300; ++i)
        if (full.insert(EnvNode{float(i % 97) / 97.f, 0.5f, 0.f, 0}) >= 0)
            ++added;
    CHECK(full.size() == Envelope::kMaxNodes);
    CHECK(added == Envelope::kMaxNodes - 2);

    // Direct edits + normalise: sort, clamp, endpoints pinned.
    Envelope d = Envelope::flat();
    d.insert(EnvNode{0.5f, 0.5f, 0.f, 0});
    d.node(1).x       = 0.9f;
    d.node(1).y       = 4.f;
    d.node(1).tension = std::numeric_limits<float>::quiet_NaN();
    d.node(0).x       = 0.3f;
    d.normalise();
    CHECK(d.node(0).x == 0.f && d.node(d.size() - 1).x == 1.f);
    for (int i = 0; i < d.size(); ++i) {
        CHECK(d.node(i).y >= 0.f && d.node(i).y <= 1.f);
        CHECK(d.node(i).tension >= -1.f && d.node(i).tension <= 1.f);
        if (i > 0)
            CHECK(d.node(i).x >= d.node(i - 1).x);
    }
}

TEST_CASE(envelope_serialize_roundtrip)
{
    kt::Rng rng;
    for (int k = 0; k < 200; ++k) {
        const Envelope e = randomEnvelope(rng);
        const std::string s = e.serialize();
        Envelope back = Envelope::flat();
        CHECK(back.deserialize(s));
        CHECK(sameNodes(e, back, 1e-5f));
        CHECK(back.serialize() == s);                  // stable
        for (char c : s)
            CHECK(c >= 32 && c < 127);                 // printable ASCII
    }
    Envelope def;
    Envelope back = Envelope::flat();
    CHECK(back.deserialize("  KE1  0 , 0 , 0.3 , 0 ;\n 0.5,1,0,0; 1,1,0 ; "));
    CHECK(sameNodes(def, back, 1e-6f));
    CHECK(back.deserialize("KE1 0,0,0;1,1,0"));        // flags optional
    CHECK(back.size() == 2);
    CHECK(back.deserialize("KE1 1,1,0,0;0,0.5,0,0"));  // unsorted input is normalised
    CHECK(back.node(0).y == 0.5f);
    CHECK(back.deserialize("KE1 0,2,5,0;1,-1,0,0"));    // clamped
    CHECK(back.node(0).y == 1.f && back.node(0).tension == 1.f && back.node(1).y == 0.f);
}

TEST_CASE(envelope_deserialize_garbage)
{
    kt::Rng        seedRng;
    const Envelope ref = randomEnvelope(seedRng);
    const char* bad[] = {
        "", " ", "KE1", "KE1 ", "KE2 0,0,0,0;1,1,0,0", "KE10,0,0,0;1,1,0,0", "ke1 0,0,0,0;1,1,0,0",
        "KE1 0,0,0,0", "KE1 0,0,0,0;", "KE1 0,0,0,0;;1,1,0,0", "KE1 0,0,0,0;1,1,0,0;x",
        "KE1 0,0,0,0;1,1,0,0 junk", "KE1 nan,0,0,0;1,1,0,0", "KE1 0,inf,0,0;1,1,0,0",
        "KE1 0,0,0,-1;1,1,0,0", "KE1 0,0,0,0;1,1,0,99999999999", "KE1 0;1", "KE1 0,0;1,1",
        "KE1 0,,0,0;1,1,0,0", "KE1 0,0,0,0,0;1,1,0,0", "KE1 0x10,0,0,0;1,1,0,0", "KE1 1e999,0,0,0;1,1,0,0",
        "KE1 ,0,0,0;1,1,0,0", "KE1 0 0 0 0;1 1 0 0", "KE1 0,0,0,0|1,1,0,0",
    };
    for (const char* b : bad) {
        Envelope e = ref;
        CHECK_MSG(!e.deserialize(b), "accepted garbage: '%s'", b);
        CHECK(sameNodes(e, ref, 0.f));
    }
    // Too many nodes.
    std::string many = "KE1 ";
    for (int i = 0; i <= Envelope::kMaxNodes; ++i)
        many += "0.5,0.5,0,0;";
    {
        Envelope e = ref;
        CHECK(!e.deserialize(many));
        CHECK(sameNodes(e, ref, 0.f));
    }
    // Random bytes, and random mutations of a valid string, never crash and never corrupt.
    kt::Rng rng;
    const std::string valid = ref.serialize();
    int accepted = 0;
    for (int k = 0; k < 3000; ++k) {
        std::string s;
        if (k % 2 == 0) {
            const int len = rng.range(0, 64);
            for (int i = 0; i < len; ++i)
                s += char(rng.next() & 0xff);
            if (k % 4 == 0)
                s = "KE1 " + s;
        } else {
            s = valid;
            const int edits = rng.range(1, 4);
            for (int i = 0; i < edits && !s.empty(); ++i)
                s[size_t(rng.range(0, int(s.size()) - 1))] = char(rng.range(32, 126));
        }
        Envelope e = ref;
        if (e.deserialize(s)) {
            ++accepted;
            CHECK(e.size() >= 2 && e.size() <= Envelope::kMaxNodes);
            CHECK(e.node(0).x == 0.f && e.node(e.size() - 1).x == 1.f);
            for (int i = 0; i < e.size(); ++i)
                CHECK(e.node(i).y >= 0.f && e.node(i).y <= 1.f);
        } else {
            CHECK(sameNodes(e, ref, 0.f));
        }
    }
    kt::note("random inputs: %d of 3000 accepted (valid after mutation)", accepted);
}

TEST_CASE(envelope_from_samples)
{
    constexpr int N = 1024;
    std::vector<float> ys(N);
    auto maxErr = [&](const Envelope& e) {
        float err = 0.f;
        for (int i = 0; i < N; ++i)
            err = std::max(err, std::fabs(e.evaluate(float(i) / N) - ys[size_t(i)]));
        return err;
    };

    // The default duck fits back into very few nodes.
    const Envelope def;
    def.render(ys.data(), N);
    const Envelope fitDef = Envelope::fromSamples(ys.data(), N, 0.01f);
    CHECK_MSG(maxErr(fitDef) <= 0.01f, "default duck error %.4f", maxErr(fitDef));
    CHECK_MSG(fitDef.size() <= 5, "default duck used %d nodes", fitDef.size());
    kt::note("default duck refit: %d nodes, max error %.5f", fitDef.size(), maxErr(fitDef));

    // A recorded-kick-like contour: instant duck, exponential recovery.
    for (int i = 0; i < N; ++i)
        ys[size_t(i)] = 1.f - std::exp(-float(i) / (0.08f * N));
    for (float tol : {0.05f, 0.01f, 0.003f}) {
        const Envelope f = Envelope::fromSamples(ys.data(), N, tol);
        CHECK_MSG(maxErr(f) <= tol * 1.0001f, "tol %.3f error %.4f", tol, maxErr(f));
        CHECK_MSG(f.size() <= 12, "tol %.3f used %d nodes", tol, f.size());
        kt::note("exponential recovery, tolerance %.3f: %d nodes", tol, f.size());
    }

    // Random piecewise envelopes (incl. steps) are reproduced within tolerance.
    kt::Rng rng;
    for (int k = 0; k < 20; ++k) {
        const Envelope src = randomEnvelope(rng);
        src.render(ys.data(), N);
        const Envelope f = Envelope::fromSamples(ys.data(), N, 0.01f);
        CHECK(f.size() <= Envelope::kMaxNodes);
        CHECK_MSG(maxErr(f) <= 0.0101f || f.size() == Envelope::kMaxNodes, "random %d error %.4f", k, maxErr(f));
    }

    // White noise must respect kMaxNodes.
    for (int i = 0; i < N; ++i)
        ys[size_t(i)] = rng.uniform();
    const Envelope noisy = Envelope::fromSamples(ys.data(), N, 0.001f);
    CHECK(noisy.size() >= 2 && noisy.size() <= Envelope::kMaxNodes);

    // Degenerate inputs.
    CHECK(Envelope::fromSamples(nullptr, 10).size() == 2);
    const float one = 0.3f;
    const Envelope single = Envelope::fromSamples(&one, 1);
    CHECK_NEAR(single.evaluate(0.7f), 0.3, 1e-6);
    float withNan[4] = {0.f, std::numeric_limits<float>::quiet_NaN(), 2.f, -1.f};
    const Envelope sane = Envelope::fromSamples(withNan, 4);
    for (int i = 0; i < sane.size(); ++i)
        CHECK(sane.node(i).y >= 0.f && sane.node(i).y <= 1.f);
}

TEST_CASE(phasemap_inverse_and_swing)
{
    kt::Rng rng;
    for (int k = 0; k < 20000; ++k) {
        PhaseMap m;
        m.rotate01  = rng.uniform() * (k % 3 == 0 ? 3.f : 1.f) - (k % 5 == 0 ? 1.f : 0.f);
        m.divisions = 1.f + rng.uniform() * 63.f;
        if (k % 2 == 0)
            m.divisions = float(rng.range(1, 64));
        m.swing = rng.uniform();
        const float p = rng.uniform();
        const float q = m.toNode(p);
        CHECK(q >= 0.f && q < 1.f);
        const float back = m.toTimeline(q);
        const float d    = std::fabs(back - p);
        CHECK_MSG(std::min(d, 1.f - d) < 2e-6f, "p %.7f -> %.7f -> %.7f (rot %.3f div %.3f sw %.3f)", p, q, back,
                  m.rotate01, m.divisions, m.swing);
        const float q2 = rng.uniform();
        const float p2 = m.toTimeline(q2);
        const float b2 = m.toNode(p2);
        const float d2 = std::fabs(b2 - q2);
        CHECK(std::min(d2, 1.f - d2) < 2e-6f);
    }
    PhaseMap m;
    m.divisions = 4.f; // pairs of 1/4 cycle each → pair width 0.5
    m.swing     = 1.f;
    CHECK_NEAR(m.toTimeline(0.25f), 0.375, 1e-6);  // 2nd cell starts at 75 % of the pair
    CHECK_NEAR(m.toTimeline(0.75f), 0.875, 1e-6);
    CHECK_NEAR(m.toTimeline(0.5f), 0.5, 1e-6);     // pair boundaries are fixed points
    CHECK_NEAR(m.toNode(0.375f), 0.25, 1e-6);
    m.swing = 0.f;
    CHECK_NEAR(m.toTimeline(0.3f), 0.3, 1e-6);
    m.rotate01 = 0.25f;
    CHECK_NEAR(m.toTimeline(0.f), 0.25, 1e-6);
    CHECK_NEAR(m.toNode(0.25f), 0.0, 1e-6);
    // Monotonic warp.
    m.rotate01 = 0.f;
    m.swing    = 0.6f;
    m.divisions = 7.f; // odd: last cell stays straight
    float prev = -1.f;
    for (int i = 0; i < 1000; ++i) {
        const float v = m.toTimeline(float(i) / 1000.f);
        CHECK(v > prev);
        prev = v;
    }
    // NaN-safe.
    m.swing = std::numeric_limits<float>::quiet_NaN();
    CHECK(std::isfinite(m.toNode(0.3f)) && std::isfinite(m.toTimeline(std::numeric_limits<float>::infinity())));
}

TEST_CASE(envelope_evaluate_left)
{
    // 1 → step to 0 at 0.25, linear up to 1 at 0.75, then a step to 0.2 → 0.8 at x = 1.
    Envelope e = Envelope::flat();
    e.insert(EnvNode{0.25f, 1.f, 0.f, 0});
    e.insert(EnvNode{0.25f, 0.f, 0.f, 0});
    e.insert(EnvNode{0.75f, 1.f, 0.f, 0});
    e.node(e.size() - 1).y = 0.8f;
    e.insert(EnvNode{1.f, 0.2f, 0.f, 0});   // lands before the last node: step at x = 1
    CHECK(e.size() == 6);

    CHECK_NEAR(e.evaluateLeft(0.25f), 1.0, 1e-6);   // value before the step
    CHECK_NEAR(e.evaluate(0.25f), 0.0, 1e-6);       // evaluate() is right-continuous
    CHECK_NEAR(e.evaluateLeft(0.2500001f), 0.0, 1e-5);
    CHECK_NEAR(e.evaluateLeft(0.5f), 0.5, 1e-6);
    CHECK_NEAR(e.evaluateLeft(0.f), 1.0, 0.0);
    CHECK_NEAR(e.evaluateLeft(1.f), 0.8, 1e-7);      // last node's value
    CHECK_NEAR(e.evaluateLeft(0.9999f), 0.2, 1e-3); // approaching the step at 1 from the left
    CHECK_NEAR(e.evaluateLeft(1.5f), 0.8, 1e-7);     // clamped, not wrapped
    CHECK_NEAR(e.evaluateLeft(-0.3f), 1.0, 0.0);
    CHECK_NEAR(e.evaluateLeft(std::numeric_limits<float>::quiet_NaN()), 1.0, 0.0);

    // Away from steps it is identical to evaluate().
    kt::Rng rng;
    for (int k = 0; k < 20; ++k) {
        const Envelope r = randomEnvelope(rng);
        for (int i = 1; i < 997; ++i) {
            const float p = float(i) / 997.f;
            bool atStep = false;
            for (int n = 0; n + 1 < r.size(); ++n)
                atStep = atStep || (r.node(n).x == p && r.node(n + 1).x == p);
            if (!atStep)
                CHECK(r.evaluateLeft(p) == r.evaluate(p));
        }
    }
    const Envelope def;
    CHECK_NEAR(def.evaluateLeft(1.f), 1.0, 0.0);
}
