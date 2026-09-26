// Kickarse — declarations for the factory preset/shape text embedded by cmake/EmbedFactory.cmake
// into a generated KickarseFactory.cpp (added to kickarse_core via target_sources() in the
// top-level CMakeLists.txt; see the "Preset agent" block at its end). Internal to Presets.cpp /
// Shapes.cpp, not part of the public kick:: API.
#pragma once

namespace kick {
namespace factory {

struct FactoryFile {
    const char* category;  // resource sub-folder name, e.g. "Sidechain", "Ring Mod"
    const char* name;      // file stem, e.g. "Classic 4-on-the-Floor Pump"
    const char* text;      // full file contents (UTF-8), verbatim
};

// Both arrays are terminated by a sentinel {"", "", ""}; use the Count values below for the
// real length rather than relying on the sentinel (kept mainly so the arrays are never empty,
// which some compilers reject for a zero-size const array).
extern const FactoryFile kPresets[];
extern const int         kPresetCount;
extern const FactoryFile kShapes[];
extern const int         kShapeCount;

} // namespace factory
} // namespace kick
