#include "runtime.h"

#include "../core/log.h"
#include "../core/paths.h"
#include "../core/settings.h"
#include "../identity/identity.h"
#include "../platform/hooks.h"
#include "../ui/suspicion.h"
#include "../ui/widgets.h"
#include "alerts.h"
#include "jvm.h"
#include "mc.h"
#include "minecraft_util.h"
#include "world_render.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>

namespace vigil::game {

double nowMs() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
}

Runtime& Runtime::instance() {
    static Runtime r;
    return r;
}

void Runtime::registerSettings() {
    auto& S = Settings::instance();
    auto grp = [](Setting& s, const char* g, bool adv = false) -> Setting& { s.group = g; s.advanced = adv; return s; };

    alertChat_ = &grp(S.addBool("alerts.chat", "Chat alerts", true,
                                "Print a line in chat when a detection fires. Hover it to read the evidence."), "Alerts");
    alertClick_ = &grp(S.addBool("alerts.clickReport", "Click to report", true,
                                 "Clicking a chat alert runs /wdr for that player. When off, clicking opens the player in Vigil."),
                       "Alerts");
    alertSound_ = &grp(S.addBool("alerts.sound", "Alert sound", true, "Play a soft chime when a detection fires."), "Alerts");
    alertToast_ = &grp(S.addBool("alerts.toast", "On-screen notifications", true,
                                 "Show a notification card when a detection fires. Pressing the menu key while it is "
                                 "visible opens that player."), "Alerts");
    highlight_ = &grp(S.addBool("highlight.enabled", "Suspect Highlight", true,
                                "Outline players whose suspicion is above the threshold, in the same colors as their halo. "
                                "Drawn by Vigil; nametags, teams and the player list are never changed."),
                      "Suspect Highlight");
    highlightAt_ = &grp(S.addFloat("highlight.threshold", "Highlight threshold", 50.0, {10.0, 100.0, 1.0, 0, "%"},
                                   "Overall suspicion at which a player is outlined."), "Suspect Highlight");
    highlightConfirmed_ = &grp(S.addBool("highlight.confirmed", "Highlight confirmed players", true,
                                         "Outline players you confirmed, in your verdict color, whatever Vigil's confidence."),
                               "Suspect Highlight");
    highlightMarkers_ = &grp(S.addBool("highlight.markers", "Over-head markers", true,
                                       "A small marker above highlighted players: the confidence ring and a shape for the "
                                       "tier (\xE2\x97\x87 elevated, \xE2\x97\x86 high, \xE2\x96\xB2 critical) or your verdict."),
                             "Suspect Highlight");
    grp(S.addInt("highlight.thickness", "Outline width", 2, {1, 6, 1, 0, "px"}, "Line width of the outline."),
        "Suspect Highlight", true);
    highlightFill_ = &grp(S.addBool("highlight.fill", "Tinted volume", true, "Fill the outlined box with a faint tint."),
                          "Suspect Highlight", true);
    anyServer_ = &grp(S.addBool("general.anyServer", "Run outside Hypixel", false,
                                "Allow Vigil to observe on servers other than hypixel.net. Game-context detection is "
                                "tuned for Hypixel's scoreboards."),
                      "Where checks run", true);
    anyServer_->warning = "Other servers use different scoreboards and mechanics; contexts may be misread.";
    anyServer_->warnWhen = [](const Setting& s) { return s.b(); };
}

void Runtime::initialize(const std::string& dllDirectory, HWND gameWindow) {
    if (initialized_) return;
    dllDir_ = dllDirectory;
    hwnd_ = gameWindow;
    startedMs_ = nowMs();
    dataDir_ = paths::dataDirectory(dllDir_);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(dataDir_) / "logs", ec);
    log::init((std::filesystem::path(dataDir_) / "logs").string());
    log::write("vigil", "Vigil starting");
    // 2.0 kept its data next to the DLL; carry it over once.
    if (paths::migrateLegacyData((std::filesystem::path(dllDir_) / "vigil").string(), dataDir_)) {
        log::write("vigil", "migrated settings and history from the previous data folder");
    }

    engine_.initialize();
    registerSettings();
    // UI, utility and identity settings are registered by the UI; do it before loading.
    ui::App::instance().initialize(this, &engine_);
    menuKey_ = Settings::instance().find("ui.menuKey");

    auto& S = Settings::instance();
    const std::string cfg = (std::filesystem::path(dataDir_) / "config.json").string();
    S.setPath(cfg);
    if (!S.load()) {
        // One-time migration of the legacy toggles file (detection toggles only; no API keys).
        const std::string legacy = (std::filesystem::path(dllDir_) / "vigil_toggles.json").string();
        const int n = S.importLegacy(legacy, {
            {"anticheat", "general.monitoring"},
            {"ac_flagsound", "alerts.sound"},
            {"ignoreTeammates", "exempt.teammates"},
            {"ac_killaura", "checks.killaura.enabled"},
            {"ac_bhop", "checks.bhop.enabled"},
            {"ac_autoblock", "checks.autoblock.enabled"},
            {"ac_noslow", "checks.noslow.enabled"},
            {"ac_scaffold", "checks.keepy.enabled"},
            {"ac_scaffoldb", "checks.flatscaff.enabled"},
            {"ac_legitscaff", "checks.legitscaff.enabled"},
            {"autoWho", "utilities.autoWho"},
            {"fastBuy", "utilities.fastBuy"},
            {"partyDetector", "utilities.partyChat"},
        });
        if (n > 0) log::writef("vigil", "migrated %d legacy settings", n);
        S.save();
    }
    engine_.setHistoryPath((std::filesystem::path(dataDir_) / "history" / "players.json").string());
    engine_.setMarksPath((std::filesystem::path(dataDir_) / "history" / "marks.json").string());
    engine_.setJournalDirectory((std::filesystem::path(dataDir_) / "sessions").string());
    engine_.resetSession(nowMs());
    initialized_ = true;
}

bool Runtime::connectedToHypixel() const { return VigilIsHypixelServerActive(); }

std::string Runtime::signalSummary() const {
    const SignalStatus& s = sampler_.signals();
    if (!sampler_.resolved()) return "Waiting for the game world";
    std::string missing;
    auto add = [&](bool ok, const char* what) {
        if (ok) return;
        if (!missing.empty()) missing += ", ";
        missing += what;
    };
    add(s.serverPositions, "server positions");
    add(s.headRotation, "head rotation");
    add(s.moveAttribute, "movement attribute");
    add(s.potions, "potion effects");
    add(s.blocks, "block probes");
    add(s.heldItem, "held items");
    add(s.swing, "arm swings");
    add(s.hurt, "hurt animations");
    return missing.empty() ? "All signals available" : "Unavailable: " + missing;
}

ui::RuntimeStatus Runtime::runtimeStatus() const {
    ui::RuntimeStatus st;
    st.hosted = true;
    st.uptimeSec = (nowMs() - startedMs_) / 1000.0;
    st.jvmAttached = g_jvm != nullptr;
    st.clientPump = scheduledRate_ > 0.5;
    st.pumpsPerSecond = pumpRate_;
    st.presentSlots = platform::presentHookSlots();
    st.presentHook = st.presentSlots > 0;
    st.framesPerSecond = frameRate_;
    st.inputHook = platform::windowHookActive();
    st.menuHost = menuHostReady();
    st.worldHookWanted = highlight_ && highlight_->b() && worldHookNeeded_;
    st.worldHook = IsRenderGlobalHookInstalled();
    st.samplerResolved = sampler_.resolved();
    if (sampler_.resolved()) {
        const SignalStatus& s = sampler_.signals();
        st.signals = {
            {"Server positions", s.serverPositions, "Speed, BHop, Reach, KillAura"},
            {"Head rotation", s.headRotation, "KillAura"},
            {"Movement attribute", s.moveAttribute, "Speed, BHop, NoSlow"},
            {"Potion effects", s.potions, "Speed, BHop"},
            {"Block probes", s.blocks, "Speed, BHop, Velocity, Scaffold"},
            {"Held items", s.heldItem, "AutoBlock, NoSlow, Scaffold"},
            {"Arm swings", s.swing, "AutoClicker, KillAura, Reach"},
            {"Hurt animations", s.hurt, "Velocity, hit attribution"},
            {"Player list", s.tabList, "NPC and bot filtering"},
            {"Player latency", s.latency, "Velocity response window"},
            {"Player skins", s.skins, "player faces"},
        };
    }
    st.hypixel = hypixel_;
    st.server = serverHost_;
    st.chatReader = chat_.available();
    st.chatLines = chat_.linesRead();
    st.dataDir = dataDir_;
    return st;
}

bool Runtime::playerFace(const std::string& playerKey, ui::FaceTexture& out) {
    unsigned int tex = 0;
    if (!sampler_.faceTexture(playerKey, tex)) return false;
    out.texture = tex;
    return true;
}

void Runtime::reportPlayer(const std::string& name) {
    std::lock_guard<std::mutex> lk(inputMu_);
    if (IsValidIgn(name)) pendingReports_.push_back(name);
}

void Runtime::openFolder(const std::string& path) {
    ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void Runtime::newSession() {
    std::lock_guard<std::mutex> lk(inputMu_);
    newSessionRequested_ = true;
}

void Runtime::ensureHooks(JNIEnv* env, jobject mc) {
    const double now = nowMs();
    if (!highlight_->b() || IsRenderGlobalHookInstalled()) return;
    // Compatibility: the RenderGlobal swap is installed only once a player actually needs an
    // outline (most sessions never do), never at startup.
    if (!worldHookNeeded_) {
        const float threshold = highlightAt_->f();
        for (const auto& r : engine_.session().players()) {
            if (!r.present || r.local) continue;
            if (r.suspicion >= threshold || (highlightConfirmed_->b() && engine_.marks().confirmed(r.key))) {
                worldHookNeeded_ = true;
                log::writef("runtime", "world highlight needed (%s); installing the RenderGlobal hook", r.name.c_str());
                break;
            }
        }
        if (!worldHookNeeded_) return;
    }
    if (now - lastHookTryMs_ < 2000.0) return;
    lastHookTryMs_ = now;
    TryInstallRenderGlobalHook(env, mc, g_injectClassLoaderGlobal);
}

void Runtime::updateRates(double now) {
    if (rateWindowStart_ <= 0.0) rateWindowStart_ = now;
    const double span = now - rateWindowStart_;
    if (span >= 1000.0) {
        pumpRate_ = pumpsInWindow_ * 1000.0 / span;
        frameRate_ = framesInWindow_ * 1000.0 / span;
        const int scheduled = scheduledPumps_.load();
        scheduledRate_ = (scheduled - scheduledAtWindowStart_) * 1000.0 / span;
        scheduledAtWindowStart_ = scheduled;
        pumpsInWindow_ = framesInWindow_ = 0;
        rateWindowStart_ = now;
    }
}

void Runtime::readChat(JNIEnv* env, jobject mc, double now) {
    if (now - lastChatMs_ < 50.0) return;
    lastChatMs_ = now;
    for (const std::string& line : chat_.poll(env, mc)) {
        utilities_.onChat(line, context_.bedwarsPregame(), hypixel_, now);
        identity::Service::instance().onChatLine(line);
        // The match tracker reads end-of-game announcements from the next frame.
        if (pendingChat_.size() < 64) pendingChat_.push_back(line);
    }
}

void Runtime::pump(JNIEnv* env, jobject mc) {
    if (!initialized_ || !env || !mc || inPump_) return;
    const double now = nowMs();
    if (now - lastPumpMs_ < 15.0) return;
    lastPumpMs_ = now;
    inPump_ = true;
    ++pumpsInWindow_;
    updateRates(now);

    jclass mcClass = env->GetObjectClass(mc);
    VigilUpdateHypixelServerWhitelist(env, mc, mcClass);
    static double lastHostRead = 0.0;
    if (now - lastHostRead > 2000.0) {
        lastHostRead = now;
        std::string host;
        serverHost_ = ReadMinecraftServerHost(env, mc, mcClass, host) ? host : std::string();
    }
    env->DeleteLocalRef(mcClass);
    jniFailed(env);
    hypixel_ = VigilIsHypixelServerActive();

    const bool allowed = hypixel_ || anyServer_->b();
    if (allowed) context_.poll(env, mc, now);
    readChat(env, mc, now);

    detection::WorldFrame frame;
    bool worldChanged = false;
    const bool sampled = sampler_.sample(env, mc, frame, worldChanged);
    if (worldChanged) {
        engine_.onWorldChanged();
        utilities_.onWorldChanged();
        identity::Service::instance().resetGame();
    }
    if (sampled) {
        frame.timeMs = now;
        frame.context = allowed ? context_.context() : detection::GameContext::Unknown;
        frame.contextDetail = allowed ? context_.detail() : std::string();
        frame.chat.swap(pendingChat_);
        engine_.onFrame(frame);
        utilities_.onSpawns(sampler_.takeSpawns(), now);
        announceMatch(env, mc, now);
    }
    utilities_.update(env, mc, context_, hypixel_, chat_.available(), now);
    for (const std::string& a : identity::Service::instance().takeAnnouncements()) {
        const auto bar = a.find('|');
        if (bar == std::string::npos) continue;
        printChat(env, mc, chatPrefix() + "\xC2\xA7" "f" + a.substr(0, bar) + " \xC2\xA7" "8\xE2\x86\x92 \xC2\xA7" "a" + a.substr(bar + 1) +
                               " \xC2\xA7" "7(likely; see /vigil " + a.substr(0, bar) + ")");
    }

    {
        bool reset = false;
        {
            std::lock_guard<std::mutex> lk(inputMu_);
            std::swap(reset, newSessionRequested_);
        }
        if (reset) {
            engine_.resetSession(now);
            alertSeq_ = engine_.alertSeq();
            printChat(env, mc, chatPrefix() + "\xC2\xA7" "7Started a new session.");
        }
    }
    if (activationChatPending_.exchange(false)) {
        const std::string key = menuKey_ ? ui::keyName(menuKey_->i()) : std::string("Right Shift");
        printChat(env, mc, chatPrefix() + "\xC2\xA7" "aActive. \xC2\xA7" "7Press \xC2\xA7" "f" + key +
                               "\xC2\xA7" "7 or type \xC2\xA7" "f/vigil\xC2\xA7" "7 to open Vigil.");
    }

    dispatchAlerts(env, mc);
    handleMenu(env, mc);
    ensureHooks(env, mc);
    Settings::instance().tick(now);
    jniFailed(env);
    inPump_ = false;
}

void Runtime::dispatchAlerts(JNIEnv* env, jobject mc) {
    for (const detection::Alert& a : engine_.alerts()) {
        if (a.seq <= alertSeq_) continue;
        alertSeq_ = a.seq;
        const auto& checks = engine_.checks();
        const std::string checkName = (a.check >= 0 && a.check < static_cast<int>(checks.size())) ? checks[a.check]->name() : "Check";
        if (alertChat_->b()) {
            ChatMarks cm;
            cm.glyph = ui::tierGlyph(ui::tierOf(a.confidence));
            cm.confirmed = engine_.marks().confirmed(a.playerKey);
            cm.watched = engine_.marks().watched(a.playerKey);
            if (!printJsonChat(env, mc, buildAlertJson(a, checkName, alertClick_->b(), cm))) {
                printChat(env, mc, buildAlertLegacy(a, checkName, cm));
            }
        }
        if (alertSound_->b()) playSound(env, mc, "note.pling", 1.6f);
    }
}

// Match briefing in chat (Settings → Alerts → Match briefing: "Screen and chat"). The on-screen
// card is drawn by the UI; both use ui::buildBriefing, three seconds into the match.
void Runtime::announceMatch(JNIEnv* env, jobject mc, double /*now*/) {
    const auto& m = engine_.match();
    if (m.matchCount() != briefedMatch_) {
        briefedMatch_ = m.matchCount();
        briefingChatDueMs_ = m.matchStartedMs() + 3000.0;
    }
    if (briefingChatDueMs_ <= 0.0 || engine_.lastTimeMs() < briefingChatDueMs_) return;
    briefingChatDueMs_ = -1.0;
    const Setting* mode = Settings::instance().find("alerts.briefing");
    if (!mode || mode->i() != 2 || !m.inMatch()) return;
    const ui::Briefing b = ui::buildBriefing(engine_);
    if (b.entries.empty()) return;
    std::string line = chatPrefix() + "\xC2\xA7" "7Match briefing \xC2\xB7 " + std::to_string(b.players) + " players:";
    const std::size_t shown = std::min<std::size_t>(4, b.entries.size());
    for (std::size_t i = 0; i < shown; ++i) {
        const ui::BriefingEntry& e = b.entries[i];
        const std::string shape = e.marks.confirmed ? "\xC2\xA7" "d" + std::string(ui::kGlyphConfirmed)
                                : e.marks.watched ? "\xC2\xA7" "b" + std::string(ui::kGlyphWatching)
                                : e.marks.nicked && e.priority == 4 ? "\xC2\xA7" "5" + std::string(ui::kGlyphNicked)
                                                                    : "\xC2\xA7" "e" + std::string(ui::tierGlyph(ui::Tier::High));
        line += (i ? " \xC2\xA7" "8\xC2\xB7 " : " ") + shape + " \xC2\xA7" "f" + e.name + " \xC2\xA7" "8(" + e.reason + ")";
    }
    if (b.entries.size() > shown) line += " \xC2\xA7" "7+" + std::to_string(b.entries.size() - shown);
    printChat(env, mc, line);
}

void Runtime::handleMenu(JNIEnv* env, jobject mc) {
    ui::App& app = ui::App::instance();
    jobject screen = currentScreen(env, mc);
    const bool chat = screen && isChatScreen(env, screen);
    screenOpen_ = screen != nullptr;
    chatOpen_ = chat;
    if (chat) {
        const std::string text = chatInputText(env, screen);
        std::lock_guard<std::mutex> lk(inputMu_);
        chatInput_ = text;
    }
    menuOnScreen_ = screen && menuScreen_ && env->IsSameObject(screen, menuScreen_);

    // Commands typed in chat.
    std::string command;
    std::deque<std::string> reports;
    {
        std::lock_guard<std::mutex> lk(inputMu_);
        command.swap(pendingCommand_);
        reports.swap(pendingReports_);
    }
    if (!command.empty()) {
        if (chat) displayScreen(env, mc, nullptr);
        addToSentHistory(env, mc, command);
        runCommand(env, mc, command);
    }
    for (const std::string& name : reports) {
        sendChatMessage(env, mc, "/wdr " + name);
    }

    const unsigned openReason = openRequest_.exchange(0);
    if (openReason != 0 && !menuOnScreen_) {
        log::write("runtime", openReason == 1 ? "menu open request: key binding" : "menu open request: chat command");
        jobject blank = newBlankScreen(env);
        if (!blank) {
            log::write("runtime", "menu open failed: could not create the host screen");
            printChat(env, mc, chatPrefix() + "\xC2\xA7" "cVigil could not open its menu. Open Status with /vigil status after reopening.");
        } else if (displayScreen(env, mc, blank)) {
            jobject retainedScreen = env->NewGlobalRef(blank);
            if (!retainedScreen || env->ExceptionCheck()) {
                if (env->ExceptionCheck()) jniFailed(env);
                log::write("runtime", "menu open failed: NewGlobalRef(host screen) returned null");
                displayScreen(env, mc, nullptr);
                printChat(env, mc, chatPrefix() + "\xC2\xA7" "cVigil could not retain its menu screen. See the runtime log.");
            } else {
                if (menuScreen_) env->DeleteGlobalRef(menuScreen_);
                menuScreen_ = retainedScreen;
                menuOnScreen_ = true;
                app.open();
                log::write("runtime", "menu opened successfully");
                if (!focusRequest_.empty()) {
                    app.focusPlayer(focusRequest_);
                    focusRequest_.clear();
                }
                if (!openPage_.empty()) {
                    app.openTo(openPage_);
                    openPage_.clear();
                }
                if (searchRequested_) {
                    app.openSearch(searchRequest_);
                    searchRequested_ = false;
                    searchRequest_.clear();
                }
            }
        } else {
            log::write("runtime", "menu open failed: host rejected the screen");
            printChat(env, mc, chatPrefix() + "\xC2\xA7" "cVigil could not open its menu. See the runtime log.");
        }
        if (blank) env->DeleteLocalRef(blank);
    }

    if (app.isOpen() && !menuOnScreen_ && menuScreen_) {
        // Something else (inventory, server GUI, ESC handled by the game) replaced the menu.
        log::write("runtime", "menu host screen is no longer current; closing Vigil UI");
        app.forceClose();
    }
    if (app.closeFinished()) {
        if (menuOnScreen_ && !displayScreen(env, mc, nullptr)) {
            log::write("runtime", "menu close failed: Minecraft did not accept a null screen");
        }
        app.acknowledgeClosed();
        if (menuScreen_) { env->DeleteGlobalRef(menuScreen_); menuScreen_ = nullptr; }
        menuOnScreen_ = false;
    }
    menuVisible_ = app.isOpen();
    if (screen) env->DeleteLocalRef(screen);
}

void Runtime::runCommand(JNIEnv* env, jobject mc, const std::string& raw) {
    std::string args = Trim(raw.size() > 6 ? raw.substr(6) : "");
    const std::string lower = LowerAscii(args);
    static const char* kPages[] = {"overview", "players", "checks", "utilities", "settings", "status"};
    if (lower.empty()) {
        openRequest_ = 2;
        return;
    }
    if (lower == "help") {
        printChat(env, mc, chatPrefix() + "\xC2\xA7" "f/vigil \xC2\xA7" "7open  \xC2\xA7" "f/vigil <player> \xC2\xA7" "7investigate  \xC2\xA7"
                           "f/vigil search <text>  \xC2\xA7" "f/vigil status  \xC2\xA7" "f/vigil reset \xC2\xA7" "7new session");
        return;
    }
    if (lower == "reset") {
        newSession();
        return;
    }
    for (const char* p : kPages) {
        if (lower == p) {
            openPage_ = p;
            openRequest_ = 2;
            return;
        }
    }
    if (lower.rfind("search", 0) == 0) {
        searchRequest_ = Trim(args.substr(6));
        searchRequested_ = true;
        openRequest_ = 2;
        return;
    }
    focusRequest_.clear();
    for (const auto& r : engine_.session().players()) {
        if (LowerAscii(r.name) == lower) { focusRequest_ = r.key; break; }
    }
    if (focusRequest_.empty()) {
        // Not observed this session: search for it (it may be a nick's real name or an old player).
        searchRequest_ = args;
        searchRequested_ = true;
    }
    openRequest_ = 2;
}

void Runtime::onRenderEntities(JNIEnv* env, jobject mc, jclass /*mcClass*/) {
    // Draw only: pumping here could open or close screens in the middle of the world pass. The
    // scheduled task and the present hook already pump every frame.
    if (!initialized_ || !highlight_ || !highlight_->b()) return;
    const float threshold = highlightAt_->f();
    const bool fill = highlightFill_->b();
    const bool confirmedToo = highlightConfirmed_->b();
    const bool markers = highlightMarkers_->b();
    // The same language as the halo: the active theme's severity color for Vigil's confidence, the
    // verdict color for players the user confirmed. NPCs have no records, so they are never drawn.
    const ui::Theme& th = ui::theme();
    auto rgbOf = [](const ui::Color& c) {
        return (static_cast<int>(c.r * 255.f + 0.5f) << 16) | (static_cast<int>(c.g * 255.f + 0.5f) << 8) | static_cast<int>(c.b * 255.f + 0.5f);
    };
    RenderSuspectHighlights(env, mc, [&](int entityId, HighlightStyle& s) {
        for (const auto& r : engine_.session().players()) {
            if (r.entityId != entityId || !r.present || r.local) continue;
            const bool confirmed = confirmedToo && engine_.marks().confirmed(r.key);
            if (r.suspicion < threshold && !confirmed) return false;
            const ui::Tier tier = ui::tierOf(r.suspicion);
            // Outline: the verdict color for a confirmed player, otherwise Vigil's severity color.
            // Marker ring: always Vigil's confidence (as on the halo); the glyph shows the verdict.
            s.rgb = confirmed ? rgbOf(th.verdict) : rgbOf(th.severity(r.suspicion));
            s.ringRgb = rgbOf(th.severity(r.suspicion));
            const float k = std::clamp((r.suspicion - threshold) / std::max(1.f, 100.f - threshold), 0.f, 1.f);
            s.fillAlpha = fill ? 0.06f + 0.08f * k : 0.f;
            s.marker = markers;
            s.tier = tier == ui::Tier::Critical ? 3 : tier == ui::Tier::High ? 2 : tier == ui::Tier::Elevated ? 1 : 0;
            s.confidence = r.suspicion;
            s.confirmed = confirmed;
            s.verdictRgb = rgbOf(th.verdict);
            return true;
        }
        return false;
    });
}

void Runtime::signalLauncher() {
    // Vigil.exe waits on this event so it can report success only once Vigil is really running.
    wchar_t name[64];
    swprintf_s(name, L"Local\\Vigil.Ready.%lu", GetCurrentProcessId());
    if (HANDLE e = OpenEventW(EVENT_MODIFY_STATE, FALSE, name)) {
        SetEvent(e);
        CloseHandle(e);
    }
}

void Runtime::onSwapBuffers(HDC hdc) {
    if (!initialized_ || !hdc) return;
    HWND wnd = WindowFromDC(hdc);
    HWND renderWindow = wnd ? wnd : hwnd_;
    if (!renderWindow) return;
    if (!hwnd_) hwnd_ = renderWindow;
    RECT rc{};
    if (!GetClientRect(renderWindow, &rc)) return;
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    ++framesInWindow_;
    if (!launcherSignaled_) {
        // The first frame proves the render path works: confirm in game and to the launcher.
        launcherSignaled_ = true;
        log::writef("runtime", "UI render path active (window=%p, size=%dx%d)", renderWindow, w, h);
        log::write("vigil", "Vigil ready");
        ui::App::instance().showActivation();
        activationChatPending_ = true;
        signalLauncher();
    }
    ui::App::instance().render(w, h, nowMs(), alertToast_->b());
    const bool visible = ui::App::instance().isOpen();
    static bool lastRenderedMenuState = false;
    if (visible != lastRenderedMenuState) {
        log::writef("runtime", "menu render state changed: %s (%dx%d)", visible ? "visible" : "hidden", w, h);
        lastRenderedMenuState = visible;
    }
    menuVisible_ = visible;
}

static int resolveVirtualKey(WPARAM wParam, LPARAM lParam) {
    const UINT scan = (lParam >> 16) & 0xFF;
    const bool extended = (lParam >> 24) & 1;
    switch (wParam) {
        case VK_SHIFT: return static_cast<int>(MapVirtualKeyA(scan, MAPVK_VSC_TO_VK_EX));
        case VK_CONTROL: return extended ? VK_RCONTROL : VK_LCONTROL;
        case VK_MENU: return extended ? VK_RMENU : VK_LMENU;
        default: return static_cast<int>(wParam);
    }
}

bool Runtime::onWindowMessage(HWND, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result) {
    if (!initialized_) return false;
    ui::App& app = ui::App::instance();
    const bool visible = menuVisible_.load();
    result = 0;
    switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            const int vk = resolveVirtualKey(wParam, lParam);
            if (visible) {
                if (vk == VK_F11 || vk == VK_F2) return false;  // fullscreen / screenshot keep working
                if (vk == VK_ESCAPE && !app.textInputActive()) { app.requestClose(); return true; }
                app.onKey(vk, true, (GetKeyState(VK_CONTROL) & 0x8000) != 0, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
                return true;
            }
            bool screenOpen = screenOpen_.load();
            const bool repeat = (lParam & (1 << 30)) != 0;
            if (menuKey_ && vk == menuKey_->i()) {
                // Re-check the screen right now: the cached state can be one pump old.
                if (JNIEnv* env = currentEnv()) {
                    if (jobject mc = minecraft(env)) {
                        if (jobject screen = currentScreen(env, mc)) {
                            screenOpen = true;
                            env->DeleteLocalRef(screen);
                        } else {
                            screenOpen = false;
                        }
                        env->DeleteLocalRef(mc);
                    }
                    jniFailed(env);
                }
            }
            if (menuKey_ && vk == menuKey_->i() && !screenOpen) {
                openRequest_ = 1;
                log::write("runtime", "menu key received");
                return true;
            }
            // Utility shortcuts (only while playing, never while typing in a screen).
            if (!screenOpen && !repeat) {
                if (Setting* toggled = utilities_.onHotkey(vk)) {
                    const auto& us = utilities::settings();
                    const utilities::Kind kind = toggled == us.autoWho ? utilities::Kind::AutoWho
                                               : toggled == us.fastBuy ? utilities::Kind::FastBuy
                                               : toggled == us.partyChat ? utilities::Kind::PartyChat : utilities::Kind::PartyQueue;
                    app.notify(toggled->label + (toggled->b() ? " on" : " off"), ui::keyName(vk));
                    utilities::Activity::instance().add(kind, toggled->label + (toggled->b() ? " turned on" : " turned off") +
                                                                  " with its shortcut");
                    return true;
                }
            }
            if (vk == VK_RETURN) {
                std::string text;
                bool chat = chatOpen_.load();
                if (JNIEnv* env = currentEnv()) {
                    if (jobject mc = minecraft(env)) {
                        jobject screen = currentScreen(env, mc);
                        chat = screen && isChatScreen(env, screen);
                        if (chat) text = chatInputText(env, screen);
                        if (screen) env->DeleteLocalRef(screen);
                        env->DeleteLocalRef(mc);
                    }
                    jniFailed(env);
                }
                if (!chat) return false;
                std::lock_guard<std::mutex> lk(inputMu_);
                if (text.empty()) text = chatInput_;
                const std::string lower = LowerAscii(Trim(text));
                if (lower == "/vigil" || lower.rfind("/vigil ", 0) == 0) {
                    pendingCommand_ = Trim(text);
                    log::write("runtime", "received /vigil command from chat");
                    return true;
                }
            }
            return false;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (visible) {
                app.onKey(resolveVirtualKey(wParam, lParam), false, false, false);
                return true;
            }
            return false;
        case WM_CHAR:
            if (visible) {
                app.onChar(static_cast<unsigned int>(wParam));
                return true;
            }
            return false;
        case WM_MOUSEMOVE:
            if (visible) { app.onMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return true; }
            return false;
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK:
            if (visible) {
                const int button = (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_LBUTTONDBLCLK) ? 0
                                 : (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP || msg == WM_RBUTTONDBLCLK) ? 1 : 2;
                const bool down = msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN ||
                                  msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONDBLCLK;
                app.onMouseButton(button, down, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                return true;
            }
            return utilities_.onMouse(msg);
        case WM_MOUSEWHEEL:
            if (visible) {
                app.onMouseWheel(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA);
                return true;
            }
            return false;
        case WM_SETCURSOR:
            if (visible && LOWORD(lParam) == HTCLIENT) {
                LPCSTR id = IDC_ARROW;
                switch (app.cursor()) {
                    case ui::Cursor::Hand: id = IDC_HAND; break;
                    case ui::Cursor::IBeam: id = IDC_IBEAM; break;
                    case ui::Cursor::Resize: id = IDC_SIZEWE; break;
                    default: break;
                }
                SetCursor(LoadCursorA(nullptr, id));
                result = TRUE;
                return true;
            }
            return false;
        default:
            return false;
    }
}

void Runtime::shutdown(JNIEnv* env) {
    if (!initialized_) return;
    engine_.flushHistory();
    Settings::instance().save();
    identity::Service::instance().stop();
    if (env && menuScreen_) {
        env->DeleteGlobalRef(menuScreen_);
        menuScreen_ = nullptr;
    }
    sampler_.reset(env);
    chat_.reset(env);
    log::write("vigil", "Vigil stopped");
}

}  // namespace vigil::game
