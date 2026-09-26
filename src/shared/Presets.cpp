// Kickarse — Presets implementation (see Presets.h for the file format and API contract).
#include "Presets.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <system_error>

#include "Envelope.h"
#include "FactoryData.h"
#include "UserData.h"

namespace kick {

namespace {

namespace fs = std::filesystem;

// ---- small text helpers ------------------------------------------------------------------------

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

bool iequals(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return true;
}

// Case-insensitive "less than", ASCII-only (good enough for sorting preset/category names).
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

void appendFloat(std::string& s, float v)
{
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof(buf), v);
    s.append(buf, r.ptr);
}

// Parses `text` fully as a float (no trailing junk). Returns false otherwise (e.g. an enum label).
bool parseFullFloat(std::string_view text, float& out) noexcept
{
    if (text.empty())
        return false;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), out, std::chars_format::general);
    return r.ec == std::errc() && r.ptr == text.data() + text.size() && std::isfinite(out);
}

int findParamBySymbol(std::string_view symbol) noexcept
{
    for (int i = 0; i < kParamCount; ++i)
        if (symbol == kParams[i].symbol)
            return i;
    return -1;
}

// value = min + labelIndex for every enum table in Params.h (all start at min = 0 with one
// consecutive integer per label); clamps into [min,max] like every other parameter.
float parseParamValue(const ParamInfo& info, std::string_view text, bool& ok) noexcept
{
    float v = 0.f;
    if (parseFullFloat(text, v)) {
        ok = true;
        return std::clamp(v, info.min, info.max);
    }
    if ((info.flags & kPFEnum) != 0 && info.labels != nullptr) {
        const int count = int(info.max - info.min) + 1;
        for (int i = 0; i < count; ++i) {
            if (iequals(text, info.labels[i])) {
                ok = true;
                return std::clamp(info.min + float(i), info.min, info.max);
            }
        }
    }
    ok = false;
    return info.def;
}

std::string formatParamValue(const ParamInfo& info, float value)
{
    if ((info.flags & kPFEnum) != 0 && info.labels != nullptr) {
        const int count = int(info.max - info.min) + 1;
        int idx = int(std::lround(double(value - info.min)));
        idx = std::clamp(idx, 0, count - 1);
        return info.labels[idx];
    }
    std::string s;
    appendFloat(s, value);
    return s;
}

// Any control character or CR/LF is replaced with a space: name/category/author are always a
// single line in the serialized file.
std::string singleLine(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        out += (c == '\n' || c == '\r') ? ' ' : c;
    return out;
}

// ---- .kkpreset parsing ----------------------------------------------------------------------

void parseLine(PresetData& out, std::string_view rawLine)
{
    std::string_view line = trim(rawLine);
    if (line.empty() || line.front() == '#')
        return;
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos)
        return;
    const std::string_view key = trim(line.substr(0, eq));
    std::string_view value = line.substr(eq + 1);

    // Inline "value   # comment" — only treated as a comment when '#' is preceded by whitespace,
    // so a value that legitimately contains '#' with no space before it is left untouched.
    for (size_t i = 1; i + 1 <= value.size(); ++i) {
        if (value[i] == '#' && isSpaceCh(value[i - 1])) {
            value = value.substr(0, i);
            break;
        }
    }
    value = trim(value);

    if (key == "version") {
        return; // only version 1 exists so far; nothing to branch on yet
    }
    if (key == "name") {
        out.name = std::string(value);
        return;
    }
    if (key == "category") {
        out.category = std::string(value);
        return;
    }
    if (key == "author") {
        out.author = std::string(value);
        return;
    }
    if (key.size() > 6 && key.substr(0, 6) == "param.") {
        const std::string_view symbol = key.substr(6);
        const int idx = findParamBySymbol(symbol);
        if (idx < 0 || idx == kParamBypass) // unknown symbol, or the never-stored bypass switch
            return;
        bool ok = false;
        const float v = parseParamValue(kParams[idx], value, ok);
        if (ok) {
            out.params[idx] = v;
            out.has[idx]    = true;
        }
        return;
    }
    if (key == "state.envA" || key == "state.envB") {
        Envelope probe;
        if (!probe.deserialize(value)) // invalid envelope text: keep the default already in `out`
            return;
        (key == "state.envA" ? out.envA : out.envB) = std::string(value);
        return;
    }
    // unknown key: ignored
}

// ---- file-name sanitising & de-duplication ----------------------------------------------------

std::string sanitiseFileBase(const std::string& name, const char* fallback)
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
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) // don't split a UTF-8 sequence
            --cut;
        s.resize(cut);
    }
    if (s.empty())
        s = fallback;

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

bool startsWith(const std::string& s, const char* prefix) noexcept
{
    const size_t n = std::strlen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

} // namespace

// ===============================================================================================
// PresetData

PresetData PresetData::defaults()
{
    PresetData d;
    d.name     = "Default";
    d.category = "Sidechain";
    d.author   = "Kickarse";
    for (int i = 0; i < kParamCount; ++i) {
        d.params[i] = kParams[i].def;
        d.has[i]    = (i != kParamBypass);
    }
    d.envA = Envelope().serialize();
    d.envB = Envelope::flat().serialize();
    return d;
}

std::string PresetData::serialize() const
{
    std::string s;
    s.reserve(512);
    s += "# Kickarse preset\n";
    s += "version=1\n";
    s += "name=" + singleLine(name) + "\n";
    s += "category=" + singleLine(category) + "\n";
    s += "author=" + singleLine(author) + "\n";
    for (int i = 0; i < kParamCount; ++i) {
        if (i == kParamBypass || !has[i])
            continue;
        s += "param.";
        s += kParams[i].symbol;
        s += "=";
        s += formatParamValue(kParams[i], params[i]);
        s += "\n";
    }
    s += "state.envA=" + envA + "\n";
    s += "state.envB=" + envB + "\n";
    return s;
}

PresetData PresetData::parse(const std::string& text)
{
    PresetData out = defaults();
    for (int i = 0; i < kParamCount; ++i)
        out.has[i] = false;

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        const size_t end = (nl == std::string::npos) ? text.size() : nl;
        parseLine(out, std::string_view(text).substr(pos, end - pos));
        if (nl == std::string::npos)
            break;
        pos = nl + 1;
    }
    return out;
}

// ===============================================================================================
// PresetStore

PresetStore::PresetStore(std::string userRoot)
    : root_(userRoot.empty() ? UserData::root() : std::move(userRoot))
{
}

std::string PresetStore::presetsDir() const
{
    return UserData::toUtf8(UserData::toPath(root_) / "Presets");
}

std::string PresetStore::presetsDir(const std::string& category) const
{
    return UserData::toUtf8(UserData::toPath(root_) / "Presets" / UserData::toPath(category));
}

std::string PresetStore::favouritesFile() const
{
    return UserData::toUtf8(UserData::toPath(root_) / "favourites.txt");
}

void PresetStore::rescan()
{
    // Belt-and-suspenders: every filesystem call above already goes through a std::error_code,
    // but std::filesystem::path construction/concatenation can in principle throw std::bad_alloc;
    // no exception may cross this API (see the file header comment), so catch defensively.
    try {
        rescanFactory();
        rescanUser();
        loadFavourites();
        rebuildEntries();
    } catch (...) {
        entries_.clear();
        for (const PresetEntry& e : factory_)
            entries_.push_back(e);
    }
}

void PresetStore::rescanFactory()
{
    factory_.clear();
    for (int i = 0; i < factory::kPresetCount; ++i) {
        const factory::FactoryFile& f = factory::kPresets[i];
        PresetEntry e;
        e.category   = f.category;
        e.name       = f.name;
        e.isFactory  = true;
        e.id         = "factory:" + e.category + "/" + e.name;
        e.path.clear();
        factory_.push_back(std::move(e));
    }
}

// Manual, non-throwing directory walk (directory_iterator's implicit ++ used by range-based for
// can throw on I/O errors; every increment here goes through an explicit std::error_code instead,
// so a mid-scan I/O hiccup just ends that sub-scan early rather than throwing across the API).
void PresetStore::rescanUser()
{
    user_.clear();
    const fs::path base = UserData::toPath(presetsDir());
    std::error_code ec;
    if (!fs::exists(base, ec) || ec)
        return;

    std::error_code catEc;
    for (fs::directory_iterator catIt(base, catEc); !catEc && catIt != fs::directory_iterator(); catIt.increment(catEc)) {
        const fs::directory_entry& catEntry = *catIt;
        std::error_code isDirEc;
        if (!catEntry.is_directory(isDirEc) || isDirEc)
            continue;
        const std::string categoryFolder = UserData::toUtf8(catEntry.path().filename());

        std::error_code fileEc;
        for (fs::directory_iterator fileIt(catEntry.path(), fileEc); !fileEc && fileIt != fs::directory_iterator();
             fileIt.increment(fileEc)) {
            const fs::directory_entry& fileEntry = *fileIt;
            std::error_code isFileEc;
            if (!fileEntry.is_regular_file(isFileEc) || isFileEc)
                continue;
            std::string ext = fileEntry.path().extension().string();
            for (char& c : ext)
                c = char(std::tolower((unsigned char)c));
            if (ext != ".kkpreset")
                continue;

            std::string text;
            if (!UserData::readFile(UserData::toUtf8(fileEntry.path()), text))
                continue;
            const PresetData pd = PresetData::parse(text);

            PresetEntry e;
            e.category  = pd.category.empty() ? categoryFolder : pd.category;
            e.name      = pd.name.empty() ? UserData::toUtf8(fileEntry.path().stem()) : pd.name;
            e.isFactory = false;
            e.path      = UserData::toUtf8(fileEntry.path());
            e.id        = "user:" + categoryFolder + "/" + UserData::toUtf8(fileEntry.path().filename());
            user_.push_back(std::move(e));
        }
    }
}

void PresetStore::loadFavourites()
{
    favourites_.clear();
    std::string text;
    if (!UserData::readFile(favouritesFile(), text))
        return;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        const size_t end = (nl == std::string::npos) ? text.size() : nl;
        const std::string_view line = trim(std::string_view(text).substr(pos, end - pos));
        if (!line.empty())
            favourites_.emplace_back(line);
        if (nl == std::string::npos)
            break;
        pos = nl + 1;
    }
}

void PresetStore::saveFavourites() const
{
    std::vector<std::string> sorted = favourites_;
    std::sort(sorted.begin(), sorted.end());
    std::string text;
    for (const std::string& id : sorted) {
        text += id;
        text += '\n';
    }
    UserData::ensureDir(root_);
    UserData::writeFile(favouritesFile(), text);
}

void PresetStore::rebuildEntries()
{
    entries_.clear();
    entries_.reserve(factory_.size() + user_.size());

    std::vector<std::string> order(std::begin(kFactoryPresetCategories), std::end(kFactoryPresetCategories));
    std::vector<std::string> extra;
    for (const PresetEntry& e : user_) {
        if (std::find(order.begin(), order.end(), e.category) == order.end()
            && std::find(extra.begin(), extra.end(), e.category) == extra.end())
            extra.push_back(e.category);
    }
    std::sort(extra.begin(), extra.end(), iless);
    order.insert(order.end(), extra.begin(), extra.end());

    for (const std::string& cat : order) {
        std::vector<const PresetEntry*> group;
        for (const PresetEntry& e : factory_)
            if (e.category == cat)
                group.push_back(&e);
        for (const PresetEntry& e : user_)
            if (e.category == cat)
                group.push_back(&e);
        std::stable_sort(group.begin(), group.end(),
                          [](const PresetEntry* a, const PresetEntry* b) { return iless(a->name, b->name); });
        for (const PresetEntry* e : group) {
            PresetEntry copy = *e;
            copy.isFavourite = std::find(favourites_.begin(), favourites_.end(), copy.id) != favourites_.end();
            entries_.push_back(std::move(copy));
        }
    }
}

const std::vector<PresetEntry>& PresetStore::entries() const noexcept
{
    return entries_;
}

std::vector<std::string> PresetStore::categories() const
{
    std::vector<std::string> out;
    for (const PresetEntry& e : entries_)
        if (std::find(out.begin(), out.end(), e.category) == out.end())
            out.push_back(e.category);
    return out;
}

const PresetEntry* PresetStore::find(const std::string& id) const
{
    for (const PresetEntry& e : entries_)
        if (e.id == id)
            return &e;
    return nullptr;
}

bool PresetStore::load(const std::string& id, PresetData& out) const
{
    try {
        if (startsWith(id, "factory:")) {
            for (int i = 0; i < factory::kPresetCount; ++i) {
                const factory::FactoryFile& f = factory::kPresets[i];
                if (id == "factory:" + std::string(f.category) + "/" + f.name) {
                    out = PresetData::parse(f.text);
                    return true;
                }
            }
            return false;
        }
        if (startsWith(id, "user:")) {
            const PresetEntry* e = find(id);
            if (e == nullptr)
                return false;
            std::string text;
            if (!UserData::readFile(e->path, text))
                return false;
            out = PresetData::parse(text);
            return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}

bool PresetStore::saveUser(const PresetData& data, bool overwrite, std::string* err)
{
    try {
        const std::string category = data.category.empty() ? "Sidechain" : data.category;
        const std::string base     = sanitiseFileBase(data.name, "Preset");
        const std::string dir      = presetsDir(category);
        if (!UserData::ensureDir(dir)) {
            if (err) *err = "Could not create the preset folder.";
            return false;
        }
        const std::string path = UserData::toUtf8(UserData::toPath(dir) / (base + ".kkpreset"));

        std::error_code ec;
        const bool exists = fs::exists(UserData::toPath(path), ec);
        if (exists && !overwrite) {
            if (err) *err = "A preset named \"" + data.name + "\" already exists in " + category + ".";
            return false;
        }

        PresetData toWrite = data;
        if (toWrite.name.empty())
            toWrite.name = base;
        toWrite.category = category;

        if (!UserData::writeFile(path, toWrite.serialize())) {
            if (err) *err = "Could not write the preset file.";
            return false;
        }
        rescan();
        return true;
    } catch (...) {
        if (err) *err = "Unexpected error while saving the preset.";
        return false;
    }
}

bool PresetStore::removeUser(const std::string& id, std::string* err)
{
    try {
        if (!startsWith(id, "user:")) {
            if (err) *err = "Factory presets cannot be removed.";
            return false;
        }
        const PresetEntry* e = find(id);
        if (e == nullptr) {
            if (err) *err = "Preset not found.";
            return false;
        }
        std::error_code ec;
        fs::remove(UserData::toPath(e->path), ec);
        if (ec) {
            if (err) *err = "Could not delete the preset file.";
            return false;
        }
        favourites_.erase(std::remove(favourites_.begin(), favourites_.end(), id), favourites_.end());
        saveFavourites();
        rescan();
        return true;
    } catch (...) {
        if (err) *err = "Unexpected error while deleting the preset.";
        return false;
    }
}

bool PresetStore::rename(const std::string& id, const std::string& newName, std::string* err, std::string* newId)
{
    try {
        if (!startsWith(id, "user:")) {
            if (err) *err = "Factory presets cannot be renamed.";
            return false;
        }
        const PresetEntry* e = find(id);
        if (e == nullptr) {
            if (err) *err = "Preset not found.";
            return false;
        }
        const std::string category = e->category;
        const std::string base     = sanitiseFileBase(newName, "Preset");
        const std::string dir      = presetsDir(category);
        const std::string newPath  = UserData::toUtf8(UserData::toPath(dir) / (base + ".kkpreset"));
        const std::string oldPath  = e->path;
        // NTFS resolves paths case-insensitively but keeps whatever case a file was created
        // with, so a rename that only changes case (e.g. "myshape" -> "MyShape") is neither a
        // collision with the file being renamed nor a no-op: it needs an actual rename to update
        // the stored case, which a plain overwrite-in-place would silently fail to do.
        const bool sameFile     = iequals(newPath, oldPath);
        const bool caseOnlyMove = sameFile && newPath != oldPath;

        if (!sameFile) {
            std::error_code ec;
            if (fs::exists(UserData::toPath(newPath), ec)) {
                if (err) *err = "A preset named \"" + newName + "\" already exists in " + category + ".";
                return false;
            }
        }

        std::string text;
        if (!UserData::readFile(oldPath, text)) {
            if (err) *err = "Could not read the preset file.";
            return false;
        }
        PresetData pd = PresetData::parse(text);
        pd.name       = newName;
        pd.category   = category;

        if (caseOnlyMove) {
            // A single rename to a case-variant of the same name is unreliable across Windows
            // versions/filesystems; go through a temporary name so the two renames involved are
            // each unambiguously a "different name" rename.
            const std::string tmpPath = oldPath + ".kkrename_tmp";
            std::error_code ec;
            fs::rename(UserData::toPath(oldPath), UserData::toPath(tmpPath), ec);
            if (ec) {
                if (err) *err = "Could not rename the preset file.";
                return false;
            }
            fs::rename(UserData::toPath(tmpPath), UserData::toPath(newPath), ec);
            if (ec) {
                std::error_code restoreEc;
                fs::rename(UserData::toPath(tmpPath), UserData::toPath(oldPath), restoreEc);
                if (err) *err = "Could not rename the preset file.";
                return false;
            }
        }

        if (!UserData::writeFile(newPath, pd.serialize())) {
            if (err) *err = "Could not write the preset file.";
            return false;
        }
        if (!sameFile) {
            std::error_code ec;
            fs::remove(UserData::toPath(oldPath), ec);
        }

        const std::string newFileName = UserData::toUtf8(UserData::toPath(newPath).filename());
        const std::string resultId    = "user:" + category + "/" + newFileName;
        if (favourites_.end() != std::find(favourites_.begin(), favourites_.end(), id)) {
            favourites_.erase(std::remove(favourites_.begin(), favourites_.end(), id), favourites_.end());
            favourites_.push_back(resultId);
            saveFavourites();
        }
        rescan();
        if (newId)
            *newId = resultId;
        return true;
    } catch (...) {
        if (err) *err = "Unexpected error while renaming the preset.";
        return false;
    }
}

void PresetStore::setFavourite(const std::string& id, bool fav)
{
    const bool already = std::find(favourites_.begin(), favourites_.end(), id) != favourites_.end();
    if (fav == already)
        return;
    if (fav)
        favourites_.push_back(id);
    else
        favourites_.erase(std::remove(favourites_.begin(), favourites_.end(), id), favourites_.end());
    saveFavourites();
    for (PresetEntry& e : entries_)
        if (e.id == id)
            e.isFavourite = fav;
}

bool PresetStore::isFavourite(const std::string& id) const
{
    return std::find(favourites_.begin(), favourites_.end(), id) != favourites_.end();
}

std::string PresetStore::neighbour(const std::string& id, PresetFilter filter, int step) const
{
    std::vector<const PresetEntry*> pool;
    const PresetEntry* current = find(id);
    for (const PresetEntry& e : entries_) {
        switch (filter) {
            case PresetFilter::All:
                pool.push_back(&e);
                break;
            case PresetFilter::Favourites:
                if (e.isFavourite)
                    pool.push_back(&e);
                break;
            case PresetFilter::Category:
                if (current != nullptr && e.category == current->category)
                    pool.push_back(&e);
                break;
        }
    }
    if (pool.size() < 2)
        return id;
    int idx = -1;
    for (size_t i = 0; i < pool.size(); ++i)
        if (pool[i]->id == id) {
            idx = int(i);
            break;
        }
    if (idx < 0)
        return step > 0 ? pool.front()->id : pool.back()->id;
    const int n = int(pool.size());
    idx = ((idx + step) % n + n) % n;
    return pool[size_t(idx)]->id;
}

std::string PresetStore::next(const std::string& id, PresetFilter filter) const
{
    return neighbour(id, filter, 1);
}

std::string PresetStore::prev(const std::string& id, PresetFilter filter) const
{
    return neighbour(id, filter, -1);
}

} // namespace kick
