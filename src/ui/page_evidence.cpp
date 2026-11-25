// Evidence tab of a player profile — the investigation workspace:
//   timeline (suspicion curve, per-check evidence lanes, detections) → observations
//   → inspector (reasoning, how confidence formed, measured values against limits, debug output)

#include "pages.h"

#include <algorithm>
#include <cmath>

namespace vigil::ui {
namespace {

using detection::Detection;
using detection::Evidence;
using detection::PlayerRecord;

const Evidence* findEvidence(const PlayerRecord& r, std::uint64_t id) {
    for (const auto& e : r.evidence) if (e.id == id) return &e;
    return nullptr;
}

const Detection* findDetection(const PlayerRecord& r, std::uint64_t id) {
    for (const auto& d : r.detections) if (d.id == id) return &d;
    return nullptr;
}

void timeline(PageContext& ctx, const PlayerRecord& p, const Rect& rc) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    const auto& checks = ctx.engine.checks();
    card(ui, rc);
    sectionHeader(ui, rc.x + 22.f, rc.y + 18.f, "TIMELINE");
    static const std::vector<std::string> windows = {"1 min", "5 min", "15 min", "Session"};
    segmented(ui, hashId("tl-window"), {rc.r() - 300.f, rc.y + 10.f, 282.f, 28.f}, windows, ctx.state.timelineWindow);

    const double t1 = ctx.nowMs;
    const double spans[3] = {60e3, 300e3, 900e3};
    double t0 = ctx.state.timelineWindow < 3 ? t1 - spans[ctx.state.timelineWindow]
                                             : std::min(ctx.engine.session().startMs(), p.firstSeenMs);
    if (t1 - t0 < 10e3) t0 = t1 - 10e3;
    const float labelW = 104.f;
    const Rect plot{rc.x + 22.f + labelW, rc.y + 50.f, rc.w - 44.f - labelW, rc.h - 84.f};
    auto xOf = [&](double tm) { return plot.x + static_cast<float>((tm - t0) / (t1 - t0)) * plot.w; };

    // Lanes: checks with evidence for this player.
    std::vector<int> lanes;
    for (std::size_t i = 0; i < p.checks.size(); ++i) if (p.checks[i].evidence > 0) lanes.push_back(static_cast<int>(i));
    std::sort(lanes.begin(), lanes.end(), [&](int a, int b) { return p.checks[static_cast<std::size_t>(a)].peak > p.checks[static_cast<std::size_t>(b)].peak; });
    if (lanes.size() > 4) lanes.resize(4);
    const float chartH = 62.f;
    const float laneH = lanes.empty() ? 0.f : std::min(26.f, (plot.h - chartH - 8.f) / lanes.size());
    const Rect chart{plot.x, plot.y, plot.w, chartH};

    const int ticks = 5;
    for (int i = 0; i <= ticks; ++i) {
        const float x = plot.x + plot.w * i / ticks;
        ui.r.rect({x, plot.y, 1.f, plot.h}, t.divider);
        const double secAgo = (t1 - t0) * (1.0 - static_cast<double>(i) / ticks) / 1000.0;
        const std::string lbl = i == ticks ? "now" : "-" + formatClock(secAgo);
        ui.fonts.draw(ui.r, type::Caption, x, plot.b() + 8.f, lbl, t.textFaint, i == 0 ? Align::Left : i == ticks ? Align::Right : Align::Center);
    }
    // Threshold guide (50%).
    ui.r.rect({chart.x, chart.b() - chart.h * 0.5f, chart.w, 1.f}, t.sevNotice.withAlpha(0.18f));
    ui.fonts.draw(ui.r, type::Caption, rc.x + 22.f, chart.y + 2.f, "Suspicion", t.textMuted);
    ui.fonts.draw(ui.r, type::Caption, rc.x + 22.f, chart.y + 18.f, formatNumber(p.suspicion, 0) + "% now", t.severity(p.suspicion));
    ui.r.pushClip(chart.inset(0, -2.f));
    sparkline(ui, chart, p.suspicionHistory, t0, t1, t.severity(std::max(p.suspicion, p.peakSuspicion * 0.7f)), true);
    ui.r.popClip();

    struct Hit { float x, y; std::uint64_t ev = 0, det = 0; std::string tip; };
    std::vector<Hit> hits;
    for (const auto& d : p.detections) {
        if (d.timeMs < t0) continue;
        const float x = xOf(d.timeMs);
        ui.r.rect({x - 0.5f, plot.y, 1.f, plot.h}, t.sevAlert.withAlpha(0.35f));
        const bool sel = ctx.state.selectedDetection == d.id;
        if (sel) ui.r.ring(x, chart.y + 6.f, 9.f, 1.5f, t.accent);
        ui.r.diamond(x, chart.y + 6.f, 5.5f, t.sevAlert);
        hits.push_back({x, chart.y + 6.f, 0, d.id, "Detection \xC2\xB7 " + checks[static_cast<std::size_t>(d.check)]->name() + " " +
                                                     formatNumber(d.confidence, 0) + "%"});
    }
    for (std::size_t li = 0; li < lanes.size(); ++li) {
        const int ci = lanes[li];
        const float ly = chart.b() + 8.f + laneH * li;
        const Rect lane{plot.x, ly, plot.w, laneH};
        ui.r.roundRect(lane.inset(0, 3.f), 6.f, t.inset.alpha(0.55f));
        ui.fonts.draw(ui.r, type::Caption, rc.x + 22.f, lane.cy() - 7.f, checks[static_cast<std::size_t>(ci)]->name(), t.textMuted,
                      Align::Left, labelW - 8.f);
        for (const auto& e : p.evidence) {
            if (e.check != ci || e.timeMs < t0) continue;
            const float x = xOf(e.timeMs);
            const bool sel = ctx.state.selectedEvidence == e.id;
            const float rad = 2.8f + 3.f * e.severity;
            const Color c = t.severity(e.confidenceAfter);
            if (sel) ui.r.ring(x, lane.cy(), rad + 4.f, 1.5f, t.accent);
            ui.r.circle(x, lane.cy(), rad, c);
            hits.push_back({x, lane.cy(), e.id, 0, e.summary});
        }
    }
    if (lanes.empty()) {
        ui.fonts.draw(ui.r, type::Small, plot.cx(), chart.b() + 18.f, "No evidence for this player yet", t.textFaint, Align::Center);
    }
    const float mx = ui.in.mx - ui.r.offsetX(), my = ui.in.my - ui.r.offsetY();
    if (ui.hover(plot.inset(-6.f))) {
        const Hit* best = nullptr;
        float bd = 9.f;
        for (const Hit& h : hits) {
            const float d = std::hypot(h.x - mx, h.y - my);
            if (d < bd) { bd = d; best = &h; }
        }
        if (best) {
            ui.cursor = Cursor::Hand;
            ui.tooltip(mixId(hashId("tl-hit"), best->ev ? best->ev : best->det + (1ull << 40)), {best->x - 6.f, best->y - 6.f, 12.f, 12.f}, best->tip);
            if (ui.in.pressed[0]) {
                ctx.state.selectedEvidence = best->ev;
                ctx.state.selectedDetection = best->det;
                ctx.state.inspectorTab = 0;
            }
        } else {
            // Scrub line with the suspicion at that moment.
            ui.r.rect({mx, plot.y, 1.f, plot.h}, t.textFaint.alpha(0.5f));
            const double tm = t0 + (mx - plot.x) / plot.w * (t1 - t0);
            float v = 0.f;
            for (const auto& pt : p.suspicionHistory) { if (pt.timeMs > tm) break; v = pt.value; }
            const std::string lbl = formatNumber(v, 0) + "% \xC2\xB7 -" + formatClock((t1 - tm) / 1000.0);
            const float lw = ui.fonts.width(type::Caption, lbl) + 12.f;
            const Rect tag{std::min(mx + 6.f, plot.r() - lw), plot.y - 2.f, lw, 18.f};
            ui.r.roundRect(tag, 5.f, t.surfaceRaised);
            ui.fonts.draw(ui.r, type::Caption, tag.x + 6.f, tag.y + 2.f, lbl, t.text);
        }
    }
}

void evidenceList(PageContext& ctx, const PlayerRecord& p, const Rect& rc) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    const auto& checks = ctx.engine.checks();
    card(ui, rc);
    sectionHeader(ui, rc.x + 22.f, rc.y + 18.f, "OBSERVATIONS");
    std::vector<int> present;
    for (std::size_t i = 0; i < p.checks.size(); ++i) if (p.checks[i].evidence > 0) present.push_back(static_cast<int>(i));
    float cx = rc.x + 22.f + ui.fonts.width(type::Overline, "OBSERVATIONS") + 16.f;
    auto filterChip = [&](int check, const std::string& label) {
        const float cw = chipWidth(ui, label);
        const Rect chr{cx, rc.y + 14.f, cw, 20.f};
        if (chr.r() > rc.r() - 16.f) return;
        const bool on = ctx.state.evidenceFilterCheck == check;
        if (ui.pressable(hashId("evf", static_cast<Id>(check + 10)), chr)) ctx.state.evidenceFilterCheck = check;
        chip(ui, chr, label, on ? t.accent : t.textMuted, false);
        if (on) ui.r.roundRectStroke(chr, 10.f, 1.f, t.accent.withAlpha(0.5f));
        cx += cw + 6.f;
    };
    filterChip(-1, "All");
    for (int ci : present) filterChip(ci, checks[static_cast<std::size_t>(ci)]->name());

    const Rect view{rc.x + 6.f, rc.y + 44.f, rc.w - 12.f, rc.h - 50.f};
    std::vector<const Evidence*> items;
    for (auto it = p.evidence.rbegin(); it != p.evidence.rend(); ++it) {
        if (ctx.state.evidenceFilterCheck >= 0 && it->check != ctx.state.evidenceFilterCheck) continue;
        items.push_back(&*it);
    }
    if (items.empty()) {
        emptyState(ui, view, Icon::Layers, "No observations", "Nothing unusual has been recorded for this player.");
        return;
    }
    const Id sid = hashId("ev-list", hashId(p.key));
    const float off = beginScroll(ui, sid, view);
    float y = view.y + off;
    const double start = ctx.engine.session().startMs();
    for (const Evidence* e : items) {
        const Rect row{view.x, y, view.w, 46.f};
        if (row.b() >= view.y && row.y <= view.b()) {
            const Id rid = mixId(hashId("evrow"), e->id);
            const bool sel = ctx.state.selectedEvidence == e->id;
            bool h = false;
            if (ui.pressable(rid, row, &h)) {
                ctx.state.selectedEvidence = e->id;
                ctx.state.selectedDetection = 0;
                ctx.state.inspectorTab = 0;
            }
            const float hv = ui.anim(mixId(rid, 1), sel ? 1.f : (h ? 0.5f : 0.f), motion::kHover);
            if (hv > 0.01f) ui.r.roundRect(row.inset(4.f, 2.f), 9.f, (sel ? t.accentSoft() : t.surfaceHover).alpha(sel ? 1.f : hv * 1.6f));
            const Color c = t.severity(e->confidenceAfter);
            ui.r.circle(row.x + 20.f, row.cy(), 4.f, c);
            ui.fonts.draw(ui.r, type::Mono, row.x + 34.f, row.cy() - 8.f, formatClock((e->timeMs - start) / 1000.0), t.textFaint);
            const std::string cn = checks[static_cast<std::size_t>(e->check)]->name();
            ui.fonts.draw(ui.r, type::SmallStrong, row.x + 84.f, row.y + 7.f, cn, t.text, Align::Left, 110.f);
            ui.fonts.draw(ui.r, type::Small, row.x + 84.f, row.y + 25.f, e->summary, t.textMuted, Align::Left, row.w - 150.f);
            ui.fonts.draw(ui.r, type::Caption, row.r() - 14.f, row.y + 8.f, "+" + formatNumber(e->contribution, 1), t.textFaint, Align::Right);
            ui.fonts.draw(ui.r, type::SmallStrong, row.r() - 14.f, row.y + 24.f, formatNumber(e->confidenceAfter, 0) + "%", c, Align::Right);
        }
        y += 46.f;
    }
    endScroll(ui, sid, view, y - (view.y + off) + 6.f);
}

float valueTable(Ui& ui, const Evidence& e, float x, float y, float w) {
    const Theme& t = ui.th();
    for (const auto& v : e.values) {
        ui.fonts.draw(ui.r, type::Small, x, y, v.name, t.textMuted, Align::Left, w * 0.55f);
        std::string val = formatNumber(v.value, std::fabs(v.value) >= 100 ? 0 : std::fabs(v.value) >= 10 ? 1 : 2);
        if (!v.unit.empty()) val += (v.unit == "%" || v.unit == "\xC2\xB0" || v.unit == "x") ? v.unit : " " + v.unit;
        ui.fonts.draw(ui.r, type::SmallStrong, x + w, y, val, t.text, Align::Right);
        y += 18.f;
        if (v.hasLimit && v.limit != 0.0) {
            const float frac = static_cast<float>(std::clamp(v.value / (v.limit * 1.5), 0.0, 1.0));
            const bool over = v.value > v.limit;
            const Rect bar{x, y + 1.f, w, 5.f};
            meter(ui, bar, frac, (over ? t.sevAlert : t.sevClear).alpha(0.9f));
            const float lx = x + w * (1.f / 1.5f);
            ui.r.rect({lx - 0.75f, y - 2.f, 1.5f, 11.f}, t.text.alpha(0.6f));
            ui.fonts.draw(ui.r, type::Caption, lx + 5.f, y + 6.f, "limit " + formatNumber(v.limit, std::fabs(v.limit) >= 10 ? 1 : 2),
                          t.textFaint);
            y += 24.f;
        }
        y += 6.f;
    }
    return y;
}

// How one observation moved confidence: strength, headroom, gain → points.
float formation(PageContext& ctx, const Evidence& ev, float x, float y, float w) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    const auto& chk = *ctx.engine.checks()[static_cast<std::size_t>(ev.check)];
    const auto params = chk.confidenceParams();
    const float before = std::max(0.f, ev.confidenceAfter - ev.contribution);
    const float strength = ev.weight * ev.severity;
    const float headroom = 100.f - before;
    const float fraction = headroom > 0.01f ? ev.contribution / headroom : 0.f;
    ui.fonts.draw(ui.r, type::Overline, x, y, "HOW CONFIDENCE FORMED", t.textFaint);
    y += 20.f;
    // Stacked bar: before | added | headroom left.
    const Rect bar{x, y, w, 10.f};
    ui.r.roundRect(bar, 5.f, t.inset);
    ui.r.pushClip(bar);
    ui.r.rect({bar.x, bar.y, bar.w * before / 100.f, bar.h}, t.severity(before).alpha(0.55f));
    ui.r.rect({bar.x + bar.w * before / 100.f, bar.y, bar.w * ev.contribution / 100.f, bar.h}, t.severity(ev.confidenceAfter));
    ui.r.popClip();
    y += 18.f;
    ui.fonts.draw(ui.r, type::Caption, x, y, formatNumber(before, 1) + "% before", t.textFaint);
    ui.fonts.draw(ui.r, type::Caption, x + w, y, formatNumber(ev.confidenceAfter, 1) + "% after", t.text, Align::Right);
    y += 22.f;
    struct Line { std::string label, value; };
    const Line lines[] = {
        {"Strength = weight \xC3\x97 severity", formatNumber(ev.weight, 2) + " \xC3\x97 " + formatNumber(ev.severity, 2) + " = " + formatNumber(strength, 2)},
        {"Headroom = 100 \xE2\x88\x92 before", formatNumber(headroom, 1)},
        {"Share of headroom (gain " + formatNumber(params.gain, 2) + ")", formatNumber(fraction * 100.f, 1) + "%"},
        {"Added", "+" + formatNumber(ev.contribution, 1) + " points"},
    };
    for (const Line& l : lines) {
        ui.fonts.draw(ui.r, type::Small, x, y, l.label, t.textMuted, Align::Left, w * 0.62f);
        ui.fonts.draw(ui.r, type::SmallStrong, x + w, y, l.value, t.text, Align::Right);
        y += 19.f;
    }
    y += 4.f;
    y += ui.fonts.drawWrapped(ui.r, type::Caption, x, y, w,
                              "Each observation adds a share of the remaining headroom, so repeated evidence approaches 100% "
                              "without reaching it, and confidence halves every " + formatNumber(params.halfLifeSec, 0) +
                                  " s without new evidence. A detection needs " + formatNumber(params.alertThreshold, 0) +
                                  "% and at least " + std::to_string(params.minEvidence) + " observations within " +
                                  formatNumber(params.windowSec, 0) + " s.",
                              t.textFaint, 3.f) + 12.f;
    return y;
}

void inspector(PageContext& ctx, const PlayerRecord& p, const Rect& rc) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    const auto& checks = ctx.engine.checks();
    card(ui, rc, true);
    static const std::vector<std::string> tabsL = {"Reasoning", "Debug"};
    segmented(ui, hashId("insp-tabs"), {rc.x + 16.f, rc.y + 14.f, 180.f, 28.f}, tabsL, ctx.state.inspectorTab);
    const Rect view{rc.x + 4.f, rc.y + 52.f, rc.w - 8.f, rc.h - 56.f};
    const Id sid = hashId("insp-scroll");
    const float off = beginScroll(ui, sid, view);
    float y = view.y + off + 6.f;
    const float x = rc.x + 20.f, w = rc.w - 40.f;

    if (ctx.state.inspectorTab == 1) {
        const auto& dl = ctx.engine.debugLog();
        int n = 0;
        for (auto it = dl.rbegin(); it != dl.rend() && n < 60; ++it) {
            if (it->player != p.name) continue;
            ui.fonts.draw(ui.r, type::Caption, x, y, checks[static_cast<std::size_t>(it->check)]->name(), t.accent);
            y += 16.f;
            y += ui.fonts.drawWrapped(ui.r, type::Mono, x, y, w, it->text, t.textMuted, 2.f) + 8.f;
            ++n;
        }
        if (n == 0) {
            y += ui.fonts.drawWrapped(ui.r, type::Small, x, y, w,
                                      "No debug output for this player. Enable Debug output on a check to record the raw inputs "
                                      "of every evaluation here.", t.textFaint, 3.f);
        }
        endScroll(ui, sid, view, y - (view.y + off) + 10.f);
        return;
    }

    const Detection* det = ctx.state.selectedDetection ? findDetection(p, ctx.state.selectedDetection) : nullptr;
    const Evidence* ev = (!det && ctx.state.selectedEvidence) ? findEvidence(p, ctx.state.selectedEvidence) : nullptr;
    if (!det && !ev && !p.evidence.empty()) ev = &p.evidence.back();

    if (det) {
        const auto& chk = *checks[static_cast<std::size_t>(det->check)];
        ui.fonts.draw(ui.r, type::Overline, x, y, "DETECTION", t.sevAlert);
        y += 18.f;
        ui.fonts.draw(ui.r, type::Title, x, y, chk.name() + " \xC2\xB7 " + formatNumber(det->confidence, 0) + "%", t.text);
        y += 26.f;
        y += ui.fonts.drawWrapped(ui.r, type::Small, x, y, w,
                                  "Confidence crossed the alert threshold with " + std::to_string(det->evidenceCount) +
                                      " observations from " + std::to_string(det->distinctIndicators) +
                                      " indicator(s) inside the evidence window.", t.textMuted, 3.f) + 12.f;
        ui.fonts.draw(ui.r, type::Overline, x, y, "SUPPORTING EVIDENCE", t.textFaint);
        y += 20.f;
        for (std::uint64_t id : det->evidence) {
            const Evidence* e = findEvidence(p, id);
            if (!e) continue;
            const Rect row{x - 8.f, y - 4.f, w + 16.f, 40.f};
            bool h = false;
            if (ui.pressable(mixId(hashId("det-ev"), id), row, &h)) {
                ctx.state.selectedEvidence = id;
                ctx.state.selectedDetection = 0;
            }
            if (h) ui.r.roundRect(row, 8.f, t.surfaceHover);
            ui.r.circle(x + 4.f, y + 9.f, 3.5f, t.severity(e->confidenceAfter));
            ui.fonts.draw(ui.r, type::SmallStrong, x + 16.f, y + 1.f, chk.indicators()[static_cast<std::size_t>(e->indicator)].name, t.text);
            ui.fonts.draw(ui.r, type::Small, x + 16.f, y + 18.f, e->summary, t.textMuted, Align::Left, w - 16.f);
            y += 44.f;
        }
    } else if (ev) {
        const auto& chk = *checks[static_cast<std::size_t>(ev->check)];
        const auto& ind = chk.indicators()[static_cast<std::size_t>(ev->indicator)];
        const Color sev = t.severity(ev->confidenceAfter);
        ui.fonts.draw(ui.r, type::Overline, x, y, chk.name() + "  \xC2\xB7  " + ind.name, sev);
        y += 20.f;
        y += ui.fonts.drawWrapped(ui.r, type::Title, x, y, w, ev->summary, t.text, 4.f) + 6.f;
        const double start = ctx.engine.session().startMs();
        ui.fonts.draw(ui.r, type::Caption, x, y, "Session " + formatClock((ev->timeMs - start) / 1000.0) + "  \xC2\xB7  " +
                                                     agoLabel(ctx.nowMs, ev->timeMs) + (ev->target.empty() ? "" : "  \xC2\xB7  vs " + ev->target),
                      t.textFaint);
        y += 26.f;
        ui.fonts.draw(ui.r, type::Overline, x, y, "WHY IT MATTERS", t.textFaint);
        y += 18.f;
        y += ui.fonts.drawWrapped(ui.r, type::Small, x, y, w, ev->detail, t.textMuted, 3.f) + 14.f;
        y = formation(ctx, *ev, x, y, w);
        ui.fonts.draw(ui.r, type::Overline, x, y, "MEASURED", t.textFaint);
        y += 20.f;
        y = valueTable(ui, *ev, x, y, w);
        y += 4.f;
    } else {
        emptyState(ui, view, Icon::Layers, "Select an observation", "Choose a marker on the timeline or a row in the list.");
    }
    endScroll(ui, sid, view, y - (view.y + off) + 10.f);
}

}  // namespace

void drawEvidenceTab(PageContext& ctx, const PlayerRecord& p, const Rect& area) {
    const float tlH = std::min(250.f, std::max(190.f, area.h * 0.40f));
    const Rect tl{area.x, area.y, area.w, tlH};
    timeline(ctx, p, tl);
    const float bottomY = tl.b() + 12.f;
    const float inspW = std::min(380.f, area.w * 0.46f);
    const Rect listR{area.x, bottomY, area.w - inspW - 12.f, area.b() - bottomY};
    const Rect insp{listR.r() + 12.f, bottomY, inspW, area.b() - bottomY};
    evidenceList(ctx, p, listR);
    inspector(ctx, p, insp);
}

}  // namespace vigil::ui
