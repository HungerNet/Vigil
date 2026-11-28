// VigilParity — full-state engine parity probe (P1_PLAN.md §3, docs/VALIDATION.md).
//
//   VigilParity <out.txt> [--quick]
//
// Replays the scenario corpus (tests/corpus.h) through fresh engines under many configurations
// and writes, for every (configuration, scenario), a digest of the *entire* engine state: every
// evidence item with its values and envelope, contributions and confidence, detections, timelines,
// confidence histories, behavior statistics, alerts, debug lines, combat events and player states.
// It also dumps the settings registry the engine declares (order, metadata, warning predicates).
//
// Two builds agree when their outputs are byte-identical. Configurations:
//   defaults · presets (Relaxed, Strict) · every engine parameter moved up and down on its own ·
//   mid-session changes (monitoring paused, preset switched, debug on, checks disabled, sampling,
//   weights, roles) applied between frames, the way the UI changes settings.
//
// Parameters reach the engine through the registry exactly as in the DLL (SettingsSync). (In P1 the
// default mode also compiled against the pre-P1 engine to produce that phase's baseline; the P3A
// modes below need EngineConfig, so the tool now requires the P1 engine.)
//
// P3A (P3A_PLAN.md §2) — the C++ half of the differential harness:
//
//   VigilParity <out.txt> [--quick] --export <dir>
//       also writes dir/corpus/<scenario>.vtrace and dir/plan.jsonl: for every run, the trace and
//       every configuration snapshot that reached the engine, with the frame before which it
//       arrived (-1 = before resetSession), as differences from the schema defaults. The Rust
//       engine replays the plan (`vigil-parity <dir> <out>`) and must write the same <out.txt>.
//   VigilParity --generate <dir> [--count N] [--seed S]
//       N generated scenarios (tests/generator.h): traces, marks files, plan.jsonl and the C++
//       result dir/expected.txt (one digest line and one summary line per scenario).
//   VigilParity --full <variant> <scenario> <dump.txt>    (with either mode's other arguments)
//       also writes the full state of that one run, to locate a difference.
//   VigilParity --bench <reps>
//       engine tick cost on the corpus under the defaults (twin: vigil-parity --bench).

#include "../../src/core/json.h"
#include "../../src/core/settings.h"
#include "../../src/detection/engine.h"
#include "../../src/detection/trace.h"
#include "../../tests/corpus.h"
#include "../../tests/generator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>

#if __has_include("../../src/detection/config_binding.h")
#include "../../src/detection/config_binding.h"
#define VIGIL_PARITY_ENGINE_CONFIG 1
#else
#define VIGIL_PARITY_ENGINE_CONFIG 0
#endif

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace vigil;
using namespace vigil::detection;

namespace {

struct Out {
    std::string s;
    void line(const char* f, ...) {
        char buf[4096];
        va_list ap;
        va_start(ap, f);
        std::vsnprintf(buf, sizeof(buf), f, ap);
        va_end(ap);
        s += buf;
        s += '\n';
    }
};

std::uint64_t fnv(const std::string& s) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

const char* typeName(SettingType t) {
    switch (t) {
        case SettingType::Bool: return "bool";
        case SettingType::Int: return "int";
        case SettingType::Float: return "float";
        case SettingType::Choice: return "choice";
        case SettingType::Key: return "key";
        default: return "text";
    }
}

// ── Host glue (the only lines that differ between the two engine generations) ───────────────

#if VIGIL_PARITY_ENGINE_CONFIG
SettingsSync g_sync;
#endif

// ── Plan capture (--export, P3A) ────────────────────────────────────────────────────────────

// A configuration snapshot as differences from the schema defaults ({key: value}, exact numbers).
json::Value configDiff(const EngineConfig& c) {
    json::Value values = json::Value::object();
    const ConfigSchema& s = *c.schema();
    for (std::size_t i = 0; i < s.size(); ++i) {
        const ParamSpec& p = s.params()[i];
        const double v = c.value(static_cast<ParamId>(i));
        if (std::memcmp(&v, &p.def, sizeof v) == 0) continue;
        values[p.key] = p.type == ParamType::Bool ? json::Value(v != 0.0) : json::Value(v);
    }
    return values;
}

struct Capture {
    bool on = false;
    json::Value configs = json::Value::array();
    void record(const Engine& e, long long frame) {
        if (!on) return;
        json::Value c = json::Value::object();
        c["frame"] = json::Value(static_cast<double>(frame));
        c["values"] = configDiff(e.config());
        configs.push(std::move(c));
    }
};
Capture g_capture;

void initEngine(Engine& e) {
    e.initialize();
#if VIGIL_PARITY_ENGINE_CONFIG
    registerEngineSettings(Settings::instance(), e.schema());
    g_sync = SettingsSync{};
#endif
}

void syncEngine(Engine& e, long long frame = -1) {
#if VIGIL_PARITY_ENGINE_CONFIG
    if (g_sync.sync(e, Settings::instance())) g_capture.record(e, frame);
#else
    (void)e;
    (void)frame;
#endif
}

bool writeText(const std::filesystem::path& p, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    return static_cast<bool>(out << text);
}

std::string traceText(const std::string& note, const std::vector<WorldFrame>& frames) {
    TraceHeader h;
    h.vigil = "parity";
    h.build = "synthetic";
    h.note = note;
    std::string text = traceHeaderLine(h) + "\n";
    for (const auto& f : frames) text += traceFrameLine(f) + "\n";
    return text;
}

void presetAll(Engine& e, int preset) {
    for (const auto& c : e.checks()) {
#if VIGIL_PARITY_ENGINE_CONFIG
        applyPreset(Settings::instance(), *c, preset);
#else
        c->applyPreset(preset);
#endif
    }
}

// ── Registry dump ───────────────────────────────────────────────────────────────────────────

void dumpRegistry(Out& o) {
    const auto& all = Settings::instance().all();
    o.line("registry %zu", all.size());
    for (const auto& sp : all) {
        const Setting& s = *sp;
        o.line("setting %s|%s|%s|%s|adv=%d|def=%.17g|value=%.17g|range=%.17g,%.17g,%.17g,%d,%s|deferred=%d", s.key.c_str(),
               typeName(s.type), s.label.c_str(), s.group.c_str(), s.advanced ? 1 : 0, s.def, s.value, s.range.min, s.range.max,
               s.range.step, s.range.decimals, s.range.unit ? s.range.unit : "", s.deferred ? 1 : 0);
        o.line("  tooltip %s", s.tooltip.c_str());
        if (!s.warning.empty()) o.line("  warning %s", s.warning.c_str());
        std::string choices;
        for (const auto& c : s.choices) choices += c + ";";
        if (!choices.empty()) o.line("  choices %s", choices.c_str());
        // The warning predicate, probed across the range (and a few special values).
        std::string probes;
        Setting copy = s;
        std::vector<double> at = {s.def, 0.0, 1.0, -1.0, 2.0};
        const double lo = s.type == SettingType::Bool ? 0.0 : s.range.min;
        const double hi = s.type == SettingType::Bool ? 1.0 : s.type == SettingType::Choice ? static_cast<double>(s.choices.size()) : s.range.max;
        for (int k = 0; k <= 40; ++k) at.push_back(lo + (hi - lo) * k / 40.0);
        for (double v : at) {
            copy.value = v;
            probes += copy.warningActive() ? '1' : '0';
        }
        o.line("  warnWhen %s", probes.c_str());
    }
}

// ── Engine state dump ───────────────────────────────────────────────────────────────────────

void dumpEngine(const Engine& e, Out& o) {
    o.line("engine tick=%lld time=%.17g ctx=%d phase=%d scene=%d active=%d recording=%d stalled=%d monitoring=%d",
           static_cast<long long>(e.lastTick()), e.lastTimeMs(), static_cast<int>(e.context()), static_cast<int>(e.phase()), e.scene(),
           e.active() ? 1 : 0, e.recording() ? 1 : 0, e.stalled() ? 1 : 0, e.monitoringEnabled() ? 1 : 0);
    const auto& st = e.stats();
    o.line("stats tracked=%d filtered=%d ticks=%llu skipped=%d combat=%d stalls=%d pruned=%d", st.trackedPlayers, st.filteredEntities,
           static_cast<unsigned long long>(st.ticksProcessed), st.skippedTicks, st.combatEvents, st.stalls, st.prunedRecords);
    o.line("match count=%d", e.match().matchCount());
    o.line("session evidence=%d detections=%d revision=%llu", e.session().totalEvidence(), e.session().totalDetections(),
           static_cast<unsigned long long>(e.session().revision()));
    for (const auto& f : e.filtered()) o.line("filtered %d %s kind=%d %s", f.entityId, f.name.c_str(), static_cast<int>(f.kind), f.reason.c_str());
    for (const auto& c : e.recentCombat()) {
        o.line("combat id=%llu tick=%lld a=%d v=%d cand=%d cert=%.9g reach=%.17g raw=%.17g aim=%.17g yawErr=%.17g", static_cast<unsigned long long>(c.id),
               static_cast<long long>(c.tick), c.attacker, c.victim, c.candidates, c.certainty, c.reach, c.reachRaw, c.aimOutside, c.yawError);
    }
    o.line("alerts seq=%llu", static_cast<unsigned long long>(e.alertSeq()));
    for (const auto& a : e.alerts()) {
        o.line("alert %llu t=%.17g %s %s check=%d conf=%.9g %s", static_cast<unsigned long long>(a.seq), a.timeMs, a.playerKey.c_str(), a.name.c_str(),
               a.check, a.confidence, a.summary.c_str());
        for (const auto& r : a.reasons) o.line("  reason %s", r.c_str());
    }
    for (const auto& d : e.debugLog()) o.line("debug t=%.17g tick=%lld check=%d %s %s", d.timeMs, static_cast<long long>(d.tick), d.check, d.player.c_str(), d.text.c_str());
    for (const PlayerRecord& r : e.session().players()) {
        const PlayerState ps = e.playerState(r);
        o.line("player %s|%s|%s%s ent=%d present=%d local=%d mate=%d first=%.17g last=%.17g susp=%.9g peak=%.9g scene=%d match=%d seen=%d part=%d top=%d",
               r.key.c_str(), r.uuid.c_str(), r.name.c_str(), r.displaySuffix.c_str(), r.entityId, r.present, r.local, r.teammate, r.firstSeenMs,
               r.lastSeenMs, r.suspicion, r.peakSuspicion, r.scene, r.matchScene, r.scenesSeen, r.participant, r.topCheck());
        o.line("  state listed=%d local=%d mate=%d exempt=%d game=%d judged=%d", ps.listed, ps.local, ps.teammate, ps.exempt, ps.inThisGame, ps.judged);
        const BehaviorStats& b = r.stats;
        o.line("  stats obs=%d combat=%d landed=%d taken=%d swings=%d reach=%.17g/%d max=%.17g aim=%.17g cps=%.17g/%.17g speed=%.17g/%.17g/%d jumps=%d air=%.17g",
               b.ticksObserved, b.ticksInCombat, b.hitsLanded, b.hitsTaken, b.swings, b.reachSum, b.reachSamples, b.reachMax, b.aimOutsideSum,
               b.cpsPeak, b.cpsRecent, b.speedPeak, b.speedSum, b.speedSamples, b.jumps, b.airTicksSum);
        std::string sr, rr, sh;
        char buf[64];
        for (float v : b.speedRecent) { std::snprintf(buf, sizeof(buf), "%.9g,", v); sr += buf; }
        for (float v : b.reachRecent) { std::snprintf(buf, sizeof(buf), "%.9g,", v); rr += buf; }
        for (const auto& p : r.suspicionHistory) { std::snprintf(buf, sizeof(buf), "%.17g:%.9g,", p.timeMs, p.value); sh += buf; }
        o.line("  speedRecent %s", sr.c_str());
        o.line("  reachRecent %s", rr.c_str());
        o.line("  suspicionHistory %s", sh.c_str());
        for (std::size_t i = 0; i < r.checks.size(); ++i) {
            const CheckRecord& cr = r.checks[i];
            std::string ic, hist;
            for (int n : cr.indicatorCounts) ic += std::to_string(n) + ",";
            for (const auto& p : cr.history) { std::snprintf(buf, sizeof(buf), "%.17g:%.9g,", p.timeMs, p.value); hist += buf; }
            o.line("  check %zu value=%.9g lastDet=%.17g peak=%.9g ev=%d det=%d lastEv=%.17g sampled=%.9g ind=%s", i, cr.model.value(),
                   cr.model.lastDetectionMs(), cr.peak, cr.evidence, cr.detections, cr.lastEvidenceMs, cr.lastSampled, ic.c_str());
            o.line("    history %s", hist.c_str());
        }
        for (const Evidence& ev : r.evidence) {
            o.line("  evidence %llu tick=%lld t=%.17g check=%d ind=%d w=%.9g sev=%.9g +%.9g ->%.9g", static_cast<unsigned long long>(ev.id),
                   static_cast<long long>(ev.tick), ev.timeMs, ev.check, ev.indicator, ev.weight, ev.severity, ev.contribution, ev.confidenceAfter);
            o.line("    summary %s", ev.summary.c_str());
            o.line("    detail %s", ev.detail.c_str());
            o.line("    target %s", ev.target.c_str());
            for (const auto& v : ev.values) {
                o.line("    value %s=%.17g %s limit=%.17g has=%d role=%d", v.name.c_str(), v.value, v.unit.c_str(), v.limit, v.hasLimit, static_cast<int>(v.role));
            }
            const Envelope& en = ev.envelope;
            o.line("    envelope %s %s obs=%.17g limit=%.17g exp=%.17g has=%d %s upper=%d", en.quantity.c_str(), en.unit.c_str(), en.observed, en.limit,
                   en.expected, en.hasExpected, en.expectedLabel.c_str(), en.upper);
            for (const auto& c : en.context) o.line("      context %s", c.c_str());
        }
        for (const Detection& d : r.detections) {
            std::string ids;
            for (auto id : d.evidence) ids += std::to_string(id) + ",";
            o.line("  detection %llu tick=%lld t=%.17g check=%d conf=%.9g n=%d distinct=%d ev=%s %s", static_cast<unsigned long long>(d.id),
                   static_cast<long long>(d.tick), d.timeMs, d.check, d.confidence, d.evidenceCount, d.distinctIndicators, ids.c_str(), d.summary.c_str());
        }
        for (const TimelineEvent& t : r.timeline) {
            o.line("  timeline %d t=%.17g tick=%lld check=%d ref=%llu v=%.9g %s", static_cast<int>(t.kind), t.timeMs, static_cast<long long>(t.tick), t.check,
                   static_cast<unsigned long long>(t.ref), t.value, t.text.c_str());
        }
    }
}

// ── Configurations ──────────────────────────────────────────────────────────────────────────

using Hook = std::function<void(Engine&, std::size_t frame, std::size_t total)>;

struct Variant {
    std::string name;
    std::function<void(Engine&)> before;   // applied to the registry before the replay
    Hook during;                            // applied between frames (may be empty)
};

Setting* find(const std::string& key) { return Settings::instance().find(key); }

void setAll(const std::string& suffix, double v) {
    for (const auto& sp : Settings::instance().all()) {
        const std::string& k = sp->key;
        if (k.rfind("checks.", 0) == 0 && k.size() > suffix.size() && k.compare(k.size() - suffix.size(), suffix.size(), suffix) == 0) sp->set(v);
    }
}

// The registry exactly as registered (a fresh install). Restored by assignment, not
// Setting::reset(), which snaps to the step and so moves float defaults such as 0.8f.
std::vector<double> g_fresh;

void rememberFreshValues() {
    g_fresh.clear();
    for (const auto& sp : Settings::instance().all()) g_fresh.push_back(sp->value);
}

void resetEngineSettings() {
    const auto& all = Settings::instance().all();
    for (std::size_t i = 0; i < all.size() && i < g_fresh.size(); ++i) all[i]->value = g_fresh[i];
    Settings::instance().touch();
}

std::vector<Variant> variants(Engine& probe, bool quick) {
    std::vector<Variant> v;
    v.push_back({"defaults", nullptr, nullptr});
    v.push_back({"preset-relaxed", [](Engine& e) { presetAll(e, 0); }, nullptr});
    v.push_back({"preset-strict", [](Engine& e) { presetAll(e, 2); }, nullptr});
    // Mid-session changes, as the UI makes them (between frames).
    v.push_back({"mid-monitoring-paused", nullptr, [](Engine&, std::size_t i, std::size_t n) {
                     if (i == n / 4) find("general.monitoring")->set(0.0);
                     if (i == n / 2) find("general.monitoring")->set(1.0);
                 }});
    v.push_back({"mid-preset-strict", nullptr, [](Engine& e, std::size_t i, std::size_t n) { if (i == n * 3 / 10) presetAll(e, 2); }});
    v.push_back({"mid-debug-on", nullptr, [](Engine&, std::size_t i, std::size_t n) { if (i == n / 5) setAll(".debug", 1.0); }});
    v.push_back({"mid-checks-toggled", nullptr, [](Engine& e, std::size_t i, std::size_t n) {
                     if (i == n * 2 / 5 || i == n * 7 / 10) {
                         const double on = i == n * 2 / 5 ? 0.0 : 1.0;
                         for (std::size_t k = 0; k < e.checks().size(); k += 2) find("checks." + e.checks()[k]->id() + ".enabled")->set(on);
                     }
                 }});
    v.push_back({"mid-sampling", nullptr, [](Engine&, std::size_t i, std::size_t n) {
                     if (i == n * 3 / 20) {
                         setAll(".sampling.enabled", 1.0);
                         setAll(".sampling.interval", 3.0);
                         setAll(".sampling.radius", 24.0);
                     }
                 }});
    v.push_back({"mid-weights-and-indicators", nullptr, [](Engine&, std::size_t i, std::size_t n) {
                     if (i == n * 7 / 20) setAll(".weight", 2.5);
                     if (i == n * 3 / 5) {
                         for (const auto& sp : Settings::instance().all()) {
                             if (sp->key.find(".ind.") != std::string::npos && sp->key.find(".enabled") != std::string::npos &&
                                 sp->key.find("checks.") == 0 && (sp->key.size() % 2) == 0) {
                                 sp->set(0.0);
                             }
                         }
                     }
                 }});
    v.push_back({"mid-roles", nullptr, [](Engine&, std::size_t i, std::size_t n) {
                     if (i == n / 10) { find("general.analyzeSelf")->set(1.0); find("exempt.teammates")->set(0.0); }
                     if (i == n / 3) { find("general.runIn.lobby")->set(1.0); find("general.runIn.pregame")->set(1.0); }
                     if (i == n * 4 / 5) find("general.runIn.replay")->set(0.0);
                 }});
    // Every engine parameter on its own, up and down.
    std::vector<std::string> keys;
    for (const auto& sp : Settings::instance().all()) keys.push_back(sp->key);
    (void)probe;
    if (quick) keys.resize(std::min<std::size_t>(keys.size(), 12));
    for (const std::string& key : keys) {
        for (int dir : {+1, -1}) {
            const Setting* s = find(key);
            if (!s) continue;
            if ((s->type == SettingType::Bool || s->type == SettingType::Choice) && dir < 0) continue;
            if (s->type != SettingType::Bool && s->type != SettingType::Choice) {
                if (dir > 0 && !(s->def < s->range.max)) continue;
                if (dir < 0 && !(s->def > s->range.min)) continue;
            }
            v.push_back({std::string(dir > 0 ? "up:" : "down:") + key,
                         [key, dir](Engine&) {
                             Setting* p = find(key);
                             if (p->type == SettingType::Bool) { p->set(p->b() ? 0.0 : 1.0); return; }
                             if (p->type == SettingType::Choice) { p->set(static_cast<double>((p->i() + 1) % static_cast<int>(p->choices.size()))); return; }
                             const double target = dir > 0 ? p->def + (p->range.max - p->def) * 0.5 : p->def - (p->def - p->range.min) * 0.5;
                             p->set(target);
                             if (p->value == p->def) p->set(dir > 0 ? p->range.max : p->range.min);
                         },
                         nullptr});
        }
    }
    return v;
}

}  // namespace

// One line per generated scenario that the Rust side reproduces too: counts that show what the
// generated set exercised (records, evidence per indicator, filtered entities, stalls, pruning…).
std::string summaryLine(const Engine& e, const std::string& name) {
    int suffixed = 0, exempt = 0;
    std::string ind;
    for (std::size_t c = 0; c < e.checks().size(); ++c) {
        std::vector<int> sum(e.checks()[c]->indicators().size(), 0);
        for (const PlayerRecord& r : e.session().players()) {
            const auto& counts = r.checks[c].indicatorCounts;
            for (std::size_t i = 0; i < counts.size() && i < sum.size(); ++i) sum[i] += counts[i];
        }
        for (std::size_t i = 0; i < sum.size(); ++i) ind += std::to_string(sum[i]) + (i + 1 < sum.size() ? "," : "");
        ind += c + 1 < e.checks().size() ? ";" : "";
    }
    for (const PlayerRecord& r : e.session().players()) {
        if (!r.displaySuffix.empty()) ++suffixed;
        if (e.playerState(r).exempt) ++exempt;
    }
    Out o;
    o.line("summary %s records=%zu evidence=%d detections=%d alerts=%llu combat=%d stalls=%d filtered=%d pruned=%d ticks=%llu "
           "suffixed=%d exempt=%d scene=%d matches=%d ind=%s",
           name.c_str(), e.session().players().size(), e.session().totalEvidence(), e.session().totalDetections(),
           static_cast<unsigned long long>(e.alertSeq()), e.stats().combatEvents, e.stats().stalls, e.stats().filteredEntities,
           e.stats().prunedRecords, static_cast<unsigned long long>(e.stats().ticksProcessed), suffixed, exempt, e.scene(),
           static_cast<int>(e.match().matchCount()), ind.c_str());
    return o.s;
}

int usage() {
    std::printf("usage: VigilParity <out.txt> [--quick] [--export <dir>] [--full <variant> <scenario> <dump.txt>]\n"
                "       VigilParity --generate <dir> [--count N] [--seed S] [--full generated <scenario> <dump.txt>]\n"
                "       VigilParity --bench <reps>\n");
    return 2;
}

// --generate: N scenarios from tests/generator.h, replayed here; plan + expected result for Rust.
int generate(const std::filesystem::path& dir, int count, std::uint64_t seed, const std::string& fullScenario,
             const std::string& fullPath) {
    Engine probe;
    probe.initialize();
    std::string plan, expected = "# VigilParity generated format 1\n";
    {
        json::Value h = json::Value::object();
        h["kind"] = json::Value("vigil-parity-plan");
        h["format"] = json::Value(1);
        h["mode"] = json::Value("generated");
        h["count"] = json::Value(count);
        h["seed"] = json::Value(static_cast<double>(seed));
        plan += json::write(h, -1) + "\n";
    }
    long long frames = 0;
    for (int i = 0; i < count; ++i) {
        auto sc = vigil::test::generatedScenario(seed, i, probe.schemaPtr());
        frames += static_cast<long long>(sc.frames.size());
        const std::string trace = "gen/" + sc.name + ".vtrace";
        // Both engines replay the frames as the trace holds them: what the Rust side reads is, by
        // construction, what this engine runs on. A frame the trace cannot represent exactly (a
        // non-finite number is written as 0) is a generator bug and stops the run.
        for (const WorldFrame& f : sc.frames) {
            bool finite = std::isfinite(f.timeMs);
            for (const PlayerFrame& p : f.players) {
                finite = finite && std::isfinite(p.pos.x) && std::isfinite(p.pos.y) && std::isfinite(p.pos.z) && std::isfinite(p.yaw) &&
                         std::isfinite(p.pitch) && std::isfinite(p.health) && std::isfinite(p.moveSpeedAttribute);
            }
            if (!finite) {
                std::printf("%s: frame %lld holds a non-finite number (generator bug)\n", sc.name.c_str(), static_cast<long long>(f.tick));
                return 1;
            }
        }
        const std::string text = traceText(sc.name, sc.frames);
        if (!writeText(dir / trace, text)) { std::printf("cannot write %s\n", trace.c_str()); return 1; }
        {
            TraceHeader header;
            std::vector<WorldFrame> parsed;
            std::string error;
            if (!readTrace((dir / trace).string(), header, parsed, error) || parsed.size() != sc.frames.size()) {
                std::printf("%s: the written trace does not read back: %s\n", sc.name.c_str(), error.c_str());
                return 1;
            }
            sc.frames = std::move(parsed);
        }
        json::Value run = json::Value::object();
        run["variant"] = json::Value("generated");
        run["scenario"] = json::Value(sc.name);
        run["trace"] = json::Value(trace);
        run["full"] = json::Value(false);
        Engine e;
        e.initialize();
        if (!sc.marks.empty()) {
            const std::string marks = "gen/" + sc.name + ".marks.json";
            // The engine may rewrite its marks file (binding); it gets a copy, the plan the original.
            writeText(dir / marks, sc.marks);
            writeText(dir / "work" / (sc.name + ".marks.json"), sc.marks);
            run["marks"] = json::Value(marks);
            e.setMarksPath((dir / "work" / (sc.name + ".marks.json")).string());
        }
        json::Value configs = json::Value::array();
        for (const auto& c : sc.configs) {
            json::Value cj = json::Value::object();
            cj["frame"] = json::Value(static_cast<double>(c.first));
            cj["values"] = configDiff(c.second);
            configs.push(std::move(cj));
        }
        run["configs"] = configs;
        plan += json::write(run, -1) + "\n";
        std::size_t next = 0;
        while (next < sc.configs.size() && sc.configs[next].first < 0) e.setConfig(sc.configs[next++].second);
        if (!sc.frames.empty()) e.resetSession(sc.frames.front().timeMs);
        for (std::size_t f = 0; f < sc.frames.size(); ++f) {
            while (next < sc.configs.size() && sc.configs[next].first == static_cast<long long>(f)) e.setConfig(sc.configs[next++].second);
            e.onFrame(sc.frames[f]);
        }
        Out d;
        dumpEngine(e, d);
        Out o;
        o.line("variant generated %s digest=%016llx evidence=%d detections=%d alerts=%llu", sc.name.c_str(),
               static_cast<unsigned long long>(fnv(d.s)), e.session().totalEvidence(), e.session().totalDetections(),
               static_cast<unsigned long long>(e.alertSeq()));
        expected += o.s + summaryLine(e, sc.name);
        if (sc.name == fullScenario) writeText(fullPath, d.s);
        if ((i + 1) % 250 == 0) std::printf("  %d/%d scenarios\n", i + 1, count);
    }
    if (!writeText(dir / "plan.jsonl", plan) || !writeText(dir / "expected.txt", expected)) return 1;
    std::printf("%d generated scenarios (%lld frames, seed %llu) -> %s\n", count, frames, static_cast<unsigned long long>(seed),
                dir.string().c_str());
    return 0;
}

// --bench: engine tick cost (RUST_PLAN §8 performance): each corpus scenario under the defaults,
// `reps` times, timing only Engine::onFrame (construction and frame building excluded).
int bench(int reps) {
    const auto corpus = vigil::test::scenarioCorpus();
    double totalMicros = 0.0;
    std::size_t totalFrames = 0;
    for (const auto& sc : corpus) {
        double micros = 0.0;
        for (int r = 0; r < reps; ++r) {
            Engine e;
            e.initialize();
            e.resetSession(sc.frames.front().timeMs);
            const auto t0 = std::chrono::steady_clock::now();
            for (const auto& f : sc.frames) e.onFrame(f);
            micros += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        }
        totalMicros += micros;
        totalFrames += sc.frames.size() * static_cast<std::size_t>(reps);
        std::printf("bench %-20s frames=%5zu reps=%d us_per_frame=%.3f\n", sc.name.c_str(), sc.frames.size(), reps,
                    micros / static_cast<double>(sc.frames.size() * static_cast<std::size_t>(reps)));
    }
    std::printf("bench %-20s frames=%5zu reps=%d us_per_frame=%.3f\n", "all", totalFrames / static_cast<std::size_t>(reps), reps,
                totalMicros / static_cast<double>(totalFrames));
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--bench") == 0) return bench(std::max(1, std::atoi(argv[2])));
    std::string outPath, exportDir, generateDir, fullVariant, fullScenario, fullPath;
    bool quick = false;
    int count = 1000;
    std::uint64_t seed = 20261127;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--quick") quick = true;
        else if (a == "--export" && i + 1 < argc) exportDir = argv[++i];
        else if (a == "--generate" && i + 1 < argc) generateDir = argv[++i];
        else if (a == "--count" && i + 1 < argc) count = std::atoi(argv[++i]);
        else if (a == "--seed" && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--full" && i + 3 < argc) { fullVariant = argv[++i]; fullScenario = argv[++i]; fullPath = argv[++i]; }
        else if (outPath.empty() && a.rfind("--", 0) != 0) outPath = a;
        else return usage();
    }
    if (!generateDir.empty()) return generate(generateDir, count, seed, fullScenario, fullPath);
    if (outPath.empty()) return usage();
    const bool exporting = !exportDir.empty();
    std::string plan;
    Out o;
    o.line("# VigilParity format 1");
    Engine probe;
    initEngine(probe);
    rememberFreshValues();
    dumpRegistry(o);
    const auto corpus = vigil::test::scenarioCorpus();
    for (const auto& sc : corpus) {
        std::string frames;
        for (const auto& f : sc.frames) frames += std::to_string(f.tick) + ":" + std::to_string(f.players.size()) + ";";
        o.line("corpus %s frames=%zu shape=%016llx", sc.name.c_str(), sc.frames.size(), static_cast<unsigned long long>(fnv(frames)));
    }
    if (exporting) {
        json::Value h = json::Value::object();
        h["kind"] = json::Value("vigil-parity-plan");
        h["format"] = json::Value(1);
        h["mode"] = json::Value("corpus");
        json::Value scs = json::Value::array();
        for (const auto& sc : corpus) {
            const std::string trace = "corpus/" + sc.name + ".vtrace";
            if (!writeText(std::filesystem::path(exportDir) / trace, traceText(sc.name, sc.frames))) {
                std::printf("cannot write %s\n", trace.c_str());
                return 1;
            }
            json::Value s = json::Value::object();
            s["name"] = json::Value(sc.name);
            s["trace"] = json::Value(trace);
            scs.push(std::move(s));
        }
        h["scenarios"] = scs;
        plan += json::write(h, -1) + "\n";
    }
    const auto vs = variants(probe, quick);
    std::string fullDefaults;
    int done = 0;
    for (const Variant& var : vs) {
        for (const auto& sc : corpus) {
            resetEngineSettings();   // every (configuration, scenario) starts from a fresh install
            g_capture = Capture{};
            g_capture.on = exporting;
            Engine e;
            initEngine(e);
            if (var.before) var.before(e);
            syncEngine(e, -1);
            if (!sc.frames.empty()) e.resetSession(sc.frames.front().timeMs);
            for (std::size_t i = 0; i < sc.frames.size(); ++i) {
                if (var.during) var.during(e, i, sc.frames.size());
                syncEngine(e, static_cast<long long>(i));
                e.onFrame(sc.frames[i]);
            }
            Out d;
            dumpEngine(e, d);
            o.line("variant %s %s digest=%016llx evidence=%d detections=%d alerts=%llu", var.name.c_str(), sc.name.c_str(),
                   static_cast<unsigned long long>(fnv(d.s)), e.session().totalEvidence(), e.session().totalDetections(),
                   static_cast<unsigned long long>(e.alertSeq()));
            if (var.name == "defaults") fullDefaults += "== " + sc.name + "\n" + d.s;
            if (var.name == fullVariant && sc.name == fullScenario) writeText(fullPath, d.s);
            if (exporting) {
                json::Value run = json::Value::object();
                run["variant"] = json::Value(var.name);
                run["scenario"] = json::Value(sc.name);
                run["trace"] = json::Value("corpus/" + sc.name + ".vtrace");
                run["full"] = json::Value(var.name == "defaults");
                run["configs"] = g_capture.configs;
                plan += json::write(run, -1) + "\n";
            }
        }
        ++done;
        if (done % 50 == 0) std::printf("  %d/%zu configurations\n", done, vs.size());
    }
    g_capture = Capture{};
    resetEngineSettings();
    o.s += "# full state, defaults\n" + fullDefaults;
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    out << o.s;
    if (exporting && !writeText(std::filesystem::path(exportDir) / "plan.jsonl", plan)) return 1;
    std::printf("%zu configurations x %zu scenarios -> %s (%s)\n", vs.size(), corpus.size(), outPath.c_str(),
                VIGIL_PARITY_ENGINE_CONFIG ? "EngineConfig engine" : "registry engine");
    return out ? 0 : 1;
}
