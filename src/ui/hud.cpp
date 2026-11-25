#include "hud.h"

#include "motion.h"
#include "pages.h"

#include <algorithm>
#include <cmath>

namespace vigil::ui {
namespace {

constexpr float kActivationLife = 7.5f;
constexpr float kNoticeLife = 2.4f;
constexpr int kMaxToasts = 3;
constexpr float kToastW = 364.f;
constexpr float kToastH = 112.f;

}  // namespace

void Hud::onAlert(const detection::Alert& a, double now) {
    // Repeat detections for a player already on screen update that card instead of stacking.
    for (Toast& t : toasts_) {
        if (t.playerKey == a.playerKey && t.leaving < 0.0) {
            if (std::find(t.checks.begin(), t.checks.end(), a.check) == t.checks.end()) t.checks.push_back(a.check);
            t.check = a.check;
            t.confidence = a.confidence;
            t.summary = a.summary;
            ++t.count;
            t.updated = now;
            return;
        }
    }
    Toast t;
    t.playerKey = a.playerKey;
    t.name = a.name;
    t.check = a.check;
    t.checks = {a.check};
    t.confidence = a.confidence;
    t.firstConfidence = a.confidence;
    t.summary = a.summary;
    t.born = t.updated = now;
    toasts_.push_front(t);
    while (toasts_.size() > 8) toasts_.pop_back();
}

void Hud::showActivation(double now) { activationAt_ = now; }

void Hud::notify(const std::string& title, const std::string& body, double now) {
    notices_.push_back({title, body, now});
    while (notices_.size() > 3) notices_.pop_front();
}

void Hud::clear() {
    toasts_.clear();
    notices_.clear();
}

float Hud::lifeOf(const Toast& t, float base) const {
    float life = base + 1.5f * std::min<int>(3, static_cast<int>(t.checks.size()) - 1);
    if (t.confidence >= 90.f) life += 2.f;   // critical detections stay longer
    return life;
}

bool Hud::idle(double now) const {
    if (now - activationAt_ < kActivationLife + 0.5) return false;
    if (!notices_.empty()) return false;
    return toasts_.empty();
}

std::string Hud::focusCandidate(double now) const {
    for (const Toast& t : toasts_) {
        if (t.leaving < 0.0 && now - t.updated < lifeOf(t, lastLife_)) return t.playerKey;
    }
    return {};
}

void Hud::draw(const Context& c) {
    lastLife_ = c.lifeSec;
    if (c.showToasts) drawToasts(c);
    drawActivation(c);
    drawNotices(c);
}

// ── Detection notifications ──────────────────────────────────────────────────────────────────

void Hud::drawToasts(const Context& c) {
    Ui& ui = c.ui;
    const Theme& t = ui.th();
    const auto& checks = c.engine.checks();
    const bool right = c.position == 0 || c.position == 2;
    const bool bottom = c.position >= 2;
    const float margin = 18.f, gap = 10.f;

    // Retire expired cards (exit animation first).
    for (Toast& tt : toasts_) {
        if (tt.leaving < 0.0 && c.now - tt.updated > lifeOf(tt, c.lifeSec)) tt.leaving = c.now;
    }
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(),
                                 [&](const Toast& tt) { return tt.leaving >= 0.0 && c.now - tt.leaving > motion::kToastOut; }),
                  toasts_.end());

    int slot = 0;
    int hidden = 0;
    for (Toast& tt : toasts_) {
        if (slot >= kMaxToasts) { if (tt.leaving < 0.0) ++hidden; continue; }
        const float stackY = margin + slot * (kToastH + gap);
        const float targetY = bottom ? ui.r.height() - stackY - kToastH : stackY;
        if (!tt.y.initialized) tt.y.snap(targetY);
        const float yy = tt.y.step(targetY, ui.dt, motion::kReflow.stiffness, motion::kReflow.damping);
        // Enter: spring from beyond the edge. Exit: quick accelerate out.
        if (!tt.x.initialized) tt.x.snap(1.f);
        float slide = tt.x.step(0.f, ui.dt, motion::kToast.stiffness, motion::kToast.damping);
        float alpha = ease::outCubic(motion::progress(c.now, tt.born, 0.16f));
        if (tt.leaving >= 0.0) {
            const float k = motion::progress(c.now, tt.leaving, motion::kToastOut);
            alpha = 1.f - k;
            slide = k * k * 0.35f;
        }
        const float travelX = (kToastW + margin + 12.f) * slide * (motionLevel() > 0.99f ? 1.f : 0.f);
        const float x = right ? ui.r.width() - kToastW - margin + travelX : margin - travelX;
        const Rect box{x, yy, kToastW, kToastH};
        const Color sev = t.severity(tt.confidence);
        const float bump = motion::pulse(c.now, tt.updated) * (tt.updated > tt.born ? 1.f : 0.f);

        ui.r.pushAlpha(alpha);
        ui.r.shadow(box, 16.f, 30.f, Color(0, 0, 0, t.light ? 0.18f : 0.55f), 12.f);
        ui.r.roundRectGradient(box, 16.f, t.surfaceRaised.lighter(t.light ? 0.f : 0.035f), t.surfaceRaised);
        ui.r.roundRectStroke(box, 16.f, 1.f, t.borderStrong.mix(sev, 0.25f + 0.5f * bump));
        // Severity edge: an inset pill, so it never crosses the rounded corners.
        ui.r.roundRect({box.x + 6.f, box.y + 16.f, 3.f, box.h - 32.f}, 1.5f, sev);

        // Confidence ring with the check glyph.
        const float rcx = box.x + 38.f, rcy = box.y + 40.f;
        confidenceRing(ui, mixId(hashId("toast-ring"), hashId(tt.playerKey)), rcx, rcy, 21.f, tt.confidence, 3.5f, true);

        const std::string checkName = tt.check >= 0 && tt.check < static_cast<int>(checks.size())
                                          ? checks[static_cast<std::size_t>(tt.check)]->name() : "Detection";
        std::string over = "DETECTION \xC2\xB7 " + checkName;
        if (tt.checks.size() > 1) over += " +" + std::to_string(tt.checks.size() - 1);
        const float tx = box.x + 72.f;
        ui.fonts.draw(ui.r, type::Overline, tx, box.y + 14.f, over, sev, Align::Left, kToastW - 140.f);
        if (tt.count > 1) {
            const std::string cnt = "\xC3\x97" + std::to_string(tt.count);
            const float cw = chipWidth(ui, cnt);
            chip(ui, {box.r() - cw - 14.f, box.y + 10.f, cw, 19.f}, cnt, sev);
        }
        // Player line: face + name + trend since first notification.
        playerAvatarByKey(ui, c.host, tt.playerKey, tt.name, tt.confidence, {tx, box.y + 31.f, 20.f, 20.f});
        const float nw = ui.fonts.draw(ui.r, type::Title, tx + 28.f, box.y + 31.f, tt.name, t.text, Align::Left, kToastW - 170.f);
        trendLabel(ui, tx + 36.f + nw, box.y + 35.f, tt.confidence - tt.firstConfidence);
        ui.fonts.draw(ui.r, type::Small, tx, box.y + 56.f, tt.summary, t.textMuted, Align::Left, kToastW - 88.f);

        // Confidence over the last minute.
        if (const detection::PlayerRecord* rec = c.engine.session().find(tt.playerKey)) {
            if (tt.check >= 0 && tt.check < static_cast<int>(rec->checks.size())) {
                const double t1 = c.engine.lastTimeMs();
                sparkline(ui, {box.x + 16.f, box.y + 78.f, 120.f, 18.f}, rec->checks[static_cast<std::size_t>(tt.check)].history,
                          t1 - 60e3, t1, sev.alpha(0.9f));
            }
        }
        // Discoverable next step.
        const std::string hint = "Investigate";
        const float hw = ui.fonts.width(type::Caption, hint);
        const float kw = keycapWidth(ui, c.menuKey);
        const float hx = box.r() - 16.f - hw - kw - 8.f;
        keycap(ui, {hx, box.y + 80.f, kw, 20.f}, c.menuKey);
        ui.fonts.draw(ui.r, type::Caption, hx + kw + 8.f, box.y + 83.f, hint, t.textMuted);
        // Remaining time.
        const float f = std::clamp(1.f - static_cast<float>(c.now - tt.updated) / lifeOf(tt, c.lifeSec), 0.f, 1.f);
        if (f > 0.01f) ui.r.roundRect({box.x + 18.f, box.b() - 5.f, (box.w - 36.f) * f, 2.f}, 1.f, sev.alpha(0.45f));
        ui.r.popAlpha();
        ++slot;
    }
    if (hidden > 0) {
        const std::string more = "+" + std::to_string(hidden) + " more \xC2\xB7 open Vigil";
        const float w = ui.fonts.width(type::Caption, more) + 22.f;
        const float stackY = margin + slot * (kToastH + gap);
        const float y = bottom ? ui.r.height() - stackY - 24.f : stackY;
        const float x = right ? ui.r.width() - margin - w : margin;
        const Rect pill{x, y, w, 24.f};
        ui.r.roundRect(pill, 12.f, t.surfaceRaised);
        ui.r.roundRectStroke(pill, 12.f, 1.f, t.borderStrong);
        ui.fonts.draw(ui.r, type::Caption, pill.cx(), pill.cy() - ui.fonts.lineHeight(type::Caption) * 0.5f, more, t.textMuted, Align::Center);
    }
}

// ── Activation card ──────────────────────────────────────────────────────────────────────────

void Hud::drawActivation(const Context& c) {
    const double age = c.now - activationAt_;
    if (age < 0.0 || age > kActivationLife + 0.4) return;
    Ui& ui = c.ui;
    const Theme& t = ui.th();
    const float w = 452.f, h = 196.f;
    // Arrive with a spring from above; leave upward with a short fade.
    const float in = ease::emphasized(motion::progress(c.now, activationAt_, 0.42f));
    const float out = age > kActivationLife ? ease::inOutCubic(static_cast<float>((age - kActivationLife) / 0.4)) : 0.f;
    const float alpha = in * (1.f - out);
    const float y = 26.f - (1.f - in) * motion::travel(26.f) - out * motion::travel(18.f);
    const Rect box{(ui.r.width() - w) * 0.5f, y, w, h};
    ui.r.pushAlpha(alpha);
    ui.r.shadow(box, 20.f, 42.f, Color(0, 0, 0, t.light ? 0.2f : 0.6f), 14.f);
    ui.r.roundRectGradient(box, 20.f, t.windowTop, t.windowBottom);
    ui.r.roundRectStroke(box, 20.f, 1.f, t.borderStrong);
    if (t.glow > 0.f) {
        ui.r.pushClip(box);
        ui.r.shadow({box.x + 20.f, box.y - 80.f, 160.f, 120.f}, 60.f, 110.f, t.accent.withAlpha(0.10f * t.glow), 0.f);
        ui.r.popClip();
    }

    // The mark: body fades in, then the point of light ignites.
    const float lx = box.x + 26.f, ly = box.y + 24.f;
    const float body = ease::outCubic(motion::progress(c.now, activationAt_ + 0.1, 0.3f));
    const float flame = ease::outBack(motion::progress(c.now, activationAt_ + 0.35, 0.35f), 2.2f);
    ui.r.pushAlpha(body);
    drawLogo(ui.r, lx, ly, 40.f, t.text, Color(0, 0, 0, 0));
    ui.r.popAlpha();
    const Point d = logoDotCenter();
    if (flame > 0.01f) {
        if (t.glow > 0.f) ui.r.circle(lx + d.x * 40.f, ly + d.y * 40.f, 9.f * flame, t.accent.withAlpha(0.18f * t.glow));
        ui.r.circle(lx + d.x * 40.f, ly + d.y * 40.f, logoDotRadius() * 40.f * flame, t.accent);
    }
    ui.fonts.draw(ui.r, type::Overline, box.x + 82.f, box.y + 24.f, "VIGIL", t.accent);
    ui.fonts.draw(ui.r, type::Heading, box.x + 82.f, box.y + 38.f, "Vigil is active", t.text);

    // Checklist, ticking in sequence.
    RuntimeStatus rs;
    if (c.host) rs = c.host->runtimeStatus();
    int enabled = 0;
    for (const auto& ch : c.engine.checks()) if (ch->enabled()) ++enabled;
    const std::string where = c.engine.active() ? "Monitoring \xC2\xB7 " + (c.engine.contextDetail().empty()
                                                                                ? std::string(detection::gameContextName(c.engine.context()))
                                                                                : c.engine.contextDetail())
                                                : std::string("Monitoring \xC2\xB7 standing by until a game starts");
    const std::string items[4] = {
        rs.hosted ? "Attached to Lunar Client" : "Attached to the game",
        "Detection engine \xC2\xB7 " + std::to_string(enabled) + " checks",
        where,
        "Interface ready",
    };
    for (int i = 0; i < 4; ++i) {
        const float k = ease::outCubic(motion::progress(c.now, activationAt_ + 0.45 + i * 0.13, 0.22f));
        const float rx = box.x + 26.f + (i % 2) * (w * 0.5f - 6.f);
        const float ry = box.y + 86.f + (i / 2) * 26.f;
        ui.r.pushAlpha(k);
        ui.r.circle(rx + 8.f, ry + 8.f, 8.f, t.sevClear.withAlpha(0.16f));
        drawIcon(ui.r, Icon::Check, rx + 2.f, ry + 2.f, 12.f, t.sevClear, 2.f);
        ui.fonts.draw(ui.r, type::Small, rx + 24.f, ry + 1.f, items[i], t.textMuted, Align::Left, w * 0.5f - 40.f);
        ui.r.popAlpha();
    }
    // How to open.
    divider(ui, box.x + 22.f, box.r() - 22.f, box.b() - 46.f);
    const float kw = keycapWidth(ui, c.menuKey);
    float hx = box.x + 26.f;
    ui.fonts.draw(ui.r, type::Small, hx, box.b() - 31.f, "Press", t.textMuted);
    hx += ui.fonts.width(type::Small, "Press") + 8.f;
    keycap(ui, {hx, box.b() - 33.f, kw, 21.f}, c.menuKey);
    hx += kw + 8.f;
    ui.fonts.draw(ui.r, type::Small, hx, box.b() - 31.f, "to open Vigil, or type /vigil in chat.", t.textMuted);
    // Remaining time.
    const float f = std::clamp(1.f - static_cast<float>(age) / kActivationLife, 0.f, 1.f);
    if (f > 0.01f) ui.r.roundRect({box.x + 22.f, box.b() - 6.f, (box.w - 44.f) * f, 2.f}, 1.f, t.accent.alpha(0.45f));
    ui.r.popAlpha();
}

// ── Notices ──────────────────────────────────────────────────────────────────────────────────

void Hud::drawNotices(const Context& c) {
    Ui& ui = c.ui;
    const Theme& t = ui.th();
    while (!notices_.empty() && c.now - notices_.front().born > kNoticeLife + 0.3) notices_.pop_front();
    float y = c.now - activationAt_ < kActivationLife ? 236.f : 26.f;
    for (const Notice& n : notices_) {
        const double age = c.now - n.born;
        const float in = ease::outCubic(motion::progress(c.now, n.born, 0.18f));
        const float out = age > kNoticeLife ? static_cast<float>((age - kNoticeLife) / 0.3) : 0.f;
        const float alpha = std::clamp(in * (1.f - out), 0.f, 1.f);
        const float tw = ui.fonts.width(type::BodyStrong, n.title);
        const float bw = n.body.empty() ? 0.f : ui.fonts.width(type::Small, n.body) + 12.f;
        const float w = tw + bw + 58.f;
        const Rect pill{(ui.r.width() - w) * 0.5f, y - (1.f - in) * motion::travel(8.f), w, 38.f};
        ui.r.pushAlpha(alpha);
        ui.r.shadow(pill, 19.f, 20.f, Color(0, 0, 0, t.light ? 0.14f : 0.45f), 6.f);
        ui.r.roundRect(pill, 19.f, t.surfaceRaised);
        ui.r.roundRectStroke(pill, 19.f, 1.f, t.borderStrong);
        drawLogo(ui.r, pill.x + 14.f, pill.y + 10.f, 18.f, t.text, t.accent);
        ui.fonts.draw(ui.r, type::BodyStrong, pill.x + 40.f, pill.cy() - ui.fonts.lineHeight(type::BodyStrong) * 0.5f, n.title, t.text);
        if (!n.body.empty()) {
            ui.fonts.draw(ui.r, type::Small, pill.x + 52.f + tw, pill.cy() - ui.fonts.lineHeight(type::Small) * 0.5f, n.body, t.textMuted);
        }
        ui.r.popAlpha();
        y += 46.f;
    }
}

}  // namespace vigil::ui
