// Utilities — optional Bed Wars helpers (Auto Who, Fast Buy, Party Detector, Party Queue
// Detector). Each card says what the utility does, whether it is doing anything right now, how
// to toggle it from the game, and what it did recently.

#include "../utilities/utilities.h"
#include "pages.h"

#include <algorithm>

namespace vigil::ui {
namespace {

using utilities::Kind;

struct UtilityInfo {
    Kind kind;
    Icon icon;
    const char* name;
    Setting* enabled;
    Setting* key;
    std::vector<Setting*> options;
};

std::string contextStatus(Kind k, bool on, const utilities::LiveState& live) {
    if (!on) return "Off";
    if (live.hosted && !live.hypixel) return "Waiting for Hypixel";
    switch (k) {
        case Kind::AutoWho: return live.hosted && !live.chatReader ? "Chat unavailable" : "Ready for the next game";
        case Kind::FastBuy: return live.quickBuyOpen ? "Quick Buy open \xC2\xB7 active" : "Active in the Quick Buy shop";
        case Kind::PartyChat:
            if (live.hosted && !live.chatReader) return "Chat unavailable";
            return live.bedwarsPregame ? "Watching this lobby" : "Waiting for a Bed Wars lobby";
        case Kind::PartyQueue:
            if (!live.bedwarsPregame) return "Waiting for a Bed Wars queue";
            if (live.queueMaxParty == 1) return "Solo queue \xC2\xB7 nothing to detect";
            return live.queueMaxParty > 1 ? "Watching \xC2\xB7 parties up to " + std::to_string(live.queueMaxParty)
                                          : "Watching \xC2\xB7 mode unknown";
    }
    return "";
}

Icon kindIcon(Kind k) {
    switch (k) {
        case Kind::AutoWho: return Icon::Chat;
        case Kind::FastBuy: return Icon::Shop;
        case Kind::PartyChat: return Icon::Party;
        case Kind::PartyQueue: return Icon::Queue;
    }
    return Icon::Utilities;
}

float utilityCard(PageContext& ctx, const UtilityInfo& u, const Rect& rc, const utilities::LiveState& live) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    const bool on = u.enabled && u.enabled->b();
    const Id id = hashId(u.name, 6060);
    const float onv = ui.anim(mixId(id, 1), on ? 1.f : 0.f, motion::kSelect);
    card(ui, rc);
    if (onv > 0.01f) ui.r.roundRectStroke(rc, t.radiusCard, 1.f, t.accent.withAlpha(0.35f * onv));
    const Rect ic{rc.x + 20.f, rc.y + 20.f, 40.f, 40.f};
    ui.r.roundRect(ic, 11.f, (on ? t.accent : t.textFaint).withAlpha(t.light ? 0.10f : 0.13f));
    drawIcon(ui.r, u.icon, ic.x + 10.f, ic.y + 10.f, 20.f, on ? t.accent : t.textMuted);
    ui.fonts.draw(ui.r, type::Title, ic.r() + 14.f, rc.y + 20.f, u.name, t.text);
    const std::string status = contextStatus(u.kind, on, live);
    const bool problem = status.find("unavailable") != std::string::npos || status.find("Waiting for Hypixel") != std::string::npos;
    const bool engaged = on && !problem && (status.rfind("Watching", 0) == 0 || status.find("active") != std::string::npos);
    const Color sc = !on ? t.textFaint : problem ? t.sevNotice : t.sevClear;
    statusDot(ui, ic.r() + 18.f, rc.y + 50.f, sc, engaged);
    ui.fonts.draw(ui.r, type::Small, ic.r() + 28.f, rc.y + 43.f, status, on ? t.text : t.textFaint, Align::Left, rc.w - 160.f);
    if (u.enabled && toggle(ui, mixId(id, 2), {rc.r() - 60.f, rc.y + 26.f, 40.f, 22.f}, on)) {
        u.enabled->set(on ? 0.0 : 1.0);
        utilities::Activity::instance().add(u.kind, std::string(u.name) + (on ? " turned off" : " turned on"));
    }
    float y = rc.y + 74.f;
    if (u.enabled) y += ui.fonts.drawWrapped(ui.r, type::Small, rc.x + 20.f, y, rc.w - 40.f, u.enabled->tooltip, t.textMuted, 3.f) + 12.f;

    // Shortcut.
    if (u.key) {
        const std::string kn = keyName(u.key->i());
        ui.fonts.draw(ui.r, type::Caption, rc.x + 20.f, y + 3.f, "SHORTCUT", t.textFaint);
        const float kw = keycapWidth(ui, kn);
        const Rect kr{rc.x + 90.f, y, kw, 22.f};
        bool kh = false;
        if (ui.pressable(mixId(id, 3), kr.inset(-4.f), &kh)) revealSetting(ctx, u.key->key);
        keycap(ui, kr, kn);
        ui.fonts.draw(ui.r, type::Caption, kr.r() + 10.f, y + 3.f, u.key->i() ? "toggles in game" : "click to assign", kh ? t.text : t.textFaint);
        y += 32.f;
    }
    // Options.
    for (Setting* s : u.options) y += settingRow(ui, *s, rc.x + 10.f, y, rc.w - 30.f);

    // Latest activity.
    const auto recent = utilities::Activity::instance().recent(u.kind, 1);
    divider(ui, rc.x + 20.f, rc.r() - 20.f, rc.b() - 40.f);
    const std::string last = recent.empty() ? "Nothing yet this session" : recent.front().text;
    drawIcon(ui.r, Icon::Clock, rc.x + 20.f, rc.b() - 29.f, 13.f, t.textFaint, 1.4f);
    ui.fonts.draw(ui.r, type::Caption, rc.x + 40.f, rc.b() - 28.f, last, recent.empty() ? t.textFaint : t.textMuted, Align::Left, rc.w - 140.f);
    if (!recent.empty()) ui.fonts.draw(ui.r, type::Caption, rc.r() - 20.f, rc.b() - 28.f, epochAgoLabel(recent.front().timeMs), t.textFaint, Align::Right);
    return y;
}

}  // namespace

void drawUtilities(PageContext& ctx, const Rect& area) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    const float top = pageHeader(ctx, area, "Utilities", pagePurpose(Page::Utilities));
    const auto& us = utilities::settings();
    const auto live = utilities::liveState();
    const std::vector<UtilityInfo> list = {
        {Kind::AutoWho, Icon::Chat, "Auto Who", us.autoWho, us.autoWhoKey, {us.autoWhoDelay}},
        {Kind::FastBuy, Icon::Shop, "Fast Buy", us.fastBuy, us.fastBuyKey, {}},
        {Kind::PartyChat, Icon::Party, "Party Detector", us.partyChat, us.partyChatKey, {}},
        {Kind::PartyQueue, Icon::Queue, "Party Queue Detector", us.partyQueue, us.partyQueueKey, {}},
    };
    const Id sid = hashId("utilities-scroll");
    const Rect view{area.x, top, area.w, area.b() - top};
    const float off = beginScroll(ui, sid, view);
    float y = top + off;
    const float gap = 14.f;
    const float cw = (area.w - 8.f - gap) * 0.5f;
    const float ch = 262.f;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Rect rc{area.x + (cw + gap) * (i % 2), y + (ch + gap) * (i / 2), cw, ch};
        utilityCard(ctx, list[i], rc, live);
    }
    y += (ch + gap) * 2.f;

    // Shared option + combined activity log.
    if (us.announceInChat) {
        const Rect c{area.x, y, area.w - 8.f, 52.f + settingRowHeight(*us.announceInChat)};
        card(ui, c);
        sectionHeader(ui, c.x + 22.f, c.y + 18.f, "OUTPUT");
        settingRow(ui, *us.announceInChat, c.x + 12.f, c.y + 44.f, c.w - 34.f);
        y += c.h + gap;
    }
    const auto log = utilities::Activity::instance().recent(40);
    const float logH = 52.f + std::max<std::size_t>(1, std::min<std::size_t>(log.size(), 12)) * 34.f + 8.f;
    const Rect lc{area.x, y, area.w - 8.f, logH};
    card(ui, lc);
    sectionHeader(ui, lc.x + 22.f, lc.y + 18.f, "ACTIVITY", "this session");
    float ly = lc.y + 46.f;
    if (log.empty()) {
        ui.fonts.draw(ui.r, type::Small, lc.x + 22.f, ly + 4.f, "Utilities record what they do here: parties found, /who sent, purchases made.",
                      t.textFaint);
    }
    for (std::size_t i = 0; i < log.size() && i < 12; ++i) {
        const auto& e = log[i];
        const Color c = e.highlight ? t.accent : t.textMuted;
        drawIcon(ui.r, kindIcon(e.kind), lc.x + 22.f, ly + 1.f, 15.f, c, 1.6f);
        ui.fonts.draw(ui.r, type::SmallStrong, lc.x + 46.f, ly, utilities::kindName(e.kind), t.text);
        ui.fonts.draw(ui.r, type::Small, lc.x + 200.f, ly, e.text, e.highlight ? t.text : t.textMuted, Align::Left, lc.w - 300.f);
        ui.fonts.draw(ui.r, type::Caption, lc.r() - 22.f, ly + 1.f, epochAgoLabel(e.timeMs), t.textFaint, Align::Right);
        ly += 34.f;
    }
    y += logH + gap;
    scrollToFlash(ui, sid, view, top + off);
    endScroll(ui, sid, view, y - (top + off));
}

}  // namespace vigil::ui
