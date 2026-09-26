// Kickarse — test runner entry point.
//   kickarse_tests              run everything (tests, benchmarks, WAV renders)
//   kickarse_tests <substring>  run only the tests whose name contains <substring>
#include <chrono>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "TestHarness.h"

namespace kt {

int g_checks   = 0;
int g_failures = 0;

namespace {
const char* g_current = "";
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

bool isReleaseBuild()
{
#ifdef NDEBUG
    return true;
#else
    return false;
#endif
}

std::string renderDir()
{
    std::filesystem::path base;
    if (const char* local = std::getenv("LOCALAPPDATA"))
        base = std::filesystem::path(local) / "KickarseBuild" / "dsp" / "renders";
    else
        base = std::filesystem::temp_directory_path() / "KickarseBuild" / "dsp" / "renders";
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    return base.string();
}

} // namespace kt

int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0, failedTests = 0;
    const auto t0 = std::chrono::steady_clock::now();
    std::printf("Kickarse DSP tests (%s build)\n", kt::isReleaseBuild() ? "Release" : "Debug");
    for (const kt::TestCase& tc : kt::registry()) {
        if (filter && !std::strstr(tc.name, filter))
            continue;
        kt::g_current         = tc.name;
        kt::g_currentFailures = 0;
        const auto s = std::chrono::steady_clock::now();
        tc.fn();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s).count();
        std::printf("[%s] %-40s %8.1f ms\n", kt::g_currentFailures ? "FAIL" : " ok ", tc.name, ms);
        std::fflush(stdout);
        ++ran;
        if (kt::g_currentFailures)
            ++failedTests;
    }
    const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\nSUMMARY: %d tests, %d passed, %d failed; %d checks, %d failed checks (%.1f s)\n", ran,
                ran - failedTests, failedTests, kt::g_checks, kt::g_failures, total);
    return failedTests == 0 ? 0 : 1;
}
