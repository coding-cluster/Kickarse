#include "InstallLogic.h"
#include "Payload.h"

#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>

#include <algorithm>
#include <cwctype>
#include <cstring>
#include <vector>

#ifndef KICKARSE_VERSION_STR_W
#define KICKARSE_VERSION_STR_W L"1.0.0"
#endif

namespace kick {

namespace {

// ------------------------------------------------------------------- strings

std::wstring ToLowerCopy(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

std::wstring Utf8ToWideStd(const std::string& s) {
    if (s.empty()) return std::wstring();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(wlen, L'\0');
    if (wlen > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], wlen);
    return out;
}

std::string WideToUtf8Std(const std::wstring& s) {
    if (s.empty()) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len, nullptr, nullptr);
    return out;
}

std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

std::wstring ParentDir(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return std::wstring();
    return path.substr(0, pos);
}

bool PathStartsWithDir(const std::wstring& path, const std::wstring& dir) {
    if (dir.empty() || path.size() < dir.size()) return false;
    if (_wcsnicmp(path.c_str(), dir.c_str(), dir.size()) != 0) return false;
    if (path.size() == dir.size()) return true;
    wchar_t c = path[dir.size()];
    return c == L'\\' || c == L'/';
}

// --------------------------------------------------------------- filesystem

bool CreateDirRecursive(const std::wstring& path) {
    if (path.empty()) return true;
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    std::wstring parent = ParentDir(path);
    if (!parent.empty() && parent.size() > 2 /* not just "C:" */) {
        if (!CreateDirRecursive(parent)) return false;
    }
    if (!CreateDirectoryW(path.c_str(), nullptr)) {
        DWORD e = GetLastError();
        if (e != ERROR_ALREADY_EXISTS) return false;
    }
    return true;
}

// Deletes everything inside `dir` (recursively) except a single file matching `exceptPathLower`
// (already lower-cased, or empty for "no exception"), then removes `dir` itself. Removal of the
// directory is allowed to fail silently when an exception was in play (that's expected: the
// excluded file, or something still open, is still there) but is reported via lockedOut when no
// exception was given at all -- that always means something else, e.g. a locked plugin binary.
bool DeleteDirRecursiveInternal(const std::wstring& dir, const std::wstring& exceptPathLower, std::wstring& lockedOut) {
    std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return true; // already gone
        lockedOut = dir;
        return false;
    }
    bool ok = true;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring full = dir + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!DeleteDirRecursiveInternal(full, exceptPathLower, lockedOut)) { ok = false; break; }
        } else {
            if (!exceptPathLower.empty() && ToLowerCopy(full) == exceptPathLower) {
                continue; // deliberately left behind
            }
            SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL);
            if (!DeleteFileW(full.c_str())) { lockedOut = full; ok = false; break; }
        }
    } while (ok && FindNextFileW(h, &fd));
    FindClose(h);
    if (ok) {
        if (!RemoveDirectoryW(dir.c_str())) {
            DWORD e = GetLastError();
            if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) {
                // fine
            } else if (!exceptPathLower.empty()) {
                // expected -- the excepted file (or something under a locked descendant) remains
            } else {
                lockedOut = dir;
                ok = false;
            }
        }
    }
    return ok;
}

bool DeleteDirRecursive(const std::wstring& dir, std::wstring& lockedOut) {
    return DeleteDirRecursiveInternal(dir, L"", lockedOut);
}

bool DeleteDirRecursiveExcept(const std::wstring& dir, const std::wstring& exceptPath, std::wstring& lockedOut) {
    return DeleteDirRecursiveInternal(dir, exceptPath.empty() ? L"" : ToLowerCopy(exceptPath), lockedOut);
}

bool DeleteFileIfExists(const std::wstring& path, std::wstring& lockedOut) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return true;
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (!DeleteFileW(path.c_str())) { lockedOut = path; return false; }
    return true;
}

bool IsSharingViolation(DWORD e) {
    return e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED || e == ERROR_LOCK_VIOLATION;
}

std::wstring FileWriteErrorMessage(const std::wstring& path, DWORD err) {
    if (IsSharingViolation(err)) {
        return L"Setup could not write \"" + path +
               L"\" because it is in use by another program.\r\n\r\n"
               L"Please close your DAW (or anything else that has Kickarse loaded) and try again.";
    }
    wchar_t buf[64];
    swprintf_s(buf, L"%lu", (unsigned long)err);
    return L"Could not write \"" + path + L"\" (Windows error " + buf + L").";
}

std::wstring LockedFileMessage(const std::wstring& path) {
    return L"Setup could not remove \"" + path +
           L"\" because it is in use by another program.\r\n\r\n"
           L"Please close your DAW (or anything else that has Kickarse loaded) and try again.";
}

bool WriteFileBytes(const std::wstring& path, const uint8_t* data, size_t size, DWORD& errOut) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0 /* exclusive: fail clearly if locked */,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { errOut = GetLastError(); return false; }
    bool ok = true;
    size_t remaining = size;
    const uint8_t* p = data;
    while (remaining > 0 && ok) {
        DWORD chunk = (DWORD)std::min<size_t>(remaining, 1u << 20);
        DWORD written = 0;
        if (!WriteFile(h, p, chunk, &written, nullptr) || written != chunk) { errOut = GetLastError(); ok = false; break; }
        p += chunk;
        remaining -= chunk;
    }
    CloseHandle(h);
    return ok;
}

// ----------------------------------------------------------------- registry

bool WriteUninstallRegistry(const std::wstring& appDir, const std::wstring& uninstallerExe, uint64_t estimatedKb) {
    HKEY hKey = nullptr;
    LONG r = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Kickarse",
        0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &hKey, nullptr);
    if (r != ERROR_SUCCESS || !hKey) return false;

    auto setStr = [&](const wchar_t* name, const std::wstring& val) {
        RegSetValueExW(hKey, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(val.c_str()),
                       (DWORD)((val.size() + 1) * sizeof(wchar_t)));
    };
    auto setDword = [&](const wchar_t* name, DWORD val) {
        RegSetValueExW(hKey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(val));
    };

    std::wstring quoted = L"\"" + uninstallerExe + L"\"";
    setStr(L"DisplayName", L"Kickarse");
    setStr(L"DisplayVersion", KICKARSE_VERSION_STR_W);
    setStr(L"Publisher", L"Kickarse");
    setStr(L"InstallLocation", appDir);
    setStr(L"UninstallString", quoted);
    setStr(L"QuietUninstallString", quoted + L" /uninstall /S");
    setStr(L"DisplayIcon", uninstallerExe);
    setDword(L"EstimatedSize", (DWORD)estimatedKb);
    setDword(L"NoModify", 1);
    setDword(L"NoRepair", 1);

    RegCloseKey(hKey);
    return true;
}

void RemoveUninstallRegistry() {
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Kickarse");
    // best effort: fine if it's absent (noReg installs never wrote one) or we lack rights here
}

// -------------------------------------------------------------- self-delete

void ScheduleSelfDeleteAndRemoveDir(const std::wstring& selfPath, const std::wstring& appDir) {
    wchar_t comspec[MAX_PATH];
    if (!GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH)) {
        wcscpy_s(comspec, L"C:\\Windows\\System32\\cmd.exe");
    }
    std::wstring cmdLine = L"\"" + std::wstring(comspec) +
        L"\" /C ping -n 2 127.0.0.1>nul & del /f /q \"" + selfPath + L"\" & rd \"" + appDir + L"\"";
    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        MoveFileExW(selfPath.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    }
}

// --------------------------------------------------------------- known folders

std::wstring KnownFolderPath(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p)) && p) {
        result = p;
    }
    if (p) CoTaskMemFree(p);
    return result;
}

// ----------------------------------------------------------- install manifest

struct ManifestLine {
    std::wstring type; // "DIR" | "FILE"
    std::wstring tag;  // "VST3" | "VST2" | "CLAP" | "APPDIR"
    std::wstring path;
};

bool ReadInstallManifest(const std::wstring& path, std::vector<ManifestLine>& out, std::wstring& err) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err = L"Install manifest not found: " + path +
              L"\r\n(pass /MANIFEST=<file> if this was installed with /NOREG)";
        return false;
    }
    DWORD size = GetFileSize(h, nullptr);
    std::string buf(size, '\0');
    DWORD readBytes = 0;
    BOOL ok = (size == 0) ? TRUE : ReadFile(h, &buf[0], size, &readBytes, nullptr);
    CloseHandle(h);
    if (!ok) { err = L"Could not read install manifest: " + path; return false; }

    std::wstring text = Utf8ToWideStd(buf);
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find(L'\n', pos);
        std::wstring line = (nl == std::wstring::npos) ? text.substr(pos) : text.substr(pos, nl - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (!line.empty() && line[0] != L'#') {
            size_t t1 = line.find(L'\t');
            size_t t2 = (t1 == std::wstring::npos) ? std::wstring::npos : line.find(L'\t', t1 + 1);
            if (t1 != std::wstring::npos && t2 != std::wstring::npos) {
                ManifestLine ml;
                ml.type = line.substr(0, t1);
                ml.tag = line.substr(t1 + 1, t2 - t1 - 1);
                ml.path = line.substr(t2 + 1);
                if (!ml.path.empty()) out.push_back(std::move(ml));
            }
        }
        if (nl == std::wstring::npos) break;
        pos = nl + 1;
    }
    return true;
}

bool WriteInstallManifest(const std::wstring& path, const InstalledPaths& p) {
    std::wstring text = L"# Kickarse install manifest v1\r\n";
    if (!p.vst3Bundle.empty()) text += L"DIR\tVST3\t" + p.vst3Bundle + L"\r\n";
    if (!p.vst2Dll.empty())    text += L"FILE\tVST2\t" + p.vst2Dll + L"\r\n";
    if (!p.clapFile.empty())   text += L"FILE\tCLAP\t" + p.clapFile + L"\r\n";
    if (!p.appDir.empty())     text += L"DIR\tAPPDIR\t" + p.appDir + L"\r\n";

    std::string utf8 = WideToUtf8Std(text);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(h);
    return ok && written == (DWORD)utf8.size();
}

} // namespace

// =========================================================== public interface

std::wstring GetDefaultVst3Dir() { return JoinPath(KnownFolderPath(FOLDERID_ProgramFilesCommon), L"VST3"); }
std::wstring GetDefaultVst2Dir() { return JoinPath(KnownFolderPath(FOLDERID_ProgramFilesCommon), L"VST2"); }
std::wstring GetDefaultClapDir() { return JoinPath(KnownFolderPath(FOLDERID_ProgramFilesCommon), L"CLAP"); }
std::wstring GetDefaultAppDir()  { return JoinPath(KnownFolderPath(FOLDERID_ProgramFiles), L"Kickarse"); }
std::wstring GetDefaultManifestPath() { return JoinPath(GetDefaultAppDir(), L"install-manifest.txt"); }
std::wstring GetDocumentsPresetsDir() {
    return JoinPath(JoinPath(KnownFolderPath(FOLDERID_Documents), L"Kickarse"), L"Presets");
}
std::wstring GetDocumentsShapesDir() {
    return JoinPath(JoinPath(KnownFolderPath(FOLDERID_Documents), L"Kickarse"), L"Shapes");
}

bool DirIsWritable(const std::wstring& dir) {
    if (!CreateDirRecursive(dir)) return false;
    std::wstring testFile = JoinPath(dir, L"~kktest.tmp");
    HANDLE h = CreateFileW(testFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    DeleteFileW(testFile.c_str());
    return true;
}

bool InstallNeedsAdmin(const InstallOptions& opts) {
    if (!opts.noReg) return true; // registry + %ProgramFiles%\Kickarse always need elevation
    if (opts.installVst3 && !DirIsWritable(opts.vst3Dir.empty() ? GetDefaultVst3Dir() : opts.vst3Dir)) return true;
    if (opts.installVst2 && !DirIsWritable(opts.vst2Dir.empty() ? GetDefaultVst2Dir() : opts.vst2Dir)) return true;
    if (opts.installClap && !DirIsWritable(opts.clapDir.empty() ? GetDefaultClapDir() : opts.clapDir)) return true;
    return false;
}

bool UninstallNeedsAdmin(const std::wstring& manifestPathIn) {
    std::wstring manifestPath = manifestPathIn.empty() ? GetDefaultManifestPath() : manifestPathIn;
    std::vector<ManifestLine> lines;
    std::wstring err;
    if (!ReadInstallManifest(manifestPath, lines, err)) return true; // unknown -> safest default
    for (auto& l : lines) {
        if (l.tag == L"APPDIR") return true; // implies HKLM + Program Files were used
        if (!DirIsWritable(ParentDir(l.path))) return true;
    }
    return false;
}

bool RunInstall(const InstallOptions& opts, const ProgressFn& progress, InstalledPaths& outPaths,
                 std::wstring& errorMessage) {
    outPaths = InstalledPaths{};
    try {
        std::wstring vst3Dir = opts.vst3Dir.empty() ? GetDefaultVst3Dir() : opts.vst3Dir;
        std::wstring vst2Dir = opts.vst2Dir.empty() ? GetDefaultVst2Dir() : opts.vst2Dir;
        std::wstring clapDir = opts.clapDir.empty() ? GetDefaultClapDir() : opts.clapDir;

        if (progress) progress(0, L"Reading payload...");
        std::vector<PayloadEntry> entries = LoadPayloadManifest();
        PayloadBlob blob = GetPayloadBlob();

        uint64_t totalBytes = 0;
        for (auto& e : entries) {
            if (e.category == PayloadCategory::VST3 && !opts.installVst3) continue;
            if (e.category == PayloadCategory::VST2 && !opts.installVst2) continue;
            if (e.category == PayloadCategory::CLAP && !opts.installClap) continue;
            if (e.category == PayloadCategory::DOC) continue; // negligible size, not worth tracking
            totalBytes += e.size;
        }
        if (totalBytes == 0) totalBytes = 1;
        uint64_t doneBytes = 0;

        auto report = [&](const std::wstring& label) {
            if (!progress) return;
            int pct = (int)((doneBytes * 90) / totalBytes);
            progress(pct, label);
        };

        // user data dirs -- always created, regardless of components/noReg; never touched by uninstall
        CreateDirRecursive(GetDocumentsPresetsDir());
        CreateDirRecursive(GetDocumentsShapesDir());

        if (opts.installVst3) {
            std::wstring destDir = JoinPath(vst3Dir, L"Kickarse.vst3");
            std::wstring locked;
            if (!DeleteDirRecursive(destDir, locked)) {
                errorMessage = LockedFileMessage(locked.empty() ? destDir : locked);
                return false;
            }
            for (auto& e : entries) {
                if (e.category != PayloadCategory::VST3) continue;
                std::wstring dest = JoinPath(destDir, e.relPath);
                if (!CreateDirRecursive(ParentDir(dest))) {
                    errorMessage = L"Could not create folder for \"" + dest + L"\".";
                    return false;
                }
                report(L"Installing " + e.relPath + L"...");
                DWORD werr = 0;
                if (!WriteFileBytes(dest, blob.data + e.offset, (size_t)e.size, werr)) {
                    errorMessage = FileWriteErrorMessage(dest, werr);
                    return false;
                }
                doneBytes += e.size;
            }
            outPaths.vst3Bundle = destDir;
        }

        if (opts.installVst2) {
            if (!CreateDirRecursive(vst2Dir)) {
                errorMessage = L"Could not create folder \"" + vst2Dir + L"\".";
                return false;
            }
            std::wstring dest = JoinPath(vst2Dir, L"Kickarse.dll");
            const PayloadEntry* found = nullptr;
            for (auto& e : entries) if (e.category == PayloadCategory::VST2) { found = &e; break; }
            if (!found) { errorMessage = L"Installer payload is missing the VST2 plug-in."; return false; }
            report(L"Installing Kickarse.dll...");
            DWORD werr = 0;
            if (!WriteFileBytes(dest, blob.data + found->offset, (size_t)found->size, werr)) {
                errorMessage = FileWriteErrorMessage(dest, werr);
                return false;
            }
            doneBytes += found->size;
            outPaths.vst2Dll = dest;
        }

        if (opts.installClap) {
            if (!CreateDirRecursive(clapDir)) {
                errorMessage = L"Could not create folder \"" + clapDir + L"\".";
                return false;
            }
            std::wstring dest = JoinPath(clapDir, L"Kickarse.clap");
            const PayloadEntry* found = nullptr;
            for (auto& e : entries) if (e.category == PayloadCategory::CLAP) { found = &e; break; }
            if (!found) { errorMessage = L"Installer payload is missing the CLAP plug-in."; return false; }
            report(L"Installing Kickarse.clap...");
            DWORD werr = 0;
            if (!WriteFileBytes(dest, blob.data + found->offset, (size_t)found->size, werr)) {
                errorMessage = FileWriteErrorMessage(dest, werr);
                return false;
            }
            doneBytes += found->size;
            outPaths.clapFile = dest;
        }

        if (progress) progress(92, L"Finishing up...");

        if (!opts.noReg) {
            std::wstring appDir = GetDefaultAppDir();
            if (!CreateDirRecursive(appDir)) {
                errorMessage = L"Could not create folder \"" + appDir + L"\".";
                return false;
            }
            outPaths.appDir = appDir;

            const PayloadEntry* doc = nullptr;
            for (auto& e : entries) if (e.category == PayloadCategory::DOC) { doc = &e; break; }
            if (doc) {
                std::wstring dest = JoinPath(appDir, doc->relPath);
                DWORD werr = 0;
                if (WriteFileBytes(dest, blob.data + doc->offset, (size_t)doc->size, werr)) {
                    outPaths.docFile = dest;
                }
                // non-fatal if this one fails
            }

            wchar_t selfPath[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
            std::wstring uninstDest = JoinPath(appDir, L"Uninstall Kickarse.exe");
            if (CopyFileW(selfPath, uninstDest.c_str(), FALSE)) {
                outPaths.uninstallerExe = uninstDest;
            } else {
                errorMessage = FileWriteErrorMessage(uninstDest, GetLastError());
                return false;
            }

            uint64_t estimatedKb = totalBytes / 1024;
            if (!WriteUninstallRegistry(appDir, uninstDest, estimatedKb)) {
                errorMessage = L"Could not write the uninstall registry entry "
                                L"(HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Kickarse). "
                                L"Setup may not be running with administrator rights.";
                return false;
            }
        }

        std::wstring manifestPath = opts.manifestPathOverride;
        if (manifestPath.empty()) {
            if (opts.noReg) {
                wchar_t tempDir[MAX_PATH] = {};
                GetTempPathW(MAX_PATH, tempDir);
                manifestPath = JoinPath(tempDir, L"KickarseInstallManifest.txt");
            } else {
                manifestPath = GetDefaultManifestPath();
            }
        }
        if (!CreateDirRecursive(ParentDir(manifestPath))) {
            errorMessage = L"Could not create folder for the install manifest \"" + manifestPath + L"\".";
            return false;
        }
        if (!WriteInstallManifest(manifestPath, outPaths)) {
            errorMessage = L"Could not write install manifest \"" + manifestPath + L"\".";
            return false;
        }
        outPaths.manifestPath = manifestPath;

        if (progress) progress(100, L"Done.");
        return true;
    } catch (const std::exception& ex) {
        errorMessage = Utf8ToWideStd(std::string(ex.what()));
        return false;
    }
}

bool RunUninstall(const std::wstring& manifestPathIn, std::wstring& errorMessage) {
    std::wstring manifestPath = manifestPathIn.empty() ? GetDefaultManifestPath() : manifestPathIn;
    std::vector<ManifestLine> lines;
    if (!ReadInstallManifest(manifestPath, lines, errorMessage)) return false;

    std::wstring appDir;
    for (auto& l : lines) {
        if (l.tag == L"APPDIR") appDir = l.path;
    }

    wchar_t selfPathBuf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, selfPathBuf, MAX_PATH);
    std::wstring selfPath = selfPathBuf;

    for (auto& l : lines) {
        if (l.tag == L"APPDIR") continue; // handled last, specially (may contain our own running exe)
        std::wstring locked;
        bool ok = (l.type == L"DIR") ? DeleteDirRecursive(l.path, locked) : DeleteFileIfExists(l.path, locked);
        if (!ok) {
            errorMessage = LockedFileMessage(locked.empty() ? l.path : locked);
            return false;
        }
    }

    RemoveUninstallRegistry(); // best effort; requires admin, harmless if absent/denied

    if (!appDir.empty()) {
        bool selfIsInAppDir = PathStartsWithDir(selfPath, appDir);
        std::wstring locked;
        if (!DeleteDirRecursiveExcept(appDir, selfIsInAppDir ? selfPath : std::wstring(), locked)) {
            errorMessage = LockedFileMessage(locked.empty() ? appDir : locked);
            return false;
        }
        if (selfIsInAppDir) {
            ScheduleSelfDeleteAndRemoveDir(selfPath, appDir);
        }
    }

    DeleteFileW(manifestPath.c_str()); // best effort
    return true;
}

} // namespace kick
