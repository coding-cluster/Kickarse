// Kickarse — editor tests: model-level transforms, clipboard, shapes, bake rotation.
#include "EditorTest.h"

using namespace et;

namespace {

const Envelope kShape = makeEnv({{0, 0, 0.2f, 0}, {0.2f, 0.5f, -0.4f, 0}, {0.4f, 1, 0.1f, 0}, {0.6f, 0.2f, 0.7f, 0},
                                 {0.8f, 0.6f, 0.3f, 0}, {1, 1, 0, 0}});

void load(Fixture& f, const Envelope& e)
{
    f.model.load(e, e, LoadHistory::Clear);
    f.listener.clear();
}

std::vector<float> sampleTimeline(const EditorModel& m, int n = 257)
{
    std::vector<float> v;
    for (int i = 0; i < n; ++i)
        v.push_back(m.valueAt((float(i) + 0.43f) / float(n)));
    return v;
}

} // namespace

TEST_CASE(xf_invert_and_scale)
{
    Fixture f;
    load(f, kShape);
    EditorModel& m = f.model;
    CHECK(m.invert());
    CHECK_NEAR(m.editEnvelope().node(2).y, 0.f, 1e-7);
    CHECK_NEAR(m.editEnvelope().node(0).y, 1.f, 1e-7);
    CHECK(m.invert());
    CHECK(ops::sameNodes(m.editEnvelope(), kShape, 1e-6f));
    m.select(1);
    CHECK(!m.invert());  // selection only: 0.5 inverts onto itself, no step
    CHECK_NEAR(m.editEnvelope().node(1).y, 0.5f, 1e-7);
    m.select(3);
    CHECK(m.invert());
    CHECK_NEAR(m.editEnvelope().node(3).y, 0.8f, 1e-6);
    CHECK_NEAR(m.editEnvelope().node(2).y, 1.f, 1e-7);
    m.selectNone();
    CHECK(m.scaleY(0.5f));
    CHECK_NEAR(m.editEnvelope().node(0).y, 0.5f, 1e-7);
    CHECK(!m.scaleY(std::nanf("")));
    CHECK(m.undoLabel() == "Scale");
}

TEST_CASE(xf_reverse_and_span)
{
    Fixture f;
    load(f, kShape);
    EditorModel& m = f.model;
    CHECK(m.reverse());
    CHECK(m.selectionCount() == 0);
    for (int k = 0; k < 50; ++k) {
        const float x = (float(k) + 0.5f) / 50.f;
        CHECK_NEAR(m.editEnvelope().evaluate(1.f - x), kShape.evaluate(x), 1e-4);
    }
    CHECK(m.reverse());
    CHECK(ops::sameNodes(m.editEnvelope(), kShape, 1e-6f));
    // On a selection span only: [0.2, 0.6] mirrored, outside untouched, span stays selected.
    m.select(1);
    m.select(3, SelectMode::Add);
    CHECK(m.reverse());
    const Envelope& e = m.editEnvelope();
    CHECK(e.size() == kShape.size());
    CHECK_NEAR(e.node(1).y, 0.2f, 1e-7);
    CHECK_NEAR(e.node(3).y, 0.5f, 1e-7);
    CHECK(e.node(4).y == 0.6f && e.node(0).y == 0.f);
    CHECK(m.isSelected(1) && m.isSelected(2) && m.isSelected(3) && m.selectionCount() == 3);
}

TEST_CASE(xf_rotate_duplicate_halve_mirror)
{
    Fixture f;
    load(f, kShape);
    EditorModel& m = f.model;
    CHECK(m.rotateShape(0.25f));
    for (int k = 0; k < 60; ++k) {
        const float x = (float(k) + 0.5f) / 60.f;
        float       y = x + 0.25f;
        y -= std::floor(y);
        CHECK_NEAR(m.editEnvelope().evaluate(y), kShape.evaluate(x), 1e-4);
    }
    CHECK(!m.rotateShape(std::nanf("")));
    load(f, kShape);
    CHECK(m.duplicate());
    CHECK(m.editEnvelope().size() == 2 * kShape.size());
    CHECK(m.halve());
    CHECK(ops::sameNodes(m.editEnvelope(), kShape, 1e-6f));
    // Too many nodes: refused without an undo step.
    ops::NodeList many;
    for (int i = 0; i < 70; ++i)
        many.push_back(EnvNode{float(i) / 69.f, float(i % 2), 0.f, 0});
    load(f, makeEnv(many));
    CHECK(!m.duplicate());
    CHECK(!m.canUndo());
    // Mirror: symmetric.
    load(f, kShape);
    CHECK(m.mirrorLeftToRight());
    for (int k = 0; k < 50; ++k) {
        const float x = 0.5f * (float(k) + 0.5f) / 50.f;
        CHECK_NEAR(m.editEnvelope().evaluate(1.f - x), m.editEnvelope().evaluate(x), 1e-4);
    }
    // Duplicate on a span repeats the span within itself.
    load(f, kShape);
    m.select(1);
    m.select(3, SelectMode::Add);
    CHECK(m.duplicate());
    CHECK_NEAR(m.editEnvelope().evaluate(0.3f), kShape.evaluate(0.4f), 1e-4);
    CHECK_NEAR(m.editEnvelope().evaluate(0.8f), kShape.evaluate(0.8f), 1e-6);
    CHECK(validEnvelope(m.editEnvelope(), "span duplicate"));
}

TEST_CASE(xf_flat_reset_apply_stretch_simplify)
{
    Fixture f;
    load(f, kShape);
    EditorModel& m = f.model;
    CHECK(m.setFlat(0.25f));
    CHECK(m.editEnvelope().size() == 2 && m.editEnvelope().node(1).y == 0.25f);
    CHECK(m.resetToDefault());
    CHECK(ops::sameNodes(m.editEnvelope(), Envelope()));
    CHECK(!m.resetToDefault());   // already default: no step
    load(f, kShape);
    m.select(1);
    m.select(3, SelectMode::Add);
    CHECK(m.setFlat());
    CHECK(m.editEnvelope().size() == kShape.size() - 1);
    CHECK_NEAR(m.editEnvelope().evaluate(0.4f), 1.f, 1e-6);
    // Apply a library shape: whole, and into the selection span.
    load(f, kShape);
    CHECK(m.applyShape(Envelope::flat()));
    CHECK(ops::sameNodes(m.editEnvelope(), Envelope::flat()));
    CHECK(m.undoLabel() == "Apply Shape");
    load(f, kShape);
    m.select(1);
    m.select(4, SelectMode::Add);   // span [0.2, 0.8]
    CHECK(m.applyShape(Envelope(), true));
    CHECK_NEAR(m.editEnvelope().evaluate(0.2f), 0.f, 1e-6);
    CHECK_NEAR(m.editEnvelope().evaluate(0.5f), 1.f, 1e-6);
    CHECK(m.editEnvelope().node(0).y == 0.f && m.editEnvelope().node(0).tension != 0.f);   // outside kept
    // Stretch the selection span onto a new interval, clamped by the neighbours.
    load(f, kShape);
    m.select(2);
    m.select(3, SelectMode::Add);   // [0.4, 0.6]
    CHECK(m.stretchSelection(0.3f, 0.7f));
    CHECK_NEAR(m.editEnvelope().node(2).x, 0.3f, 1e-6);
    CHECK_NEAR(m.editEnvelope().node(3).x, 0.7f, 1e-6);
    CHECK(m.stretchSelection(0.f, 1.f));   // clamped at 0.2 and 0.8
    CHECK_NEAR(m.editEnvelope().node(2).x, 0.2f, 1e-6);
    CHECK_NEAR(m.editEnvelope().node(3).x, 0.8f, 1e-6);
    m.selectNone();
    CHECK(!m.stretchSelection(0.1f, 0.2f));
    // Stretch gesture: dragging the end edge.
    load(f, kShape);
    m.select(2);
    m.select(3, SelectMode::Add);
    CHECK(m.beginStretch(StretchEdge::End));
    m.updateStretch(0.1f);
    CHECK_NEAR(m.editEnvelope().node(3).x, 0.7f, 1e-6);
    m.updateStretch(0.5f);
    CHECK_NEAR(m.editEnvelope().node(3).x, 0.8f, 1e-6);
    CHECK(m.endGesture());
    m.selectAll();
    CHECK(!m.beginStretch(StretchEdge::Start));   // the endpoint can't move
    // Simplify.
    std::vector<float> ys(256);
    for (size_t i = 0; i < ys.size(); ++i)
        ys[i] = 0.5f + 0.4f * std::sin(float(i) / 256.f * 19.f);
    load(f, Envelope::fromSamples(ys.data(), 256, 0.001f));
    const int before = m.editEnvelope().size();
    CHECK(m.simplify(4, 0.f));
    CHECK(m.editEnvelope().size() <= 4 && m.editEnvelope().size() < before);
}

TEST_CASE(xf_clipboard_and_bands)
{
    Fixture f;
    EditorModel& m = f.model;
    m.load(kShape, Envelope::flat(), LoadHistory::Clear);
    const std::string text = m.copy();
    CHECK(text.rfind("KE1 ", 0) == 0);
    CHECK(m.clipboard() == text);
    m.setActiveBand(Band::B);
    CHECK(m.paste());
    CHECK(ops::sameNodes(m.envelope(Band::B), kShape));
    CHECK(m.undoLabel() == "Paste");
    // Copying a selection span gives a normalised [0,1] envelope.
    m.setActiveBand(Band::A);
    m.select(1);
    m.select(3, SelectMode::Add);
    Envelope clip;
    CHECK(clip.deserialize(m.copy()));
    CHECK(clip.size() == 3);
    CHECK_NEAR(clip.node(1).x, 0.5f, 1e-6);
    CHECK(!m.setClipboard("not an envelope"));
    CHECK(m.clipboard() == m.copy());
    CHECK(m.setClipboard("KE1 0,0.5,0,0;1,0.5,0,0"));
    m.selectNone();
    CHECK(m.paste());
    CHECK(m.envelope(Band::A).size() == 2);
    // Paste into a selection span.
    m.load(kShape, kShape, LoadHistory::Clear);
    m.select(1);
    m.select(4, SelectMode::Add);
    CHECK(m.paste(true));
    CHECK_NEAR(m.envelope(Band::A).evaluate(0.5f), 0.5f, 1e-6);
    CHECK(m.envelope(Band::A).node(0).y == 0.f);
    // Copy between bands.
    m.load(kShape, Envelope::flat(), LoadHistory::Clear);
    CHECK(m.copyToBand(Band::B));
    CHECK(ops::sameNodes(m.envelope(Band::B), kShape));
    CHECK(m.undoLabel() == "Copy to High Band");
    CHECK(!m.copyToBand(Band::A));   // same band
}

TEST_CASE(xf_bake_rotation_exact)
{
    for (float swing : {0.f, 60.f}) {
        Fixture f;
        load(f, kShape);
        EditorModel& m = f.model;
        f.setTiming(100.f, swing);
        const std::vector<float> before = sampleTimeline(m);
        CHECK(m.bakeRotation());
        CHECK(m.timing().rotateDeg == 0.f);
        CHECK(f.listener.params.size() == 1 && f.listener.params[0].first == kick::kParamRotate
              && f.listener.params[0].second == 0.f);
        const std::vector<float> after = sampleTimeline(m);
        float worst = 0.f;
        for (size_t i = 0; i < before.size(); ++i)
            worst = std::max(worst, std::fabs(before[i] - after[i]));
        CHECK_MSG(worst < 1e-3f, "swing %g: baked curve differs by %g", double(swing), double(worst));
        CHECK(validEnvelope(m.editEnvelope(), "bake"));
        // Exact under swing: the remap has breakpoints at both sets of swing kinks (+2 for the seam cut).
        CHECK(m.editEnvelope().size() <= kShape.size() + (swing > 0.f ? 8 : 2));
        CHECK(m.undoLabel() == "Bake Rotation");
        CHECK(m.undo());
        CHECK(ops::sameNodes(m.editEnvelope(), kShape));
        CHECK(m.timing().rotateDeg == 100.f);
        CHECK(f.listener.params.back().second == 100.f);
    }
    Fixture g;
    CHECK(!g.model.bakeRotation());   // no rotation: nothing to bake
}
