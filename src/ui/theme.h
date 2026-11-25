#pragma once
// Vigil UI — theme system. A theme is a complete design specification (surfaces, elevation
// strategy, radii, backdrop, accent, severity and chart palettes), not a color swap.

#include "gfx.h"

#include <string>
#include <vector>

namespace vigil::ui {

struct Theme {
    const char* name = "";
    const char* description = "";
    bool light = false;

    Color backdropTint;
    float backdropBlur = 1.f;

    Color windowTop, windowBottom;  // window surface gradient
    Color rail;
    Color surface;        // cards
    Color surfaceRaised;  // popovers, selected rows
    Color surfaceHover;
    Color inset;          // tracks, fields, wells
    Color border;
    Color borderStrong;
    Color divider;
    Color highlight;      // top-edge light catch on raised surfaces

    Color text, textMuted, textFaint, textOnAccent;
    Color accent;
    Color sevClear, sevNotice, sevAlert, sevCritical;
    Color chart[6];

    float radiusWindow = 18.f;
    float radiusCard = 14.f;
    float radiusControl = 9.f;
    float shadowStrength = 1.f;   // 0 disables elevation shadows (hairline themes)
    float glow = 0.5f;            // accent glow intensity

    Color accentSoft() const { return accent.withAlpha(light ? 0.12f : 0.14f); }
    Color severity(float confidence) const;   // 0..100
};

// Parameters of the Custom theme (Settings → Appearance → Theme editor).
struct CustomTheme {
    int base = 0;            // built-in theme it starts from
    float hue = 228.f;       // accent hue, degrees
    float tint = 0.3f;       // how much the surfaces lean toward the accent hue (0..1)
    float contrast = 1.f;    // 0.8 softer … 1.4 crisper
    float radius = 1.f;      // corner radius scale
    float glow = 0.5f;
};

const std::vector<Theme>& themes();                                  // built-in themes
const std::vector<std::pair<const char*, Color>>& accentChoices();  // index 0 = theme default
Theme deriveCustomTheme(const CustomTheme& c);
Color hsv(float hueDeg, float s, float v);

// Active theme with user accent applied. `index == themes().size()` selects the Custom theme.
const Theme& theme();
void setTheme(int index, int accentChoice, const CustomTheme& custom);

}  // namespace vigil::ui
