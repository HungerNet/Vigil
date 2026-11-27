// VigilReplay — replays a diagnostic trace through the detection engine (docs/VALIDATION.md).
//
//   VigilReplay <trace.vtrace>                         prints what the engine concluded
//   VigilReplay <trace.vtrace> --json <out.json>       writes the outcome
//   VigilReplay <trace.vtrace> --expect <exp.json>     compares with an expected outcome (exit 1 on a difference)
//   VigilReplay <trace.vtrace> --write-expect <exp.json>  records the current outcome as the expectation
//   VigilReplay <trace.vtrace> --config <cfg.json> …    replays with an engine configuration document
//                                                      (vigil-engine-config) instead of the defaults
//   VigilReplay --synthesize <out.vtrace>              writes the deterministic golden scenario
//   VigilReplay --engine-schema <out.json>             writes the engine's parameter declaration
//   VigilReplay --engine-config <out.json>             writes the default engine configuration
//
// Traces come from Status → Diagnostics → Record trace (in game) or from --synthesize.

#include "../../src/core/json.h"
#include "../../src/detection/config.h"
#include "../../src/detection/engine.h"
#include "../../src/detection/replay.h"
#include "../../src/detection/trace.h"
#include "../../tests/scenarios.h"
#include "core/version.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using namespace vigil::detection;

namespace {

int usage() {
    std::printf("usage: VigilReplay <trace> [--config cfg.json] [--json out.json] [--expect expected.json] [--write-expect expected.json]\n"
                "       VigilReplay --synthesize <out.vtrace>\n"
                "       VigilReplay --engine-schema <out.json> | --engine-config <out.json>\n");
    return 2;
}

bool readText(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool writeText(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    return static_cast<bool>(out << text);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    if (std::strcmp(argv[1], "--synthesize") == 0) {
        if (argc < 3) return usage();
        const auto frames = vigil::test::goldenArena();
        TraceHeader h;
        h.vigil = VIGIL_VERSION_STRING;
        h.build = "synthetic";
        h.note = "goldenArena (tests/scenarios.h)";
        std::string text = traceHeaderLine(h) + "\n";
        for (const auto& f : frames) text += traceFrameLine(f) + "\n";
        if (!writeText(argv[2], text)) { std::printf("cannot write %s\n", argv[2]); return 1; }
        std::printf("wrote %zu frames to %s\n", frames.size(), argv[2]);
        return 0;
    }
    if (std::strcmp(argv[1], "--engine-schema") == 0 || std::strcmp(argv[1], "--engine-config") == 0) {
        if (argc < 3) return usage();
        Engine engine;
        engine.initialize();
        const bool schema = std::strcmp(argv[1], "--engine-schema") == 0;
        const std::string text = (schema ? schemaToJson(engine.schema()) : configToJson(engine.config())) + "\n";
        if (!writeText(argv[2], text)) { std::printf("cannot write %s\n", argv[2]); return 1; }
        std::printf("wrote the engine %s (%zu parameters) to %s\n", schema ? "schema" : "default configuration",
                    engine.schema().size(), argv[2]);
        return 0;
    }
    TraceHeader header;
    std::vector<WorldFrame> frames;
    std::string error;
    if (!readTrace(argv[1], header, frames, error)) {
        std::printf("cannot read trace: %s\n", error.c_str());
        return 1;
    }
    Engine engine;
    engine.initialize();
    for (int i = 2; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--config") != 0) continue;
        std::string text;
        EngineConfig config;
        ConfigReadReport report;
        if (!readText(argv[i + 1], text) || !configFromJson(text, engine.schemaPtr(), config, error, &report) || !engine.setConfig(config)) {
            std::printf("cannot use configuration %s: %s\n", argv[i + 1], error.empty() ? "unreadable" : error.c_str());
            return 1;
        }
        std::printf("configuration %s (%zu unknown keys ignored, %zu defaults kept)\n", argv[i + 1], report.unknown.size(),
                    report.missing.size());
    }
    const ReplayOutcome outcome = replayFrames(engine, frames);
    std::printf("trace recorded by Vigil %s (build %s), %d frames\n", header.vigil.c_str(), header.build.c_str(), outcome.frames);
    for (const auto& kv : outcome.players) {
        std::printf("  %-18s suspicion %5.1f%%  peak %5.1f%%", kv.first.c_str(), kv.second.suspicion, kv.second.peak);
        for (const auto& d : kv.second.detections) std::printf("  %s:%d", d.first.c_str(), d.second);
        std::printf("\n");
    }
    int code = 0;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string opt = argv[i], path = argv[i + 1];
        if (opt == "--json" || opt == "--write-expect") {
            if (!writeText(path, outcomeToJson(outcome))) { std::printf("cannot write %s\n", path.c_str()); code = 1; }
        } else if (opt == "--expect") {
            std::string text;
            ReplayOutcome expected;
            if (!readText(path, text) || !outcomeFromJson(text, expected, error)) {
                std::printf("cannot read expectation %s: %s\n", path.c_str(), error.c_str());
                return 1;
            }
            const auto diffs = compareOutcomes(expected, outcome);
            for (const auto& d : diffs) std::printf("  DIFF %s\n", d.c_str());
            std::printf(diffs.empty() ? "outcome matches the expectation\n" : "%zu differences\n", diffs.size());
            if (!diffs.empty()) code = 1;
        } else if (opt != "--config") {
            return usage();
        }
    }
    return code;
}
