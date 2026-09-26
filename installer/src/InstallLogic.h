// Core install / uninstall engine. Shared by the silent (/S) code path and the GUI wizard's
// worker thread -- neither one duplicates file-system logic.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace kick {

struct InstallOptions {
    bool installVst3 = true;
    bool installVst2 = true;
    bool installClap = true;

    // Fixed by default (resolved via SHGetKnownFolderPath); overridable for testing/automation.
    std::wstring vst3Dir;              // parent dir; bundle goes to <vst3Dir>\Kickarse.vst3
    std::wstring vst2Dir;              // dll goes to <vst2Dir>\Kickarse.dll
    std::wstring clapDir;              // file goes to <clapDir>\Kickarse.clap

    bool noReg = false;                // skip HKLM + uninstaller copy to %ProgramFiles%
    std::wstring manifestPathOverride; // empty => default location
};

// What actually got written, for the Finish page / manifest bookkeeping.
struct InstalledPaths {
    std::wstring vst3Bundle;   // empty if not installed
    std::wstring vst2Dll;
    std::wstring clapFile;
    std::wstring docFile;
    std::wstring appDir;       // %ProgramFiles%\Kickarse, empty if noReg
    std::wstring uninstallerExe;
    std::wstring manifestPath;
};

using ProgressFn = std::function<void(int percent, const std::wstring& status)>;

// Default target locations (64-bit known folders).
std::wstring GetDefaultVst3Dir();
std::wstring GetDefaultVst2Dir();
std::wstring GetDefaultClapDir();
std::wstring GetDefaultAppDir();                       // %ProgramFiles%\Kickarse
std::wstring GetDefaultManifestPath();                 // %ProgramFiles%\Kickarse\install-manifest.txt
std::wstring GetDocumentsPresetsDir();                 // %USERPROFILE%\Documents\Kickarse\Presets
std::wstring GetDocumentsShapesDir();                  // %USERPROFILE%\Documents\Kickarse\Shapes

// True if we can create/write into `dir` without elevation (creates it as a side effect if it
// didn't exist and that succeeded -- callers that just want to *test* should be aware of that).
bool DirIsWritable(const std::wstring& dir);

// Whether performing `opts` as specified would require administrator rights.
bool InstallNeedsAdmin(const InstallOptions& opts);

// Whether undoing whatever `manifestPath` describes needs administrator rights.
bool UninstallNeedsAdmin(const std::wstring& manifestPath);

// Runs (or re-runs, after the caller fixes a sharing violation) the install. Returns true on
// success. On failure, errorMessage is a user-presentable string (e.g. naming the locked file).
bool RunInstall(const InstallOptions& opts, const ProgressFn& progress, InstalledPaths& outPaths,
                 std::wstring& errorMessage);

// Reads the manifest written by RunInstall and removes exactly what it lists (never touches
// Documents\Kickarse\Presets or \Shapes -- those are simply never recorded in the manifest).
// If the manifest's own install used the "copy of myself in Program Files" pattern, this also
// arranges for that copy to delete itself after this call returns (see SelfDeleteHelper).
bool RunUninstall(const std::wstring& manifestPath, std::wstring& errorMessage);

} // namespace kick
