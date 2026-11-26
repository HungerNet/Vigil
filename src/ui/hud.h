#pragma once
// Vigil UI — in-game HUD: detection notifications, the activation card shown after injection, and
// short notices (utility hotkeys). Drawn only while the menu is closed.

#include "../detection/engine.h"
#include "app.h"
#include "suspicion.h"
#include "widgets.h"

#include <deque>
#include <string>
#include <vector>

namespace vigil::ui {

class Hud {
public:
    void onAlert(const detection::Alert& a, double now);
    void showActivation(double now);
    void notify(const std::string& title, const std::string& body, double now);
    // Match briefing: who in the new game is worth knowing about (shown only when non-empty).
    void showBriefing(const Briefing& b, double now);
    void clear();

    bool idle(double now) const;
    // Player of the newest notification still on screen (opening the menu investigates them).
    std::string focusCandidate(double now) const;

    struct Context {
        Ui& ui;
        detection::Engine& engine;
        Host* host;
        double now;              // seconds
        float lifeSec;           // base notification duration
        int position;            // 0 top right, 1 top left, 2 bottom right, 3 bottom left
        std::string menuKey;     // name of the menu key ("Right Shift")
        bool showToasts;
    };
    void draw(const Context& c);

private:
    struct Toast {
        std::string playerKey;
        std::string name;
        int check = -1;
        std::vector<int> checks;   // distinct checks folded into this card
        float confidence = 0.f;
        float firstConfidence = 0.f;
        std::string summary;
        int count = 1;
        double born = 0.0;
        double updated = 0.0;
        double leaving = -1.0;     // time the exit started
        Spring x, y;
    };
    struct Notice {
        std::string title, body;
        double born = 0.0;
    };

    float lifeOf(const Toast& t, float base) const;
    void drawToasts(const Context& c);
    void drawActivation(const Context& c);
    void drawNotices(const Context& c);
    void drawBriefing(const Context& c);

    std::deque<Toast> toasts_;
    std::deque<Notice> notices_;
    Briefing briefing_;
    double briefingAt_ = -100.0;
    double activationAt_ = -100.0;
    float lastLife_ = 6.f;
};

}  // namespace vigil::ui
