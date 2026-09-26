// Command-line parsing shared by main.cpp (silent / uninstall paths) and Gui.cpp (initial wizard
// defaults + building the argument string handed to an elevated relaunch of the GUI).
#pragma once

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <cwctype>
#include <cwchar>

namespace kick {

struct ParsedArgs {
    bool silent = false;
    bool uninstall = false;
    bool noReg = false;
    bool elevatedGui = false; // internal: "I am the elevated relaunch of the GUI, resume at Install"

    bool hasComponents = false; // /COMPONENTS= was given at all
    bool vst3 = true, vst2 = true, clap = true;

    std::wstring vst3Dir, vst2Dir, clapDir; // empty => default
    std::wstring manifestPath;              // empty => default
};

inline bool IStartsWith(const std::wstring& s, const std::wstring& prefix) {
    if (s.size() < prefix.size()) return false;
    return _wcsnicmp(s.c_str(), prefix.c_str(), prefix.size()) == 0;
}

inline std::wstring StripQuotes(std::wstring s) {
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') return s.substr(1, s.size() - 2);
    return s;
}

inline std::wstring ToLowerCopyW(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

inline ParsedArgs ParseArgs() {
    ParsedArgs a;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (_wcsicmp(arg.c_str(), L"/S") == 0) a.silent = true;
        else if (_wcsicmp(arg.c_str(), L"/uninstall") == 0) a.uninstall = true;
        else if (_wcsicmp(arg.c_str(), L"/NOREG") == 0) a.noReg = true;
        else if (_wcsicmp(arg.c_str(), L"/ELEVATEDGUI") == 0) a.elevatedGui = true;
        else if (IStartsWith(arg, L"/VST3DIR=")) a.vst3Dir = StripQuotes(arg.substr(9));
        else if (IStartsWith(arg, L"/VST2DIR=")) a.vst2Dir = StripQuotes(arg.substr(9));
        else if (IStartsWith(arg, L"/CLAPDIR=")) a.clapDir = StripQuotes(arg.substr(9));
        else if (IStartsWith(arg, L"/MANIFEST=")) a.manifestPath = StripQuotes(arg.substr(10));
        else if (IStartsWith(arg, L"/COMPONENTS=")) {
            a.hasComponents = true;
            a.vst3 = a.vst2 = a.clap = false;
            std::wstring list = arg.substr(12);
            size_t pos = 0;
            while (pos <= list.size()) {
                size_t comma = list.find(L',', pos);
                std::wstring tok = (comma == std::wstring::npos) ? list.substr(pos) : list.substr(pos, comma - pos);
                std::wstring lower = ToLowerCopyW(tok);
                if (lower == L"vst3") a.vst3 = true;
                else if (lower == L"vst2") a.vst2 = true;
                else if (lower == L"clap") a.clap = true;
                if (comma == std::wstring::npos) break;
                pos = comma + 1;
            }
        }
    }
    if (argv) LocalFree(argv);
    return a;
}

// True if this process's own file is named "Uninstall Kickarse.exe" (how the copy left behind
// in %ProgramFiles%\Kickarse is meant to be launched, in addition to /uninstall).
inline bool IsRunningAsUninstallerName() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p = path;
    size_t pos = p.find_last_of(L"\\/");
    std::wstring base = (pos == std::wstring::npos) ? p : p.substr(pos + 1);
    return _wcsicmp(base.c_str(), L"Uninstall Kickarse.exe") == 0;
}

inline bool IsProcessElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    bool result = false;
    if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
        result = elevation.TokenIsElevated != 0;
    }
    CloseHandle(token);
    return result;
}

// Relaunches this same exe elevated ("runas") with `args` as its command line. If `waitForIt` is
// true, blocks until the child exits and returns its exit code; otherwise returns 0 immediately
// after a successful launch. Returns -1 if the elevation request itself failed or was declined
// (e.g. the user clicked "No" on the UAC prompt), and sets *winErrorOut if given.
inline int RelaunchElevated(const std::wstring& args, bool waitForIt, DWORD* winErrorOut = nullptr) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = args.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        if (winErrorOut) *winErrorOut = GetLastError();
        return -1;
    }
    if (!waitForIt || !sei.hProcess) {
        if (sei.hProcess) CloseHandle(sei.hProcess);
        return 0;
    }
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return (int)code;
}

// Rebuilds "argv[1..]" from the current process's own command line, re-quoting as needed --
// used to forward a silent-mode invocation verbatim to an elevated relaunch of ourselves.
inline std::wstring RebuildArgsExcludingProgram() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring out;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) out += L" ";
        std::wstring a = argv[i];
        bool needQuote = a.find(L' ') != std::wstring::npos && !(a.size() >= 2 && a.front() == L'"');
        out += needQuote ? (L"\"" + a + L"\"") : a;
    }
    if (argv) LocalFree(argv);
    return out;
}

} // namespace kick
