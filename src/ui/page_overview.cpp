// Overview — what is happening right now: health and context, session metrics, live activity
// grouped by player and check, and the watchlist.

#include "pages.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace vigil::ui {
namespace {

struct Group {
    const detection::PlayerRecord* player = nullptr;
    int check = -1;
    int count = 0;
    bool detection = false;
    double lastMs = 0.0;
    float confidence = 0.f;
    std::string summary;
    std::uint64_t evidenceId = 0;
    std::uint64_t detectionId = 0;
};

// Entrance progress for list items: 0 on first sight, eases to 1.
float entrance(Ui& ui, Id id) {
    float& v = ui.value(id, -1.f);
    if (v < 0.f) v = 0.f;
    v = approach(v, 1.f, 3.f / motion::kListItem, ui.dt);
    return ease::outCubic(v);
}

void metric(Ui& ui, float x, float y, const char* label, double value, const std::string& sub, Color tint, Id id) {
    const Theme& t = ui.th();
    ui.r.circle(x + 4.f, y + 6.f, 3.5f, tint);
    ui.fonts.draw(ui.r, type::Overline, x + 14.f, y, label, t.textFaint);
    const float shown = ui.anim(id, static_cast<float>(value), 6.f);
    ui.fonts.draw(ui.r, type::Metric, x, y + 16.f, formatNumber(std::round(shown), 0), t.text);
    ui.fonts.draw(ui.r, type::Caption, x, y + 54.f, sub, t.textMuted);
}

}  // namespace

void drawOverview(PageContext& ctx, const Rect& area) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    const auto& session = ctx.engine.session();
    const auto& checks = ctx.engine.checks();
    const float top = pageHeader(ctx, area, "Overview", pagePurpose(Page::Overview));

    // ── Hero: health + metrics ──
    const Health hl = computeHealth(ctx);
    const Rect hero{area.x, top, area.w, 116.f};
    card(ui, hero);
    {
        const Color hc = healthColor(t, hl.level);
        const Rect badge{hero.x + 22.f, hero.y + 24.f, 44.f, 44.f};
        ui.r.roundRect(badge, 12.f, hc.withAlpha(0.13f));
        if (hl.level == Health::Active && motionLevel() > 0.01f) {
            const float k = static_cast<float>(std::fmod(ui.time, 2.4) / 2.4);
            ui.r.roundRectStroke(badge.inset(-6.f * ease::outCubic(k)), 12.f + 6.f * k, 1.5f, hc.withAlpha(0.35f * (1.f - k)));
        }
        drawIcon(ui.r, hl.level == Health::Active ? Icon::Eye : hl.level == Health::Degraded ? Icon::Warning : Icon::Pulse,
                 badge.x + 11.f, badge.y + 11.f, 22.f, hc);
        const std::string title = hl.level == Health::Active ? "Watching " + std::to_string(ctx.engine.stats().trackedPlayers) + " players"
                                                             : hl.title;
        std::string detail = hl.detail;
        const int npcs = ctx.engine.stats().filteredEntities;
        if (hl.level == Health::Active && npcs > 0) detail += " \xC2\xB7 " + std::to_string(npcs) + (npcs == 1 ? " NPC ignored" : " NPCs ignored");
        const float textW = hero.w * 0.44f - (badge.r() + 16.f - hero.x) - 30.f;
        ui.fonts.draw(ui.r, type::Title, badge.r() + 16.f, hero.y + 26.f, title, t.text, Align::Left, textW);
        ui.fonts.draw(ui.r, type::Small, badge.r() + 16.f, hero.y + 50.f, detail, t.textMuted, Align::Left, textW);
        bool h = false;
        const Rect link{badge.r() + 12.f, hero.y + 72.f, 120.f, 24.f};
        if (ui.pressable(hashId("ov-status"), link, &h)) ctx.navigate(Page::Status);
        ui.fonts.draw(ui.r, type::SmallStrong, link.x + 4.f, link.y + 4.f, "View status", h ? t.text : t.accent);
        drawIcon(ui.r, Icon::ArrowRight, link.x + 8.f + ui.fonts.width(type::SmallStrong, "View status"), link.y + 5.f, 13.f,
                 h ? t.text : t.accent);
    }
    int flagged = 0, inGame = 0, sessionPlayers = 0;
    for (const auto& r : session.players()) {
        if (r.local) continue;
        ++sessionPlayers;
        if (!ctx.engine.inThisGame(r)) continue;
        ++inGame;
        if (r.suspicion >= 40.f) ++flagged;
    }
    {
        const float mx0 = hero.x + hero.w * 0.44f;
        const float mw = (hero.r() - 22.f - mx0) / 4.f;
        const float my = hero.y + 24.f;
        ui.r.rect({mx0 - 22.f, hero.y + 20.f, 1.f, hero.h - 40.f}, t.divider);
        metric(ui, mx0, my, "THIS GAME", static_cast<double>(inGame), std::to_string(sessionPlayers) + " this session", t.chart[0],
               hashId("m-players"));
        metric(ui, mx0 + mw, my, "EVIDENCE", session.totalEvidence(), std::to_string(checks.size()) + " checks", t.chart[1],
               hashId("m-evidence"));
        metric(ui, mx0 + mw * 2.f, my, "DETECTIONS", session.totalDetections(),
               session.totalDetections() ? "evidence-gated" : "none this session", t.sevAlert, hashId("m-det"));
        metric(ui, mx0 + mw * 3.f, my, "FLAGGED", flagged, "elevated or higher", t.sevNotice, hashId("m-watch"));
    }

    const float gap = 14.f;
    const float by = hero.b() + gap;
    const float bh = area.b() - by;
    const float leftW = std::floor((area.w - gap) * 0.6f);
    const Rect feed{area.x, by, leftW, bh};
    const Rect watchCard{area.x + leftW + gap, by, area.w - leftW - gap, bh};

    // ── Live activity, grouped by player and check ──
    card(ui, feed);
    sectionHeader(ui, feed.x + 20.f, feed.y + 18.f, "LIVE ACTIVITY", "grouped by player and check");
    std::map<std::pair<std::string, int>, Group> groups;
    const double horizon = ctx.nowMs - 15 * 60e3;
    for (const auto& r : session.players()) {
        for (const auto& e : r.evidence) {
            if (e.timeMs < horizon) continue;
            Group& g = groups[{r.key, e.check}];
            g.player = &r;
            g.check = e.check;
            ++g.count;
            if (e.timeMs >= g.lastMs) {
                g.lastMs = e.timeMs;
                g.summary = e.summary;
                g.evidenceId = e.id;
                g.confidence = std::max(g.confidence, e.confidenceAfter);
            }
        }
        for (const auto& d : r.detections) {
            if (d.timeMs < horizon) continue;
            Group& g = groups[{r.key, d.check}];
            g.player = &r;
            g.check = d.check;
            g.detection = true;
            g.detectionId = d.id;
            g.confidence = std::max(g.confidence, d.confidence);
            if (d.timeMs >= g.lastMs) g.lastMs = d.timeMs;
        }
    }
    std::vector<Group> items;
    for (auto& kv : groups) items.push_back(kv.second);
    std::sort(items.begin(), items.end(), [](const Group& a, const Group& b) { return a.lastMs > b.lastMs; });

    const Rect feedView{feed.x + 8.f, feed.y + 44.f, feed.w - 16.f, feed.h - 52.f};
    if (items.empty()) {
        emptyState(ui, feedView, Icon::Pulse, "Watching quietly",
                   "Every player in view is analyzed during games. Observations appear here the moment behavior "
                   "departs from what vanilla Minecraft allows.");
    } else {
        const Id sid = hashId("overview-feed");
        const float off = beginScroll(ui, sid, feedView);
        float y = feedView.y + off;
        for (const Group& g : items) {
            const Id iid = mixId(hashId(g.player->key, 31), static_cast<Id>(g.check + 1));
            const float e = entrance(ui, iid);
            const float rowH = 60.f;
            const Rect row{feedView.x, y, feedView.w, rowH};
            if (row.b() >= feedView.y && row.y <= feedView.b()) {
                ui.r.pushAlpha(e);
                ui.r.pushOffset((1.f - e) * -motion::travel(motion::kListTravel), 0.f);
                bool h = false;
                if (ui.pressable(iid, row, &h)) {
                    ctx.openPlayer(g.player->key, ProfileTab::Evidence);
                    ctx.state.selectedDetection = g.detection ? g.detectionId : 0;
                    ctx.state.selectedEvidence = g.detection ? 0 : g.evidenceId;
                }
                const float hv = ui.anim(mixId(iid, 1), h ? 1.f : 0.f, motion::kHover);
                if (g.detection) ui.r.roundRect(row.inset(4.f, 3.f), 10.f, t.sevAlert.withAlpha(0.06f + 0.05f * hv));
                else if (hv > 0.01f) ui.r.roundRect(row.inset(4.f, 3.f), 10.f, t.surfaceHover.alpha(hv));
                const auto& chk = *checks[static_cast<std::size_t>(g.check)];
                const Color sev = t.severity(g.confidence);
                playerAvatar(ctx, *g.player, {row.x + 12.f, row.cy() - 18.f, 36.f, 36.f});
                const float nx = row.x + 62.f;
                const float nw = ui.fonts.draw(ui.r, type::BodyStrong, nx, row.y + 12.f, g.player->name, t.text, Align::Left, row.w * 0.4f);
                // The check, as an icon + name chip; a detection is marked with a bolt.
                const std::string tag = chk.name();
                const float cw = chipWidth(ui, tag) + 16.f;
                const Rect chr{nx + nw + 8.f, row.y + 11.f, cw, 19.f};
                ui.r.roundRect(chr, 9.5f, (g.detection ? sev : t.textMuted).withAlpha(t.light ? 0.12f : 0.14f));
                drawIcon(ui.r, g.detection ? Icon::Bolt : checkIcon(chk.id()), chr.x + 6.f, chr.y + 3.f, 13.f, g.detection ? sev : t.textMuted, 1.6f);
                ui.fonts.draw(ui.r, type::Caption, chr.x + 22.f, chr.cy() - ui.fonts.lineHeight(type::Caption) * 0.5f, tag,
                              g.detection ? (t.light ? sev.darker(0.15f) : sev.lighter(0.1f)) : t.textMuted);
                if (g.count > 1) {
                    const std::string cnt = "\xC3\x97" + std::to_string(g.count);
                    ui.fonts.draw(ui.r, type::NumericCaption, chr.r() + 6.f, row.y + 14.f, cnt, t.textFaint);
                }
                ui.fonts.draw(ui.r, type::Small, nx, row.y + 34.f, g.summary, t.textMuted, Align::Left, row.w - 180.f);
                ui.fonts.draw(ui.r, type::Caption, row.r() - 14.f, row.y + 13.f, agoLabel(ctx.nowMs, g.lastMs), t.textFaint, Align::Right);
                suspicionLabel(ui, row.r() - 14.f, row.y + 33.f, g.confidence, Align::Right);
                ui.r.popOffset();
                ui.r.popAlpha();
            }
            y += rowH;
        }
        endScroll(ui, sid, feedView, y - (feedView.y + off) + 8.f);
    }

    // ── Watchlist: your marks first (from any game), then who stands out in this game ──
    card(ui, watchCard);
    sectionHeader(ui, watchCard.x + 20.f, watchCard.y + 18.f, "WATCHLIST", "marked and flagged");
    auto players = sortedPlayers(ctx.engine, 0, {}, false);
    players.erase(std::remove_if(players.begin(), players.end(),
                                 [&](const detection::PlayerRecord* p) {
                                     if (ctx.engine.marks().watched(p->key)) return false;
                                     return !ctx.engine.inThisGame(*p) || (p->suspicion < 15.f && p->peakSuspicion < 40.f);
                                 }),
                  players.end());
    const Rect wl{watchCard.x + 8.f, watchCard.y + 44.f, watchCard.w - 16.f, watchCard.h - 84.f};
    if (players.empty()) {
        emptyState(ui, wl, Icon::Target, "No one stands out",
                   "Players in this game with rising suspicion appear here, with anyone you watch or have confirmed.");
    } else {
        float y = wl.y;
        const double t1 = ctx.nowMs, t0 = t1 - 5 * 60 * 1000.0;
        for (const detection::PlayerRecord* p : players) {
            if (y + 60.f > wl.b()) break;
            const Rect row{wl.x, y, wl.w, 60.f};
            const Id rid = hashId(p->key, 991);
            bool h = false;
            if (ui.pressable(rid, row, &h)) ctx.openPlayer(p->key);
            const float hv = ui.anim(mixId(rid, 1), h ? 1.f : 0.f, motion::kHover);
            if (hv > 0.01f) ui.r.roundRect(row.inset(4.f, 3.f), 10.f, t.surfaceHover.alpha(hv));
            playerAvatar(ctx, *p, {row.x + 12.f, row.cy() - 18.f, 36.f, 36.f});
            const float nx = row.x + 62.f;
            ui.fonts.draw(ui.r, type::BodyStrong, nx, row.y + 12.f, p->name, p->present ? t.text : t.textMuted, Align::Left, row.w - 200.f);
            const int topc = p->topCheck();
            std::string detail = ctx.engine.marks().confirmed(p->key) ? "Confirmed by you"
                               : topc >= 0 ? checks[static_cast<std::size_t>(topc)]->name() : tierName(tierOf(p->suspicion));
            detail += " \xC2\xB7 " + presenceLabel(ctx, *p);
            ui.fonts.draw(ui.r, type::Small, nx, row.y + 33.f, detail, t.textFaint, Align::Left, row.w - 220.f);
            suspicionLabel(ui, row.r() - 112.f, row.y + 12.f, p->suspicion, Align::Right, false);
            trendLabel(ui, row.r() - 112.f, row.y + 33.f, confidenceTrend(p->suspicionHistory, p->suspicion, ctx.nowMs), true);
            sparkline(ui, {row.r() - 100.f, row.y + 14.f, 84.f, 32.f}, p->suspicionHistory, t0, t1, t.severity(p->suspicion));
            y += 60.f;
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
