#pragma once
// Vigil detection framework — session records, timelines and cross-session history.

#include "evidence.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace vigil::detection {

struct ConfidencePoint {
    double timeMs = 0.0;
    float value = 0.f;
};

enum class TimelineKind : std::uint8_t { Seen, Left, Evidence, Detection, Hit, Hurt, Carried };

struct TimelineEvent {
    TimelineKind kind = TimelineKind::Seen;
    double timeMs = 0.0;
    Tick tick = 0;
    int check = -1;
    std::uint64_t ref = 0;   // evidence/detection id
    float value = 0.f;       // confidence after, or reach for hits
    std::string text;
};

struct CheckRecord {
    ConfidenceModel model;
    float peak = 0.f;
    int evidence = 0;
    int detections = 0;
    double lastEvidenceMs = 0.0;
    std::vector<int> indicatorCounts;
    std::vector<ConfidencePoint> history;
    float lastSampled = -1.f;
};

// Behavioral statistics that describe how a player plays, independent of any check verdict.
struct BehaviorStats {
    int ticksObserved = 0;
    int ticksInCombat = 0;
    int hitsLanded = 0;
    int hitsTaken = 0;
    int swings = 0;
    double reachSum = 0.0;
    int reachSamples = 0;
    double reachMax = 0.0;
    std::deque<float> reachRecent;      // last 120 attributed reaches
    double aimOutsideSum = 0.0;
    double cpsPeak = 0.0;
    double cpsRecent = 0.0;
    double speedPeak = 0.0;             // 10-tick average, b/tick
    double speedSum = 0.0;
    int speedSamples = 0;
    int jumps = 0;
    double airTicksSum = 0.0;
    std::deque<float> speedRecent;      // sampled every 10 ticks
};

struct PlayerRecord {
    std::string key;      // uuid, or name when uuid is unavailable
    std::string uuid;
    std::string name;
    int entityId = -1;
    bool present = false;
    bool local = false;
    bool teammate = false;
    double firstSeenMs = 0.0;
    double lastSeenMs = 0.0;

    std::vector<CheckRecord> checks;
    std::deque<Evidence> evidence;
    std::vector<Detection> detections;
    std::deque<TimelineEvent> timeline;
    BehaviorStats stats;

    float suspicion = 0.f;            // live while present; held (as last seen) while away
    float peakSuspicion = 0.f;
    std::vector<ConfidencePoint> suspicionHistory;

    // Relevance (PLAN.md §6). A scene is one world or one match; "this game" = the current scene.
    int scene = -1;                   // scene the player was last seen in
    int matchScene = -1;              // scene of the last match they took part in
    int scenesSeen = 0;               // distinct scenes this session
    bool participant = false;         // seen during the current match (or its queue / post-game)
    std::string displaySuffix;        // disambiguates two listed players with the same name

    // Cross-session history (loaded from disk).
    int priorSessions = 0;
    int priorDetections = 0;
    float priorPeak = 0.f;
    double lastSeenEpochMs = 0.0;

    int topCheck() const;   // index of the highest-confidence check, -1 if none
};

struct DebugLine {
    double timeMs = 0.0;
    Tick tick = 0;
    int check = -1;
    std::string player;
    std::string text;
};

struct Alert {
    std::uint64_t seq = 0;
    double timeMs = 0.0;
    std::string playerKey;
    std::string name;
    int check = -1;
    float confidence = 0.f;
    std::string summary;
    std::vector<std::string> reasons;
};

class Session {
public:
    void reset(double nowMs, std::size_t checkCount);
    PlayerRecord& record(const std::string& key, const std::string& uuid, const std::string& name, double nowMs);
    PlayerRecord* find(const std::string& key);
    const PlayerRecord* find(const std::string& key) const;
    std::vector<PlayerRecord>& players() { return players_; }
    const std::vector<PlayerRecord>& players() const { return players_; }
    // Removes records for which `drop` returns true; returns how many were removed.
    int prune(const std::function<bool(const PlayerRecord&)>& drop);

    double startMs() const { return startMs_; }
    std::uint64_t startEpochMs() const { return startEpochMs_; }
    int totalEvidence() const { return totalEvidence_; }
    int totalDetections() const { return totalDetections_; }
    void countEvidence() { ++totalEvidence_; }
    void countDetection() { ++totalDetections_; }
    std::uint64_t nextEvidenceId() { return nextEvidenceId_++; }
    std::uint64_t nextDetectionId() { return nextDetectionId_++; }
    std::uint64_t revision() const { return revision_; }
    void bump() { ++revision_; }

    // Cross-session history persistence.
    void loadHistory(const std::string& path);
    void saveHistory(const std::string& path, const std::vector<std::string>& checkIds) const;
    void applyHistory(PlayerRecord& r) const;

    // Optional session journal (JSON lines).
    void setJournal(const std::string& path) { journalPath_ = path; }
    void journal(const std::string& line) const;

private:
    struct HistoryEntry {
        std::string name;
        int sessions = 0;
        int detections = 0;
        float peak = 0.f;
        double lastSeen = 0.0;
    };

    std::vector<PlayerRecord> players_;
    std::unordered_map<std::string, std::size_t> index_;
    std::map<std::string, HistoryEntry> history_;
    std::size_t checkCount_ = 0;
    double startMs_ = 0.0;
    std::uint64_t startEpochMs_ = 0;
    int totalEvidence_ = 0;
    int totalDetections_ = 0;
    std::uint64_t nextEvidenceId_ = 1;
    std::uint64_t nextDetectionId_ = 1;
    std::uint64_t revision_ = 1;
    std::string journalPath_;
};

}  // namespace vigil::detection
