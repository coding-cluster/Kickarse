// Kickarse — editor tests: pure node-list operations (EnvelopeOps).
#include "EditorTest.h"

using namespace et;

namespace {

// A random valid envelope: 2 … maxNodes nodes, some vertical steps, random tensions.
Envelope randomEnvelope(Rng& rng, int maxNodes = 16)
{
    const int n = 2 + rng.below(maxNodes - 1);
    std::vector<float> xs;
    for (int i = 0; i < n - 2; ++i)
        xs.push_back(rng.chance(0.15f) && !xs.empty() ? xs.back() : rng.uniform());
    std::sort(xs.begin(), xs.end());
    ops::NodeList v;
    v.push_back(EnvNode{0.f, rng.uniform(), rng.range(-1.f, 1.f), 0});
    for (float x : xs)
        v.push_back(EnvNode{x, rng.uniform(), rng.chance(0.3f) ? 0.f : rng.range(-1.f, 1.f),
                            rng.chance(0.2f) ? uint32_t(kick::kNodeQuickShift) : 0u});
    v.push_back(EnvNode{1.f, rng.uniform(), 0.f, 0});
    return makeEnv(v);
}

// Max |a(x) − b(x)| over x strictly between nodes (avoids the step points themselves).
float offNodeDistance(const Envelope& a, const Envelope& b, int samples = 997)
{
    float d = 0.f;
    for (int i = 0; i < samples; ++i) {
        const float x = (float(i) + 0.37f) / float(samples);
        d = std::max(d, std::fabs(a.evaluate(x) - b.evaluate(x)));
    }
    return d;
}

Envelope applyOp(const Envelope& e, bool (*fn)(ops::NodeList&))
{
    ops::NodeList v = ops::toList(e);
    Envelope out = e;
    if (fn(v))
        ops::fromList(v, out);
    return out;
}

bool reverseOp(ops::NodeList& v)
{
    ops::reverse(v);
    return true;
}

bool invertOp(ops::NodeList& v)
{
    ops::invertY(v);
    return true;
}

} // namespace

TEST_CASE(ops_list_roundtrip_and_validity)
{
    Rng rng(1);
    for (int i = 0; i < 300; ++i) {
        const Envelope e = randomEnvelope(rng, 40);
        CHECK(validEnvelope(e, "random"));
        Envelope back;
        CHECK(ops::fromList(ops::toList(e), back));
        CHECK(ops::sameNodes(e, back));
    }
    Envelope out = Envelope::flat();
    CHECK(!ops::fromList({}, out));
    CHECK(!ops::fromList({EnvNode{}}, out));
    ops::NodeList tooMany(size_t(Envelope::kMaxNodes) + 1, EnvNode{0.5f, 0.5f, 0.f, 0});
    CHECK(!ops::fromList(tooMany, out));
    CHECK(ops::sameNodes(out, Envelope::flat()));   // untouched on failure
    // Garbage values are sanitised into a valid envelope.
    const ops::NodeList junk = {EnvNode{std::nanf(""), 7.f, 9.f, 0}, EnvNode{0.3f, -2.f, std::nanf(""), 0},
                                EnvNode{-4.f, std::numeric_limits<float>::infinity(), -9.f, 0}};
    CHECK(ops::fromList(junk, out));
    CHECK(validEnvelope(out, "junk"));
    std::string why;
    CHECK(ops::isValid(Envelope(), &why));
}

TEST_CASE(ops_cut_preserves_curve)
{
    Rng rng(2);
    for (int i = 0; i < 300; ++i) {
        const Envelope e = randomEnvelope(rng);
        ops::NodeList  v = ops::toList(e);
        for (int k = 0; k < 4; ++k) {
            const float x   = rng.uniform();
            const int   idx = ops::cut(v, x, rng.chance(0.5f));
            CHECK(idx >= 0);
            if (idx >= 0)
                CHECK(std::fabs(v[size_t(idx)].x - x) <= ops::kSameX);
        }
        if (int(v.size()) > Envelope::kMaxNodes)
            continue;
        Envelope c;
        CHECK(ops::fromList(v, c));
        CHECK_MSG(ops::curveDistance(e, c) < 2e-5f, "cut changed the curve by %g", double(ops::curveDistance(e, c)));
    }
    // An existing step: first vs last node at that x.
    ops::NodeList step = ops::toList(makeEnv({{0, 1, 0, 0}, {0.5f, 0.2f, 0, 0}, {0.5f, 0.8f, 0, 0}, {1, 1, 0, 0}}));
    CHECK(ops::cut(step, 0.5f, false) == 1);
    CHECK(ops::cut(step, 0.5f, true) == 2);
    CHECK(ops::cut(step, std::nanf(""), false) == -1);
}

TEST_CASE(ops_replace_span_connects)
{
    const Envelope base = makeEnv({{0, 0, 0.3f, 0}, {0.5f, 1, 0, 0}, {1, 1, 0, 0}});
    ops::NodeList  v    = ops::toList(base);
    CHECK(ops::replaceSpan(v, 0.6f, 0.8f, {{0.6f, 0.2f, 0, 0}, {0.8f, 0.4f, 0, 0}}));
    Envelope e;
    CHECK(ops::fromList(v, e));
    CHECK(validEnvelope(e, "replace"));
    // Inside: the line.
    CHECK_NEAR(e.evaluate(0.7f), 0.3f, 1e-5);
    // Neighbouring segments stretch to meet its ends (continuous, no step).
    CHECK_NEAR(e.evaluateLeft(0.6f), 0.2f, 1e-6);
    CHECK_NEAR(e.evaluate(0.55f), 0.6f, 1e-5);
    CHECK_NEAR(e.evaluate(0.9f), 0.7f, 1e-5);
    // Everything before the previous node is untouched.
    for (int i = 0; i < 50; ++i) {
        const float x = 0.5f * float(i) / 50.f;
        CHECK_NEAR(e.evaluate(x), base.evaluate(x), 1e-6);
    }
    // Nodes strictly inside the span are replaced.
    ops::NodeList w = ops::toList(makeEnv({{0, 1, 0, 0}, {0.3f, 0.2f, 0.5f, 0}, {0.4f, 0.9f, 0, 0}, {1, 1, 0, 0}}));
    CHECK(ops::replaceSpan(w, 0.25f, 0.45f, {{0.25f, 0.5f, 0, 0}, {0.45f, 0.5f, 0, 0}}));
    for (const EnvNode& n : w)
        CHECK(!(n.x > 0.25f && n.x < 0.45f));
    // A span touching the endpoints only sets their y.
    ops::NodeList full = ops::toList(Envelope());
    CHECK(ops::replaceSpan(full, 0.f, 1.f, {{0, 0.1f, 0, 0}, {1, 0.9f, 0, 0}}));
    CHECK(full.size() == 2);
    CHECK_NEAR(full.front().y, 0.1f, 1e-7);
    CHECK_NEAR(full.back().y, 0.9f, 1e-7);
    // Zero width: a vertical step, arriving at the first value, leaving at the second.
    ops::NodeList s = ops::toList(Envelope::flat());
    CHECK(ops::replaceSpan(s, 0.3f, 0.3f, {{0.3f, 0.2f, 0, 0}, {0.3f, 0.9f, 0, 0}}));
    CHECK(ops::fromList(s, e));
    CHECK_NEAR(e.evaluateLeft(0.3f), 0.2f, 1e-6);
    CHECK_NEAR(e.evaluate(0.3f), 0.9f, 1e-6);
    CHECK_NEAR(e.evaluate(0.15f), 0.6f, 1e-5);
    // Bad input.
    CHECK(!ops::replaceSpan(s, 0.6f, 0.4f, {{0.4f, 0, 0, 0}, {0.6f, 0, 0, 0}}));
    CHECK(!ops::replaceSpan(s, 0.2f, 0.4f, {{0.2f, 0, 0, 0}}));
}

TEST_CASE(ops_transform_roundtrips)
{
    Rng rng(3);
    for (int i = 0; i < 400; ++i) {
        const Envelope e = randomEnvelope(rng, 30);
        // invert ∘ invert = id, reverse ∘ reverse = id (node-exact up to float rounding).
        CHECK(ops::sameNodes(applyOp(applyOp(e, invertOp), invertOp), e, 1e-6f));
        const Envelope r = applyOp(e, reverseOp);
        CHECK(validEnvelope(r, "reverse"));
        CHECK(ops::sameNodes(applyOp(r, reverseOp), e, 1e-6f));
        // The reversed curve is the mirror image in time (tensions mirrored exactly).
        float worst = 0.f;
        for (int k = 0; k < 400; ++k) {
            const float x = (float(k) + 0.41f) / 400.f;
            worst = std::max(worst, std::fabs(r.evaluate(1.f - x) - e.evaluate(x)));
        }
        CHECK_MSG(worst < 1e-3f, "reverse mismatch %g", double(worst));

        // duplicate: two copies; halve undoes it exactly.
        ops::NodeList v = ops::toList(e);
        if (ops::duplicate(v)) {
            Envelope d;
            CHECK(ops::fromList(v, d));
            CHECK(validEnvelope(d, "duplicate"));
            for (int k = 0; k < 100; ++k) {
                const float x = (float(k) + 0.3f) / 100.f;
                CHECK_NEAR(d.evaluate(0.5f * x), e.evaluate(x), 1e-3);
                CHECK_NEAR(d.evaluate(0.5f + 0.5f * x), e.evaluate(x), 1e-3);
            }
            CHECK(ops::halve(v));
            Envelope h;
            CHECK(ops::fromList(v, h));
            CHECK(ops::sameNodes(h, e, 1e-6f));
        }
        // halve: the first half stretched.
        ops::NodeList hv = ops::toList(e);
        CHECK(ops::halve(hv));
        Envelope hh;
        CHECK(ops::fromList(hv, hh));
        for (int k = 0; k < 100; ++k) {
            const float x = (float(k) + 0.3f) / 100.f;
            CHECK_NEAR(hh.evaluate(x), e.evaluate(0.5f * x), 1e-3);
        }
        // rotate: new(x) = old(x − a); rotating back restores the curve.
        const float   a  = rng.uniform();
        ops::NodeList rv = ops::toList(e);
        if (ops::rotate(rv, a) && int(rv.size()) <= Envelope::kMaxNodes) {
            Envelope ro;
            CHECK(ops::fromList(rv, ro));
            CHECK(validEnvelope(ro, "rotate"));
            float wr = 0.f;
            for (int k = 0; k < 300; ++k) {
                const float x = (float(k) + 0.29f) / 300.f;
                float       y = x + a;
                y -= std::floor(y);
                wr = std::max(wr, std::fabs(ro.evaluate(y) - e.evaluate(x)));
            }
            CHECK_MSG(wr < 1e-3f, "rotate mismatch %g (a = %g)", double(wr), double(a));
            if (ops::rotate(rv, 1.f - a) && int(rv.size()) <= Envelope::kMaxNodes) {
                Envelope back;
                CHECK(ops::fromList(rv, back));
                CHECK(offNodeDistance(back, e) < 1e-3f);
            }
        }
        // mirror: symmetric around the middle, left half untouched.
        ops::NodeList mv = ops::toList(e);
        if (ops::mirrorLeftToRight(mv)) {
            Envelope mi;
            CHECK(ops::fromList(mv, mi));
            CHECK(validEnvelope(mi, "mirror"));
            for (int k = 0; k < 100; ++k) {
                const float x = 0.5f * (float(k) + 0.37f) / 100.f;
                CHECK_NEAR(mi.evaluate(x), e.evaluate(x), 1e-3);
                CHECK_NEAR(mi.evaluate(1.f - x), e.evaluate(x), 1e-3);
            }
            for (int k = 0; k < mi.size(); ++k)
                if (mi.node(k).x > 0.5f)
                    CHECK(!(mi.node(k).flags & kick::kNodeQuickShift));
        }
    }
    // Edge cases.
    ops::NodeList flat = ops::toList(Envelope::flat());
    CHECK(ops::duplicate(flat));
    CHECK(flat.size() == 3);   // equal ends merge
    ops::NodeList full(size_t(70), EnvNode{0.5f, 0.5f, 0.f, 0});
    full.front().x = 0.f;
    full.back().x  = 1.f;
    CHECK(!ops::duplicate(full));   // 140 nodes would not fit
    ops::NodeList r0 = ops::toList(Envelope());
    CHECK(ops::rotate(r0, 1.f));    // whole cycle: nothing to do
    CHECK(r0.size() == 3);
    CHECK(!ops::rotate(r0, std::nanf("")));
}

TEST_CASE(ops_scale_invert_and_flags)
{
    ops::NodeList v = ops::toList(makeEnv({{0, 0, 0, 0}, {0.5f, 0.4f, 0, ops::kTagSelected}, {1, 1, 0, 0}}));
    ops::scaleY(v, 0.5f);
    CHECK_NEAR(v[0].y, 0.5f, 1e-7);
    CHECK_NEAR(v[1].y, 0.7f, 1e-7);
    CHECK_NEAR(v[2].y, 1.f, 1e-7);
    ops::invertY(v, ops::kTagSelected);
    CHECK_NEAR(v[1].y, 0.3f, 1e-6);
    CHECK_NEAR(v[0].y, 0.5f, 1e-7);
    ops::scaleY(v, -3.f);   // negative factor clamps to 0: everything to unity
    for (const EnvNode& n : v)
        CHECK(n.y == 1.f);
    ops::scaleY(v, std::nanf(""));
    CHECK(v[0].y == 1.f);
}

TEST_CASE(ops_transform_span_keeps_outside)
{
    const Envelope e = makeEnv({{0, 0, 0.2f, 0}, {0.2f, 0.5f, -0.4f, 0}, {0.4f, 1, 0.1f, 0}, {0.6f, 0.2f, 0.7f, 0},
                                {0.8f, 0.6f, 0.3f, 0}, {1, 1, 0, 0}});
    ops::NodeList v = ops::toList(e);
    CHECK(ops::transformSpan(v, 1, 3, [](ops::NodeList& c) {
        ops::reverse(c);
        return true;
    }));
    Envelope r;
    CHECK(ops::fromList(v, r));
    // Outside the span: identical nodes (including the outgoing tension of the span's last node).
    CHECK(r.size() == e.size());
    CHECK(r.node(0).x == 0.f && r.node(0).tension == 0.2f);
    CHECK(r.node(4).x == 0.8f && r.node(4).y == 0.6f);
    CHECK(r.node(3).tension == e.node(3).tension);
    // Inside: mirrored within [0.2, 0.6].
    CHECK_NEAR(r.node(1).y, 0.2f, 1e-7);
    CHECK_NEAR(r.node(3).y, 0.5f, 1e-7);
    for (int k = 0; k < 40; ++k) {
        const float x = 0.2f + 0.4f * (float(k) + 0.5f) / 40.f;
        CHECK_NEAR(r.evaluate(0.8f - x), e.evaluate(x), 2e-5);
    }
    CHECK(!ops::transformSpan(v, 2, 2, [](ops::NodeList&) { return true; }));
    CHECK(!ops::transformSpan(v, -1, 2, [](ops::NodeList&) { return true; }));
    CHECK(!ops::transformSpan(v, 0, 5, [](ops::NodeList&) { return false; }));
}

TEST_CASE(ops_simplify_and_decimate)
{
    // A dense fit of a smooth recovery, then simplified.
    std::vector<float> ys(512);
    for (size_t i = 0; i < ys.size(); ++i) {
        const float x = float(i) / 512.f;
        ys[i] = 1.f - std::exp(-x * 9.f) + 0.03f * std::sin(x * 60.f);
        ys[i] = std::clamp(ys[i], 0.f, 1.f);
    }
    const Envelope dense = Envelope::fromSamples(ys.data(), 512, 0.002f);
    CHECK(dense.size() > 12);
    ops::NodeList v = ops::toList(dense);
    CHECK(ops::simplify(v, 8, 0.f));
    CHECK(v.size() <= 8);
    Envelope s;
    CHECK(ops::fromList(v, s));
    CHECK(validEnvelope(s, "simplify"));
    CHECK_MSG(ops::curveDistance(s, dense) < 0.12f, "simplified error %g", double(ops::curveDistance(s, dense)));
    // Tolerance-driven: nothing is removed when every removal costs more than the tolerance.
    ops::NodeList keep = ops::toList(Envelope());
    CHECK(ops::simplify(keep, 128, 1e-6f));
    CHECK(keep.size() == 3);
    // A redundant node on a straight line is removed at zero tolerance.
    ops::NodeList line = ops::toList(makeEnv({{0, 0, 0, 0}, {0.5f, 0.5f, 0, 0}, {1, 1, 0, 0}}));
    CHECK(ops::simplify(line, 128, 1e-6f));
    CHECK(line.size() == 2);
    // Steps survive simplification that isn't forced.
    ops::NodeList step = ops::toList(makeEnv({{0, 1, 0, 0}, {0.5f, 1, 0, 0}, {0.5f, 0, 0, 0}, {1, 0, 0, 0}}));
    CHECK(ops::simplify(step, 128, 0.01f));
    Envelope st;
    CHECK(ops::fromList(step, st));
    CHECK_NEAR(st.evaluate(0.5f), 0.f, 1e-6);
    CHECK_NEAR(st.evaluateLeft(0.5f), 1.f, 1e-6);
    Envelope big = dense;
    ops::decimate(big, 5);
    CHECK(big.size() == 5);
    CHECK(validEnvelope(big, "decimate"));
}
