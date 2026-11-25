#include "pages.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace vigil::ui {

const char* pageTitle(Page p) {
    switch (p) {
        case Page::Overview: return "Overview";
        case Page::Players: return "Players";
        case Page::Checks: return "Checks";
        case Page::Utilities: return "Utilities";
        case Page::Settings: return "Settings";
        case Page::Status: return "Status";
    }
    return "";
}

const char* pageSection(Page p) {
    switch (p) {
        case Page::Overview: return "Monitor";
        case Page::Players: return "Investigate";
        case Page::Checks:
        case Page::Utilities:
        case Page::Settings: return "Configure";
        case Page::Status: return "System";
    }
    return "";
}

const char* pagePurpose(Page p) {
    switch (p) {
        case Page::Overview: return "What is happening right now.";
        case Page::Players: return "Everyone Vigil has observed. Open a profile to investigate.";
        case Page::Checks: return "What Vigil looks for and how much evidence it needs before it speaks.";
        case Page::Utilities: return "Optional Bed Wars helpers. Each one is off until you turn it on.";
        case Page::Settings: return "Alerts, scope, keybinds, appearance and data. Changes save automatically.";
        case Page::Status: return "Whether Vigil is running correctly, and what each part is doing.";
    }
    return "";
}

Icon pageIcon(Page p) {
    switch (p) {
        case Page::Overview: return Icon::Overview;
        case Page::Players: return Icon::Players;
        case Page::Checks: return Icon::Checks;
        case Page::Utilities: return Icon::Utilities;
        case Page::Settings: return Icon::Settings;
        case Page::Status: return Icon::Status;
    }
    return Icon::Dot;
}

Icon checkIcon(const std::string& id) {
    if (id == "killaura") return Icon::KillAura;
    if (id == "reach") return Icon::Reach;
    if (id == "autoclicker") return Icon::AutoClicker;
    if (id == "velocity") return Icon::Velocity;
    if (id == "autoblock") return Icon::AutoBlock;
    if (id == "speed") return Icon::Speed;
    if (id == "bhop") return Icon::BHop;
    if (id == "noslow") return Icon::NoSlow;
    if (id == "keepy") return Icon::KeepY;
    if (id == "flatscaff") return Icon::FlatScaffold;
    if (id == "legitscaff") return Icon::LegitScaff;
    return Icon::Checks;
}

Icon categoryIcon(detection::CheckCategory c) {
    switch (c) {
        case detection::CheckCategory::Combat: return Icon::Combat;
        case detection::CheckCategory::Movement: return Icon::Movement;
        default: return Icon::World;
    }
}

std::string agoLabel(double nowMs, double thenMs) {
    const double s = std::max(0.0, (nowMs - thenMs) / 1000.0);
    if (s < 5) return "now";
    if (s < 60) return std::to_string(static_cast<int>(s)) + "s ago";
    if (s < 3600) return std::to_string(static_cast<int>(s / 60)) + "m ago";
    return std::to_string(static_cast<int>(s / 3600)) + "h ago";
}

std::string epochAgoLabel(double epochMs) {
    using namespace std::chrono;
    const double now = static_cast<double>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    return agoLabel(now, epochMs);
}

float pageHeader(PageContext& ctx, const Rect& area, std::string_view title, std::string_view subtitle) {
    const Theme& t = ctx.ui.th();
    // Title and purpose share one line: the title names the place, the purpose says what it is for.
    const float tw = ctx.ui.fonts.draw(ctx.ui.r, type::Heading, area.x, area.y, title, t.text);
    if (!subtitle.empty()) {
        const float baseline = area.y + ctx.ui.fonts.lineHeight(type::Heading) - ctx.ui.fonts.lineHeight(type::Body) - 3.f;
        ctx.ui.fonts.draw(ctx.ui.r, type::Body, area.x + tw + 16.f, baseline, subtitle, t.textMuted, Align::Left, area.w - tw - 30.f);
    }
    return area.y + 46.f;
}

std::vector<const detection::PlayerRecord*> sortedPlayers(const detection::Session& s, int sortMode, const std::string& filter,
                                                          bool includeLocal) {
    std::vector<const detection::PlayerRecord*> out;
    std::string f = filter;
    std::transform(f.begin(), f.end(), f.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& r : s.players()) {
        if (r.local && !includeLocal) continue;
        if (!f.empty()) {
            std::string n = r.name;
            std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (n.find(f) == std::string::npos) continue;
        }
        out.push_back(&r);
    }
    std::stable_sort(out.begin(), out.end(), [sortMode](const detection::PlayerRecord* a, const detection::PlayerRecord* b) {
        if (sortMode == 1) return a->lastSeenMs > b->lastSeenMs;
        if (sortMode == 2) return a->name < b->name;
        const float sa = std::max(a->suspicion, a->peakSuspicion * 0.35f) + (a->evidence.empty() ? 0.f : 0.5f);
        const float sb = std::max(b->suspicion, b->peakSuspicion * 0.35f) + (b->evidence.empty() ? 0.f : 0.5f);
        if (std::fabs(sa - sb) > 0.01f) return sa > sb;
        if (a->present != b->present) return a->present;
        return a->name < b->name;
    });
    return out;
}

void sparkline(Ui& ui, const Rect& rc, const std::vector<detection::ConfidencePoint>& pts, double t0, double t1, Color c, bool fill) {
    if (pts.empty() || t1 <= t0) return;
    std::vector<Point> line;
    float lastV = 0.f;
    bool started = false;
    for (const auto& p : pts) {
        if (p.timeMs < t0) { lastV = p.value; started = true; continue; }
        if (p.timeMs > t1) break;
        if (line.empty() && started) line.push_back({rc.x, rc.b() - rc.h * lastV / 100.f});
        const float x = rc.x + static_cast<float>((p.timeMs - t0) / (t1 - t0)) * rc.w;
        line.push_back({x, rc.b() - rc.h * std::clamp(p.value, 0.f, 100.f) / 100.f});
        lastV = p.value;
    }
    if (line.empty()) {
        if (!started) return;
        line.push_back({rc.x, rc.b() - rc.h * lastV / 100.f});
    }
    line.push_back({rc.r(), line.back().y});
    if (fill) ui.r.areaUnder(line, rc.b(), c.withAlpha(0.22f * c.a), c.withAlpha(0.f));
    ui.r.polyline(line, 1.6f, c);
}

float confidenceTrend(const std::vector<detection::ConfidencePoint>& history, float current, double nowMs, double windowMs) {
    if (history.empty()) return 0.f;
    const double t0 = nowMs - windowMs;
    float before = history.front().timeMs <= t0 ? history.front().value : 0.f;
    for (const auto& p : history) {
        if (p.timeMs > t0) break;
        before = p.value;
    }
    return current - before;
}

float trendLabel(Ui& ui, float x, float y, float delta, bool alignRight) {
    if (std::fabs(delta) < 1.f) return 0.f;
    const Theme& t = ui.th();
    const bool up = delta > 0.f;
    const Color c = up ? t.sevAlert : t.sevClear;
    const std::string txt = (up ? "+" : "\xE2\x88\x92") + formatNumber(std::fabs(delta), 0);
    const float tw = ui.fonts.width(type::Caption, txt);
    const float w = tw + 18.f;
    const float x0 = alignRight ? x - w : x;
    drawIcon(ui.r, up ? Icon::TrendUp : Icon::TrendDown, x0, y + 0.5f, 13.f, c, 1.6f);
    ui.fonts.draw(ui.r, type::Caption, x0 + 16.f, y, txt, c);
    return w;
}

void playerAvatarByKey(Ui& ui, Host* host, const std::string& key, const std::string& name, float suspicion, const Rect& rc) {
    const Theme& t = ui.th();
    FaceTexture face;
    if (host && host->playerFace(key, face) && face.texture) {
        const float rad = std::max(3.f, rc.w * 0.18f);
        ui.r.shadow(rc, rad, 6.f, Color(0, 0, 0, t.light ? 0.10f : 0.30f), 1.5f);
        ui.r.roundRect(rc, rad, t.inset);
        const Rect img = rc.inset(rc.w * 0.06f);
        ui.r.image(face.texture, img, face.u0, face.v0, face.u1, face.v1);
        ui.r.image(face.texture, img.inset(-img.w * 0.04f), face.hu0, face.hv0, face.hu1, face.hv1);
        ui.r.roundRectStroke(rc, rad, 1.f, t.borderStrong);
        return;
    }
    const Color sev = t.severity(suspicion);
    ui.r.roundRect(rc, rc.w * 0.3f, sev.withAlpha(0.16f));
    std::string ini = name.empty() ? "?" : name.substr(0, 1);
    for (char& ch : ini) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    const TextStyle st = rc.w >= 40.f ? type::Title : type::BodyStrong;
    ui.fonts.draw(ui.r, st, rc.cx(), rc.cy() - ui.fonts.lineHeight(st) * 0.5f, ini, sev, Align::Center);
}

void playerAvatar(PageContext& ctx, const detection::PlayerRecord& p, const Rect& rc, bool ring) {
    playerAvatarByKey(ctx.ui, ctx.host, p.key, p.name, p.suspicion, rc);
    if (ring && p.suspicion >= 1.f) {
        const Theme& t = ctx.ui.th();
        const float rad = std::max(3.f, rc.w * 0.18f);
        ctx.ui.r.roundRectStroke(rc.inset(-3.f), rad + 3.f, 2.f, t.severity(p.suspicion).withAlpha(0.35f + 0.6f * p.suspicion / 100.f));
    }
}

std::string keyLabel(const std::string& settingKey) {
    const Setting* s = Settings::instance().find(settingKey);
    return s ? keyName(s->i()) : std::string("None");
}

// ── Health ───────────────────────────────────────────────────────────────────────────────────

Health computeHealth(const PageContext& ctx) {
    Health h;
    const auto& e = ctx.engine;
    RuntimeStatus rs;
    if (ctx.host) rs = ctx.host->runtimeStatus();
    if (rs.hosted) {
        if (!rs.presentHook) ++h.issues;
        if (!rs.inputHook) ++h.issues;
        if (!rs.clientPump) ++h.issues;
        if (rs.samplerResolved) for (const auto& s : rs.signals) if (!s.available) ++h.issues;
    }
    if (!e.monitoringEnabled()) {
        h.level = Health::Paused;
        h.title = "Paused";
        h.detail = "Monitoring is turned off";
    } else if (h.issues > 0) {
        h.level = Health::Degraded;
        h.title = "Needs attention";
        h.detail = std::to_string(h.issues) + (h.issues == 1 ? " issue" : " issues") + " \xC2\xB7 open Status";
    } else if (e.active()) {
        h.level = Health::Active;
        h.title = "Monitoring";
        h.detail = e.contextDetail().empty() ? detection::gameContextName(e.context()) : e.contextDetail();
    } else {
        h.level = Health::Standby;
        h.title = "Standing by";
        if (ctx.host && !ctx.host->connectedToHypixel()) h.detail = "Not on Hypixel";
        else h.detail = std::string(detection::gameContextName(e.context())) + " \xC2\xB7 waiting for a game";
    }
    return h;
}

Color healthColor(const Theme& t, Health::Level l) {
    switch (l) {
        case Health::Active: return t.sevClear;
        case Health::Standby: return t.accent;
        case Health::Degraded: return t.sevNotice;
        case Health::Paused: return t.textFaint;
    }
    return t.textFaint;
}

}  // namespace vigil::ui
