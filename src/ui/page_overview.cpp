// Overview — live monitoring at a glance: session metrics, the activity feed and the watchlist.

#include "pages.h"

#include <algorithm>
#include <cmath>

namespace vigil::ui {
namespace {

struct FeedItem {
    const detection::PlayerRecord* player;
    const detection::Evidence* evidence;
    const detection::Detection* detection;
    double timeMs;
};

// Entrance progress for list items: 0 on first sight, eases to 1.
float entrance(Ui& ui, Id id) {
    float& v = ui.value(id, -1.f);
    if (v < 0.f) v = 0.f;
    v = approach(v, 1.f, 9.f, ui.dt);
    return ease::outCubic(v);
}

void statTile(PageContext& ctx, const Rect& rc, Icon icon, const char* label, double value, const std::string& sub, Color tint,
              Id id) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    card(ui, rc);
    const Rect ic{rc.x + 18.f, rc.y + 18.f, 34.f, 34.f};
    ui.r.roundRect(ic, 10.f, tint.withAlpha(t.light ? 0.10f : 0.12f));
    drawIcon(ui.r, icon, ic.x + 8.f, ic.y + 8.f, 18.f, tint);
    ui.fonts.draw(ui.r, type::Overline, rc.x + 64.f, rc.y + 21.f, label, t.textFaint);
    const float shown = ui.anim(id, static_cast<float>(value), 6.f);
    ui.fonts.draw(ui.r, type::Metric, rc.x + 64.f, rc.y + 34.f, formatNumber(std::round(shown), 0), t.text);
    ui.fonts.draw(ui.r, type::Small, rc.x + 18.f, rc.b() - 26.f, sub, t.textMuted, Align::Left, rc.w - 36.f);
}

}  // namespace

void drawOverview(PageContext& ctx, const Rect& area) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    const auto& session = ctx.engine.session();
    const auto& checks = ctx.engine.checks();

    std::string subtitle;
    if (!ctx.engine.monitoringEnabled()) subtitle = "Monitoring is paused. Turn it on in Settings to resume observation.";
    else if (ctx.engine.active()) {
        subtitle = "Observing " + std::to_string(ctx.engine.stats().trackedPlayers) + " players";
        if (!ctx.engine.contextDetail().empty()) subtitle += " \xC2\xB7 " + ctx.engine.contextDetail();
    } else {
        subtitle = std::string("Standing by \xC2\xB7 ") + detection::gameContextName(ctx.engine.context()) +
                   ". Checks run during games and replays.";
    }
    pageHeader(ctx, area, "Overview", subtitle);

    // Metrics.
    int watch = 0, present = 0;
    for (const auto& r : session.players()) {
        if (r.local) continue;
        if (r.present) ++present;
        if (r.suspicion >= 50.f) ++watch;
    }
    const float gap = 14.f;
    const float tileW = (area.w - gap * 3.f) / 4.f;
    const float ty = area.y + 70.f;
    const float tileH = 112.f;
    statTile(ctx, {area.x, ty, tileW, tileH}, Icon::Players, "PLAYERS OBSERVED", static_cast<double>(session.players().size()),
             std::to_string(present) + " in view now", t.chart[0], hashId("tile-players"));
    statTile(ctx, {area.x + (tileW + gap), ty, tileW, tileH}, Icon::Layers, "EVIDENCE", session.totalEvidence(),
             "Observations across " + std::to_string(checks.size()) + " checks", t.chart[1], hashId("tile-evidence"));
    statTile(ctx, {area.x + (tileW + gap) * 2.f, ty, tileW, tileH}, Icon::Bolt, "DETECTIONS", session.totalDetections(),
             session.totalDetections() ? "Evidence-gated alerts" : "None this session", t.sevAlert, hashId("tile-det"));
    statTile(ctx, {area.x + (tileW + gap) * 3.f, ty, tileW, tileH}, Icon::Target, "UNDER WATCH", watch,
             "Suspicion at or above 50%", t.sevNotice, hashId("tile-watch"));

    // Lower cards.
    const float by = ty + tileH + gap;
    const float bh = area.b() - by;
    const float leftW = std::floor((area.w - gap) * 0.6f);
    const Rect feed{area.x, by, leftW, bh};
    const Rect watchCard{area.x + leftW + gap, by, area.w - leftW - gap, bh};

    // ── Live activity ──
    card(ui, feed);
    sectionHeader(ui, feed.x + 20.f, feed.y + 18.f, "LIVE ACTIVITY");
    std::vector<FeedItem> items;
    for (const auto& r : session.players()) {
        for (const auto& d : r.detections) items.push_back({&r, nullptr, &d, d.timeMs});
        int n = 0;
        for (auto it = r.evidence.rbegin(); it != r.evidence.rend() && n < 30; ++it, ++n) items.push_back({&r, &*it, nullptr, it->timeMs});
    }
    std::sort(items.begin(), items.end(), [](const FeedItem& a, const FeedItem& b) { return a.timeMs > b.timeMs; });
    if (items.size() > 40) items.resize(40);

    const Rect feedView{feed.x + 8.f, feed.y + 44.f, feed.w - 16.f, feed.h - 52.f};
    if (items.empty()) {
        emptyState(ui, feedView, Icon::Pulse, "Watching quietly",
                   "Every player in view is analyzed during games. Observations appear here the moment behavior "
                   "departs from what vanilla Minecraft allows.");
    } else {
        const Id sid = hashId("overview-feed");
        const float off = beginScroll(ui, sid, feedView);
        float y = feedView.y + off;
        for (const FeedItem& it : items) {
            const bool isDet = it.detection != nullptr;
            const Id iid = mixId(hashId(isDet ? "fd" : "fe"), isDet ? it.detection->id : it.evidence->id);
            const float e = entrance(ui, iid);
            const float rowH = 56.f;
            const Rect row{feedView.x, y, feedView.w, rowH};
            if (row.b() >= feedView.y && row.y <= feedView.b()) {
                ui.r.pushAlpha(e);
                ui.r.pushOffset((1.f - e) * -10.f * motionLevel(), 0.f);
                bool h = false;
                if (ui.pressable(iid, row, &h)) {
                    ctx.openPlayer(it.player->key);
                    if (isDet) ctx.state.selectedDetection = it.detection->id;
                    else ctx.state.selectedEvidence = it.evidence->id;
                }
                const float hv = ui.anim(mixId(iid, 1), h ? 1.f : 0.f, 16.f);
                if (isDet) ui.r.roundRect(row.inset(4.f, 3.f), 10.f, t.sevAlert.withAlpha(0.07f + 0.05f * hv));
                else if (hv > 0.01f) ui.r.roundRect(row.inset(4.f, 3.f), 10.f, t.surfaceHover.alpha(hv));
                const int ci = isDet ? it.detection->check : it.evidence->check;
                const auto& chk = *checks[static_cast<std::size_t>(ci)];
                const float conf = isDet ? it.detection->confidence : it.evidence->confidenceAfter;
                const Color sev = t.severity(conf);
                const Rect ic{row.x + 14.f, row.cy() - 15.f, 30.f, 30.f};
                ui.r.roundRect(ic, 9.f, sev.withAlpha(0.13f));
                drawIcon(ui.r, isDet ? Icon::Bolt : checkIcon(chk.id()), ic.x + 6.f, ic.y + 6.f, 18.f, sev);
                const float nx = row.x + 56.f;
                const float nw = ui.fonts.draw(ui.r, type::BodyStrong, nx, row.y + 10.f, it.player->name, t.text);
                const std::string tag = isDet ? "Detection \xC2\xB7 " + chk.name() : chk.name();
                const float cw = chipWidth(ui, tag);
                chip(ui, {nx + nw + 8.f, row.y + 9.f, cw, 19.f}, tag, isDet ? t.sevAlert : t.textMuted);
                const std::string line = isDet ? it.detection->summary : it.evidence->summary;
                ui.fonts.draw(ui.r, type::Small, nx, row.y + 31.f, line, t.textMuted, Align::Left, row.w - 160.f);
                ui.fonts.draw(ui.r, type::Caption, row.r() - 14.f, row.y + 12.f, agoLabel(ctx.nowMs, it.timeMs), t.textFaint, Align::Right);
                ui.fonts.draw(ui.r, type::Caption, row.r() - 14.f, row.y + 31.f, formatNumber(conf, 0) + "%", sev, Align::Right);
                ui.r.popOffset();
                ui.r.popAlpha();
            }
            y += rowH;
        }
        endScroll(ui, sid, feedView, y - (feedView.y + off) + 8.f);
    }

    // ── Watchlist ──
    card(ui, watchCard);
    sectionHeader(ui, watchCard.x + 20.f, watchCard.y + 18.f, "WATCHLIST");
    auto players = sortedPlayers(session, 0, {}, false);
    players.erase(std::remove_if(players.begin(), players.end(),
                                 [](const detection::PlayerRecord* p) { return p->suspicion < 1.f && p->peakSuspicion < 10.f; }),
                  players.end());
    const Rect wl{watchCard.x + 8.f, watchCard.y + 44.f, watchCard.w - 16.f, watchCard.h - 84.f};
    if (players.empty()) {
        emptyState(ui, wl, Icon::Target, "No one stands out", "Players with rising suspicion are ranked here.");
    } else {
        float y = wl.y;
        const double t1 = ctx.nowMs, t0 = t1 - 5 * 60 * 1000.0;
        for (const detection::PlayerRecord* p : players) {
            if (y + 58.f > wl.b()) break;
            const Rect row{wl.x, y, wl.w, 58.f};
            const Id rid = hashId(p->key, 991);
            bool h = false;
            if (ui.pressable(rid, row, &h)) ctx.openPlayer(p->key);
            const float hv = ui.anim(mixId(rid, 1), h ? 1.f : 0.f, 16.f);
            if (hv > 0.01f) ui.r.roundRect(row.inset(4.f, 3.f), 10.f, t.surfaceHover.alpha(hv));
            confidenceRing(ui, mixId(rid, 2), row.x + 32.f, row.cy(), 17.f, p->suspicion, 3.5f, true);
            const float nx = row.x + 60.f;
            ui.fonts.draw(ui.r, type::BodyStrong, nx, row.y + 11.f, p->name, p->present ? t.text : t.textMuted, Align::Left, row.w - 170.f);
            const int top = p->topCheck();
            const std::string detail = top >= 0 ? checks[static_cast<std::size_t>(top)]->name() + " \xC2\xB7 " +
                                                      formatNumber(p->checks[static_cast<std::size_t>(top)].model.value(), 0) + "%"
                                                : "Peak " + formatNumber(p->peakSuspicion, 0) + "%";
            ui.fonts.draw(ui.r, type::Small, nx, row.y + 31.f, detail + (p->present ? "" : " \xC2\xB7 left"), t.textFaint,
                          Align::Left, row.w - 170.f);
            sparkline(ui, {row.r() - 96.f, row.y + 14.f, 80.f, 30.f}, p->suspicionHistory, t0, t1, t.severity(p->suspicion));
            y += 58.f;
        }
    }
    // Engine diagnostics footer.
    const auto& st = ctx.engine.stats();
    const std::string diag = formatNumber(st.avgTickMicros / 1000.0, 2) + " ms per tick \xC2\xB7 " +
                             std::to_string(st.combatEvents) + " hits attributed";
    divider(ui, watchCard.x + 18.f, watchCard.r() - 18.f, watchCard.b() - 36.f);
    drawIcon(ui.r, Icon::Pulse, watchCard.x + 18.f, watchCard.b() - 26.f, 14.f, t.textFaint, 1.4f);
    ui.fonts.draw(ui.r, type::Caption, watchCard.x + 38.f, watchCard.b() - 25.f, diag, t.textFaint, Align::Left, watchCard.w - 56.f);
}

}  // namespace vigil::ui
