#include "theme.h"

#include <algorithm>

namespace vigil::ui {
namespace {

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
    const std::uint32_t chart[6] = {0x8FA8FF, 0x5CC8B2, 0xE8B35A, 0xF0766A, 0xB48CFF, 0x5EB8FF};
    for (int i = 0; i < 6; ++i) t.chart[i] = Color::hex(chart[i]);
    return t;
}

Theme vesper() {
    Theme t = midnight();
    t.name = "Vesper";
    t.description = "Warm espresso and candle gold — the vigil light";
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
    const std::uint32_t chart[6] = {0xE2B866, 0x8CC7A1, 0xD98E5F, 0xE8746A, 0xC9A7E8, 0x86B6D9};
    for (int i = 0; i < 6; ++i) t.chart[i] = Color::hex(chart[i]);
    t.glow = 0.65f;
    return t;
}

Theme obsidian() {
    Theme t = midnight();
    t.name = "Obsidian";
    t.description = "True black, hairlines, no shadows — maximum contrast";
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
    t.radiusWindow = 14.f;
    t.radiusCard = 10.f;
    t.radiusControl = 7.f;
    t.shadowStrength = 0.f;
    t.glow = 0.3f;
    const std::uint32_t chart[6] = {0x4ADE9F, 0xF5F5F5, 0xFACC15, 0xFB7185, 0xA78BFA, 0x60A5FA};
    for (int i = 0; i < 6; ++i) t.chart[i] = Color::hex(chart[i]);
    return t;
}

Theme glacier() {
    Theme t;
    t.name = "Glacier";
    t.description = "Cool paper tones and deep iris — a light workspace";
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
    const std::uint32_t chart[6] = {0x4C5FD7, 0x13917B, 0xB7791F, 0xD2513F, 0x8B5CF6, 0x0EA5E9};
    for (int i = 0; i < 6; ++i) t.chart[i] = Color::hex(chart[i]);
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
    const std::uint32_t chart[6] = {0x5EE0F0, 0xB48CFF, 0xF5C26B, 0xFF7A90, 0x7CF29A, 0x8FA8FF};
    for (int i = 0; i < 6; ++i) t.chart[i] = Color::hex(chart[i]);
    t.glow = 0.9f;
    return t;
}

int g_theme = 0;
int g_accent = 0;
Theme g_active = midnight();

}  // namespace

Color Theme::severity(float c) const {
    c = std::clamp(c, 0.f, 100.f);
    if (c < 35.f) return sevClear.mix(sevNotice, c / 35.f);
    if (c < 70.f) return sevNotice.mix(sevAlert, (c - 35.f) / 35.f);
    return sevAlert.mix(sevCritical, (c - 70.f) / 30.f);
}

const std::vector<Theme>& themes() {
    static const std::vector<Theme> all = {midnight(), vesper(), obsidian(), glacier(), aurora()};
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

const Theme& theme() { return g_active; }

void setTheme(int index, int accentChoice) {
    const auto& all = themes();
    index = std::clamp(index, 0, static_cast<int>(all.size()) - 1);
    accentChoice = std::clamp(accentChoice, 0, static_cast<int>(accentChoices().size()) - 1);
    if (index == g_theme && accentChoice == g_accent && g_active.name == all[static_cast<std::size_t>(index)].name) return;
    g_theme = index;
    g_accent = accentChoice;
    g_active = all[static_cast<std::size_t>(index)];
    if (accentChoice > 0) {
        Color a = accentChoices()[static_cast<std::size_t>(accentChoice)].second;
        if (g_active.light) a = a.darker(0.28f);  // keep contrast on light surfaces
        g_active.accent = a;
        g_active.chart[0] = a;
    }
}

}  // namespace vigil::ui
