// Kickarse — editor-model tests: minimal self-registering harness + shared helpers.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "editor/EditorController.h"
#include "editor/EditorModel.h"
#include "editor/EnvelopeOps.h"

namespace et {

struct TestCase {
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& registry();

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

extern int g_checks;
extern int g_failures;

void fail(const char* file, int line, const std::string& message);
void note(const char* fmt, ...);

} // namespace et

#define ET_CAT2(a, b) a##b
#define ET_CAT(a, b) ET_CAT2(a, b)

#define TEST_CASE(name)                                                  \
    static void name();                                                  \
    static const et::Registrar ET_CAT(name, _registrar)(#name, &name);   \
    static void name()

#define CHECK(cond)                                  \
    do {                                             \
        ++et::g_checks;                              \
        if (!(cond))                                 \
            et::fail(__FILE__, __LINE__, #cond);     \
    } while (0)

#define CHECK_MSG(cond, ...)                                      \
    do {                                                          \
        ++et::g_checks;                                           \
        if (!(cond)) {                                            \
            char et_buf_[512];                                    \
            std::snprintf(et_buf_, sizeof(et_buf_), __VA_ARGS__); \
            et::fail(__FILE__, __LINE__, et_buf_);                \
        }                                                         \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                              \
    do {                                                                                   \
        ++et::g_checks;                                                                    \
        const double et_a_ = double(a), et_b_ = double(b);                                 \
        if (!(std::fabs(et_a_ - et_b_) <= double(tol))) {                                  \
            char et_buf_[512];                                                             \
            std::snprintf(et_buf_, sizeof(et_buf_), "%s = %.9g, %s = %.9g (tol %.3g)", #a,  \
                          et_a_, #b, et_b_, double(tol));                                  \
            et::fail(__FILE__, __LINE__, et_buf_);                                         \
        }                                                                                  \
    } while (0)

// ---- helpers shared by the test files ------------------------------------------------------------
namespace et {

using kick::EnvNode;
using kick::Envelope;
using namespace kick::editor;

// Builds an envelope from {x, y, tension, flags} rows (must be sorted; endpoints at 0 and 1).
Envelope makeEnv(const std::vector<EnvNode>& nodes);
// Checks structural validity and the absence of editor tag bits; reports `where` on failure.
bool validEnvelope(const Envelope& e, const char* where);
std::string describe(const Envelope& e);

// Records every listener callback.
struct RecordingListener : EditorListener {
    struct EnvEvent {
        Band     band;
        Envelope env;
        bool     final;
    };
    std::vector<EnvEvent>                   envEvents;
    std::vector<std::pair<uint32_t, float>> params;
    int                                     historyEvents = 0;
    int                                     stateEvents   = 0;

    void envelopeChanged(Band band, const Envelope& env, bool final) override
    {
        envEvents.push_back({band, env, final});
    }
    void parameterChanged(uint32_t id, float value) override { params.push_back({id, value}); }
    void historyChanged() override { ++historyEvents; }
    void editorStateChanged() override { ++stateEvents; }
    int  finals() const
    {
        int n = 0;
        for (const EnvEvent& e : envEvents)
            n += e.final ? 1 : 0;
        return n;
    }
    int lives() const { return int(envEvents.size()) - finals(); }
    void clear()
    {
        envEvents.clear();
        params.clear();
        historyEvents = 0;
        stateEvents   = 0;
    }
};

// A model with snapping off, no rotation/swing, a fake clock and a 4-cell grid (rate 1/4, grid 1/16).
struct Fixture {
    EditorModel       model;
    RecordingListener listener;
    double            clock = 100.0;

    Fixture();
    void setTiming(float rotateDeg, float swingPct, int gridIndex = kick::kDefaultGridIndex,
                   int rateIndex = kick::kDefaultRateIndex);
    void setSnap(bool enabled, bool snapY = false, float magnet = 0.f);
    void advance(double seconds) { clock += seconds; }
};

// A deterministic xorshift RNG for the fuzz tests.
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint64_t next()
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    float uniform() { return float(next() >> 40) / float(1ull << 24); }                  // [0,1)
    float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }
    int   below(int n) { return n > 0 ? int(next() % uint64_t(n)) : 0; }
    bool  chance(float p) { return uniform() < p; }
};

} // namespace et
