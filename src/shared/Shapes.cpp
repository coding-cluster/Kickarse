// Kickarse — ShapeLibrary implementation (see Shapes.h for the file format and API contract).
#include "Shapes.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string_view>
#include <system_error>

#include "FactoryData.h"
#include "UserData.h"

namespace kick {

namespace {

namespace fs = std::filesystem;

bool isSpaceCh(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string_view trim(std::string_view s) noexcept
{
    size_t b = 0, e = s.size();
    while (b < e && isSpaceCh(s[b]))
        ++b;
    while (e > b && isSpaceCh(s[e - 1]))
        --e;
    return s.substr(b, e - b);
}

bool iless(const std::string& a, const std::string& b) noexcept
{
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const int ca = std::tolower((unsigned char)a[i]);
        const int cb = std::tolower((unsigned char)b[i]);
        if (ca != cb)
            return ca < cb;
    }
    return a.size() < b.size();
}

bool iequals(const std::string& a, const char* b) noexcept
{
    size_t i = 0;
    for (; a[i] != '\0' && b[i] != '\0'; ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return a[i] == '\0' && b[i] == '\0';
}

struct ParsedShapeFile {
    std::string name;
    std::string category;
    Envelope    env; // Envelope's own default ctor: the classic duck; overwritten if env= parses
};

ParsedShapeFile parseShapeFile(const std::string& text)
{
    ParsedShapeFile out;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        const size_t end = (nl == std::string::npos) ? text.size() : nl;
        std::string_view line = trim(std::string_view(text).substr(pos, end - pos));
        if (nl == std::string::npos)
            pos = text.size() + 1;
        else
            pos = nl + 1;

        if (line.empty() || line.front() == '#')
            continue;
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos)
            continue;
        const std::string_view key = trim(line.substr(0, eq));
        std::string_view value = line.substr(eq + 1);
        for (size_t i = 1; i + 1 <= value.size(); ++i) {
            if (value[i] == '#' && isSpaceCh(value[i - 1])) {
                value = value.substr(0, i);
                break;
            }
        }
        value = trim(value);

        if (key == "version") {
            continue;
        } else if (key == "name") {
            out.name = std::string(value);
        } else if (key == "category") {
            out.category = std::string(value);
        } else if (key == "env") {
            Envelope probe;
            if (probe.deserialize(value))
                out.env = probe;
        }
    }
    return out;
}

std::string serializeShapeFile(const std::string& name, const std::string& category, const Envelope& env)
{
    std::string s;
    s += "# Kickarse shape\n";
    s += "version=1\n";
    s += "name=" + name + "\n";
    s += "category=" + category + "\n";
    s += "env=" + env.serialize() + "\n";
    return s;
}

std::string sanitiseFileBase(const std::string& name)
{
    std::string s;
    s.reserve(name.size());
    for (unsigned char c : name) {
        if (c < 0x20)
            continue;
        switch (c) {
            case '<': case '>': case ':': case '"': case '/': case '\\': case '|': case '?': case '*':
                s += '_';
                break;
            default:
                s += char(c);
        }
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '.'))
        s.pop_back();
    size_t start = 0;
    while (start < s.size() && s[start] == ' ')
        ++start;
    s.erase(0, start);

    constexpr size_t kMaxBytes = 100;
    if (s.size() > kMaxBytes) {
        size_t cut = kMaxBytes;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
            --cut;
        s.resize(cut);
    }
    if (s.empty())
        s = "Shape";

    static const char* kReserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5",
        "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (const char* r : kReserved) {
        if (iequals(s, r)) {
            s += '_';
            break;
        }
    }
    return s;
}

std::string uniquePath(const fs::path& dir, const std::string& base, const char* ext)
{
    std::error_code ec;
    fs::path candidate = dir / (base + ext);
    if (!fs::exists(candidate, ec))
        return UserData::toUtf8(candidate);
    for (int n = 2; n < 1000; ++n) {
        candidate = dir / (base + " (" + std::to_string(n) + ")" + ext);
        if (!fs::exists(candidate, ec))
            return UserData::toUtf8(candidate);
    }
    return UserData::toUtf8(candidate);
}

bool startsWith(const std::string& s, const char* prefix) noexcept
{
    const size_t n = std::char_traits<char>::length(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

int categoryIndex(const std::string& category) noexcept
{
    for (int i = 0; i < kNumShapeCategories; ++i)
        if (category == kShapeCategories[i])
            return i;
    return -1;
}

} // namespace

ShapeLibrary::ShapeLibrary(std::string userRoot)
    : root_(userRoot.empty() ? UserData::root() : std::move(userRoot))
{
}

std::string ShapeLibrary::shapesDir() const
{
    return UserData::toUtf8(UserData::toPath(root_) / "Shapes");
}

void ShapeLibrary::rescan()
{
    try {
        rescanFactory();
        rescanUser();
    } catch (...) {
        user_.clear();
    }
}

void ShapeLibrary::rescanFactory()
{
    for (auto& v : factory_)
        v.clear();
    for (int i = 0; i < factory::kShapeCount; ++i) {
        const factory::FactoryFile& f = factory::kShapes[i];
        const int catIdx = categoryIndex(f.category);
        if (catIdx < 0 || catIdx >= 3) // factory shapes only live in the first 3 categories
            continue;
        const ParsedShapeFile parsed = parseShapeFile(f.text);
        ShapeEntry e;
        e.category  = f.category;
        e.name      = f.name;
        e.isFactory = true;
        e.id        = "factory:" + e.category + "/" + e.name;
        e.env       = parsed.env;
        factory_[size_t(catIdx)].push_back(std::move(e));
    }
}

void ShapeLibrary::rescanUser()
{
    user_.clear();
    const fs::path base = UserData::toPath(shapesDir());
    std::error_code ec;
    if (!fs::exists(base, ec) || ec)
        return;

    std::error_code fileEc;
    for (fs::directory_iterator it(base, fileEc); !fileEc && it != fs::directory_iterator(); it.increment(fileEc)) {
        const fs::directory_entry& entry = *it;
        std::error_code isFileEc;
        if (!entry.is_regular_file(isFileEc) || isFileEc)
            continue;
        std::string ext = entry.path().extension().string();
        for (char& c : ext)
            c = char(std::tolower((unsigned char)c));
        if (ext != ".kkshape")
            continue;

        std::string text;
        if (!UserData::readFile(UserData::toUtf8(entry.path()), text))
            continue;
        const ParsedShapeFile parsed = parseShapeFile(text);

        ShapeEntry e;
        e.name      = parsed.name.empty() ? UserData::toUtf8(entry.path().stem()) : parsed.name;
        e.category  = "User";
        e.isFactory = false;
        e.env       = parsed.env;
        e.id        = "user:" + UserData::toUtf8(entry.path().filename());
        user_.push_back(std::move(e));
    }
}

std::vector<ShapeEntry> ShapeLibrary::entries(const std::string& category) const
{
    std::vector<ShapeEntry> out;
    const int idx = categoryIndex(category);
    if (idx < 0)
        return out;
    out = (idx < 3) ? factory_[size_t(idx)] : user_;
    std::stable_sort(out.begin(), out.end(),
                      [](const ShapeEntry& a, const ShapeEntry& b) { return iless(a.name, b.name); });
    return out;
}

std::vector<ShapeEntry> ShapeLibrary::entries() const
{
    std::vector<ShapeEntry> out;
    for (const char* cat : kShapeCategories) {
        std::vector<ShapeEntry> group = entries(cat);
        out.insert(out.end(), group.begin(), group.end());
    }
    return out;
}

bool ShapeLibrary::saveUser(const std::string& name, const Envelope& env, std::string* err)
{
    try {
        const std::string dir = shapesDir();
        if (!UserData::ensureDir(dir)) {
            if (err) *err = "Could not create the shapes folder.";
            return false;
        }
        const std::string base = sanitiseFileBase(name);
        const std::string path = uniquePath(UserData::toPath(dir), base, ".kkshape");

        Envelope normalised = env;
        normalised.normalise();
        if (!UserData::writeFile(path, serializeShapeFile(name.empty() ? base : name, "User", normalised))) {
            if (err) *err = "Could not write the shape file.";
            return false;
        }
        rescan();
        return true;
    } catch (...) {
        if (err) *err = "Unexpected error while saving the shape.";
        return false;
    }
}

bool ShapeLibrary::removeUser(const std::string& id, std::string* err)
{
    try {
        if (!startsWith(id, "user:")) {
            if (err) *err = "Factory shapes cannot be removed.";
            return false;
        }
        const std::string fileName = id.substr(5);
        const fs::path path = UserData::toPath(shapesDir()) / UserData::toPath(fileName);
        std::error_code ec;
        fs::remove(path, ec);
        if (ec) {
            if (err) *err = "Could not delete the shape file.";
            return false;
        }
        rescan();
        return true;
    } catch (...) {
        if (err) *err = "Unexpected error while deleting the shape.";
        return false;
    }
}

} // namespace kick
