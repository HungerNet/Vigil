#pragma once
// Vigil detection framework — the engine.
//
// Owns player tracks, the combat resolver, all checks and the session. Fed one WorldFrame per
// client tick by the game adapter (or by tests / the UI preview with synthetic frames).

#include "check.h"
#include "combat.h"
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
    std::uint64_t ticksProcessed = 0;
    int skippedTicks = 0;
    int combatEvents = 0;
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
    bool active() const { return active_; }
    bool monitoringEnabled() const;
    Tick lastTick() const { return tick_; }
    double lastTimeMs() const { return timeMs_; }
    const std::vector<CombatEvent>& recentCombat() const { return recentCombat_; }

    // Persistence hooks (set by the host).
    void setHistoryPath(const std::string& p) { historyPath_ = p; session_.loadHistory(p); }
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
    };

    void registerGlobalSettings();
    bool contextAllowed(GameContext c) const;
    void computeExemptions(Tracked& t);
    void updateStats(Tracked& t, PlayerRecord& r);
    void sampleHistory(PlayerRecord& r);
    void updateRelayFloor(const PlayerTrack& t);
    PlayerRecord& recordFor(Tracked& t);
    bool shouldAnalyze(const Tracked& t) const;

    std::vector<std::unique_ptr<Check>> checks_;
    std::unordered_map<int, Tracked> tracked_;
    CombatResolver combat_;
    Session session_;
    std::deque<Alert> alerts_;
    std::deque<DebugLine> debugLog_;
    std::vector<CombatEvent> recentCombat_;
    EngineStats stats_;

    Tick tick_ = 0;
    double timeMs_ = 0.0;
    GameContext context_ = GameContext::Unknown;
    std::string contextDetail_;
    bool active_ = false;
    int localId_ = -1;
    std::uint64_t alertSeq_ = 0;
    int relayFloor_ = 0;
    double lastHistoryFlushMs_ = 0.0;
    std::string historyPath_;
    std::string journalDir_;

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
