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

    // Pure classification used by poll(); exposed for tests.
    static detection::GameContext classify(const std::string& titleLower, const std::string& linesLower, std::string& outDetail);

private:
    detection::GameContext context_ = detection::GameContext::Unknown;
    std::string detail_;
    double lastPollMs_ = 0.0;
};

}  // namespace vigil::game
