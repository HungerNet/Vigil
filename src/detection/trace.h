#pragma once
// Vigil detection framework — diagnostic traces (docs/VALIDATION.md, docs/LIVE_VALIDATION.md).
//
// A trace is the exact sequence of WorldFrames the engine received: what Vigil saw, nothing it
// concluded. Replaying a trace through an engine reproduces that session's evidence, confidence
// and detections bit-for-bit (the engine is deterministic for a given frame sequence and settings).
// That makes traces three things at once:
//
//   live validation   the user records a session in game; a developer replays it offline
//   regression tests  a replayed trace plus its expected outcome is a golden test (tests/traces)
//   migration parity  a future Rust engine must produce the same outcome from the same trace
//                     (RUST_PLAN.md §6)
//
// Format: UTF-8 JSON lines. Line 1 is the header {"kind":"vigil-trace","format":1,...}; each
// further line is one frame. Player fields use short keys and are omitted when they hold their
// default value. Numbers round-trip exactly. Traces contain player names; they stay on the user's
// computer unless the user sends one.

#include "types.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace vigil::detection {

struct TraceHeader {
    static constexpr int kFormat = 1;
    int format = kFormat;
    std::string vigil;          // version that recorded it
    std::string build;
    double startedEpochMs = 0.0;
    std::string note;
};

std::string traceHeaderLine(const TraceHeader& h);
bool parseTraceHeader(const std::string& line, TraceHeader& out, std::string& error);
std::string traceFrameLine(const WorldFrame& f);
bool parseTraceFrame(const std::string& line, WorldFrame& out, std::string& error);
// Reads a whole trace file (bounded: 2 GB is never reached; recordings are capped when written).
bool readTrace(const std::string& path, TraceHeader& header, std::vector<WorldFrame>& frames, std::string& error);

// Appends frames to a trace file, stopping on its own at a size or duration cap.
class TraceWriter {
public:
    static constexpr std::uint64_t kDefaultMaxBytes = 64ull * 1024 * 1024;
    static constexpr int kDefaultMaxFrames = 20 * 60 * 15;   // 15 minutes of client ticks

    bool start(const std::string& path, const TraceHeader& header, std::uint64_t maxBytes = kDefaultMaxBytes,
               int maxFrames = kDefaultMaxFrames);
    void write(const WorldFrame& f);
    void stop(const std::string& reason = "stopped");
    bool active() const { return active_; }
    const std::string& path() const { return path_; }
    std::uint64_t bytes() const { return bytes_; }
    int frames() const { return frames_; }
    const std::string& stopReason() const { return stopReason_; }

private:
    std::ofstream out_;
    std::string path_;
    std::string stopReason_;
    std::uint64_t bytes_ = 0, maxBytes_ = 0;
    int frames_ = 0, maxFrames_ = 0;
    bool active_ = false;
};

}  // namespace vigil::detection
