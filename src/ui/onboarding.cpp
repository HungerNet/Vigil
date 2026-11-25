// First-run introduction — five short cards over the menu, skippable at every step and replayable
// from Settings → About or search. Each card teaches one concept with a small live illustration.

#include "pages.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>

namespace vigil::ui {
namespace {

constexpr int kSteps = 5;

void finish(PageContext& ctx) {
    ctx.state.onboardingOpen = false;
    if (Setting* s = Settings::instance().find("ui.onboarded")) s->set(1.0);
}

void goTo(PageContext& ctx, int step) {
    step = std::clamp(step, 0, kSteps - 1);
    if (step == ctx.state.onboardingStep) return;
    ctx.state.onboardingStep = step;
    ctx.state.onboardingStepAt = ctx.ui.time;
}

// ── Illustrations ────────────────────────────────────────────────────────────────────────────

void illoWelcome(Ui& ui, const Rect& rc, double t0) {
    const Theme& t = ui.th();
    const float k = ease::outCubic(motion::progress(ui.time, t0, 0.5f));
    const float s = 84.f;
    const float x = rc.cx() - s * 0.5f, y = rc.cy() - s * 0.5f;
    ui.r.circle(rc.cx(), rc.cy() - 6.f, 70.f * k, t.accent.withAlpha(0.07f));
    ui.r.pushAlpha(k);
    drawLogo(ui.r, x, y, s, t.text, Color(0, 0, 0, 0));
    ui.r.popAlpha();
    const float flame = ease::outBack(motion::progress(ui.time, t0 + 0.3, 0.4f), 2.f);
    const Point d = logoDotCenter();
    const float breathe = 1.f + 0.06f * std::sin(static_cast<float>(ui.time) * 2.2f) * (motionLevel() > 0.99f ? 1.f : 0.f);
    ui.r.circle(x + d.x * s, y + d.y * s, 13.f * flame * breathe, t.accent.withAlpha(0.18f));
    ui.r.circle(x + d.x * s, y + d.y * s, logoDotRadius() * s * flame, t.accent);
}

void illoPipeline(Ui& ui, const Rect& rc, double t0) {
    const Theme& t = ui.th();
    struct Node { Icon icon; const char* label; };
    const Node nodes[] = {{Icon::Eye, "Observe"}, {Icon::Checks, "Check"}, {Icon::Evidence, "Evidence"}, {Icon::Bolt, "Detection"}};
    const float gap = (rc.w - 4 * 64.f) / 3.f;
    for (int i = 0; i < 4; ++i) {
        const float k = ease::outCubic(motion::progress(ui.time, t0 + 0.08 * i, 0.3f));
        const float x = rc.x + i * (64.f + gap);
        const Rect box{x, rc.cy() - 40.f + (1.f - k) * 8.f, 64.f, 64.f};
        ui.r.pushAlpha(k);
        const Color c = i == 3 ? t.sevAlert : t.accent;
        ui.r.roundRect(box, 16.f, c.withAlpha(0.13f));
        drawIcon(ui.r, nodes[i].icon, box.x + 20.f, box.y + 20.f, 24.f, c);
        ui.fonts.draw(ui.r, type::SmallStrong, box.cx(), box.b() + 10.f, nodes[i].label, t.text, Align::Center);
        ui.r.popAlpha();
        if (i < 3) {
            const float lk = ease::outCubic(motion::progress(ui.time, t0 + 0.08 * i + 0.2, 0.3f));
            const float x0 = box.r() + 8.f, x1 = box.r() + gap - 8.f;
            ui.r.line(x0, box.cy(), x0 + (x1 - x0) * lk, box.cy(), 1.5f, t.textFaint);
            // A dot travels along the arrow continuously: evidence flowing.
            if (motionLevel() > 0.01f) {
                const float f = static_cast<float>(std::fmod(ui.time * 0.7 + i * 0.33, 1.0));
                ui.r.circle(x0 + (x1 - x0) * f, box.cy(), 2.5f, t.accent.alpha(lk));
            }
        }
    }
}

void illoConfidence(Ui& ui, const Rect& rc, double t0) {
    const Theme& t = ui.th();
    // A 6-second loop: three observations raise confidence, then it decays.
    const float period = 6.f;
    const float tt = static_cast<float>(std::fmod(ui.time - t0, static_cast<double>(period)));
    float v = 0.f;
    const float hits[3] = {0.6f, 1.4f, 2.1f};
    for (float h : hits) if (tt >= h) v += (100.f - v) * 0.42f;
    const float lastHit = 2.1f;
    if (tt > lastHit + 0.6f) v *= std::pow(0.5f, (tt - lastHit - 0.6f) / 1.2f);
    const float cx = rc.x + 90.f, cy = rc.cy() - 4.f;
    confidenceRing(ui, hashId("onb-ring"), cx, cy, 46.f, v, 7.f, true);
    for (int i = 0; i < 3; ++i) {
        const float age = tt - hits[i];
        if (age >= 0.f && age < 0.6f) ui.r.ring(cx, cy, 46.f + 18.f * age, 2.f, t.accent.withAlpha(0.5f * (1.f - age / 0.6f)));
    }
    // Threshold explanation beside the ring.
    const float x = rc.x + 180.f;
    const float y0 = rc.y + 18.f;
    auto line = [&](float y, Color c, const char* head, const char* body) {
        ui.r.circle(x + 5.f, y + 8.f, 4.f, c);
        ui.fonts.draw(ui.r, type::SmallStrong, x + 18.f, y, head, t.text);
        ui.fonts.draw(ui.r, type::Caption, x + 18.f, y + 18.f, body, t.textMuted, Align::Left, rc.r() - x - 20.f);
    };
    line(y0, t.accent, "Evidence adds", "Each observation adds a share of the remaining headroom.");
    line(y0 + 46.f, t.sevClear, "Time fades it", "Without new evidence confidence halves on a fixed schedule.");
    line(y0 + 92.f, t.sevAlert, "A gate guards alerts", "Alerts need high confidence and several observations.");
}

void illoProfile(Ui& ui, const Rect& rc, double t0) {
    const Theme& t = ui.th();
    const Rect panel{rc.x + 30.f, rc.y + 6.f, rc.w - 60.f, rc.h - 12.f};
    ui.r.roundRect(panel, 12.f, t.surface);
    ui.r.roundRectStroke(panel, 12.f, 1.f, t.border);
    ui.r.roundRect({panel.x + 16.f, panel.y + 14.f, 30.f, 30.f}, 7.f, t.chart[4].withAlpha(0.4f));
    ui.r.roundRect({panel.x + 56.f, panel.y + 18.f, 90.f, 8.f}, 4.f, t.text);
    ui.r.roundRect({panel.x + 56.f, panel.y + 32.f, 140.f, 6.f}, 3.f, t.textFaint);
    const char* tabsL[] = {"Summary", "Evidence", "Behavior", "Identity"};
    const int active = static_cast<int>(std::fmod((ui.time - t0) / 1.4, 4.0));
    float tx = panel.x + 16.f;
    for (int i = 0; i < 4; ++i) {
        const float w = ui.fonts.draw(ui.r, type::SmallStrong, tx, panel.y + 56.f, tabsL[i], i == active ? t.text : t.textFaint);
        if (i == active) ui.r.roundRect({tx, panel.y + 76.f, w, 2.f}, 1.f, t.accent);
        tx += w + 20.f;
    }
    // Timeline lanes with markers.
    const Rect plot{panel.x + 16.f, panel.y + 92.f, panel.w - 32.f, panel.h - 106.f};
    std::vector<Point> pts;
    for (int i = 0; i <= 30; ++i) {
        const float f = i / 30.f;
        const float v = 0.15f + 0.6f * std::max(0.f, std::sin(f * 3.4f - 0.6f)) * (f > 0.25f ? 1.f : 0.f);
        pts.push_back({plot.x + plot.w * f, plot.b() - plot.h * v});
    }
    ui.r.areaUnder(pts, plot.b(), t.sevAlert.withAlpha(0.18f), t.sevAlert.withAlpha(0.f));
    ui.r.polyline(pts, 1.6f, t.sevAlert);
    for (float f : {0.35f, 0.5f, 0.62f}) ui.r.diamond(plot.x + plot.w * f, plot.y + 4.f, 4.f, t.sevAlert);
}

// ── Steps ────────────────────────────────────────────────────────────────────────────────────

struct Step {
    const char* over;
    const char* title;
    const char* body;
};

const Step kStepText[kSteps] = {
    {"WELCOME", "Vigil keeps watch",
     "Vigil observes the players around you and measures their behavior against what vanilla Minecraft allows. It never "
     "guesses: everything it reports is backed by numbers you can inspect."},
    {"HOW DETECTIONS WORK", "From observation to detection",
     "Every game tick, eleven checks study each player in view. When behavior departs from vanilla, the check records "
     "evidence: what was measured, the limit it broke, and why that matters."},
    {"CONFIDENCE", "Confidence is earned, and it fades",
     "Evidence builds a confidence score for each check. One odd moment is never enough; a detection needs sustained, "
     "independent evidence, and confidence settles back when behavior returns to normal."},
    {"INVESTIGATIONS", "Every player has a profile",
     "Open a player to see how their confidence evolved, the evidence behind it, how they fight and move, and who they "
     "might be when nicked. Detection notifications lead straight there."},
    {"MAKE IT YOURS", "Two things worth knowing",
     "Choose the key that opens Vigil, and remember search: press Ctrl K anywhere to find any player, check or setting."},
};

}  // namespace

void drawOnboarding(PageContext& ctx, const Rect& win) {
    Ui& ui = ctx.ui;
    const Theme& t = ui.th();
    auto& st = ctx.state;
    const float in = ease::emphasized(motion::progress(ui.time, st.onboardingStepAt - (st.onboardingStep == 0 ? 0.0 : 10.0), motion::kModalIn));
    ui.r.roundRect(win, t.radiusWindow, Color(0, 0, 0, (t.light ? 0.22f : 0.45f) * in));

    // Keyboard.
    if (!ui.captureKey) {
        if (ui.consumeKey(VK_RIGHT) || ui.consumeKey(VK_RETURN)) {
            if (st.onboardingStep == kSteps - 1) { finish(ctx); return; }
            goTo(ctx, st.onboardingStep + 1);
        }
        if (ui.consumeKey(VK_LEFT)) goTo(ctx, st.onboardingStep - 1);
    }

    const float w = std::min(640.f, win.w - 80.f), h = 470.f;
    const Rect panel{win.cx() - w * 0.5f, win.cy() - h * 0.5f + (1.f - in) * motion::travel(motion::kModalTravel), w, h};
    ui.r.pushAlpha(in);
    ui.r.shadow(panel, 20.f, 50.f, Color(0, 0, 0, t.light ? 0.22f : 0.6f), 18.f);
    ui.r.roundRectGradient(panel, 20.f, t.windowTop.lighter(t.light ? 0.f : 0.02f), t.windowBottom);
    ui.r.roundRectStroke(panel, 20.f, 1.f, t.borderStrong);

    // Step content slides in the direction of travel.
    const int step = st.onboardingStep;
    const float k = ease::emphasized(motion::progress(ui.time, st.onboardingStepAt, 0.24f));
    const Rect illo{panel.x + 40.f, panel.y + 34.f, panel.w - 80.f, 170.f};
    ui.r.pushAlpha(k);
    ui.r.pushOffset((1.f - k) * motion::travel(16.f), 0.f);
    switch (step) {
        case 0: illoWelcome(ui, illo, st.onboardingStepAt); break;
        case 1: illoPipeline(ui, illo, st.onboardingStepAt); break;
        case 2: illoConfidence(ui, illo, st.onboardingStepAt); break;
        case 3: illoProfile(ui, illo, st.onboardingStepAt); break;
        default: break;
    }
    const Step& s = kStepText[step];
    float ty = panel.y + (step == 4 ? 40.f : 226.f);
    ui.fonts.draw(ui.r, type::Overline, panel.x + 40.f, ty, s.over, t.accent);
    ui.fonts.draw(ui.r, type::Heading, panel.x + 40.f, ty + 18.f, s.title, t.text);
    ty += 58.f;
    ty += ui.fonts.drawWrapped(ui.r, type::Body, panel.x + 40.f, ty, panel.w - 80.f, s.body, t.textMuted, 5.f);
    if (step == 4) {
        // Menu key binding, inline.
        ty += 22.f;
        if (Setting* mk = Settings::instance().find("ui.menuKey")) {
            ui.fonts.draw(ui.r, type::BodyStrong, panel.x + 40.f, ty + 7.f, "Open Vigil with", t.text);
            int vk = mk->i();
            if (keybind(ui, hashId("onb-key"), {panel.x + 200.f, ty, 160.f, 32.f}, vk)) mk->set(vk);
            if (ui.captureKey == hashId("onb-key")) {
                ui.fonts.draw(ui.r, type::Caption, panel.x + 372.f, ty + 9.f, "Press a key \xC2\xB7 Esc cancels", t.accent);
            }
        }
        ty += 50.f;
        ui.fonts.draw(ui.r, type::BodyStrong, panel.x + 40.f, ty + 4.f, "Search anything", t.text);
        float kx = panel.x + 200.f;
        for (const char* kk : {"Ctrl", "K"}) {
            const float kw = keycapWidth(ui, kk);
            keycap(ui, {kx, ty + 2.f, kw, 22.f}, kk);
            kx += kw + 6.f;
        }
        ui.fonts.draw(ui.r, type::Small, kx + 6.f, ty + 4.f, "or  /", t.textMuted);
        ty += 46.f;
        ui.fonts.draw(ui.r, type::BodyStrong, panel.x + 40.f, ty + 4.f, "Theme", t.text);
        Setting* th = Settings::instance().find("ui.theme");
        float sx = panel.x + 200.f;
        const auto& all = themes();
        for (std::size_t i = 0; i < all.size(); ++i) {
            const Rect sw{sx, ty - 2.f, 30.f, 30.f};
            const Id id = hashId(all[i].name, 7171);
            bool hh = false;
            if (ui.pressable(id, sw, &hh) && th) th->set(static_cast<double>(i));
            const bool sel = th && th->i() == static_cast<int>(i);
            ui.r.circle(sw.cx(), sw.cy(), 12.f, all[i].windowTop);
            ui.r.circle(sw.cx(), sw.cy(), 6.f, all[i].accent);
            ui.r.ring(sw.cx(), sw.cy(), 12.f, sel ? 2.f : 1.f, sel ? t.accent : t.borderStrong);
            ui.tooltip(mixId(id, 1), sw, all[i].name);
            sx += 36.f;
        }
    }
    ui.r.popOffset();
    ui.r.popAlpha();

    // Footer: progress dots, skip, back/next.
    const float fy = panel.b() - 58.f;
    divider(ui, panel.x + 24.f, panel.r() - 24.f, fy - 12.f);
    for (int i = 0; i < kSteps; ++i) {
        const float active = ui.anim(hashId("onb-dot", static_cast<Id>(i)), i == step ? 1.f : 0.f, motion::kSelect);
        const float dw = 7.f + 14.f * active;
        const float dx = panel.x + 40.f + i * 16.f + (i > step ? 14.f : 0.f);
        ui.r.roundRect({dx, fy + 14.f, dw, 7.f}, 3.5f, t.textFaint.mix(t.accent, active));
    }
    const bool last = step == kSteps - 1;
    const char* nextL = last ? "Start using Vigil" : "Next";
    const Icon arrow = Icon::ArrowRight;
    const float nw = buttonWidth(ui, nextL, !last);
    if (button(ui, hashId("onb-next"), {panel.r() - 40.f - nw, fy + 2.f, nw, 34.f}, nextL, ButtonKind::Primary, last ? nullptr : &arrow)) {
        if (last) finish(ctx); else goTo(ctx, step + 1);
    }
    if (step > 0) {
        const float bw = buttonWidth(ui, "Back");
        if (button(ui, hashId("onb-back"), {panel.r() - 48.f - nw - bw, fy + 2.f, bw, 34.f}, "Back", ButtonKind::Ghost)) goTo(ctx, step - 1);
    } else {
        const float bw = buttonWidth(ui, "Skip");
        if (button(ui, hashId("onb-skip"), {panel.r() - 48.f - nw - bw, fy + 2.f, bw, 34.f}, "Skip", ButtonKind::Ghost)) finish(ctx);
    }
    ui.r.popAlpha();
}

}  // namespace vigil::ui
