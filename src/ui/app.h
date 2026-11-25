#pragma once
// Vigil UI — the application shell. Drawn natively with OpenGL inside the host's frame, so it is
// independent of Minecraft's GUI system; the host forwards window input while the menu is open.

#include <memory>
#include <string>

namespace vigil::detection {
class Engine;
}

namespace vigil::ui {

// Services the UI needs from its host (the game runtime or the preview harness).
class Host {
public:
    virtual ~Host() = default;
    virtual void reportPlayer(const std::string& name) = 0;     // explicit user action only
    virtual void openFolder(const std::string& path) = 0;
    virtual std::string dataDirectory() const = 0;
    virtual bool connectedToHypixel() const = 0;
    virtual std::string signalSummary() const = 0;              // adapter diagnostics
    virtual void newSession() = 0;
};

enum class Cursor { Arrow, Hand, IBeam, Resize };

class App {
public:
    static App& instance();

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

    // Input, in window client pixels. Return true when consumed.
    void onMouseMove(int x, int y);
    void onMouseButton(int button, bool down, int x, int y);
    void onMouseWheel(float notches);
    void onChar(unsigned int codepoint);
    void onKey(int vk, bool down, bool ctrl, bool shift);
    Cursor cursor() const;
    bool textInputActive() const;

    // Rendering with the GL context current. Draws the menu when open and the in-game HUD
    // (detection toasts) otherwise.
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
