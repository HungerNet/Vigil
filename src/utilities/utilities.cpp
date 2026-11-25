#include "utilities.h"

#include "../core/settings.h"

#include <algorithm>
#include <cctype>
#include <chrono>

namespace vigil::utilities {

// ── Settings ─────────────────────────────────────────────────────────────────────────────────

namespace {
UtilitySettings g_settings;
}

void registerSettings() {
    if (g_settings.autoWho) return;
    auto& S = Settings::instance();
    auto grp = [](Setting& s, bool adv = false) -> Setting& { s.group = "Utilities"; s.advanced = adv; return s; };
    g_settings.autoWho = &grp(S.addBool("utilities.autoWho", "Auto Who", false,
                                        "Sends /who as soon as a game starts so you see everyone in the lobby."));
    g_settings.autoWhoDelay = &grp(S.addInt("utilities.autoWho.delay", "Delay", 400, {0, 3000, 50, 0, "ms"},
                                            "Wait this long after the game-start message before sending /who."), true);
    g_settings.autoWhoKey = &grp(S.addKey("utilities.autoWho.key", "Toggle Auto Who", 0,
                                          "Keyboard shortcut that turns Auto Who on or off."), true);
    g_settings.fastBuy = &grp(S.addBool("utilities.fastBuy", "Fast Buy", false,
                                        "In the Bed Wars Quick Buy menu, a left click buys instantly (it is sent as the "
                                        "shop's middle-click purchase)."));
    g_settings.fastBuyKey = &grp(S.addKey("utilities.fastBuy.key", "Toggle Fast Buy", 0,
                                          "Keyboard shortcut that turns Fast Buy on or off."), true);
    g_settings.partyChat = &grp(S.addBool("utilities.partyChat", "Party Detector", false,
                                          "In Bed Wars pre-game lobbies, groups the \"has joined\" and \"has quit\" lines "
                                          "that arrive together and reports the party size."));
    g_settings.partyChatKey = &grp(S.addKey("utilities.partyChat.key", "Toggle Party Detector", 0,
                                            "Keyboard shortcut that turns the Party Detector on or off."), true);
    g_settings.partyQueue = &grp(S.addBool("utilities.partyQueue", "Party Queue Detector", false,
                                           "When you join a Bed Wars queue on a busy server, finds parties from players "
                                           "that were spawned with consecutive entity ids."));
    g_settings.partyQueueKey = &grp(S.addKey("utilities.partyQueue.key", "Toggle Party Queue Detector", 0,
                                             "Keyboard shortcut that turns the Party Queue Detector on or off."), true);
    g_settings.announceInChat = &grp(S.addBool("utilities.announce", "Announce in chat", true,
                                               "Print utility results (parties found, /who sent) as a Vigil chat line. "
                                               "Results always appear on the Utilities page."));
}

const UtilitySettings& settings() { return g_settings; }

// ── Activity ─────────────────────────────────────────────────────────────────────────────────

const char* kindName(Kind k) {
    switch (k) {
        case Kind::AutoWho: return "Auto Who";
        case Kind::FastBuy: return "Fast Buy";
        case Kind::PartyChat: return "Party Detector";
        case Kind::PartyQueue: return "Party Queue Detector";
    }
    return "";
}

double epochMs() {
    using namespace std::chrono;
    return static_cast<double>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

namespace {
std::mutex g_liveMu;
LiveState g_live;
}  // namespace

void setLiveState(const LiveState& s) {
    std::lock_guard<std::mutex> lk(g_liveMu);
    g_live = s;
}

LiveState liveState() {
    std::lock_guard<std::mutex> lk(g_liveMu);
    return g_live;
}

Activity& Activity::instance() {
    static Activity a;
    return a;
}

void Activity::add(Kind kind, std::string text, bool highlight) {
    std::lock_guard<std::mutex> lk(mu_);
    entries_.push_front({epochMs(), kind, std::move(text), highlight});
    while (entries_.size() > 200) entries_.pop_back();
    ++counts_[static_cast<int>(kind)];
}

std::vector<ActivityEntry> Activity::recent(int max) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<ActivityEntry> out;
    for (const auto& e : entries_) {
        if (static_cast<int>(out.size()) >= max) break;
        out.push_back(e);
    }
    return out;
}

std::vector<ActivityEntry> Activity::recent(Kind kind, int max) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<ActivityEntry> out;
    for (const auto& e : entries_) {
        if (static_cast<int>(out.size()) >= max) break;
        if (e.kind == kind) out.push_back(e);
    }
    return out;
}

int Activity::count(Kind kind) const {
    std::lock_guard<std::mutex> lk(mu_);
    return counts_[static_cast<int>(kind)];
}

void Activity::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    entries_.clear();
    for (int& c : counts_) c = 0;
}

// ── Text helpers ─────────────────────────────────────────────────────────────────────────────

std::string stripFormatting(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        // '§' is C2 A7 in UTF-8.
        if (static_cast<unsigned char>(s[i]) == 0xC2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA7) {
            i += 2;
            continue;
        }
        out.push_back(s[i]);
    }
    return out;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// ── Auto Who ─────────────────────────────────────────────────────────────────────────────────

bool matchesWhoTrigger(const std::string& cleanLine) {
    // Carried over from the legacy client; each is the first line of a game in its mode.
    static const char* kTriggers[] = {
        "protect your bed", "players swap teams", "all generators are maxed", "select an ultimate",
        "collect lucky blocks", "become a soul collector", "every few seconds", "gather resources",
        "opponents:", "opponent:", "bed wars duels", "teaming with the murderer is not allowed!",
        "teaming with the detective/innocents is not allowed!",
    };
    if (cleanLine.empty()) return false;
    const std::string l = lower(cleanLine);
    // Player chat ("Name: protect your bed") must not trigger: game messages have no "name:" prefix
    // except the Duels "Opponent:" lines, which start with the keyword itself.
    const auto colon = l.find(':');
    for (const char* t : kTriggers) {
        const auto pos = l.find(t);
        if (pos == std::string::npos) continue;
        if (colon != std::string::npos && colon < pos) continue;
        return true;
    }
    return false;
}

bool AutoWho::onChat(const std::string& cleanLine, double nowMs, double delayMs) {
    if (!matchesWhoTrigger(cleanLine)) return false;
    if (nowMs - lastTrigger_ < 3000.0) return false;   // one /who per game start
    lastTrigger_ = nowMs;
    scheduledAt_ = nowMs + std::max(0.0, delayMs);
    retries_ = 0;
    return true;
}

void AutoWho::markFailed(double nowMs) {
    if (++retries_ > 8) { reset(); return; }
    scheduledAt_ = nowMs + 250.0;
}

// ── Party detector (chat) ────────────────────────────────────────────────────────────────────

bool PartyChatDetector::parseJoin(const std::string& cleanLine, std::string& name) {
    // "Name has joined (5/16)!"
    const auto pos = cleanLine.find(" has joined (");
    if (pos == std::string::npos || pos == 0) return false;
    if (cleanLine.find('/', pos) == std::string::npos) return false;
    name = trim(cleanLine.substr(0, pos));
    return !name.empty() && name.find(' ') == std::string::npos;
}

bool PartyChatDetector::parseQuit(const std::string& cleanLine, std::string& name) {
    const auto pos = cleanLine.find(" has quit!");
    if (pos == std::string::npos || pos == 0) return false;
    name = trim(cleanLine.substr(0, pos));
    return !name.empty() && name.find(' ') == std::string::npos;
}

void PartyChatDetector::onChat(const std::string& cleanLine, double nowMs) {
    std::string name;
    if (parseJoin(cleanLine, name)) {
        if (join_.seen.insert(lower(name)).second) join_.names.push_back(name);
        join_.last = nowMs;
    } else if (parseQuit(cleanLine, name)) {
        if (quit_.seen.insert(lower(name)).second) quit_.names.push_back(name);
        quit_.last = nowMs;
    }
}

void PartyChatDetector::flush(Batch& b, bool joined, std::vector<PartyAnnouncement>& out) {
    if (b.names.size() >= 2) out.push_back({joined, static_cast<int>(b.names.size()), b.names});
    b = Batch{};
}

std::vector<PartyAnnouncement> PartyChatDetector::poll(double nowMs) {
    std::vector<PartyAnnouncement> out;
    if (!join_.names.empty() && nowMs - join_.last >= kSettleMs) flush(join_, true, out);
    if (!quit_.names.empty() && nowMs - quit_.last >= kSettleMs) flush(quit_, false, out);
    return out;
}

void PartyChatDetector::reset() {
    join_ = Batch{};
    quit_ = Batch{};
}

// ── Party queue detector ─────────────────────────────────────────────────────────────────────

int maxPartySizeFromMode(const std::string& modeText) {
    const std::string m = lower(modeText);
    if (m.find("eight_one") != std::string::npos || m.find("solo") != std::string::npos) return 1;
    if (m.find("eight_two") != std::string::npos || m.find("doubles") != std::string::npos) return 2;
    if (m.find("four_three") != std::string::npos || m.find("3v3v3v3") != std::string::npos) return 3;
    if (m.find("four_four") != std::string::npos || m.find("4v4v4v4") != std::string::npos) return 4;
    if (m.find("two_four") != std::string::npos || m.find("4v4") != std::string::npos) return 4;
    return 0;
}

void QueuePartyDetector::prune(double nowMs) {
    while (!spawns_.empty() && nowMs - spawns_.front().second > kWindowMs) spawns_.pop_front();
}

void QueuePartyDetector::schedule(double nowMs) {
    if (!active_ || !modeKnown_ || maxParty_ < 2) return;
    analyzeAt_ = nowMs + kAnalyzeDelayMs;
}

void QueuePartyDetector::setActive(bool active, double nowMs) {
    if (active_ == active) return;
    active_ = active;
    if (active) {
        // Spawns that arrived just before the queue was recognised are kept.
        prune(nowMs);
        if (!spawns_.empty()) schedule(nowMs);
    } else {
        reset();
    }
}

void QueuePartyDetector::setMaxPartySize(int n, double nowMs) {
    const bool changed = n != maxParty_ || !modeKnown_;
    maxParty_ = n;
    modeKnown_ = true;
    if (changed && active_ && !spawns_.empty()) schedule(nowMs);
}

void QueuePartyDetector::onSpawn(int entityId, double nowMs) {
    if (entityId <= 0) return;
    spawns_.push_back({entityId, nowMs});
    prune(nowMs);
    if (active_) schedule(nowMs);
}

QueuePartyDetector::Result QueuePartyDetector::poll(double nowMs) {
    Result r;
    prune(nowMs);
    if (analyzeAt_ <= 0.0 || nowMs < analyzeAt_) return r;
    analyzeAt_ = 0.0;
    if (spawns_.empty() || maxParty_ < 2) return r;

    std::vector<int> ids;
    bool allLow = true;
    for (const auto& s : spawns_) {
        if (s.first <= kIdThreshold) continue;
        allLow = false;
        if (announcedIds_.count(s.first)) continue;
        ids.push_back(s.first);
    }
    if (allLow) {
        if (!requeueAnnounced_) {
            requeueAnnounced_ = true;
            r.requeueSuggested = true;
        }
        return r;
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    for (std::size_t i = 0; i < ids.size();) {
        std::size_t j = i + 1;
        while (j < ids.size() && ids[j] == ids[j - 1] + 1) ++j;
        // A run longer than the maximum party size is several parties (or a party plus solos).
        for (std::size_t c = i; c < j;) {
            const std::size_t len = std::min(static_cast<std::size_t>(maxParty_), j - c);
            if (len >= 2) {
                QueueParty p{static_cast<int>(len), ids[c], ids[c + len - 1]};
                for (int id = p.minId; id <= p.maxId; ++id) announcedIds_.insert(id);
                r.parties.push_back(p);
            }
            c += len;
        }
        i = j;
    }
    return r;
}

void QueuePartyDetector::reset() {
    spawns_.clear();
    announcedIds_.clear();
    analyzeAt_ = 0.0;
    requeueAnnounced_ = false;
    maxParty_ = 0;
    modeKnown_ = false;
}

// ── Fast Buy ─────────────────────────────────────────────────────────────────────────────────

bool isQuickBuyMenu(const std::string& firstSlotName) { return trim(stripFormatting(firstSlotName)) == "Quick Buy"; }

}  // namespace vigil::utilities
