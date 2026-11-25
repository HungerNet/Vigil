#include "pages.h"

#include <algorithm>
#include <cmath>

namespace vigil::ui {

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

void pageHeader(PageContext& ctx, const Rect& area, std::string_view title, std::string_view subtitle) {
    const Theme& t = ctx.ui.th();
    ctx.ui.fonts.draw(ctx.ui.r, type::Heading, area.x, area.y, title, t.text);
    if (!subtitle.empty()) ctx.ui.fonts.draw(ctx.ui.r, type::Body, area.x, area.y + 34.f, subtitle, t.textMuted, Align::Left, area.w - 80.f);
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

}  // namespace vigil::ui
