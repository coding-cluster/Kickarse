// Kickarse — minimal self-registering test harness for the preset/shape test runner.
// Deliberately not shared with tests/TestHarness.h (DSP agent's own runner, a separate
// executable): keeping the two independent means either side can evolve its harness without
// coordinating, at the cost of a small amount of duplication.
#pragma once

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace kpt {

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

// A fresh, isolated scratch directory for filesystem-touching tests (see TestMain.cpp); wiped
// clean on every call. Pass a name unique to the calling test (e.g. the test's own name).
std::string scratchDir(const char* subdir);

} // namespace kpt

#define KPT_CAT2(a, b) a##b
#define KPT_CAT(a, b) KPT_CAT2(a, b)

#define TEST_CASE(name)                                                   \
    static void name();                                                  \
    static const kpt::Registrar KPT_CAT(name, _registrar)(#name, &name); \
    static void name()

#define CHECK(cond)                              \
    do {                                          \
        ++kpt::g_checks;                          \
        if (!(cond))                              \
            kpt::fail(__FILE__, __LINE__, #cond); \
    } while (0)

#define CHECK_MSG(cond, ...)                                         \
    do {                                                             \
        ++kpt::g_checks;                                             \
        if (!(cond)) {                                               \
            char kpt_buf_[512];                                      \
            std::snprintf(kpt_buf_, sizeof(kpt_buf_), __VA_ARGS__);   \
            kpt::fail(__FILE__, __LINE__, kpt_buf_);                  \
        }                                                             \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                             \
    do {                                                                                  \
        ++kpt::g_checks;                                                                  \
        const double kpt_a_ = double(a), kpt_b_ = double(b);                              \
        if (!(std::fabs(kpt_a_ - kpt_b_) <= double(tol))) {                               \
            char kpt_buf_[512];                                                           \
            std::snprintf(kpt_buf_, sizeof(kpt_buf_), "%s = %.9g, %s = %.9g (tol %.3g)",  \
                          #a, kpt_a_, #b, kpt_b_, double(tol));                            \
            kpt::fail(__FILE__, __LINE__, kpt_buf_);                                      \
        }                                                                                  \
    } while (0)
