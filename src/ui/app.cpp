#include "app.h"

#include "../core/settings.h"
#include "pages.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <deque>

namespace vigil::ui {

namespace {
bool g_previewFramebuffer = false;
}

// Exposed for the preview harness (renders into its own framebuffer).
void setPreviewMode(bool preview) { g_previewFramebuffer = preview; }

struct App::Impl {
    Host* host = nullptr;
    detection::Engine* engine = nullptr;
    Ui ui;
    AppState state;
    bool ready = false;

    bool open = false;
    bool closing = false;
    bool closeDone = false;
    double openedAt = 0.0, closingAt = 0.0;
    float progress = 0.f;
    float scale = 1.f;

    Page shownPage = Page::Overview;
    double pageChangedAt = -10.0;

    struct Toast {
        detection::Alert alert;
        double born = 0.0;
        Spring x;
        Spring y;
    };
    std::deque<Toast> toasts;
    std::uint64_t toastSeq = 0;
    std::uint64_t badgeSeq = 0;

    Setting* theme = nullptr;
    Setting* accent = nullptr;
    Setting* uiScale = nullptr;
    Setting* motion = nullptr;
    Setting* blur = nullptr;
    Setting* toastSeconds = nullptr;

    void registerSettings() {
        auto& S = Settings::instance();
        std::vector<std::string> names;
        for (const Theme& t : themes()) names.push_back(t.name);
        std::vector<std::string> accents;
        for (const auto& a : accentChoices()) accents.push_back(a.first);
        theme = &S.addChoice("ui.theme", "Theme", 0, names, "The overall look of Vigil.");
        accent = &S.addChoice("ui.accent", "Accent", 0, accents, "Accent color used for focus, selection and charts. "
                                                                  "\"Theme\" keeps each theme's own signature color.");
        uiScale = &S.addFloat("ui.scale", "Interface scale", 1.0, {0.8, 1.4, 0.05, 2, "x"},
                              "Size of the Vigil interface relative to the game window.");
        motion = &S.addChoice("ui.motion", "Motion", 0, {"Full", "Reduced", "Off"},
                              "Reduced keeps state changes but removes travel and overshoot. Off disables animation.");
        blur = &S.addFloat("ui.blur", "Backdrop blur", 1.0, {0.0, 1.0, 0.05, 2, ""},
                           "How strongly the game behind Vigil is blurred while the menu is open.");
        toastSeconds = &S.addFloat("ui.toastSeconds", "Notification duration", 6.0, {2.0, 20.0, 0.5, 1, "s"},
                                   "How long on-screen detection notifications stay visible.");
        for (Setting* s : {theme, accent, uiScale, motion, blur, toastSeconds}) s->group = "Appearance";
        toastSeconds->group = "Alerts";
    }

    void applySettings() {
        setTheme(theme->i(), accent->i());
        setMotionLevel(motion->i() == 0 ? 1.f : motion->i() == 1 ? 0.35f : 0.f);
    }

    void pullAlerts(bool hud) {
        for (const auto& a : engine->alerts()) {
            if (a.seq <= toastSeq) continue;
            toastSeq = a.seq;
            if (!open || state.page != Page::Evidence) ++state.unseenDetections;
            if (hud && !open) {
                Toast t;
                t.alert = a;
                t.born = ui.time;
                t.x.snap(1.f);
                t.y.snap(-1.f);
                toasts.push_front(t);
                while (toasts.size() > 3) toasts.pop_back();
            }
        }
    }

    void drawToasts() {
        const Theme& t = ui.th();
        const float life = toastSeconds->f();
        const float w = 330.f, h = 74.f, gap = 10.f;
        float y = 18.f;
        int index = 0;
        for (auto it = toasts.begin(); it != toasts.end();) {
            const float age = static_cast<float>(ui.time - it->born);
            if (age > life + 0.5f) { it = toasts.erase(it); continue; }
            const bool leaving = age > life;
            const float x = it->x.step(leaving ? 1.1f : 0.f, ui.dt, 260.f, 24.f);
            const float targetY = y;
            if (it->y.x < 0.f) it->y.snap(targetY);
            const float yy = it->y.step(targetY, ui.dt, 300.f, 28.f);
            const float alpha = leaving ? std::clamp(1.f - (age - life) / 0.5f, 0.f, 1.f) : std::clamp(age / 0.18f, 0.f, 1.f);
            const Rect box{ui.r.width() - w - 18.f + x * (w + 24.f), yy, w, h};
            ui.r.pushAlpha(alpha);
            ui.r.shadow(box, 14.f, 26.f, Color(0, 0, 0, t.light ? 0.16f : 0.5f), 10.f);
            ui.r.roundRectGradient(box, 14.f, t.surfaceRaised.lighter(0.03f), t.surfaceRaised);
            ui.r.roundRectStroke(box, 14.f, 1.f, t.borderStrong);
            const Color sev = t.severity(it->alert.confidence);
            confidenceRing(ui, mixId(hashId("toast"), it->alert.seq), box.x + 34.f, box.cy(), 17.f, it->alert.confidence, 3.5f, true);
            const auto& checks = engine->checks();
            const std::string checkName = it->alert.check >= 0 && it->alert.check < static_cast<int>(checks.size())
                                              ? checks[static_cast<std::size_t>(it->alert.check)]->name()
                                              : "Detection";
            ui.fonts.draw(ui.r, type::Overline, box.x + 62.f, box.y + 13.f, "DETECTION \xC2\xB7 " + checkName, sev);
            ui.fonts.draw(ui.r, type::Title, box.x + 62.f, box.y + 28.f, it->alert.name, t.text, Align::Left, w - 76.f);
            ui.fonts.draw(ui.r, type::Small, box.x + 62.f, box.y + 49.f, it->alert.summary, t.textMuted, Align::Left, w - 76.f);
            // Remaining-time hairline.
            const float f = std::clamp(1.f - age / life, 0.f, 1.f);
            ui.r.pushClip(box);
            ui.r.rect({box.x, box.b() - 2.f, box.w * f, 2.f}, sev.alpha(0.6f));
            ui.r.popClip();
            ui.r.popAlpha();
            y += h + gap;
            ++index;
            ++it;
        }
    }

    void drawRail(const Rect& rail, float p) {
        const Theme& t = ui.th();
        if (t.rail.a > 0.f) {
            ui.r.pushClip(rail);
            ui.r.roundRect({rail.x, rail.y, rail.w + 30.f, rail.h}, t.radiusWindow, t.rail);
            ui.r.popClip();
        }
        ui.r.rect({rail.r() - 1.f, rail.y + 16.f, 1.f, rail.h - 32.f}, t.divider);

        // Brand.
        const float lx = rail.x + 22.f, ly = rail.y + 24.f;
        if (t.glow > 0.f) ui.r.circle(lx + 15.f, ly + 9.f, 16.f, t.accent.withAlpha(0.10f * t.glow));
        drawLogo(ui.r, lx, ly, 30.f, t.text, t.accent);
        ui.fonts.draw(ui.r, type::Wordmark, lx + 42.f, ly + 2.f, "VIGIL", t.text);
        ui.fonts.draw(ui.r, type::Caption, lx + 42.f, ly + 19.f, "Detection & analysis", t.textFaint);

        struct Item { Page page; Icon icon; const char* label; };
        static const Item items[] = {
            {Page::Overview, Icon::Overview, "Overview"},
            {Page::Checks, Icon::Checks, "Checks"},
            {Page::Evidence, Icon::Evidence, "Evidence"},
            {Page::Players, Icon::Players, "Players"},
            {Page::Settings, Icon::Settings, "Settings"},
        };
        const float itemH = 40.f;
        float y = rail.y + 92.f;
        int activeIndex = 0;
        for (int i = 0; i < 5; ++i) if (items[i].page == state.page) activeIndex = i;
        const float settingsGap = 18.f;
        auto itemY = [&](int i) { return y + i * (itemH + 4.f) + (i == 4 ? settingsGap : 0.f); };
        const float indicatorY = ui.springTo(hashId("nav-indicator"), itemY(activeIndex), 320.f, 26.f);
        const Rect pill{rail.x + 12.f, indicatorY, rail.w - 24.f, itemH};
        ui.r.roundRect(pill, 10.f, t.accentSoft());
        ui.r.roundRect({pill.x, pill.y + 11.f, 3.f, itemH - 22.f}, 1.5f, t.accent);
        ui.r.rect({rail.x + 24.f, itemY(4) - settingsGap * 0.5f - 2.f, rail.w - 48.f, 1.f}, t.divider);

        for (int i = 0; i < 5; ++i) {
            const Item& it = items[i];
            const Rect rc{rail.x + 12.f, itemY(i), rail.w - 24.f, itemH};
            const Id id = hashId(it.label, 77);
            bool h = false;
            if (ui.pressable(id, rc, &h)) {
                state.page = it.page;
                if (it.page == Page::Evidence) state.unseenDetections = 0;
            }
            const bool sel = state.page == it.page;
            const float hv = ui.anim(mixId(id, 1), sel ? 1.f : (h ? 0.55f : 0.f), 16.f);
            if (h && !sel) ui.r.roundRect(rc, 10.f, t.surfaceHover.alpha(0.6f));
            const Color c = t.textMuted.mix(sel ? t.accent : t.text, hv);
            drawIcon(ui.r, it.icon, rc.x + 14.f, rc.cy() - 9.f, 18.f, c);
            ui.fonts.draw(ui.r, type::Label, rc.x + 44.f, rc.cy() - ui.fonts.lineHeight(type::Label) * 0.5f, it.label,
                          sel ? t.text : t.textMuted.mix(t.text, hv));
            if (it.page == Page::Evidence && state.unseenDetections > 0) {
                const std::string n = std::to_string(std::min(99, state.unseenDetections));
                const float bw = std::max(20.f, ui.fonts.width(type::Caption, n) + 12.f);
                const Rect b{rc.r() - bw - 10.f, rc.cy() - 10.f, bw, 20.f};
                const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ui.time) * 3.f);
                if (motionLevel() > 0.01f) ui.r.roundRect(b.inset(-2.f * pulse), 12.f, t.sevAlert.withAlpha(0.18f * pulse));
                ui.r.roundRect(b, 10.f, t.sevAlert);
                ui.fonts.draw(ui.r, type::Caption, b.cx(), b.cy() - ui.fonts.lineHeight(type::Caption) * 0.5f, n,
                              Color::hex(0x1A0A08), Align::Center);
            }
        }

        // Status card.
        const Rect sc{rail.x + 14.f, rail.b() - 98.f, rail.w - 28.f, 82.f};
        ui.r.roundRect(sc, 12.f, t.inset.alpha(t.light ? 0.9f : 0.7f));
        ui.r.roundRectStroke(sc, 12.f, 1.f, t.border);
        const bool active = engine->active();
        const bool monitoring = engine->monitoringEnabled();
        const Color dotC = active ? t.sevClear : (monitoring ? t.sevNotice : t.textFaint);
        statusDot(ui, sc.x + 18.f, sc.y + 21.f, dotC, active);
        const char* stateText = active ? "Monitoring" : (monitoring ? "Standing by" : "Paused");
        ui.fonts.draw(ui.r, type::BodyStrong, sc.x + 32.f, sc.y + 12.f, stateText, t.text);
        std::string where = engine->contextDetail().empty() ? detection::gameContextName(engine->context())
                                                            : engine->contextDetail();
        if (host && !host->connectedToHypixel()) where = "Not on Hypixel";
        ui.fonts.draw(ui.r, type::Small, sc.x + 32.f, sc.y + 31.f, where, t.textMuted, Align::Left, sc.w - 44.f);
        const double sessionSec = (engine->lastTimeMs() - engine->session().startMs()) / 1000.0;
        drawIcon(ui.r, Icon::Clock, sc.x + 11.f, sc.y + 55.f, 14.f, t.textFaint, 1.4f);
        ui.fonts.draw(ui.r, type::Caption, sc.x + 31.f, sc.y + 56.f, "Session " + formatClock(sessionSec), t.textFaint);
        (void)p;
    }

    void drawMenu() {
        const Theme& t = ui.th();
        const double now = ui.time;
        if (closing) {
            const float k = static_cast<float>((now - closingAt) / (0.22 * std::max(0.4f, motionLevel())));
            progress = motionLevel() <= 0.01f ? 0.f : 1.f - ease::outCubic(k);
            if (k >= 1.f || motionLevel() <= 0.01f) {
                progress = 0.f;
                open = false;
                closing = false;
                closeDone = true;
                return;
            }
        } else {
            const float k = static_cast<float>((now - openedAt) / 0.42);
            progress = motionLevel() <= 0.01f ? 1.f : ease::emphasized(k);
        }
        const float p = std::clamp(progress, 0.f, 1.f);
        const float m = motionLevel();

        ui.r.backdrop(p, t.backdropBlur * blur->f(), t.backdropTint);

        const float W = std::min(1180.f, ui.r.width() - 48.f);
        const float H = std::min(760.f, ui.r.height() - 48.f);
        Rect win{(ui.r.width() - W) * 0.5f, (ui.r.height() - H) * 0.5f + (1.f - p) * 22.f * m, W, H};
        ui.r.pushAlpha(p);
        if (t.shadowStrength > 0.f) {
            ui.r.shadow(win, t.radiusWindow, 70.f, Color(0, 0, 0, (t.light ? 0.18f : 0.55f) * t.shadowStrength), 24.f);
        }
        ui.r.roundRectGradient(win, t.radiusWindow, t.windowTop, t.windowBottom);
        if (t.glow > 0.f && !t.light) {
            // A faint accent bloom in the upper-left corner gives the surface depth.
            ui.r.pushClip(win);
            ui.r.shadow({win.x + 40.f, win.y - 120.f, 260.f, 160.f}, 80.f, 160.f, t.accent.withAlpha(0.05f * t.glow), 0.f);
            ui.r.popClip();
        }
        ui.r.roundRectStroke(win, t.radiusWindow, 1.f, t.borderStrong);

        Rect body = win;
        const Rect rail = body.takeLeft(232.f);
        drawRail(rail, p);

        // Close button.
        if (iconButton(ui, hashId("close"), {win.r() - 46.f, win.y + 18.f, 30.f, 30.f}, Icon::Close, "Close  \xC2\xB7  Esc")) {
            requestClose();
        }

        if (state.page != shownPage) {
            shownPage = state.page;
            pageChangedAt = now;
            if (shownPage == Page::Evidence) state.unseenDetections = 0;
        }
        const float a = motionLevel() <= 0.01f ? 1.f : ease::outCubic(static_cast<float>((now - pageChangedAt) / 0.28));
        const Rect content = body.inset(30.f, 24.f);
        ui.r.pushClip({body.x, body.y, body.w, body.h});
        ui.r.pushAlpha(a);
        ui.r.pushOffset(0.f, (1.f - a) * 10.f * m);
        PageContext ctx{ui, *engine, host, state, engine->lastTimeMs()};
        switch (shownPage) {
            case Page::Overview: drawOverview(ctx, content); break;
            case Page::Checks: drawChecks(ctx, content); break;
            case Page::Evidence: drawEvidence(ctx, content); break;
            case Page::Players: drawPlayers(ctx, content); break;
            case Page::Settings: drawSettings(ctx, content); break;
        }
        ui.r.popOffset();
        ui.r.popAlpha();
        ui.r.popClip();
        ui.r.popAlpha();

        // Keyboard navigation.
        bool ctrl = false;
        for (int i = 0; i < 5; ++i) {
            if (ui.consumeKey('1' + i, &ctrl) && ctrl) state.page = static_cast<Page>(i);
        }
    }

    void requestClose() {
        if (!open || closing) return;
        closing = true;
        closingAt = ui.time;
        ui.focus = 0;
        ui.popup = 0;
        ui.captureKey = 0;
    }
};

App& App::instance() {
    static App app;
    return app;
}

App::App() : d_(std::make_unique<Impl>()) {}
App::~App() = default;

void App::initialize(Host* host, detection::Engine* engine) {
    if (d_->ready) return;
    d_->host = host;
    d_->engine = engine;
    d_->registerSettings();
    d_->ready = true;
}

bool App::initialized() const { return d_->ready; }

void App::open() {
    if (d_->open && !d_->closing) return;
    d_->open = true;
    d_->closing = false;
    d_->closeDone = false;
    d_->openedAt = d_->ui.time;
    // Start at the current animated progress when reopening mid-close.
    d_->ui.in = InputState{};
}

void App::requestClose() { d_->requestClose(); }

void App::forceClose() {
    d_->open = false;
    d_->closing = false;
    d_->closeDone = true;
    d_->progress = 0.f;
}

bool App::isOpen() const { return d_->open; }
bool App::closeFinished() const { return d_->closeDone; }
void App::acknowledgeClosed() { d_->closeDone = false; }

void App::focusPlayer(const std::string& key) {
    d_->state.selectedPlayer = key;
    d_->state.selectedEvidence = 0;
    d_->state.page = Page::Evidence;
}

void App::onMouseMove(int x, int y) {
    d_->ui.in.mx = x / d_->scale;
    d_->ui.in.my = y / d_->scale;
}

void App::onMouseButton(int button, bool down, int x, int y) {
    if (button < 0 || button > 2) return;
    onMouseMove(x, y);
    if (down && !d_->ui.in.down[button]) d_->ui.in.pressed[button] = true;
    if (!down && d_->ui.in.down[button]) d_->ui.in.released[button] = true;
    d_->ui.in.down[button] = down;
}

void App::onMouseWheel(float notches) { d_->ui.in.wheel += notches; }

void App::onChar(unsigned int cp) { d_->ui.in.chars.push_back(cp); }

void App::onKey(int vk, bool down, bool ctrl, bool shift) { d_->ui.in.keys.push_back({vk, down, ctrl, shift}); }

Cursor App::cursor() const { return d_->ui.cursor; }

bool App::textInputActive() const { return d_->ui.focus != 0 || d_->ui.captureKey != 0 || d_->ui.popup != 0; }

void App::render(int width, int height, double nowMs, bool inGameHud) {
    Impl& d = *d_;
    if (!d.ready || !d.engine) return;
    if (!d.open && d.toasts.empty()) {
        // Normal play with nothing to show: collect new alerts without touching any GL state.
        d.ui.time = nowMs / 1000.0;
        d.pullAlerts(inGameHud);
        if (d.toasts.empty()) return;
    }
    d.applySettings();
    d.scale = std::clamp(height / 920.f, 0.8f, 1.8f) * d.uiScale->f();
    d.ui.r.begin(width, height, d.scale, !g_previewFramebuffer);
    d.ui.fonts.setScale(d.scale);
    d.ui.beginFrame(nowMs / 1000.0);
    d.pullAlerts(inGameHud);
    if (d.open) d.drawMenu();
    else if (!d.toasts.empty()) d.drawToasts();
    // ESC from within the UI (no text focus) is handled by the host; Escape while a popup or
    // text field is focused only dismisses that.
    if (d.ui.consumeKey(VK_ESCAPE)) {
        if (d.ui.popup) d.ui.popup = 0;
        else if (d.ui.focus) d.ui.focus = 0;
        else d.requestClose();
    }
    d.ui.endFrame();
    d.ui.r.end();
}

void App::releaseGraphics() {
    d_->ui.fonts.releaseGraphics();
    d_->ui.r.releaseResources();
}

}  // namespace vigil::ui
