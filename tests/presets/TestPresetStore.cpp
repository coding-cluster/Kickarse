// Kickarse — PresetStore tests: rescan/entries/categories ordering, save/rename/remove of user
// presets in an isolated scratch root, favourites persistence, prev/next wrap with filters,
// file-name sanitising, and non-ASCII names.
#include <string>
#include <vector>

#include "TestHarness.h"
#include "shared/Presets.h"
#include "shared/UserData.h"

using kick::PresetData;
using kick::PresetEntry;
using kick::PresetFilter;
using kick::PresetStore;

namespace {

bool contains(const std::vector<PresetEntry>& v, const std::string& id)
{
    for (const auto& e : v)
        if (e.id == id)
            return true;
    return false;
}

const PresetEntry* findEntry(const std::vector<PresetEntry>& v, const std::string& id)
{
    for (const auto& e : v)
        if (e.id == id)
            return &e;
    return nullptr;
}

} // namespace

TEST_CASE(store_rescan_lists_all_factory_presets_grouped_by_category)
{
    PresetStore store(kpt::scratchDir("store_factory_list"));
    store.rescan();
    const auto& entries = store.entries();
    CHECK(entries.size() == 30); // exactly the factory content authored for this project

    for (const auto& e : entries) {
        CHECK(e.isFactory);
        CHECK(!e.isFavourite);
        CHECK(e.path.empty());
    }

    // Grouped by category, in the fixed factory order (Sidechain, Rhythmic, Spectral, Ring Mod,
    // Multiband, Creative): once we move past a category index we never see it again.
    static const char* kOrder[] = {"Sidechain", "Rhythmic", "Spectral", "Ring Mod", "Multiband", "Creative"};
    int lastCatIdx = -1;
    for (const auto& e : entries) {
        int catIdx = -1;
        for (int i = 0; i < 6; ++i)
            if (e.category == kOrder[i])
                catIdx = i;
        CHECK_MSG(catIdx >= 0, "unexpected factory category: %s", e.category.c_str());
        if (catIdx != lastCatIdx) {
            CHECK_MSG(catIdx > lastCatIdx, "category order broken at %s/%s", e.category.c_str(), e.name.c_str());
            lastCatIdx = catIdx;
        }
    }

    const auto cats = store.categories();
    CHECK(cats.size() == 6);
    for (int i = 0; i < 6 && i < int(cats.size()); ++i)
        CHECK(cats[size_t(i)] == kOrder[i]);
}

TEST_CASE(store_load_factory_preset_round_trips_fields)
{
    PresetStore store(kpt::scratchDir("store_factory_load"));
    store.rescan();
    CHECK(!store.entries().empty());
    const PresetEntry first = store.entries().front();
    PresetData data;
    CHECK(store.load(first.id, data));
    CHECK(data.name == first.name);
    CHECK(data.category == first.category);
}

TEST_CASE(store_save_user_preset_then_load_and_list)
{
    const std::string root = kpt::scratchDir("store_save_user");
    PresetStore store(root);
    store.rescan();
    const size_t before = store.entries().size();

    PresetData data  = PresetData::defaults();
    data.name        = "My Great Preset";
    data.category    = "Sidechain";
    data.params[kick::kParamDepth] = 42.f;

    std::string err;
    CHECK(store.saveUser(data, false, &err));
    CHECK(err.empty());
    CHECK(store.entries().size() == before + 1);

    const PresetEntry* e = nullptr;
    for (const auto& entry : store.entries())
        if (entry.name == "My Great Preset")
            e = &entry;
    CHECK(e != nullptr);
    if (e) {
        CHECK(!e->isFactory);
        CHECK(e->id.rfind("user:", 0) == 0);
        CHECK(!e->path.empty());

        PresetData loaded;
        CHECK(store.load(e->id, loaded));
        CHECK(loaded.name == "My Great Preset");
        CHECK_NEAR(loaded.params[kick::kParamDepth], 42.0, 1e-4);
    }
}

TEST_CASE(store_save_user_overwrite_semantics)
{
    const std::string root = kpt::scratchDir("store_overwrite");
    PresetStore store(root);
    store.rescan();

    PresetData data = PresetData::defaults();
    data.name       = "Dup Name";
    data.category   = "Creative";

    std::string err;
    CHECK(store.saveUser(data, false, &err));
    CHECK(!store.saveUser(data, false, &err)); // already exists, overwrite = false
    CHECK(!err.empty());

    data.params[kick::kParamDepth] = 7.f;
    CHECK(store.saveUser(data, true, &err)); // overwrite = true succeeds

    PresetData loaded;
    bool found = false;
    for (const auto& entry : store.entries()) {
        if (entry.name == "Dup Name") {
            found = true;
            CHECK(store.load(entry.id, loaded));
        }
    }
    CHECK(found);
    CHECK_NEAR(loaded.params[kick::kParamDepth], 7.0, 1e-4);
}

TEST_CASE(store_rename_and_remove_user_preset)
{
    const std::string root = kpt::scratchDir("store_rename_remove");
    PresetStore store(root);
    store.rescan();

    PresetData data = PresetData::defaults();
    data.name       = "Original Name";
    data.category   = "Rhythmic";
    std::string err;
    CHECK(store.saveUser(data, false, &err));

    std::string id;
    for (const auto& entry : store.entries())
        if (entry.name == "Original Name")
            id = entry.id;
    CHECK(!id.empty());

    std::string newId;
    CHECK(store.rename(id, "Renamed Preset", &err, &newId));
    CHECK(!newId.empty());
    CHECK(newId != id);
    CHECK(!contains(store.entries(), id));
    CHECK(contains(store.entries(), newId));
    const PresetEntry* renamed = findEntry(store.entries(), newId);
    CHECK(renamed != nullptr);
    if (renamed)
        CHECK(renamed->name == "Renamed Preset");

    CHECK(store.removeUser(newId, &err));
    CHECK(!contains(store.entries(), newId));

    // Factory presets cannot be removed or renamed.
    CHECK(!store.entries().empty());
    const std::string factoryId = store.entries().front().id;
    CHECK(!store.removeUser(factoryId, &err));
    CHECK(!err.empty());
    CHECK(!store.rename(factoryId, "Nope", &err));
}

TEST_CASE(store_rename_case_only_is_not_a_collision)
{
    // NTFS is case-insensitive: renaming "myshape" -> "MyShape" must succeed, not fail with
    // "already exists" against itself.
    const std::string root = kpt::scratchDir("store_rename_case_only");
    PresetStore store(root);
    store.rescan();

    PresetData data = PresetData::defaults();
    data.name       = "lowercase name";
    data.category   = "Sidechain";
    std::string err;
    CHECK(store.saveUser(data, false, &err));

    std::string id;
    for (const auto& entry : store.entries())
        if (entry.name == "lowercase name")
            id = entry.id;
    CHECK(!id.empty());

    std::string newId;
    CHECK_MSG(store.rename(id, "Lowercase Name", &err, &newId), "case-only rename failed: %s", err.c_str());
    const PresetEntry* renamed = findEntry(store.entries(), newId);
    CHECK(renamed != nullptr);
    if (renamed)
        CHECK(renamed->name == "Lowercase Name");
}

TEST_CASE(store_favourites_persist_across_instances)
{
    const std::string root = kpt::scratchDir("store_favourites");
    PresetStore storeA(root);
    storeA.rescan();
    CHECK(storeA.entries().size() >= 2);
    const std::string idA = storeA.entries()[0].id;
    const std::string idB = storeA.entries()[1].id;

    CHECK(!storeA.isFavourite(idA));
    storeA.setFavourite(idA, true);
    storeA.setFavourite(idB, true);
    CHECK(storeA.isFavourite(idA));
    CHECK(storeA.isFavourite(idB));
    storeA.setFavourite(idB, false);
    CHECK(!storeA.isFavourite(idB));

    PresetStore storeB(root); // simulates re-opening the plugin
    storeB.rescan();
    CHECK(storeB.isFavourite(idA));
    CHECK(!storeB.isFavourite(idB));
    const PresetEntry* eA = findEntry(storeB.entries(), idA);
    CHECK(eA != nullptr);
    if (eA)
        CHECK(eA->isFavourite);
}

TEST_CASE(store_next_prev_all_filter_wraps)
{
    PresetStore store(kpt::scratchDir("store_next_prev_all"));
    store.rescan();
    const auto& entries = store.entries();
    CHECK(entries.size() >= 3);

    const std::string first  = entries.front().id;
    const std::string second = entries[1].id;
    const std::string last   = entries.back().id;

    CHECK(store.next(first, PresetFilter::All) == second);
    CHECK(store.prev(first, PresetFilter::All) == last);   // wraps backward
    CHECK(store.next(last, PresetFilter::All) == first);   // wraps forward
}

TEST_CASE(store_next_prev_category_filter_stays_within_category)
{
    PresetStore store(kpt::scratchDir("store_next_prev_category"));
    store.rescan();
    const auto& entries = store.entries();

    // Find a full run of same-category entries (factory content groups categories contiguously).
    size_t runStart = 0;
    while (runStart < entries.size()) {
        size_t runEnd = runStart + 1;
        while (runEnd < entries.size() && entries[runEnd].category == entries[runStart].category)
            ++runEnd;
        if (runEnd - runStart >= 3)
            break;
        runStart = runEnd;
    }
    CHECK(runStart < entries.size());
    size_t runEnd = runStart + 1;
    while (runEnd < entries.size() && entries[runEnd].category == entries[runStart].category)
        ++runEnd;
    CHECK(runEnd - runStart >= 3);

    const std::string firstOfRun  = entries[runStart].id;
    const std::string secondOfRun = entries[runStart + 1].id;
    const std::string lastOfRun   = entries[runEnd - 1].id;

    CHECK(store.next(firstOfRun, PresetFilter::Category) == secondOfRun);
    CHECK(store.prev(firstOfRun, PresetFilter::Category) == lastOfRun); // wraps within the category
    CHECK(store.next(lastOfRun, PresetFilter::Category) == firstOfRun);
}

TEST_CASE(store_next_prev_favourites_filter)
{
    PresetStore store(kpt::scratchDir("store_next_prev_favourites"));
    store.rescan();
    const auto& entries = store.entries();
    CHECK(entries.size() >= 11);

    const std::string a = entries[0].id;
    const std::string b = entries[5].id;
    const std::string c = entries[10].id;

    // No favourites yet: neighbour is the id itself (nothing to move to).
    CHECK(store.next(a, PresetFilter::Favourites) == a);

    store.setFavourite(a, true);
    CHECK(store.next(a, PresetFilter::Favourites) == a); // still alone

    store.setFavourite(b, true);
    store.setFavourite(c, true);
    CHECK(store.next(a, PresetFilter::Favourites) == b);
    CHECK(store.next(b, PresetFilter::Favourites) == c);
    CHECK(store.next(c, PresetFilter::Favourites) == a); // wraps
    CHECK(store.prev(a, PresetFilter::Favourites) == c);
}

TEST_CASE(store_filename_sanitising)
{
    const std::string root = kpt::scratchDir("store_sanitise");
    PresetStore store(root);
    store.rescan();

    PresetData data = PresetData::defaults();
    data.name       = "Bad:Name/Test*?<>|Chars";
    data.category   = "Creative";
    std::string err;
    CHECK(store.saveUser(data, false, &err));

    bool found = false;
    for (const auto& entry : store.entries()) {
        if (entry.name == data.name) {
            found = true;
            // entry.path is an absolute path, so it legitimately contains ':' (drive letter) and
            // '\\' (separators); only the FILE NAME component (after the last separator) needs to
            // be free of the characters that are illegal anywhere in a Windows file name.
            const size_t slash = entry.path.find_last_of("/\\");
            const std::string fileName = slash == std::string::npos ? entry.path : entry.path.substr(slash + 1);
            for (char bad : std::string("\"*:<>?|/\\")) {
                CHECK_MSG(fileName.find(bad) == std::string::npos, "illegal char '%c' leaked into file name", bad);
            }
        }
    }
    CHECK(found);

    // A reserved Windows device name gets a disambiguating suffix rather than colliding/failing.
    PresetData reserved = PresetData::defaults();
    reserved.name       = "CON";
    reserved.category   = "Creative";
    CHECK(store.saveUser(reserved, false, &err));
}

TEST_CASE(store_non_ascii_name_round_trips)
{
    const std::string root = kpt::scratchDir("store_non_ascii");
    PresetStore store(root);
    store.rescan();

    PresetData data = PresetData::defaults();
    data.name       = "K\xC3\xB6g\xC3\xA9 \xE6\xB1\x89\xE5\xAD\x97 \xCE\xA9"; // "Kögé 汉字 Ω" (UTF-8)
    data.category   = "Creative";
    std::string err;
    CHECK_MSG(store.saveUser(data, false, &err), "save failed: %s", err.c_str());

    std::string id;
    for (const auto& entry : store.entries())
        if (entry.name == data.name)
            id = entry.id;
    CHECK(!id.empty());

    PresetData loaded;
    CHECK(store.load(id, loaded));
    CHECK(loaded.name == data.name);

    CHECK(store.removeUser(id, &err));
    CHECK(!contains(store.entries(), id));
}

TEST_CASE(store_case_insensitive_name_sort_within_category)
{
    const std::string root = kpt::scratchDir("store_sort_case");
    PresetStore store(root);
    store.rescan();

    PresetData a = PresetData::defaults();
    a.name       = "Banana";
    a.category   = "ZZZCustomCategory";
    PresetData b = PresetData::defaults();
    b.name       = "apple";
    b.category   = "ZZZCustomCategory";

    std::string err;
    CHECK(store.saveUser(a, false, &err));
    CHECK(store.saveUser(b, false, &err));

    std::vector<std::string> namesInCategory;
    for (const auto& e : store.entries())
        if (e.category == "ZZZCustomCategory")
            namesInCategory.push_back(e.name);
    CHECK(namesInCategory.size() == 2);
    if (namesInCategory.size() == 2) {
        CHECK(namesInCategory[0] == "apple");  // case-insensitive: apple < Banana
        CHECK(namesInCategory[1] == "Banana");
    }

    // A brand new category must sort after all six fixed factory categories.
    const auto cats = store.categories();
    CHECK(!cats.empty());
    CHECK(cats.back() == "ZZZCustomCategory");
}
