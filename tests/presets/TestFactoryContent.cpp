// Kickarse — validates the embedded factory content itself (resources/presets/**,
// resources/shapes/**, embedded by cmake/EmbedFactory.cmake): every preset and shape parses,
// every envelope it carries deserializes to a valid Envelope, names are unique within their
// category, and categories are the ones PresetStore/ShapeLibrary actually expose.
#include <set>
#include <string>

#include "TestHarness.h"
#include "shared/Envelope.h"
#include "shared/FactoryData.h"
#include "shared/Presets.h"
#include "shared/Shapes.h"

using kick::Envelope;
using kick::PresetData;

TEST_CASE(factory_preset_count_matches_authored_content)
{
    CHECK(kick::factory::kPresetCount == 30);
}

TEST_CASE(factory_shape_count_matches_authored_content)
{
    CHECK(kick::factory::kShapeCount == 48); // 16 Sidechain + 16 Rhythmic + 16 Simple
}

TEST_CASE(factory_every_preset_parses_and_its_envelopes_deserialize)
{
    std::set<std::string> seenIds;
    for (int i = 0; i < kick::factory::kPresetCount; ++i) {
        const auto& f = kick::factory::kPresets[i];
        CHECK(f.category[0] != '\0');
        CHECK(f.name[0] != '\0');

        const PresetData d = PresetData::parse(f.text);
        CHECK_MSG(d.name == f.name, "%s/%s: name= line does not match its file name (got \"%s\")",
                  f.category, f.name, d.name.c_str());
        CHECK_MSG(d.category == f.category, "%s/%s: category= line does not match its folder",
                  f.category, f.name);

        Envelope envA, envB;
        CHECK_MSG(envA.deserialize(d.envA), "%s/%s: envA does not deserialize", f.category, f.name);
        CHECK_MSG(envB.deserialize(d.envB), "%s/%s: envB does not deserialize", f.category, f.name);

        for (int p = 0; p < kick::kParamCount; ++p)
            CHECK(d.params[p] >= kick::kParams[p].min && d.params[p] <= kick::kParams[p].max);
        CHECK(!d.has[kick::kParamBypass]);

        const std::string id = std::string(f.category) + "/" + f.name;
        CHECK_MSG(seenIds.insert(id).second, "duplicate factory preset id: %s", id.c_str());
    }
}

TEST_CASE(factory_every_shape_parses_and_deserializes)
{
    std::set<std::string> seenIds;
    static const char* kShapeCats[] = {"Sidechain", "Rhythmic", "Simple"};
    for (int i = 0; i < kick::factory::kShapeCount; ++i) {
        const auto& f = kick::factory::kShapes[i];
        CHECK(f.category[0] != '\0');
        CHECK(f.name[0] != '\0');

        bool knownCategory = false;
        for (const char* c : kShapeCats)
            if (std::string(f.category) == c)
                knownCategory = true;
        CHECK_MSG(knownCategory, "unexpected factory shape category: %s", f.category);

        // The .kkshape text has an "env=KE1 ..." line; extract it the same way Shapes.cpp does
        // (find the line, take the value) rather than depending on that internal parser, so this
        // test also catches an author typo like a missing "env=" key.
        const std::string text(f.text);
        const size_t envKeyPos = text.find("\nenv=");
        CHECK_MSG(envKeyPos != std::string::npos, "%s/%s: no env= line found", f.category, f.name);
        if (envKeyPos == std::string::npos)
            continue;
        size_t start = envKeyPos + 5; // skip "\nenv="
        size_t end   = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string envText = text.substr(start, end - start);

        Envelope env;
        CHECK_MSG(env.deserialize(envText), "%s/%s: env= value does not deserialize", f.category, f.name);
        CHECK(env.size() <= Envelope::kMaxNodes);
        // Keep each shape's node budget documented in the design (<= 24 nodes).
        CHECK_MSG(env.size() <= 24, "%s/%s: %d nodes > 24", f.category, f.name, env.size());

        const std::string id = std::string(f.category) + "/" + f.name;
        CHECK_MSG(seenIds.insert(id).second, "duplicate factory shape id: %s", id.c_str());
    }
}

TEST_CASE(factory_sidechain_shapes_start_ducked_and_recover_to_unity)
{
    for (int i = 0; i < kick::factory::kShapeCount; ++i) {
        const auto& f = kick::factory::kShapes[i];
        if (std::string(f.category) != "Sidechain")
            continue;
        const std::string text(f.text);
        const size_t envKeyPos = text.find("\nenv=");
        if (envKeyPos == std::string::npos)
            continue;
        const size_t start = envKeyPos + 5;
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        Envelope env;
        CHECK(env.deserialize(text.substr(start, end - start)));
        CHECK_MSG(env.evaluate(0.0f) < 0.55f, "%s: does not start ducked (y=%.3f)", f.name, env.evaluate(0.0f));
        CHECK_MSG(env.node(env.size() - 1).y > 0.99f, "%s: does not recover to unity", f.name);
    }
}

TEST_CASE(factory_preset_store_and_shape_library_see_all_factory_content)
{
    kick::PresetStore presets(kpt::scratchDir("factory_via_store"));
    presets.rescan();
    int factoryCount = 0;
    for (const auto& e : presets.entries())
        if (e.isFactory)
            ++factoryCount;
    CHECK(factoryCount == kick::factory::kPresetCount);

    kick::ShapeLibrary shapes(kpt::scratchDir("factory_via_shapelib"));
    shapes.rescan();
    int shapeCount = 0;
    for (const char* cat : {"Sidechain", "Rhythmic", "Simple"})
        shapeCount += int(shapes.entries(cat).size());
    CHECK(shapeCount == kick::factory::kShapeCount);
}
