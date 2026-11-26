#include "theme.h"

#include "anim.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vigil::ui {
namespace {

void setChart(Theme& t, const std::uint32_t (&c)[6]) {
    for (int i = 0; i < 6; ++i) t.chart[i] = Color::hex(c[i]);
}

Theme midnight() {
    Theme t;
    t.name = "Midnight";
    t.description = "Ink graphite, iris accent, frosted backdrop";
    t.backdropTint = Color::hex(0x05070B, 0.58f);
    t.windowTop = Color::hex(0x131722);
    t.windowBottom = Color::hex(0x0E1117);
    t.rail = Color::hex(0x0B0E13, 0.72f);
    t.surface = Color::hex(0x161A23);
    t.surfaceRaised = Color::hex(0x1C212C);
    t.surfaceHover = Color::hex(0x1F2531);
    t.inset = Color::hex(0x0B0E14);
    t.border = Color::hex(0xFFFFFF, 0.06f);
    t.borderStrong = Color::hex(0xFFFFFF, 0.11f);
    t.divider = Color::hex(0xFFFFFF, 0.05f);
    t.highlight = Color::hex(0xFFFFFF, 0.05f);
    t.text = Color::hex(0xE8ECF3);
    t.textMuted = Color::hex(0x9AA3B2);
    t.textFaint = Color::hex(0x5F6878);
    t.textOnAccent = Color::hex(0x0B0E14);
    t.accent = Color::hex(0x8FA8FF);
    t.sevClear = Color::hex(0x5CC8B2);
    t.sevNotice = Color::hex(0xE8B35A);
    t.sevAlert = Color::hex(0xF0766A);
    t.sevCritical = Color::hex(0xE5484D);
    t.verdict = Color::hex(0xC98BFF);
    setChart(t, {0x8FA8FF, 0x5CC8B2, 0xE8B35A, 0xF0766A, 0xB48CFF, 0x5EB8FF});
    return t;
}

Theme vesper() {
    Theme t = midnight();
    t.name = "Vesper";
    t.description = "Warm espresso and candle gold \xE2\x80\x94 the vigil light";
    t.backdropTint = Color::hex(0x0C0804, 0.56f);
    t.windowTop = Color::hex(0x1A1612);
    t.windowBottom = Color::hex(0x120F0C);
    t.rail = Color::hex(0x0F0C0A, 0.7f);
    t.surface = Color::hex(0x1D1915);
    t.surfaceRaised = Color::hex(0x241F1A);
    t.surfaceHover = Color::hex(0x2A241E);
    t.inset = Color::hex(0x0E0B09);
    t.border = Color::hex(0xFFE8C8, 0.07f);
    t.borderStrong = Color::hex(0xFFE8C8, 0.12f);
    t.divider = Color::hex(0xFFE8C8, 0.05f);
    t.highlight = Color::hex(0xFFE8C8, 0.05f);
    t.text = Color::hex(0xF2EADF);
    t.textMuted = Color::hex(0xB4A694);
    t.textFaint = Color::hex(0x7A6E61);
    t.textOnAccent = Color::hex(0x1A1208);
    t.accent = Color::hex(0xE2B866);
    t.sevClear = Color::hex(0x8CC7A1);
    setChart(t, {0xE2B866, 0x8CC7A1, 0xD98E5F, 0xE8746A, 0xC9A7E8, 0x86B6D9});
    t.glow = 0.65f;
    return t;
}

Theme obsidian() {
    Theme t = midnight();
    t.name = "Obsidian";
    t.description = "True black, hairlines, no shadows \xE2\x80\x94 maximum contrast";
    t.backdropTint = Color::hex(0x000000, 0.74f);
    t.backdropBlur = 0.6f;
    t.windowTop = Color::hex(0x050505);
    t.windowBottom = Color::hex(0x000000);
    t.rail = Color::hex(0x000000, 0.0f);
    t.surface = Color::hex(0x0A0A0A);
    t.surfaceRaised = Color::hex(0x121212);
    t.surfaceHover = Color::hex(0x161616);
    t.inset = Color::hex(0x050505);
    t.border = Color::hex(0xFFFFFF, 0.11f);
    t.borderStrong = Color::hex(0xFFFFFF, 0.2f);
    t.divider = Color::hex(0xFFFFFF, 0.09f);
    t.highlight = Color::hex(0xFFFFFF, 0.0f);
    t.text = Color::hex(0xF5F5F5);
    t.textMuted = Color::hex(0xA3A3A3);
    t.textFaint = Color::hex(0x6B6B6B);
    t.textOnAccent = Color::hex(0x000000);
    t.accent = Color::hex(0x4ADE9F);
    t.verdict = Color::hex(0xC084FC);
    t.radiusWindow = 14.f;
    t.radiusCard = 10.f;
    t.radiusControl = 7.f;
    t.shadowStrength = 0.f;
    t.glow = 0.3f;
    setChart(t, {0x4ADE9F, 0xF5F5F5, 0xFACC15, 0xFB7185, 0xA78BFA, 0x60A5FA});
    return t;
}

Theme glacier() {
    Theme t;
    t.name = "Glacier";
    t.description = "Cool paper tones and deep iris \xE2\x80\x94 a light workspace";
    t.light = true;
    t.backdropTint = Color::hex(0xE9EDF3, 0.62f);
    t.windowTop = Color::hex(0xFAFBFC);
    t.windowBottom = Color::hex(0xF3F5F8);
    t.rail = Color::hex(0xEDF0F5, 0.85f);
    t.surface = Color::hex(0xFFFFFF);
    t.surfaceRaised = Color::hex(0xFFFFFF);
    t.surfaceHover = Color::hex(0xF1F3F7);
    t.inset = Color::hex(0xE9EDF3);
    t.border = Color::hex(0x101828, 0.08f);
    t.borderStrong = Color::hex(0x101828, 0.14f);
    t.divider = Color::hex(0x101828, 0.06f);
    t.highlight = Color::hex(0xFFFFFF, 0.6f);
    t.text = Color::hex(0x121722);
    t.textMuted = Color::hex(0x566073);
    t.textFaint = Color::hex(0x8A93A3);
    t.textOnAccent = Color::hex(0xFFFFFF);
    t.accent = Color::hex(0x4C5FD7);
    t.sevClear = Color::hex(0x13917B);
    t.sevNotice = Color::hex(0xB7791F);
    t.sevAlert = Color::hex(0xD2513F);
    t.sevCritical = Color::hex(0xC9302C);
    t.verdict = Color::hex(0x8A3FD1);
    setChart(t, {0x4C5FD7, 0x13917B, 0xB7791F, 0xD2513F, 0x8B5CF6, 0x0EA5E9});
    t.shadowStrength = 0.45f;
    t.glow = 0.2f;
    return t;
}

Theme aurora() {
    Theme t = midnight();
    t.name = "Aurora";
    t.description = "Deep blue-violet depth with a cyan signal";
    t.backdropTint = Color::hex(0x08081A, 0.55f);
    t.windowTop = Color::hex(0x171B3A);
    t.windowBottom = Color::hex(0x0C0E20);
    t.rail = Color::hex(0x0A0C1C, 0.6f);
    t.surface = Color::hex(0x161A36);
    t.surfaceRaised = Color::hex(0x1D2244);
    t.surfaceHover = Color::hex(0x232951);
    t.inset = Color::hex(0x0A0C1A);
    t.border = Color::hex(0xA0B4FF, 0.10f);
    t.borderStrong = Color::hex(0xA0B4FF, 0.18f);
    t.divider = Color::hex(0xA0B4FF, 0.07f);
    t.highlight = Color::hex(0xC8D4FF, 0.07f);
    t.text = Color::hex(0xEEF0FF);
    t.textMuted = Color::hex(0xA5ABD6);
    t.textFaint = Color::hex(0x6B71A0);
    t.textOnAccent = Color::hex(0x051018);
    t.accent = Color::hex(0x5EE0F0);
    t.verdict = Color::hex(0xE48AFF);
    setChart(t, {0x5EE0F0, 0xB48CFF, 0xF5C26B, 0xFF7A90, 0x7CF29A, 0x8FA8FF});
    t.glow = 0.9f;
    return t;
}

// Deep forest floor with a chartreuse shoot — calm, organic, unmistakably not blue.
Theme verdant() {
    Theme t = midnight();
    t.name = "Verdant";
    t.description = "Forest floor and a fresh chartreuse signal";
    t.backdropTint = Color::hex(0x050A07, 0.6f);
    t.windowTop = Color::hex(0x131B16);
    t.windowBottom = Color::hex(0x0C120F);
    t.rail = Color::hex(0x09100C, 0.72f);
    t.surface = Color::hex(0x16201A);
    t.surfaceRaised = Color::hex(0x1C2821);
    t.surfaceHover = Color::hex(0x213026);
    t.inset = Color::hex(0x0A100D);
    t.border = Color::hex(0xD6F5E0, 0.07f);
    t.borderStrong = Color::hex(0xD6F5E0, 0.12f);
    t.divider = Color::hex(0xD6F5E0, 0.05f);
    t.highlight = Color::hex(0xD6F5E0, 0.05f);
    t.text = Color::hex(0xE7F2EA);
    t.textMuted = Color::hex(0x9DB4A6);
    t.textFaint = Color::hex(0x5F7568);
    t.textOnAccent = Color::hex(0x0E1606);
    t.accent = Color::hex(0xC5E27A);
    t.sevClear = Color::hex(0x6FD0A5);
    t.sevNotice = Color::hex(0xE6C15A);
    setChart(t, {0xC5E27A, 0x6FD0A5, 0xE6C15A, 0xF08A6E, 0xB9A3F0, 0x7FC4E8});
    t.radiusWindow = 20.f;
    t.radiusCard = 16.f;
    t.glow = 0.55f;
    return t;
}

// Twilight plum with a peach horizon — soft and warm for long sessions.
Theme dusk() {
    Theme t = midnight();
    t.name = "Dusk";
    t.description = "Twilight plum with a peach horizon";
    t.backdropTint = Color::hex(0x0B060C, 0.58f);
    t.windowTop = Color::hex(0x1F1722);
    t.windowBottom = Color::hex(0x150F17);
    t.rail = Color::hex(0x120C13, 0.7f);
    t.surface = Color::hex(0x221A25);
    t.surfaceRaised = Color::hex(0x2A202D);
    t.surfaceHover = Color::hex(0x302533);
    t.inset = Color::hex(0x110C12);
    t.border = Color::hex(0xFFD9EA, 0.07f);
    t.borderStrong = Color::hex(0xFFD9EA, 0.12f);
    t.divider = Color::hex(0xFFD9EA, 0.05f);
    t.highlight = Color::hex(0xFFD9EA, 0.06f);
    t.text = Color::hex(0xF4EAF0);
    t.textMuted = Color::hex(0xBBA6B5);
    t.textFaint = Color::hex(0x7F6B7A);
    t.textOnAccent = Color::hex(0x1E0E08);
    t.accent = Color::hex(0xFFA27F);
    t.sevClear = Color::hex(0x8ED1B8);
    t.sevNotice = Color::hex(0xF2C46B);
    t.sevAlert = Color::hex(0xFF7C7C);
    t.sevCritical = Color::hex(0xF04A5E);
    setChart(t, {0xFFA27F, 0x8ED1B8, 0xF2C46B, 0xFF7C7C, 0xC9A0F2, 0x86B8F0});
    t.glow = 0.7f;
    return t;
}

// Warm editorial paper with an ink-and-vermilion accent; a second, warmer light theme.
Theme paper() {
    Theme t;
    t.name = "Paper";
    t.description = "Warm editorial paper, ink and vermilion";
    t.light = true;
    t.backdropTint = Color::hex(0xEFE9DE, 0.66f);
    t.backdropBlur = 0.9f;
    t.windowTop = Color::hex(0xFBF8F2);
    t.windowBottom = Color::hex(0xF5F0E7);
    t.rail = Color::hex(0xEFE8DC, 0.9f);
    t.surface = Color::hex(0xFFFDF8);
    t.surfaceRaised = Color::hex(0xFFFFFF);
    t.surfaceHover = Color::hex(0xF3EDE2);
    t.inset = Color::hex(0xEDE6DA);
    t.border = Color::hex(0x2B2118, 0.09f);
    t.borderStrong = Color::hex(0x2B2118, 0.16f);
    t.divider = Color::hex(0x2B2118, 0.07f);
    t.highlight = Color::hex(0xFFFFFF, 0.7f);
    t.text = Color::hex(0x1F1A14);
    t.textMuted = Color::hex(0x6B5F52);
    t.textFaint = Color::hex(0x9A8E80);
    t.textOnAccent = Color::hex(0xFFFFFF);
    t.accent = Color::hex(0xC4472E);
    t.sevClear = Color::hex(0x2E8A5E);
    t.sevNotice = Color::hex(0xAF7A1C);
    t.sevAlert = Color::hex(0xC4472E);
    t.sevCritical = Color::hex(0xA2201F);
    t.verdict = Color::hex(0x7A3E9D);
    setChart(t, {0xC4472E, 0x2E8A5E, 0xAF7A1C, 0x3F5FA8, 0x8A4FA0, 0x2C8C9C});
    t.radiusWindow = 14.f;
    t.radiusCard = 10.f;
    t.radiusControl = 7.f;
    t.shadowStrength = 0.35f;
    t.glow = 0.f;
    return t;
}

// High contrast: pure black, white type, signal yellow. Designed for readability first.
Theme signal() {
    Theme t = midnight();
    t.name = "Signal";
    t.description = "High contrast for clarity and accessibility";
    t.backdropTint = Color::hex(0x000000, 0.86f);
    t.backdropBlur = 0.4f;
    t.windowTop = Color::hex(0x000000);
    t.windowBottom = Color::hex(0x000000);
    t.rail = Color::hex(0x000000, 0.f);
    t.surface = Color::hex(0x0B0B0B);
    t.surfaceRaised = Color::hex(0x161616);
    t.surfaceHover = Color::hex(0x222222);
    t.inset = Color::hex(0x000000);
    t.border = Color::hex(0xFFFFFF, 0.30f);
    t.borderStrong = Color::hex(0xFFFFFF, 0.48f);
    t.divider = Color::hex(0xFFFFFF, 0.22f);
    t.highlight = Color::hex(0xFFFFFF, 0.f);
    t.text = Color::hex(0xFFFFFF);
    t.textMuted = Color::hex(0xD6D6D6);
    t.textFaint = Color::hex(0xA8A8A8);
    t.textOnAccent = Color::hex(0x000000);
    t.accent = Color::hex(0xFFD400);
    t.sevClear = Color::hex(0x00E676);
    t.sevNotice = Color::hex(0xFFD400);
    t.sevAlert = Color::hex(0xFF8A00);
    t.sevCritical = Color::hex(0xFF2D55);
    t.verdict = Color::hex(0xE040FB);
    setChart(t, {0xFFD400, 0x00E676, 0x40C4FF, 0xFF8A00, 0xE040FB, 0xFFFFFF});
    t.radiusWindow = 12.f;
    t.radiusCard = 8.f;
    t.radiusControl = 6.f;
    t.shadowStrength = 0.f;
    t.glow = 0.f;
    return t;
}

int g_theme = -1;
int g_accent = -1;
CustomTheme g_custom;
Theme g_active = midnight();   // what is on screen (interpolated during a transition)
Theme g_from = g_active;       // what was on screen when the change began
Theme g_target = g_active;     // where the transition is going
bool g_pendingStart = false;
bool g_transitioning = false;
double g_changeAt = 0.0;
double g_lastChangeAt = -10.0;
float g_duration = 0.42f;

bool sameCustom(const CustomTheme& a, const CustomTheme& b) { return std::memcmp(&a, &b, sizeof(CustomTheme)) == 0; }

Color lerp(const Color& a, const Color& b, float t) { return a.mix(b, t); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }

}  // namespace

Theme blendThemes(const Theme& a, const Theme& b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    Theme o = b;
    o.light = t < 0.5f ? a.light : b.light;
    Color Theme::*colors[] = {&Theme::backdropTint, &Theme::windowTop, &Theme::windowBottom, &Theme::rail, &Theme::surface,
                              &Theme::surfaceRaised, &Theme::surfaceHover, &Theme::inset, &Theme::border, &Theme::borderStrong,
                              &Theme::divider, &Theme::highlight, &Theme::text, &Theme::textMuted, &Theme::textFaint,
                              &Theme::textOnAccent, &Theme::accent, &Theme::sevClear, &Theme::sevNotice, &Theme::sevAlert,
                              &Theme::sevCritical, &Theme::verdict};
    for (Color Theme::*c : colors) o.*c = lerp(a.*c, b.*c, t);
    for (int i = 0; i < 6; ++i) o.chart[i] = lerp(a.chart[i], b.chart[i], t);
    float Theme::*floats[] = {&Theme::backdropBlur, &Theme::radiusWindow, &Theme::radiusCard, &Theme::radiusControl,
                              &Theme::shadowStrength, &Theme::glow};
    for (float Theme::*f : floats) o.*f = lerp(a.*f, b.*f, t);
    return o;
}

Color hsv(float h, float s, float v) {
    h = std::fmod(std::fmod(h, 360.f) + 360.f, 360.f) / 60.f;
    const int i = static_cast<int>(h);
    const float f = h - i, p = v * (1 - s), q = v * (1 - s * f), u = v * (1 - s * (1 - f));
    switch (i) {
        case 0: return {v, u, p};
        case 1: return {q, v, p};
        case 2: return {p, v, u};
        case 3: return {p, q, v};
        case 4: return {u, p, v};
        default: return {v, p, q};
    }
}

Color Theme::severity(float c) const {
    c = std::clamp(c, 0.f, 100.f);
    const Color elevated = sevNotice.mix(sevAlert, 0.45f);
    if (c < 15.f) return sevClear;
    if (c < 40.f) return sevClear.mix(sevNotice, (c - 15.f) / 25.f);
    if (c < 65.f) return sevNotice.mix(elevated, (c - 40.f) / 25.f);
    if (c < 85.f) return elevated.mix(sevAlert, (c - 65.f) / 20.f);
    return sevAlert.mix(sevCritical, (c - 85.f) / 15.f);
}

const std::vector<Theme>& themes() {
    static const std::vector<Theme> all = {midnight(), vesper(), obsidian(), glacier(), aurora(),
                                           verdant(), dusk(), paper(), signal()};
    return all;
}

const std::vector<std::pair<const char*, Color>>& accentChoices() {
    static const std::vector<std::pair<const char*, Color>> a = {
        {"Theme", Color{}},
        {"Iris", Color::hex(0x8FA8FF)},
        {"Gold", Color::hex(0xE2B866)},
        {"Mint", Color::hex(0x4ADE9F)},
        {"Coral", Color::hex(0xFF8A73)},
        {"Violet", Color::hex(0xB48CFF)},
        {"Sky", Color::hex(0x5EB8FF)},
    };
    return a;
}

Theme deriveCustomTheme(const CustomTheme& c) {
    const auto& all = themes();
    Theme t = all[static_cast<std::size_t>(std::clamp(c.base, 0, static_cast<int>(all.size()) - 1))];
    t.name = "Custom";
    t.description = "Your own variation";
    const Color hueColor = t.light ? hsv(c.hue, 0.62f, 0.72f) : hsv(c.hue, 0.48f, 1.f);
    t.accent = hueColor;
    t.chart[0] = hueColor;
    t.textOnAccent = t.light ? Color::hex(0xFFFFFF) : hsv(c.hue, 0.6f, 0.08f);
    // Surfaces lean toward the hue.
    const float k = std::clamp(c.tint, 0.f, 1.f) * (t.light ? 0.05f : 0.09f);
    const Color tintTo = t.light ? hsv(c.hue, 0.25f, 1.f) : hsv(c.hue, 0.6f, 0.35f);
    for (Color* s : {&t.windowTop, &t.windowBottom, &t.surface, &t.surfaceRaised, &t.surfaceHover, &t.inset}) {
        const float a = s->a;
        *s = s->mix(tintTo, k);
        s->a = a;
    }
    // Contrast: borders and secondary text move toward or away from the primary text.
    const float ct = std::clamp(c.contrast, 0.8f, 1.4f);
    t.border.a = std::clamp(t.border.a * ct, 0.f, 0.6f);
    t.borderStrong.a = std::clamp(t.borderStrong.a * ct, 0.f, 0.7f);
    t.divider.a = std::clamp(t.divider.a * ct, 0.f, 0.5f);
    if (ct > 1.f) {
        t.textMuted = t.textMuted.mix(t.text, (ct - 1.f) * 0.8f);
        t.textFaint = t.textFaint.mix(t.textMuted, (ct - 1.f) * 0.8f);
    } else {
        t.textFaint = t.textFaint.mix(t.windowBottom, (1.f - ct) * 0.6f);
    }
    const float rs = std::clamp(c.radius, 0.5f, 1.5f);
    t.radiusWindow *= rs;
    t.radiusCard *= rs;
    t.radiusControl *= rs;
    t.glow = std::clamp(c.glow, 0.f, 1.f);
    return t;
}

const Theme& theme() { return g_active; }

Theme withHigherContrast(Theme t) {
    // Secondary and faint text move toward the primary text color, borders gain weight. Applied
    // on top of any theme (Settings → Accessibility → Text contrast), so every design stays itself.
    t.textMuted = t.textMuted.mix(t.text, 0.40f);
    t.textFaint = t.textFaint.mix(t.text, 0.45f);
    t.border = t.border.mix(t.text, 0.10f);
    t.borderStrong = t.borderStrong.mix(t.text, 0.16f);
    t.divider = t.divider.mix(t.text, 0.08f);
    return t;
}

void setTheme(int index, int accentChoice, const CustomTheme& custom, bool higherContrast) {
    const auto& all = themes();
    index = std::clamp(index, 0, static_cast<int>(all.size()));
    accentChoice = std::clamp(accentChoice, 0, static_cast<int>(accentChoices().size()) - 1);
    const bool isCustom = index == static_cast<int>(all.size());
    static bool s_contrast = false;
    if (index == g_theme && accentChoice == g_accent && higherContrast == s_contrast && (!isCustom || sameCustom(custom, g_custom))) return;
    const bool first = g_theme < 0;
    g_theme = index;
    g_accent = accentChoice;
    g_custom = custom;
    s_contrast = higherContrast;
    Theme next = isCustom ? deriveCustomTheme(custom) : all[static_cast<std::size_t>(index)];
    if (accentChoice > 0 && !isCustom) {
        Color a = accentChoices()[static_cast<std::size_t>(accentChoice)].second;
        if (next.light) a = a.darker(0.28f);  // keep contrast on light surfaces
        next.accent = a;
        next.chart[0] = a;
    }
    if (higherContrast) next = withHigherContrast(next);
    if (first) {
        g_active = g_from = g_target = next;
        return;
    }
    // Retarget from whatever is on screen, so a change in the middle of a transition never jumps.
    g_from = g_active;
    g_target = next;
    g_pendingStart = true;
    g_transitioning = true;
}

void tickTheme(double now, float motion) {
    if (!g_transitioning) return;
    if (g_pendingStart) {
        g_pendingStart = false;
        // Scrubbing (the theme editor's sliders) follows quickly; a deliberate change crossfades.
        g_duration = now - g_lastChangeAt < 0.25 ? 0.12f : 0.42f;
        g_changeAt = now;
        g_lastChangeAt = now;
    }
    const float k = motion <= 0.01f ? 1.f : static_cast<float>((now - g_changeAt) / (g_duration * (motion < 0.99f ? 0.6f : 1.f)));
    if (k >= 1.f) {
        g_active = g_target;
        g_transitioning = false;
        return;
    }
    g_active = blendThemes(g_from, g_target, ease::emphasized(std::max(0.f, k)));
}

bool themeTransitioning() { return g_transitioning; }

}  // namespace vigil::ui
