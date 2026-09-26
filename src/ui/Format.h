// Kickarse UI — value formatting and parameter mapping (DESIGN.md §4.3). Pure functions.
#pragma once

#include <string>

#include "shared/Params.h"

namespace kick { namespace ui {

// Normalised [0,1] <-> plain value, honouring kPFLog (log mapping) from Params.h.
float toNorm(int id, float plain) noexcept;
float fromNorm(int id, float norm) noexcept;
float clampParam(int id, float v) noexcept;
bool  isStepped(int id) noexcept;   // enum / integer / bool

// Display text for a parameter value ("−24.0 dB", "150 Hz", "Mid + Side", ...). Minus signs are
// U+2212. noteC3 selects the MIDI note naming convention (true: C3 = 60, false: C5 = 60).
std::string formatParam(int id, float v, bool noteC3 = true);
std::string noteName(int note, bool noteC3 = true);
const char* paramLabel(int id);  // short UI name used by hints ("Depth", "Low mix", ...)

// Parse typed text for a parameter ("2.5k" -> 2500, "-6" -> -6). false if not a number.
bool parseValue(const std::string& text, float& out) noexcept;

// "−" (U+2212) instead of '-' in a formatted number string.
std::string niceMinus(std::string s);

}} // namespace kick::ui
