// Kickarse — envelope shape library: framework-independent .kkshape file I/O, factory + user
// content. UI-thread only (see docs/ARCHITECTURE.md); no exceptions escape.
//
// .kkshape file format (UTF-8 text, same key=value / '#' comment conventions as .kkpreset):
//   # Kickarse shape
//   version=1
//   name=Fast Snap
//   category=Sidechain
//   env=KE1 0,0,0,0;0.08,1,0,0;1,1,0,0
#pragma once

#include <string>
#include <vector>

#include "Envelope.h"

namespace kick {

// Fixed category order. Sidechain/Rhythmic/Simple are factory-only (compiled in from
// resources/shapes/**); User is disk-only (<root>\Shapes\*.kkshape, flat, no sub-folders).
inline constexpr const char* kShapeCategories[] = {"Sidechain", "Rhythmic", "Simple", "User"};
inline constexpr int kNumShapeCategories = int(sizeof(kShapeCategories) / sizeof(kShapeCategories[0]));

struct ShapeEntry {
    std::string id;        // "factory:<Category>/<Name>" or "user:<file name>"
    std::string name;
    std::string category;  // one of kShapeCategories
    bool        isFactory = false;
    Envelope    env;       // always a valid, normalised envelope (falls back to Envelope::flat())
};

class ShapeLibrary {
public:
    // userRoot overrides the "<Documents>\Kickarse" root for this library (tests only); ""
    // resolves it once via UserData::root(). rescan() is not called implicitly.
    explicit ShapeLibrary(std::string userRoot = {});

    // Reloads the compiled-in Sidechain/Rhythmic/Simple shapes and rescans <root>\Shapes\*.kkshape
    // for the User category.
    void rescan();

    // Entries for one category, sorted by name (case-insensitive); empty for an unknown category.
    std::vector<ShapeEntry> entries(const std::string& category) const;
    // All categories concatenated in kShapeCategories order (a UI convenience).
    std::vector<ShapeEntry> entries() const;

    // Sanitises `name` into a file name under <root>\Shapes\ and writes it; a colliding name is
    // made unique by appending " (2)", " (3)", ... (there is no overwrite flag: re-saving under
    // the same display name always creates another entry, like a "Save As"). Returns false only
    // on an I/O error.
    bool saveUser(const std::string& name, const Envelope& env, std::string* err = nullptr);
    bool removeUser(const std::string& id, std::string* err = nullptr);

private:
    std::string root_;
    std::vector<ShapeEntry> factory_[3]; // indexed like kShapeCategories[0..2]
    std::vector<ShapeEntry> user_;       // kShapeCategories[3] ("User")

    std::string shapesDir() const;
    void rescanFactory();
    void rescanUser();
};

} // namespace kick
