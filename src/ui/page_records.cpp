// Records — the user's durable records about players (PLAN.md §3.4):
//   Confirmed   players you concluded are cheating (with reasons, a note, Vigil's confidence then)
//   Watching    players announced by the match briefing when they are in your game
//   Exempt      players Vigil lists but never judges (friends, party members)
//   Saved       investigations frozen at the moment you saved them; export and import
// Anyone can be added: a player in this session, or any username (bound to the real account the
// first time Vigil sees it).

#include "../identity/identity.h"
#include "pages.h"
#include "records.h"
#include "sound.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>   // virtual-key codes (list navigation)

#include <algorithm>
#include <cmath>

namespace vigil::ui {
namespace {

using detection::Mark;
using detection::MarkKind;

const char* kTabs[] = {"Confirmed", "Watching", "Exempt", "Saved"};

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

MarkKind kindOfTab(int tab) { return tab == 0 ? MarkKind::Confirm : tab == 1 ? MarkKind::Watch : MarkKind::Exempt; }

// ── Picker result hand-off (the picker draws in an overlay, after the page) ─────────────────
struct Pick {
    bool ready = false;
    std::string name, key;
} g_pick;

// Last result banner of a Records action (save, export, import, add, remove).
void actionBannerImpl(PageContext& ctx, const Rect& rc) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    const RecordsService& rs = RecordsService::instance();
    const double age = ui.time - rs.lastResultAt();
    if (age < 0.0 || age > 6.0 || rs.lastResult().message.empty()) return;
    const float a = static_cast<float>(std::min(1.0, std::min(age / 0.15, (6.0 - age) / 0.4)));
    const ActionResult& r = rs.lastResult();
    const float w = std::min(rc.w, ui.fonts.width(type::Small, r.message) + 64.f);
    const Rect b{rc.x + (rc.w - w) * 0.5f, rc.b() - 48.f, w, 38.f};
    ui.r.pushAlpha(a);
    ui.r.shadow(b, 12.f, 18.f, Color(0, 0, 0, t.light ? 0.12f : 0.4f), 4.f);
    ui.r.roundRect(b, 12.f, t.surfaceRaised);
    ui.r.roundRectStroke(b, 12.f, 1.f, (r.ok ? t.sevClear : t.sevNotice).withAlpha(0.5f));
    drawIcon(ui.r, r.ok ? Icon::Check : Icon::Warning, b.x + 14.f, b.cy() - 8.f, 16.f, r.ok ? t.sevClear : t.sevNotice, 1.8f);
    ui.fonts.draw(ui.r, type::Small, b.x + 40.f, b.cy() - ui.fonts.lineHeight(type::Small) * 0.5f, r.message, t.text, Align::Left, b.w - 52.f);
    ui.r.popAlpha();
}

// Note field that saves when you leave it (not on every keystroke).
void noteField(PageContext& ctx, const Rect& rc, const std::string& owner, const std::string& stored,
               const std::function<void(const std::string&)>& save) {
    Ui& ui = ctx.ui;
    static std::string s_owner, s_buffer;
    if (s_owner != owner) { s_owner = owner; s_buffer = stored; }
    const Id id = hashId("note-field", hashId(owner));
    textField(ui, id, rc, s_buffer, "Add a note (only you see it)", Icon::Edit, false, 300);
    if (ui.focus != id && s_buffer != stored) save(s_buffer);
}

// ── List ────────────────────────────────────────────────────────────────────────────────────

struct Row {
    std::string key;         // mark key or snapshot id
    std::string face;        // player key for the face texture
    std::string name;
    std::string sub;
    PlayerMarks marks;
    float confidence = 0.f;
    bool present = false;
};

std::vector<Row> rowsFor(PageContext& ctx, int tab, const std::string& query) {
    std::vector<Row> rows;
    const auto& engine = ctx.engine;
    if (tab < 3) {
        for (const Mark& m : engine.marks().search(query, static_cast<int>(kindOfTab(tab)))) {
            Row r;
            r.key = m.key;
            r.face = m.key;
            r.name = m.name.empty() ? m.key.substr(0, 8) : m.name;
            r.marks.confirmed = m.confirmed;
            r.marks.watched = m.watch || m.confirmed;
            r.marks.exempt = m.exempt;
            const detection::PlayerRecord* rec = engine.session().find(m.key);
            if (rec) {
                r.confidence = rec->suspicion;
                r.present = rec->present;
            }
            if (m.confirmed) {
                r.sub = "Confirmed " + epochAgoLabel(m.confirmedAtEpochMs);
                if (!m.reasons.empty()) r.sub += " \xC2\xB7 " + m.reasons.front();
            } else if (m.exempt) {
                r.sub = "Exempt since " + epochAgoLabel(m.exemptAtEpochMs > 0.0 ? m.exemptAtEpochMs : m.updatedEpochMs);
            } else {
                r.sub = "Watching since " + epochAgoLabel(m.addedEpochMs > 0.0 ? m.addedEpochMs : m.updatedEpochMs);
            }
            if (m.addedByName()) r.sub += " \xC2\xB7 not seen yet";
            else if (rec && engine.inThisGame(*rec)) r.sub += " \xC2\xB7 in this game";
            rows.push_back(std::move(r));
        }
    } else {
        const std::string q = lower(query);
        for (const auto& s : RecordsService::instance().store().list()) {
            if (!q.empty() && lower(s.name).find(q) == std::string::npos && lower(s.note).find(q) == std::string::npos) continue;
            Row r;
            r.key = s.id;
            r.face = s.uuid.empty() ? s.id : s.uuid;
            r.name = s.name;
            r.confidence = s.suspicion;
            r.marks.confirmed = s.confirmed;
            r.sub = (s.origin == "imported" ? "Imported " : "Saved ") + epochAgoLabel(s.savedAtEpochMs);
            if (s.detections > 0) r.sub += " \xC2\xB7 " + std::to_string(s.detections) + (s.detections == 1 ? " detection" : " detections");
            else if (s.peakSuspicion >= 15.f) r.sub += " \xC2\xB7 peak " + formatNumber(s.peakSuspicion, 0) + "%";
            rows.push_back(std::move(r));
        }
    }
    return rows;
}

void recordList(PageContext& ctx, const Rect& rc) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    auto& st = ctx.state;
    auto& marks = ctx.engine.marks();
    card(ui, rc);
    textField(ui, hashId("records-search"), {rc.x + 14.f, rc.y + 14.f, rc.w - 28.f, 34.f}, st.recordsSearch, "Search names, notes, reasons");
    const Rect segR{rc.x + 14.f, rc.y + 56.f, rc.w - 28.f, 28.f};
    const int before = st.recordsTab;
    segmented(ui, hashId("records-tabs"), segR, {kTabs[0], kTabs[1], kTabs[2], kTabs[3]}, st.recordsTab);
    if (before != st.recordsTab) { st.selectedRecord.clear(); st.addOpen = false; }
    const int counts[] = {marks.confirmedCount(), marks.watchedCount() - marks.confirmedCount(), marks.exemptCount(),
                          static_cast<int>(RecordsService::instance().store().list().size())};
    static const char* kTips[] = {"Players you confirmed as cheaters. Your conclusion; Vigil's confidence is separate.",
                                  "Players announced by the match briefing when they are in your game.",
                                  "Players Vigil lists but never judges: friends, party members.",
                                  "Investigations saved from a profile, or imported from a file."};
    for (int i = 0; i < 4; ++i) {
        ui.tooltip(hashId("records-tab-tip", static_cast<Id>(i)), {segR.x + segR.w / 4.f * i, segR.y, segR.w / 4.f, segR.h},
                   std::string(kTips[i]) + " (" + std::to_string(counts[i]) + ")");
    }

    // Actions: add someone (marks) or import (saved).
    const float ay = rc.y + 94.f;
    Rect addAnchor{rc.x + 14.f, ay, 0.f, 30.f};
    if (st.recordsTab < 3) {
        const Icon ic = Icon::UserPlus;
        const char* lbl = st.recordsTab == 0 ? "Confirm a player" : st.recordsTab == 1 ? "Watch a player" : "Exempt a player";
        addAnchor.w = buttonWidth(ui, lbl, true);
        if (button(ui, hashId("records-add"), addAnchor, lbl, ButtonKind::Secondary, &ic)) {
            st.addOpen = !st.addOpen;
            st.addQuery.clear();
            if (st.addOpen) { ui.focus = hashId("picker-field"); ui.kbFocus = ui.focus; }
        }
    } else {
        const Icon ic = Icon::Import;
        const float bw = buttonWidth(ui, "Import", true);
        if (button(ui, hashId("records-import"), {rc.x + 14.f, ay, bw, 30.f}, "Import", ButtonKind::Secondary, &ic)) {
            RecordsService& rs = RecordsService::instance();
            const ActionResult r = rs.import(ctx.engine, ctx.host);
            rs.announce(r, ui.time);
            if (r.ok && !r.id.empty()) st.selectedRecord = r.id;
        }
        ui.tooltip(hashId("records-import-tip"), {rc.x + 14.f, ay, bw, 30.f},
                   "Imports a profile file copied in Explorer (Ctrl+C), a copied path, or files in the imports folder.");
        if (ctx.host && iconButton(ui, hashId("records-exports"), {rc.x + 22.f + bw, ay, 30.f, 30.f}, Icon::Folder, "Open the exports folder")) {
            ctx.host->openFolder(RecordsService::instance().store().exportsDir());
        }
    }
    ui.fonts.draw(ui.r, type::NumericCaption, rc.r() - 18.f, ay + 8.f, std::to_string(counts[st.recordsTab]), t.textFaint, Align::Right);

    // Picker for "add": suggestions from this session, or any username.
    if (st.recordsTab < 3 && st.addOpen) {
        std::string name, key;
        if (playerPicker(ctx, addAnchor, st.addQuery, name, key)) {
            const MarkKind kind = kindOfTab(st.recordsTab);
            const detection::PlayerRecord* rec = key.empty() ? nullptr : ctx.engine.session().find(key);
            std::string used;
            if (kind == MarkKind::Confirm && rec) {
                marks.confirm(key, name, {}, rec->suspicion);
                used = key;
            } else {
                used = marks.addByName(name, kind, key);
            }
            st.addOpen = false;
            if (!used.empty()) {
                st.selectedRecord = used;
                playSound(kind == MarkKind::Watch ? Sound::Saved : Sound::Confirm);
                RecordsService::instance().announce({true, (kind == MarkKind::Confirm ? "Confirmed " : kind == MarkKind::Exempt ? "Exempted " : "Watching ") + name + ".", {}, {}},
                                                    ui.time);
            }
        }
    }

    const auto rows = rowsFor(ctx, st.recordsTab, st.recordsSearch);
    const Rect view{rc.x + 6.f, rc.y + 134.f, rc.w - 12.f, rc.h - 140.f};
    if (rows.empty()) {
        static const char* kTitles[] = {"No confirmations", "Nobody watched", "No exemptions", "No saved profiles"};
        static const char* kBodies[] = {
            "Confirm a player from their profile, or with the button above, by name. Vigil never confirms anyone on its own.",
            "Watch players to have the match briefing announce them when they are in your game.",
            "Exempt friends and party members: they stay listed but are never judged.",
            "Save a profile from its More menu to keep the investigation, or import one someone sent you."};
        const bool searching = !st.recordsSearch.empty();
        emptyState(ui, view, st.recordsTab == 3 ? Icon::Save : st.recordsTab == 2 ? Icon::Exempt : Icon::Records,
                   searching ? "No matches" : kTitles[st.recordsTab], searching ? "Try another name, note or reason." : kBodies[st.recordsTab]);
        return;
    }
    if (st.selectedRecord.empty() || std::none_of(rows.begin(), rows.end(), [&](const Row& r) { return r.key == st.selectedRecord; })) {
        st.selectedRecord = rows.front().key;
    }
    const Id sid = hashId("records-list", static_cast<Id>(st.recordsTab));
    const float off = beginScroll(ui, sid, view);
    float y = view.y + off + 2.f;
    std::vector<std::pair<std::string, float>> rowTops;
    for (const Row& r : rows) {
        const Rect row{view.x, y, view.w, 56.f};
        rowTops.push_back({r.key, y - (view.y + off)});
        if (row.b() >= view.y && row.y <= view.b()) {
            const Id rid = hashId(r.key, 7301);
            const bool sel = st.selectedRecord == r.key;
            bool h = false;
            if (ui.pressable(rid, row, &h)) st.selectedRecord = r.key;
            const float hv = ui.anim(mixId(rid, 1), sel ? 1.f : (h ? 0.5f : 0.f), motion::kHover);
            if (hv > 0.01f) ui.r.roundRect(row.inset(4.f, 2.f), 10.f, (sel ? t.accentSoft() : t.surfaceHover).alpha(sel ? 1.f : hv * 1.6f));
            HaloStyle hs;
            hs.confidence = r.confidence;
            hs.marks = r.marks;
            hs.present = st.recordsTab == 3 || r.present || r.confidence > 0.f;
            hs.cutout = sel ? t.surface.mix(t.accent, 0.12f) : t.surface;
            playerHalo(ui, ctx.host, mixId(rid, 2), r.face, r.name, {row.x + 10.f, row.cy() - 17.f, 34.f, 34.f}, hs);
            ui.fonts.draw(ui.r, type::BodyStrong, row.x + 54.f, row.y + 9.f, r.name, t.text, Align::Left, row.w - 70.f);
            ui.fonts.draw(ui.r, type::Caption, row.x + 54.f, row.y + 29.f, r.sub, t.textFaint, Align::Left, row.w - 70.f);
        }
        y += 58.f;
    }
    endScroll(ui, sid, view, y - (view.y + off) + 6.f);
    // ↑/↓ through the list.
    bool rowFocused = ui.kbFocus == 0;
    for (const auto& rt : rowTops) rowFocused = rowFocused || ui.kbFocus == hashId(rt.first, 7301);
    if (!ui.focus && !ui.modal && rowFocused) {
        const int dir = ui.consumeKey(VK_DOWN) ? 1 : ui.consumeKey(VK_UP) ? -1 : 0;
        if (dir != 0) {
            int idx = 0;
            for (std::size_t i = 0; i < rowTops.size(); ++i) if (rowTops[i].first == st.selectedRecord) idx = static_cast<int>(i);
            const int next = std::clamp(idx + dir, 0, static_cast<int>(rowTops.size()) - 1);
            st.selectedRecord = rowTops[static_cast<std::size_t>(next)].first;
            ui.setKeyboardFocus(hashId(st.selectedRecord, 7301));
            ScrollState& ss = scrollState(ui, sid);
            const float top = rowTops[static_cast<std::size_t>(next)].second;
            if (top < ss.target) ss.target = std::max(0.f, top - 4.f);
            else if (top + 58.f > ss.target + view.h) ss.target = top + 58.f - view.h + 4.f;
        }
    }
}

// ── Mark detail ─────────────────────────────────────────────────────────────────────────────

void markDetail(PageContext& ctx, const Mark& m, const Rect& area) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    auto& marks = ctx.engine.marks();
    const detection::PlayerRecord* rec = ctx.engine.session().find(m.key);
    const Id sid = hashId("record-detail", hashId(m.key));
    const float off = beginScroll(ui, sid, area);
    float y = area.y + off;
    const float w = area.w - 8.f;

    // Header: who, your decisions, and the actions on them.
    {
        const Rect c{area.x, y, w, 100.f};
        card(ui, c);
        HaloStyle hs;
        hs.confidence = rec ? rec->suspicion : 0.f;
        hs.marks.confirmed = m.confirmed;
        hs.marks.watched = m.watch || m.confirmed;
        hs.marks.exempt = m.exempt;
        hs.present = rec && rec->present;
        playerHalo(ui, ctx.host, hashId("rec-halo", hashId(m.key)), m.key, m.name, {c.x + 18.f, c.cy() - 30.f, 60.f, 60.f}, hs);
        const float nx = c.x + 94.f;
        // Four 34 px actions sit at the right of the name row; the name and chip share the rest.
        const float nameRoom = c.r() - 16.f - 4.f * 40.f - 12.f - nx;
        const float chipW = markChipWidth(ui, hs.marks);
        const float nw = ui.fonts.draw(ui.r, type::Heading, nx, c.y + 18.f, m.name, t.text, Align::Left, std::max(80.f, nameRoom - chipW - 12.f));
        if (nx + nw + 12.f + chipW <= nx + nameRoom) markChip(ui, nx + nw + 12.f, c.y + 18.f + ui.fonts.ascent(type::Heading) - 15.f, hs.marks);
        std::string sub;
        if (m.addedByName()) sub = "Added by name \xC2\xB7 not seen yet";
        else if (rec && ctx.engine.inThisGame(*rec)) sub = std::string(rec->present ? "In this game" : "Left this game") + " \xC2\xB7 " + tierName(tierOf(rec->suspicion));
        else if (rec) sub = "Earlier this session";
        else sub = "Not seen this session";
        if (!m.uuid.empty()) sub += " \xC2\xB7 " + m.uuid.substr(0, 8);
        ui.fonts.draw(ui.r, type::Small, nx, c.y + 58.f, sub, t.textMuted, Align::Left, c.w - nx + c.x - 200.f);

        // Actions (right): Open profile · Watch · Confirm · Exempt · Remove.
        float ax = c.r() - 16.f;
        const float ay = c.y + 16.f;
        float& armed = ui.value(hashId("rec-remove-armed", hashId(m.key)), -100.f);
        const bool isArmed = ui.time - armed < 3.0;
        ax -= 34.f;
        if (iconButton(ui, hashId("rec-remove", hashId(m.key)), {ax, ay, 34.f, 34.f}, isArmed ? Icon::Check : Icon::Trash,
                       isArmed ? "Click again to remove every mark on this player" : "Remove from Records", isArmed)) {
            if (isArmed) { marks.remove(m.key); armed = -100.f; ctx.state.selectedRecord.clear(); }
            else armed = static_cast<float>(ui.time);
        }
        ax -= 40.f;
        if (iconButton(ui, hashId("rec-exempt", hashId(m.key)), {ax, ay, 34.f, 34.f}, Icon::Exempt,
                       m.exempt ? "Remove exemption: Vigil judges this player again" : "Exempt: listed, never judged (clears a confirmation)", m.exempt)) {
            marks.setExempt(m.key, m.name, !m.exempt);
            if (!m.exempt) playSound(Sound::Confirm);
        }
        ax -= 40.f;
        if (iconButton(ui, hashId("rec-confirm", hashId(m.key)), {ax, ay, 34.f, 34.f}, m.confirmed ? Icon::ShieldCheck : Icon::Shield,
                       m.confirmed ? "Remove confirmation (keeps watching)" : "Confirm as a cheater (your conclusion)", m.confirmed)) {
            if (m.confirmed) marks.unconfirm(m.key);
            else {
                marks.confirm(m.key, m.name, {}, rec ? rec->suspicion : -1.f);
                playSound(Sound::Confirm);
            }
        }
        ax -= 40.f;
        if (iconButton(ui, hashId("rec-watch", hashId(m.key)), {ax, ay, 34.f, 34.f}, Icon::Pin,
                       m.confirmed ? "Confirmed players are always watched" : m.watch ? "Stop watching" : "Watch", m.watch || m.confirmed) &&
            !m.confirmed) {
            marks.setWatch(m.key, m.name, !m.watch);
        }
        if (rec) {
            // Second row, right: the live profile.
            const Icon ic = Icon::Players;
            const float bw = buttonWidth(ui, "Open profile", true);
            if (button(ui, hashId("rec-open", hashId(m.key)), {c.r() - 16.f - bw, c.y + 54.f, bw, 32.f}, "Open profile", ButtonKind::Secondary, &ic)) {
                ctx.openPlayer(m.key);
            }
        }
        y += c.h + 12.f;
    }
    // The mark may have just been removed by an action above.
    const Mark* cur = marks.find(m.key);
    if (!cur) { endScroll(ui, sid, area, y - (area.y + off)); return; }

    // Your decision.
    {
        const float hh = cur->confirmed ? 226.f : 150.f;
        const Rect c{area.x, y, w, hh};
        card(ui, c);
        const float x = c.x + 22.f, iw = c.w - 44.f;
        float cy = c.y + 18.f;
        if (cur->confirmed) {
            sectionHeader(ui, x, cy, "YOUR CONCLUSION", "never set by Vigil, never read by a check");
            cy += 28.f;
            std::string when = "Confirmed " + epochAgoLabel(cur->confirmedAtEpochMs);
            if (cur->origin == "name") when += " by name";
            const float ww = ui.fonts.draw(ui.r, type::Body, x, cy, when, t.text);
            if (cur->confidenceAtConfirm >= 0.f) {
                ui.fonts.draw(ui.r, type::Small, x + ww + 12.f, cy + 1.f, "\xC2\xB7 Vigil's confidence then:", t.textMuted);
                suspicionLabel(ui, x + ww + 20.f + ui.fonts.width(type::Small, "\xC2\xB7 Vigil's confidence then:"), cy + 1.f, cur->confidenceAtConfirm);
            } else {
                ui.fonts.draw(ui.r, type::Small, x + ww + 12.f, cy + 1.f, "\xC2\xB7 not observed by Vigil at the time", t.textFaint);
            }
            cy += 30.f;
            ui.fonts.draw(ui.r, type::Overline, x, cy, "REASONS", t.textFaint);
            cy += 18.f;
            std::vector<std::string> options;
            for (const auto& chk : ctx.engine.checks()) options.push_back(chk->name());
            options.push_back("Other");
            float rx = x;
            for (std::size_t i = 0; i < options.size(); ++i) {
                const float cw = chipWidth(ui, options[i]) + 4.f;
                if (rx + cw > x + iw) { rx = x; cy += 30.f; }
                const Rect cr{rx, cy, cw, 24.f};
                const bool on = std::find(cur->reasons.begin(), cur->reasons.end(), options[i]) != cur->reasons.end();
                bool ch = false;
                if (ui.pressable(hashId("rec-reason", hashId(m.key + options[i])), cr, &ch)) {
                    std::vector<std::string> next = cur->reasons;
                    if (on) next.erase(std::remove(next.begin(), next.end(), options[i]), next.end());
                    else next.push_back(options[i]);
                    marks.setReasons(m.key, next);
                }
                ui.r.roundRect(cr, 12.f, on ? t.verdict.withAlpha(0.22f) : (ch ? t.surfaceHover : t.inset));
                ui.r.roundRectStroke(cr, 12.f, 1.f, on ? t.verdict.withAlpha(0.7f) : t.border);
                ui.fonts.draw(ui.r, type::Caption, cr.cx(), cr.cy() - ui.fonts.lineHeight(type::Caption) * 0.5f, options[i], on ? t.text : t.textMuted,
                              Align::Center);
                rx += cw + 6.f;
            }
            cy += 36.f;
        } else if (cur->exempt) {
            sectionHeader(ui, x, cy, "EXEMPTION", "your decision");
            cy += 28.f;
            cy += ui.fonts.drawWrapped(ui.r, type::Body, x, cy, iw,
                                       "Exempt " + epochAgoLabel(cur->exemptAtEpochMs > 0.0 ? cur->exemptAtEpochMs : cur->updatedEpochMs) +
                                           ". Listed, but never judged: no evidence, detections, alerts, briefing or highlight.",
                                       t.text, 3.f) + 12.f;
        } else {
            sectionHeader(ui, x, cy, "WATCHING", "your decision");
            cy += 28.f;
            cy += ui.fonts.drawWrapped(ui.r, type::Body, x, cy, iw,
                                       "Watching since " + epochAgoLabel(cur->addedEpochMs > 0.0 ? cur->addedEpochMs : cur->updatedEpochMs) +
                                           ". The match briefing names this player whenever they are in your game.",
                                       t.text, 3.f) + 12.f;
        }
        const std::string key = m.key;
        detection::Marks* mk = &marks;
        noteField(ctx, {x, cy, iw, 34.f}, key, cur->note, [mk, key](const std::string& note) { mk->setNote(key, note); });
        y += hh + 12.f;
    }

    // Vigil's view: this session, earlier sessions, saved profiles of this player.
    {
        std::vector<const detection::SnapshotSummary*> saved;
        for (const auto& s : RecordsService::instance().store().list()) {
            if ((!m.uuid.empty() && s.uuid == m.uuid) || lower(s.name) == lower(m.name)) saved.push_back(&s);
        }
        const float hh = 112.f + std::min<std::size_t>(saved.size(), 4) * 40.f;
        const Rect c{area.x, y, w, hh};
        card(ui, c);
        const float x = c.x + 22.f, iw = c.w - 44.f;
        float cy = c.y + 18.f;
        sectionHeader(ui, x, cy, "VIGIL'S VIEW", "separate from your marks");
        cy += 28.f;
        if (rec) {
            float lx = x + suspicionLabel(ui, x, cy, rec->suspicion) + 10.f;
            std::string what = concernLabel(ctx.engine, *rec);
            if (!rec->detections.empty()) what += (what.empty() ? "" : " \xC2\xB7 ") + std::to_string(rec->detections.size()) + " detections this session";
            if (rec->priorSessions > 0) what += (what.empty() ? "" : " \xC2\xB7 ") + std::to_string(rec->priorSessions) + " earlier sessions";
            ui.fonts.draw(ui.r, type::Small, lx, cy, what.empty() ? "Nothing unusual observed" : what, t.textMuted, Align::Left, iw - (lx - x));
        } else {
            ui.fonts.draw(ui.r, type::Small, x, cy, m.addedByName() ? "Vigil has not observed this player." : "Not observed this session.",
                          t.textFaint);
        }
        cy += 30.f;
        if (saved.empty()) {
            ui.fonts.draw(ui.r, type::Caption, x, cy, "No saved profiles of this player. Save one from their profile's More menu.", t.textFaint,
                          Align::Left, iw);
        } else {
            ui.fonts.draw(ui.r, type::Overline, x, cy, "SAVED PROFILES", t.textFaint);
            cy += 18.f;
            for (std::size_t i = 0; i < saved.size() && i < 4; ++i) {
                const Rect row{x - 8.f, cy, iw + 16.f, 36.f};
                bool h = false;
                if (ui.pressable(hashId("rec-saved", hashId(saved[i]->id)), row, &h)) ctx.openRecord(saved[i]->id, 3);
                if (h) ui.r.roundRect(row, 8.f, t.surfaceHover);
                drawIcon(ui.r, Icon::Save, row.x + 10.f, row.cy() - 8.f, 16.f, t.textMuted, 1.6f);
                ui.fonts.draw(ui.r, type::SmallStrong, row.x + 34.f, row.y + 9.f, "Saved " + epochAgoLabel(saved[i]->savedAtEpochMs), t.text);
                suspicionLabel(ui, row.r() - 10.f, row.y + 9.f, saved[i]->peakSuspicion, Align::Right);
                cy += 40.f;
            }
        }
        y += hh + 12.f;
    }
    endScroll(ui, sid, area, y - (area.y + off));
}

// ── Saved profile ───────────────────────────────────────────────────────────────────────────

void snapshotDetail(PageContext& ctx, const std::string& id, const Rect& area) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    RecordsService& rs = RecordsService::instance();
    std::string error;
    const detection::ProfileSnapshot* s = rs.loaded(ctx.engine, id, error);
    if (!s) {
        card(ui, area);
        emptyState(ui, area, Icon::Warning, "This profile can't be opened", error.empty() ? "The file may have been moved or deleted." : error);
        return;
    }
    const detection::PlayerRecord& p = s->record;
    // Header.
    const Rect c{area.x, area.y, area.w, 100.f};
    card(ui, c);
    HaloStyle hs;
    hs.confidence = p.suspicion;
    hs.marks.confirmed = s->confirmed;
    hs.marks.watched = s->watched;
    hs.marks.exempt = s->exempt;
    playerHalo(ui, ctx.host, hashId("snap-halo", hashId(id)), p.key, p.name, {c.x + 18.f, c.cy() - 30.f, 60.f, 60.f}, hs);
    const float nx = c.x + 94.f;
    ui.fonts.draw(ui.r, type::Heading, nx, c.y + 14.f, p.name, t.text, Align::Left, c.w * 0.35f);
    std::string sub = (s->origin == "imported" ? "Imported \xC2\xB7 saved " : "Saved ") + epochAgoLabel(s->savedAtEpochMs);
    if (s->nicked) sub += " \xC2\xB7 nicked" + (s->likelyAccount.empty() ? std::string() : ", likely " + s->likelyAccount);
    float sx = nx + suspicionLabel(ui, nx, c.y + 50.f, p.suspicion) + 8.f;
    ui.fonts.draw(ui.r, type::Small, sx, c.y + 50.f, "\xC2\xB7 " + sub, t.textMuted, Align::Left, c.r() - 220.f - sx);
    ui.tooltip(hashId("snap-sub", hashId(id)), {nx, c.y + 46.f, c.r() - 220.f - nx, 22.f},
               "A saved profile is frozen at the moment it was saved: confidence, evidence and detections as Vigil saw them "
               "then. It never changes. Saved by Vigil " + (s->vigilVersion.empty() ? std::string("?") : s->vigilVersion) + ".");
    // Actions.
    float ax = c.r() - 16.f;
    const float ay = c.y + 16.f;
    float& armed = ui.value(hashId("snap-del-armed", hashId(id)), -100.f);
    const bool isArmed = ui.time - armed < 3.0;
    ax -= 34.f;
    if (iconButton(ui, hashId("snap-del", hashId(id)), {ax, ay, 34.f, 34.f}, isArmed ? Icon::Check : Icon::Trash,
                   isArmed ? "Click again to delete this saved profile" : "Delete", isArmed)) {
        if (isArmed) {
            rs.announce(rs.remove(id), ui.time);
            ctx.state.selectedRecord.clear();
            return;
        }
        armed = static_cast<float>(ui.time);
    }
    ax -= 40.f;
    if (iconButton(ui, hashId("snap-export", hashId(id)), {ax, ay, 34.f, 34.f}, Icon::Export, "Export a shareable copy to the exports folder")) {
        rs.announce(rs.exportSaved(id), ui.time);
    }
    if (const detection::PlayerRecord* live = ctx.engine.session().find(p.key)) {
        const Icon ic = Icon::Players;
        const float bw = buttonWidth(ui, "Live profile", true);
        ax -= bw + 10.f;
        if (button(ui, hashId("snap-live", hashId(id)), {ax, ay, bw, 34.f}, "Live profile", ButtonKind::Secondary, &ic)) ctx.openPlayer(live->key);
    }
    const std::string sid = id;
    const detection::Engine* eng = &ctx.engine;
    noteField(ctx, {nx, c.y + 72.f - 4.f, std::max(160.f, ax - nx - 16.f), 26.f}, "snap:" + id, s->note,
              [eng, sid](const std::string& note) { RecordsService::instance().setNote(*eng, sid, note); });

    float y = c.b() + 10.f;
    if (!s->droppedChecks.empty()) {
        std::string d = "Saved with checks this version doesn't have; their observations are not shown:";
        for (const auto& x : s->droppedChecks) d += " " + x;
        ui.fonts.draw(ui.r, type::Caption, area.x + 6.f, y, d, t.sevNotice, Align::Left, area.w - 12.f);
        y += 20.f;
    }
    // The same investigation surface as a live profile, read-only.
    static const std::vector<std::string> tabLabels = {"Summary", "Evidence", "Behavior"};
    int tab = ctx.state.snapshotTab == ProfileTab::Evidence ? 1 : ctx.state.snapshotTab == ProfileTab::Behavior ? 2 : 0;
    tabs(ui, hashId("snap-tabs"), area.x + 6.f, y + 6.f, tabLabels, tab);
    ctx.state.snapshotTab = tab == 1 ? ProfileTab::Evidence : tab == 2 ? ProfileTab::Behavior : ProfileTab::Summary;
    divider(ui, area.x, area.r(), y + 34.f);
    const Rect content{area.x, y + 46.f, area.w, area.b() - y - 46.f};
    PageContext sctx = ctx;
    sctx.nowMs = s->capturedAtMs > 0.0 ? s->capturedAtMs : p.lastSeenMs;
    sctx.sessionStartOverride = s->sessionStartMs;
    sctx.snapshot = true;
    switch (ctx.state.snapshotTab) {
        case ProfileTab::Evidence: drawEvidenceTab(sctx, p, content); break;
        case ProfileTab::Behavior: drawBehaviorTab(sctx, p, content); break;
        default: drawSummaryTab(sctx, p, content); break;
    }
}

}  // namespace

void actionBanner(PageContext& ctx, const Rect& rc) { actionBannerImpl(ctx, rc); }

// ── Player picker (shared) ──────────────────────────────────────────────────────────────────

bool playerPicker(PageContext& ctx, const Rect& anchor, std::string& query, std::string& name, std::string& key) {
    Ui& ui = ctx.ui;
    const Id pid = hashId("player-picker");
    if (g_pick.ready) {
        g_pick.ready = false;
        name = g_pick.name;
        key = g_pick.key;
        ui.popup = 0;
        ui.focus = 0;
        return true;
    }
    // Closed elsewhere (Esc closes the innermost popup; a click outside).
    float& shown = ui.value(mixId(pid, 9), 0.f);
    if (shown > 0.5f && ui.popup != pid) {
        shown = 0.f;
        ctx.state.addOpen = false;
        return false;
    }
    ui.popup = pid;
    shown = 1.f;
    // Suggestions: players from this session (newest first), then "add by name".
    struct Suggestion { std::string name, key, sub; float confidence; bool present; };
    std::vector<Suggestion> list;
    const std::string q = lower(query);
    for (const detection::PlayerRecord* r : sortedPlayers(ctx.engine, 1, query, false)) {
        if (list.size() >= 6) break;
        list.push_back({r->name, r->key, presenceLabel(ctx, *r), r->suspicion, r->present});
    }
    bool exact = false;
    for (const auto& s : list) exact = exact || lower(s.name) == q;
    const bool canAddName = detection::isValidPlayerName(query) && !exact;
    const float itemH = 44.f, w = 320.f;
    const int count = static_cast<int>(list.size()) + (canAddName ? 1 : 0);
    const float h = 56.f + std::max(1, count) * itemH + 8.f;
    const Rect pop{anchor.x + ui.r.offsetX(), anchor.b() + 6.f + ui.r.offsetY(), w, h};
    ui.popupRect = pop;
    if (ui.in.pressed[0] && !pop.contains(ui.in.mx, ui.in.my) && !ui.hoverRaw(anchor)) {
        ctx.state.addOpen = false;
        ui.popup = 0;
        return false;
    }
    Host* host = ctx.host;
    std::string* queryPtr = &query;
    const float openT = ui.anim(mixId(pid, 1), 1.f, 3.f / motion::kPopover);
    ui.addOverlay([&ui, pid, pop, list, canAddName, queryPtr, host, openT, itemH]() {
        const Theme& t = ui.th();
        ui.r.pushClip({0, 0, ui.r.width(), ui.r.height()});
        ui.r.pushAlpha(openT);
        const Rect b = pop.offset(0, (1.f - openT) * -motion::travel(motion::kPopoverTravel));
        ui.r.shadow(b, 12.f, 24.f, Color(0, 0, 0, t.light ? 0.14f : 0.45f), 8.f);
        ui.r.roundRect(b, 12.f, t.surfaceRaised);
        ui.r.roundRectStroke(b, 12.f, 1.f, t.borderStrong);
        const Id fid = hashId("picker-field");
        // Enter picks the first suggestion (or adds the typed name).
        if (ui.focus == fid && ui.consumeKey(VK_RETURN)) {
            if (!list.empty()) g_pick = {true, list.front().name, list.front().key};
            else if (canAddName) g_pick = {true, *queryPtr, {}};
        }
        textField(ui, fid, {b.x + 10.f, b.y + 10.f, b.w - 20.f, 34.f}, *queryPtr, "Name of any player");
        float y = b.y + 52.f;
        if (list.empty() && !canAddName) {
            ui.fonts.draw(ui.r, type::Small, b.x + 18.f, y + 12.f, queryPtr->empty() ? "Type a name, or pick someone from this session."
                                                                                     : "Not a valid Minecraft name.", t.textFaint);
        }
        for (std::size_t i = 0; i < list.size(); ++i) {
            const Rect row{b.x + 5.f, y, b.w - 10.f, itemH};
            bool h = false;
            if (ui.pressable(mixId(pid, 100 + i), row, &h)) g_pick = {true, list[i].name, list[i].key};
            if (h) ui.r.roundRect(row, 9.f, t.surfaceHover);
            HaloStyle hs;
            hs.confidence = list[i].confidence;
            hs.present = list[i].present;
            hs.badge = false;
            playerHalo(ui, host, mixId(pid, 200 + i), list[i].key, list[i].name, {row.x + 8.f, row.cy() - 14.f, 28.f, 28.f}, hs);
            ui.fonts.draw(ui.r, type::BodyStrong, row.x + 46.f, row.y + 5.f, list[i].name, t.text, Align::Left, row.w - 56.f);
            ui.fonts.draw(ui.r, type::Caption, row.x + 46.f, row.y + 24.f, list[i].sub, t.textFaint, Align::Left, row.w - 56.f);
            y += itemH;
        }
        if (canAddName) {
            const Rect row{b.x + 5.f, y, b.w - 10.f, itemH};
            bool h = false;
            if (ui.pressable(mixId(pid, 99), row, &h)) g_pick = {true, *queryPtr, {}};
            if (h) ui.r.roundRect(row, 9.f, t.surfaceHover);
            drawIcon(ui.r, Icon::UserPlus, row.x + 13.f, row.cy() - 9.f, 18.f, t.accent, 1.6f);
            ui.fonts.draw(ui.r, type::BodyStrong, row.x + 46.f, row.y + 5.f, "Add \"" + *queryPtr + "\" by name", t.text, Align::Left, row.w - 56.f);
            ui.fonts.draw(ui.r, type::Caption, row.x + 46.f, row.y + 24.f, "Follows their account once Vigil sees them", t.textFaint, Align::Left,
                          row.w - 56.f);
        }
        ui.r.popAlpha();
        ui.r.popClip();
    });
    return false;
}

void drawRecords(PageContext& ctx, const Rect& area) {
    Ui& ui = ctx.ui;
    RecordsService& rs = RecordsService::instance();
    rs.refreshIfStale(ctx.engine, ui.time);
    const float top = pageHeader(ctx, area, "Records", pagePurpose(Page::Records));
    const Rect list{area.x, top, 330.f, area.b() - top};
    recordList(ctx, list);
    const Rect main{list.r() + 14.f, top, area.r() - list.r() - 14.f, area.b() - top};
    if (ctx.state.selectedRecord.empty()) {
        card(ui, main);
        emptyState(ui, main, Icon::Records, "Your records",
                   "Everything here is yours and stays on this computer: who you confirmed, watch or exempted, and the "
                   "profiles you saved. Vigil never adds to it on its own.");
    } else if (ctx.state.recordsTab == 3) {
        snapshotDetail(ctx, ctx.state.selectedRecord, main);
    } else if (const Mark* m = ctx.engine.marks().find(ctx.state.selectedRecord)) {
        const Mark copy = *m;   // actions may change the store while drawing
        markDetail(ctx, copy, main);
    } else {
        card(ui, main);
        emptyState(ui, main, Icon::Records, "Select a record", "Choose someone on the left, or add a player.");
    }
    actionBanner(ctx, main);
}

}  // namespace vigil::ui
