// Status — is Vigil healthy? Injection and hooks, the detection engine, signals from the game,
// services (utilities, chat, identity), data, and a live runtime log.

#include "../core/log.h"
#include "../identity/identity.h"
#include "../utilities/utilities.h"
#include "core/version.h"
#include "pages.h"

#include <algorithm>

namespace vigil::ui {
namespace {

enum class State { Ok, Warn, Off, Info };

float statusRow(Ui& ui, float x, float y, float w, State s, const std::string& label, const std::string& value) {
    const Theme& t = ui.th();
    const Color c = s == State::Ok ? t.sevClear : s == State::Warn ? t.sevNotice : s == State::Off ? t.textFaint : t.accent;
    ui.r.circle(x + 7.f, y + 8.f, 7.f, c.withAlpha(0.15f));
    if (s == State::Ok) drawIcon(ui.r, Icon::Check, x + 1.f, y + 2.f, 12.f, c, 2.f);
    else if (s == State::Warn) drawIcon(ui.r, Icon::Warning, x + 1.f, y + 1.f, 12.f, c, 1.8f);
    else ui.r.circle(x + 7.f, y + 8.f, 2.5f, c);
    ui.fonts.draw(ui.r, type::Small, x + 22.f, y, label, t.text, Align::Left, w * 0.58f);
    if (!value.empty()) ui.fonts.draw(ui.r, type::Small, x + w, y, value, t.textMuted, Align::Right, w * 0.42f);
    return y + 26.f;
}

Rect systemCard(Ui& ui, const Rect& rc, Icon icon, const char* title) {
    const Theme& t = ui.th();
    card(ui, rc);
    drawIcon(ui.r, icon, rc.x + 20.f, rc.y + 17.f, 16.f, t.textMuted);
    ui.fonts.draw(ui.r, type::Overline, rc.x + 44.f, rc.y + 19.f, title, t.textFaint);
    return {rc.x + 20.f, rc.y + 48.f, rc.w - 40.f, rc.h - 56.f};
}

}  // namespace

void drawStatus(PageContext& ctx, const Rect& area) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    const auto& e = ctx.engine;
    RuntimeStatus rs;
    if (ctx.host) rs = ctx.host->runtimeStatus();
    const float top = pageHeader(ctx, area, "Status", pagePurpose(Page::Status));
    const Health hl = computeHealth(ctx);

    const Id sid = hashId("status-scroll");
    const Rect view{area.x, top, area.w, area.b() - top};
    const float off = beginScroll(ui, sid, view);
    float y = top + off;
    const float w = area.w - 8.f;

    // ── Hero ──
    {
        const Rect hero{area.x, y, w, 104.f};
        card(ui, hero);
        const Color hc = healthColor(t, hl.level);
        const Rect badge{hero.x + 22.f, hero.y + 22.f, 60.f, 60.f};
        ui.r.roundRect(badge, 16.f, hc.withAlpha(0.13f));
        if (motionLevel() > 0.01f && hl.level == Health::Active) {
            const float k = static_cast<float>(std::fmod(ui.time, 2.4) / 2.4);
            ui.r.roundRectStroke(badge.inset(-8.f * ease::outCubic(k)), 16.f + 8.f * k, 1.5f, hc.withAlpha(0.3f * (1.f - k)));
        }
        drawLogo(ui.r, badge.x + 14.f, badge.y + 14.f, 32.f, hc, t.text);
        std::string title;
        switch (hl.level) {
            case Health::Active: title = "Vigil is running correctly"; break;
            case Health::Standby: title = "Vigil is running and standing by"; break;
            case Health::Degraded: title = "Vigil is running with issues"; break;
            case Health::Paused: title = "Monitoring is paused"; break;
        }
        ui.fonts.draw(ui.r, type::Heading, badge.r() + 18.f, hero.y + 22.f, title, t.text);
        std::string sub = hl.detail + " \xC2\xB7 version " + VIGIL_VERSION_STRING;
        if (rs.hosted) sub += " \xC2\xB7 up " + formatDuration(rs.uptimeSec);
        else sub += " \xC2\xB7 preview";
        ui.fonts.draw(ui.r, type::Small, badge.r() + 18.f, hero.y + 58.f, sub, t.textMuted, Align::Left, hero.w - 340.f);
        if (hl.level == Health::Paused) {
            Setting* mon = Settings::instance().find("general.monitoring");
            const Icon pl = Icon::Play;
            if (mon && button(ui, hashId("status-resume"), {hero.r() - 190.f, hero.cy() - 17.f, 170.f, 34.f}, "Resume monitoring",
                              ButtonKind::Primary, &pl)) {
                mon->set(1.0);
            }
        } else {
            const std::string issues = hl.issues == 0 ? "No issues" : std::to_string(hl.issues) + (hl.issues == 1 ? " issue" : " issues");
            chip(ui, {hero.r() - 22.f - chipWidth(ui, issues), hero.cy() - 11.f, chipWidth(ui, issues), 22.f}, issues,
                 hl.issues ? t.sevNotice : t.sevClear);
        }
        y += hero.h + 14.f;
    }

    const float gap = 14.f;
    const float cw = (w - gap * 2.f) / 3.f;
    const float ch = 246.f;
    // ── Injection & hooks ──
    {
        const Rect in = systemCard(ui, {area.x, y, cw, ch}, Icon::Plug, "INJECTION & HOOKS");
        float ry = in.y;
        if (!rs.hosted) {
            ry = statusRow(ui, in.x, ry, in.w, State::Info, "Preview harness", "simulated");
            ui.fonts.drawWrapped(ui.r, type::Caption, in.x, ry + 4.f, in.w,
                                 "In the game this card shows the JVM attachment, the client pump, the render and input hooks, "
                                 "and the menu host screen.", t.textFaint, 3.f);
        } else {
            ry = statusRow(ui, in.x, ry, in.w, rs.jvmAttached ? State::Ok : State::Warn, "Attached to the game", rs.jvmAttached ? "JVM" : "no");
            ry = statusRow(ui, in.x, ry, in.w, rs.clientPump ? State::Ok : State::Warn, "Client pump",
                           rs.clientPump ? formatNumber(rs.pumpsPerSecond, 0) + " / s" : "not running");
            ry = statusRow(ui, in.x, ry, in.w, rs.presentHook ? State::Ok : State::Warn, "Render hook",
                           rs.presentHook ? std::to_string(rs.presentSlots) + " slot" + (rs.presentSlots == 1 ? "" : "s") + " \xC2\xB7 " +
                                                formatNumber(rs.framesPerSecond, 0) + " fps"
                                          : "not installed");
            ry = statusRow(ui, in.x, ry, in.w, rs.inputHook ? State::Ok : State::Warn, "Input hook", rs.inputHook ? "window procedure" : "missing");
            ry = statusRow(ui, in.x, ry, in.w, rs.menuHost ? State::Ok : State::Off, "Menu host screen", rs.menuHost ? "ready" : "created on open");
            ry = statusRow(ui, in.x, ry, in.w, !rs.worldHookWanted ? State::Off : rs.worldHook ? State::Ok : State::Warn, "World render hook",
                           !rs.worldHookWanted ? "Suspect Highlight off" : rs.worldHook ? "Suspect Highlight" : "retrying");
            ry = statusRow(ui, in.x, ry, in.w, rs.hypixel ? State::Ok : State::Info, "Server", rs.server.empty() ? "not connected" : rs.server);
        }
    }
    // ── Detection engine ──
    {
        const Rect in = systemCard(ui, {area.x + cw + gap, y, cw, ch}, Icon::Chip, "DETECTION ENGINE");
        int enabled = 0;
        for (const auto& c : e.checks()) if (c->enabled()) ++enabled;
        const auto& st = e.stats();
        float ry = in.y;
        ry = statusRow(ui, in.x, ry, in.w, e.monitoringEnabled() ? State::Ok : State::Off, "Monitoring", e.monitoringEnabled() ? "on" : "paused");
        ry = statusRow(ui, in.x, ry, in.w, e.active() ? State::Ok : State::Info, "Context",
                       e.contextDetail().empty() ? detection::gameContextName(e.context()) : e.contextDetail());
        ry = statusRow(ui, in.x, ry, in.w, enabled > 0 ? State::Ok : State::Warn, "Checks enabled",
                       std::to_string(enabled) + " of " + std::to_string(e.checks().size()));
        ry = statusRow(ui, in.x, ry, in.w, st.avgTickMicros < 2000.0 ? State::Ok : State::Warn, "Tick cost (avg / peak)",
                       formatNumber(st.avgTickMicros / 1000.0, 2) + " / " + formatNumber(st.peakTickMicros / 1000.0, 1) + " ms");
        ry = statusRow(ui, in.x, ry, in.w, State::Info, "Players tracked", std::to_string(st.trackedPlayers));
        ry = statusRow(ui, in.x, ry, in.w, State::Info, "Ticks analysed", std::to_string(st.ticksProcessed));
        ry = statusRow(ui, in.x, ry, in.w, st.skippedTicks < 200 ? State::Info : State::Warn, "Skipped ticks", std::to_string(st.skippedTicks));
    }
    // ── Signals ──
    {
        const Rect in = systemCard(ui, {area.x + (cw + gap) * 2.f, y, cw, ch}, Icon::Pulse, "SIGNALS FROM THE GAME");
        float ry = in.y;
        if (rs.signals.empty()) {
            ry = statusRow(ui, in.x, ry, in.w, State::Info, rs.hosted ? "Waiting for the game world" : "Simulated signals", "");
        }
        for (const auto& s : rs.signals) {
            if (ry > in.b() - 10.f) break;
            ry = statusRow(ui, in.x, ry, in.w, s.available ? State::Ok : (rs.samplerResolved ? State::Warn : State::Off), s.name,
                           s.available ? "" : "feeds " + s.feeds);
        }
    }
    y += ch + gap;

    // ── Services and data ──
    {
        const Rect in = systemCard(ui, {area.x, y, cw, ch}, Icon::Utilities, "UTILITIES");
        const auto& us = utilities::settings();
        const auto live = utilities::liveState();
        float ry = in.y;
        ry = statusRow(ui, in.x, ry, in.w, !rs.hosted ? State::Info : rs.chatReader ? State::Ok : State::Warn, "Chat reader",
                       !rs.hosted ? "preview" : rs.chatReader ? std::to_string(rs.chatLines) + " lines read" : "unavailable");
        struct U { const char* name; Setting* s; };
        const U list[] = {{"Auto Who", us.autoWho}, {"Fast Buy", us.fastBuy}, {"Party Detector", us.partyChat}, {"Party Queue Detector", us.partyQueue}};
        for (const U& u : list) ry = statusRow(ui, in.x, ry, in.w, u.s && u.s->b() ? State::Ok : State::Off, u.name, u.s && u.s->b() ? "on" : "off");
        if (live.bedwarsPregame) ry = statusRow(ui, in.x, ry, in.w, State::Info, "Bed Wars lobby", live.mode.empty() ? "mode unknown" : live.mode);
    }
    {
        const Rect in = systemCard(ui, {area.x + cw + gap, y, cw, ch}, Icon::Globe, "IDENTITY & NETWORK");
        const auto& is = identity::settings();
        const auto st = identity::Service::instance().stats();
        float ry = in.y;
        ry = statusRow(ui, in.x, ry, in.w, is.skinDenick && is.skinDenick->b() ? State::Ok : State::Off, "Skin denicker",
                       is.skinDenick && is.skinDenick->b() ? (is.autoLookup && is.autoLookup->b() ? "automatic" : "on request") : "off");
        const bool nd = is.numberDenick && is.numberDenick->b();
        const bool key = is.bordicKey && !is.bordicKey->text.empty();
        ry = statusRow(ui, in.x, ry, in.w, !nd ? State::Off : key ? State::Ok : State::Warn, "Number denicker", !nd ? "off" : key ? "on" : "needs a key");
        ry = statusRow(ui, in.x, ry, in.w, State::Info, "Nicked players seen", std::to_string(identity::Service::instance().nicked().size()));
        ry = statusRow(ui, in.x, ry, in.w, st.failures ? State::Warn : State::Info, "Lookups",
                       std::to_string(st.requests) + " sent \xC2\xB7 " + std::to_string(st.failures) + " failed");
        if (st.pending) ry = statusRow(ui, in.x, ry, in.w, State::Info, "Queued", std::to_string(st.pending));
        if (!st.lastError.empty()) ry = statusRow(ui, in.x, ry, in.w, State::Warn, "Last error", st.lastError);
        ui.fonts.drawWrapped(ui.r, type::Caption, in.x, ry + 2.f, in.w,
                             "No analytics or telemetry. The network is used only by the denickers above.", t.textFaint, 3.f);
    }
    {
        const Rect in = systemCard(ui, {area.x + (cw + gap) * 2.f, y, cw, ch}, Icon::Database, "DATA");
        float ry = in.y;
        const std::string dir = ctx.host ? ctx.host->dataDirectory() : std::string();
        ry = statusRow(ui, in.x, ry, in.w, State::Ok, "Settings", "auto-saved");
        ry = statusRow(ui, in.x, ry, in.w, Settings::instance().getBool("data.history") ? State::Ok : State::Off, "Player history",
                       Settings::instance().getBool("data.history") ? "on" : "off");
        ry = statusRow(ui, in.x, ry, in.w, Settings::instance().getBool("data.journal") ? State::Ok : State::Off, "Session journal",
                       Settings::instance().getBool("data.journal") ? "on" : "off");
        ui.fonts.draw(ui.r, type::Caption, in.x, ry + 4.f, "Location", t.textFaint);
        ui.fonts.drawWrapped(ui.r, type::Mono, in.x, ry + 22.f, in.w, dir.empty() ? "\xE2\x80\x94" : dir, t.textMuted, 2.f, 2);
        const Icon f = Icon::Folder;
        if (ctx.host && button(ui, hashId("status-open-data"), {in.x, in.b() - 34.f, buttonWidth(ui, "Open folder", true), 32.f}, "Open folder",
                               ButtonKind::Secondary, &f)) {
            ctx.host->openFolder(dir);
        }
    }
    y += ch + gap;

    // ── Runtime log ──
    {
        const auto lines = log::recent(16);
        const float lh = 54.f + std::max<std::size_t>(1, lines.size()) * 19.f + 10.f;
        const Rect c{area.x, y, w, lh};
        card(ui, c);
        drawIcon(ui.r, Icon::Layers, c.x + 20.f, c.y + 17.f, 16.f, t.textMuted);
        ui.fonts.draw(ui.r, type::Overline, c.x + 44.f, c.y + 19.f, "RUNTIME LOG", t.textFaint);
        ui.fonts.draw(ui.r, type::Caption, c.r() - 22.f, c.y + 18.f, "newest last \xC2\xB7 full logs in the data folder", t.textFaint, Align::Right);
        float ly = c.y + 48.f;
        if (lines.empty()) ui.fonts.draw(ui.r, type::Small, c.x + 22.f, ly, "Nothing logged yet.", t.textFaint);
        for (const auto& l : lines) {
            ui.fonts.draw(ui.r, type::Mono, c.x + 22.f, ly, l, t.textMuted, Align::Left, c.w - 44.f);
            ly += 19.f;
        }
        y += lh + gap;
    }
    endScroll(ui, sid, view, y - (top + off));
}

}  // namespace vigil::ui
