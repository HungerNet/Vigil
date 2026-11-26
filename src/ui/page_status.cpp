// Status — is Vigil healthy? Injection and hooks, the detection engine, signals from the game,
// services (utilities, chat, identity), data, and a live runtime log.

#include "../core/log.h"
#include "../identity/identity.h"
#include "../utilities/utilities.h"
#include "core/version.h"
#include "pages.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>

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

std::string yesNo(bool b) { return b ? "yes" : "no"; }

std::string localStamp(const char* format) {
    const std::time_t now = std::time(nullptr);
    std::tm tmv{};
    localtime_s(&tmv, &now);
    char buf[64];
    std::strftime(buf, sizeof(buf), format, &tmv);
    return buf;
}

}  // namespace

// Everything a developer needs to understand a live problem, without player names: versions,
// hooks, signals and the bindings behind them, the engine, services, records counts, settings you
// changed (never keys or secrets) and recent runtime log lines (docs/LIVE_VALIDATION.md).
std::string buildDiagnosticsReport(const PageContext& ctx) {
    const auto& e = ctx.engine;
    RuntimeStatus rs;
    if (ctx.host) rs = ctx.host->runtimeStatus();
    const Health hl = computeHealth(ctx);
    std::string r;
    auto line = [&r](const std::string& s) { r += s + "\r\n"; };
    line("Vigil diagnostics report");
    line("Generated " + localStamp("%Y-%m-%d %H:%M:%S"));
    line("Version " + std::string(VIGIL_VERSION_STRING) + " (build " + VIGIL_BUILD_ID + ")" +
         (rs.hosted ? " \xC2\xB7 runtime " + rs.runtimeVersion + " (build " + rs.runtimeBuild + ")" : " \xC2\xB7 preview harness"));
    line("Health: " + hl.title + " \xE2\x80\x94 " + hl.detail);
    line("");
    line("[Injection & hooks]");
    if (rs.hosted) {
        line("uptime " + formatDuration(rs.uptimeSec) + " \xC2\xB7 JVM attached " + yesNo(rs.jvmAttached) + " \xC2\xB7 client pump " +
             (rs.clientPump ? formatNumber(rs.pumpsPerSecond, 0) + "/s" : "no"));
        line("render hook " + (rs.presentHook ? std::to_string(rs.presentSlots) + " slot(s), " + formatNumber(rs.framesPerSecond, 0) + " fps" : "no") +
             " \xC2\xB7 input hook " + yesNo(rs.inputHook) + " \xC2\xB7 menu host " + (rs.menuHost ? "ready" : "created on open"));
        line("world hook " + std::string(!rs.worldHookWanted ? "not needed yet" : rs.worldHook ? "installed" : "retrying") + " \xC2\xB7 server " +
             (rs.server.empty() ? "none" : rs.server) + (rs.hypixel ? " (Hypixel)" : ""));
    } else {
        line("preview (simulated)");
    }
    line("");
    line("[Engine]");
    int enabled = 0;
    for (const auto& c : e.checks()) if (c->enabled()) ++enabled;
    const auto& st = e.stats();
    line("monitoring " + std::string(e.monitoringEnabled() ? "on" : "paused") + " \xC2\xB7 phase " + detection::gamePhaseName(e.phase()) +
         (e.contextDetail().empty() ? "" : " (" + e.contextDetail() + ")") + " \xC2\xB7 checks " + std::to_string(enabled) + "/" +
         std::to_string(e.checks().size()));
    line("tick " + formatNumber(st.avgTickMicros / 1000.0, 2) + " ms avg, " + formatNumber(st.peakTickMicros / 1000.0, 1) + " ms peak \xC2\xB7 players " +
         std::to_string(st.trackedPlayers) + " \xC2\xB7 NPCs ignored " + std::to_string(st.filteredEntities) + " \xC2\xB7 stalls " +
         std::to_string(st.stalls) + " \xC2\xB7 skipped ticks " + std::to_string(st.skippedTicks) + " \xC2\xB7 combat events " +
         std::to_string(st.combatEvents));
    line("session: " + std::to_string(e.session().players().size()) + " players, " + std::to_string(e.session().totalEvidence()) +
         " observations, " + std::to_string(e.session().totalDetections()) + " detections, matches " + std::to_string(e.match().matchCount()));
    line("");
    line("[Signals]");
    if (rs.signals.empty()) line(rs.hosted ? "not resolved yet (no world loaded)" : "simulated");
    for (const auto& s : rs.signals) line(std::string(s.available ? "  ok       " : "  MISSING  ") + s.name + "  (feeds " + s.feeds + ")");
    if (rs.bindingsTotal > 0) {
        line("bindings: " + std::to_string(rs.bindingsTotal) + " total, " + std::to_string(rs.bindingsMissing) + " missing, " +
             std::to_string(rs.bindingsObfuscated) + " via obfuscated fallback names");
        for (const auto& n : rs.bindingNotes) line("  " + n);
    }
    line("chat reader " + (rs.hosted ? (rs.chatReader ? "yes, " + std::to_string(rs.chatLines) + " lines" : "unavailable") : std::string("preview")));
    line("");
    line("[Records & data]");
    const auto& marks = e.marks();
    line(std::to_string(marks.confirmedCount()) + " confirmed \xC2\xB7 " + std::to_string(marks.watchedCount() - marks.confirmedCount()) +
         " watching \xC2\xB7 " + std::to_string(marks.exemptCount()) + " exempt" + (marks.readOnly() ? " \xC2\xB7 " + marks.loadNote() : ""));
    line("");
    line("[Settings changed from defaults]");
    int changed = 0;
    for (const auto& sp : Settings::instance().all()) {
        const Setting& s = *sp;
        if (s.isDefault() || s.group.empty()) continue;
        if (s.type == SettingType::Text) { line("  " + s.key + " = (text, not included)"); ++changed; continue; }
        line("  " + s.key + " = " + (s.type == SettingType::Bool ? std::string(s.b() ? "on" : "off")
                                     : s.type == SettingType::Choice && s.i() >= 0 && s.i() < static_cast<int>(s.choices.size())
                                         ? s.choices[static_cast<std::size_t>(s.i())]
                                         : formatSettingValue(s, s.value)));
        ++changed;
    }
    if (changed == 0) line("  none");
    line("");
    line("[Recent runtime log] (detection lines omitted: they name players)");
    for (const auto& l : log::recent(60)) {
        if (l.find("  detections  ") != std::string::npos) continue;
        line("  " + l);
    }
    return r;
}

namespace {

// Diagnostics: copy or save the report; record a trace of what Vigil sees.
float diagnosticsCard(PageContext& ctx, float x, float y, float w) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    RuntimeStatus rs;
    if (ctx.host) rs = ctx.host->runtimeStatus();
    const TraceInfo ti = ctx.host ? ctx.host->traceInfo() : TraceInfo{};
    const Rect c{x, y, w, 178.f};
    card(ui, c);
    drawIcon(ui.r, Icon::Pulse, c.x + 20.f, c.y + 17.f, 16.f, t.textMuted);
    ui.fonts.draw(ui.r, type::Overline, c.x + 44.f, c.y + 19.f, "DIAGNOSTICS", t.textFaint);
    ui.fonts.draw(ui.r, type::Caption, c.r() - 22.f, c.y + 18.f, "for bug reports and live validation", t.textFaint, Align::Right);
    float ry = c.y + 48.f;
    const float cx = c.x + 20.f, cw = c.w * 0.5f - 30.f;
    ry = statusRow(ui, cx, ry, cw, State::Info, "Version", std::string(VIGIL_VERSION_STRING) + " \xC2\xB7 build " + VIGIL_BUILD_ID);
    if (rs.hosted && rs.bindingsTotal > 0) {
        const State bs = rs.bindingsMissing > 0 ? State::Warn : State::Ok;
        const std::string v = std::to_string(rs.bindingsTotal - rs.bindingsMissing) + "/" + std::to_string(rs.bindingsTotal) +
                              (rs.bindingsObfuscated ? " \xC2\xB7 " + std::to_string(rs.bindingsObfuscated) + " via fallback" : "");
        statusRow(ui, cx, ry, cw, bs, "Game bindings", v);
        std::string tip = "Members of Minecraft that Vigil reads.";
        for (std::size_t i = 0; i < rs.bindingNotes.size() && i < 12; ++i) tip += "\n" + rs.bindingNotes[i];
        ui.tooltip(hashId("diag-bindings"), {cx, ry - 4.f, cw, 24.f}, tip);
        ry += 26.f;
    } else {
        ry = statusRow(ui, cx, ry, cw, State::Off, "Game bindings", rs.hosted ? "resolved when a world loads" : "preview");
    }
    const std::string traceState = !ti.supported ? "in game only"
                                 : ti.active    ? "recording \xC2\xB7 " + std::to_string(ti.frames) + " frames \xC2\xB7 " +
                                                   formatNumber(static_cast<double>(ti.bytes) / (1024.0 * 1024.0), 1) + " MB"
                                 : ti.path.empty() ? "off"
                                                   : "last: " + std::filesystem::path(ti.path).filename().string() +
                                                         (ti.stopReason.empty() ? "" : " (" + ti.stopReason + ")");
    statusRow(ui, cx, ry, cw, ti.active ? State::Ok : State::Off, "Trace recording", traceState);

    // Actions (right column).
    const float bx = c.x + c.w * 0.5f + 10.f;
    float by = c.y + 46.f;
    static std::string s_note;
    static double s_noteAt = -100.0;
    const Icon copy = Icon::Copy, save = Icon::Save, rec = ti.active ? Icon::Close : Icon::Dot, folder = Icon::Folder;
    const float b1 = buttonWidth(ui, "Copy report", true);
    if (button(ui, hashId("diag-copy"), {bx, by, b1, 32.f}, "Copy report", ButtonKind::Secondary, &copy)) {
        copyToClipboard(buildDiagnosticsReport(ctx));
        s_note = "Report copied. Paste it into your bug report.";
        s_noteAt = ui.time;
    }
    const float b2 = buttonWidth(ui, "Save report", true);
    if (button(ui, hashId("diag-save"), {bx + b1 + 8.f, by, b2, 32.f}, "Save report", ButtonKind::Ghost, &save) && ctx.host) {
        const std::filesystem::path dir = std::filesystem::path(ctx.host->dataDirectory()) / "diagnostics";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const std::filesystem::path file = dir / ("report-" + localStamp("%Y%m%d-%H%M%S") + ".txt");
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << buildDiagnosticsReport(ctx);
        s_note = out ? "Saved " + file.filename().string() + " in the diagnostics folder." : "The report could not be saved.";
        s_noteAt = ui.time;
    }
    by += 42.f;
    if (ti.supported) {
        const char* lbl = ti.active ? "Stop recording" : "Record a trace";
        const float b3 = buttonWidth(ui, lbl, true);
        if (button(ui, hashId("diag-trace"), {bx, by, b3, 32.f}, lbl, ti.active ? ButtonKind::Danger : ButtonKind::Secondary, &rec)) {
            ctx.host->setTraceRecording(!ti.active);
        }
        ui.tooltip(hashId("diag-trace-tip"), {bx, by, b3, 32.f},
                   "Records exactly what Vigil sees (positions, states, chat) so a developer can replay it. Stops by itself "
                   "after 15 minutes or 64 MB. Traces include player names and stay in your data folder.");
        if (iconButton(ui, hashId("diag-folder"), {bx + b3 + 8.f, by + 1.f, 30.f, 30.f}, Icon::Folder, "Open the traces folder") && ctx.host) {
            const std::filesystem::path dir = std::filesystem::path(ctx.host->dataDirectory()) / "traces";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            ctx.host->openFolder(dir.string());
        }
        (void)folder;
    }
    by += 42.f;
    const bool noteFresh = ui.time - s_noteAt < 5.0;
    ui.fonts.drawWrapped(ui.r, type::Caption, bx, by, c.r() - 20.f - bx,
                         noteFresh ? s_note : "Reports never include player names, keys or your notes.", noteFresh ? t.sevClear : t.textFaint, 3.f, 2);
    return c.h;
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
                           !rs.worldHookWanted ? "installed when first needed" : rs.worldHook ? "Suspect Highlight" : "retrying");
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
        const std::string phase = std::string(detection::gamePhaseName(e.phase())) +
                                  (e.contextDetail().empty() ? std::string() : " \xC2\xB7 " + e.contextDetail());
        ry = statusRow(ui, in.x, ry, in.w, e.active() ? State::Ok : State::Info, "Game phase",
                       phase + (e.active() ? "" : e.recording() ? " \xC2\xB7 listing only" : " \xC2\xB7 idle"));
        ry = statusRow(ui, in.x, ry, in.w, enabled > 0 ? State::Ok : State::Warn, "Checks enabled",
                       std::to_string(enabled) + " of " + std::to_string(e.checks().size()));
        ry = statusRow(ui, in.x, ry, in.w, st.avgTickMicros < 2000.0 ? State::Ok : State::Warn, "Tick cost (avg / peak)",
                       formatNumber(st.avgTickMicros / 1000.0, 2) + " / " + formatNumber(st.peakTickMicros / 1000.0, 1) + " ms");
        ry = statusRow(ui, in.x, ry, in.w, State::Info, "Players \xC2\xB7 NPCs ignored",
                       std::to_string(st.trackedPlayers) + " \xC2\xB7 " + std::to_string(st.filteredEntities));
        ry = statusRow(ui, in.x, ry, in.w, State::Info, "Server stalls skipped", std::to_string(st.stalls));
        ry = statusRow(ui, in.x, ry, in.w, st.skippedTicks < 200 ? State::Info : State::Warn, "Skipped ticks", std::to_string(st.skippedTicks));
    }
    // ── Signals ──
    {
        const Rect in = systemCard(ui, {area.x + (cw + gap) * 2.f, y, cw, ch}, Icon::Pulse, "SIGNALS FROM THE GAME");
        float ry = in.y;
        if (rs.signals.empty()) {
            ry = statusRow(ui, in.x, ry, in.w, State::Info, rs.hosted ? "Waiting for the game world" : "Simulated signals", "");
        }
        // Missing signals first, as full rows that say what they feed; available ones in two columns.
        for (const auto& s : rs.signals) {
            if (s.available || ry > in.b() - 10.f) continue;
            ry = statusRow(ui, in.x, ry, in.w, rs.samplerResolved ? State::Warn : State::Off, s.name, "feeds " + s.feeds);
        }
        int col = 0;
        const float colW = (in.w - 12.f) * 0.5f;
        for (const auto& s : rs.signals) {
            if (!s.available) continue;
            if (ry > in.b() - 10.f) break;
            statusRow(ui, in.x + col * (colW + 12.f), ry, colW, State::Ok, s.name, "");
            if (!s.feeds.empty()) ui.tooltip(hashId("sig", hashId(s.name)), {in.x + col * (colW + 12.f), ry - 4.f, colW, 24.f}, "Feeds " + s.feeds);
            if (++col == 2) { col = 0; ry += 26.f; }
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

    // ── Diagnostics ──
    y += diagnosticsCard(ctx, area.x, y, w) + gap;

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
