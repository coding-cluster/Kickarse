// Kickarse-Setup.exe entry point: decides uninstall vs install and silent vs GUI, then hands off
// to InstallLogic.cpp (the actual file/registry work) or Gui.cpp (the wizard). Elevation for the
// *silent* path is decided here; the GUI's own elevation decision happens in Gui.cpp right before
// the Install page starts writing anything (see requirement: elevate only when actually needed).
#include "Args.h"
#include "InstallLogic.h"
#include "Gui.h"

#include <windows.h>
#include <cstdio>

HINSTANCE g_hInst = nullptr;

namespace kick {
namespace {

int RunUninstallFlow(const ParsedArgs& args) {
    if (!args.silent) {
        int r = MessageBoxW(nullptr,
            L"This will remove Kickarse (VST3 / VST2 / CLAP) from this computer.\r\n\r\n"
            L"Your presets and shapes in Documents\\Kickarse will be kept.\r\n\r\nContinue?",
            L"Uninstall Kickarse", MB_YESNO | MB_ICONQUESTION);
        if (r != IDYES) return 1;
    }

    bool ok;
    std::wstring err;
    bool needsAdmin = UninstallNeedsAdmin(args.manifestPath);
    if (needsAdmin && !IsProcessElevated()) {
        std::wstring childArgs = L"/uninstall /S";
        if (!args.manifestPath.empty()) childArgs += L" /MANIFEST=\"" + args.manifestPath + L"\"";
        DWORD werr = 0;
        int code = RelaunchElevated(childArgs, /*waitForIt=*/true, &werr);
        if (code < 0) {
            ok = false;
            err = L"Administrator rights are required to uninstall Kickarse, and the elevation "
                  L"request was cancelled or failed.";
        } else {
            ok = (code == 0);
            if (!ok) err = L"Uninstall failed.";
        }
    } else {
        ok = RunUninstall(args.manifestPath, err);
    }

    if (!args.silent) {
        MessageBoxW(nullptr, ok ? L"Kickarse has been uninstalled." : err.c_str(), L"Uninstall Kickarse",
                    ok ? (MB_OK | MB_ICONINFORMATION) : (MB_OK | MB_ICONERROR));
    } else if (!ok) {
        fwprintf(stderr, L"Kickarse Uninstall: %s\n", err.c_str());
    }
    return ok ? 0 : 1;
}

int RunSilentInstallFlow(const ParsedArgs& args) {
    InstallOptions opts;
    opts.installVst3 = args.vst3;
    opts.installVst2 = args.vst2;
    opts.installClap = args.clap;
    opts.vst3Dir = args.vst3Dir;
    opts.vst2Dir = args.vst2Dir;
    opts.clapDir = args.clapDir;
    opts.noReg = args.noReg;
    opts.manifestPathOverride = args.manifestPath;

    if (InstallNeedsAdmin(opts) && !IsProcessElevated()) {
        std::wstring childArgs = RebuildArgsExcludingProgram(); // /S and everything else, verbatim
        DWORD werr = 0;
        int code = RelaunchElevated(childArgs, /*waitForIt=*/true, &werr);
        if (code < 0) {
            fwprintf(stderr, L"Kickarse Setup: administrator rights are required and the elevation "
                              L"request was cancelled or failed.\n");
            return 1;
        }
        return code;
    }

    InstalledPaths paths;
    std::wstring err;
    bool ok = RunInstall(opts, nullptr, paths, err);
    if (!ok) {
        fwprintf(stderr, L"Kickarse Setup: %s\n", err.c_str());
        return 1;
    }
    return 0;
}

} // namespace
} // namespace kick

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    g_hInst = hInstance;
    kick::ParsedArgs args = kick::ParseArgs();
    bool uninstallMode = args.uninstall || kick::IsRunningAsUninstallerName();

    if (uninstallMode) {
        return kick::RunUninstallFlow(args);
    }
    if (args.silent) {
        return kick::RunSilentInstallFlow(args);
    }
    return kick::RunInstallWizard(args);
}
