// Settings — everything outside individual checks and utilities: monitoring scope, alerts,
// keybinds, Suspect Highlight, exemptions, hit attribution, identity tools, appearance (themes
// and the theme editor), data and about.

#include "core/version.h"
#include "pages.h"
#include "records.h"

#include <algorithm>
#include <cmath>

namespace vigil::ui {
namespace {

struct Section {
    const char* name;
    Icon icon;
    std::vector<const char*> groups;
    const char* blurb;
};

const std::vector<Section>& sections() {
    static const std::vector<Section> s = {
        {"General", Icon::Pulse, {"Monitoring", "Where checks run", "Calibration"}, "Where Vigil observes and how it calibrates."},
        {"Alerts", Icon::Bell, {"Alerts"}, "How detections reach you while you play."},
        {"Keybinds", Icon::Keyboard, {}, "Every shortcut in one place. Click a binding, then press the new key."},
        {"Suspect Highlight", Icon::Target, {"Suspect Highlight"}, "Outline suspicious players in the world, colored by severity."},
        {"Exemptions", Icon::Filter, {"Exemptions"},
         "Players and situations Vigil deliberately does not judge, to avoid false positives."},
        {"Hit attribution", Icon::Combat, {"Hit attribution"},
         "How Vigil decides who hit whom. These settings affect KillAura, Reach and Velocity."},
        {"Appearance", Icon::Palette, {"Appearance"}, "Themes are complete designs; the editor lets you make your own."},
        {"Accessibility", Icon::Accessibility, {"Accessibility"},
         "Keyboard navigation, readability, motion and sound. Everything in Vigil can be used without a mouse."},
        {"Data", Icon::Database, {"Data"}, "What Vigil keeps on disk."},
        {"About", Icon::Info, {}, ""},
    };
    return s;
}

bool skipInList(const Setting& s) {
    return s.key == "ui.theme" || s.key == "ui.accent" || s.key.rfind("checks.", 0) == 0 || s.group.empty();
}

float groupRows(PageContext& ctx, const char* group, float x, float y, float w, bool showAdvanced) {
    float h = 0.f;
    for (const auto& sp : Settings::instance().all()) {
        Setting& s = *sp;
        if (s.group != group || skipInList(s)) continue;
        if (s.advanced && !showAdvanced) continue;
        h += settingRow(ctx.ui, s, x, y + h, w);
    }
    return h;
}

float measureGroup(const char* group, bool showAdvanced, bool& hasAdvanced) {
    float h = 0.f;
    for (const auto& sp : Settings::instance().all()) {
        const Setting& s = *sp;
        if (s.group != group || skipInList(s)) continue;
        if (s.advanced) { hasAdvanced = true; if (!showAdvanced) continue; }
        h += settingRowHeight(s);
    }
    return h;
}

// Draws a group card; returns its height.
float groupCard(PageContext& ctx, const char* g, float x, float y, float w) {
    Ui& ui = ctx.ui;
    bool hasAdv = false;
    const float rowsH = measureGroup(g, ctx.state.showAdvanced, hasAdv);
    if (rowsH <= 0.f && !hasAdv) return 0.f;
    const float h = 52.f + rowsH + (hasAdv ? 36.f : 0.f);
    const Rect c{x, y, w, h};
    card(ui, c);
    std::string title = g;
    std::transform(title.begin(), title.end(), title.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    sectionHeader(ui, c.x + 22.f, c.y + 18.f, title);
    float gy = c.y + 44.f;
    gy += groupRows(ctx, g, c.x + 12.f, gy, c.w - 34.f, ctx.state.showAdvanced);
    if (hasAdv) {
        const std::string lbl = ctx.state.showAdvanced ? "Hide advanced" : "Show advanced";
        const Icon chev = ctx.state.showAdvanced ? Icon::ChevronDown : Icon::ChevronRight;
        if (button(ui, hashId("set-adv", hashId(g)), {c.x + 16.f, gy + 2.f, buttonWidth(ui, lbl, true), 28.f}, lbl, ButtonKind::Ghost, &chev)) {
            ctx.state.showAdvanced = !ctx.state.showAdvanced;
        }
    }
    return h + 14.f;
}

float hueOf(Color c) {
    const float mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b});
    const float d = mx - mn;
    if (d < 1e-4f) return 220.f;
    float h = mx == c.r ? std::fmod((c.g - c.b) / d, 6.f) : mx == c.g ? (c.b - c.r) / d + 2.f : (c.r - c.g) / d + 4.f;
    h *= 60.f;
    return h < 0.f ? h + 360.f : h;
}

void themeTile(PageContext& ctx, const Theme& th, const Rect& rc, bool sel, Id id, bool& clicked) {
    Ui& ui = ctx.ui;
    const Theme& cur = ui.th();
    bool h = false;
    clicked = ui.pressable(id, rc, &h);
    const float hv = ui.anim(mixId(id, 1), sel ? 1.f : (h ? 0.5f : 0.f), motion::kHover);
    ui.r.shadow(rc, 12.f, 14.f, Color(0, 0, 0, 0.25f * hv), 4.f);
    ui.r.roundRectGradient(rc, 12.f, th.windowTop, th.windowBottom);
    Rect mini = rc.inset(10.f);
    mini.h = 62.f;
    const Rect railR{mini.x, mini.y, 20.f, mini.h};
    ui.r.roundRect(railR, 6.f, th.rail.a > 0.f ? th.rail.withAlpha(1.f) : th.surface);
    for (int k = 0; k < 3; ++k) ui.r.roundRect({railR.x + 5.f, railR.y + 8.f + k * 11.f, 10.f, 4.f}, 2.f, k == 0 ? th.accent : th.textFaint);
    // The miniature scales with its tile, so narrow tiles (large interface scale) never overflow.
    const Rect card1{railR.r() + 6.f, mini.y, mini.w - 26.f, 26.f};
    ui.r.roundRect(card1, 6.f, th.surface);
    ui.r.roundRectStroke(card1, 6.f, 1.f, th.border);
    const bool roomForRing = card1.w >= 64.f;
    const float textRoom = card1.w - 16.f - (roomForRing ? 22.f : 0.f);
    ui.r.roundRect({card1.x + 8.f, card1.y + 7.f, std::min(30.f, textRoom * 0.6f), 4.f}, 2.f, th.text);
    ui.r.roundRect({card1.x + 8.f, card1.y + 15.f, std::min(46.f, textRoom), 4.f}, 2.f, th.textFaint);
    if (roomForRing) ui.r.arc(card1.r() - 12.f, card1.cy(), 6.f, 2.f, 0.f, 4.f, th.severity(80.f), true);
    const Rect card2{railR.r() + 6.f, card1.b() + 6.f, mini.w - 26.f, 30.f};
    ui.r.roundRect(card2, 6.f, th.surface);
    ui.r.roundRectStroke(card2, 6.f, 1.f, th.border);
    std::vector<Point> spark;
    for (int k = 0; k < 8; ++k) spark.push_back({card2.x + 8.f + k * (card2.w - 16.f) / 7.f, card2.b() - 8.f - (k % 3) * 4.f - k * 1.1f});
    ui.r.polyline(spark, 1.4f, th.accent);
    ui.fonts.draw(ui.r, type::BodyStrong, rc.x + 12.f, rc.b() - 42.f, th.name, th.text);
    ui.fonts.draw(ui.r, type::Caption, rc.x + 12.f, rc.b() - 23.f, th.description, th.textMuted, Align::Left, rc.w - 24.f);
    ui.r.roundRectStroke(rc, 12.f, sel ? 2.f : 1.f, sel ? cur.accent : th.borderStrong.mix(cur.accent.withAlpha(0.5f), hv * 0.5f));
    if (sel) {
        ui.r.circle(rc.r() - 16.f, rc.y + 16.f, 9.f, cur.accent);
        drawIcon(ui.r, Icon::Check, rc.r() - 23.f, rc.y + 9.f, 14.f, cur.textOnAccent, 2.f);
    }
}

float appearance(PageContext& ctx, float x, float y, float w) {
    Ui& ui = ctx.ui;
    const Theme& cur = ui.th();
    Setting* themeS = Settings::instance().find("ui.theme");
    Setting* accentS = Settings::instance().find("ui.accent");
    const auto& all = themes();
    const int customIndex = static_cast<int>(all.size());
    const bool isCustom = themeS->i() == customIndex;
    const float gap = 12.f;
    const int cols = 5;
    const float cw = (w - 36.f - gap * (cols - 1)) / cols;
    const float ch = 124.f;
    const int tiles = customIndex + 1;
    const int rows = (tiles + cols - 1) / cols;
    const float accentH = isCustom ? 0.f : 52.f;
    const float cardH = 46.f + rows * ch + (rows - 1) * gap + 22.f + accentH;
    card(ui, {x, y, w, cardH});
    sectionHeader(ui, x + 22.f, y + 16.f, "THEME");
    for (int i = 0; i < tiles; ++i) {
        const Rect rc{x + 18.f + (cw + gap) * (i % cols), y + 42.f + (ch + gap) * (i / cols), cw, ch};
        bool clicked = false;
        if (i < customIndex) {
            themeTile(ctx, all[static_cast<std::size_t>(i)], rc, themeS->i() == i, hashId(all[static_cast<std::size_t>(i)].name, 31337), clicked);
            if (clicked) themeS->set(i);
        } else {
            CustomTheme preview;
            preview.base = Settings::instance().find("ui.custom.base")->i();
            preview.hue = Settings::instance().find("ui.custom.hue")->f();
            preview.tint = Settings::instance().find("ui.custom.tint")->f();
            preview.contrast = Settings::instance().find("ui.custom.contrast")->f();
            preview.radius = Settings::instance().find("ui.custom.radius")->f();
            preview.glow = Settings::instance().find("ui.custom.glow")->f();
            themeTile(ctx, deriveCustomTheme(preview), rc, isCustom, hashId("custom-theme-tile"), clicked);
            if (clicked && !isCustom) {
                // Start the custom theme from what is on screen now.
                Settings::instance().find("ui.custom.base")->set(std::min(themeS->i(), customIndex - 1));
                Settings::instance().find("ui.custom.hue")->set(std::round(hueOf(cur.accent)));
                themeS->set(customIndex);
            }
        }
    }
    float yy = y + 42.f + rows * ch + (rows - 1) * gap + 22.f;
    if (!isCustom) {
        ui.fonts.draw(ui.r, type::BodyStrong, x + 28.f, yy, "Accent", cur.text);
        infoIcon(ui, hashId("accent-info"), x + 28.f + ui.fonts.width(type::BodyStrong, "Accent") + 7.f, yy + 0.5f, accentS->tooltip);
        float sx = x + 160.f;
        const auto& acc = accentChoices();
        for (std::size_t i = 0; i < acc.size(); ++i) {
            const Rect sw{sx, yy - 6.f, 30.f, 30.f};
            const Id id = hashId(acc[i].first, 4545);
            bool h = false;
            if (ui.pressable(id, sw, &h)) accentS->set(static_cast<double>(i));
            const bool sel = accentS->i() == static_cast<int>(i);
            const Color c = i == 0 ? all[static_cast<std::size_t>(std::min(themeS->i(), customIndex - 1))].accent : acc[i].second;
            const float grow = ui.springTo(mixId(id, 1), sel ? 1.f : (h ? 0.5f : 0.f), motion::kGrow.stiffness, motion::kGrow.damping);
            if (sel) ui.r.ring(sw.cx(), sw.cy(), 14.f + 2.f * grow, 2.f, c);
            ui.r.circle(sw.cx(), sw.cy(), 9.f + 1.5f * grow, c);
            if (i == 0) ui.r.ring(sw.cx(), sw.cy(), 9.f, 1.5f, cur.text.alpha(0.5f));
            ui.tooltip(mixId(id, 2), sw, acc[i].first);
            sx += 42.f;
        }
    }
    float total = cardH + 14.f;
    // Theme editor.
    if (isCustom) {
        const float edY = y + total;
        const float h = groupCard(ctx, "Theme editor", x, edY, w);
        // Hue spectrum under the hue slider (second row): control starts at x + w − 272, slider is 164 wide.
        const Rect spec{x + w - 272.f, edY + 44.f + 54.f + 27.f + 9.f, 164.f, 3.f};
        for (int i = 0; i < 36; ++i) {
            ui.r.rect({spec.x + spec.w * i / 36.f, spec.y, spec.w / 36.f + 0.5f, spec.h}, hsv(i * 10.f, 0.55f, 1.f));
        }
        total += h;
    }
    return total;
}

void dataActions(PageContext& ctx, float x, float y, float w) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    const Rect c{x, y, w, 142.f};
    card(ui, c);
    sectionHeader(ui, c.x + 22.f, c.y + 18.f, "LOCATION & ACTIONS");
    const std::string dir = ctx.host ? ctx.host->dataDirectory() : std::string("vigil");
    ui.fonts.draw(ui.r, type::Small, c.x + 22.f, c.y + 44.f, "Vigil keeps its configuration, player history, session journals and logs in", t.textMuted);
    ui.fonts.draw(ui.r, type::Mono, c.x + 22.f, c.y + 63.f, dir, t.text, Align::Left, c.w - 80.f);
    if (iconButton(ui, hashId("copy-dir"), {c.r() - 50.f, c.y + 56.f, 28.f, 28.f}, Icon::Copy, "Copy path")) copyToClipboard(dir);
    const Icon f = Icon::Folder, r = Icon::Reset;
    const float b1 = buttonWidth(ui, "Open data folder", true);
    if (button(ui, hashId("open-folder"), {c.x + 22.f, c.y + 94.f, b1, 34.f}, "Open data folder", ButtonKind::Secondary, &f) && ctx.host) {
        ctx.host->openFolder(dir);
    }
    const float b2 = buttonWidth(ui, "Start a new session", true);
    float& armed = ui.value(hashId("new-session-armed"), -100.f);
    const bool isArmed = ui.time - armed < 3.0;
    if (button(ui, hashId("new-session"), {c.x + 34.f + b1, c.y + 94.f, b2 + (isArmed ? 30.f : 0.f), 34.f},
               isArmed ? "Confirm new session" : "Start a new session", isArmed ? ButtonKind::Danger : ButtonKind::Ghost, &r)) {
        if (isArmed) { if (ctx.host) ctx.host->newSession(); armed = -100.f; }
        else armed = static_cast<float>(ui.time);
    }
}

// The user's records (confirmed, watching, exempt, saved): what is stored and where to manage it.
float recordsCard(PageContext& ctx, float x, float y, float w) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    auto& marks = ctx.engine.marks();
    const Rect c{x, y, w, 150.f};
    card(ui, c);
    sectionHeader(ui, c.x + 22.f, c.y + 18.f, "YOUR RECORDS", "history/marks.json \xC2\xB7 profiles/");
    const int confirmed = marks.confirmedCount(), watched = marks.watchedCount() - confirmed, exempt = marks.exemptCount();
    const int saved = static_cast<int>(RecordsService::instance().store().list().size());
    ui.fonts.draw(ui.r, type::Body, c.x + 22.f, c.y + 45.f,
                  std::to_string(confirmed) + " confirmed \xC2\xB7 " + std::to_string(watched) + " watching \xC2\xB7 " + std::to_string(exempt) +
                      " exempt \xC2\xB7 " + std::to_string(saved) + " saved profiles",
                  t.text);
    std::string sub = "Your decisions and saved investigations, stored only on this computer. Checks never read them.";
    if (marks.readOnly()) sub = marks.loadNote() + ".";
    ui.fonts.draw(ui.r, type::Small, c.x + 22.f, c.y + 70.f, sub, marks.readOnly() ? t.sevNotice : t.textMuted, Align::Left, c.w - 44.f);
    const Icon rec = Icon::Records, rst = Icon::Reset;
    const float b1 = buttonWidth(ui, "Manage in Records", true);
    if (button(ui, hashId("marks-review"), {c.x + 22.f, c.y + 100.f, b1, 34.f}, "Manage in Records", ButtonKind::Secondary, &rec)) {
        ctx.navigate(Page::Records);
    }
    const int all = static_cast<int>(marks.all().size());
    float& armed = ui.value(hashId("marks-clear-armed"), -100.f);
    const bool isArmed = ui.time - armed < 3.0;
    const std::string lbl = isArmed ? "Click again to clear " + std::to_string(all) + " marks" : "Clear all marks";
    if (all > 0 &&
        button(ui, hashId("marks-clear"), {c.x + 34.f + b1, c.y + 100.f, buttonWidth(ui, lbl, true), 34.f}, lbl,
               isArmed ? ButtonKind::Danger : ButtonKind::Ghost, &rst)) {
        if (isArmed) {
            for (const auto& m : marks.all()) marks.remove(m.key);
            armed = -100.f;
        } else {
            armed = static_cast<float>(ui.time);
        }
    }
    return c.h + 14.f;
}

// Exemptions section: the players you exempted live in Records; this card points there.
float exemptPlayersCard(PageContext& ctx, float x, float y, float w) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    const auto exempt = ctx.engine.marks().search("", static_cast<int>(detection::MarkKind::Exempt));
    const Rect c{x, y, w, 132.f};
    card(ui, c);
    sectionHeader(ui, c.x + 22.f, c.y + 18.f, "EXEMPT PLAYERS", "listed, never judged");
    std::string names;
    for (std::size_t i = 0; i < exempt.size() && i < 5; ++i) names += (i ? ", " : "") + exempt[i].name;
    if (exempt.size() > 5) names += " and " + std::to_string(exempt.size() - 5) + " more";
    ui.fonts.draw(ui.r, type::Body, c.x + 22.f, c.y + 44.f, exempt.empty() ? "Nobody is exempt." : names, exempt.empty() ? t.textMuted : t.text,
                  Align::Left, c.w - 44.f);
    ui.fonts.draw(ui.r, type::Small, c.x + 22.f, c.y + 66.f,
                  "Friends and party members: Vigil lists them but records no evidence, alerts or highlights for them.", t.textMuted,
                  Align::Left, c.w - 44.f);
    const Icon ic = Icon::Exempt;
    const char* lbl = exempt.empty() ? "Exempt a player" : "Manage exemptions";
    if (button(ui, hashId("exempt-manage"), {c.x + 22.f, c.y + 88.f, buttonWidth(ui, lbl, true), 32.f}, lbl, ButtonKind::Secondary, &ic)) {
        ctx.openRecord({}, 2);
        if (exempt.empty()) { ctx.state.addOpen = true; ctx.ui.focus = hashId("picker-field"); ctx.ui.kbFocus = ctx.ui.focus; }
    }
    return c.h + 14.f;
}

float keybinds(PageContext& ctx, float x, float y, float w) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    std::vector<Setting*> keys;
    for (const auto& sp : Settings::instance().all()) if (sp->type == SettingType::Key) keys.push_back(sp.get());
    float rowsH = 0.f;
    for (Setting* s : keys) rowsH += settingRowHeight(*s);
    const Rect c{x, y, w, 52.f + rowsH + 44.f};
    card(ui, c);
    sectionHeader(ui, c.x + 22.f, c.y + 18.f, "BINDINGS");
    float gy = c.y + 44.f;
    for (Setting* s : keys) gy += settingRow(ui, *s, c.x + 12.f, gy, c.w - 34.f);
    bool anyModified = false;
    for (Setting* s : keys) if (!s->isDefault()) anyModified = true;
    if (anyModified) {
        const Icon ri = Icon::Reset;
        if (button(ui, hashId("reset-keys"), {c.x + 16.f, gy + 4.f, buttonWidth(ui, "Reset all keybinds", true), 30.f}, "Reset all keybinds",
                   ButtonKind::Ghost, &ri)) {
            for (Setting* s : keys) s->reset();
        }
    } else {
        ui.fonts.draw(ui.r, type::Caption, c.x + 22.f, gy + 12.f, "All bindings are at their defaults.", t.textFaint);
    }
    float total = c.h + 14.f;
    // Built-in shortcuts reference.
    struct Shortcut { const char* keys; const char* what; };
    static const Shortcut kShortcuts[] = {
        {"Ctrl K  or  /", "Search everything"},
        {"Ctrl 1 \xE2\x80\xA6 7", "Go to a page"},
        {"Tab  \xC2\xB7  Shift Tab", "Move between controls"},
        {"Enter  \xC2\xB7  Space", "Activate the focused control"},
        {"\xE2\x86\x90 \xE2\x86\x92  \xC2\xB7  Shift", "Adjust a slider or choice (Shift: 10 steps)"},
        {"\xE2\x86\x91 \xE2\x86\x93", "Move through player and record lists"},
        {"Enter on a slider", "Type an exact value"},
        {"Backspace", "Back"},
        {"Esc", "Close the innermost layer, then Vigil"},
        {"/vigil", "Open Vigil from chat"},
        {"/vigil <player>", "Investigate a player"},
        {"/vigil reset", "Start a new session"},
    };
    const float sh = 52.f + 30.f * (sizeof(kShortcuts) / sizeof(kShortcuts[0]));
    const Rect s{x, y + total, w, sh};
    card(ui, s);
    sectionHeader(ui, s.x + 22.f, s.y + 18.f, "BUILT-IN SHORTCUTS", "not rebindable");
    float sy = s.y + 46.f;
    for (const Shortcut& k : kShortcuts) {
        const float kw = keycapWidth(ui, k.keys);
        keycap(ui, {s.x + 22.f, sy - 2.f, kw, 22.f}, k.keys);
        ui.fonts.draw(ui.r, type::Small, s.x + 40.f + std::max(kw, 120.f), sy + 1.f, k.what, t.textMuted);
        sy += 30.f;
    }
    return total + sh + 14.f;
}

void about(PageContext& ctx, float x, float y, float w) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    if (t.glow > 0.f) ui.r.circle(x + 44.f, y + 38.f, 44.f, t.accent.withAlpha(0.08f * t.glow));
    drawLogo(ui.r, x + 10.f, y + 6.f, 68.f, t.text, t.accent);
    ui.fonts.draw(ui.r, type::Hero, x + 100.f, y + 4.f, "Vigil", t.text);
    ui.fonts.draw(ui.r, type::Body, x + 102.f, y + 48.f, std::string("Cheat detection and player analysis \xC2\xB7 version ") + VIGIL_VERSION_STRING,
                  t.textMuted);
    float yy = y + 104.f;
    const Icon play = Icon::Play;
    if (button(ui, hashId("replay-intro"), {x + 10.f, yy, buttonWidth(ui, "Replay the introduction", true), 34.f}, "Replay the introduction",
               ButtonKind::Secondary, &play)) {
        ctx.state.onboardingOpen = true;
        ctx.state.onboardingStep = 0;
        ctx.state.onboardingStepAt = ui.time;
    }
    yy += 54.f;
    const char* paras[] = {
        "Vigil observes the players around you, measures their behavior against what vanilla Minecraft allows, and reports "
        "only what it can explain. Every detection is backed by recorded evidence with the measured values, the limits they "
        "were compared against and the reasoning that connects them.",
        "Vigil has no analytics, telemetry or usage tracking and never reports anyone on its own. It contacts the network "
        "only for the identity tools you enable (Mojang's public profile service and, with your key, Bordic), and only for "
        "nicked players.",
        "Typography: Inter, by Rasmus Andersson and the Inter Project Authors, used under the SIL Open Font License 1.1.",
    };
    for (const char* p : paras) yy += ui.fonts.drawWrapped(ui.r, type::Body, x + 10.f, yy, w - 20.f, p, t.textMuted, 5.f) + 14.f;
}

}  // namespace

int settingsSectionCount() { return static_cast<int>(sections().size()); }
const char* settingsSectionName(int i) { return sections()[static_cast<std::size_t>(std::clamp(i, 0, settingsSectionCount() - 1))].name; }

int settingsSectionByName(const std::string& name) {
    for (int i = 0; i < settingsSectionCount(); ++i) if (name == sections()[static_cast<std::size_t>(i)].name) return i;
    return 0;
}

int settingsSectionForGroup(const std::string& group) {
    if (group == "Theme editor") return settingsSectionByName("Appearance");
    if (group == "Keybinds") return settingsSectionByName("Keybinds");
    for (int i = 0; i < settingsSectionCount(); ++i) {
        for (const char* g : sections()[static_cast<std::size_t>(i)].groups) if (group == g) return i;
    }
    return 0;
}

void revealSetting(PageContext& ctx, const std::string& key) {
    Setting* s = Settings::instance().find(key);
    if (!s) return;
    if (key.rfind("checks.", 0) == 0) {
        const std::string rest = key.substr(7);
        const std::string id = rest.substr(0, rest.find('.'));
        if (detection::Check* c = ctx.engine.checkById(id)) ctx.state.selectedCheck = c->index();
        ctx.navigate(Page::Checks);
    } else if ((s->group == "Utilities" || s->group == "Identity") && s->type != SettingType::Key) {
        ctx.navigate(Page::Utilities);   // denickers live with the other optional tools
    } else {
        ctx.navigate(Page::Settings);
        ctx.state.settingsSection = s->type == SettingType::Key ? settingsSectionByName("Keybinds") : settingsSectionForGroup(s->group);
        if (s->group == "Theme editor") {
            Setting* th = Settings::instance().find("ui.theme");
            if (th) th->set(static_cast<double>(themes().size()));
        }
    }
    if (s->advanced) ctx.state.showAdvanced = true;
    ctx.state.focusSetting = key;
    ctx.state.focusAt = ctx.ui.time;
}

void drawSettings(PageContext& ctx, const Rect& area) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    const float top = pageHeader(ctx, area, "Settings", pagePurpose(Page::Settings));
    const Rect nav{area.x, top, 212.f, area.b() - top};
    const auto& secs = sections();
    ctx.state.settingsSection = std::clamp(ctx.state.settingsSection, 0, static_cast<int>(secs.size()) - 1);

    // Section navigation.
    const float itemH = 36.f;
    const float ind = ui.springTo(hashId("set-ind"), nav.y + ctx.state.settingsSection * (itemH + 2.f), motion::kIndicator.stiffness,
                                  motion::kIndicator.damping);
    ui.r.roundRect({nav.x, ind, nav.w, itemH}, 10.f, t.accentSoft());
    for (std::size_t i = 0; i < secs.size(); ++i) {
        const Rect rc{nav.x, nav.y + i * (itemH + 2.f), nav.w, itemH};
        const Id id = hashId(secs[i].name, 1212);
        bool h = false;
        if (ui.pressable(id, rc, &h)) ctx.state.settingsSection = static_cast<int>(i);
        const bool sel = ctx.state.settingsSection == static_cast<int>(i);
        if (h && !sel) ui.r.roundRect(rc, 10.f, t.surfaceHover.alpha(0.7f));
        drawIcon(ui.r, secs[i].icon, rc.x + 12.f, rc.cy() - 8.f, 16.f, sel ? t.accent : t.textMuted);
        ui.fonts.draw(ui.r, type::BodyStrong, rc.x + 38.f, rc.cy() - ui.fonts.lineHeight(type::BodyStrong) * 0.5f, secs[i].name,
                      sel ? t.text : t.textMuted);
    }

    const Section& sec = secs[static_cast<std::size_t>(ctx.state.settingsSection)];
    const Rect panel{nav.r() + 16.f, top, area.r() - nav.r() - 16.f, area.b() - top};
    const Id sid = hashId("settings-scroll", static_cast<Id>(ctx.state.settingsSection));
    const float off = beginScroll(ui, sid, panel);
    float y = panel.y + off;
    const float w = panel.w - 8.f;
    const std::string name = sec.name;

    if (name == "About") {
        const Rect c{panel.x, y, w, 470.f};
        card(ui, c);
        about(ctx, c.x + 18.f, c.y + 22.f, c.w - 36.f);
        y += c.h;
        endScroll(ui, sid, panel, y - (panel.y + off));
        return;
    }

    ui.fonts.draw(ui.r, type::Title, panel.x + 4.f, y, sec.name, t.text);
    ui.fonts.draw(ui.r, type::Small, panel.x + 4.f, y + 24.f, sec.blurb, t.textMuted, Align::Left, w);
    y += 56.f;

    if (name == "Appearance") y += appearance(ctx, panel.x, y, w);
    if (name == "Keybinds") y += keybinds(ctx, panel.x, y, w);
    if (name == "Exemptions") y += exemptPlayersCard(ctx, panel.x, y, w);
    for (const char* g : sec.groups) y += groupCard(ctx, g, panel.x, y, w);
    if (name == "Data") {
        y += recordsCard(ctx, panel.x, y, w);
        dataActions(ctx, panel.x, y, w);
        y += 156.f;
    }
    scrollToFlash(ui, sid, panel, panel.y + off);
    endScroll(ui, sid, panel, y - (panel.y + off));
}

}  // namespace vigil::ui
