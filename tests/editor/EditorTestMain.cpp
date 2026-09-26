// Kickarse — editor-model test runner entry point and shared helpers.
#include <chrono>
#include <cstdarg>
#include <cstring>

#include "EditorTest.h"

namespace et {

int g_checks   = 0;
int g_failures = 0;

namespace {
const char* g_current         = "";
int         g_currentFailures = 0;
} // namespace

std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

void fail(const char* file, int line, const std::string& message)
{
    ++g_failures;
    ++g_currentFailures;
    if (g_currentFailures <= 8) {
        const char* base = std::strrchr(file, '\\');
        if (!base)
            base = std::strrchr(file, '/');
        std::printf("    FAIL %s:%d  %s\n", base ? base + 1 : file, line, message.c_str());
    } else if (g_currentFailures == 9) {
        std::printf("    ... further failures in %s suppressed\n", g_current);
    }
}

void note(const char* fmt, ...)
{
    std::printf("    ");
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::printf("\n");
}

Envelope makeEnv(const std::vector<EnvNode>& nodes)
{
    Envelope e;
    if (!ops::fromList(nodes, e))
        e = Envelope::flat();
    return e;
}

std::string describe(const Envelope& e)
{
    std::string s = e.serialize();
    if (s.size() > 400)
        s = s.substr(0, 400) + "...";
    return s;
}

bool validEnvelope(const Envelope& e, const char* where)
{
    std::string why;
    bool ok = ops::isValid(e, &why);
    if (ok) {
        for (int i = 0; i < e.size(); ++i) {
            if (e.node(i).flags & ops::kTagMask) {
                ok  = false;
                why = "editor tag bits leaked";
            }
        }
    }
    ++g_checks;
    if (!ok) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%s: invalid envelope (%s): %s", where, why.c_str(), describe(e).c_str());
        fail(__FILE__, __LINE__, buf);
    }
    return ok;
}

Fixture::Fixture()
{
    model.setClock([this] { return clock; });
    model.setListener(&listener);
    setSnap(false);
    setTiming(0.f, 0.f);
    model.setLinked(false);
    listener.clear();
}

void Fixture::setTiming(float rotateDeg, float swingPct, int gridIndex, int rateIndex)
{
    TimingParams t;
    t.rotateDeg    = rotateDeg;
    t.swingPercent = swingPct;
    t.gridIndex    = gridIndex;
    t.rateIndex    = rateIndex;
    t.timeMode     = kick::kTimeSync;
    model.setTiming(t);
}

void Fixture::setSnap(bool enabled, bool snapY, float magnet)
{
    SnapSettings s;
    s.enabled = enabled;
    s.snapY   = snapY;
    s.yMagnet = magnet;
    model.setSnap(s);
}

} // namespace et

int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    const auto  start  = std::chrono::steady_clock::now();
    int run = 0, failedTests = 0;
    for (const et::TestCase& t : et::registry()) {
        if (filter && !std::strstr(t.name, filter))
            continue;
        et::g_current         = t.name;
        et::g_currentFailures = 0;
        const auto t0 = std::chrono::steady_clock::now();
        t.fn();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("%-4s %-44s %8.1f ms\n", et::g_currentFailures ? "FAIL" : "ok", t.name, ms);
        ++run;
        if (et::g_currentFailures)
            ++failedTests;
    }
    const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("\n%d tests, %d checks, %d failed checks in %d tests (%.2f s)\n", run, et::g_checks,
                et::g_failures, failedTests, total);
    return et::g_failures == 0 ? 0 : 1;
}
