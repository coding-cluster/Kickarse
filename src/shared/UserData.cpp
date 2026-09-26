// Kickarse — UserData implementation (see UserData.h for the contract).
#include "UserData.h"

#include <fstream>
#include <sstream>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <shlobj.h>
#endif

namespace kick {

namespace {

std::string g_rootOverride;

#if defined(_WIN32)

std::wstring utf8ToWide(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    if (n <= 0)
        return {};
    std::wstring w(size_t(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string wideToUtf8(const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};
    std::string s(size_t(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string documentsRoot()
{
    PWSTR wpath = nullptr;
    std::string result;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &wpath)) && wpath != nullptr)
        result = wideToUtf8(wpath);
    if (wpath != nullptr)
        ::CoTaskMemFree(wpath);
    if (result.empty())
        result = "C:\\"; // pathological fallback; ensureDir() will simply fail if unusable
    if (!result.empty() && result.back() == '\\')
        result.pop_back();
    return result + "\\Kickarse";
}

#else // non-Windows dev/test builds only; the shipping plugin is Windows-only (see SPEC.md)

std::string documentsRoot()
{
    return "./Kickarse";
}

#endif

} // namespace

std::string UserData::root()
{
    return g_rootOverride.empty() ? documentsRoot() : g_rootOverride;
}

std::string UserData::presetsDir()
{
    return toUtf8(toPath(root()) / "Presets");
}

std::string UserData::presetsDir(const std::string& category)
{
    return toUtf8(toPath(root()) / "Presets" / toPath(category));
}

std::string UserData::shapesDir()
{
    return toUtf8(toPath(root()) / "Shapes");
}

std::string UserData::favouritesFile()
{
    return toUtf8(toPath(root()) / "favourites.txt");
}

void UserData::setRootOverride(const std::string& utf8Path)
{
    g_rootOverride = utf8Path;
}

std::string UserData::rootOverride()
{
    return g_rootOverride;
}

bool UserData::ensureDir(const std::string& utf8Path)
{
    const std::filesystem::path p = toPath(utf8Path);
    std::error_code ec;
    if (std::filesystem::exists(p, ec))
        return std::filesystem::is_directory(p, ec);
    std::filesystem::create_directories(p, ec);
    return !ec && std::filesystem::exists(p, ec);
}

std::filesystem::path UserData::toPath(const std::string& utf8)
{
#if defined(_WIN32)
    return std::filesystem::path(utf8ToWide(utf8));
#else
    return std::filesystem::path(utf8);
#endif
}

std::string UserData::toUtf8(const std::filesystem::path& path)
{
#if defined(_WIN32)
    return wideToUtf8(path.wstring());
#else
    return path.string();
#endif
}

bool UserData::readFile(const std::string& utf8Path, std::string& outText)
{
    std::ifstream in(toPath(utf8Path), std::ios::binary);
    if (!in)
        return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad())
        return false;
    outText = ss.str();
    return true;
}

bool UserData::writeFile(const std::string& utf8Path, const std::string& text)
{
    std::ofstream out(toPath(utf8Path), std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(text.data(), std::streamsize(text.size()));
    return bool(out);
}

} // namespace kick
