#include "app.h"

#include "../core/settings.h"
#include "../identity/identity.h"
#include "../utilities/utilities.h"
#include "hud.h"
#include "pages.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>

namespace vigil::ui {

namespace {
bool g_previewFramebuffer = false;

struct RailItem {
    Page page;
    const char* group;   // printed above the first item of each group
};
constexpr RailItem kRail[] = {
    {Page::Overview, "MONITOR"},
    {Page::Players, "INVESTIGATE"},
    {Page::Checks, "CONFIGURE"},
    {Page::Utilities, nullptr},
    {Page::Settings, nullptr},
};
}  // namespace

// Exposed for the preview harness (renders into its own framebuffer).
void setPreviewMode(bool preview) { g_previewFramebuffer = preview; }

struct App::Impl {
    Host* host = nullptr;
    detection::Engine* engine = nullptr;
    Ui ui;
    AppState state;
    Hud hud;
    bool ready = false;

    bool open = false;
    bool closing = false;
    bool closeDone = false;
    double openedAt = 0.0, closingAt = 0.0;
    float scale = 1.f;

    Page shownPage = Page::Overview;
    double pageChangedAt = -10.0;
    ProfileTab shownTab = ProfileTab::Summary;
    std::uint64_t alertSeq = 0;
    bool pendingActivation = false;

    Setting* theme = nullptr;
    Setting* accent = nullptr;
    Setting* uiScale = nullptr;
    Setting* motionSetting = nullptr;
    Setting* blur = nullptr;
    Setting* toastSeconds = nullptr;
    Setting* toastPosition = nullptr;
    Setting* menuKey = nullptr;
    Setting* onboarded = nullptr;
    Setting* customBase = nullptr;
    Setting* customHue = nullptr;
    Setting* customTint = nullptr;
    Setting* customContrast = nullptr;
    Setting* customRadius = nullptr;
    Setting* customGlow = nullptr;

    void registerSettings() {
        auto& S = Settings::instance();
        std::vector<std::string> names;
        for (const Theme& t : themes()) names.push_back(t.name);
        names.push_back("Custom");
        std::vector<std::string> accents;
        for (const auto& a : accentChoices()) accents.push_back(a.first);
        auto grp = [](Setting& s, const char* g, bool adv = false) -> Setting& { s.group = g; s.advanced = adv; return s; };
        theme = &grp(S.addChoice("ui.theme", "Theme", 0, names, "The overall look of Vigil."), "Appearance");
        accent = &grp(S.addChoice("ui.accent", "Accent", 0, accents, "Accent color used for focus, selection and charts. "
                                                                    "\"Theme\" keeps each theme's own signature color."),
                      "Appearance");
        uiScale = &grp(S.addFloat("ui.scale", "Interface scale", 1.0, {0.8, 1.4, 0.05, 2, "x"},
                                  "Size of the Vigil interface relative to the game window."), "Appearance");
        motionSetting = &grp(S.addChoice("ui.motion", "Motion", 0, {"Full", "Reduced", "Off"},
                                         "Reduced keeps state changes but removes travel and overshoot. Off disables animation."),
                             "Appearance");
        blur = &grp(S.addFloat("ui.blur", "Backdrop blur", 1.0, {0.0, 1.0, 0.05, 2, ""},
                               "How strongly the game behind Vigil is blurred while the menu is open."), "Appearance");
        toastSeconds = &grp(S.addFloat("ui.toastSeconds", "Notification duration", 6.0, {2.0, 20.0, 0.5, 1, "s"},
                                       "How long on-screen detection notifications stay visible. Critical detections and "
                                       "notifications that combine several checks stay a little longer."), "Alerts");
        toastPosition = &grp(S.addChoice("ui.toastPosition", "Notification position", 0,
                                         {"Top right", "Top left", "Bottom right", "Bottom left"},
                                         "Where detection notifications appear on screen."), "Alerts");
        menuKey = &grp(S.addKey("ui.menuKey", "Open Vigil", VK_RSHIFT,
                                "Opens Vigil while no other screen is open. Pressing it while a detection notification is "
                                "visible opens that player. You can also type /vigil in chat."), "Keybinds");
        onboarded = &S.addBool("ui.onboarded", "Introduction seen", false, "Whether the first-run introduction was shown.");
        onboarded->group = "";
        // Theme editor (Settings → Appearance).
        std::vector<std::string> bases;
        for (const Theme& t : themes()) bases.push_back(t.name);
        customBase = &grp(S.addChoice("ui.custom.base", "Start from", 0, bases, "The built-in theme your custom theme is based on."),
                          "Theme editor");
        customHue = &grp(S.addFloat("ui.custom.hue", "Accent hue", 228.0, {0.0, 360.0, 1.0, 0, "\xC2\xB0"},
                                    "Hue of the accent color, used for focus, selection and charts."), "Theme editor");
        customTint = &grp(S.addFloat("ui.custom.tint", "Surface tint", 0.3, {0.0, 1.0, 0.05, 2, ""},
                                     "How much the surfaces lean toward the accent hue."), "Theme editor");
        customContrast = &grp(S.addFloat("ui.custom.contrast", "Contrast", 1.0, {0.8, 1.4, 0.05, 2, "x"},
                                         "Strength of borders and secondary text."), "Theme editor");
        customRadius = &grp(S.addFloat("ui.custom.radius", "Corner radius", 1.0, {0.5, 1.5, 0.05, 2, "x"},
                                       "Roundness of windows, cards and controls."), "Theme editor");
        customGlow = &grp(S.addFloat("ui.custom.glow", "Glow", 0.5, {0.0, 1.0, 0.05, 2, ""},
                                     "Soft light around accented elements."), "Theme editor");
        utilities::registerSettings();
        identity::registerSettings();
    }

    CustomTheme customParams() const {
        CustomTheme c;
        c.base = customBase->i();
        c.hue = customHue->f();
        c.tint = customTint->f();
        c.contrast = customContrast->f();
        c.radius = customRadius->f();
        c.glow = customGlow->f();
        return c;
    }

    void applySettings() {
        setTheme(theme->i(), accent->i(), customParams());
        setMotionLevel(motionSetting->i() == 0 ? 1.f : motionSetting->i() == 1 ? 0.35f : 0.f);
    }

    PageContext context() { return PageContext{ui, *engine, host, state, engine->lastTimeMs()}; }

    void pullAlerts(bool toasts) {
        for (const auto& a : engine->alerts()) {
            if (a.seq <= alertSeq) continue;
            alertSeq = a.seq;
            const bool viewing = open && state.page == Page::Players && state.selectedPlayer == a.playerKey;
            if (!viewing) {
                state.unseenPlayers.insert(a.playerKey);
                state.detectionPulseAt = ui.time;
            }
            if (!open && toasts) hud.onAlert(a, ui.time);
        }
    }

    // ── Rail ──────────────────────────────────────────────────────────────────────────────────
    void drawRail(const Rect& rail) {
        const Theme& t = ui.th();
        PageContext ctx = context();
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

        const float itemH = 38.f;
        float y = rail.y + 84.f;
        std::vector<std::pair<Page, float>> positions;
        for (const RailItem& it : kRail) {
            if (it.group) {
                y += 10.f;
                ui.fonts.draw(ui.r, type::Overline, rail.x + 26.f, y, it.group, t.textFaint.alpha(0.85f));
                y += 20.f;
            }
            positions.push_back({it.page, y});
            y += itemH + 2.f;
        }
        // Selection indicator glides between items; Status lives in the footer.
        float targetY = -1.f;
        for (auto& p : positions) if (p.first == state.page) targetY = p.second;
        const float indicatorAlpha = ui.anim(hashId("nav-ind-alpha"), targetY >= 0.f ? 1.f : 0.f, motion::kSelect);
        if (targetY >= 0.f || indicatorAlpha > 0.01f) {
            float& lastY = ui.value(hashId("nav-ind-last"), targetY);
            if (targetY >= 0.f) lastY = targetY;
            const float iy = ui.springTo(hashId("nav-indicator"), lastY, motion::kIndicator.stiffness, motion::kIndicator.damping);
            const Rect pill{rail.x + 12.f, iy, rail.w - 24.f, itemH};
            ui.r.pushAlpha(indicatorAlpha);
            ui.r.roundRect(pill, 10.f, t.accentSoft());
            ui.r.roundRect({pill.x, pill.y + 11.f, 3.f, itemH - 22.f}, 1.5f, t.accent);
            ui.r.popAlpha();
        }
        for (auto& [page, py] : positions) {
            const Rect rc{rail.x + 12.f, py, rail.w - 24.f, itemH};
            const Id id = hashId(pageTitle(page), 77);
            bool h = false;
            if (ui.pressable(id, rc, &h)) ctx.navigate(page);
            const bool sel = state.page == page;
            const float hv = ui.anim(mixId(id, 1), sel ? 1.f : (h ? 0.55f : 0.f), motion::kHover);
            if (h && !sel) ui.r.roundRect(rc, 10.f, t.surfaceHover.alpha(0.6f));
            const Color c = t.textMuted.mix(sel ? t.accent : t.text, hv);
            drawIcon(ui.r, pageIcon(page), rc.x + 14.f, rc.cy() - 9.f, 18.f, c);
            ui.fonts.draw(ui.r, type::Label, rc.x + 44.f, rc.cy() - ui.fonts.lineHeight(type::Label) * 0.5f, pageTitle(page),
                          sel ? t.text : t.textMuted.mix(t.text, hv));
            if (page == Page::Players && !state.unseenPlayers.empty()) {
                const std::string n = std::to_string(std::min<std::size_t>(99, state.unseenPlayers.size()));
                const float bw = std::max(20.f, ui.fonts.width(type::Caption, n) + 12.f);
                const Rect b{rc.r() - bw - 10.f, rc.cy() - 10.f, bw, 20.f};
                const float p = motion::pulse(ui.time, state.detectionPulseAt);
                if (p > 0.01f) ui.r.roundRect(b.inset(-5.f * p), 14.f, t.sevAlert.withAlpha(0.25f * p));
                ui.r.roundRect(b, 10.f, t.sevAlert);
                ui.fonts.draw(ui.r, type::Caption, b.cx(), b.cy() - ui.fonts.lineHeight(type::Caption) * 0.5f, n,
                              Color::hex(0x1A0A08), Align::Center);
                ui.tooltip(mixId(id, 9), b, n + (n == "1" ? " player has" : " players have") + " new detections");
            }
        }

        // Health card → Status.
        const Health hl = computeHealth(ctx);
        const Rect sc{rail.x + 14.f, rail.b() - 100.f, rail.w - 28.f, 84.f};
        const Id sid = hashId("rail-health");
        bool sh = false;
        if (ui.pressable(sid, sc, &sh)) ctx.navigate(Page::Status);
        const bool onStatus = state.page == Page::Status;
        const float shv = ui.anim(mixId(sid, 1), onStatus ? 1.f : (sh ? 0.6f : 0.f), motion::kHover);
        ui.r.roundRect(sc, 12.f, t.inset.alpha(t.light ? 0.9f : 0.7f).mix(t.accentSoft(), shv * 0.6f));
        ui.r.roundRectStroke(sc, 12.f, 1.f, onStatus ? t.accent.withAlpha(0.6f) : t.border.mix(t.borderStrong, shv));
        const Color dotC = healthColor(t, hl.level);
        statusDot(ui, sc.x + 18.f, sc.y + 21.f, dotC, hl.level == Health::Active);
        ui.fonts.draw(ui.r, type::BodyStrong, sc.x + 32.f, sc.y + 12.f, hl.title, t.text);
        drawIcon(ui.r, Icon::ChevronRight, sc.r() - 24.f, sc.y + 13.f, 14.f, t.textFaint.mix(t.text, shv));
        ui.fonts.draw(ui.r, type::Small, sc.x + 32.f, sc.y + 31.f, hl.detail, t.textMuted, Align::Left, sc.w - 44.f);
        const double sessionSec = (engine->lastTimeMs() - engine->session().startMs()) / 1000.0;
        drawIcon(ui.r, Icon::Clock, sc.x + 11.f, sc.y + 56.f, 14.f, t.textFaint, 1.4f);
        ui.fonts.draw(ui.r, type::Caption, sc.x + 31.f, sc.y + 57.f, "Session " + formatClock(sessionSec), t.textFaint);
        ui.tooltip(mixId(sid, 2), sc, "Open Status: injection, hooks, detection engine and signals.");
    }

    // ── Top bar: back, breadcrumb, search, close ─────────────────────────────────────────────
    void drawTopBar(const Rect& bar) {
        const Theme& t = ui.th();
        PageContext ctx = context();
        float x = bar.x;
        const bool canBack = !state.history.empty();
        const float backA = ui.anim(hashId("back-a"), canBack ? 1.f : 0.f, motion::kReveal);
        if (backA > 0.01f) {
            ui.r.pushAlpha(backA);
            if (iconButton(ui, hashId("back"), {x, bar.cy() - 15.f, 30.f, 30.f}, Icon::Back, "Back \xC2\xB7 Backspace") && canBack) ctx.back();
            ui.r.popAlpha();
            x += 38.f * backA;
        }
        // Breadcrumb.
        std::vector<std::string> crumbs = {pageSection(state.page), pageTitle(state.page)};
        if (state.page == Page::Players && !state.selectedPlayer.empty()) {
            if (const auto* p = engine->session().find(state.selectedPlayer)) crumbs.push_back(p->name);
        }
        const float cy = bar.cy() - ui.fonts.lineHeight(type::Small) * 0.5f;
        for (std::size_t i = 0; i < crumbs.size(); ++i) {
            const bool last = i + 1 == crumbs.size();
            const TextStyle st = last ? type::SmallStrong : type::Small;
            const float w = ui.fonts.draw(ui.r, st, x, cy, crumbs[i], last ? t.text : t.textFaint);
            x += w;
            if (!last) {
                drawIcon(ui.r, Icon::ChevronRight, x + 3.f, bar.cy() - 6.f, 12.f, t.textFaint.alpha(0.7f));
                x += 20.f;
            }
        }
        // Search field (opens the palette).
        const float sw = 300.f;
        const Rect sr{bar.r() - 46.f - sw, bar.cy() - 17.f, sw, 34.f};
        const Id id = hashId("open-search");
        bool h = false;
        if (ui.pressable(id, sr, &h)) openSearch();
        const float hv = ui.anim(mixId(id, 1), h ? 1.f : 0.f, motion::kHover);
        ui.r.roundRect(sr, t.radiusControl, t.inset.mix(t.surfaceHover, hv * 0.5f));
        ui.r.roundRectStroke(sr, t.radiusControl, 1.f, t.border.mix(t.borderStrong, hv));
        drawIcon(ui.r, Icon::Search, sr.x + 11.f, sr.cy() - 8.f, 16.f, t.textFaint.mix(t.textMuted, hv));
        ui.fonts.draw(ui.r, type::Body, sr.x + 36.f, sr.cy() - ui.fonts.lineHeight(type::Body) * 0.5f, "Search players, checks, settings",
                      t.textFaint, Align::Left, sw - 110.f);
        const float k2 = keycapWidth(ui, "K"), k1 = keycapWidth(ui, "Ctrl");
        keycap(ui, {sr.r() - 10.f - k2, sr.cy() - 10.f, k2, 20.f}, "K");
        keycap(ui, {sr.r() - 14.f - k2 - k1, sr.cy() - 10.f, k1, 20.f}, "Ctrl");
        if (iconButton(ui, hashId("close"), {bar.r() - 32.f, bar.cy() - 16.f, 32.f, 32.f}, Icon::Close, "Close \xC2\xB7 Esc")) requestClose();
    }

    void openSearch() {
        state.searchOpen = true;
        state.searchQuery.clear();
        state.searchIndex = 0;
        state.searchChangedAt = ui.time;
        ui.focus = hashId("search-field");
        ui.popup = 0;
    }

    void drawMenu() {
        const Theme& t = ui.th();
        const double now = ui.time;
        float p;
        if (closing) {
            const float k = motion::progress(now, closingAt, motion::kMenuClose);
            p = 1.f - ease::inOutCubic(k);
            if (k >= 1.f) {
                open = false;
                closing = false;
                closeDone = true;
                return;
            }
        } else {
            p = ease::emphasized(motion::progress(now, openedAt, motion::kMenuOpen));
        }
        p = std::clamp(p, 0.f, 1.f);

        ui.r.backdrop(p, t.backdropBlur * blur->f(), t.backdropTint);

        const float W = std::min(1180.f, ui.r.width() - 48.f);
        const float H = std::min(780.f, ui.r.height() - 48.f);
        Rect win{(ui.r.width() - W) * 0.5f, (ui.r.height() - H) * 0.5f + (1.f - p) * motion::travel(14.f), W, H};
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

        const bool overlay = state.searchOpen || state.onboardingOpen;
        ui.modal = overlay;

        Rect body = win;
        const Rect rail = body.takeLeft(232.f);
        drawRail(rail);
        const Rect bar{body.x + 30.f, body.y + 14.f, body.w - 48.f, 44.f};
        drawTopBar(bar);

        // Page transition: travels in the direction of navigation.
        if (state.page != shownPage) {
            shownPage = state.page;
            pageChangedAt = now;
            if (shownPage == Page::Players) state.unseenPlayers.erase(state.selectedPlayer);
        }
        const float a = ease::emphasized(motion::progress(now, pageChangedAt, motion::kPage));
        const Rect content{body.x + 30.f, body.y + 70.f, body.w - 60.f, body.h - 94.f};
        ui.r.pushClip({body.x, body.y + 60.f, body.w, body.h - 60.f});
        ui.r.pushAlpha(a);
        ui.r.pushOffset(0.f, (1.f - a) * motion::travel(motion::kPageTravel) * static_cast<float>(state.navDirection));
        PageContext ctx = context();
        switch (shownPage) {
            case Page::Overview: drawOverview(ctx, content); break;
            case Page::Players: drawPlayers(ctx, content); break;
            case Page::Checks: drawChecks(ctx, content); break;
            case Page::Utilities: drawUtilities(ctx, content); break;
            case Page::Settings: drawSettings(ctx, content); break;
            case Page::Status: drawStatus(ctx, content); break;
        }
        ui.r.popOffset();
        ui.r.popAlpha();
        ui.r.popClip();

        ui.modal = false;
        if (state.searchOpen) drawSearch(ctx, win);
        if (state.onboardingOpen) drawOnboarding(ctx, win);
        ui.r.popAlpha();

        handleKeys(ctx);
    }

    void handleKeys(PageContext& ctx) {
        bool ctrl = false;
        for (int i = 0; i < kPageCount; ++i) {
            if (ui.consumeKey('1' + i, &ctrl)) {
                if (ctrl && !state.searchOpen && !state.onboardingOpen) ctx.navigate(static_cast<Page>(i));
            }
        }
        if (!state.searchOpen && !state.onboardingOpen) {
            if (ui.consumeKey('K', &ctrl) && ctrl) openSearch();
            if (!ui.focus && !ui.captureKey) {
                if (ui.consumeKey(VK_OEM_2)) openSearch();   // "/"
                if (ui.consumeKey(VK_BACK)) ctx.back();
            }
        }
    }

    void requestClose() {
        if (!open || closing) return;
        closing = true;
        closingAt = ui.time;
        ui.focus = 0;
        ui.popup = 0;
        ui.captureKey = 0;
        state.searchOpen = false;
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
    Impl& d = *d_;
    if (d.open && !d.closing) return;
    d.open = true;
    d.closing = false;
    d.closeDone = false;
    d.openedAt = d.ui.time;
    d.ui.in = InputState{};
    // Opening while a detection notification is on screen investigates that player.
    const std::string focus = d.hud.focusCandidate(d.ui.time);
    if (!focus.empty() && d.engine) {
        PageContext ctx = d.context();
        ctx.openPlayer(focus, ProfileTab::Summary);
    }
    d.hud.clear();
    if (d.onboarded && !d.onboarded->b()) {
        d.state.onboardingOpen = true;
        d.state.onboardingStep = 0;
        d.state.onboardingStepAt = d.ui.time;
    }
}

void App::requestClose() { d_->requestClose(); }

void App::forceClose() {
    d_->open = false;
    d_->closing = false;
    d_->closeDone = true;
}

bool App::isOpen() const { return d_->open; }
bool App::closeFinished() const { return d_->closeDone; }
void App::acknowledgeClosed() { d_->closeDone = false; }

void App::focusPlayer(const std::string& key) {
    PageContext ctx = d_->context();
    ctx.openPlayer(key, ProfileTab::Evidence);
}

void App::openTo(const std::string& page, const std::string& playerKey, const std::string& tab, const std::string& section) {
    PageContext ctx = d_->context();
    if (!playerKey.empty()) {
        const ProfileTab pt = tab == "evidence" ? ProfileTab::Evidence : tab == "behavior" ? ProfileTab::Behavior
                            : tab == "identity" ? ProfileTab::Identity : ProfileTab::Summary;
        ctx.openPlayer(playerKey, pt);
        return;
    }
    for (int i = 0; i < kPageCount; ++i) {
        std::string name = pageTitle(static_cast<Page>(i));
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (name == page) ctx.navigate(static_cast<Page>(i));
    }
    if (!section.empty()) {
        for (int i = 0; i < settingsSectionCount(); ++i) {
            std::string name = settingsSectionName(i);
            for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (name == section) { ctx.navigate(Page::Settings); d_->state.settingsSection = i; }
        }
    }
}

void App::openSearch(const std::string& query) {
    d_->openSearch();
    d_->state.searchQuery = query;
}

void App::showActivation() { d_->pendingActivation = true; }

void App::notify(const std::string& title, const std::string& body) { d_->hud.notify(title, body, d_->ui.time); }

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

bool App::textInputActive() const {
    const Impl& d = *d_;
    return d.ui.focus != 0 || d.ui.captureKey != 0 || d.ui.popup != 0 || d.state.searchOpen || d.state.onboardingOpen;
}

void App::render(int width, int height, double nowMs, bool inGameHud) {
    Impl& d = *d_;
    if (!d.ready || !d.engine) return;
    const double nowSec = nowMs / 1000.0;
    if (d.pendingActivation) {
        d.pendingActivation = false;
        d.hud.showActivation(nowSec);
    }
    if (!d.open && d.hud.idle(nowSec)) {
        // Normal play with nothing to show: collect new alerts without touching any GL state.
        d.ui.time = nowSec;
        d.pullAlerts(inGameHud);
        if (d.hud.idle(nowSec)) return;
    }
    d.applySettings();
    d.scale = std::clamp(height / 920.f, 0.8f, 1.8f) * d.uiScale->f();
    d.ui.r.begin(width, height, d.scale, !g_previewFramebuffer);
    d.ui.fonts.setScale(d.scale);
    d.ui.beginFrame(nowSec);
    d.ui.flashKey = d.state.focusSetting;
    d.ui.flashAt = d.state.focusAt;
    d.pullAlerts(inGameHud);
    if (d.open) {
        d.drawMenu();
    } else {
        Hud::Context hc{d.ui, *d.engine, d.host, nowSec, d.toastSeconds->f(), d.toastPosition->i(), keyName(d.menuKey->i()), inGameHud};
        d.hud.draw(hc);
    }
    // Escape closes the innermost layer: popup → field → overlay → menu.
    if (d.open && d.ui.consumeKey(VK_ESCAPE)) {
        if (d.ui.popup) d.ui.popup = 0;
        else if (d.ui.focus && !d.state.searchOpen) d.ui.focus = 0;
        else if (d.state.searchOpen) { d.state.searchOpen = false; d.ui.focus = 0; }
        else if (d.state.onboardingOpen) { d.state.onboardingOpen = false; d.onboarded->set(1.0); }
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
