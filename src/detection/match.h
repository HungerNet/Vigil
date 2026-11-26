#pragma once
// Vigil detection framework — game phases and scenes.
//
// The scoreboard tells Vigil whether it is in a lobby, a pre-game queue, a match or a replay. It
// cannot see the end of a match (the scoreboard is unchanged after a win), and nothing marks the
// start of one. The MatchTracker adds both:
//
//   phase   Idle · Lobby · Queue · Match · Post-game · Replay
//   scene   a counter that advances on every world change and every match start. Players are
//           "in this game" when they were seen in the current scene.
//
// Pure C++: tests and the preview drive it with synthetic contexts and chat lines.

#include "types.h"

#include <string>
#include <vector>

namespace vigil::detection {

enum class GamePhase : std::uint8_t { Idle, Lobby, Queue, Match, PostGame, Replay };

const char* gamePhaseName(GamePhase p);

// End-of-game announcements across Hypixel modes ("1st Killer -", "Reward Summary", "Winner:",
// "VICTORY!", "GAME OVER!"). Player chat ("Name: winner: me") never matches.
bool matchesGameEnd(const std::string& cleanLine);

class MatchTracker {
public:
    struct Update {
        bool sceneChanged = false;
        bool matchStarted = false;
        bool matchEnded = false;
    };

    void onWorldChanged();
    // Call once per frame with the scoreboard context and the chat lines received since the last
    // frame (formatting stripped).
    Update onFrame(GameContext context, const std::vector<std::string>& chat, double nowMs);

    GamePhase phase() const { return phase_; }
    int scene() const { return scene_; }
    int matchScene() const { return matchScene_; }       // scene of the current or last match (-1 = none)
    // Scene of the queue that turned into the current match in the same world (-1 = none). The
    // queue and its match are the same game for relevance and confidence.
    int queueScene() const { return queueScene_; }
    double matchStartedMs() const { return matchStartedMs_; }
    double matchEndedMs() const { return matchEndedMs_; }
    std::uint64_t matchCount() const { return matches_; }
    bool inMatch() const { return phase_ == GamePhase::Match || phase_ == GamePhase::PostGame; }

private:
    GamePhase phase_ = GamePhase::Idle;
    GamePhase lastSolid_ = GamePhase::Idle;   // last phase other than Idle (scoreboard flicker)
    bool postGame_ = false;
    bool endReported_ = true;     // the current match's end was already reported
    bool pendingSceneChange_ = false;
    int scene_ = 0;
    int matchScene_ = -1;
    int queueScene_ = -1;
    double matchStartedMs_ = -1.0;
    double matchEndedMs_ = -1.0;
    std::uint64_t matches_ = 0;
};

}  // namespace vigil::detection
