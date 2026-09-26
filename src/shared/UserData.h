// Kickarse — user data folder layout, UTF-8/filesystem path helpers and small file I/O helpers
// shared by Presets.cpp and Shapes.cpp. UI-thread only; the audio thread never touches the
// filesystem (see docs/ARCHITECTURE.md, "Threads & data flow").
//
// Layout: <Documents>\Kickarse\Presets\<Category>\*.kkpreset, \Shapes\*.kkshape,
// \favourites.txt. Documents is resolved via SHGetKnownFolderPath(FOLDERID_Documents); this
// works fine when Documents is OneDrive-redirected, which it is on the dev machine.
#pragma once

#include <filesystem>
#include <string>

namespace kick {

class UserData {
public:
    // <Documents>\Kickarse (UTF-8). Honours setRootOverride() when set.
    static std::string root();

    static std::string presetsDir();                              // root()\Presets
    static std::string presetsDir(const std::string& category);    // root()\Presets\<Category>
    static std::string shapesDir();                                // root()\Shapes
    static std::string favouritesFile();                           // root()\favourites.txt

    // Test hook: redirects root() under a scratch folder so tests never touch the real Documents
    // folder. Pass "" to clear the override and go back to the real Documents-based root. Not
    // used by the shipping plugin. PresetStore/ShapeLibrary also accept an explicit root in their
    // constructor, which does not need this override at all (preferred for parallel/isolated
    // tests); this global hook remains for code that calls UserData::root() directly.
    static void setRootOverride(const std::string& utf8Path);
    static std::string rootOverride();  // "" when not overridden

    // Creates the directory (and any missing parents). Returns true if it exists afterwards
    // (including when it already did).
    static bool ensureDir(const std::string& utf8Path);

    // UTF-8 <-> native filesystem path. On Windows, std::filesystem::path stores UTF-16
    // internally; round-tripping through a narrow (ANSI codepage) string would corrupt non-ASCII
    // names, so every path in this module is converted through these two functions instead of
    // std::filesystem::path's narrow-string constructor/string().
    static std::filesystem::path toPath(const std::string& utf8);
    static std::string           toUtf8(const std::filesystem::path& path);

    // Whole-file UTF-8 text I/O, no exceptions. false on any failure to open/read/write.
    static bool readFile(const std::string& utf8Path, std::string& outText);
    static bool writeFile(const std::string& utf8Path, const std::string& text);
};

} // namespace kick
