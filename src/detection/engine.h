#pragma once
// Vigil detection framework — the engine.
//
// Owns player tracks, the combat resolver, all checks, the session, the match tracker and the
// user's marks. Fed one WorldFrame per client tick by the game adapter (or by tests / the UI
// preview with synthetic frames).
//
// Per frame:  phase & scene → tracks → who is a player (NPCs, duplicates) → records (relevance,
//             presence) → exemptions (incl. server stalls) → checks → combat → history.

#include "check.h"
#include "combat.h"
#include "marks.h"
#include "match.h"
#include "roster.h"
#include "session.h"

#include <deque>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace vigil::detection {

struct EngineStats {
    double avgTickMicros = 0.0;
    double peakTickMicros = 0.0;
    int trackedPlayers = 0;
    int filteredEntities = 0;      // NPCs and duplicates ignored this tick
    std::uint64_t ticksProcessed = 0;
    int skippedTicks = 0;
    int combatEvents = 0;
    int stalls = 0;                // server/client stalls detected this session
    int prunedRecords = 0;
};

// How Vigil treats a listed player right now (Status, tests, the Records page).
struct PlayerState {
    bool listed = false;       // has a record (players only; NPCs never do)
    bool local = false;
    bool teammate = false;
    bool exempt = false;       // the user exempted them
    bool inThisGame = false;
    bool judged = false;       // checks would judge them in a phase that is analyzed
};

// An entity in the player list that Vigil does not treat as a player.
struct FilteredEntity {
    int entityId = -1;
    std::string name;
    EntityKind kind = EntityKind::Npc;
    std::string reason;
};

class Engine final : public CheckContext {
public:
    Engine();
    ~Engine() override;

    // Creates checks and registers all detection settings. Call before Settings::load().
    void initialize();

    void onFrame(const WorldFrame& frame);
    void onWorldChanged();             // entity ids become invalid; session continues
    void resetSession(double nowMs);

    const std::vector<std::unique_ptr<Check>>& checks() const { return checks_; }
    Check* checkById(const std::string& id) const;
    Session& session() { return session_; }
    const Session& session() const { return session_; }

    const std::deque<Alert>& alerts() const { return alerts_; }
    std::uint64_t alertSeq() const { return alertSeq_; }
    const std::deque<DebugLine>& debugLog() const { return debugLog_; }
    const EngineStats& stats() const { return stats_; }
    GameContext context() const { return context_; }
    const std::string& contextDetail() const { return contextDetail_; }
    bool active() const { return active_; }           // checks are running
    bool recording() const { return recording_; }     // players are being recorded
    bool monitoringEnabled() const;
    Tick lastTick() const { return tick_; }
    double lastTimeMs() const { return timeMs_; }
    const std::vector<CombatEvent>& recentCombat() const { return recentCombat_; }

    // Relevance.
    const MatchTracker& match() const { return match_; }
    GamePhase phase() const { return match_.phase(); }
    int scene() const { return match_.scene(); }
    bool inThisGame(const PlayerRecord& r) const { return r.scene == match_.scene(); }
    const std::vector<FilteredEntity>& filtered() const { return filtered_; }
    bool stalled() const { return tick_ < stallUntil_; }

    // The user's marks (watch, confirmed by you, exempt).
    Marks& marks() { return marks_; }
    const Marks& marks() const { return marks_; }
    PlayerState playerState(const PlayerRecord& r) const;

    // Persistence hooks (set by the host).
    void setHistoryPath(const std::string& p) { historyPath_ = p; session_.loadHistory(p); }
    void setMarksPath(const std::string& p) { marks_.setPath(p); }
    void setJournalDirectory(const std::string& d) { journalDir_ = d; }
    void flushHistory();

    // CheckContext
    Tick tick() const override { return tick_; }
    double timeMs() const override { return timeMs_; }
    const Exemptions& exemptions(const PlayerTrack& t) const override;
    PlayerTrack* track(int entityId) const override;
    const PlayerTrack* localTrack() const override;
    void emit(const PlayerTrack& subject, int indicator, float severity, std::string summary, std::string detail,
              std::vector<EvidenceValue> values, std::string target) override;
    bool debugEnabled() const override;
    void debug(const PlayerTrack& subject, const std::string& line) override;
    int swingRelayFloor() const override { return relayFloor_; }

private:
    struct Tracked {
        PlayerTrack track;
        std::vector<std::unique_ptr<PlayerCheckState>> states;
        Exemptions ex;
        Tick lastSeen = 0;
        std::string recordKey;
        // Who this entity is.
        EntityObservation obs;
        Vec3 firstPos;
        EntityVerdict verdict;
        // Unlisted and still inside the tab-list grace: not recorded or judged until it is listed
        // or the grace ends (a real player is listed almost immediately).
        bool pending = false;
        bool isPlayer() const { return verdict.kind == EntityKind::Player && !pending; }
    };

    void registerGlobalSettings();
    bool phaseAnalyzed(GamePhase p) const;
    bool phaseRecorded(GamePhase p) const;
    void computeExemptions(Tracked& t);
    void classifyAll(const std::string& localName);
    void detectStall();
    void updateStats(Tracked& t, PlayerRecord& r);
    void sampleHistory(PlayerRecord& r, bool observing);
    void refreshSuspicion(PlayerRecord& r);
    void updateRelayFloor(const PlayerTrack& t);
    PlayerRecord& recordFor(Tracked& t);
    bool shouldAnalyze(const Tracked& t) const;
    void pruneRecords();
    void freshStates(Tracked& t);

    std::vector<std::unique_ptr<Check>> checks_;
    std::unordered_map<int, Tracked> tracked_;
    CombatResolver combat_;
    Session session_;
    MatchTracker match_;
    Marks marks_;
    std::deque<Alert> alerts_;
    std::deque<DebugLine> debugLog_;
    std::vector<CombatEvent> recentCombat_;
    std::vector<FilteredEntity> filtered_;
    EngineStats stats_;

    Tick tick_ = 0;
    double timeMs_ = 0.0;
    GameContext context_ = GameContext::Unknown;
    std::string contextDetail_;
    bool active_ = false;
    bool recording_ = false;
    int localId_ = -1;
    std::uint64_t alertSeq_ = 0;
    int relayFloor_ = 0;
    double lastHistoryFlushMs_ = 0.0;
    std::string historyPath_;
    std::string journalDir_;
    Tick stallUntil_ = -1;
    int noUpdateRun_ = 0;

    // Evaluation context for emit()/debug().
    Check* current_ = nullptr;

    Setting* monitoring_ = nullptr;
    Setting* runInGame_ = nullptr;
    Setting* runPregame_ = nullptr;
    Setting* runLobby_ = nullptr;
    Setting* runReplay_ = nullptr;
    Setting* analyzeSelf_ = nullptr;
    Setting* ignoreTeammates_ = nullptr;
    Setting* kbTicks_ = nullptr;
    Setting* teleportTicks_ = nullptr;
    Setting* iceTicks_ = nullptr;
    Setting* freshTicks_ = nullptr;
    Setting* swingWindow_ = nullptr;
    Setting* settleTicks_ = nullptr;
    Setting* compTicks_ = nullptr;
    Setting* combatRange_ = nullptr;
    Setting* journal_ = nullptr;
    Setting* history_ = nullptr;
};

// Implemented in checks/registry.cpp.
std::vector<std::unique_ptr<Check>> createAllChecks();

}  // namespace vigil::detection
