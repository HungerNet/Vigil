#include "marks.h"

#include "../core/json.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace vigil::detection {

double epochNowMs() {
    using namespace std::chrono;
    return static_cast<double>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

void Marks::setPath(const std::string& path) {
    path_ = path;
    marks_.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) return;
    std::ostringstream ss;
    ss << in.rdbuf();
    json::Value root;
    if (!json::parse(ss.str(), root)) return;
    for (const auto& kv : root.get("players").members()) {
        Mark m;
        m.key = kv.first;
        m.name = kv.second.get("name").asString();
        m.watch = kv.second.get("watch").asBool();
        m.confirmed = kv.second.get("confirmed").asBool();
        m.confirmedAtEpochMs = kv.second.get("confirmedAt").asNumber();
        m.updatedEpochMs = kv.second.get("updated").asNumber();
        for (const auto& r : kv.second.get("reasons").items()) m.reasons.push_back(r.asString());
        if (m.watch || m.confirmed) marks_[m.key] = m;
    }
    ++revision_;
}

bool Marks::save() const {
    if (path_.empty()) return false;
    json::Value root = json::Value::object();
    root["format"] = json::Value(1);
    json::Value& players = root["players"];
    players = json::Value::object();
    for (const auto& kv : marks_) {
        const Mark& m = kv.second;
        json::Value& p = players[kv.first];
        p = json::Value::object();
        p["name"] = json::Value(m.name);
        p["watch"] = json::Value(m.watch);
        p["confirmed"] = json::Value(m.confirmed);
        if (m.confirmed) p["confirmedAt"] = json::Value(m.confirmedAtEpochMs);
        p["updated"] = json::Value(m.updatedEpochMs);
        json::Value& reasons = p["reasons"];
        reasons = json::Value::array();
        for (const auto& r : m.reasons) reasons.push(json::Value(r));
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path(), ec);
    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << json::write(root, 1);
    return true;
}

const Mark* Marks::find(const std::string& key) const {
    auto it = marks_.find(key);
    return it == marks_.end() ? nullptr : &it->second;
}

bool Marks::watched(const std::string& key) const {
    const Mark* m = find(key);
    return m && (m->watch || m->confirmed);
}

bool Marks::confirmed(const std::string& key) const {
    const Mark* m = find(key);
    return m && m->confirmed;
}

void Marks::changed() {
    ++revision_;
    save();
}

void Marks::setWatch(const std::string& key, const std::string& name, bool on) {
    if (key.empty()) return;
    Mark& m = marks_[key];
    m.key = key;
    if (!name.empty()) m.name = name;
    m.watch = on;
    m.updatedEpochMs = epochNowMs();
    if (!m.watch && !m.confirmed) marks_.erase(key);
    changed();
}

void Marks::confirm(const std::string& key, const std::string& name, std::vector<std::string> reasons) {
    if (key.empty()) return;
    Mark& m = marks_[key];
    m.key = key;
    if (!name.empty()) m.name = name;
    m.confirmed = true;
    m.watch = true;
    m.confirmedAtEpochMs = epochNowMs();
    m.updatedEpochMs = m.confirmedAtEpochMs;
    m.reasons = std::move(reasons);
    changed();
}

void Marks::unconfirm(const std::string& key) {
    auto it = marks_.find(key);
    if (it == marks_.end()) return;
    it->second.confirmed = false;
    it->second.reasons.clear();
    it->second.updatedEpochMs = epochNowMs();
    // Removing a confirmation keeps the player watched; unpin separately.
    changed();
}

void Marks::remove(const std::string& key) {
    if (marks_.erase(key)) changed();
}

void Marks::rememberName(const std::string& key, const std::string& name) {
    auto it = marks_.find(key);
    if (it == marks_.end() || name.empty() || it->second.name == name) return;
    it->second.name = name;
    changed();
}

std::vector<Mark> Marks::all() const {
    std::vector<Mark> out;
    for (const auto& kv : marks_) out.push_back(kv.second);
    std::sort(out.begin(), out.end(), [](const Mark& a, const Mark& b) {
        if (a.confirmed != b.confirmed) return a.confirmed;
        return a.updatedEpochMs > b.updatedEpochMs;
    });
    return out;
}

int Marks::confirmedCount() const {
    int n = 0;
    for (const auto& kv : marks_) if (kv.second.confirmed) ++n;
    return n;
}

int Marks::watchedCount() const {
    int n = 0;
    for (const auto& kv : marks_) if (kv.second.watch || kv.second.confirmed) ++n;
    return n;
}

}  // namespace vigil::detection
