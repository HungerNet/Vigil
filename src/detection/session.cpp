#include "session.h"

#include "../core/json.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace vigil::detection {

int PlayerRecord::topCheck() const {
    int best = -1;
    float bestValue = 0.5f;
    for (std::size_t i = 0; i < checks.size(); ++i) {
        if (checks[i].model.value() > bestValue) {
            bestValue = checks[i].model.value();
            best = static_cast<int>(i);
        }
    }
    return best;
}

void Session::reset(double nowMs, std::size_t checkCount) {
    players_.clear();
    index_.clear();
    checkCount_ = checkCount;
    startMs_ = nowMs;
    startEpochMs_ = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    totalEvidence_ = 0;
    totalDetections_ = 0;
    ++revision_;
}

PlayerRecord& Session::record(const std::string& key, const std::string& uuid, const std::string& name, double nowMs) {
    auto it = index_.find(key);
    if (it != index_.end()) {
        PlayerRecord& r = players_[it->second];
        if (!name.empty()) r.name = name;
        return r;
    }
    PlayerRecord r;
    r.key = key;
    r.uuid = uuid;
    r.name = name.empty() ? key : name;
    r.firstSeenMs = r.lastSeenMs = nowMs;
    r.checks.resize(checkCount_);
    applyHistory(r);
    players_.push_back(std::move(r));
    index_[key] = players_.size() - 1;
    ++revision_;
    return players_.back();
}

PlayerRecord* Session::find(const std::string& key) {
    auto it = index_.find(key);
    return it == index_.end() ? nullptr : &players_[it->second];
}

const PlayerRecord* Session::find(const std::string& key) const {
    auto it = index_.find(key);
    return it == index_.end() ? nullptr : &players_[it->second];
}

void Session::loadHistory(const std::string& path) {
    history_.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) return;
    std::ostringstream ss;
    ss << in.rdbuf();
    json::Value root;
    if (!json::parse(ss.str(), root)) return;
    for (const auto& kv : root.get("players").members()) {
        HistoryEntry e;
        e.name = kv.second.get("name").asString();
        e.sessions = static_cast<int>(kv.second.get("sessions").asNumber());
        e.detections = static_cast<int>(kv.second.get("detections").asNumber());
        e.peak = static_cast<float>(kv.second.get("peak").asNumber());
        e.lastSeen = kv.second.get("lastSeen").asNumber();
        history_[kv.first] = e;
    }
}

void Session::applyHistory(PlayerRecord& r) const {
    auto it = history_.find(r.key);
    if (it == history_.end()) return;
    r.priorSessions = it->second.sessions;
    r.priorDetections = it->second.detections;
    r.priorPeak = it->second.peak;
    r.lastSeenEpochMs = it->second.lastSeen;
}

void Session::saveHistory(const std::string& path, const std::vector<std::string>& /*checkIds*/) const {
    // Merge this session into the stored history (only players with any evidence, to keep the
    // file small and to avoid recording everyone who walked past).
    std::map<std::string, HistoryEntry> merged = history_;
    const double nowEpoch = static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    for (const PlayerRecord& r : players_) {
        if (r.local || r.evidence.empty()) continue;
        HistoryEntry& e = merged[r.key];
        auto prior = history_.find(r.key);
        const HistoryEntry base = prior == history_.end() ? HistoryEntry{} : prior->second;
        e.name = r.name;
        e.sessions = base.sessions + 1;
        e.detections = base.detections + static_cast<int>(r.detections.size());
        e.peak = std::max(base.peak, r.peakSuspicion);
        e.lastSeen = nowEpoch;
    }
    json::Value root = json::Value::object();
    root["format"] = json::Value(1);
    json::Value& players = root["players"];
    players = json::Value::object();
    for (const auto& kv : merged) {
        json::Value& p = players[kv.first];
        p = json::Value::object();
        p["name"] = json::Value(kv.second.name);
        p["sessions"] = json::Value(kv.second.sessions);
        p["detections"] = json::Value(kv.second.detections);
        p["peak"] = json::Value(static_cast<double>(kv.second.peak));
        p["lastSeen"] = json::Value(kv.second.lastSeen);
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (out) out << json::write(root, 1);
}

void Session::journal(const std::string& line) const {
    if (journalPath_.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(journalPath_).parent_path(), ec);
    std::ofstream out(journalPath_, std::ios::binary | std::ios::app);
    if (out) out << line << "\n";
}

}  // namespace vigil::detection
