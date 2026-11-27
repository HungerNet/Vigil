// Checks — module manager. Category → check list → detail with sensitivity presets, indicators,
// thresholds, confidence behavior, sampling and debug. All controls are generated from the
// settings each check declares.

#include "pages.h"

#include "../core/settings.h"
#include "../detection/config_binding.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace vigil::ui {
namespace {

std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

int sessionEvidence(const detection::Session& s, int check) {
    int n = 0;
    for (const auto& r : s.players()) n += r.checks[static_cast<std::size_t>(check)].evidence;
    return n;
}

int sessionDetections(const detection::Session& s, int check) {
    int n = 0;
    for (const auto& r : s.players()) n += r.checks[static_cast<std::size_t>(check)].detections;
    return n;
}

// Card with a title; returns the content start y.
float sectionCard(Ui& ui, const Rect& rc, const char* title, const char* subtitle) {
    card(ui, rc);
    sectionHeader(ui, rc.x + 22.f, rc.y + 18.f, title, subtitle ? subtitle : "");
    return rc.y + 44.f;
}

}  // namespace

void drawChecks(PageContext& ctx, const Rect& area) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    auto& checks = ctx.engine.checks();
    if (checks.empty()) return;
    auto& st = ctx.state;
    st.selectedCheck = std::clamp(st.selectedCheck, 0, static_cast<int>(checks.size()) - 1);

    const float top = pageHeader(ctx, area, "Checks", pagePurpose(Page::Checks));

    const Rect list{area.x, top, 300.f, area.b() - top};
    const Rect detail{list.r() + 16.f, top, area.r() - list.r() - 16.f, area.b() - top};

    // ── List ──
    card(ui, list);
    textField(ui, hashId("check-search"), {list.x + 14.f, list.y + 14.f, list.w - 28.f, 34.f}, st.checkSearch, "Search checks");
    const Rect lv{list.x + 6.f, list.y + 58.f, list.w - 12.f, list.h - 64.f};
    const Id lsid = hashId("checks-list");
    // Signals the game adapter could not provide, by the checks they feed (Status lists them too).
    RuntimeStatus rs;
    if (ctx.host) rs = ctx.host->runtimeStatus();
    auto missingFor = [&](const std::string& checkName) {
        std::string missing;
        if (!rs.hosted || !rs.samplerResolved) return missing;
        for (const auto& s : rs.signals) {
            if (s.available || s.feeds.find(checkName) == std::string::npos) continue;
            missing += (missing.empty() ? "" : ", ") + s.name;
        }
        return missing;
    };
    const float loff = beginScroll(ui, lsid, lv);
    float y = lv.y + loff + 4.f;
    const std::string q = lowerCopy(st.checkSearch);
    for (detection::CheckCategory cat : {detection::CheckCategory::Combat, detection::CheckCategory::Movement, detection::CheckCategory::World}) {
        bool headerDrawn = false;
        for (const auto& c : checks) {
            if (c->category() != cat) continue;
            if (!q.empty() && lowerCopy(c->name()).find(q) == std::string::npos && lowerCopy(c->summary()).find(q) == std::string::npos) continue;
            if (!headerDrawn) {
                headerDrawn = true;
                y += 8.f;
                drawIcon(ui.r, categoryIcon(cat), lv.x + 12.f, y - 1.f, 13.f, t.textFaint, 1.5f);
                std::string cn = detection::categoryName(cat);
                std::transform(cn.begin(), cn.end(), cn.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
                ui.fonts.draw(ui.r, type::Overline, lv.x + 31.f, y, cn, t.textFaint);
                y += 22.f;
            }
            const Rect row{lv.x, y, lv.w, 58.f};
            const Id rid = hashId(c->id(), 4242);
            const bool sel = st.selectedCheck == c->index();
            bool h = false;
            const Rect toggleRect{row.r() - 50.f, row.cy() - 10.f, 36.f, 20.f};
            const bool overToggle = ui.hover(toggleRect.inset(-4.f));
            if (ui.pressable(rid, row, &h) && !overToggle) st.selectedCheck = c->index();
            const float hv = ui.anim(mixId(rid, 1), sel ? 1.f : (h ? 0.5f : 0.f), 16.f);
            if (hv > 0.01f) ui.r.roundRect(row.inset(4.f, 2.f), 11.f, (sel ? t.accentSoft() : t.surfaceHover).alpha(sel ? 1.f : hv * 1.6f));
            const bool on = c->enabled(liveConfig(ctx.engine));
            const Rect ic{row.x + 14.f, row.cy() - 16.f, 32.f, 32.f};
            ui.r.roundRect(ic, 9.f, (on ? t.accent : t.textFaint).withAlpha(t.light ? 0.10f : 0.13f));
            drawIcon(ui.r, checkIcon(c->id()), ic.x + 7.f, ic.y + 7.f, 18.f, on ? t.accent : t.textFaint);
            // Indicators with one meaning each (PLAN.md U1): a number badge = observations this
            // session (red only when the check also detected someone); a warning = a game signal
            // this check needs is missing, so it runs with reduced accuracy.
            const int ev = sessionEvidence(ctx.engine.session(), c->index());
            const int det = sessionDetections(ctx.engine.session(), c->index());
            float nameW = row.w - 120.f;
            if (ev > 0) {
                const std::string n = ev > 999 ? "999+" : std::to_string(ev);
                const float bw = std::max(22.f, ui.fonts.width(type::NumericCaption, n) + 12.f);
                const Rect badge{toggleRect.x - 10.f - bw, row.y + 11.f, bw, 18.f};
                const Color bc = det > 0 ? t.sevAlert : t.textMuted;
                ui.r.roundRect(badge, 9.f, bc.withAlpha(det > 0 ? 0.18f : 0.12f));
                ui.fonts.draw(ui.r, type::NumericCaption, badge.cx(), badge.cy() - ui.fonts.lineHeight(type::NumericCaption) * 0.5f, n,
                              det > 0 ? (t.light ? bc.darker(0.15f) : bc.lighter(0.1f)) : t.textMuted, Align::Center);
                ui.tooltip(mixId(rid, 5), badge,
                           std::to_string(ev) + (ev == 1 ? " observation" : " observations") + " this session" +
                               (det > 0 ? " \xC2\xB7 " + std::to_string(det) + (det == 1 ? " detection" : " detections") : std::string(" \xC2\xB7 no detections")));
                nameW = badge.x - (row.x + 56.f) - 8.f;
            }
            const std::string missing = missingFor(c->name());
            if (!missing.empty()) {
                const Rect wr{row.x + 36.f, row.y + 6.f, 16.f, 16.f};
                ui.r.circle(wr.cx(), wr.cy(), 8.f, t.surface);
                drawIcon(ui.r, Icon::Warning, wr.x + 1.f, wr.y + 1.f, 14.f, t.sevNotice, 1.7f);
                ui.tooltip(mixId(rid, 6), wr.inset(-3.f), "Missing game signal: " + missing + ". This check runs with reduced accuracy; see Status.");
            }
            ui.fonts.draw(ui.r, type::BodyStrong, row.x + 56.f, row.y + 11.f, c->name(), on ? t.text : t.textMuted, Align::Left, nameW);
            ui.fonts.draw(ui.r, type::Small, row.x + 56.f, row.y + 31.f, c->summary(), t.textFaint, Align::Left, row.w - 120.f);
            Setting* en = Settings::instance().find(c->settingsPrefix() + "enabled");
            if (en && toggle(ui, mixId(rid, 2), toggleRect, en->b())) en->set(en->b() ? 0.0 : 1.0);
            y += 60.f;
        }
    }
    endScroll(ui, lsid, lv, y - (lv.y + loff) + 8.f);

    // ── Detail ──
    detection::Check& c = *checks[static_cast<std::size_t>(st.selectedCheck)];
    const Id did = hashId("check-detail");
    // Reset scroll when switching checks.
    float& lastSel = ui.value(mixId(did, 1), -1.f);
    if (lastSel != static_cast<float>(st.selectedCheck)) { lastSel = static_cast<float>(st.selectedCheck); scrollTo(ui, did, 0.f); }
    const Rect dv{detail.x, detail.y, detail.w, detail.h};
    const float doff = beginScroll(ui, did, dv);
    float dy = dv.y + doff;
    const float w = detail.w - 10.f;

    // Header block.
    {
        const Rect hdr{detail.x, dy, w, 0.f};
        const auto aboutLines = ui.fonts.wrap(type::Body, c.about(), w - 44.f);
        const float hh = 104.f + aboutLines.size() * (ui.fonts.lineHeight(type::Body) + 4.f) + (c.requirements().empty() ? 0.f : 30.f);
        const Rect hc{hdr.x, hdr.y, w, hh};
        card(ui, hc);
        const Rect ic{hc.x + 22.f, hc.y + 22.f, 44.f, 44.f};
        ui.r.roundRect(ic, 12.f, t.accent.withAlpha(0.13f));
        if (t.glow > 0.f) ui.r.shadow(ic, 12.f, 18.f, t.accent.withAlpha(0.10f * t.glow), 0.f);
        drawIcon(ui.r, checkIcon(c.id()), ic.x + 10.f, ic.y + 10.f, 24.f, t.accent);
        ui.fonts.draw(ui.r, type::Heading, ic.r() + 16.f, hc.y + 20.f, c.name(), t.text);
        const std::string cat = detection::categoryName(c.category());
        chip(ui, {ic.r() + 16.f, hc.y + 54.f, chipWidth(ui, cat), 20.f}, cat, t.accent);
        const int ev = sessionEvidence(ctx.engine.session(), c.index());
        const int det = sessionDetections(ctx.engine.session(), c.index());
        const std::string counts = std::to_string(ev) + " observations \xC2\xB7 " + std::to_string(det) + " detections this session";
        ui.fonts.draw(ui.r, type::Small, ic.r() + 24.f + chipWidth(ui, cat), hc.y + 56.f, counts, t.textMuted);
        Setting* en = Settings::instance().find(c.settingsPrefix() + "enabled");
        if (en) {
            ui.fonts.draw(ui.r, type::SmallStrong, hc.r() - 72.f, hc.y + 26.f, en->b() ? "Enabled" : "Disabled",
                          en->b() ? t.text : t.textMuted, Align::Right);
            if (toggle(ui, hashId("detail-enable"), {hc.r() - 62.f, hc.y + 24.f, 40.f, 22.f}, en->b())) en->set(en->b() ? 0.0 : 1.0);
        }
        float ay = hc.y + 88.f;
        for (const auto& l : aboutLines) {
            ui.fonts.draw(ui.r, type::Body, hc.x + 22.f, ay, l, t.textMuted);
            ay += ui.fonts.lineHeight(type::Body) + 4.f;
        }
        // Signals the check depends on.
        const auto reqs = c.requirements();
        if (!reqs.empty()) {
            float rx = hc.x + 22.f;
            ui.fonts.draw(ui.r, type::Caption, rx, ay + 6.f, "Uses", t.textFaint);
            rx += ui.fonts.width(type::Caption, "Uses") + 10.f;
            for (const auto& r : reqs) {
                const float cw = chipWidth(ui, r);
                if (rx + cw > hc.r() - 20.f) break;
                chip(ui, {rx, ay + 4.f, cw, 19.f}, r, t.textMuted);
                rx += cw + 6.f;
            }
        }
        // Reset this check to its defaults.
        bool anyModified = false;
        for (Setting* s : Settings::instance().withPrefix(c.settingsPrefix())) if (!s->isDefault()) { anyModified = true; break; }
        if (anyModified) {
            float& armed = ui.value(hashId("check-reset-armed", c.index()), -100.f);
            const bool isArmed = ui.time - armed < 3.0;
            const char* lbl = isArmed ? "Confirm reset" : "Reset check";
            const Icon ri = Icon::Reset;
            const float bw = buttonWidth(ui, lbl, true);
            if (button(ui, hashId("check-reset", c.index()), {hc.r() - bw - 20.f, hc.y + 56.f, bw, 30.f}, lbl,
                       isArmed ? ButtonKind::Danger : ButtonKind::Ghost, &ri)) {
                if (isArmed) {
                    for (Setting* s : Settings::instance().withPrefix(c.settingsPrefix())) s->reset();
                    armed = -100.f;
                } else {
                    armed = static_cast<float>(ui.time);
                }
            }
        }
        dy += hh + 14.f;
    }

    // Sensitivity.
    {
        Setting* preset = detection::presetSetting(Settings::instance(), c);
        const Rect sc{detail.x, dy, w, 120.f};
        float cy = sectionCard(ui, sc, "SENSITIVITY", nullptr);
        int sel = std::min(preset->i(), 2);
        const bool custom = preset->i() == 3;
        std::vector<std::string> opts = {"Relaxed", "Balanced", "Strict"};
        if (segmented(ui, hashId("preset", c.index()), {sc.x + 22.f, cy, 330.f, 32.f}, opts, sel)) detection::applyPreset(Settings::instance(), c, sel);
        if (custom) chip(ui, {sc.x + 364.f, cy + 6.f, chipWidth(ui, "Custom"), 20.f}, "Custom", t.accent);
        const char* explain = custom ? "You have adjusted individual thresholds. Choose a preset to return to a tested combination."
                            : sel == 0 ? "Favors certainty: requires stronger and more frequent evidence before reporting."
                            : sel == 1 ? "The tested balance between reporting early and avoiding false positives."
                                       : "Reports earlier and on subtler evidence, with a higher risk of false positives.";
        ui.fonts.draw(ui.r, type::Small, sc.x + 22.f, cy + 44.f, explain, custom ? t.accent : t.textMuted, Align::Left, sc.w - 44.f);
        dy += sc.h + 14.f;
    }

    auto onEdited = [&c](Setting& s) {
        Setting* preset = detection::presetSetting(Settings::instance(), c);
        if (detection::isPresetControlled(c, s) && preset->i() != 3) preset->set(3);
    };
    SettingRowOptions rowOpt;
    rowOpt.onEdited = onEdited;

    // Indicators.
    {
        const auto& inds = c.indicators();
        const float rowH = 62.f;
        const Rect ic{detail.x, dy, w, 52.f + rowH * inds.size()};
        float iy = sectionCard(ui, ic, "INDICATORS", "independent evidence sources");
        const detection::PlayerRecord* dummy = nullptr;
        (void)dummy;
        std::vector<int> counts(inds.size(), 0);
        for (const auto& r : ctx.engine.session().players()) {
            const auto& cr = r.checks[static_cast<std::size_t>(c.index())];
            for (std::size_t i = 0; i < cr.indicatorCounts.size() && i < counts.size(); ++i) counts[i] += cr.indicatorCounts[i];
        }
        for (std::size_t i = 0; i < inds.size(); ++i) {
            const auto& ind = inds[i];
            // The indicator's two registry entries (on/off and weight).
            Setting* const enabledSetting = detection::settingFor(Settings::instance(), c, ind.enabled);
            Setting* const weightSetting = detection::settingFor(Settings::instance(), c, ind.weight);
            if (!enabledSetting || !weightSetting) continue;
            const Id iid = hashId(enabledSetting->key);
            const float rx = ic.x + 22.f;
            const bool on = enabledSetting->b();
            if (toggle(ui, iid, {rx, iy + 6.f, 36.f, 20.f}, on)) enabledSetting->set(on ? 0.0 : 1.0);
            ui.fonts.draw(ui.r, type::BodyStrong, rx + 50.f, iy + 4.f, ind.name, on ? t.text : t.textMuted);
            const float textW = ic.w - 50.f - 22.f - 300.f;
            ui.fonts.draw(ui.r, type::Small, rx + 50.f, iy + 24.f, ind.description, t.textFaint, Align::Left, textW);
            ui.tooltip(mixId(iid, 10), {rx + 50.f, iy + 22.f, textW, 18.f}, ind.description);
            // Weight.
            double wv = weightSetting->value;
            const float sx = ic.r() - 286.f;
            if (i == 0) ui.fonts.draw(ui.r, type::Overline, sx, iy - 20.f, "WEIGHT", t.textFaint);
            if (slider(ui, mixId(iid, 7), {sx, iy + 10.f, 120.f, 14.f}, wv, weightSetting->range.min, weightSetting->range.max,
                       weightSetting->range.step, weightSetting->def)) {
                weightSetting->set(wv);
            }
            ui.tooltip(mixId(iid, 8), {sx, iy + 2.f, 120.f, 30.f}, weightSetting->tooltip);
            double typed = weightSetting->value;
            if (numberBox(ui, mixId(iid, 13), mixId(iid, 7), {sx + 128.f, iy + 4.f, 60.f, 24.f}, typed, weightSetting->range.min,
                          weightSetting->range.max, weightSetting->range.step, formatNumber(weightSetting->value, 2) + "x",
                          !weightSetting->isDefault())) {
                weightSetting->set(typed);
            }
            // Session observations from this indicator.
            if (i == 0) ui.fonts.draw(ui.r, type::Overline, ic.r() - 22.f, iy - 20.f, "THIS SESSION", t.textFaint, Align::Right);
            const std::string cnt = std::to_string(counts[i]);
            const float cw = std::max(34.f, ui.fonts.width(type::SmallStrong, cnt) + 18.f);
            const Rect cr{ic.r() - 22.f - cw, iy + 5.f, cw, 22.f};
            ui.r.roundRect(cr, 11.f, counts[i] ? t.accentSoft() : t.inset);
            ui.fonts.draw(ui.r, type::SmallStrong, cr.cx(), cr.cy() - ui.fonts.lineHeight(type::SmallStrong) * 0.5f, cnt,
                          counts[i] ? t.accent : t.textFaint, Align::Center);
            if (enabledSetting->warningActive()) {
                drawIcon(ui.r, Icon::Warning, rx + 50.f + ui.fonts.width(type::BodyStrong, ind.name) + 8.f, iy + 4.f, 14.f, t.sevNotice, 1.5f);
                ui.tooltip(mixId(iid, 9), {rx + 50.f + ui.fonts.width(type::BodyStrong, ind.name) + 4.f, iy, 22.f, 22.f},
                           enabledSetting->warning);
            }
            if (i + 1 < inds.size()) divider(ui, ic.x + 22.f, ic.r() - 22.f, iy + rowH - 7.f);
            iy += rowH;
        }
        dy += ic.h + 14.f;
    }

    // Grouped settings (Detection, Confidence, Sampling, Debug).
    auto settings = Settings::instance().withPrefix(c.settingsPrefix());
    struct Group { const char* key; const char* title; const char* subtitle; };
    const Group groups[] = {
        {"Detection", "DETECTION", "thresholds"},
        {"Confidence", "CONFIDENCE", "how evidence becomes a detection"},
        {"Sampling", "SAMPLING", "optional performance mode"},
        {"Debug", "DEBUG", nullptr},
    };
    for (const Group& g : groups) {
        std::vector<Setting*> rows;
        bool hasAdvanced = false;
        for (Setting* s : settings) {
            if (s->group != g.key) continue;
            if (s->advanced) { hasAdvanced = true; if (!st.showAdvanced) continue; }
            rows.push_back(s);
        }
        if (rows.empty() && !hasAdvanced) continue;
        // Measure.
        float h = 52.f;
        for (Setting* s : rows) h += 54.f + (s->warningActive() ? 26.f : 0.f);
        if (hasAdvanced) h += 36.f;
        if (std::string(g.key) == "Debug" && c.debug(liveConfig(ctx.engine))) h += 150.f;
        const Rect gc{detail.x, dy, w, h};
        float gy = sectionCard(ui, gc, g.title, g.subtitle);
        for (Setting* s : rows) gy += settingRow(ui, *s, gc.x + 12.f, gy, gc.w - 34.f, rowOpt);
        if (hasAdvanced) {
            const std::string lbl = st.showAdvanced ? "Hide advanced" : "Show advanced";
            const Icon chev = st.showAdvanced ? Icon::ChevronDown : Icon::ChevronRight;
            if (button(ui, hashId("adv", hashId(g.key, c.index())), {gc.x + 16.f, gy + 2.f, buttonWidth(ui, lbl, true), 28.f}, lbl,
                       ButtonKind::Ghost, &chev)) {
                st.showAdvanced = !st.showAdvanced;
            }
            gy += 36.f;
        }
        if (std::string(g.key) == "Debug" && c.debug(liveConfig(ctx.engine))) {
            const Rect log{gc.x + 22.f, gy + 4.f, gc.w - 44.f, 136.f};
            ui.r.roundRect(log, 10.f, t.inset);
            float ly = log.y + 10.f;
            int shown = 0;
            const auto& dl = ctx.engine.debugLog();
            for (auto it = dl.rbegin(); it != dl.rend() && shown < 7; ++it) {
                if (it->check != c.index()) continue;
                ui.fonts.draw(ui.r, type::Mono, log.x + 12.f, ly, it->player + "  " + it->text, t.textMuted, Align::Left, log.w - 24.f);
                ly += 17.f;
                ++shown;
            }
            if (shown == 0) ui.fonts.draw(ui.r, type::Small, log.x + 12.f, ly, "Waiting for the next evaluation\xE2\x80\xA6", t.textFaint);
        }
        dy += h + 14.f;
    }
    scrollToFlash(ui, did, dv, dv.y + doff);
    endScroll(ui, did, dv, dy - (dv.y + doff) + 10.f);
}

}  // namespace vigil::ui
