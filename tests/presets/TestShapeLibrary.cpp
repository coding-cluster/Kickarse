// Kickarse — ShapeLibrary tests: factory category contents, user save/remove (with the auto
// unique-name-on-collision behaviour), and category ordering.
#include <cctype>
#include <string>

#include "TestHarness.h"
#include "shared/Shapes.h"

using kick::Envelope;
using kick::ShapeEntry;
using kick::ShapeLibrary;

TEST_CASE(shapes_factory_categories_have_sixteen_each)
{
    ShapeLibrary lib(kpt::scratchDir("shapes_factory_counts"));
    lib.rescan();
    for (const char* cat : {"Sidechain", "Rhythmic", "Simple"}) {
        const auto entries = lib.entries(cat);
        CHECK_MSG(entries.size() == 16, "%s: expected 16 shapes, got %zu", cat, entries.size());
        for (const auto& e : entries) {
            CHECK(e.isFactory);
            CHECK(e.category == cat);
            CHECK(e.id.rfind("factory:", 0) == 0);
        }
    }
    CHECK(lib.entries("User").empty());
    CHECK(lib.entries("Not A Category").empty());
}

TEST_CASE(shapes_factory_entries_sorted_by_name_case_insensitive)
{
    ShapeLibrary lib(kpt::scratchDir("shapes_sorted"));
    lib.rescan();
    const auto entries = lib.entries("Sidechain");
    for (size_t i = 1; i < entries.size(); ++i) {
        std::string a = entries[i - 1].name, b = entries[i].name;
        for (char& c : a)
            c = char(std::tolower((unsigned char)c));
        for (char& c : b)
            c = char(std::tolower((unsigned char)c));
        CHECK_MSG(a <= b, "not sorted: %s before %s", entries[i - 1].name.c_str(), entries[i].name.c_str());
    }
}

TEST_CASE(shapes_save_and_remove_user_shape)
{
    const std::string root = kpt::scratchDir("shapes_save_remove");
    ShapeLibrary lib(root);
    lib.rescan();
    CHECK(lib.entries("User").empty());

    Envelope env = Envelope::flat();
    env.node(0).y = 0.2f;
    std::string err;
    CHECK(lib.saveUser("My Shape", env, &err));
    CHECK_MSG(err.empty(), "unexpected error: %s", err.c_str());

    const auto entries = lib.entries("User");
    CHECK(entries.size() == 1);
    CHECK(entries[0].name == "My Shape");
    CHECK(entries[0].category == "User");
    CHECK(!entries[0].isFactory);
    CHECK(entries[0].id.rfind("user:", 0) == 0);
    CHECK_NEAR(entries[0].env.node(0).y, 0.2, 1e-4);

    CHECK(lib.removeUser(entries[0].id, &err));
    CHECK(lib.entries("User").empty());

    // A factory shape cannot be removed.
    const auto factoryEntries = lib.entries("Sidechain");
    CHECK(!factoryEntries.empty());
    CHECK(!lib.removeUser(factoryEntries[0].id, &err));
    CHECK(!err.empty());
}

TEST_CASE(shapes_save_user_duplicate_name_is_uniquified)
{
    const std::string root = kpt::scratchDir("shapes_dup_name");
    ShapeLibrary lib(root);
    lib.rescan();

    std::string err;
    CHECK(lib.saveUser("Repeat", Envelope::flat(), &err));
    CHECK(lib.saveUser("Repeat", Envelope::flat(), &err)); // no overwrite flag: must not collide/fail

    const auto entries = lib.entries("User");
    CHECK(entries.size() == 2);
    CHECK(entries[0].id != entries[1].id);
}

TEST_CASE(shapes_entries_all_categories_matches_fixed_order)
{
    ShapeLibrary lib(kpt::scratchDir("shapes_all"));
    lib.rescan();
    std::string err;
    CHECK(lib.saveUser("Solo User Shape", Envelope::flat(), &err));

    const auto all = lib.entries();
    CHECK(all.size() == 16 * 3 + 1);
    // First 16 must be Sidechain, next 16 Rhythmic, next 16 Simple, last the lone User shape.
    for (int i = 0; i < 16; ++i)
        CHECK(all[size_t(i)].category == "Sidechain");
    for (int i = 16; i < 32; ++i)
        CHECK(all[size_t(i)].category == "Rhythmic");
    for (int i = 32; i < 48; ++i)
        CHECK(all[size_t(i)].category == "Simple");
    CHECK(all[48].category == "User");
}
