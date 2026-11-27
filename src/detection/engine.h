#pragma once
// Vigil detection framework — the engine.
//
// Owns player tracks, the combat resolver, all checks, the session, the match tracker and the
// user's marks. Fed one WorldFrame per client tick by the game adapter (or by tests / the UI
// preview with synthetic frames).
//
// Per frame:  phase & scene → tracks → who is a player (NPCs, duplicates) → records (relevance,
//             presence) → exemptions (incl. server stalls) → checks → combat → history.
//
// Configuration: initialize() declares every parameter in the engine's ConfigSchema and starts on
// its defaults. The host replaces the snapshot between frames with setConfig() (in Vigil, from the
// settings registry: config_binding.h). The engine reads no global state: what it concludes is a
// function of the frames it receives and the EngineConfig it runs with (docs/ARCHITECTURE.md §6).

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

    // Creates the checks and declares every parameter in schema(); the engine starts on the
    // schema's defaults. Hosts that show or persist parameters register them in the settings
    // registry afterwards (registerEngineSettings, before Settings::load()).
    void initialize();

    // Configuration (config.h). setConfig() takes effect from the next frame; it refuses a
    // snapshot built for a different schema and returns false.
    const ConfigSchema& schema() const { return *schema_; }
    std::shared_ptr<const ConfigSchema> schemaPtr() const { return schema_; }
    const EngineConfig& config() const override { return config_; }
    bool setConfig(const EngineConfig& config);

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
    bool monitoringEnabled() const { return monitoringEnabled(config_); }
    // The same question asked of another snapshot of this engine's schema (the UI asks the
    // registry's current values, so a toggle shows on the frame it happens).
    bool monitoringEnabled(const EngineConfig& c) const;
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
    PlayerState playerState(const PlayerRecord& r) const { return playerState(r, config_); }
    PlayerState playerState(const PlayerRecord& r, const EngineConfig& c) const;

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

    void registerGlobalParams(ConfigSchema& schema);
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

    // The parameters this engine declared, and the snapshot it runs with.
    std::shared_ptr<ConfigSchema> schema_;
    EngineConfig config_;

    ParamId monitoring_ = kNoParam;
    ParamId runInGame_ = kNoParam;
    ParamId runPregame_ = kNoParam;
    ParamId runLobby_ = kNoParam;
    ParamId runReplay_ = kNoParam;
    ParamId analyzeSelf_ = kNoParam;
    ParamId ignoreTeammates_ = kNoParam;
    ParamId kbTicks_ = kNoParam;
    ParamId teleportTicks_ = kNoParam;
    ParamId iceTicks_ = kNoParam;
    ParamId freshTicks_ = kNoParam;
    ParamId swingWindow_ = kNoParam;
    ParamId settleTicks_ = kNoParam;
    ParamId compTicks_ = kNoParam;
    ParamId combatRange_ = kNoParam;
    ParamId journal_ = kNoParam;
    ParamId history_ = kNoParam;
};

// Implemented in checks/registry.cpp.
std::vector<std::unique_ptr<Check>> createAllChecks();

}  // namespace vigil::detection
