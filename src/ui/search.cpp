// Global search — one palette over pages, players, checks, settings, evidence, themes,
// utilities, investigation tools and actions. Ctrl+K or "/" anywhere in Vigil.

#include "../identity/identity.h"
#include "../utilities/utilities.h"
#include "pages.h"
#include "records.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <functional>

namespace vigil::ui {
namespace {

enum Group { gPlayers, gPages, gChecks, gSettings, gUtilities, gEvidence, gThemes, gActions, gCount };
const char* kGroupNames[gCount] = {"PLAYERS", "PAGES", "CHECKS", "SETTINGS", "UTILITIES", "EVIDENCE", "THEMES", "ACTIONS"};

struct Result {
    int group = 0;
    Icon icon = Icon::Dot;
    std::string title;
    std::string subtitle;
    int score = 0;
    std::function<void(PageContext&)> action;
    std::string avatarKey;
    float suspicion = -1.f;
};

std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<std::string> tokens(const std::string& q) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : lowerCopy(q)) {
        if (std::isspace(static_cast<unsigned char>(c))) { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Scores a candidate: every token must match the title, the location path or the keywords.
// Title prefix > title word start > title substring > location path > keywords.
int score(const std::vector<std::string>& toks, const std::string& title, const std::string& path, const std::string& keywords) {
    if (toks.empty()) return 1;
    const std::string t = lowerCopy(title), p = lowerCopy(path), k = lowerCopy(keywords);
    int total = 0;
    for (const auto& tok : toks) {
        int best = 0;
        const auto at = t.find(tok);
        if (at == 0) best = 100;
        else if (at != std::string::npos) best = (t[at - 1] == ' ' || t[at - 1] == '(' || t[at - 1] == '-') ? 70 : 40;
        else if (p.find(tok) != std::string::npos) best = 32;
        else if (k.find(tok) != std::string::npos) best = 16;
        if (best == 0) return 0;
        total += best;
    }
    return total - static_cast<int>(t.size() / 8);
}

std::vector<Result> collect(PageContext& ctx, const std::string& query) {
    const auto toks = tokens(query);
    std::vector<Result> out;
    auto add = [&](Result r, const std::string& keywords) {
        r.score = score(toks, r.title, r.subtitle, keywords);
        if (r.score > 0) out.push_back(std::move(r));
    };
    auto& e = ctx.engine;

    // Players (and nicked players by the name behind the nick).
    for (const auto* p : sortedPlayers(e, 0, {}, true)) {
        identity::Profile ip;
        std::string kw = p->uuid;
        std::string sub = std::string(tierName(tierOf(p->suspicion))) + " \xC2\xB7 " + presenceLabel(ctx, *p);
        if (e.marks().confirmed(p->key)) { sub = "Confirmed by you \xC2\xB7 " + sub; kw += " confirmed marked"; }
        else if (e.marks().exempt(p->key)) { sub = "Exempt \xC2\xB7 " + sub; kw += " exempt marked"; }
        else if (e.marks().watched(p->key)) { sub = "Watching \xC2\xB7 " + sub; kw += " watched marked"; }
        if (identity::Service::instance().find(p->key, ip) && ip.nicked) {
            kw += " nick nicked " + ip.realName;
            for (const auto& c : ip.candidates) kw += " " + c;
            sub += " \xC2\xB7 nicked" + (ip.realName.empty() ? std::string() : " (skin of " + ip.realName + ")");
        }
        const std::string key = p->key;
        Result r{gPlayers, Icon::Players, p->name, sub, 0, [key](PageContext& c) { c.openPlayer(key); }, key, p->suspicion};
        add(r, kw);
        if (toks.empty() && out.size() >= 4) break;
    }
    if (toks.empty()) {
        // Suggestions only: a few players, the pages and the most useful actions.
        for (int i = 0; i < kPageCount; ++i) {
            const Page pg = static_cast<Page>(i);
            add({gPages, pageIcon(pg), pageTitle(pg), std::string(pageSection(pg)) + " \xC2\xB7 Ctrl " + std::to_string(i + 1), 0,
                 [pg](PageContext& c) { c.navigate(pg); }}, "");
        }
        return out;
    }
    // Pages and settings sections.
    for (int i = 0; i < kPageCount; ++i) {
        const Page pg = static_cast<Page>(i);
        add({gPages, pageIcon(pg), pageTitle(pg), pagePurpose(pg), 0, [pg](PageContext& c) { c.navigate(pg); }}, pageSection(pg));
    }
    for (int i = 0; i < settingsSectionCount(); ++i) {
        const std::string n = settingsSectionName(i);
        add({gPages, Icon::Settings, "Settings \xE2\x80\xBA " + n, "Settings section", 0,
             [i](PageContext& c) { c.navigate(Page::Settings); c.state.settingsSection = i; }}, n);
    }
    for (const char* tab : {"Summary", "Evidence", "Behavior", "Identity"}) {
        const std::string tname = tab;
        if (ctx.state.selectedPlayer.empty()) break;
        const auto* p = e.session().find(ctx.state.selectedPlayer);
        if (!p) break;
        const ProfileTab pt = tname == "Summary" ? ProfileTab::Summary : tname == "Evidence" ? ProfileTab::Evidence
                            : tname == "Behavior" ? ProfileTab::Behavior : ProfileTab::Identity;
        const std::string key = p->key;
        add({gPages, Icon::Players, p->name + " \xE2\x80\xBA " + tname, "Profile tab", 0,
             [key, pt](PageContext& c) { c.openPlayer(key, pt); }}, "profile tab");
    }
    // Records: marked players not in this session, and saved profiles.
    for (const auto& m : e.marks().all()) {
        if (e.session().find(m.key)) continue;   // already listed as a player
        const std::string key = m.key;
        const int tab = m.confirmed ? 0 : m.exempt ? 2 : 1;
        const std::string what = m.confirmed ? "Confirmed by you" : m.exempt ? "Exempt" : "Watching";
        add({gPlayers, Icon::Records, m.name, what + " \xC2\xB7 Records" + (m.addedByName() ? " \xC2\xB7 added by name" : ""), 0,
             [key, tab](PageContext& c) { c.openRecord(key, tab); }},
            "record marked " + what + " " + m.note);
    }
    for (const auto& s : RecordsService::instance().store().list()) {
        const std::string id = s.id;
        add({gPlayers, Icon::Save, s.name, "Saved profile \xC2\xB7 " + epochAgoLabel(s.savedAtEpochMs), 0,
             [id](PageContext& c) { c.openRecord(id, 3); }},
            "saved profile snapshot " + s.note);
    }
    // Checks.
    for (const auto& c : e.checks()) {
        const int idx = c->index();
        add({gChecks, checkIcon(c->id()), c->name(), c->summary(), 0,
             [idx](PageContext& pc) { pc.navigate(Page::Checks); pc.state.selectedCheck = idx; }},
            std::string(detection::categoryName(c->category())) + " check");
    }
    // Settings (every registered, visible setting).
    for (const auto& sp : Settings::instance().all()) {
        const Setting& s = *sp;
        if (s.group.empty()) continue;
        std::string where = s.group;
        if (s.key.rfind("checks.", 0) == 0) {
            const std::string id = s.key.substr(7, s.key.find('.', 7) - 7);
            if (const auto* c = e.checkById(id)) where = c->name() + " \xE2\x80\xBA " + s.group;
        }
        if (s.type == SettingType::Key) where = "Keybinds \xC2\xB7 " + keyName(s.i());
        const std::string key = s.key;
        add({gSettings, s.type == SettingType::Key ? Icon::Keyboard : Icon::Settings, s.label, where, 0,
             [key](PageContext& c) { revealSetting(c, key); }},
            s.tooltip + (s.type == SettingType::Key ? " hotkey shortcut keybind" : ""));
    }
    // Utilities and investigation tools.
    struct Tool { const char* name; Icon icon; const char* kw; const char* setting; };
    static const Tool kTools[] = {
        {"Auto Who", Icon::Chat, "who utility bed wars game start", "utilities.autoWho"},
        {"Fast Buy", Icon::Shop, "quick buy shop utility instant", "utilities.fastBuy"},
        {"Party Detector", Icon::Party, "party lobby joined quit utility", "utilities.partyChat"},
        {"Party Queue Detector", Icon::Queue, "party queue entity id utility", "utilities.partyQueue"},
        {"Skin denicker", Icon::Mask, "denick nick skin identity investigation tool", "identity.skinDenick"},
        {"Number denicker", Icon::Mask, "denick nick final bed number bordic identity investigation tool", "identity.numberDenick"},
    };
    for (const Tool& tl : kTools) {
        const std::string key = tl.setting;
        const Setting* s = Settings::instance().find(key);
        add({gUtilities, tl.icon, tl.name, s && s->b() ? "On" : "Off", 0, [key](PageContext& c) { revealSetting(c, key); }}, tl.kw);
    }
    // Evidence (recent observations).
    int evCount = 0;
    for (const auto& r : e.session().players()) {
        for (auto it = r.evidence.rbegin(); it != r.evidence.rend() && evCount < 400; ++it, ++evCount) {
            const std::string key = r.key;
            const std::uint64_t id = it->id;
            add({gEvidence, checkIcon(e.checks()[static_cast<std::size_t>(it->check)]->id()), it->summary,
                 r.name + " \xC2\xB7 " + e.checks()[static_cast<std::size_t>(it->check)]->name() + " \xC2\xB7 " + agoLabel(ctx.nowMs, it->timeMs), 0,
                 [key, id](PageContext& c) { c.openPlayer(key, ProfileTab::Evidence); c.state.selectedEvidence = id; }},
                r.name);
        }
    }
    // Themes.
    const auto& th = themes();
    for (std::size_t i = 0; i <= th.size(); ++i) {
        const bool custom = i == th.size();
        const std::string name = custom ? "Custom theme" : std::string(th[i].name) + " theme";
        add({gThemes, Icon::Palette, name, custom ? "Build your own in the theme editor" : th[i].description, 0,
             [i, custom](PageContext& c) {
                 Settings::instance().find("ui.theme")->set(static_cast<double>(i));
                 if (custom) revealSetting(c, "ui.custom.hue");
             }},
            "appearance color look");
    }
    // Actions.
    add({gActions, Icon::Reset, "Start a new session", "Clears this session's players and evidence", 0,
         [](PageContext& c) { if (c.host) c.host->newSession(); }}, "reset clear session");
    add({gActions, Icon::Folder, "Open data folder", "Configuration, history, journals and logs", 0,
         [](PageContext& c) { if (c.host) c.host->openFolder(c.host->dataDirectory()); }}, "files logs config");
    add({gActions, Icon::Play, "Replay the introduction", "The first-run tour", 0,
         [](PageContext& c) { c.state.onboardingOpen = true; c.state.onboardingStep = 0; c.state.onboardingStepAt = c.ui.time; }},
        "onboarding tour help tutorial");
    {
        const bool mon = Settings::instance().getBool("general.monitoring", true);
        add({gActions, mon ? Icon::Minus : Icon::Play, mon ? "Pause monitoring" : "Resume monitoring", "Master switch", 0,
             [mon](PageContext&) { Settings::instance().find("general.monitoring")->set(mon ? 0.0 : 1.0); }}, "monitoring stop start");
    }
    add({gActions, Icon::Keyboard, "Change the menu key", "Currently " + keyLabel("ui.menuKey"), 0,
         [](PageContext& c) { revealSetting(c, "ui.menuKey"); }}, "hotkey keybind open shortcut right shift");
    add({gActions, Icon::Status, "Check Vigil's status", "Injection, hooks, engine and signals", 0,
         [](PageContext& c) { c.navigate(Page::Status); }}, "health diagnostics working injected");
    add({gActions, Icon::UserPlus, "Confirm, watch or exempt a player by name", "Records", 0,
         [](PageContext& c) { c.openRecord({}, 0); c.state.addOpen = true; c.ui.focus = hashId("picker-field"); c.ui.kbFocus = c.ui.focus; }},
        "add player username confirm watch exempt friend party");
    add({gActions, Icon::Import, "Import a profile", "Records \xE2\x80\xBA Saved", 0, [](PageContext& c) { c.openRecord({}, 3); }},
        "import load file profile snapshot share");
    add({gActions, Icon::Keyboard, "Keyboard shortcuts", "Tab, Enter, arrows and page shortcuts", 0,
         [](PageContext& c) { c.navigate(Page::Settings); c.state.settingsSection = settingsSectionByName("Keybinds"); }},
        "keyboard navigation focus tab accessibility shortcuts");
    add({gActions, Icon::Pulse, "Copy a diagnostics report", "Status \xE2\x80\xBA Diagnostics", 0, [](PageContext& c) { c.navigate(Page::Status); }},
        "diagnostics report bug support trace record log");

    std::stable_sort(out.begin(), out.end(), [](const Result& a, const Result& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.group < b.group;
    });
    // At most 6 per group, 24 total; then group for display, ordered by each group's best result.
    std::vector<Result> trimmed;
    int perGroup[gCount] = {};
    for (auto& r : out) {
        if (perGroup[r.group] >= 6 || trimmed.size() >= 24) continue;
        ++perGroup[r.group];
        trimmed.push_back(std::move(r));
    }
    std::vector<int> groupOrder;
    for (const auto& r : trimmed) if (std::find(groupOrder.begin(), groupOrder.end(), r.group) == groupOrder.end()) groupOrder.push_back(r.group);
    std::stable_sort(trimmed.begin(), trimmed.end(), [&](const Result& a, const Result& b) {
        return std::find(groupOrder.begin(), groupOrder.end(), a.group) < std::find(groupOrder.begin(), groupOrder.end(), b.group);
    });
    return trimmed;
}

}  // namespace

void drawSearch(PageContext& ctx, const Rect& win) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    auto& st = ctx.state;
    const Id fieldId = hashId("search-field");
    const float in = ease::emphasized(motion::progress(ui.time, st.searchChangedAt, motion::kModalIn));

    // Dim the window behind the palette.
    ui.r.roundRect(win, t.radiusWindow, Color(0, 0, 0, (t.light ? 0.18f : 0.38f) * in));

    auto results = collect(ctx, st.searchQuery);
    st.searchIndex = std::clamp(st.searchIndex, 0, std::max(0, static_cast<int>(results.size()) - 1));
    auto close = [&]() { st.searchOpen = false; ui.focus = 0; };

    // Keyboard first (before the text field consumes Enter/Escape).
    if (ui.consumeKey(VK_ESCAPE)) { close(); return; }
    if (ui.consumeKey(VK_DOWN)) st.searchIndex = std::min(st.searchIndex + 1, static_cast<int>(results.size()) - 1);
    if (ui.consumeKey(VK_UP)) st.searchIndex = std::max(st.searchIndex - 1, 0);
    if (ui.consumeKey(VK_RETURN) && !results.empty()) {
        auto action = results[static_cast<std::size_t>(st.searchIndex)].action;
        close();
        action(ctx);
        return;
    }

    const float w = std::min(660.f, win.w - 80.f);
    const float rowH = 46.f;
    float listH = 0.f;
    int lastGroup = -1;
    for (const auto& r : results) { if (r.group != lastGroup) { listH += 28.f; lastGroup = r.group; } listH += rowH; }
    const float maxList = win.h - 220.f;
    const float panelH = 64.f + std::min(listH, maxList) + (results.empty() ? 90.f : 12.f) + 36.f;
    const Rect panel{win.cx() - w * 0.5f, win.y + 76.f - (1.f - in) * motion::travel(motion::kModalTravel), w, panelH};
    ui.r.pushAlpha(in);
    ui.r.shadow(panel, 16.f, 44.f, Color(0, 0, 0, t.light ? 0.22f : 0.6f), 16.f);
    ui.r.roundRect(panel, 16.f, t.surfaceRaised);
    ui.r.roundRectStroke(panel, 16.f, 1.f, t.borderStrong);

    // Field.
    const Rect field{panel.x + 12.f, panel.y + 12.f, panel.w - 24.f, 42.f};
    ui.focus = fieldId;
    if (textField(ui, fieldId, field, st.searchQuery, "Search players, checks, settings, evidence, themes\xE2\x80\xA6")) st.searchIndex = 0;
    ui.focus = fieldId;
    divider(ui, panel.x, panel.r(), field.b() + 10.f);

    // Results.
    const Rect view{panel.x + 6.f, field.b() + 12.f, panel.w - 12.f, std::min(listH, maxList) + 4.f};
    if (results.empty()) {
        ui.fonts.draw(ui.r, type::BodyStrong, panel.cx(), view.y + 26.f, "No results for \xE2\x80\x9C" + st.searchQuery + "\xE2\x80\x9D", t.text,
                      Align::Center);
        ui.fonts.draw(ui.r, type::Small, panel.cx(), view.y + 50.f, "Try a player name, a check, or a setting such as \xE2\x80\x9Creach\xE2\x80\x9D.",
                      t.textMuted, Align::Center);
    } else {
        const Id sid = hashId("search-results");
        const float off = beginScroll(ui, sid, view);
        float y = view.y + off;
        lastGroup = -1;
        float selY = -1.f;
        for (std::size_t i = 0; i < results.size(); ++i) {
            const Result& r = results[i];
            if (r.group != lastGroup) {
                lastGroup = r.group;
                ui.fonts.draw(ui.r, type::Overline, view.x + 12.f, y + 10.f,
                              st.searchQuery.empty() && r.group == gPlayers ? "SUGGESTED" : kGroupNames[r.group], t.textFaint);
                y += 28.f;
            }
            const Rect row{view.x, y, view.w, rowH};
            const Id rid = mixId(hashId("sr"), i * 131 + static_cast<Id>(r.group));
            bool h = false;
            const bool clicked = ui.pressable(rid, row, &h);
            if (h && ui.in.mx != ui.value(hashId("search-last-mx"), 0.f)) st.searchIndex = static_cast<int>(i);
            const bool sel = st.searchIndex == static_cast<int>(i);
            if (sel) selY = y;
            const float hv = ui.anim(rid, sel ? 1.f : 0.f, motion::kHover);
            if (hv > 0.01f) ui.r.roundRect(row.inset(4.f, 1.f), 9.f, t.accentSoft().alpha(hv * 1.2f));
            if (!r.avatarKey.empty()) {
                const auto* p = ctx.engine.session().find(r.avatarKey);
                if (p) playerAvatar(ctx, *p, {row.x + 14.f, row.cy() - 14.f, 28.f, 28.f});
            } else {
                const Rect ic{row.x + 14.f, row.cy() - 14.f, 28.f, 28.f};
                ui.r.roundRect(ic, 8.f, t.inset);
                drawIcon(ui.r, r.icon, ic.x + 6.f, ic.y + 6.f, 16.f, sel ? t.accent : t.textMuted);
            }
            ui.fonts.draw(ui.r, type::BodyStrong, row.x + 54.f, row.y + 6.f, r.title, t.text, Align::Left, row.w - 170.f);
            ui.fonts.draw(ui.r, type::Caption, row.x + 54.f, row.y + 25.f, r.subtitle, t.textFaint, Align::Left, row.w - 170.f);
            if (r.suspicion >= 0.f) {
                if (tierOf(r.suspicion) != Tier::Clear) suspicionLabel(ui, row.r() - 16.f, row.y + 14.f, r.suspicion, Align::Right);
            } else if (sel) {
                const float kw = keycapWidth(ui, "Enter");
                keycap(ui, {row.r() - 16.f - kw, row.cy() - 10.f, kw, 20.f}, "Enter");
            }
            if (clicked) {
                auto action = r.action;
                close();
                ui.r.popClip();
                ui.r.popAlpha();
                action(ctx);
                return;
            }
            y += rowH;
        }
        ui.value(hashId("search-last-mx"), 0.f) = ui.in.mx;
        // Keep the keyboard selection visible.
        ScrollState& ss = scrollState(ui, sid);
        if (selY >= 0.f) {
            const float cy = selY - (view.y + off);
            if (cy < ss.target) ss.target = cy - 30.f;
            else if (cy + rowH > ss.target + view.h) ss.target = cy + rowH - view.h + 6.f;
        }
        endScroll(ui, sid, view, y - (view.y + off) + 4.f);
    }
    // Footer hints.
    const float fy = panel.b() - 30.f;
    divider(ui, panel.x, panel.r(), fy - 6.f);
    float fx = panel.x + 16.f;
    for (const auto& [k, label] : {std::pair<const char*, const char*>{"\xE2\x86\x91\xE2\x86\x93", "move"}, {"Enter", "open"}, {"Esc", "close"}}) {
        const float kw = keycapWidth(ui, k);
        keycap(ui, {fx, fy, kw, 20.f}, k);
        fx += kw + 6.f;
        fx += ui.fonts.draw(ui.r, type::Caption, fx, fy + 3.f, label, t.textFaint) + 16.f;
    }
    ui.r.popAlpha();
}

}  // namespace vigil::ui
