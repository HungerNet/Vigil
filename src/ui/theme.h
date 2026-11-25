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

const std::vector<Theme>& themes();
const std::vector<std::pair<const char*, Color>>& accentChoices();  // index 0 = theme default

// Active theme with user accent applied.
const Theme& theme();
void setTheme(int index, int accentChoice);

}  // namespace vigil::ui
