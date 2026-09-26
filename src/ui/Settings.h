// Kickarse UI — global UI settings, persisted in <Documents>\Kickarse\settings.txt (not per
// project): UI size, MIDI note naming, pop-up tooltips, hint line, shape favourites.
#pragma once

#include <set>
#include <string>

namespace kick { namespace ui {

struct Settings {
    float scale     = 1.f;    // 1.0 … 2.0, user UI size on top of the host's DPI scale
    bool  noteC3    = true;   // true: C3 = 60 (Ableton/Bitwig), false: C5 = 60 (FL Studio)
    bool  tooltips  = false;  // pop-up tooltips (the hint line is always on)
    bool  animate   = true;   // shape morphs
    std::set<std::string> favouriteShapes;  // ShapeEntry ids

    static std::string path();
    void load();
    void save() const;
};

}} // namespace kick::ui
