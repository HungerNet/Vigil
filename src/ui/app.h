#pragma once
// Vigil UI — the application shell. Drawn natively with OpenGL inside the host's frame, so it is
// independent of Minecraft's GUI system; the host forwards window input while the menu is open.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vigil::detection {
class Engine;
}

namespace vigil::ui {

// A player's face from their skin texture (u/v in texture space). The hat layer is drawn on top.
struct FaceTexture {
    unsigned int texture = 0;
    float u0 = 8.f / 64.f, v0 = 8.f / 64.f, u1 = 16.f / 64.f, v1 = 16.f / 64.f;
    float hu0 = 40.f / 64.f, hv0 = 8.f / 64.f, hu1 = 48.f / 64.f, hv1 = 16.f / 64.f;
};

struct SignalStatus {
    std::string name;
    bool available = false;
    std::string feeds;        // checks that depend on it
};

// Health of the runtime as reported by the host (Status page, rail health card).
struct RuntimeStatus {
    bool hosted = false;               // running inside the game (false in the preview)
    double uptimeSec = 0.0;
    bool jvmAttached = false;
    bool clientPump = false;
    double pumpsPerSecond = 0.0;
    bool presentHook = false;
    int presentSlots = 0;
    double framesPerSecond = 0.0;
    bool inputHook = false;
    bool menuHost = false;             // the screen that hosts the menu could be created
    bool worldHook = false;
    bool worldHookWanted = false;
    bool samplerResolved = false;
    std::vector<SignalStatus> signals;
    bool hypixel = false;
    std::string server;
    bool chatReader = false;
    int chatLines = 0;
    std::string dataDir;
    // Live validation (docs/LIVE_VALIDATION.md).
    std::string runtimeVersion;        // this runtime's version and build
    std::string runtimeBuild;
    int bindingsTotal = 0, bindingsMissing = 0, bindingsObfuscated = 0;
    std::vector<std::string> bindingNotes;   // "missing: isBurning", "posX → s"
};

// Diagnostic trace recording (Status → Diagnostics). Hosts that can't record report !supported.
struct TraceInfo {
    bool supported = false;
    bool active = false;
    std::string path;            // current or last recording
    int frames = 0;
    std::uint64_t bytes = 0;
    std::string stopReason;      // why the last recording stopped
};

// Services the UI needs from its host (the game runtime or the preview harness).
class Host {
public:
    virtual ~Host() = default;
    virtual void reportPlayer(const std::string& name) = 0;     // explicit user action only
    virtual void openFolder(const std::string& path) = 0;
    virtual std::string dataDirectory() const = 0;
    virtual bool connectedToHypixel() const = 0;
    virtual std::string signalSummary() const = 0;              // adapter diagnostics (one line)
    virtual void newSession() = 0;
    virtual RuntimeStatus runtimeStatus() const = 0;
    virtual bool playerFace(const std::string& playerKey, FaceTexture& out) = 0;
    // Diagnostic traces: the frames the engine receives, written to <data>\traces (off by default).
    virtual TraceInfo traceInfo() const { return {}; }
    virtual bool setTraceRecording(bool on) { (void)on; return false; }
};

enum class Cursor { Arrow, Hand, IBeam, Resize };

class App {
public:
    static App& instance();

    // Registers UI, utility and identity settings. Call before Settings::load().
    void initialize(Host* host, detection::Engine* engine);
    bool initialized() const;

    // Menu lifecycle.
    void open();
    void requestClose();          // animated
    void forceClose();            // immediate (screen replaced by the game)
    bool isOpen() const;          // visible or animating
    bool closeFinished() const;   // close animation finished; host should release its screen
    void acknowledgeClosed();
    void focusPlayer(const std::string& key);
    // Deep link (chat commands, preview): page is "overview", "players", "checks", "utilities",
    // "settings" or "status"; optional player key, profile tab ("summary", "evidence",
    // "behavior", "identity") and settings section name.
    void openTo(const std::string& page, const std::string& playerKey = {}, const std::string& tab = {},
                const std::string& section = {});
    void openSearch(const std::string& query = {});
    // Preview harness only: "scope=N" (roster scope), "confirm" (open the confirm popover for the
    // selected player), "details" (expand the inspector's details).
    void previewOption(const std::string& option);

    // In-game activation card ("Vigil is active"), shown once after injection.
    void showActivation();
    // Brief confirmation in the HUD (e.g. "Fast Buy on" after a hotkey).
    void notify(const std::string& title, const std::string& body);

    // Input, in window client pixels.
    void onMouseMove(int x, int y);
    void onMouseButton(int button, bool down, int x, int y);
    void onMouseWheel(float notches);
    void onChar(unsigned int codepoint);
    void onKey(int vk, bool down, bool ctrl, bool shift);
    Cursor cursor() const;
    bool textInputActive() const;   // a field, key capture or overlay wants Escape/typing

    // Rendering with the GL context current. Draws the menu when open and the in-game HUD
    // (activation card, detection notifications) otherwise.
    void render(int width, int height, double nowMs, bool inGameHud);

    // Release GL objects (context about to be destroyed).
    void releaseGraphics();

private:
    App();
    ~App();
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace vigil::ui
