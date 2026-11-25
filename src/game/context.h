#pragma once
// Vigil game layer — infers where the player is (lobby, pre-game, match, replay) from the
// Hypixel sidebar scoreboard.

#include "../detection/types.h"

#include <jni.h>

#include <string>

namespace vigil::game {

class ContextDetector {
public:
    void poll(JNIEnv* env, jobject mc, double nowMs);
    detection::GameContext context() const { return context_; }
    const std::string& detail() const { return detail_; }   // e.g. "BED WARS"
    // Value of the sidebar's "Mode:" line in pre-game lobbies (e.g. "doubles"), lower-case.
    const std::string& mode() const { return mode_; }
    bool bedwarsPregame() const {
        return context_ == detection::GameContext::Pregame && detail_.find("BED WARS") != std::string::npos;
    }

    // Pure classification used by poll(); exposed for tests.
    static detection::GameContext classify(const std::string& titleLower, const std::string& linesLower, std::string& outDetail);

private:
    detection::GameContext context_ = detection::GameContext::Unknown;
    std::string detail_;
    std::string mode_;
    double lastPollMs_ = 0.0;
};

}  // namespace vigil::game
