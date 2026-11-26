#include "replay.h"

#include "../core/json.h"
#include "engine.h"

#include <cmath>
#include <cstdio>

namespace vigil::detection {

ReplayOutcome summarize(const Engine& engine, int frames) {
    ReplayOutcome o;
    o.frames = frames;
    const auto& checks = engine.checks();
    for (const PlayerRecord& r : engine.session().players()) {
        PlayerOutcome p;
        p.name = r.name;
        p.suspicion = r.suspicion;
        p.peak = r.peakSuspicion;
        for (std::size_t i = 0; i < r.checks.size() && i < checks.size(); ++i) {
            if (r.checks[i].evidence) p.evidence[checks[i]->id()] = r.checks[i].evidence;
            if (r.checks[i].detections) p.detections[checks[i]->id()] = r.checks[i].detections;
        }
        o.players[r.name] = p;
    }
    return o;
}

ReplayOutcome replayFrames(Engine& engine, const std::vector<WorldFrame>& frames) {
    if (!frames.empty()) engine.resetSession(frames.front().timeMs);
    for (const WorldFrame& f : frames) engine.onFrame(f);
    return summarize(engine, static_cast<int>(frames.size()));
}

std::string outcomeToJson(const ReplayOutcome& o) {
    json::Value root = json::Value::object();
    root["kind"] = json::Value("vigil-outcome");
    root["format"] = json::Value(1);
    root["frames"] = json::Value(o.frames);
    json::Value& players = root["players"];
    players = json::Value::object();
    for (const auto& kv : o.players) {
        json::Value p = json::Value::object();
        // Rounded so expectation files stay readable; comparisons use a tolerance.
        p["suspicion"] = json::Value(std::round(kv.second.suspicion * 100.0) / 100.0);
        p["peak"] = json::Value(std::round(kv.second.peak * 100.0) / 100.0);
        json::Value ev = json::Value::object();
        for (const auto& e : kv.second.evidence) ev[e.first] = json::Value(e.second);
        p["evidence"] = ev;
        json::Value det = json::Value::object();
        for (const auto& d : kv.second.detections) det[d.first] = json::Value(d.second);
        p["detections"] = det;
        players[kv.first] = p;
    }
    return json::write(root, 2);
}

bool outcomeFromJson(const std::string& text, ReplayOutcome& out, std::string& error) {
    json::Value root;
    if (!json::parse(text, root, &error) || root.get("kind").asString() != "vigil-outcome") {
        if (error.empty()) error = "not a Vigil outcome file";
        return false;
    }
    out = ReplayOutcome{};
    out.frames = static_cast<int>(root.get("frames").asNumber());
    for (const auto& kv : root.get("players").members()) {
        PlayerOutcome p;
        p.name = kv.first;
        p.suspicion = static_cast<float>(kv.second.get("suspicion").asNumber());
        p.peak = static_cast<float>(kv.second.get("peak").asNumber());
        for (const auto& e : kv.second.get("evidence").members()) p.evidence[e.first] = static_cast<int>(e.second.asNumber());
        for (const auto& d : kv.second.get("detections").members()) p.detections[d.first] = static_cast<int>(d.second.asNumber());
        out.players[kv.first] = p;
    }
    return true;
}

std::vector<std::string> compareOutcomes(const ReplayOutcome& expected, const ReplayOutcome& actual, float tolerance) {
    std::vector<std::string> diffs;
    char buf[256];
    if (expected.frames != actual.frames) {
        std::snprintf(buf, sizeof(buf), "frames: expected %d, got %d", expected.frames, actual.frames);
        diffs.push_back(buf);
    }
    for (const auto& kv : expected.players) {
        auto it = actual.players.find(kv.first);
        if (it == actual.players.end()) { diffs.push_back(kv.first + ": missing"); continue; }
        const PlayerOutcome& e = kv.second;
        const PlayerOutcome& a = it->second;
        if (std::fabs(e.suspicion - a.suspicion) > tolerance) {
            std::snprintf(buf, sizeof(buf), "%s: suspicion expected %.2f, got %.2f", kv.first.c_str(), e.suspicion, a.suspicion);
            diffs.push_back(buf);
        }
        if (std::fabs(e.peak - a.peak) > tolerance) {
            std::snprintf(buf, sizeof(buf), "%s: peak expected %.2f, got %.2f", kv.first.c_str(), e.peak, a.peak);
            diffs.push_back(buf);
        }
        auto cmp = [&](const std::map<std::string, int>& x, const std::map<std::string, int>& y, const char* what) {
            std::map<std::string, int> keys = x;
            for (const auto& k : y) keys.emplace(k.first, 0);
            for (const auto& k : keys) {
                const int ex = x.count(k.first) ? x.at(k.first) : 0;
                const int ac = y.count(k.first) ? y.at(k.first) : 0;
                if (ex != ac) {
                    std::snprintf(buf, sizeof(buf), "%s: %s %s expected %d, got %d", kv.first.c_str(), k.first.c_str(), what, ex, ac);
                    diffs.push_back(buf);
                }
            }
        };
        cmp(e.evidence, a.evidence, "evidence");
        cmp(e.detections, a.detections, "detections");
    }
    for (const auto& kv : actual.players) {
        if (!expected.players.count(kv.first)) diffs.push_back(kv.first + ": not expected");
    }
    return diffs;
}

}  // namespace vigil::detection
