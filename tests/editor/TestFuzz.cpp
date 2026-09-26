// Kickarse — editor tests: random-operation fuzzing (validity, invariants, undo/redo consistency).
#include "EditorTest.h"

using namespace et;

namespace {

float wild(Rng& rng)
{
    switch (rng.below(12)) {
    case 0:  return std::nanf("");
    case 1:  return std::numeric_limits<float>::infinity();
    case 2:  return -1e9f;
    case 3:  return rng.range(-2.f, 3.f);
    default: return rng.uniform();
    }
}

float coord(Rng& rng, bool allowWild)
{
    return allowWild && rng.chance(0.08f) ? wild(rng) : rng.uniform();
}

Envelope randomEnvelope(Rng& rng)
{
    const int n = 2 + rng.below(rng.chance(0.1f) ? Envelope::kMaxNodes - 1 : 12);
    std::vector<float> xs;
    for (int i = 0; i < n - 2; ++i)
        xs.push_back(rng.chance(0.15f) && !xs.empty() ? xs.back() : rng.uniform());
    std::sort(xs.begin(), xs.end());
    ops::NodeList v;
    v.push_back(EnvNode{0.f, rng.uniform(), rng.range(-1.f, 1.f), 0});
    for (float x : xs)
        v.push_back(EnvNode{x, rng.uniform(), rng.range(-1.f, 1.f), rng.chance(0.2f) ? uint32_t(kick::kNodeQuickShift) : 0u});
    v.push_back(EnvNode{1.f, rng.uniform(), 0.f, 0});
    return makeEnv(v);
}

bool checkInvariants(const EditorModel& m, const char* where)
{
    bool ok = validEnvelope(m.envelope(Band::A), where) && validEnvelope(m.envelope(Band::B), where);
    const int n = m.editEnvelope().size();
    for (int i = n; i < Envelope::kMaxNodes; ++i)
        if (m.selection().test(size_t(i)))
            ok = false;
    const QuickShiftRange r = m.quickShiftRange();
    if (r.valid && !(r.start >= 0.f && r.end <= 1.f && r.start <= r.end))
        ok = false;
    ++et::g_checks;
    if (!ok)
        et::fail(__FILE__, __LINE__, std::string("invariant broken after ") + where);
    return ok;
}

// One random operation. undoableOnly: no loads/params/timing changes (for the history round trip).
const char* randomOp(EditorModel& m, Rng& rng, bool undoableOnly, bool wildArgs)
{
    const int n = m.editEnvelope().size();
    const int idx = wildArgs && rng.chance(0.05f) ? rng.below(300) - 100 : rng.below(std::max(n, 1));
    switch (rng.below(undoableOnly ? 30 : 38)) {
    case 0:  m.addNode(coord(rng, wildArgs), coord(rng, wildArgs), rng.chance(0.5f)); return "addNode";
    case 1:  m.addNodeOnCurve(coord(rng, wildArgs), rng.chance(0.5f)); return "addNodeOnCurve";
    case 2:  m.deleteNode(idx); return "deleteNode";
    case 3:  m.deleteSelection(); return "deleteSelection";
    case 4:  m.setTension(idx, rng.range(-2.f, 2.f)); return "setTension";
    case 5:  m.straightenSegment(idx); return "straighten";
    case 6:  m.setNodePosition(idx, coord(rng, wildArgs), coord(rng, wildArgs)); return "setNodePosition";
    case 7:  m.nudgeSelection(rng.below(5) - 2, rng.below(5) - 2, rng.chance(0.5f)); return "nudge";
    case 8: {
        m.select(idx, SelectMode(rng.below(4)));
        if (m.beginNodeDrag(idx))
            for (int k = rng.below(6); k >= 0; --k)
                m.updateNodeDrag(rng.range(-1.2f, 1.2f), rng.range(-1.2f, 1.2f),
                                 DragOptions{rng.chance(0.3f), AxisLock(rng.below(3))});
        rng.chance(0.8f) ? (void)m.endGesture() : m.cancelGesture();
        return "nodeDrag";
    }
    case 9: {
        if (m.beginTensionDrag(idx, rng.chance(0.5f)))
            for (int k = rng.below(5); k >= 0; --k)
                m.updateTensionDrag(rng.range(-3.f, 3.f));
        rng.chance(0.8f) ? (void)m.endGesture() : m.cancelGesture();
        return "tensionDrag";
    }
    case 10: {
        if (m.beginLine(coord(rng, wildArgs), coord(rng, wildArgs), DragOptions{rng.chance(0.5f), AxisLock::None}))
            for (int k = rng.below(5); k >= 0; --k) {
                const float t = rng.chance(0.2f) ? m.gestureInfo().lineStart.x : coord(rng, wildArgs);
                m.updateLine(t, coord(rng, wildArgs), DragOptions{rng.chance(0.5f), AxisLock::None});
            }
        rng.chance(0.85f) ? (void)m.endGesture() : m.cancelGesture();
        return "line";
    }
    case 11: {
        if (m.beginPencil(coord(rng, wildArgs), coord(rng, wildArgs))) {
            float t = rng.uniform(), v = rng.uniform();
            for (int k = rng.below(120); k >= 0; --k) {
                t = std::clamp(t + rng.range(-0.03f, 0.04f), 0.f, 1.f);
                v = std::clamp(v + rng.range(-0.1f, 0.1f), 0.f, 1.f);
                m.addPencilPoint(t, v);
            }
        }
        rng.chance(0.9f) ? (void)m.endGesture() : m.cancelGesture();
        return "pencil";
    }
    case 12: {
        const QuickShiftPart part = QuickShiftPart(rng.below(4));
        if (m.beginQuickShiftDrag(part, coord(rng, wildArgs)))
            for (int k = rng.below(5); k >= 0; --k)
                m.updateQuickShiftDrag(rng.range(-1.2f, 1.2f), DragOptions{rng.chance(0.5f), AxisLock::None});
        rng.chance(0.8f) ? (void)m.endGesture() : m.cancelGesture();
        return "qsDrag";
    }
    case 13: {
        if (m.beginStretch(StretchEdge(rng.below(2))))
            for (int k = rng.below(4); k >= 0; --k)
                m.updateStretch(rng.range(-1.f, 1.f), DragOptions{rng.chance(0.5f), AxisLock::None});
        rng.chance(0.8f) ? (void)m.endGesture() : m.cancelGesture();
        return "stretch";
    }
    case 14: m.toggleQuickShiftMember(idx); return "qsToggle";
    case 15: m.setQuickShiftGroupFromSelection(); return "qsFromSel";
    case 16: m.setQuickShiftRange(coord(rng, wildArgs), coord(rng, wildArgs), rng.chance(0.7f)); return "qsRange";
    case 17: m.shiftGroup(rng.range(-1.f, 1.f), rng.chance(0.5f)); return "shiftGroup";
    case 18: m.invert(); return "invert";
    case 19: m.reverse(); return "reverse";
    case 20: m.rotateShape(wildArgs && rng.chance(0.1f) ? wild(rng) : rng.range(-2.f, 2.f)); return "rotate";
    case 21: m.duplicate(); return "duplicate";
    case 22: m.halve(); return "halve";
    case 23: m.scaleY(rng.range(-1.f, 3.f)); return "scaleY";
    case 24: m.setFlat(coord(rng, wildArgs)); return "setFlat";
    case 25: m.mirrorLeftToRight(); return "mirror";
    case 26: m.simplify(2 + rng.below(20), rng.range(0.f, 0.1f)); return "simplify";
    case 27: m.stretchSelection(coord(rng, wildArgs), coord(rng, wildArgs)); return "stretchSelection";
    case 28: {
        m.copy();
        m.paste(rng.chance(0.5f));
        return "copyPaste";
    }
    case 29: {
        switch (rng.below(4)) {
        case 0:  m.selectAll(); break;
        case 1:  m.selectNone(); break;
        case 2:  m.selectRect(rng.uniform(), rng.uniform(), rng.uniform(), rng.uniform(), SelectMode(rng.below(4))); break;
        default: m.selectQuickShiftGroup(); break;
        }
        return "select";
    }
    // Not undo-neutral: loads, timing, bands, link, history, parameters.
    case 30: m.load(randomEnvelope(rng), randomEnvelope(rng), LoadHistory(rng.below(3)), rng.chance(0.5f)); return "load";
    case 31: {
        static const char* junk[] = {"KE1 0,0,0,0;1,1,0,0", "KE1 garbage", "", "KE1 0.5,nan,0;1,1,0",
                                     "KE1 0,2,9,1;0.5,-1,0,4294967295;1,1,1,1"};
        m.loadState(Band(rng.below(2)), junk[rng.below(5)], LoadHistory::Keep, rng.chance(0.5f));
        return "loadState";
    }
    case 32: {
        TimingParams t;
        t.rotateDeg    = rng.chance(0.4f) ? 0.f : rng.range(-400.f, 400.f);
        t.swingPercent = rng.chance(0.5f) ? 0.f : rng.range(0.f, 100.f);
        t.gridIndex    = rng.below(kick::kNumGrids);
        t.rateIndex    = rng.below(kick::kNumRates);
        t.timeMode     = rng.below(2);
        m.setTiming(t);
        SnapSettings s;
        s.enabled = rng.chance(0.5f);
        s.snapY   = rng.chance(0.3f);
        s.yMagnet = rng.chance(0.5f) ? 0.015f : wild(rng);
        m.setSnap(s);
        return "timing";
    }
    case 33: m.setActiveBand(Band(rng.below(2))); return "band";
    case 34: rng.chance(0.5f) ? m.setLinked(rng.chance(0.5f)) : (void)m.setLinkedByUser(rng.chance(0.5f)); return "link";
    case 35: rng.chance(0.5f) ? (void)m.undo() : (void)m.redo(); return "undoRedo";
    case 36: m.recordParameterChange(uint32_t(rng.below(kick::kParamCount + 2)), rng.range(0.f, 100.f), rng.range(0.f, 100.f)); return "param";
    default: m.bakeRotation(); return "bake";
    }
}

} // namespace

TEST_CASE(fuzz_random_operations_stay_valid)
{
    constexpr int kSeeds = 6;
    constexpr int kOps   = 1500;
    for (int seed = 1; seed <= kSeeds; ++seed) {
        Rng     rng(uint64_t(seed) * 7919u);
        Fixture f;
        f.model.setHistoryLimit(64);
        for (int i = 0; i < kOps; ++i) {
            const char* op = randomOp(f.model, rng, false, true);
            if (rng.chance(0.03f))
                f.advance(1.0);
            if (!checkInvariants(f.model, op))
                return;
            // Listener payloads must be valid too (they are pushed to the host).
            for (const RecordingListener::EnvEvent& e : f.listener.envEvents)
                if (!validEnvelope(e.env, op))
                    return;
            f.listener.envEvents.clear();
        }
    }
}

TEST_CASE(fuzz_undo_all_redo_all)
{
    for (int seed = 1; seed <= 8; ++seed) {
        Rng     rng(uint64_t(seed) * 104729u);
        Fixture f;
        EditorModel& m = f.model;
        m.load(randomEnvelope(rng), randomEnvelope(rng), LoadHistory::Clear);
        m.setHistoryLimit(100000);
        const Envelope a0 = m.envelope(Band::A), b0 = m.envelope(Band::B);
        for (int i = 0; i < 300; ++i) {
            randomOp(m, rng, true, false);
            f.advance(1.0);   // no coalescing across ops
            if (rng.chance(0.1f))
                m.setActiveBand(Band(rng.below(2)));
        }
        const Envelope a1 = m.envelope(Band::A), b1 = m.envelope(Band::B);
        int undone = 0;
        while (m.undo())
            ++undone;
        CHECK(ops::sameNodes(m.envelope(Band::A), a0) && ops::sameNodes(m.envelope(Band::B), b0));
        int redone = 0;
        while (m.redo())
            ++redone;
        CHECK(undone == redone);
        CHECK(ops::sameNodes(m.envelope(Band::A), a1) && ops::sameNodes(m.envelope(Band::B), b1));
    }
}

TEST_CASE(fuzz_ops_on_node_lists)
{
    Rng rng(4242);
    for (int i = 0; i < 3000; ++i) {
        const Envelope e = randomEnvelope(rng);
        ops::NodeList  v = ops::toList(e);
        bool           ok = true;
        switch (rng.below(8)) {
        case 0: ok = ops::rotate(v, wild(rng)); break;
        case 1: ok = ops::duplicate(v); break;
        case 2: ok = ops::halve(v); break;
        case 3: ok = ops::mirrorLeftToRight(v); break;
        case 4: ops::reverse(v); break;
        case 5: ok = ops::replaceSpan(v, wild(rng), wild(rng), {{rng.uniform(), wild(rng), wild(rng), 0}, {rng.uniform(), wild(rng), 0, 0}}); break;
        case 6: ok = ops::simplify(v, rng.below(10) - 2, wild(rng)); break;
        default: {
            const int a = rng.below(int(v.size())), b = rng.below(int(v.size()));
            ok = ops::transformSpan(v, std::min(a, b), std::max(a, b), [&](ops::NodeList& c) { return ops::rotate(c, rng.uniform()); });
            break;
        }
        }
        (void)ok;
        Envelope out;
        if (v.size() <= size_t(Envelope::kMaxNodes)) {
            CHECK(ops::fromList(v, out));
            if (!validEnvelope(out, "ops fuzz"))
                return;
        }
    }
}
