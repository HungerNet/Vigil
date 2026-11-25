// Players — the investigation experience. One roster, one profile per player:
//   Summary   confidence over the session, what drives it, milestones, strongest evidence
//   Evidence  timeline, observations and the inspector (page_evidence.cpp)
//   Behavior  how the player fights, clicks and moves, independent of any verdict
//   Identity  account, nick status and the denickers

#include "../identity/identity.h"
#include "pages.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace vigil::ui {
namespace {

using detection::PlayerRecord;

bool isNicked(const std::string& key) {
    identity::Profile ip;
    return identity::Service::instance().find(key, ip) && ip.nicked;
}

// ── Roster ───────────────────────────────────────────────────────────────────────────────────

void roster(PageContext& ctx, const Rect& rc) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    card(ui, rc);
    textField(ui, hashId("roster-search"), {rc.x + 14.f, rc.y + 14.f, rc.w - 28.f, 34.f}, ctx.state.playerSearch, "Find a player");
    static const std::vector<std::string> filters = {"All", "Flagged", "In view", "Nicked"};
    segmented(ui, hashId("roster-filter"), {rc.x + 14.f, rc.y + 56.f, rc.w - 28.f, 28.f}, filters, ctx.state.rosterFilter);
    auto players = sortedPlayers(ctx.engine.session(), ctx.state.playersSort, ctx.state.playerSearch, true);
    players.erase(std::remove_if(players.begin(), players.end(),
                                 [&](const PlayerRecord* p) {
                                     switch (ctx.state.rosterFilter) {
                                         case 1: return p->suspicion < 20.f && p->detections.empty();
                                         case 2: return !p->present;
                                         case 3: return !isNicked(p->key);
                                         default: return false;
                                     }
                                 }),
                  players.end());
    // Sort control.
    static const char* kSorts[] = {"Suspicion", "Recent", "Name"};
    const std::string sortLbl = std::string("Sorted by ") + kSorts[ctx.state.playersSort];
    const Rect sortR{rc.x + 18.f, rc.y + 92.f, ui.fonts.width(type::Caption, sortLbl) + 18.f, 18.f};
    bool sh = false;
    if (ui.pressable(hashId("roster-sort"), sortR, &sh)) ctx.state.playersSort = (ctx.state.playersSort + 1) % 3;
    ui.fonts.draw(ui.r, type::Caption, sortR.x, sortR.y + 2.f, sortLbl, sh ? t.text : t.textFaint);
    drawIcon(ui.r, Icon::ChevronDown, sortR.r() - 14.f, sortR.y + 3.f, 12.f, sh ? t.text : t.textFaint);
    ui.fonts.draw(ui.r, type::Caption, rc.r() - 18.f, rc.y + 94.f, std::to_string(players.size()), t.textFaint, Align::Right);

    const Rect view{rc.x + 6.f, rc.y + 116.f, rc.w - 12.f, rc.h - 122.f};
    if (players.empty()) {
        emptyState(ui, view, Icon::Players, ctx.engine.session().players().empty() ? "No players yet" : "No matches",
                   ctx.engine.session().players().empty() ? "Players appear once Vigil observes them in a game or replay."
                                                          : "Try another filter or search.");
        return;
    }
    const Id sid = hashId("roster-list");
    const float off = beginScroll(ui, sid, view);
    float y = view.y + off + 2.f;
    for (const PlayerRecord* p : players) {
        const Rect row{view.x, y, view.w, 52.f};
        if (row.b() >= view.y && row.y <= view.b()) {
            const Id rid = hashId(p->key, 9001);
            const bool sel = ctx.state.selectedPlayer == p->key;
            bool h = false;
            if (ui.pressable(rid, row, &h) && !sel) {
                ctx.state.selectedPlayer = p->key;
                ctx.state.selectedEvidence = 0;
                ctx.state.selectedDetection = 0;
                ctx.state.unseenPlayers.erase(p->key);
            }
            const float hv = ui.anim(mixId(rid, 1), sel ? 1.f : (h ? 0.5f : 0.f), motion::kHover);
            if (hv > 0.01f) ui.r.roundRect(row.inset(4.f, 2.f), 10.f, (sel ? t.accentSoft() : t.surfaceHover).alpha(sel ? 1.f : hv * 1.6f));
            playerAvatar(ctx, *p, {row.x + 12.f, row.cy() - 15.f, 30.f, 30.f});
            if (p->present) {
                ui.r.circle(row.x + 41.f, row.cy() + 13.f, 4.6f, t.surface);
                ui.r.circle(row.x + 41.f, row.cy() + 13.f, 3.2f, t.sevClear);
            }
            const float nx = row.x + 52.f;
            const float nw = ui.fonts.draw(ui.r, type::BodyStrong, nx, row.y + 8.f, p->name + (p->local ? " (you)" : ""),
                                           p->present ? t.text : t.textMuted, Align::Left, row.w - 120.f);
            if (ctx.state.unseenPlayers.count(p->key)) ui.r.circle(nx + nw + 7.f, row.y + 16.f, 3.2f, t.sevAlert);
            if (isNicked(p->key)) drawIcon(ui.r, Icon::Mask, nx + nw + (ctx.state.unseenPlayers.count(p->key) ? 14.f : 6.f), row.y + 9.f, 14.f, t.textFaint, 1.5f);
            const std::string sub = !p->detections.empty() ? std::to_string(p->detections.size()) + " detections"
                                  : !p->evidence.empty() ? std::to_string(p->evidence.size()) + " observations"
                                                         : (p->present ? "In view" : "Left");
            ui.fonts.draw(ui.r, type::Caption, nx, row.y + 28.f, sub, t.textFaint, Align::Left, row.w - 130.f);
            ui.fonts.draw(ui.r, type::SmallStrong, row.r() - 14.f, row.y + 9.f, formatNumber(p->suspicion, 0) + "%",
                          t.severity(p->suspicion), Align::Right);
            trendLabel(ui, row.r() - 14.f, row.y + 27.f, confidenceTrend(p->suspicionHistory, p->suspicion, ctx.nowMs), true);
        }
        y += 54.f;
    }
    endScroll(ui, sid, view, y - (view.y + off) + 6.f);
}

// ── Profile header ───────────────────────────────────────────────────────────────────────────

void profileHeader(PageContext& ctx, const PlayerRecord& p, const Rect& rc) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    card(ui, rc);
    playerAvatar(ctx, p, {rc.x + 20.f, rc.y + 18.f, 52.f, 52.f});
    const float nx = rc.x + 88.f;
    // Right-hand cluster (ring + actions) has a fixed width; the identity block gets the rest.
    const float clusterW = 300.f + (!p.local && ctx.host ? 0.f : -90.f);
    const float textMax = std::max(120.f, rc.w - (nx - rc.x) - clusterW - 24.f);
    const float nw = ui.fonts.draw(ui.r, type::Heading, nx, rc.y + 14.f, p.name, t.text, Align::Left, textMax);
    float cx = nx + nw + 12.f;
    auto addChip = [&](const std::string& s, Color c) {
        const float cw = chipWidth(ui, s);
        if (cx + cw > nx + textMax) return;
        chip(ui, {cx, rc.y + 20.f, cw, 20.f}, s, c);
        cx += cw + 6.f;
    };
    addChip(p.present ? "In view" : "Left view", p.present ? t.sevClear : t.textMuted);
    if (p.local) addChip("You", t.accent);
    if (p.teammate) addChip("Teammate", t.textMuted);
    identity::Profile ip;
    if (identity::Service::instance().find(p.key, ip) && ip.nicked) {
        std::string nick = "Nicked";
        if (!ip.realName.empty()) nick += " \xC2\xB7 skin of " + ip.realName;
        else if (ip.candidates.size() == 1) nick += " \xC2\xB7 likely " + ip.candidates.front();
        addChip(nick, t.chart[4]);
    }
    const double observed = p.stats.ticksObserved * 0.05;
    std::string sub = "Observed " + formatDuration(observed) + " \xC2\xB7 " + std::to_string(p.evidence.size()) + " evidence \xC2\xB7 " +
                      std::to_string(p.detections.size()) + " detections \xC2\xB7 peak " + formatNumber(p.peakSuspicion, 0) + "%";
    if (p.priorSessions > 0) {
        sub += " \xC2\xB7 seen in " + std::to_string(p.priorSessions) + " earlier session" + (p.priorSessions == 1 ? "" : "s");
    }
    ui.fonts.draw(ui.r, type::Small, nx, rc.y + 50.f, sub, t.textMuted, Align::Left, textMax);

    // Confidence ring + trend.
    const float ringX = rc.r() - clusterW + 10.f;
    confidenceRing(ui, hashId("profile-ring", hashId(p.key)), ringX, rc.cy(), 25.f, p.suspicion, 5.f, true);
    ui.fonts.draw(ui.r, type::Caption, ringX + 34.f, rc.cy() - 16.f, "SUSPICION", t.textFaint);
    const float trend = confidenceTrend(p.suspicionHistory, p.suspicion, ctx.nowMs);
    if (std::fabs(trend) >= 1.f) trendLabel(ui, ringX + 34.f, rc.cy() + 2.f, trend);
    else ui.fonts.draw(ui.r, type::Caption, ringX + 34.f, rc.cy() + 2.f, "steady", t.textFaint);

    // Actions.
    float ax = rc.r() - 18.f;
    if (!p.local && ctx.host) {
        float& armedAt = ui.value(hashId("report-armed", hashId(p.key)), -100.f);
        const bool armed = ui.time - armedAt < 3.0;
        const std::string label = armed ? "Confirm /wdr" : "Report";
        const Icon ic = Icon::Report;
        const float bw = buttonWidth(ui, label, true);
        ax -= bw;
        const Rect br{ax, rc.cy() - 17.f, bw, 34.f};
        if (button(ui, hashId("report", hashId(p.key)), br, label, armed ? ButtonKind::Danger : ButtonKind::Secondary, &ic)) {
            if (armed) { ctx.host->reportPlayer(p.name); armedAt = -100.f; }
            else armedAt = static_cast<float>(ui.time);
        }
        ui.tooltip(hashId("report-tip", hashId(p.key)), br, "Sends /wdr " + p.name + " to Hypixel. Click twice to confirm.");
        ax -= 8.f;
    }
    const bool watched = ctx.state.watched.count(p.key) > 0;
    ax -= 34.f;
    if (iconButton(ui, hashId("watch", hashId(p.key)), {ax, rc.cy() - 17.f, 34.f, 34.f}, Icon::Pin,
                   watched ? "Unpin from the watchlist" : "Pin to the watchlist", watched)) {
        if (watched) ctx.state.watched.erase(p.key); else ctx.state.watched.insert(p.key);
    }
    ax -= 38.f;
    if (iconButton(ui, hashId("copy", hashId(p.key)), {ax, rc.cy() - 17.f, 34.f, 34.f}, Icon::Copy, "Copy name")) copyToClipboard(p.name);
}

// ── Summary ──────────────────────────────────────────────────────────────────────────────────

struct Milestone {
    double timeMs;
    Icon icon;
    Color color;
    std::string text;
};

std::vector<Milestone> milestones(PageContext& ctx, const PlayerRecord& p) {
    const Theme& t = ctx.ui.th();
    const auto& checks = ctx.engine.checks();
    std::vector<Milestone> out;
    out.push_back({p.firstSeenMs, Icon::Eye, t.textMuted, "First observed"});
    // The evidence buffer is capped, so the first evidence time comes from the confidence history.
    double firstEvidence = -1.0;
    for (const auto& pt : p.suspicionHistory) if (pt.value > 0.5f) { firstEvidence = pt.timeMs; break; }
    if (firstEvidence >= 0.0) {
        std::string text = "First evidence";
        if (!p.evidence.empty() && std::fabs(p.evidence.front().timeMs - firstEvidence) < 2500.0) {
            const auto& e = p.evidence.front();
            text += " \xC2\xB7 " + checks[static_cast<std::size_t>(e.check)]->name() + ": " + e.summary;
        }
        out.push_back({firstEvidence, Icon::Spark, t.sevNotice, text});
    }
    double crossed = -1.0, peakAt = -1.0;
    float peak = 0.f;
    for (const auto& pt : p.suspicionHistory) {
        if (crossed < 0.0 && pt.value >= 50.f) crossed = pt.timeMs;
        if (pt.value > peak) { peak = pt.value; peakAt = pt.timeMs; }
    }
    if (crossed >= 0.0) out.push_back({crossed, Icon::TrendUp, t.sevAlert, "Suspicion crossed 50%"});
    const std::size_t firstDet = p.detections.size() > 3 ? p.detections.size() - 3 : 0;
    for (std::size_t i = firstDet; i < p.detections.size(); ++i) {
        const auto& d = p.detections[i];
        out.push_back({d.timeMs, Icon::Bolt, t.sevCritical,
                       "Detection \xC2\xB7 " + checks[static_cast<std::size_t>(d.check)]->name() + " at " + formatNumber(d.confidence, 0) + "%"});
    }
    if (peakAt >= 0.0 && peak >= 20.f) out.push_back({peakAt, Icon::TrendUp, t.severity(peak), "Peak suspicion " + formatNumber(peak, 0) + "%"});
    if (p.peakSuspicion >= 50.f && p.suspicion < p.peakSuspicion * 0.4f) {
        out.push_back({ctx.nowMs, Icon::TrendDown, t.sevClear, "Settled to " + formatNumber(p.suspicion, 0) + "% \xE2\x80\x94 behavior has normalized"});
    }
    std::sort(out.begin(), out.end(), [](const Milestone& a, const Milestone& b) { return a.timeMs < b.timeMs; });
    if (out.size() > 8) out.erase(out.begin() + 1, out.begin() + static_cast<std::ptrdiff_t>(out.size() - 7));
    return out;
}

void sessionChart(PageContext& ctx, const PlayerRecord& p, const Rect& rc) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    card(ui, rc);
    sectionHeader(ui, rc.x + 20.f, rc.y + 18.f, "CONFIDENCE OVER THE SESSION");
    const Rect plot{rc.x + 20.f, rc.y + 46.f, rc.w - 40.f, rc.h - 76.f};
    const double t1 = ctx.nowMs;
    double t0 = std::min(ctx.engine.session().startMs(), p.firstSeenMs);
    if (t1 - t0 < 60e3) t0 = t1 - 60e3;
    for (float g : {50.f, 70.f}) {
        const float gy = plot.b() - plot.h * g / 100.f;
        ui.r.rect({plot.x, gy, plot.w, 1.f}, (g >= 70.f ? t.sevAlert : t.sevNotice).withAlpha(0.18f));
        ui.fonts.draw(ui.r, type::Caption, plot.r(), gy - 15.f, formatNumber(g, 0) + "%", t.textFaint, Align::Right);
    }
    // Per-check lines (thin) under the overall curve.
    int ci = 0;
    for (std::size_t i = 0; i < p.checks.size(); ++i) {
        if (p.checks[i].peak < 10.f) continue;
        sparkline(ui, plot, p.checks[i].history, t0, t1, t.chart[(ci++ + 1) % 6].alpha(0.5f), false);
    }
    sparkline(ui, plot, p.suspicionHistory, t0, t1, t.severity(std::max(p.suspicion, p.peakSuspicion * 0.7f)), true);
    for (const auto& d : p.detections) {
        if (d.timeMs < t0) continue;
        const float x = plot.x + static_cast<float>((d.timeMs - t0) / (t1 - t0)) * plot.w;
        ui.r.rect({x - 0.5f, plot.y, 1.f, plot.h}, t.sevAlert.withAlpha(0.3f));
        ui.r.diamond(x, plot.y + 4.f, 4.5f, t.sevAlert);
    }
    ui.fonts.draw(ui.r, type::Caption, plot.x, plot.b() + 8.f, "-" + formatClock((t1 - t0) / 1000.0), t.textFaint);
    ui.fonts.draw(ui.r, type::Caption, plot.r(), plot.b() + 8.f, "now", t.textFaint, Align::Right);
}

void drivers(PageContext& ctx, const PlayerRecord& p, const Rect& rc) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    const auto& checks = ctx.engine.checks();
    card(ui, rc);
    sectionHeader(ui, rc.x + 20.f, rc.y + 18.f, "WHAT DRIVES SUSPICION");
    std::vector<int> rows;
    for (std::size_t i = 0; i < p.checks.size(); ++i) if (p.checks[i].evidence > 0) rows.push_back(static_cast<int>(i));
    std::sort(rows.begin(), rows.end(), [&](int a, int b) {
        return p.checks[static_cast<std::size_t>(a)].model.value() + p.checks[static_cast<std::size_t>(a)].peak * 0.1f >
               p.checks[static_cast<std::size_t>(b)].model.value() + p.checks[static_cast<std::size_t>(b)].peak * 0.1f;
    });
    float y = rc.y + 46.f;
    if (rows.empty()) {
        ui.fonts.drawWrapped(ui.r, type::Small, rc.x + 20.f, y, rc.w - 40.f,
                             "No check has evidence for this player. Suspicion is the highest confidence across checks, so "
                             "it stays at 0% until behavior departs from vanilla.", t.textFaint, 3.f);
        return;
    }
    for (int ci : rows) {
        if (y + 40.f > rc.b() - 8.f) break;
        const auto& cr = p.checks[static_cast<std::size_t>(ci)];
        const auto& chk = *checks[static_cast<std::size_t>(ci)];
        const float v = cr.model.value();
        drawIcon(ui.r, checkIcon(chk.id()), rc.x + 20.f, y + 2.f, 16.f, t.textMuted);
        ui.fonts.draw(ui.r, type::BodyStrong, rc.x + 44.f, y, chk.name(), t.text);
        ui.fonts.draw(ui.r, type::SmallStrong, rc.r() - 20.f, y, formatNumber(v, 0) + "%", t.severity(v), Align::Right);
        trendLabel(ui, rc.r() - 60.f, y + 1.f, confidenceTrend(cr.history, v, ctx.nowMs), true);
        meter(ui, {rc.x + 44.f, y + 21.f, rc.w - 64.f, 5.f}, v / 100.f, t.severity(v));
        const float px = rc.x + 44.f + (rc.w - 64.f) * cr.peak / 100.f;
        ui.r.rect({px - 0.75f, y + 18.f, 1.5f, 11.f}, t.text.alpha(0.45f));
        ui.tooltip(hashId("drv", static_cast<Id>(ci)), {rc.x + 20.f, y - 2.f, rc.w - 40.f, 34.f},
                   std::to_string(cr.evidence) + " observations \xC2\xB7 peak " + formatNumber(cr.peak, 0) + "% (marker) \xC2\xB7 " +
                       std::to_string(cr.detections) + " detections");
        y += 40.f;
    }
}

void milestoneList(PageContext& ctx, const PlayerRecord& p, const Rect& rc) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    card(ui, rc);
    sectionHeader(ui, rc.x + 20.f, rc.y + 18.f, "HOW CONFIDENCE EVOLVED");
    auto ms = milestones(ctx, p);
    // When space is short keep the beginning of the story and its most recent events.
    const int capacity = std::max(2, static_cast<int>((rc.h - 52.f) / 32.f));
    if (static_cast<int>(ms.size()) > capacity) ms.erase(ms.begin() + 1, ms.begin() + static_cast<std::ptrdiff_t>(ms.size() - capacity + 1));
    float y = rc.y + 46.f;
    for (std::size_t i = 0; i < ms.size(); ++i) {
        if (y + 30.f > rc.b() - 6.f) break;
        const Milestone& m = ms[i];
        if (i + 1 < ms.size() && y + 60.f <= rc.b()) ui.r.rect({rc.x + 29.5f, y + 20.f, 1.f, 16.f}, t.divider);
        ui.r.circle(rc.x + 30.f, y + 9.f, 10.f, m.color.withAlpha(0.14f));
        drawIcon(ui.r, m.icon, rc.x + 23.f, y + 2.f, 14.f, m.color, 1.6f);
        ui.fonts.draw(ui.r, type::Small, rc.x + 50.f, y + 1.f, m.text, t.text, Align::Left, rc.w - 140.f);
        ui.fonts.draw(ui.r, type::Caption, rc.r() - 20.f, y + 2.f, agoLabel(ctx.nowMs, m.timeMs), t.textFaint, Align::Right);
        y += 32.f;
    }
}

void topEvidence(PageContext& ctx, const PlayerRecord& p, const Rect& rc) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    const auto& checks = ctx.engine.checks();
    card(ui, rc);
    sectionHeader(ui, rc.x + 20.f, rc.y + 18.f, "STRONGEST EVIDENCE");
    // Strongest observation per indicator, so the list shows different kinds of evidence.
    std::map<std::pair<int, int>, const detection::Evidence*> best;
    for (const auto& e : p.evidence) {
        auto& slot = best[{e.check, e.indicator}];
        if (!slot || e.weight * e.severity > slot->weight * slot->severity ||
            (e.weight * e.severity == slot->weight * slot->severity && e.timeMs > slot->timeMs)) {
            slot = &e;
        }
    }
    std::vector<const detection::Evidence*> ev;
    for (const auto& kv : best) ev.push_back(kv.second);
    std::sort(ev.begin(), ev.end(), [](const detection::Evidence* a, const detection::Evidence* b) {
        return a->weight * a->severity > b->weight * b->severity;
    });
    float y = rc.y + 42.f;
    if (ev.empty()) {
        ui.fonts.draw(ui.r, type::Small, rc.x + 20.f, y + 4.f, "Nothing recorded yet.", t.textFaint);
        return;
    }
    for (const detection::Evidence* e : ev) {
        if (y + 44.f > rc.b() - 6.f) break;
        const Rect row{rc.x + 8.f, y, rc.w - 16.f, 44.f};
        const Id rid = mixId(hashId("top-ev"), e->id);
        bool h = false;
        if (ui.pressable(rid, row, &h)) {
            ctx.state.profileTab = ProfileTab::Evidence;
            ctx.state.selectedEvidence = e->id;
            ctx.state.selectedDetection = 0;
        }
        if (h) ui.r.roundRect(row, 9.f, t.surfaceHover);
        const Color c = t.severity(e->confidenceAfter);
        ui.r.circle(row.x + 14.f, row.y + 14.f, 4.f, c);
        const auto& chk = *checks[static_cast<std::size_t>(e->check)];
        ui.fonts.draw(ui.r, type::SmallStrong, row.x + 28.f, row.y + 6.f, chk.indicators()[static_cast<std::size_t>(e->indicator)].name,
                      t.text, Align::Left, row.w - 130.f);
        ui.fonts.draw(ui.r, type::Small, row.x + 28.f, row.y + 23.f, e->summary, t.textMuted, Align::Left, row.w - 90.f);
        ui.fonts.draw(ui.r, type::Caption, row.r() - 10.f, row.y + 7.f, "strength " + formatNumber(e->weight * e->severity, 2), t.textFaint,
                      Align::Right);
        y += 46.f;
    }
}

void summaryTab(PageContext& ctx, const PlayerRecord& p, const Rect& area) {
    const float gap = 12.f;
    const float leftW = std::floor((area.w - gap) * 0.58f);
    const float chartH = std::min(250.f, area.h * 0.5f);
    sessionChart(ctx, p, {area.x, area.y, leftW, chartH});
    milestoneList(ctx, p, {area.x, area.y + chartH + gap, leftW, area.h - chartH - gap});
    const float rx = area.x + leftW + gap, rw = area.w - leftW - gap;
    const float drvH = std::min(area.h * 0.52f, 70.f + 40.f * std::max<std::size_t>(1, std::count_if(p.checks.begin(), p.checks.end(),
                                                                                                         [](const auto& c) { return c.evidence > 0; })));
    drivers(ctx, p, {rx, area.y, rw, drvH});
    topEvidence(ctx, p, {rx, area.y + drvH + gap, rw, area.h - drvH - gap});
}

// ── Behavior ─────────────────────────────────────────────────────────────────────────────────

void statLine(Ui& ui, float x, float y, float w, const std::string& label, const std::string& value, Color vc) {
    const Theme& t = ui.th();
    ui.fonts.draw(ui.r, type::Small, x, y, label, t.textMuted);
    ui.fonts.draw(ui.r, type::BodyStrong, x + w, y - 1.f, value, vc, Align::Right);
}

void reachHistogram(Ui& ui, const PlayerRecord& p, const Rect& rc) {
    const Theme& t = ui.th();
    constexpr int kBins = 20;  // 2.0 .. 4.0 blocks
    int bins[kBins] = {};
    int maxBin = 1;
    for (float r : p.stats.reachRecent) {
        const int b = std::clamp(static_cast<int>((r - 2.0f) / 0.1f), 0, kBins - 1);
        maxBin = std::max(maxBin, ++bins[b]);
    }
    const float bw = rc.w / kBins;
    for (int i = 0; i < kBins; ++i) {
        const float h = rc.h * bins[i] / static_cast<float>(maxBin);
        const float lo = 2.0f + i * 0.1f;
        const Color c = lo >= 3.0f ? t.sevAlert : t.chart[0];
        if (bins[i] > 0) ui.r.roundRect({rc.x + i * bw + 1.f, rc.b() - h, bw - 2.f, h}, 2.f, c.alpha(0.85f));
    }
    const float vx = rc.x + rc.w * 0.5f;  // 3.0 blocks
    ui.r.rect({vx - 0.5f, rc.y - 4.f, 1.f, rc.h + 4.f}, t.text.alpha(0.5f));
    ui.fonts.draw(ui.r, type::Caption, vx + 4.f, rc.y - 6.f, "vanilla 3.0", t.textFaint);
    ui.fonts.draw(ui.r, type::Caption, rc.x, rc.b() + 4.f, "2.0", t.textFaint);
    ui.fonts.draw(ui.r, type::Caption, rc.r(), rc.b() + 4.f, "4.0 blocks", t.textFaint, Align::Right);
}

std::vector<std::string> patterns(const PageContext& ctx, const PlayerRecord& p) {
    const auto& checks = ctx.engine.checks();
    std::vector<std::string> out;
    if (p.evidence.empty()) {
        out.push_back("No unusual behavior has been observed for this player.");
        return out;
    }
    std::map<detection::CheckCategory, int> byCat;
    std::map<int, int> byCheck;
    int inCombat = 0;
    for (const auto& e : p.evidence) {
        byCat[checks[static_cast<std::size_t>(e.check)]->category()]++;
        byCheck[e.check]++;
        if (!e.target.empty()) ++inCombat;
    }
    auto topCat = std::max_element(byCat.begin(), byCat.end(), [](auto& a, auto& b) { return a.second < b.second; });
    out.push_back("Most evidence comes from " + std::string(detection::categoryName(topCat->first)) + " checks (" +
                  std::to_string(topCat->second) + " of " + std::to_string(p.evidence.size()) + ").");
    if (byCheck.size() >= 2) {
        out.push_back(std::to_string(byCheck.size()) + " different checks agree. Independent checks pointing the same way are "
                      "stronger evidence than one check repeating.");
    }
    if (inCombat > 0) out.push_back(std::to_string(inCombat) + " observations involved another player, so they occurred during fights.");
    if (p.stats.reachSamples >= 5) {
        const double avg = p.stats.reachSum / p.stats.reachSamples;
        out.push_back("Average attributed hit distance is " + formatNumber(avg, 2) + " blocks over " + std::to_string(p.stats.reachSamples) +
                      " hits" + (avg > 3.0 ? ", beyond vanilla reach." : "."));
    }
    if (p.priorSessions > 0 && p.priorDetections > 0) {
        out.push_back("Vigil detected this player " + std::to_string(p.priorDetections) + " time(s) in earlier sessions.");
    }
    return out;
}

void behaviorTab(PageContext& ctx, const PlayerRecord& p, const Rect& area) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    const auto& checks = ctx.engine.checks();
    const Id sid = hashId("behavior-scroll");
    const float off = beginScroll(ui, sid, area);
    float y = area.y + off;
    const float w = area.w - 8.f, gap = 12.f;
    const float colW = (w - gap) * 0.5f;
    {
        const Rect c{area.x, y, colW, 214.f};
        card(ui, c);
        sectionHeader(ui, c.x + 20.f, c.y + 18.f, "COMBAT");
        const auto& s = p.stats;
        const float sx = c.x + 20.f, sw = c.w - 40.f;
        statLine(ui, sx, c.y + 46.f, sw, "Hits landed / taken", std::to_string(s.hitsLanded) + " / " + std::to_string(s.hitsTaken), t.text);
        statLine(ui, sx, c.y + 70.f, sw, "Average reach", s.reachSamples ? formatNumber(s.reachSum / s.reachSamples, 2) + " blocks" : "\xE2\x80\x94", t.text);
        statLine(ui, sx, c.y + 94.f, sw, "Longest hit", s.reachSamples ? formatNumber(s.reachMax, 2) + " blocks" : "\xE2\x80\x94",
                 s.reachMax > 3.0 ? t.sevAlert : t.text);
        if (!s.reachRecent.empty()) reachHistogram(ui, p, {sx, c.y + 136.f, sw, 48.f});
        else ui.fonts.draw(ui.r, type::Small, sx, c.y + 140.f, "Reach distribution appears after attributed hits.", t.textFaint);
    }
    {
        const Rect c{area.x + colW + gap, y, colW, 214.f};
        card(ui, c);
        sectionHeader(ui, c.x + 20.f, c.y + 18.f, "CLICKING & MOVEMENT");
        const auto& s = p.stats;
        const float sx = c.x + 20.f, sw = c.w - 40.f;
        statLine(ui, sx, c.y + 46.f, sw, "Peak swings per second", formatNumber(s.cpsPeak, 0), t.text);
        const double avgSpeed = s.speedSamples ? s.speedSum / s.speedSamples : 0.0;
        statLine(ui, sx, c.y + 70.f, sw, "Average speed", formatNumber(avgSpeed * 20.0, 1) + " m/s", t.text);
        statLine(ui, sx, c.y + 94.f, sw, "Peak speed (0.5 s)", formatNumber(s.speedPeak * 20.0, 1) + " m/s", s.speedPeak > 0.42 ? t.sevNotice : t.text);
        statLine(ui, sx, c.y + 118.f, sw, "Jumps \xC2\xB7 average air time",
                 std::to_string(s.jumps) + " \xC2\xB7 " + (s.jumps ? formatNumber(s.airTicksSum / s.jumps, 1) + " ticks" : "\xE2\x80\x94"), t.text);
        if (s.speedRecent.size() > 2) {
            const Rect sp{sx, c.y + 150.f, sw, 40.f};
            float mx = 0.45f;
            for (float v : s.speedRecent) mx = std::max(mx, v);
            std::vector<Point> pts;
            for (std::size_t i = 0; i < s.speedRecent.size(); ++i) {
                pts.push_back({sp.x + sp.w * i / (s.speedRecent.size() - 1), sp.b() - sp.h * s.speedRecent[i] / mx});
            }
            const float vy = sp.b() - sp.h * static_cast<float>(detection::mc::kSprintJumpAverage) / mx;
            ui.r.rect({sp.x, vy, sp.w, 1.f}, t.sevNotice.withAlpha(0.35f));
            ui.r.areaUnder(pts, sp.b(), t.chart[1].withAlpha(0.2f), t.chart[1].withAlpha(0.f));
            ui.r.polyline(pts, 1.5f, t.chart[1]);
            ui.fonts.draw(ui.r, type::Caption, sp.r(), vy - 15.f, "sprint-jump", t.textFaint, Align::Right);
        }
    }
    y += 214.f + gap;
    {
        std::vector<int> rows;
        for (std::size_t i = 0; i < p.checks.size(); ++i) if (p.checks[i].evidence > 0 || p.checks[i].peak > 0.f) rows.push_back(static_cast<int>(i));
        const float h = 52.f + std::max<std::size_t>(1, rows.size()) * 44.f;
        const Rect c{area.x, y, w, h};
        card(ui, c);
        sectionHeader(ui, c.x + 20.f, c.y + 18.f, "CONFIDENCE BY CHECK", "last 15 minutes");
        float ry = c.y + 46.f;
        const double t1 = ctx.nowMs, t0 = t1 - 15 * 60e3;
        if (rows.empty()) ui.fonts.draw(ui.r, type::Small, c.x + 20.f, ry + 6.f, "No check has evidence for this player.", t.textFaint);
        for (int ci : rows) {
            const auto& cr = p.checks[static_cast<std::size_t>(ci)];
            const auto& chk = *checks[static_cast<std::size_t>(ci)];
            drawIcon(ui.r, checkIcon(chk.id()), c.x + 20.f, ry + 6.f, 18.f, t.textMuted);
            ui.fonts.draw(ui.r, type::BodyStrong, c.x + 48.f, ry + 3.f, chk.name(), t.text);
            ui.fonts.draw(ui.r, type::Caption, c.x + 48.f, ry + 21.f, std::to_string(cr.evidence) + " evidence \xC2\xB7 peak " + formatNumber(cr.peak, 0) + "%", t.textFaint);
            const float bx = c.x + 230.f;
            const float bw = c.w - 230.f - 240.f;
            meter(ui, {bx, ry + 13.f, bw, 6.f}, cr.model.value() / 100.f, t.severity(cr.model.value()));
            ui.fonts.draw(ui.r, type::SmallStrong, bx + bw + 12.f, ry + 8.f, formatNumber(cr.model.value(), 0) + "%", t.severity(cr.model.value()));
            sparkline(ui, {c.r() - 180.f, ry + 2.f, 160.f, 30.f}, cr.history, t0, t1, t.severity(cr.peak));
            ry += 44.f;
        }
        y += h + gap;
    }
    {
        const auto lines = patterns(ctx, p);
        float h = 56.f;
        for (const auto& l : lines) h += ui.fonts.wrap(type::Body, l, w - 70.f).size() * 20.f + 10.f;
        const Rect c{area.x, y, w, h};
        card(ui, c);
        sectionHeader(ui, c.x + 20.f, c.y + 18.f, "PATTERNS");
        float ly = c.y + 46.f;
        for (const auto& l : lines) {
            drawIcon(ui.r, Icon::Spark, c.x + 20.f, ly + 1.f, 14.f, t.accent, 1.4f);
            ly += ui.fonts.drawWrapped(ui.r, type::Body, c.x + 44.f, ly, w - 70.f, l, t.textMuted, 4.f) + 10.f;
        }
        y += h + gap;
    }
    endScroll(ui, sid, area, y - (area.y + off));
}

// ── Identity ─────────────────────────────────────────────────────────────────────────────────

float infoRow(Ui& ui, float x, float y, float w, const std::string& label, const std::string& value, Color vc, Id copyId = 0) {
    const Theme& t = ui.th();
    ui.fonts.draw(ui.r, type::Small, x, y, label, t.textMuted);
    float vx = x + w;
    if (copyId) {
        if (iconButton(ui, copyId, {x + w - 24.f, y - 5.f, 24.f, 24.f}, Icon::Copy, "Copy")) copyToClipboard(value);
        vx -= 30.f;
    }
    ui.fonts.draw(ui.r, type::SmallStrong, vx, y, value, vc, Align::Right, w * 0.62f);
    return y + 26.f;
}

void identityTab(PageContext& ctx, const PlayerRecord& p, const Rect& area) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    identity::Profile ip;
    const bool known = identity::Service::instance().find(p.key, ip);
    const auto& is = identity::settings();
    const float gap = 12.f;
    const float colW = (area.w - gap) * 0.5f;

    // Account.
    {
        const Rect c{area.x, area.y, colW, 214.f};
        card(ui, c);
        sectionHeader(ui, c.x + 20.f, c.y + 18.f, "ACCOUNT");
        float y = c.y + 48.f;
        const float x = c.x + 20.f, w = c.w - 40.f;
        y = infoRow(ui, x, y, w, "Name in game", p.name, t.text, hashId("copy-name", hashId(p.key)));
        y = infoRow(ui, x, y, w, "UUID", p.uuid.empty() ? "unknown" : p.uuid, t.textMuted, p.uuid.empty() ? 0 : hashId("copy-uuid", hashId(p.key)));
        const int v = known ? ip.uuidVersion : identity::uuidVersion(p.uuid);
        const std::string kind = v == 4 ? "Real account" : v == 1 ? "Hypixel nick" : v == 2 ? "Server NPC" : "Unknown";
        y = infoRow(ui, x, y, w, "Account type", kind + (v ? " (UUID v" + std::to_string(v) + ")" : ""), v == 1 ? t.chart[4] : t.text);
        y = infoRow(ui, x, y, w, "Earlier sessions",
                    p.priorSessions ? std::to_string(p.priorSessions) + " \xC2\xB7 " + std::to_string(p.priorDetections) + " detections" : "none",
                    t.text);
        ui.fonts.drawWrapped(ui.r, type::Caption, x, y + 2.f, w,
                             v == 1 ? "Hypixel gives nicked players a temporary identity. The denickers on the right try to "
                                      "identify the account behind it."
                                    : "This is the player's real account; there is nothing to denick.",
                             t.textFaint, 3.f);
    }
    // Skin denicker.
    {
        const Rect c{area.x + colW + gap, area.y, colW, 214.f};
        card(ui, c);
        sectionHeader(ui, c.x + 20.f, c.y + 18.f, "SKIN DENICKER");
        const float x = c.x + 20.f, w = c.w - 40.f;
        float y = c.y + 48.f;
        if (!ip.nicked) {
            ui.fonts.drawWrapped(ui.r, type::Small, x, y, w, "Only nicked players are looked up.", t.textFaint, 3.f);
        } else if (!is.skinDenick || !is.skinDenick->b()) {
            y += ui.fonts.drawWrapped(ui.r, type::Small, x, y, w, "The skin denicker is turned off.", t.textMuted, 3.f) + 10.f;
            if (button(ui, hashId("idn-enable"), {x, y, buttonWidth(ui, "Open settings"), 32.f}, "Open settings")) {
                revealSetting(ctx, "identity.skinDenick");
            }
        } else {
            const char* skinKind = ip.skin == identity::SkinKind::NickPool ? "Hypixel nick skin"
                                 : ip.skin == identity::SkinKind::Foreign  ? "Another account's skin"
                                 : ip.skin == identity::SkinKind::Own      ? "Own skin"
                                                                           : "Unknown";
            y = infoRow(ui, x, y, w, "Skin", skinKind, t.text);
            if (!ip.textures.profileName.empty() && ip.skin == identity::SkinKind::Foreign) {
                y = infoRow(ui, x, y, w, "Signed for", ip.textures.profileName, t.textMuted);
            }
            const Color stateC = ip.skinLookup == identity::Lookup::Done ? t.sevClear
                               : ip.skinLookup == identity::Lookup::Failed ? t.sevAlert : t.textMuted;
            if (!ip.realName.empty()) y = infoRow(ui, x, y, w, "Skin owner today", ip.realName, t.accent, hashId("copy-real", hashId(p.key)));
            else y = infoRow(ui, x, y, w, "Lookup", identity::lookupName(ip.skinLookup), stateC);
            y += ui.fonts.drawWrapped(ui.r, type::Caption, x, y, w, ip.skinNote, t.textFaint, 3.f, 4) + 8.f;
            const bool canLookup = ip.skin == identity::SkinKind::Foreign &&
                                   (ip.skinLookup == identity::Lookup::Idle || ip.skinLookup == identity::Lookup::Failed);
            if (canLookup) {
                const Icon ic = Icon::Globe;
                const char* lbl = ip.skinLookup == identity::Lookup::Failed ? "Try again" : "Look up skin owner";
                if (button(ui, hashId("idn-lookup", hashId(p.key)), {x, c.b() - 48.f, buttonWidth(ui, lbl, true), 32.f}, lbl,
                           ButtonKind::Secondary, &ic)) {
                    identity::Service::instance().requestSkinLookup(p.key);
                }
            }
        }
    }
    // Number denicker.
    const float y2 = area.y + 214.f + gap;
    {
        const Rect c{area.x, y2, colW, area.b() - y2};
        card(ui, c);
        sectionHeader(ui, c.x + 20.f, c.y + 18.f, "NUMBER DENICKER");
        const float x = c.x + 20.f, w = c.w - 40.f;
        float y = c.y + 48.f;
        const bool on = is.numberDenick && is.numberDenick->b();
        const bool hasKey = is.bordicKey && !is.bordicKey->text.empty();
        if (!ip.nicked) {
            ui.fonts.drawWrapped(ui.r, type::Small, x, y, w, "Only nicked players are looked up.", t.textFaint, 3.f);
        } else if (!on || !hasKey) {
            y += ui.fonts.drawWrapped(ui.r, type::Small, x, y, w,
                                      "Bed Wars announces lifetime counters with every final kill and bed. With your own Bordic API "
                                      "key, Vigil matches those counters to accounts.",
                                      t.textMuted, 3.f) + 12.f;
            if (button(ui, hashId("nd-setup"), {x, y, buttonWidth(ui, "Set up number denicker"), 32.f}, "Set up number denicker")) {
                revealSetting(ctx, on ? "identity.bordicKey" : "identity.numberDenick");
            }
        } else {
            y = infoRow(ui, x, y, w, "Final-kill counter", ip.finalsNumber.empty() ? "not seen yet" : "#" + ip.finalsNumber, t.text);
            y = infoRow(ui, x, y, w, "Bed counter", ip.bedsNumber.empty() ? "not seen yet" : "#" + ip.bedsNumber, t.text);
            y += ui.fonts.drawWrapped(ui.r, type::Caption, x, y, w, ip.numberNote.empty() ? "Waiting for a final kill or bed by this player." : ip.numberNote,
                                      t.textFaint, 3.f) + 10.f;
            for (const auto& cand : ip.candidates) {
                if (y + 26.f > c.b() - 8.f) break;
                const Rect row{x - 6.f, y, w + 12.f, 26.f};
                ui.r.roundRect(row, 7.f, ip.candidates.size() == 1 ? t.accentSoft() : t.inset);
                ui.fonts.draw(ui.r, type::SmallStrong, row.x + 10.f, row.y + 5.f, cand, ip.candidates.size() == 1 ? t.accent : t.text);
                if (iconButton(ui, hashId("copy-cand", hashId(cand)), {row.r() - 26.f, row.y + 1.f, 24.f, 24.f}, Icon::Copy, "Copy")) copyToClipboard(cand);
                y += 30.f;
            }
        }
    }
    // Privacy.
    {
        const Rect c{area.x + colW + gap, y2, colW, area.b() - y2};
        card(ui, c);
        sectionHeader(ui, c.x + 20.f, c.y + 18.f, "PRIVACY");
        const float x = c.x + 20.f, w = c.w - 40.f;
        float y = c.y + 48.f;
        const char* lines[] = {
            "Only nicked players are looked up, and only by the denickers you enable.",
            "Skin owners come from Mojang's public profile service.",
            "Counters go to Bordic, one at a time, with your key.",
            "Nothing about you is sent. No analytics, no telemetry, no automatic reports.",
        };
        for (const char* l : lines) {
            if (y > c.b() - 44.f) break;
            drawIcon(ui.r, Icon::Check, x, y + 1.f, 13.f, t.sevClear, 1.8f);
            y += ui.fonts.drawWrapped(ui.r, type::Small, x + 20.f, y, w - 20.f, l, t.textMuted, 3.f, 2) + 7.f;
        }
        const auto st = identity::Service::instance().stats();
        ui.fonts.draw(ui.r, type::Caption, x, c.b() - 26.f,
                      std::to_string(st.requests) + (st.requests == 1 ? " lookup" : " lookups") + " this session" +
                          (st.failures ? " \xC2\xB7 " + std::to_string(st.failures) + " failed" : ""),
                      t.textFaint);
    }
}

}  // namespace

void drawPlayers(PageContext& ctx, const Rect& area) {
    Ui& ui = ctx.ui;
    const float top = pageHeader(ctx, area, "Players", pagePurpose(Page::Players));
    const Rect list{area.x, top, 286.f, area.b() - top};
    roster(ctx, list);
    const Rect main{list.r() + 14.f, top, area.r() - list.r() - 14.f, area.b() - top};

    const PlayerRecord* p = ctx.state.selectedPlayer.empty() ? nullptr : ctx.engine.session().find(ctx.state.selectedPlayer);
    if (!p) {
        auto players = sortedPlayers(ctx.engine.session(), 0, {}, false);
        if (!players.empty()) {
            p = players.front();
            ctx.state.selectedPlayer = p->key;
        }
    }
    if (!p) {
        card(ui, main);
        emptyState(ui, main, Icon::Players, "Nobody to investigate yet",
                   "When Vigil observes players in a game or replay, they appear on the left. Select one to see their "
                   "confidence, evidence, behavior and identity.");
        return;
    }
    const Rect head{main.x, main.y, main.w, 88.f};
    profileHeader(ctx, *p, head);

    static const std::vector<std::string> tabLabels = {"Summary", "Evidence", "Behavior", "Identity"};
    int tab = static_cast<int>(ctx.state.profileTab);
    tabs(ui, hashId("profile-tabs"), main.x + 6.f, head.b() + 16.f, tabLabels, tab);
    ctx.state.profileTab = static_cast<ProfileTab>(tab);
    divider(ui, main.x, main.r(), head.b() + 44.f);

    // Tab change: short cross-fade with a small rise.
    if (ctx.state.profileTabShown != ctx.state.profileTab) {
        ctx.state.profileTabShown = ctx.state.profileTab;
        ctx.state.profileTabChangedAt = ui.time;
    }
    const float k = ease::outCubic(motion::progress(ui.time, ctx.state.profileTabChangedAt, motion::kTab));
    const Rect content{main.x, head.b() + 56.f, main.w, main.b() - head.b() - 56.f};
    ui.r.pushAlpha(k);
    ui.r.pushOffset(0.f, (1.f - k) * motion::travel(motion::kTabTravel));
    switch (ctx.state.profileTab) {
        case ProfileTab::Summary: summaryTab(ctx, *p, content); break;
        case ProfileTab::Evidence: drawEvidenceTab(ctx, *p, content); break;
        case ProfileTab::Behavior: behaviorTab(ctx, *p, content); break;
        case ProfileTab::Identity: identityTab(ctx, *p, content); break;
    }
    ui.r.popOffset();
    ui.r.popAlpha();
}

}  // namespace vigil::ui
