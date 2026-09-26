// Kickarse — editor tests: TimelineMap (node x ↔ timeline, seam, grid, snapping).
#include "EditorTest.h"

using namespace et;

namespace {

TimelineMap mapFor(float rotateDeg, float swingPct, int grid = kick::kDefaultGridIndex, int rate = kick::kDefaultRateIndex,
                   int timeMode = kick::kTimeSync)
{
    TimingParams t;
    t.rotateDeg    = rotateDeg;
    t.swingPercent = swingPct;
    t.gridIndex    = grid;
    t.rateIndex    = rate;
    t.timeMode     = timeMode;
    return TimelineMap(makePhaseMap(t));
}

float wrapDiff(float a, float b)
{
    float d = std::fabs(a - b);
    return std::min(d, 1.f - d);
}

} // namespace

TEST_CASE(timeline_identity_and_divisions)
{
    const TimelineMap m = mapFor(0.f, 0.f);
    CHECK_NEAR(m.divisions(), 4.f, 1e-6);   // rate 1/4 over grid 1/16
    CHECK(!m.swingActive());
    CHECK(m.rotation() == 0.f);
    for (int i = 0; i <= 100; ++i) {
        const float x = float(i) / 100.f;
        CHECK(m.toSeam(x) == x);
        CHECK(m.fromSeam(x) == x);
    }
    CHECK(m.toTimeline(0.f) == 0.f);
    CHECK(m.toTimeline(1.f) == 1.f);   // last node drawn at the right edge (DESIGN.md §7.1)
    CHECK(m.toTimeline(0.5f) == 0.5f);
    CHECK(m.gridCount() == 4);
    CHECK_NEAR(m.gridNode(3), 0.75f, 1e-7);

    // ms mode: the grid divides one cycle (cycleBeats = 1).
    const TimelineMap ms = mapFor(0.f, 0.f, 6 /*1/32*/, 0, kick::kTimeFree);
    CHECK_NEAR(ms.divisions(), 8.f, 1e-6);
    // Triplet rate over a straight grid: fractional divisions, last cell partial.
    const TimelineMap tri = mapFor(0.f, 0.f, 4 /*1/16*/, 8 /*1/4T*/);
    CHECK_NEAR(tri.divisions(), 2.6666667f, 1e-5);
    CHECK(tri.gridCount() == 3);
    CHECK_NEAR(tri.gridNode(2), 0.75f, 1e-6);
    // Coarse grid over a short rate clamps to one division.
    const TimelineMap one = mapFor(0.f, 0.f, 0 /*1/2*/, 12 /*1/16*/);
    CHECK(one.divisions() == 1.f);
    CHECK(one.gridCount() == 1);
}

TEST_CASE(timeline_rotation_and_seam)
{
    const TimelineMap m = mapFor(90.f, 0.f);   // rotate = 0.25
    CHECK_NEAR(m.rotation(), 0.25f, 1e-7);
    CHECK_NEAR(m.toTimeline(0.f), 0.25f, 1e-7);
    CHECK_NEAR(m.toTimeline(1.f), 0.25f, 1e-7);   // both endpoints meet at the seam
    CHECK_NEAR(m.toTimeline(0.75f), 0.f, 1e-7);   // wraps to the left edge
    CHECK_NEAR(m.toTimeline(0.5f), 0.75f, 1e-7);
    CHECK_NEAR(m.toNode(0.1f), 0.85f, 1e-6);
    CHECK(m.toNode(0.25f) == 0.f);
    CHECK_NEAR(m.timelineToSeam(0.1f), 0.85f, 1e-6);
    CHECK_NEAR(m.seamToTimeline(0.85f), 0.1f, 1e-6);
    // Negative and > 360 rotations wrap like the engine's.
    const TimelineMap neg = mapFor(-90.f, 0.f);
    CHECK_NEAR(neg.rotation(), 0.75f, 1e-6);
    const TimelineMap big = mapFor(450.f, 0.f);
    CHECK_NEAR(big.rotation(), 0.25f, 1e-6);
    // Non-finite parameters degrade to the identity map.
    TimingParams bad;
    bad.rotateDeg    = std::nanf("");
    bad.swingPercent = std::numeric_limits<float>::infinity();
    const TimelineMap b(makePhaseMap(bad));
    CHECK(b.rotation() == 0.f);
    CHECK(!b.swingActive());
}

TEST_CASE(timeline_swing_warp)
{
    const TimelineMap m = mapFor(0.f, 100.f);   // 4 cells, pairs of 0.5, midpoint → 0.375
    CHECK(m.swingActive());
    CHECK_NEAR(m.toSeam(0.25f), 0.375f, 1e-6);
    CHECK_NEAR(m.toSeam(0.5f), 0.5f, 1e-6);
    CHECK_NEAR(m.toSeam(0.75f), 0.875f, 1e-6);
    CHECK_NEAR(m.toSeam(0.3f), 0.4f, 1e-6);
    CHECK(m.toSeam(1.f) == 1.f);
    CHECK(m.toSeam(0.f) == 0.f);
    float worst = 0.f;
    for (int i = 0; i <= 10000; ++i) {
        const float x = float(i) / 10000.f;
        worst = std::max(worst, std::fabs(m.fromSeam(m.toSeam(x)) - x));
        if (i > 0)
            CHECK(m.toSeam(x) >= m.toSeam(float(i - 1) / 10000.f));   // monotonic
    }
    CHECK(worst <= 2e-6f);

    std::vector<float> kinks;
    m.kinksBetween(0.f, 1.f, kinks);
    CHECK(kinks.size() == 3);
    if (kinks.size() == 3) {
        CHECK_NEAR(kinks[0], 0.25f, 1e-7);
        CHECK_NEAR(kinks[1], 0.5f, 1e-7);
        CHECK_NEAR(kinks[2], 0.75f, 1e-7);
    }
    m.kinksBetween(0.3f, 0.6f, kinks);
    CHECK(kinks.size() == 1);
    mapFor(0.f, 0.f).kinksBetween(0.f, 1.f, kinks);
    CHECK(kinks.empty());
    // Odd divisions: the trailing incomplete pair stays straight (and has no kinks).
    const TimelineMap tri = mapFor(0.f, 100.f, 4, 8);   // 2.667 divisions: one complete pair
    tri.kinksBetween(0.f, 1.f, kinks);
    CHECK(kinks.size() == 2);   // the pair's midpoint and its end
    CHECK(tri.toSeam(0.9f) == 0.9f);
}

TEST_CASE(timeline_agrees_with_phasemap)
{
    Rng rng(11);
    float worst = 0.f;
    for (int c = 0; c < 400; ++c) {
        TimingParams t;
        t.rotateDeg    = rng.range(-720.f, 720.f);
        t.swingPercent = rng.range(0.f, 100.f);
        t.gridIndex    = rng.below(kick::kNumGrids);
        t.rateIndex    = rng.below(kick::kNumRates);
        t.timeMode     = rng.below(2);
        const kick::PhaseMap pm = makePhaseMap(t);
        const TimelineMap    m(pm);
        for (int k = 0; k < 50; ++k) {
            const float x = rng.uniform();
            worst = std::max(worst, wrapDiff(m.toTimeline(x), pm.toTimeline(x)));
            const float p = rng.uniform();
            CHECK(m.toNode(p) == pm.toNode(p));
            worst = std::max(worst, wrapDiff(m.fromSeam(m.timelineToSeam(p)), pm.toNode(p)));
        }
    }
    CHECK_MSG(worst <= 3e-6f, "worst deviation %g", double(worst));
}

TEST_CASE(timeline_snapping_swing_and_rotate)
{
    // Straight grid.
    const TimelineMap m = mapFor(0.f, 0.f);
    CHECK(m.snapNode(0.1f) == 0.f);
    CHECK(m.snapNode(0.13f) == 0.25f);
    CHECK(m.snapNode(0.9f) == 1.f);   // the cycle end is a snap target
    CHECK(m.snapNode(0.6f) == 0.5f);
    // Swung grid: lines are shown at 0, 0.375, 0.5, 0.875 and snapping uses those positions.
    const TimelineMap s = mapFor(0.f, 100.f);
    CHECK(s.snapNode(s.toNode(0.36f)) == 0.25f);
    CHECK_NEAR(s.toTimeline(s.snapNode(s.toNode(0.36f))), 0.375f, 1e-6);
    CHECK(s.snapNode(s.toNode(0.45f)) == 0.5f);     // 0.05 from 0.5, 0.075 from 0.375
    CHECK(s.snapNode(s.toNode(0.43f)) == 0.25f);    // 0.055 from 0.375, 0.07 from 0.5
    // Rotation shifts the whole (swung) grid with the shape.
    const TimelineMap r = mapFor(36.f, 100.f);      // rotate 0.1
    CHECK(r.snapNode(r.toNode(0.47f)) == 0.25f);
    CHECK_NEAR(r.toTimeline(0.25f), 0.475f, 1e-6);
    // Every snapped result is a grid line or the cycle end.
    Rng rng(5);
    for (int i = 0; i < 2000; ++i) {
        const TimelineMap mm = mapFor(rng.range(0.f, 360.f), rng.range(0.f, 100.f), rng.below(kick::kNumGrids),
                                      rng.below(kick::kNumRates));
        const float q = mm.snapNode(rng.uniform());
        const float k = q * mm.divisions();
        CHECK(q == 1.f || std::fabs(k - std::round(k)) < 1e-3f);
    }
    CHECK_NEAR(TimelineMap::snapDelta(0.1f, 0.0625f), 0.125f, 1e-7);
    CHECK_NEAR(TimelineMap::snapDelta(-0.03f, 0.0625f), 0.f, 1e-7);
    CHECK(TimelineMap::snapDelta(0.1f, 0.f) == 0.1f);
    CHECK(TimelineMap::snapDelta(std::nanf(""), 0.1f) == 0.f);
}

TEST_CASE(timeline_params_from_values)
{
    float values[kick::kParamCount] = {};
    for (int i = 0; i < kick::kParamCount; ++i)
        values[i] = kick::kParams[i].def;
    values[kick::kParamRotate] = 45.f;
    values[kick::kParamGrid]   = 2.4f;   // rounds like the engine
    values[kick::kParamSwing]  = 30.f;
    values[kick::kParamRate]   = 99.f;   // clamped
    const TimingParams t = TimingParams::fromParamValues(values);
    CHECK(t.rotateDeg == 45.f);
    CHECK(t.gridIndex == 2);
    CHECK(t.swingPercent == 30.f);
    CHECK(t.rateIndex == kick::kNumRates - 1);
    CHECK(TimingParams::fromParamValues(nullptr).gridIndex == kick::kDefaultGridIndex);
    const kick::PhaseMap pm = makePhaseMap(t);
    CHECK_NEAR(pm.rotate01, 0.125f, 1e-7);
    CHECK_NEAR(pm.swing, 0.3f, 1e-7);
}
