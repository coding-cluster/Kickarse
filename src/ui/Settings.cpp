// Kickarse UI — settings file I/O (see Settings.h). Plain "key=value" lines, one shape favourite
// per "fav=<id>" line; unknown keys are ignored so older/newer builds can share the file.
#include "Settings.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "shared/UserData.h"

namespace kick { namespace ui {

std::string Settings::path()
{
    return UserData::root() + "\\settings.txt";
}

void Settings::load()
{
    std::string text;
    if (!UserData::readFile(path(), text))
        return;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#')
            continue;
        const std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        if (key == "scale")
            scale = std::clamp(float(std::atof(val.c_str())), 1.f, 2.f);
        else if (key == "noteNaming")
            noteC3 = val != "C5";
        else if (key == "tooltips")
            tooltips = val == "1";
        else if (key == "animate")
            animate = val != "0";
        else if (key == "fav" && !val.empty())
            favouriteShapes.insert(val);
    }
}

void Settings::save() const
{
    UserData::ensureDir(UserData::root());
    std::ostringstream out;
    out << "# Kickarse UI settings\n";
    out << "scale=" << scale << "\n";
    out << "noteNaming=" << (noteC3 ? "C3" : "C5") << "\n";
    out << "tooltips=" << (tooltips ? 1 : 0) << "\n";
    out << "animate=" << (animate ? 1 : 0) << "\n";
    for (const std::string& f : favouriteShapes)
        out << "fav=" << f << "\n";
    UserData::writeFile(path(), out.str());
}

}} // namespace kick::ui
