// Settings — everything outside individual checks: monitoring scope, alerts, Suspect Highlight,
// exemptions, hit attribution, appearance (themes), data and about.

#include "pages.h"

#include <algorithm>

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
        {"General", Icon::Pulse, {"Monitoring", "Where checks run", "Calibration", "Menu"},
         "Choose where Vigil observes and how you open it."},
        {"Alerts", Icon::Bell, {"Alerts"}, "How detections reach you while you play."},
        {"Suspect Highlight", Icon::Target, {"Suspect Highlight"}, "Outline suspicious players in the world, colored by severity."},
        {"Exemptions", Icon::Filter, {"Exemptions"}, "Situations in which checks deliberately look away to avoid false positives."},
        {"Hit attribution", Icon::Combat, {"Hit attribution"},
         "How Vigil decides who hit whom. These settings affect KillAura, Reach and Velocity."},
        {"Appearance", Icon::Palette, {"Appearance"}, "Themes are complete designs; accents tune them to your taste."},
        {"Data", Icon::Database, {"Data"}, "What Vigil keeps on disk. Nothing ever leaves your computer."},
        {"About", Icon::Info, {}, ""},
    };
    return s;
}

float groupRows(PageContext& ctx, const char* group, float x, float y, float w, bool showAdvanced, bool& hadAdvanced) {
    float h = 0.f;
    for (const auto& sp : Settings::instance().all()) {
        Setting& s = *sp;
        if (s.group != group || s.key.rfind("checks.", 0) == 0) continue;
        if (s.key == "ui.theme" || s.key == "ui.accent") continue;  // drawn as galleries
        if (s.advanced) { hadAdvanced = true; if (!showAdvanced) continue; }
        h += settingRow(ctx.ui, s, x, y + h, w);
    }
    return h;
}

float measureGroup(const char* group, bool showAdvanced, bool& hasAdvanced) {
    float h = 0.f;
    for (const auto& sp : Settings::instance().all()) {
        const Setting& s = *sp;
        if (s.group != group || s.key.rfind("checks.", 0) == 0) continue;
        if (s.key == "ui.theme" || s.key == "ui.accent") continue;
        if (s.advanced) { hasAdvanced = true; if (!showAdvanced) continue; }
        h += 54.f + (s.warningActive() ? 26.f : 0.f);
    }
    return h;
}

void themeGallery(PageContext& ctx, float x, float& y, float w) {
    Ui& ui = ctx.ui;
    const Theme& cur = ui.th();
    Setting* themeS = Settings::instance().find("ui.theme");
    Setting* accentS = Settings::instance().find("ui.accent");
    const auto& all = themes();
    const float gap = 12.f;
    const int cols = 5;
    const float cw = (w - gap * (cols - 1)) / cols;
    const float ch = 132.f;
    for (std::size_t i = 0; i < all.size(); ++i) {
        const Theme& th = all[i];
        const Rect rc{x + (cw + gap) * (i % cols), y + (ch + gap) * (i / cols), cw, ch};
        const Id id = hashId(th.name, 31337);
        bool h = false;
        if (ui.pressable(id, rc, &h) && themeS) themeS->set(static_cast<double>(i));
        const bool sel = themeS && themeS->i() == static_cast<int>(i);
        const float hv = ui.anim(mixId(id, 1), sel ? 1.f : (h ? 0.5f : 0.f), 16.f);
        // Miniature of the theme.
        ui.r.shadow(rc, 12.f, 14.f, Color(0, 0, 0, 0.25f * hv), 4.f);
        ui.r.roundRectGradient(rc, 12.f, th.windowTop, th.windowBottom);
        Rect mini = rc.inset(10.f);
        mini.h = 70.f;
        const Rect railR{mini.x, mini.y, 22.f, mini.h};
        ui.r.roundRect(railR, 6.f, th.rail.a > 0.f ? th.rail.withAlpha(1.f) : th.surface);
        for (int k = 0; k < 3; ++k) ui.r.roundRect({railR.x + 5.f, railR.y + 8.f + k * 12.f, 12.f, 4.f}, 2.f, k == 0 ? th.accent : th.textFaint);
        const Rect card1{railR.r() + 6.f, mini.y, mini.w - 28.f, 30.f};
        ui.r.roundRect(card1, 6.f, th.surface);
        ui.r.roundRectStroke(card1, 6.f, 1.f, th.border);
        ui.r.roundRect({card1.x + 8.f, card1.y + 9.f, 34.f, 4.f}, 2.f, th.text);
        ui.r.roundRect({card1.x + 8.f, card1.y + 18.f, 52.f, 4.f}, 2.f, th.textFaint);
        ui.r.circle(card1.r() - 13.f, card1.cy(), 7.f, th.accent.withAlpha(0.25f));
        ui.r.arc(card1.r() - 13.f, card1.cy(), 7.f, 2.f, 0.f, 4.f, th.sevAlert, true);
        const Rect card2{railR.r() + 6.f, card1.b() + 6.f, mini.w - 28.f, 34.f};
        ui.r.roundRect(card2, 6.f, th.surface);
        ui.r.roundRectStroke(card2, 6.f, 1.f, th.border);
        std::vector<Point> spark;
        for (int k = 0; k < 8; ++k) spark.push_back({card2.x + 8.f + k * (card2.w - 16.f) / 7.f, card2.b() - 8.f - (k % 3) * 5.f - k * 1.2f});
        ui.r.polyline(spark, 1.4f, th.accent);
        ui.fonts.draw(ui.r, type::BodyStrong, rc.x + 12.f, rc.b() - 44.f, th.name, th.text);
        ui.fonts.draw(ui.r, type::Caption, rc.x + 12.f, rc.b() - 24.f, th.description, th.textMuted, Align::Left, rc.w - 24.f);
        ui.r.roundRectStroke(rc, 12.f, sel ? 2.f : 1.f, sel ? cur.accent : th.borderStrong.mix(cur.accent.withAlpha(0.5f), hv * 0.5f));
        if (sel) {
            ui.r.circle(rc.r() - 16.f, rc.y + 16.f, 9.f, cur.accent);
            drawIcon(ui.r, Icon::Check, rc.r() - 23.f, rc.y + 9.f, 14.f, cur.textOnAccent, 2.f);
        }
    }
    y += ch * ((all.size() + cols - 1) / cols) + gap * ((all.size() - 1) / cols) + 22.f;

    // Accent swatches.
    ui.fonts.draw(ui.r, type::BodyStrong, x + 10.f, y, "Accent", cur.text);
    infoIcon(ui, hashId("accent-info"), x + 10.f + ui.fonts.width(type::BodyStrong, "Accent") + 7.f, y + 0.5f,
             accentS ? accentS->tooltip : "");
    float sx = x + 140.f;
    const auto& acc = accentChoices();
    for (std::size_t i = 0; i < acc.size(); ++i) {
        const Rect sw{sx, y - 6.f, 30.f, 30.f};
        const Id id = hashId(acc[i].first, 4545);
        bool h = false;
        if (ui.pressable(id, sw, &h) && accentS) accentS->set(static_cast<double>(i));
        const bool sel = accentS && accentS->i() == static_cast<int>(i);
        const Color c = i == 0 ? themes()[static_cast<std::size_t>(themeS ? themeS->i() : 0)].accent : acc[i].second;
        const float grow = ui.springTo(mixId(id, 1), sel ? 1.f : (h ? 0.5f : 0.f), 360.f, 22.f);
        if (sel) ui.r.ring(sw.cx(), sw.cy(), 14.f + 2.f * grow, 2.f, c);
        ui.r.circle(sw.cx(), sw.cy(), 9.f + 1.5f * grow, c);
        if (i == 0) ui.r.ring(sw.cx(), sw.cy(), 9.f, 1.5f, cur.text.alpha(0.5f));
        ui.tooltip(mixId(id, 2), sw, acc[i].first);
        sx += 42.f;
    }
    y += 40.f;
}

void about(PageContext& ctx, float x, float y, float w) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    if (t.glow > 0.f) ui.r.circle(x + 44.f, y + 38.f, 44.f, t.accent.withAlpha(0.08f * t.glow));
    drawLogo(ui.r, x + 10.f, y + 6.f, 68.f, t.text, t.accent);
    ui.fonts.draw(ui.r, type::Hero, x + 100.f, y + 4.f, "Vigil", t.text);
    ui.fonts.draw(ui.r, type::Body, x + 102.f, y + 48.f, "Cheat detection and player analysis \xC2\xB7 version 2.0.0", t.textMuted);
    float yy = y + 110.f;
    const char* paras[] = {
        "Vigil observes the players around you, measures their behavior against what vanilla Minecraft allows, "
        "and reports only what it can explain. Every detection is backed by recorded evidence with the measured "
        "values, the limits they were compared against and the reasoning that connects them.",
        "Vigil runs entirely on your computer. It makes no network requests, uses no API keys and stores its "
        "data only in the vigil folder next to Vigil.dll.",
        "Typography: Inter, by Rasmus Andersson and the Inter Project Authors, used under the SIL Open Font License 1.1.",
    };
    for (const char* p : paras) yy += ui.fonts.drawWrapped(ui.r, type::Body, x + 10.f, yy, w - 20.f, p, t.textMuted, 5.f) + 14.f;
    if (ctx.host) {
        ui.fonts.draw(ui.r, type::Overline, x + 10.f, yy + 4.f, "SIGNALS", t.textFaint);
        ui.fonts.draw(ui.r, type::Small, x + 10.f, yy + 24.f, ctx.host->signalSummary(), t.text);
    }
}

}  // namespace

void drawSettings(PageContext& ctx, const Rect& area) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    pageHeader(ctx, area, "Settings", "Changes apply immediately and are saved automatically.");
    const float top = area.y + 70.f;
    const Rect nav{area.x, top, 220.f, area.b() - top};
    const auto& secs = sections();
    ctx.state.settingsSection = std::clamp(ctx.state.settingsSection, 0, static_cast<int>(secs.size()) - 1);

    // Section navigation.
    const float itemH = 38.f;
    const float ind = ui.springTo(hashId("set-ind"), nav.y + ctx.state.settingsSection * (itemH + 2.f), 320.f, 26.f);
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

    if (std::string(sec.name) == "About") {
        const Rect c{panel.x, y, w, 420.f};
        card(ui, c);
        about(ctx, c.x + 18.f, c.y + 22.f, c.w - 36.f);
        y += c.h;
        endScroll(ui, sid, panel, y - (panel.y + off));
        return;
    }

    // Blurb card header.
    ui.fonts.draw(ui.r, type::Title, panel.x + 4.f, y, sec.name, t.text);
    ui.fonts.draw(ui.r, type::Small, panel.x + 4.f, y + 24.f, sec.blurb, t.textMuted, Align::Left, w);
    y += 56.f;

    if (std::string(sec.name) == "Appearance") {
        const Rect gal{panel.x, y, w, 0.f};
        float gy = gal.y + 20.f;
        const float startY = gal.y;
        // Measure by drawing into the card after background: draw card first with an estimate.
        card(ui, {panel.x, startY, w, 132.f + 22.f + 48.f + 30.f});
        sectionHeader(ui, panel.x + 22.f, startY + 16.f, "THEME");
        gy = startY + 42.f;
        themeGallery(ctx, panel.x + 18.f, gy, w - 36.f);
        y = startY + 132.f + 22.f + 48.f + 30.f + 14.f;
    }

    for (const char* g : sec.groups) {
        bool hasAdv = false;
        const float rowsH = measureGroup(g, ctx.state.showAdvanced, hasAdv);
        if (rowsH <= 0.f && !hasAdv) continue;
        const float h = 52.f + rowsH + (hasAdv ? 36.f : 0.f);
        const Rect c{panel.x, y, w, h};
        card(ui, c);
        std::string title = g;
        std::transform(title.begin(), title.end(), title.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
        sectionHeader(ui, c.x + 22.f, c.y + 18.f, title);
        bool had = false;
        float gy = c.y + 44.f;
        gy += groupRows(ctx, g, c.x + 12.f, gy, c.w - 34.f, ctx.state.showAdvanced, had);
        if (hasAdv) {
            const std::string lbl = ctx.state.showAdvanced ? "Hide advanced" : "Show advanced";
            const Icon chev = ctx.state.showAdvanced ? Icon::ChevronDown : Icon::ChevronRight;
            if (button(ui, hashId("set-adv", hashId(g)), {c.x + 16.f, gy + 2.f, buttonWidth(ui, lbl, true), 28.f}, lbl,
                       ButtonKind::Ghost, &chev)) {
                ctx.state.showAdvanced = !ctx.state.showAdvanced;
            }
        }
        y += h + 14.f;
    }

    if (std::string(sec.name) == "Data" && ctx.host) {
        const Rect c{panel.x, y, w, 96.f};
        card(ui, c);
        sectionHeader(ui, c.x + 22.f, c.y + 18.f, "ACTIONS");
        const Icon f = Icon::Folder, r = Icon::Reset;
        const float b1 = buttonWidth(ui, "Open data folder", true);
        if (button(ui, hashId("open-folder"), {c.x + 22.f, c.y + 46.f, b1, 34.f}, "Open data folder", ButtonKind::Secondary, &f)) {
            ctx.host->openFolder(ctx.host->dataDirectory());
        }
        const float b2 = buttonWidth(ui, "Start a new session", true);
        float& armed = ui.value(hashId("new-session-armed"), -100.f);
        const bool isArmed = ui.time - armed < 3.0;
        if (button(ui, hashId("new-session"), {c.x + 34.f + b1, c.y + 46.f, b2 + (isArmed ? 30.f : 0.f), 34.f},
                   isArmed ? "Confirm new session" : "Start a new session", isArmed ? ButtonKind::Danger : ButtonKind::Ghost, &r)) {
            if (isArmed) { ctx.host->newSession(); armed = -100.f; }
            else armed = static_cast<float>(ui.time);
        }
        y += c.h + 14.f;
    }
    endScroll(ui, sid, panel, y - (panel.y + off));
}

}  // namespace vigil::ui
