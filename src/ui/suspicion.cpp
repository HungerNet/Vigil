#include "suspicion.h"

#include "../identity/identity.h"
#include "motion.h"

#include <algorithm>
#include <cmath>

namespace vigil::ui {

Tier tierOf(float c) {
    if (c < 15.f) return Tier::Clear;
    if (c < 40.f) return Tier::Low;
    if (c < 65.f) return Tier::Elevated;
    if (c < 85.f) return Tier::High;
    return Tier::Critical;
}

const char* tierName(Tier t) {
    switch (t) {
        case Tier::Low: return "Low";
        case Tier::Elevated: return "Elevated";
        case Tier::High: return "High";
        case Tier::Critical: return "Critical";
        default: return "No concern";
    }
}

Color tierColor(const Theme& t, Tier tier) {
    switch (tier) {
        case Tier::Low: return t.severity(30.f);
        case Tier::Elevated: return t.severity(55.f);
        case Tier::High: return t.severity(76.f);
        case Tier::Critical: return t.severity(96.f);
        default: return t.textFaint;
    }
}

const char* tierGlyph(Tier t) {
    switch (t) {
        case Tier::Elevated: return "\xE2\x97\x87";   // ◇
        case Tier::High: return "\xE2\x97\x86";       // ◆
        case Tier::Critical: return "\xE2\x9A\xA0";   // ⚠
        default: return "";
    }
}

PlayerMarks marksOf(const detection::Engine& e, const std::string& key) {
    PlayerMarks m;
    m.watched = e.marks().watched(key);
    m.confirmed = e.marks().confirmed(key);
    identity::Profile ip;
    m.nicked = identity::Service::instance().find(key, ip) && ip.nicked;
    return m;
}

void playerHalo(Ui& ui, Host* host, Id id, const std::string& key, const std::string& name, const Rect& rc, const HaloStyle& s) {
    const Theme& t = ui.th();
    const float size = std::min(rc.w, rc.h);
    const float cx = rc.cx(), cy = rc.cy();
    const float v = ui.anim(id, s.confidence, motion::kValue);
    const Tier tier = tierOf(v);
    const int ti = static_cast<int>(tier);
    // Weight grows with the tier: the halo reads heavier as suspicion rises, without color.
    static const float kWeight[] = {0.f, 0.050f, 0.066f, 0.082f, 0.10f};
    const float trackW = std::max(1.f, size * 0.035f);
    const float arcW = tier == Tier::Clear ? trackW : std::clamp(size * kWeight[ti], 1.6f, 5.f);
    const float ringR = size * 0.5f - arcW * 0.5f - 0.75f;
    const float faceR = ringR - arcW * 0.5f - std::max(1.5f, size * 0.055f);
    const float alpha = s.present ? 1.f : 0.5f;
    const Color sev = t.severity(v);

    // Glow from High: a soft bloom behind the ring, stronger at Critical.
    if (tier >= Tier::High && t.glow > 0.f) {
        const float g = (tier == Tier::Critical ? 0.32f : 0.2f) * std::max(0.35f, t.glow) * alpha;
        ui.r.shadow({cx - ringR, cy - ringR, ringR * 2.f, ringR * 2.f}, ringR, size * 0.22f, sev.withAlpha(g), 0.f);
    }
    const float p = motion::pulse(ui.time, s.pulseAt);
    if (p > 0.01f) ui.r.ring(cx, cy, ringR + arcW * 0.5f + 1.5f + size * 0.12f * p, 1.5f, sev.withAlpha(0.55f * p));

    // Face.
    FaceTexture face;
    const bool hasFace = host && host->playerFace(key, face) && face.texture;
    ui.r.circle(cx, cy, faceR, t.light ? Color::hex(0xE4E8EF) : t.inset.lighter(0.05f));
    if (hasFace) {
        const Color tint(1.f, 1.f, 1.f, alpha);
        ui.r.imageDisc(face.texture, cx, cy, faceR, face.u0, face.v0, face.u1, face.v1, tint);
        ui.r.imageDisc(face.texture, cx, cy, faceR, face.hu0, face.hv0, face.hu1, face.hv1, tint);
    } else {
        std::string ini = name.empty() ? "?" : name.substr(0, 1);
        for (char& ch : ini) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        const TextStyle st = faceR * 2.f >= 40.f ? type::Title : faceR * 2.f >= 24.f ? type::BodyStrong : type::Caption;
        ui.fonts.draw(ui.r, st, cx, cy - ui.fonts.lineHeight(st) * 0.5f, ini, t.textMuted.alpha(alpha), Align::Center);
    }
    // Anti-aliased rim over the disc edge.
    ui.r.ring(cx, cy, faceR, std::max(1.f, size * 0.025f), (t.light ? Color(0, 0, 0, 0.12f) : Color(0, 0, 0, 0.35f)).alpha(alpha));

    // Track and confidence arc (12 o'clock, clockwise).
    ui.r.ring(cx, cy, ringR, trackW, (t.light ? Color(0, 0, 0, 0.09f) : Color(1, 1, 1, 0.09f)).alpha(alpha));
    if (v >= 1.f) ui.r.arc(cx, cy, ringR, arcW, 0.f, 6.2831853f * std::clamp(v / 100.f, 0.f, 1.f), sev.alpha(alpha), true);

    // The user's verdict (shield) or watch mark (ring) sits on the halo at 4:30.
    if (s.badge && (s.marks.confirmed || s.marks.watched)) {
        const float br = std::max(5.f, size * 0.17f);
        const float bx = cx + ringR * 0.7071f, by = cy + ringR * 0.7071f;
        const Color cut = s.cutout.a > 0.f ? s.cutout : t.surface;
        ui.r.circle(bx, by, br + std::max(1.5f, size * 0.035f), cut);
        if (s.marks.confirmed) {
            ui.r.circle(bx, by, br, t.verdict);
            drawIcon(ui.r, Icon::ShieldCheck, bx - br * 0.72f, by - br * 0.72f, br * 1.44f, t.textOnAccent, 1.8f);
        } else {
            ui.r.circle(bx, by, br, t.accent);
            ui.r.ring(bx, by, br * 0.42f, std::max(1.2f, br * 0.22f), t.textOnAccent);
        }
    }
}

float suspicionLabelWidth(Ui& ui, float c, bool percent) {
    const Tier tier = tierOf(c);
    float w = ui.fonts.width(type::SmallStrong, tierName(tier));
    if (percent && tier != Tier::Clear) w += ui.fonts.width(type::Numeric, " " + formatNumber(c, 0) + "%");
    return w;
}

float suspicionLabel(Ui& ui, float x, float y, float c, Align align, bool percent) {
    const Theme& t = ui.th();
    const Tier tier = tierOf(c);
    const float w = suspicionLabelWidth(ui, c, percent);
    float px = align == Align::Right ? x - w : align == Align::Center ? x - w * 0.5f : x;
    px += ui.fonts.draw(ui.r, type::SmallStrong, px, y, tierName(tier), tier == Tier::Clear ? t.textFaint : tierColor(t, tier));
    if (percent && tier != Tier::Clear) ui.fonts.draw(ui.r, type::Numeric, px, y, " " + formatNumber(c, 0) + "%", t.textMuted);
    return w;
}

float markChipWidth(Ui& ui, const PlayerMarks& m) {
    if (m.confirmed) return ui.fonts.width(type::Caption, "Confirmed by you") + 34.f;
    if (m.watched) return ui.fonts.width(type::Caption, "Watching") + 32.f;
    return 0.f;
}

float markChip(Ui& ui, float x, float y, const PlayerMarks& m) {
    const Theme& t = ui.th();
    const float w = markChipWidth(ui, m);
    if (w <= 0.f) return 0.f;
    const Rect rc{x, y, w, 20.f};
    const Color c = m.confirmed ? t.verdict : t.accent;
    ui.r.roundRect(rc, 10.f, c.withAlpha(t.light ? 0.14f : 0.16f));
    ui.r.roundRectStroke(rc, 10.f, 1.f, c.withAlpha(0.45f));
    const Color tc = t.light ? c.darker(0.2f) : c.lighter(0.15f);
    if (m.confirmed) drawIcon(ui.r, Icon::ShieldCheck, rc.x + 7.f, rc.y + 3.f, 14.f, tc, 1.6f);
    else {
        ui.r.ring(rc.x + 13.f, rc.cy(), 4.2f, 1.6f, tc);
        ui.r.circle(rc.x + 13.f, rc.cy(), 1.4f, tc);
    }
    ui.fonts.draw(ui.r, type::Caption, rc.x + 25.f, rc.cy() - ui.fonts.lineHeight(type::Caption) * 0.5f,
                  m.confirmed ? "Confirmed by you" : "Watching", tc);
    return w;
}

Briefing buildBriefing(const detection::Engine& e) {
    Briefing b;
    for (const auto& r : e.session().players()) {
        if (r.local || !e.inThisGame(r) || !r.participant) continue;
        ++b.players;
        BriefingEntry be;
        be.key = r.key;
        be.name = r.name;
        be.suspicion = r.suspicion;
        be.marks = marksOf(e, r.key);
        if (be.marks.confirmed) {
            be.priority = 0;
            be.reason = "Confirmed by you";
            if (const detection::Mark* m = e.marks().find(r.key)) {
                if (!m->reasons.empty()) be.reason += " \xC2\xB7 " + m->reasons.front();
            }
        } else if (be.marks.watched) {
            be.priority = 1;
            be.reason = "On your watchlist";
        } else if (r.priorDetections > 0) {
            be.priority = 2;
            be.reason = "Detected " + std::to_string(r.priorDetections) + (r.priorDetections == 1 ? " time" : " times") +
                        " in earlier sessions";
        } else if (r.peakSuspicion >= 65.f && r.scenesSeen > 1) {
            be.priority = 3;
            be.reason = "High suspicion in an earlier game";
        } else if (be.marks.nicked) {
            be.priority = 4;
            identity::Profile ip;
            identity::Service::instance().find(r.key, ip);
            be.reason = !ip.realName.empty() ? "Nicked \xC2\xB7 likely " + ip.realName
                      : ip.candidates.size() == 1 ? "Nicked \xC2\xB7 likely " + ip.candidates.front() : "Nicked";
        } else {
            continue;
        }
        b.entries.push_back(be);
    }
    std::stable_sort(b.entries.begin(), b.entries.end(), [](const BriefingEntry& a, const BriefingEntry& c) {
        if (a.priority != c.priority) return a.priority < c.priority;
        return a.suspicion > c.suspicion;
    });
    return b;
}

}  // namespace vigil::ui
