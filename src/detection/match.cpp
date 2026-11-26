#include "match.h"

#include <algorithm>
#include <cctype>

namespace vigil::detection {

const char* gamePhaseName(GamePhase p) {
    switch (p) {
        case GamePhase::Lobby: return "Lobby";
        case GamePhase::Queue: return "Queue";
        case GamePhase::Match: return "Match";
        case GamePhase::PostGame: return "Post-game";
        case GamePhase::Replay: return "Replay";
        default: return "Idle";
    }
}

bool matchesGameEnd(const std::string& cleanLine) {
    static const char* kTriggers[] = {
        "1st killer", "reward summary", "winner:", "winner -", "winners:", "winners -", "victory!", "game over!",
    };
    if (cleanLine.empty()) return false;
    std::string l = cleanLine;
    std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // Player chat ("[MVP+] Name: winner: me") has a "name:" prefix before the keyword.
    const auto colon = l.find(':');
    for (const char* t : kTriggers) {
        const auto pos = l.find(t);
        if (pos == std::string::npos) continue;
        if (colon != std::string::npos && colon < pos) continue;
        return true;
    }
    return false;
}

void MatchTracker::onWorldChanged() { pendingSceneChange_ = true; }

MatchTracker::Update MatchTracker::onFrame(GameContext context, const std::vector<std::string>& chat, double nowMs) {
    Update u;
    auto reportEnd = [&]() {
        if (endReported_) return;
        endReported_ = true;
        u.matchEnded = true;
        matchEndedMs_ = nowMs;
    };

    if (pendingSceneChange_) {
        pendingSceneChange_ = false;
        ++scene_;
        u.sceneChanged = true;
        reportEnd();   // a new world always ends the match (no-op when already reported)
        postGame_ = false;
        lastSolid_ = GamePhase::Idle;
    }

    GamePhase base = GamePhase::Idle;
    switch (context) {
        case GameContext::Lobby: base = GamePhase::Lobby; break;
        case GameContext::Pregame: base = GamePhase::Queue; break;
        case GameContext::InGame: base = GamePhase::Match; break;
        case GameContext::Replay: base = GamePhase::Replay; break;
        default: base = GamePhase::Idle; break;
    }
    // The scoreboard can be empty for a poll or two while a world loads: hold the last phase.
    GamePhase next = base == GamePhase::Idle ? lastSolid_ : base;
    if (next == GamePhase::PostGame) next = GamePhase::Match;

    const bool inMatchBefore = lastSolid_ == GamePhase::Match || lastSolid_ == GamePhase::PostGame;
    const bool inMatchNow = next == GamePhase::Match;
    if (inMatchNow && !inMatchBefore) {
        // A new match: a queue turned into a game, or a new world loaded straight into one.
        queueScene_ = (!u.sceneChanged && lastSolid_ == GamePhase::Queue) ? scene_ : -1;
        if (!u.sceneChanged) {
            ++scene_;
            u.sceneChanged = true;
        }
        u.matchStarted = true;
        matchScene_ = scene_;
        matchStartedMs_ = nowMs;
        endReported_ = false;
        postGame_ = false;
        ++matches_;
    }
    if (inMatchNow) {
        for (const std::string& line : chat) {
            if (!postGame_ && matchesGameEnd(line)) {
                postGame_ = true;
                reportEnd();
            }
        }
    } else {
        if (inMatchBefore) reportEnd();   // left the match without an announcement we recognised
        postGame_ = false;
    }

    if (inMatchNow && postGame_) next = GamePhase::PostGame;
    if (next != GamePhase::Idle) lastSolid_ = next;
    phase_ = next;
    return u;
}

}  // namespace vigil::detection
