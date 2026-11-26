#pragma once
// Vigil game layer — runtime orchestration: a small, explicit pipeline.
//   pump:    whitelist → context → sample → engine → chat → utilities → identity → alerts → menu
//   swap:    UI render (menu, activation card, notifications), launcher handshake
//   wndproc: input routing, shortcuts, Fast Buy

#include "../detection/engine.h"
#include "../detection/trace.h"
#include "../ui/app.h"
#include "chat.h"
#include "context.h"
#include "sampler.h"
#include "utilities_runtime.h"

#include <windows.h>
#include <jni.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <string>

namespace vigil::game {

double nowMs();  // high-resolution monotonic clock

class Runtime final : public ui::Host {
public:
    static Runtime& instance();

    void initialize(const std::string& dllDirectory, HWND gameWindow);
    bool initialized() const { return initialized_.load(); }

    // Minecraft client thread.
    void pump(JNIEnv* env, jobject mc);
    void notePumpSource(bool scheduledTask) { if (scheduledTask) scheduledPumps_.fetch_add(1); }
    void onRenderEntities(JNIEnv* env, jobject mc, jclass mcClass);
    // Render thread (Minecraft's thread on Lunar) — called from the SwapBuffers hook.
    void onSwapBuffers(HDC hdc);
    // Window procedure hook. Returns true when the message was consumed.
    bool onWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result);
    void shutdown(JNIEnv* env);

    detection::Engine& engine() { return engine_; }
    const std::string& dataDir() const { return dataDir_; }

    // ui::Host
    void reportPlayer(const std::string& name) override;
    void openFolder(const std::string& path) override;
    std::string dataDirectory() const override { return dataDir_; }
    bool connectedToHypixel() const override;
    std::string signalSummary() const override;
    void newSession() override;
    ui::RuntimeStatus runtimeStatus() const override;
    bool playerFace(const std::string& playerKey, ui::FaceTexture& out) override;
    ui::TraceInfo traceInfo() const override;
    bool setTraceRecording(bool on) override;

private:
    Runtime() = default;
    void registerSettings();
    void dispatchAlerts(JNIEnv* env, jobject mc);
    void handleMenu(JNIEnv* env, jobject mc);
    void runCommand(JNIEnv* env, jobject mc, const std::string& raw);
    void ensureHooks(JNIEnv* env, jobject mc);
    void readChat(JNIEnv* env, jobject mc, double now);
    void announceMatch(JNIEnv* env, jobject mc, double now);   // match briefing chat line
    void updateRates(double now);
    void signalLauncher();

    std::atomic<bool> initialized_{false};  // set on the bootstrap thread, read on the game thread
    std::string dllDir_;
    std::string dataDir_;
    HWND hwnd_ = nullptr;

    detection::Engine engine_;
    detection::TraceWriter trace_;   // diagnostic recording (Status → Diagnostics), off by default
    std::string lastTracePath_;
    bool pendingWorldChange_ = false;
    Sampler sampler_;
    ContextDetector context_;
    ChatReader chat_;
    UtilitiesRuntime utilities_;
    std::uint64_t alertSeq_ = 0;
    std::vector<std::string> pendingChat_;   // chat lines for the next engine frame
    std::uint64_t briefedMatch_ = 0;         // last match announced in chat
    double briefingChatDueMs_ = -1.0;
    double lastPumpMs_ = 0.0;
    double lastHookTryMs_ = 0.0;
    double lastChatMs_ = 0.0;
    double startedMs_ = 0.0;
    bool inPump_ = false;
    std::string serverHost_;
    bool hypixel_ = false;

    // Health counters (Status page).
    std::atomic<int> scheduledPumps_{0};
    int pumpsInWindow_ = 0, framesInWindow_ = 0;
    double rateWindowStart_ = 0.0;
    double pumpRate_ = 0.0, frameRate_ = 0.0, scheduledRate_ = 0.0;
    int scheduledAtWindowStart_ = 0;

    // Activation and launcher handshake.
    bool launcherSignaled_ = false;
    std::atomic<bool> activationChatPending_{false};

    // Menu / input state (Minecraft thread).
    jobject menuScreen_ = nullptr;   // global ref to VigilMenuScreen hosting the menu
    bool menuOnScreen_ = false;
    std::atomic<unsigned> openRequest_{0};  // 1 = menu key, 2 = chat command
    std::atomic<bool> menuVisible_{false};
    std::atomic<bool> screenOpen_{false};
    std::atomic<bool> chatOpen_{false};
    std::mutex inputMu_;
    std::string chatInput_;
    std::string pendingCommand_;
    std::deque<std::string> pendingReports_;
    std::string focusRequest_;
    std::string openPage_;            // deep link for /vigil <page>
    std::string searchRequest_;       // /vigil search <query>
    bool searchRequested_ = false;
    bool newSessionRequested_ = false;

    Setting* menuKey_ = nullptr;
    Setting* alertChat_ = nullptr;
    Setting* alertSound_ = nullptr;
    Setting* alertClick_ = nullptr;
    Setting* alertToast_ = nullptr;
    Setting* highlight_ = nullptr;
    Setting* highlightAt_ = nullptr;
    Setting* highlightFill_ = nullptr;
    Setting* highlightConfirmed_ = nullptr;
    Setting* highlightMarkers_ = nullptr;
    // The RenderGlobal hook is installed the first time someone needs highlighting, not at startup.
    bool worldHookNeeded_ = false;
    Setting* anyServer_ = nullptr;
};

}  // namespace vigil::game
