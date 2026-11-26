#pragma once
// Vigil detection framework — replaying traces and comparing outcomes (docs/VALIDATION.md).
//
// An outcome is what an engine concluded from a frame sequence, per player and per check: how
// many observations, how many detections, the final and peak confidence. Two engines (two
// versions, or C++ and a future Rust port) agree on a trace when their outcomes are equal.

#include "types.h"

#include <map>
#include <string>
#include <vector>

namespace vigil::detection {

class Engine;

struct PlayerOutcome {
    std::string name;
    float suspicion = 0.f;
    float peak = 0.f;
    std::map<std::string, int> evidence;     // check id → observations
    std::map<std::string, int> detections;   // check id → detections
};

struct ReplayOutcome {
    int frames = 0;
    std::map<std::string, PlayerOutcome> players;   // keyed by name (stable across runs)
};

// Feeds `frames` to `engine` (initialized, fresh session) and summarizes the result.
ReplayOutcome replayFrames(Engine& engine, const std::vector<WorldFrame>& frames);
ReplayOutcome summarize(const Engine& engine, int frames);

std::string outcomeToJson(const ReplayOutcome& o);
bool outcomeFromJson(const std::string& text, ReplayOutcome& out, std::string& error);
// Differences between an expected and an actual outcome; empty when they agree. Confidence values
// compare within `tolerance` points; counts compare exactly.
std::vector<std::string> compareOutcomes(const ReplayOutcome& expected, const ReplayOutcome& actual, float tolerance = 0.01f);

}  // namespace vigil::detection
