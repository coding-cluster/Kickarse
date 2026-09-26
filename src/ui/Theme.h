// Kickarse UI — design tokens. Every value here is quoted from docs/design/DESIGN.md (§2–§5) and
// the executable prototype (design/prototype/index.html). Geometry is in 1x logical pixels; the
// whole UI is drawn under one uniform scale (see View::paint).
#pragma once

#include <cstdint>

namespace kick { namespace ui {

struct Rgba {
    float r, g, b, a;
    constexpr Rgba withAlpha(float na) const noexcept { return {r, g, b, na}; }
};

constexpr Rgba hex(std::uint32_t rgb, float a = 1.f) noexcept
{
    return {float((rgb >> 16) & 0xFF) / 255.f, float((rgb >> 8) & 0xFF) / 255.f, float(rgb & 0xFF) / 255.f, a};
}

constexpr Rgba mix(const Rgba& x, const Rgba& y, float t) noexcept
{
    return {x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t};
}

namespace col {
// neutrals (warm graphite)
inline constexpr Rgba ink0     = hex(0x0C0C0B);
inline constexpr Rgba ink1     = hex(0x121211);
inline constexpr Rgba ink2     = hex(0x181816);
inline constexpr Rgba ink3     = hex(0x1E1D1B);
inline constexpr Rgba ink4     = hex(0x262523);
inline constexpr Rgba ink5     = hex(0x2F2E2B);
inline constexpr Rgba ink6     = hex(0x3A3935);
inline constexpr Rgba ink7     = hex(0x4A4843);
inline constexpr Rgba textDim  = hex(0x9E998F);
inline constexpr Rgba textMute = hex(0xB5B0A5);
inline constexpr Rgba text     = hex(0xD5D0C5);
inline constexpr Rgba textHi   = hex(0xF2EEE4);
// semantic hues: duck = the envelope / amount of ducking, high = the high band, kick = sidechain
inline constexpr Rgba duck     = hex(0xFFB81C);
inline constexpr Rgba duckHi   = hex(0xFFD066);
inline constexpr Rgba high     = hex(0x4DB2F5);
inline constexpr Rgba highHi   = hex(0x8FD0FA);
inline constexpr Rgba kick     = hex(0xFF4A3D);
// warm white for hairlines / light, and the "bone" used for waveform fills
inline constexpr Rgba warm     = hex(0xFFF8EB);
inline constexpr Rgba bone     = hex(0xF5F0E6);
inline constexpr Rgba black    = hex(0x000000);
inline constexpr Rgba white    = hex(0xFFFFFF);
} // namespace col

// Base canvas (DESIGN.md §2.1)
inline constexpr float kBaseW = 1080.f;
inline constexpr float kBaseH = 660.f;

// Knob angles: 135° start, 270° sweep, clockwise in a y-down space.
inline constexpr float kPi       = 3.14159265358979f;
inline constexpr float kKnobA0   = kPi * 0.75f;
inline constexpr float kKnobSweep = kPi * 1.5f;

// Fonts (resources/fonts, loaded by name from kick::res)
enum class Font { Sc, ScSemi, Exp };

// Editor geometry (DESIGN.md §2.2)
struct RectF {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    constexpr bool contains(float px, float py) const noexcept { return px >= x && px < x + w && py >= y && py < y + h; }
    constexpr float right() const noexcept { return x + w; }
    constexpr float bottom() const noexcept { return y + h; }
    constexpr float cx() const noexcept { return x + w * 0.5f; }
    constexpr float cy() const noexcept { return y + h * 0.5f; }
};

namespace layout {
inline constexpr RectF editorWell {256.f, 88.f, 612.f, 328.f};
inline constexpr RectF plot       {268.f, 100.f, 588.f, 272.f};
inline constexpr RectF labelStrip {268.f, 374.f, 588.f, 14.f};
inline constexpr RectF qsLane     {268.f, 392.f, 588.f, 16.f};
inline constexpr float leftX = 16.f, leftW = 212.f;     // left column content
inline constexpr float rightX = 896.f, rightW = 168.f;  // right column content
inline constexpr float modePanelY = 248.f;              // origin of the per-mode panel
} // namespace layout

}} // namespace kick::ui
