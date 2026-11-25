#pragma once
// Vigil game layer — runtime orchestration. Replaces the legacy PumpMinecraftClientLogic god
// function with a small, explicit pipeline:
//   pump: whitelist → context → sample → engine → alerts → commands → menu lifecycle
//   swap: UI render        wndproc: input routing

#include "../detection/engine.h"
#include "../ui/app.h"
#include "context.h"
#include "sampler.h"

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

private:
    Runtime() = default;
    void registerSettings();
    void dispatchAlerts(JNIEnv* env, jobject mc);
    void handleMenu(JNIEnv* env, jobject mc);
    void runCommand(JNIEnv* env, jobject mc, const std::string& raw);
    void ensureHooks(JNIEnv* env, jobject mc);

    std::atomic<bool> initialized_{false};  // set on the bootstrap thread, read on the game thread
    std::string dllDir_;
    std::string dataDir_;
    HWND hwnd_ = nullptr;

    detection::Engine engine_;
    Sampler sampler_;
    ContextDetector context_;
    std::uint64_t alertSeq_ = 0;
    double lastPumpMs_ = 0.0;
    double lastHookTryMs_ = 0.0;
    bool inPump_ = false;

    // Menu / input state (Minecraft thread).
    jobject menuScreen_ = nullptr;   // global ref to the blank GuiScreen hosting the menu
    bool menuOnScreen_ = false;
    std::atomic<bool> requestOpen_{false};
    std::atomic<bool> menuVisible_{false};
    std::atomic<bool> screenOpen_{false};
    std::atomic<bool> chatOpen_{false};
    std::mutex inputMu_;
    std::string chatInput_;
    std::string pendingCommand_;
    std::deque<std::string> pendingReports_;
    std::string focusRequest_;
    bool newSessionRequested_ = false;

    Setting* menuKey_ = nullptr;
    Setting* alertChat_ = nullptr;
    Setting* alertSound_ = nullptr;
    Setting* alertClick_ = nullptr;
    Setting* alertToast_ = nullptr;
    Setting* highlight_ = nullptr;
    Setting* highlightAt_ = nullptr;
    Setting* highlightFill_ = nullptr;
    Setting* anyServer_ = nullptr;
};

}  // namespace vigil::game
