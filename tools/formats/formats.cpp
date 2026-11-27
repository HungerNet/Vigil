// VigilFormats — the C++ side of the format fixtures shared with Rust (P2_PLAN.md, VALIDATION.md §8).
//
//   VigilFormats canon <kind> <in> <out>     canonical form (parse + write) of <in> read as <kind>
//                                            (json, trace, outcome, marks, profile, config);
//                                            exit 0 written · 3 refused (reason on stderr) · 2 usage
//   VigilFormats generate <fixtures-dir>     regenerates every generated fixture (deterministic)
//   VigilFormats canon-all <fixtures-dir>    writes canon/<kind>/<name> (or <name>.refused) for every
//                                            file in inputs/<kind>/
//   VigilFormats check <fixtures-dir>        regenerates both into memory and compares with the
//                                            committed files (exit 1 on any drift); also checks that
//                                            every canonical file is a fixed point
//   VigilFormats corpus <dir>                writes the scenario corpus as traces (cross-language
//                                            round trips over ~5 000 realistic frames)
//
// The Rust twin is `vigil-fmt canon` (rust/crates/vigil-formats). Both produce the same bytes for
// every input, or both refuse.

#include "../../src/core/json.h"
#include "../../src/detection/config.h"
#include "../../src/detection/engine.h"
#include "../../src/detection/marks.h"
#include "../../src/detection/replay.h"
#include "../../src/detection/snapshot.h"
#include "../../src/detection/trace.h"
#include "../../tests/corpus.h"

#include <algorithm>
#include <cfloat>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace vigil;
using namespace vigil::detection;

namespace {

bool readFile(const fs::path& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool writeFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    return static_cast<bool>(out << text);
}

std::string withoutCr(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    return s;
}

// One engine for the catalog and the schema (both are what this build declares).
Engine& engine() {
    static Engine e;
    static bool ready = false;
    if (!ready) {
        e.initialize();
        ready = true;
    }
    return e;
}

// ── Canonical forms ─────────────────────────────────────────────────────────────────────────

// Returns true and the canonical bytes, or false and the reason.
bool canon(const std::string& kind, const std::string& input, std::string& out, std::string& reason) {
    if (kind == "json") {
        json::Value v;
        if (!json::parse(input, v, &reason)) return false;
        out = json::write(v, -1);
        return true;
    }
    if (kind == "trace") {
        // readTrace reads a file; mirror it on the bytes through a temporary file.
        const fs::path tmp = fs::temp_directory_path() / "vigil-formats-trace.tmp";
        writeFile(tmp, input);
        TraceHeader h;
        std::vector<WorldFrame> frames;
        const bool ok = readTrace(tmp.string(), h, frames, reason);
        std::error_code ec;
        fs::remove(tmp, ec);
        if (!ok) return false;
        out = traceHeaderLine(h) + "\n";
        for (const auto& f : frames) out += traceFrameLine(f) + "\n";
        return true;
    }
    if (kind == "outcome") {
        ReplayOutcome o;
        if (!outcomeFromJson(input, o, reason)) return false;
        out = outcomeToJson(o);
        return true;
    }
    if (kind == "marks") {
        // Marks loads from and saves to a path.
        const fs::path tmp = fs::temp_directory_path() / "vigil-formats-marks.tmp.json";
        writeFile(tmp, input);
        Marks m;
        m.setPath(tmp.string());
        bool ok = false;
        if (m.readOnly()) {
            reason = "read-only: " + m.loadNote();
        } else {
            ok = m.save() && readFile(tmp, out);
            if (!ok) reason = "could not save";
        }
        std::error_code ec;
        fs::remove(tmp, ec);
        return ok;
    }
    if (kind == "profile") {
        const CheckCatalog cat = CheckCatalog::of(engine());
        ProfileSnapshot s;
        if (!snapshotFromJson(input, cat, s, reason)) return false;
        out = snapshotToJson(s, cat);
        return true;
    }
    if (kind == "config") {
        EngineConfig c;
        if (!configFromJson(input, engine().schemaPtr(), c, reason)) return false;
        out = configToJson(c);
        return true;
    }
    reason = "unknown kind " + kind;
    return false;
}

const char* const kKinds[] = {"json", "trace", "outcome", "marks", "profile", "config"};

// ── Generated fixtures ──────────────────────────────────────────────────────────────────────

std::string bitsHex(double d) {
    std::uint64_t b;
    std::memcpy(&b, &d, sizeof b);
    char buf[24];
    std::snprintf(buf, sizeof buf, "%016" PRIx64, b);
    return buf;
}

// Doubles the number formatter must reproduce: special values, every regime of %.0f/%.15g/%.17g,
// ties, subnormals, values the engine writes (floats promoted, 1/32 positions), random bits.
std::string numberTable() {
    std::vector<double> v = {0.0, -0.0, 1.0, -1.0, 0.1, 0.5, 0.4, 2.5, 1.0 / 3.0, 2.0 / 3.0, 0.1 + 0.2, 64.03125,
                             1e15, 1e15 - 1, 1e15 + 1, -1e15, 999999999999999.9, 999999999999999.5, 1e16, 9007199254740992.0,
                             9007199254740994.0, 123456789012345.6, 100000000000000.5, 1e-5, 1e-4, 9.9999e-5, 0.0001,
                             0.00009999999999999999, 1e21, 1e22, 1e100, 1e-100, 1.7976931348623157e308, 2.2250738585072014e-308,
                             4.9406564584124654e-324, -4.9406564584124654e-324, 5e-324, 123456.789012345, 0.356123456789,
                             std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()};
    for (int e = -1074; e <= 1023; e += 7) v.push_back(std::ldexp(1.0, e));
    for (int e = -20; e <= 20; ++e) {
        for (int k : {0, 1, 4, 9, 12345}) v.push_back((k + 0.5) * std::pow(10.0, e));
    }
    std::mt19937_64 rng(20261127);
    for (int i = 0; i < 2000; ++i) {
        std::uint64_t b = rng();
        double d;
        std::memcpy(&d, &b, sizeof d);
        if (std::isfinite(d)) v.push_back(d);
    }
    std::uniform_int_distribution<std::int64_t> kd(1, 999999999);
    std::uniform_int_distribution<int> nd(0, 20);
    for (int i = 0; i < 1000; ++i) v.push_back(static_cast<double>(kd(rng)) / std::pow(10.0, nd(rng)) * (i % 2 ? -1 : 1));
    std::uniform_real_distribution<double> wide(-1000.0, 1000.0);
    for (int i = 0; i < 800; ++i) v.push_back(static_cast<double>(static_cast<float>(wide(rng))));
    for (int i = 0; i < 300; ++i) v.push_back(std::round(wide(rng) * 32.0) / 32.0);
    std::string out = "# bits<TAB>text written by core/json (VigilFormats generate)\n";
    for (double d : v) out += bitsHex(d) + "\t" + json::write(json::Value(d), -1) + "\n";
    return out;
}

// The math functions the engine uses, on its input ranges: whether Rust's f64 functions return the
// same bits as the MSVC CRT (RUST_PLAN.md §8, floating-point parity).
std::string mathTable() {
    std::mt19937_64 rng(1127);
    std::uniform_real_distribution<double> angle(-12.6, 12.6), coord(-60.0, 60.0), unit(0.0, 1.0), yaw(-2000.0, 2000.0);
    std::string out = "# function<TAB>a<TAB>b<TAB>result (bits; VigilFormats generate)\n";
    auto row = [&](const char* fn, double a, double b, double r) { out += std::string(fn) + "\t" + bitsHex(a) + "\t" + bitsHex(b) + "\t" + bitsHex(r) + "\n"; };
    for (int i = 0; i < 300; ++i) { const double a = angle(rng); row("sin", a, 0.0, std::sin(a)); }
    for (int i = 0; i < 300; ++i) { const double a = angle(rng); row("cos", a, 0.0, std::cos(a)); }
    for (int i = 0; i < 300; ++i) { const double y = coord(rng), x = coord(rng); row("atan2", y, x, std::atan2(y, x)); }
    for (int i = 0; i < 300; ++i) {
        // Decay and friction: pow(0.5, t/halfLife), pow(0.91, n), pow(0.546, n), and general bases.
        const double base = i % 4 == 0 ? 0.5 : i % 4 == 1 ? 0.91 : i % 4 == 2 ? 0.546 : unit(rng) * 2.0;
        const double ex = i % 4 == 0 ? unit(rng) * 40.0 : static_cast<double>(i % 40);
        row("pow", base, ex, std::pow(base, ex));
    }
    for (int i = 0; i < 200; ++i) { const double a = unit(rng) * 1e4; row("sqrt", a, 0.0, std::sqrt(a)); }
    for (int i = 0; i < 200; ++i) { const double a = yaw(rng); row("fmod", a, 360.0, std::fmod(a, 360.0)); }
    for (int i = 0; i < 100; ++i) { const double a = coord(rng) * 1000.0; row("floor", a, 0.0, std::floor(a)); row("ceil", a, 0.0, std::ceil(a)); row("round", a, 0.0, std::round(a)); }
    for (int i = 0; i < 100; ++i) { const double a = coord(rng), b = coord(rng); row("fmax", a, b, std::fmax(a, b)); }
    return out;
}

// A trace that sets every field, default and not, with hostile strings.
std::string edgeTrace() {
    TraceHeader h;
    h.vigil = "fixture";
    h.build = "edge \"quoted\" \\ build";
    h.startedEpochMs = 1764201600123.0;
    h.note = std::string("tab\there, newline\nthere, NUL\x01 and bytes \xC0\x80 \xED\xA0\xBD\xED\xB8\x80 \xFF");
    std::string out = traceHeaderLine(h) + "\n";
    WorldFrame f;
    f.tick = 4503599627370497;   // beyond int32
    f.timeMs = 1234.5625;
    f.skippedTicks = 7;
    f.context = GameContext::Replay;
    f.contextDetail = "Bed Wars \xC2\xB7 4v4v4v4";
    f.worldChanged = true;
    f.chat = {"plain", "", std::string("\x1b[31mcolor\x7f"), "emoji \xF0\x9F\x98\x80", std::string("\xED\xA0\xBD\xED\xB8\x80 modified UTF-8")};
    PlayerFrame a;
    a.entityId = 2147483647;
    a.uuid = "00000000-0000-4000-8000-000000000001";
    a.name = "Every_Field";
    a.isLocal = a.isTeammate = a.serverPosition = a.onGround = a.sprinting = a.sneaking = a.usingItem = a.blocking = true;
    a.invisible = a.riding = a.inWater = a.inLava = a.onLadder = a.inWeb = a.dead = a.swingInProgress = true;
    a.potionsKnown = a.potionParticles = a.sprintModifier = a.wallNear = a.burning = a.tabKnown = a.tabListed = true;
    a.pos = {-0.0, 1e-300, 29999999.96875};
    a.yaw = -179.9999847f;
    a.pitch = 90.f;
    a.heldItem = 0;
    a.swingProgress = -3;
    a.hurtTime = 10;
    a.health = 0.f;
    a.potions = {4, 0, 1, 12};
    a.moveSpeedAttribute = 0.13000000000000001;
    a.blockBelow = 174;
    a.blockAboveHead = 0;
    a.wallMask = 0xFF;
    a.pingMs = 0;
    PlayerFrame b;   // every field at its default (and so omitted)
    b.pos = {1.0 / 3.0, 64.0, -1e15};
    PlayerFrame c;
    c.entityId = -2147483647;
    c.name = "only \"potions\" count";
    c.potions = {-1, -1, -1, 1};
    c.health = std::numeric_limits<float>::denorm_min();
    c.yaw = 1e-45f;
    f.players = {a, b, c};
    out += traceFrameLine(f) + "\n";
    WorldFrame g;   // a frame with nothing in it
    g.tick = -1;
    out += traceFrameLine(g) + "\n";
    return out;
}

// Real saved profiles: players from corpus sessions, captured as the Records page saves them.
std::string sessionProfile(const std::vector<WorldFrame>& frames, const std::string& player) {
    Engine e;
    e.initialize();
    replayFrames(e, frames);
    for (const PlayerRecord& r : e.session().players()) {
        if (r.name != player) continue;
        ProfileSnapshot s = captureSnapshot(e, r);
        s.vigilVersion = "fixture";
        s.savedAtEpochMs = 1764201600000.0;
        s.note = "Saved for the format fixtures \xE2\x80\x94 \"quotes\", a tab\t and a newline\n.";
        s.nicked = true;
        s.likelyAccount = "Likely_Main";
        s.candidates = {"Likely_Main", "Other"};
        s.reasons = {"KillAura", "Reach"};
        s.watched = s.confirmed = true;
        return snapshotToJson(s, CheckCatalog::of(e));
    }
    return {};
}

std::string catalogJson() {
    const CheckCatalog cat = CheckCatalog::of(engine());
    json::Value root = json::Value::object();
    root["kind"] = json::Value("vigil-check-catalog");
    json::Value checks = json::Value::array();
    for (std::size_t i = 0; i < cat.checks.size(); ++i) {
        json::Value c = json::Value::object();
        c["id"] = json::Value(cat.checks[i]);
        json::Value inds = json::Value::array();
        for (const auto& ind : cat.indicators[i]) inds.push(json::Value(ind));
        c["indicators"] = inds;
        checks.push(c);
    }
    root["checks"] = checks;
    return json::write(root, 2) + "\n";
}

std::string configStrict() {
    EngineConfig c(engine().schemaPtr());
    for (const auto& chk : engine().checks()) {
        for (const PresetEntry& p : chk->presets()) c.set(p.param, p.strict);
    }
    return configToJson(c) + "\n";
}

std::string configOdd() {
    const auto schema = engine().schemaPtr();
    EngineConfig c(schema);
    for (std::size_t i = 0; i < schema->size(); ++i) {
        const ParamSpec& p = schema->params()[i];
        const ParamId id = static_cast<ParamId>(i);
        if (p.type == ParamType::Bool) c.set(id, p.def != 0.0 ? 0.0 : 1.0);
        else if (p.type == ParamType::Float) c.set(id, p.def + 0.1 + 1.0 / 3.0);
        else c.set(id, p.def + 1.0);
    }
    return configToJson(c, -1) + "\n";
}

// All 256 byte values, and the sequences JNI modified UTF-8 produces, inside strings and keys.
std::string stringsJson() {
    json::Value root = json::Value::object();
    std::string all;
    for (int i = 0; i < 256; ++i) all.push_back(static_cast<char>(i));
    root["all bytes"] = json::Value(all);
    root["modified utf-8"] = json::Value(std::string("NUL \xC0\x80, emoji \xED\xA0\xBD\xED\xB8\x80"));
    root[std::string("key with \x01 and \xFF bytes")] = json::Value("value");
    root["empty"] = json::Value("");
    return json::write(root, 2) + "\n";
}

// name → contents of every generated input (relative to the fixtures directory).
std::map<std::string, std::string> generated() {
    std::map<std::string, std::string> g;
    g["numbers.txt"] = numberTable();
    g["math.txt"] = mathTable();
    g["check-catalog.json"] = catalogJson();
    g["inputs/json/strings.json"] = stringsJson();
    g["inputs/trace/edge.vtrace"] = edgeTrace();
    const auto duels = vigil::test::corpus::duels();
    g["inputs/profile/session-aura.json"] = sessionProfile(duels, "Aura");
    g["inputs/profile/session-hopper.json"] = sessionProfile(vigil::test::goldenArena(), "LowHopper");
    g["inputs/config/strict.json"] = configStrict();
    g["inputs/config/odd.json"] = configOdd();
    return g;
}

// relative path → canonical file contents (".refused" files hold the reason).
std::map<std::string, std::string> canonAll(const fs::path& dir) {
    std::map<std::string, std::string> out;
    for (const char* kind : kKinds) {
        const fs::path in = dir / "inputs" / kind;
        if (!fs::exists(in)) continue;
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(in)) if (e.is_regular_file()) files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            std::string text, canonical, reason;
            readFile(f, text);
            const std::string rel = std::string("canon/") + kind + "/" + f.filename().string();
            if (canon(kind, text, canonical, reason)) out[rel] = canonical;
            else out[rel + ".refused"] = reason + "\n";
        }
    }
    return out;
}

int writeAll(const fs::path& dir, const std::map<std::string, std::string>& files) {
    for (const auto& kv : files) {
        if (!writeFile(dir / kv.first, kv.second)) { std::printf("cannot write %s\n", kv.first.c_str()); return 2; }
    }
    std::printf("wrote %zu files under %s\n", files.size(), dir.string().c_str());
    return 0;
}

int check(const fs::path& dir) {
    int problems = 0;
    auto compare = [&](const std::map<std::string, std::string>& expected) {
        for (const auto& kv : expected) {
            std::string committed;
            if (!readFile(dir / kv.first, committed)) {
                std::printf("  MISSING %s\n", kv.first.c_str());
                ++problems;
            } else if (withoutCr(committed) != kv.second) {
                std::printf("  DRIFT   %s (regenerate: VigilFormats generate / canon-all)\n", kv.first.c_str());
                ++problems;
            }
        }
    };
    const auto gen = generated();
    compare(gen);
    const auto can = canonAll(dir);
    compare(can);
    // Nothing committed that the code no longer produces.
    if (fs::exists(dir / "canon")) {
        for (const auto& e : fs::recursive_directory_iterator(dir / "canon")) {
            if (!e.is_regular_file()) continue;
            const std::string rel = fs::relative(e.path(), dir).generic_string();
            if (!can.count(rel)) { std::printf("  STALE   %s\n", rel.c_str()); ++problems; }
        }
    }
    // Every canonical form is a fixed point.
    int fixed = 0;
    for (const auto& kv : can) {
        if (kv.first.size() > 8 && kv.first.compare(kv.first.size() - 8, 8, ".refused") == 0) continue;
        const std::string kind = kv.first.substr(6, kv.first.find('/', 6) - 6);
        std::string again, reason;
        if (!canon(kind, kv.second, again, reason) || again != kv.second) {
            std::printf("  NOT A FIXED POINT %s (%s)\n", kv.first.c_str(), reason.c_str());
            ++problems;
        } else {
            ++fixed;
        }
    }
    std::printf("%zu generated files, %zu canonical files (%d fixed points): %s\n", gen.size(), can.size(), fixed,
                problems ? "DIFFERENCES" : "all match the committed fixtures");
    return problems ? 1 : 0;
}

int usage() {
    std::printf("usage: VigilFormats canon <kind> <in> <out> | generate <dir> | canon-all <dir> | check <dir> | corpus <dir>\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string cmd = argv[1];
    if (cmd == "canon") {
        if (argc < 5) return usage();
        std::string text, out, reason;
        if (!readFile(argv[3], text)) { std::printf("cannot read %s\n", argv[3]); return 2; }
        if (!canon(argv[2], text, out, reason)) {
            std::fprintf(stderr, "refused: %s\n", reason.c_str());
            return 3;
        }
        return writeFile(argv[4], out) ? 0 : 2;
    }
    if (cmd == "generate") return writeAll(argv[2], generated());
    if (cmd == "canon-all") return writeAll(argv[2], canonAll(argv[2]));
    if (cmd == "check") return check(argv[2]);
    if (cmd == "corpus") {
        std::map<std::string, std::string> files;
        for (const auto& sc : vigil::test::scenarioCorpus()) {
            TraceHeader h;
            h.vigil = "corpus";
            h.build = "synthetic";
            h.note = sc.name;
            std::string text = traceHeaderLine(h) + "\n";
            for (const auto& f : sc.frames) text += traceFrameLine(f) + "\n";
            files[sc.name + ".vtrace"] = text;
        }
        return writeAll(argv[2], files);
    }
    return usage();
}
