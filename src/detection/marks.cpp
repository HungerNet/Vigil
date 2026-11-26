#include "marks.h"

#include "../core/json.h"
#include "roster.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace vigil::detection {

double epochNowMs() {
    using namespace std::chrono;
    return static_cast<double>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

std::string lowerName(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

void Marks::setPath(const std::string& path) {
    path_ = path;
    marks_.clear();
    readOnly_ = false;
    loadNote_.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) return;
    std::ostringstream ss;
    ss << in.rdbuf();
    json::Value root;
    if (!json::parse(ss.str(), root) || !root.isObject()) {
        // Never overwrite a file we could not read: the user's records may be recoverable.
        readOnly_ = true;
        loadNote_ = "marks.json could not be read; changes are not saved until it is fixed or removed";
        return;
    }
    const int format = static_cast<int>(root.get("format").asNumber(1));
    if (format > kFormat) {
        readOnly_ = true;
        loadNote_ = "marks.json was written by a newer Vigil; changes are not saved";
    }
    for (const auto& kv : root.get("players").members()) {
        const json::Value& v = kv.second;
        Mark m;
        m.key = kv.first;
        m.name = v.get("name").asString();
        m.uuid = v.get("uuid").asString();
        m.watch = v.get("watch").asBool();
        m.confirmed = v.get("confirmed").asBool();
        m.exempt = v.get("exempt").asBool();
        m.addedEpochMs = v.get("added").asNumber();
        m.confirmedAtEpochMs = v.get("confirmedAt").asNumber();
        m.exemptAtEpochMs = v.get("exemptAt").asNumber();
        m.updatedEpochMs = v.get("updated").asNumber();
        m.note = v.get("note").asString();
        m.confidenceAtConfirm = static_cast<float>(v.get("confidenceAtConfirm").asNumber(-1.0));
        m.origin = v.get("origin").asString();
        for (const auto& r : v.get("reasons").items()) m.reasons.push_back(r.asString());
        if (m.addedEpochMs <= 0.0) m.addedEpochMs = m.confirmedAtEpochMs > 0.0 ? m.confirmedAtEpochMs : m.updatedEpochMs;
        if (m.any()) marks_[m.key] = m;
    }
    ++revision_;
}

bool Marks::save() const {
    if (path_.empty() || readOnly_) return false;
    json::Value root = json::Value::object();
    root["format"] = json::Value(kFormat);
    json::Value& players = root["players"];
    players = json::Value::object();
    for (const auto& kv : marks_) {
        const Mark& m = kv.second;
        json::Value& p = players[kv.first];
        p = json::Value::object();
        p["name"] = json::Value(m.name);
        if (!m.uuid.empty()) p["uuid"] = json::Value(m.uuid);
        p["watch"] = json::Value(m.watch);
        p["confirmed"] = json::Value(m.confirmed);
        p["exempt"] = json::Value(m.exempt);
        p["added"] = json::Value(m.addedEpochMs);
        if (m.confirmed) p["confirmedAt"] = json::Value(m.confirmedAtEpochMs);
        if (m.confirmed && m.confidenceAtConfirm >= 0.f) p["confidenceAtConfirm"] = json::Value(static_cast<double>(m.confidenceAtConfirm));
        if (m.exempt) p["exemptAt"] = json::Value(m.exemptAtEpochMs);
        p["updated"] = json::Value(m.updatedEpochMs);
        if (!m.note.empty()) p["note"] = json::Value(m.note);
        if (!m.origin.empty()) p["origin"] = json::Value(m.origin);
        json::Value& reasons = p["reasons"];
        reasons = json::Value::array();
        for (const auto& r : m.reasons) reasons.push(json::Value(r));
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path(), ec);
    const std::string tmp = path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << json::write(root, 1);
    }
    std::filesystem::rename(tmp, path_, ec);
    if (ec) {
        std::filesystem::remove(path_, ec);
        std::filesystem::rename(tmp, path_, ec);
    }
    return !ec;
}

const Mark* Marks::find(const std::string& key) const {
    auto it = marks_.find(key);
    return it == marks_.end() ? nullptr : &it->second;
}

const Mark* Marks::findByName(const std::string& name) const {
    const std::string lower = lowerName(name);
    for (const auto& kv : marks_) if (lowerName(kv.second.name) == lower && !kv.second.addedByName()) return &kv.second;
    return find("user:" + lower);
}

bool Marks::watched(const std::string& key) const {
    const Mark* m = find(key);
    return m && (m->watch || m->confirmed);
}

bool Marks::confirmed(const std::string& key) const {
    const Mark* m = find(key);
    return m && m->confirmed;
}

bool Marks::exempt(const std::string& key) const {
    const Mark* m = find(key);
    return m && m->exempt;
}

void Marks::changed() {
    ++revision_;
    save();
}

Mark& Marks::touch(const std::string& key, const std::string& name) {
    Mark& m = marks_[key];
    const double now = epochNowMs();
    if (m.key.empty()) {
        m.key = key;
        m.addedEpochMs = now;
        if (key.rfind("user:", 0) != 0 && key.rfind("name:", 0) != 0) m.uuid = key;
    }
    if (!name.empty()) m.name = name;
    m.updatedEpochMs = now;
    return m;
}

void Marks::setWatch(const std::string& key, const std::string& name, bool on) {
    if (key.empty()) return;
    Mark& m = touch(key, name);
    if (m.origin.empty()) m.origin = "profile";
    m.watch = on;
    if (on) m.exempt = false;   // watching someone means judging them
    if (!m.any()) marks_.erase(key);
    changed();
}

void Marks::confirm(const std::string& key, const std::string& name, std::vector<std::string> reasons, float confidence) {
    if (key.empty()) return;
    Mark& m = touch(key, name);
    if (m.origin.empty()) m.origin = "profile";
    m.confirmed = true;
    m.watch = true;
    m.exempt = false;
    m.confirmedAtEpochMs = m.updatedEpochMs;
    m.confidenceAtConfirm = confidence;
    m.reasons = std::move(reasons);
    changed();
}

void Marks::unconfirm(const std::string& key) {
    auto it = marks_.find(key);
    if (it == marks_.end()) return;
    it->second.confirmed = false;
    it->second.reasons.clear();
    it->second.confidenceAtConfirm = -1.f;
    it->second.updatedEpochMs = epochNowMs();
    // Removing a confirmation keeps the player watched; unpin separately.
    changed();
}

void Marks::setExempt(const std::string& key, const std::string& name, bool on) {
    if (key.empty()) return;
    Mark& m = touch(key, name);
    if (m.origin.empty()) m.origin = "profile";
    m.exempt = on;
    if (on) {
        m.exemptAtEpochMs = m.updatedEpochMs;
        m.confirmed = false;
        m.watch = false;
        m.reasons.clear();
        m.confidenceAtConfirm = -1.f;
    }
    if (!m.any()) marks_.erase(key);
    changed();
}

void Marks::setNote(const std::string& key, const std::string& note) {
    auto it = marks_.find(key);
    if (it == marks_.end() || it->second.note == note) return;
    it->second.note = note.size() > 500 ? note.substr(0, 500) : note;
    it->second.updatedEpochMs = epochNowMs();
    changed();
}

void Marks::setReasons(const std::string& key, std::vector<std::string> reasons) {
    auto it = marks_.find(key);
    if (it == marks_.end() || !it->second.confirmed) return;
    it->second.reasons = std::move(reasons);
    it->second.updatedEpochMs = epochNowMs();
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

std::string Marks::addByName(const std::string& name, MarkKind kind, const std::string& knownKey) {
    if (!isValidPlayerName(name)) return {};
    std::string key = knownKey;
    if (key.empty()) {
        // Someone already marked under a real key with this name keeps that key.
        const std::string lower = lowerName(name);
        for (const auto& kv : marks_) {
            if (!kv.second.addedByName() && lowerName(kv.second.name) == lower) { key = kv.first; break; }
        }
        if (key.empty()) key = "user:" + lower;
    }
    const bool fresh = marks_.find(key) == marks_.end();
    switch (kind) {
        case MarkKind::Watch: setWatch(key, name, true); break;
        case MarkKind::Confirm: confirm(key, name, {}, -1.f); break;
        case MarkKind::Exempt: setExempt(key, name, true); break;
    }
    if (fresh) {
        auto it = marks_.find(key);
        if (it != marks_.end()) {
            it->second.origin = knownKey.empty() ? "name" : "profile";
            save();
        }
    }
    return key;
}

bool Marks::bindName(const std::string& recordKey, const std::string& name, const std::string& uuid) {
    if (name.empty() || recordKey.empty()) return false;
    auto it = marks_.find("user:" + lowerName(name));
    if (it == marks_.end()) return false;
    // Real accounts only (UUID version 4), or a record Vigil could only key by name.
    const bool real = uuidVersionOf(uuid) == 4;
    const bool nameOnly = uuid.empty() && recordKey.rfind("name:", 0) == 0;
    if (!real && !nameOnly) return false;
    Mark m = it->second;
    marks_.erase(it);
    m.key = recordKey;
    m.uuid = uuid;
    m.name = name;
    m.updatedEpochMs = epochNowMs();
    auto existing = marks_.find(recordKey);
    if (existing != marks_.end()) {
        // The account already had marks: merge, the stronger decision wins (confirm > exempt > watch).
        Mark& e = existing->second;
        if (m.confirmed && !e.confirmed) {
            e.confirmed = true;
            e.exempt = false;
            e.confirmedAtEpochMs = m.confirmedAtEpochMs;
            e.reasons = m.reasons;
        }
        if (m.exempt && !e.confirmed) e.exempt = true;
        e.watch = e.watch || m.watch || e.confirmed;
        if (e.note.empty()) e.note = m.note;
        e.updatedEpochMs = m.updatedEpochMs;
    } else {
        marks_[recordKey] = m;
    }
    changed();
    return true;
}

std::vector<Mark> Marks::all() const {
    std::vector<Mark> out;
    for (const auto& kv : marks_) out.push_back(kv.second);
    auto rank = [](const Mark& m) { return m.confirmed ? 0 : m.watch ? 1 : 2; };
    std::sort(out.begin(), out.end(), [&](const Mark& a, const Mark& b) {
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        return a.updatedEpochMs > b.updatedEpochMs;
    });
    return out;
}

std::vector<Mark> Marks::search(const std::string& query, int kind) const {
    const std::string q = lowerName(query);
    std::vector<Mark> out;
    for (const Mark& m : all()) {
        if (kind == static_cast<int>(MarkKind::Watch) && !(m.watch && !m.confirmed)) continue;
        if (kind == static_cast<int>(MarkKind::Confirm) && !m.confirmed) continue;
        if (kind == static_cast<int>(MarkKind::Exempt) && !m.exempt) continue;
        if (!q.empty()) {
            bool hit = lowerName(m.name).find(q) != std::string::npos || lowerName(m.note).find(q) != std::string::npos;
            for (const auto& r : m.reasons) hit = hit || lowerName(r).find(q) != std::string::npos;
            if (!hit) continue;
        }
        out.push_back(m);
    }
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

int Marks::exemptCount() const {
    int n = 0;
    for (const auto& kv : marks_) if (kv.second.exempt) ++n;
    return n;
}

}  // namespace vigil::detection
