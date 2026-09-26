// Kickarse — preset/shape test runner entry point.
//   kickarse_preset_tests              run everything
//   kickarse_preset_tests <substring>  run only the tests whose name contains <substring>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "TestHarness.h"

namespace kpt {

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

// A fresh, isolated directory for a test that needs to touch the filesystem (PresetStore /
// ShapeLibrary constructed with this as their root), under %LOCALAPPDATA%\KickarseBuild\presets
// (never inside OneDrive, and never the real Documents\Kickarse). Wiped clean on every call so
// re-running the suite never sees a previous run's leftovers.
std::string scratchDir(const char* subdir)
{
    std::filesystem::path base;
    if (const char* local = std::getenv("LOCALAPPDATA"))
        base = std::filesystem::path(local) / "KickarseBuild" / "presets" / "scratch" / subdir;
    else
        base = std::filesystem::temp_directory_path() / "KickarseBuild" / "presets" / "scratch" / subdir;
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(base, ec);
    return base.string();
}

} // namespace kpt

int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0, failedTests = 0;
    const auto t0 = std::chrono::steady_clock::now();
#ifdef NDEBUG
    std::printf("Kickarse preset tests (Release build)\n");
#else
    std::printf("Kickarse preset tests (Debug build)\n");
#endif
    for (const kpt::TestCase& tc : kpt::registry()) {
        if (filter && !std::strstr(tc.name, filter))
            continue;
        kpt::g_current         = tc.name;
        kpt::g_currentFailures = 0;
        const auto s = std::chrono::steady_clock::now();
        tc.fn();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s).count();
        std::printf("[%s] %-40s %8.1f ms\n", kpt::g_currentFailures ? "FAIL" : " ok ", tc.name, ms);
        std::fflush(stdout);
        ++ran;
        if (kpt::g_currentFailures)
            ++failedTests;
    }
    const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\nSUMMARY: %d tests, %d passed, %d failed; %d checks, %d failed checks (%.1f s)\n", ran,
                ran - failedTests, failedTests, kpt::g_checks, kpt::g_failures, total);
    return failedTests == 0 ? 0 : 1;
}
