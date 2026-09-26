// Kickarse — minimal self-registering test harness.
#pragma once

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace kt {

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
void note(const char* fmt, ...);      // informational line in the log
bool isReleaseBuild();
std::string renderDir();              // %LOCALAPPDATA%\KickarseBuild\dsp\renders (created on demand)

} // namespace kt

#define KT_CAT2(a, b) a##b
#define KT_CAT(a, b) KT_CAT2(a, b)

#define TEST_CASE(name)                                                   \
    static void name();                                                   \
    static const kt::Registrar KT_CAT(name, _registrar)(#name, &name);    \
    static void name()

#define CHECK(cond)                                   \
    do {                                              \
        ++kt::g_checks;                               \
        if (!(cond))                                  \
            kt::fail(__FILE__, __LINE__, #cond);      \
    } while (0)

#define CHECK_MSG(cond, ...)                                  \
    do {                                                      \
        ++kt::g_checks;                                       \
        if (!(cond)) {                                        \
            char kt_buf_[512];                                \
            std::snprintf(kt_buf_, sizeof(kt_buf_), __VA_ARGS__); \
            kt::fail(__FILE__, __LINE__, kt_buf_);            \
        }                                                     \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                             \
    do {                                                                                  \
        ++kt::g_checks;                                                                   \
        const double kt_a_ = double(a), kt_b_ = double(b);                                \
        if (!(std::fabs(kt_a_ - kt_b_) <= double(tol))) {                                 \
            char kt_buf_[512];                                                            \
            std::snprintf(kt_buf_, sizeof(kt_buf_), "%s = %.9g, %s = %.9g (tol %.3g)", #a, \
                          kt_a_, #b, kt_b_, double(tol));                                  \
            kt::fail(__FILE__, __LINE__, kt_buf_);                                        \
        }                                                                                 \
    } while (0)
