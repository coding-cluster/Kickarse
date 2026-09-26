// Kickarse — preset model & store: framework-independent .kkpreset file I/O, factory + user
// content, favourites. UI-thread only (see docs/ARCHITECTURE.md); no exceptions escape; every
// I/O failure is reported through a return value / *err, never thrown.
//
// .kkpreset file format (UTF-8 text, LF or CRLF lines, both accepted):
//   # Kickarse preset          -- '#' starts a full-line comment; also allowed inline after a
//   version=1                     value when preceded by whitespace, e.g. "param.mode=Sync   # ..."
//   name=Classic Pump
//   category=Sidechain
//   author=Kickarse
//   param.mode=Sync             -- keyed by the stable Params.h `symbol`; enum values may be
//   param.depth=100                written/read as either the label ("Sync") or the numeric value
//   ...                            (2); this file always *writes* labels. `bypass` is never
//   state.envA=KE1 0,0,0.3,0;...   written or read here (see docs/ARCHITECTURE.md, State keys).
//   state.envB=KE1 0,1,0,0;1,1,0,0
// Unknown keys are ignored. A parameter with no `param.<symbol>` line falls back to its
// Params.h default (PresetData::has[] records which ones were actually present, so a hand
// authored factory preset can specify only the parameters it cares about). All numeric values
// are clamped to the parameter's [min,max]. An invalid/unparsable state.envA or state.envB falls
// back to the default envelope rather than being carried through as garbage.
#pragma once

#include <string>
#include <vector>

#include "Params.h"

namespace kick {

// One full preset: every automatable parameter (never `bypass`) plus the two envelopes.
struct PresetData {
    std::string name;
    std::string category;
    std::string author;
    float       params[kParamCount] = {};
    bool        has[kParamCount]    = {};   // true where explicitly set (by the file, or by the
                                             // caller before saveUser()); false => value is the
                                             // Params.h default and was not written by serialize()
    std::string envA;                       // Envelope::serialize() text, e.g. "KE1 0,0,0.3,0;..."
    std::string envB;

    // Every param at its Params.h default, has[]=true for all except bypass (a freshly-defaulted
    // preset is considered fully specified), envA = the classic duck, envB = flat (no ducking).
    static PresetData defaults();

    // UTF-8 .kkpreset text; see the file-format comment above. Only params with has[i]==true are
    // written (bypass is always skipped); name/category/author/envA/envB are always written.
    std::string serialize() const;

    // Best-effort parse of text produced by serialize() (or hand-authored factory content, or
    // arbitrary/garbage/partial input). Never fails: unrecognised bytes simply contribute no
    // keys, and every field that was not found ends up at PresetData::defaults()'s value, with
    // has[] left false for any parameter that was not found.
    static PresetData parse(const std::string& text);
};

enum class PresetFilter { All, Favourites, Category };

struct PresetEntry {
    std::string id;         // "factory:<Category>/<Name>" or "user:<Category>/<file name>"
    std::string name;
    std::string category;
    bool        isFactory   = false;
    bool        isFavourite = false;
    std::string path;       // absolute file path for a user preset; empty for a factory preset
};

// Fixed factory category order; any other category (created by the user by saving into a new
// category name) sorts after these, alphabetically.
inline constexpr const char* kFactoryPresetCategories[] = {
    "Sidechain", "Rhythmic", "Spectral", "Ring Mod", "Multiband", "Creative",
};
inline constexpr int kNumFactoryPresetCategories =
    int(sizeof(kFactoryPresetCategories) / sizeof(kFactoryPresetCategories[0]));

class PresetStore {
public:
    // userRoot overrides the "<Documents>\Kickarse" root for this store (tests only); "" (the
    // default) resolves it once via UserData::root() (which itself honours
    // UserData::setRootOverride(), if that was used instead). rescan() is NOT called implicitly;
    // call it once after construction.
    explicit PresetStore(std::string userRoot = {});

    // Reloads the (fixed, compiled-in) factory list, rescans <root>\Presets\<Category>\*.kkpreset
    // on disk, and reloads favourites.txt. Safe to call any time from the UI thread.
    void rescan();

    // Sorted by category (factory categories in kFactoryPresetCategories order, then any other
    // category alphabetically), then by name (case-insensitive) within a category.
    const std::vector<PresetEntry>& entries() const noexcept;
    std::vector<std::string>        categories() const; // categories() present in entries(), in that order

    bool load(const std::string& id, PresetData& out) const;

    // Sanitises data.name into a file name and writes to <root>\Presets\<data.category>\. Fails
    // (returns false, *err set when err != nullptr) if that file already exists and !overwrite,
    // or on I/O error; succeeds and replaces the file when overwrite is true. An empty category
    // defaults to "Sidechain". Calls rescan() on success.
    bool saveUser(const PresetData& data, bool overwrite, std::string* err = nullptr);

    // Only ids starting with "user:" can be removed/renamed; factory presets are read-only.
    bool removeUser(const std::string& id, std::string* err = nullptr);

    // Renames the on-disk file and the preset's internal `name=` field; keeps its category and
    // favourite status. *newId (if given) receives the preset's new id, since renaming changes
    // it. Fails if a preset with that (sanitised) name already exists in the same category.
    bool rename(const std::string& id, const std::string& newName, std::string* err = nullptr,
                std::string* newId = nullptr);

    void setFavourite(const std::string& id, bool fav);
    bool isFavourite(const std::string& id) const;

    // Wrap-around neighbour within entries(), restricted by filter (Category = same category as
    // the entry `id` currently refers to). Returns `id` unchanged when it has no neighbour under
    // that filter (the filtered list has 0 or 1 entries) or isn't found in entries() at all.
    std::string next(const std::string& id, PresetFilter filter) const;
    std::string prev(const std::string& id, PresetFilter filter) const;

private:
    std::string root_;
    std::vector<PresetEntry> factory_;
    std::vector<PresetEntry> user_;
    std::vector<PresetEntry> entries_; // factory_ + user_, merged & sorted; cached by rescan()
    std::vector<std::string> favourites_;

    std::string presetsDir() const;
    std::string presetsDir(const std::string& category) const;
    std::string favouritesFile() const;

    void loadFavourites();
    void saveFavourites() const;
    void rebuildEntries();
    void rescanFactory();
    void rescanUser();

    const PresetEntry* find(const std::string& id) const;
    std::string neighbour(const std::string& id, PresetFilter filter, int step) const;
};

} // namespace kick
