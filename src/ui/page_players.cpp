// Players — behavioral analysis over time. Independent of verdicts: how a player fights, clicks
// and moves, how suspicion evolved per check, and what patterns the evidence shows.

#include "pages.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace vigil::ui {
namespace {

using detection::PlayerRecord;

void statLine(Ui& ui, float x, float y, float w, const std::string& label, const std::string& value, Color vc) {
    const Theme& t = ui.th();
    ui.fonts.draw(ui.r, type::Small, x, y, label, t.textMuted);
    ui.fonts.draw(ui.r, type::BodyStrong, x + w, y - 1.f, value, vc, Align::Right);
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
    auto topCheck = std::max_element(byCheck.begin(), byCheck.end(), [](auto& a, auto& b) { return a.second < b.second; });
    out.push_back(checks[static_cast<std::size_t>(topCheck->first)]->name() + " produced the most observations (" +
                  std::to_string(topCheck->second) + "), peaking at " +
                  formatNumber(p.checks[static_cast<std::size_t>(topCheck->first)].peak, 0) + "% confidence.");
    if (byCheck.size() >= 2) {
        out.push_back(std::to_string(byCheck.size()) + " different checks agree. Independent checks pointing the same way are "
                      "stronger evidence than one check repeating.");
    }
    if (inCombat > 0) {
        out.push_back(std::to_string(inCombat) + " observations involved another player, so they occurred during fights.");
    }
    if (p.stats.reachSamples >= 5) {
        const double avg = p.stats.reachSum / p.stats.reachSamples;
        out.push_back("Average attributed hit distance is " + formatNumber(avg, 2) + " blocks over " +
                      std::to_string(p.stats.reachSamples) + " hits" + (avg > 3.0 ? ", beyond vanilla reach." : "."));
    }
    if (p.priorSessions > 0 && p.priorDetections > 0) {
        out.push_back("Vigil detected this player " + std::to_string(p.priorDetections) + " time(s) in earlier sessions.");
    }
    return out;
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

}  // namespace

void drawPlayers(PageContext& ctx, const Rect& area) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    const auto& checks = ctx.engine.checks();
    pageHeader(ctx, area, "Players", "How each player behaves over time, independent of any verdict.");
    const float top = area.y + 70.f;
    const Rect list{area.x, top, 270.f, area.b() - top};

    // List with sort control.
    card(ui, list);
    static const std::vector<std::string> sorts = {"Suspicion", "Recent", "Name"};
    segmented(ui, hashId("pl-sort"), {list.x + 14.f, list.y + 14.f, list.w - 28.f, 30.f}, sorts, ctx.state.playersSort);
    auto players = sortedPlayers(ctx.engine.session(), ctx.state.playersSort, {}, true);
    const Rect view{list.x + 6.f, list.y + 54.f, list.w - 12.f, list.h - 60.f};
    if (!players.empty() && (ctx.state.selectedPlayer.empty() || !ctx.engine.session().find(ctx.state.selectedPlayer))) {
        ctx.state.selectedPlayer = players.front()->key;
    }
    {
        const Id sid = hashId("pl-list");
        const float off = beginScroll(ui, sid, view);
        float y = view.y + off + 2.f;
        for (const PlayerRecord* p : players) {
            const Rect row{view.x, y, view.w, 50.f};
            const Id rid = hashId(p->key, 9001);
            const bool sel = ctx.state.selectedPlayer == p->key;
            bool h = false;
            if (ui.pressable(rid, row, &h)) ctx.state.selectedPlayer = p->key;
            const float hv = ui.anim(mixId(rid, 1), sel ? 1.f : (h ? 0.5f : 0.f), 16.f);
            if (hv > 0.01f) ui.r.roundRect(row.inset(4.f, 2.f), 10.f, (sel ? t.accentSoft() : t.surfaceHover).alpha(sel ? 1.f : hv * 1.6f));
            // Initial avatar.
            const Rect av{row.x + 12.f, row.cy() - 15.f, 30.f, 30.f};
            ui.r.roundRect(av, 15.f, t.severity(p->suspicion).withAlpha(0.16f));
            std::string ini = p->name.empty() ? "?" : p->name.substr(0, 1);
            ui.fonts.draw(ui.r, type::BodyStrong, av.cx(), av.cy() - 9.f, ini, t.severity(p->suspicion), Align::Center);
            ui.fonts.draw(ui.r, type::BodyStrong, row.x + 52.f, row.y + 8.f, p->name + (p->local ? " (you)" : ""),
                          p->present ? t.text : t.textMuted, Align::Left, row.w - 110.f);
            ui.fonts.draw(ui.r, type::Caption, row.x + 52.f, row.y + 28.f,
                          (p->present ? "In view \xC2\xB7 " : "Left \xC2\xB7 ") + formatDuration(p->stats.ticksObserved * 0.05) + " observed",
                          t.textFaint, Align::Left, row.w - 110.f);
            ui.fonts.draw(ui.r, type::SmallStrong, row.r() - 14.f, row.y + 16.f, formatNumber(p->suspicion, 0) + "%",
                          t.severity(p->suspicion), Align::Right);
            y += 52.f;
        }
        endScroll(ui, sid, view, y - (view.y + off) + 6.f);
    }
    if (players.empty()) {
        emptyState(ui, view, Icon::Players, "No players yet", "Players are profiled while Vigil observes a game.");
    }

    const Rect main{list.r() + 14.f, top, area.r() - list.r() - 14.f, area.b() - top};
    const PlayerRecord* p = ctx.engine.session().find(ctx.state.selectedPlayer);
    if (!p) {
        card(ui, main);
        emptyState(ui, main, Icon::Players, "Select a player", "Behavior profiles build up as Vigil observes play.");
        return;
    }

    const Id sid = hashId("pl-profile");
    const float off = beginScroll(ui, sid, main);
    float y = main.y + off;
    const float w = main.w - 8.f;
    const float gap = 12.f;

    // Header.
    {
        const Rect hc{main.x, y, w, 84.f};
        card(ui, hc);
        confidenceRing(ui, hashId("pl-ring", hashId(p->key)), hc.x + 46.f, hc.cy(), 26.f, p->suspicion, 5.f, true);
        ui.fonts.draw(ui.r, type::Heading, hc.x + 88.f, hc.y + 16.f, p->name, t.text, Align::Left, hc.w - 260.f);
        const std::string sub = "First seen " + agoLabel(ctx.nowMs, p->firstSeenMs) + " \xC2\xB7 " +
                                formatDuration(p->stats.ticksObserved * 0.05) + " observed \xC2\xB7 " +
                                formatNumber(p->stats.ticksObserved ? 100.0 * p->stats.ticksInCombat / p->stats.ticksObserved : 0.0, 0) +
                                "% in combat";
        ui.fonts.draw(ui.r, type::Small, hc.x + 88.f, hc.y + 52.f, sub, t.textMuted, Align::Left, hc.w - 260.f);
        const Icon ic = Icon::Evidence;
        const float bw = buttonWidth(ui, "Open evidence", true);
        if (button(ui, hashId("pl-open-ev"), {hc.r() - bw - 18.f, hc.cy() - 17.f, bw, 34.f}, "Open evidence", ButtonKind::Secondary, &ic)) {
            ctx.openPlayer(p->key);
        }
        y += hc.h + gap;
    }

    const float colW = (w - gap) * 0.5f;
    // Combat.
    {
        const Rect c{main.x, y, colW, 214.f};
        card(ui, c);
        sectionHeader(ui, c.x + 20.f, c.y + 18.f, "COMBAT");
        const auto& s = p->stats;
        const float sx = c.x + 20.f, sw = c.w - 40.f;
        statLine(ui, sx, c.y + 46.f, sw, "Hits landed / taken", std::to_string(s.hitsLanded) + " / " + std::to_string(s.hitsTaken), t.text);
        statLine(ui, sx, c.y + 70.f, sw, "Average reach",
                 s.reachSamples ? formatNumber(s.reachSum / s.reachSamples, 2) + " blocks" : "\xE2\x80\x94", t.text);
        statLine(ui, sx, c.y + 94.f, sw, "Longest hit", s.reachSamples ? formatNumber(s.reachMax, 2) + " blocks" : "\xE2\x80\x94",
                 s.reachMax > 3.0 ? t.sevAlert : t.text);
        if (!s.reachRecent.empty()) reachHistogram(ui, *p, {sx, c.y + 136.f, sw, 48.f});
        else ui.fonts.draw(ui.r, type::Small, sx, c.y + 140.f, "Reach distribution appears after attributed hits.", t.textFaint);
    }
    // Clicking & movement.
    {
        const Rect c{main.x + colW + gap, y, colW, 214.f};
        card(ui, c);
        sectionHeader(ui, c.x + 20.f, c.y + 18.f, "CLICKING & MOVEMENT");
        const auto& s = p->stats;
        const float sx = c.x + 20.f, sw = c.w - 40.f;
        statLine(ui, sx, c.y + 46.f, sw, "Peak swings per second", formatNumber(s.cpsPeak, 0), t.text);
        const double avgSpeed = s.speedSamples ? s.speedSum / s.speedSamples : 0.0;
        statLine(ui, sx, c.y + 70.f, sw, "Average speed", formatNumber(avgSpeed * 20.0, 1) + " m/s", t.text);
        statLine(ui, sx, c.y + 94.f, sw, "Peak speed (0.5 s)", formatNumber(s.speedPeak * 20.0, 1) + " m/s",
                 s.speedPeak > 0.42 ? t.sevNotice : t.text);
        statLine(ui, sx, c.y + 118.f, sw, "Jumps \xC2\xB7 average air time",
                 std::to_string(s.jumps) + " \xC2\xB7 " + (s.jumps ? formatNumber(s.airTicksSum / s.jumps, 1) + " ticks" : "\xE2\x80\x94"),
                 t.text);
        // Speed trace.
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

    // Confidence by check.
    {
        std::vector<int> rows;
        for (std::size_t i = 0; i < p->checks.size(); ++i) if (p->checks[i].evidence > 0 || p->checks[i].peak > 0.f) rows.push_back(static_cast<int>(i));
        const float h = 52.f + std::max<std::size_t>(1, rows.size()) * 44.f;
        const Rect c{main.x, y, w, h};
        card(ui, c);
        sectionHeader(ui, c.x + 20.f, c.y + 18.f, "CONFIDENCE BY CHECK", "last 15 minutes");
        float ry = c.y + 46.f;
        const double t1 = ctx.nowMs, t0 = t1 - 15 * 60e3;
        if (rows.empty()) ui.fonts.draw(ui.r, type::Small, c.x + 20.f, ry + 6.f, "No check has evidence for this player.", t.textFaint);
        for (int ci : rows) {
            const auto& cr = p->checks[static_cast<std::size_t>(ci)];
            const auto& chk = *checks[static_cast<std::size_t>(ci)];
            drawIcon(ui.r, checkIcon(chk.id()), c.x + 20.f, ry + 6.f, 18.f, t.textMuted);
            ui.fonts.draw(ui.r, type::BodyStrong, c.x + 48.f, ry + 3.f, chk.name(), t.text);
            ui.fonts.draw(ui.r, type::Caption, c.x + 48.f, ry + 21.f,
                          std::to_string(cr.evidence) + " evidence \xC2\xB7 peak " + formatNumber(cr.peak, 0) + "%", t.textFaint);
            const float bx = c.x + 230.f;
            const float bw = c.w - 230.f - 240.f;
            meter(ui, {bx, ry + 13.f, bw, 6.f}, cr.model.value() / 100.f, t.severity(cr.model.value()));
            ui.fonts.draw(ui.r, type::SmallStrong, bx + bw + 12.f, ry + 8.f, formatNumber(cr.model.value(), 0) + "%",
                          t.severity(cr.model.value()));
            sparkline(ui, {c.r() - 180.f, ry + 2.f, 160.f, 30.f}, cr.history, t0, t1, t.severity(cr.peak));
            ry += 44.f;
        }
        y += h + gap;
    }

    // Patterns & history.
    {
        const auto lines = patterns(ctx, *p);
        float h = 56.f;
        for (const auto& l : lines) h += ui.fonts.wrap(type::Body, l, w - 70.f).size() * 20.f + 10.f;
        const Rect c{main.x, y, w, h};
        card(ui, c);
        sectionHeader(ui, c.x + 20.f, c.y + 18.f, "PATTERNS");
        float ly = c.y + 46.f;
        for (const auto& l : lines) {
            drawIcon(ui.r, Icon::Spark, c.x + 20.f, ly + 1.f, 14.f, t.accent, 1.4f);
            ly += ui.fonts.drawWrapped(ui.r, type::Body, c.x + 44.f, ly, w - 70.f, l, t.textMuted, 4.f) + 10.f;
        }
        y += h + gap;
    }
    endScroll(ui, sid, main, y - (main.y + off));
}

}  // namespace vigil::ui
