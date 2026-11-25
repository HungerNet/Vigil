#pragma once
// Vigil Utilities — optional Bed Wars helpers restored from the legacy client:
//   Auto Who              sends /who when a game starts
//   Fast Buy              turns Quick Buy clicks into instant purchases
//   Party Detector        batches lobby "has joined"/"has quit" lines into party sizes
//   Party Queue Detector  finds parties from runs of consecutive entity ids when joining a queue
//
// This layer is pure C++ (no JNI, no Windows): parsers and state machines take chat text,
// entity ids and timestamps, so they are unit-tested. src/game wires them to the client.

#include <cstdint>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace vigil {
struct Setting;
}

namespace vigil::utilities {

// ── Settings (registered once, shared by the runtime and the UI) ──────────────────────────────
struct UtilitySettings {
    Setting* autoWho = nullptr;
    Setting* autoWhoDelay = nullptr;
    Setting* autoWhoKey = nullptr;
    Setting* fastBuy = nullptr;
    Setting* fastBuyKey = nullptr;
    Setting* partyChat = nullptr;
    Setting* partyChatKey = nullptr;
    Setting* partyQueue = nullptr;
    Setting* partyQueueKey = nullptr;
    Setting* announceInChat = nullptr;
};
void registerSettings();
const UtilitySettings& settings();

// ── Activity log (thread-safe; read by the UI) ────────────────────────────────────────────────
enum class Kind { AutoWho, FastBuy, PartyChat, PartyQueue };
const char* kindName(Kind k);

struct ActivityEntry {
    double timeMs = 0.0;   // wall clock (epoch ms)
    Kind kind = Kind::AutoWho;
    std::string text;
    bool highlight = false;  // a result (party found) rather than routine activity
};

class Activity {
public:
    static Activity& instance();
    void add(Kind kind, std::string text, bool highlight = false);
    std::vector<ActivityEntry> recent(int max) const;              // newest first, all kinds
    std::vector<ActivityEntry> recent(Kind kind, int max) const;   // newest first
    int count(Kind kind) const;
    void clear();

private:
    mutable std::mutex mu_;
    std::deque<ActivityEntry> entries_;
    int counts_[4] = {};
};

double epochMs();

// What the runtime currently sees, for contextual status on the Utilities page.
struct LiveState {
    bool hosted = false;          // running in the game (false in the preview)
    bool hypixel = false;
    bool chatReader = false;      // chat lines can be read
    bool bedwarsPregame = false;  // in a Bed Wars pre-game lobby
    std::string mode;             // "Doubles", "4v4v4v4", …
    int queueMaxParty = 0;
    bool quickBuyOpen = false;
};
void setLiveState(const LiveState& s);
LiveState liveState();

// ── Text helpers ──────────────────────────────────────────────────────────────────────────────
std::string stripFormatting(const std::string& s);   // removes §x codes
std::string lower(std::string s);
std::string trim(const std::string& s);

// ── Auto Who ──────────────────────────────────────────────────────────────────────────────────
// Game-start lines across Hypixel modes (Bed Wars, Duels, SkyWars, Murder Mystery, …).
bool matchesWhoTrigger(const std::string& cleanLine);

class AutoWho {
public:
    // Returns true when a /who was scheduled by this line.
    bool onChat(const std::string& cleanLine, double nowMs, double delayMs);
    // Returns true when /who should be sent now. Call markSent() or markFailed() after trying.
    bool due(double nowMs) const { return scheduledAt_ > 0.0 && nowMs >= scheduledAt_; }
    void markSent() { scheduledAt_ = 0.0; retries_ = 0; }
    void markFailed(double nowMs);
    void reset() { scheduledAt_ = 0.0; retries_ = 0; }
    bool pending() const { return scheduledAt_ > 0.0; }

private:
    double lastTrigger_ = -1e18;
    double scheduledAt_ = 0.0;
    int retries_ = 0;
};

// ── Party detector (lobby chat) ───────────────────────────────────────────────────────────────
struct PartyAnnouncement {
    bool joined = true;        // false = quit
    int size = 0;
    std::vector<std::string> names;
};

class PartyChatDetector {
public:
    static constexpr double kSettleMs = 600.0;   // batch window after the last line

    // Feed every clean chat line while in a Bed Wars pre-game lobby.
    void onChat(const std::string& cleanLine, double nowMs);
    // Announcements whose batch has settled (size ≥ 2 only).
    std::vector<PartyAnnouncement> poll(double nowMs);
    void reset();

    // "Name has joined (5/16)!" → Name ; "Name has quit!" → Name
    static bool parseJoin(const std::string& cleanLine, std::string& name);
    static bool parseQuit(const std::string& cleanLine, std::string& name);

private:
    struct Batch {
        std::vector<std::string> names;
        std::set<std::string> seen;
        double last = 0.0;
    };
    void flush(Batch& b, bool joined, std::vector<PartyAnnouncement>& out);
    Batch join_, quit_;
};

// ── Party queue detector (entity ids) ─────────────────────────────────────────────────────────
// When a party joins a queue for a game already running on the server, its members are spawned
// one after another and receive consecutive global entity ids. Runs of consecutive ids (split
// into chunks of the mode's maximum party size) reveal parties.
struct QueueParty {
    int size = 0;
    int minId = 0;
    int maxId = 0;
};

class QueuePartyDetector {
public:
    static constexpr int kIdThreshold = 100;      // fresh servers hand out ids < 100; heuristic does not apply
    static constexpr double kWindowMs = 6000.0;
    static constexpr double kAnalyzeDelayMs = 600.0;

    void setActive(bool active, double nowMs);
    bool active() const { return active_; }
    void setMaxPartySize(int n, double nowMs);
    int maxPartySize() const { return maxParty_; }
    void onSpawn(int entityId, double nowMs);
    struct Result {
        std::vector<QueueParty> parties;
        bool requeueSuggested = false;   // every recent id was low: a brand new server
    };
    Result poll(double nowMs);
    void reset();

private:
    void schedule(double nowMs);
    void prune(double nowMs);
    bool active_ = false;
    int maxParty_ = 0;
    bool modeKnown_ = false;
    double analyzeAt_ = 0.0;
    bool requeueAnnounced_ = false;
    std::deque<std::pair<int, double>> spawns_;
    std::set<int> announcedIds_;
};

// Max party size from a Bed Wars mode label ("Doubles", "3v3v3v3", "4v4v4v4", "4v4", "Solo")
// or a locraw mode ("BEDWARS_EIGHT_TWO"). 0 = unknown, 1 = solo.
int maxPartySizeFromMode(const std::string& modeText);

// ── Fast Buy ──────────────────────────────────────────────────────────────────────────────────
// The Bed Wars shop's first slot is named "Quick Buy".
bool isQuickBuyMenu(const std::string& firstSlotName);

}  // namespace vigil::utilities
