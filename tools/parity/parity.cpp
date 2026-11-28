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
//       result dir/expected.txt (one digest line and one summary line per scenario; P3B adds one
//       "sinks" line: journal, history flushes, log channel, marks file, profiles, history fields).
//   VigilParity --marks <dir> [--count N] [--seed S]
//       P3B: N random action scripts over the full marks state machine (see marksScripts); the
//       state after every action, dir/plan.jsonl + dir/expected.txt (one digest line per script).
//   VigilParity --full <variant> <scenario> <dump.txt>    (with either mode's other arguments)
//       also writes the full state of that one run (generated runs: also <dump.txt>.sinks.txt;
//       marks scripts: --full marks <script> <dump.txt>), to locate a difference.
//   VigilParity --bench <reps>
//       engine tick cost on the corpus under the defaults (twin: vigil-parity --bench).

#include "../../src/core/json.h"
#include "../../src/core/log.h"
#include "../../src/core/settings.h"
#include "../../src/detection/engine.h"
#include "../../src/detection/snapshot.h"
#include "../../src/detection/trace.h"
#include "../../tests/corpus.h"
#include "../../tests/generator.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <random>
#include <sstream>

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

// ── Persistence sinks of generated runs (P3B_PLAN.md §3.6) ──────────────────────────────────
//
// Every generated scenario also runs with a cross-session history file, a journal directory, a
// log directory and its marks file, and one "sinks" line records what they hold: the journal (all
// files in name order), the history file after every flush, the "detections" log lines without
// their timestamps, the final marks file, the profiles captured from the records, and the history
// fields of every record. Wall-clock values (a flush's lastSeen, a binding's updated time) are
// replaced by NOW when they lie inside the run's wall-clock window; the Rust tool injects a clock and
// masks its own window the same way, so everything else compares byte for byte.

std::string readText(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The number after "<key>": becomes NOW when lo <= value <= hi.
std::string maskTimes(const std::string& text, const std::vector<std::string>& keys, double lo, double hi) {
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        bool matched = false;
        for (const std::string& k : keys) {
            const std::string pat = "\"" + k + "\": ";
            if (text.compare(i, pat.size(), pat) != 0) continue;
            std::size_t j = i + pat.size(), e = j;
            while (e < text.size() && (std::isdigit(static_cast<unsigned char>(text[e])) || text[e] == '-' || text[e] == '+' || text[e] == '.' ||
                                       text[e] == 'e' || text[e] == 'E')) {
                ++e;
            }
            const double v = std::strtod(text.substr(j, e - j).c_str(), nullptr);
            out += pat;
            if (e > j && v >= lo && v <= hi) out += "NOW";
            else out.append(text, j, e - j);
            i = e;
            matched = true;
            break;
        }
        if (!matched) out += text[i++];
    }
    return out;
}

// "YYYY-MM-DD HH:MM:SS  message" -> "message"; a line of any other shape is kept, marked "!".
std::string stripLogStamps(const std::string& text, int& lines) {
    std::string out;
    lines = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        static const char* shape = "dddd-dd-dd dd:dd:dd  ";
        bool stamped = line.size() >= 21;
        for (int k = 0; stamped && k < 21; ++k) {
            stamped = shape[k] == 'd' ? std::isdigit(static_cast<unsigned char>(line[k])) != 0 : line[k] == shape[k];
        }
        out += stamped ? line.substr(21) : "!" + line;
        out += '\n';
        ++lines;
        pos = nl + 1;
    }
    return out;
}

// A history file for a generated scenario, drawn from its own generator (the scenario's frames
// and configurations are unchanged): none, unreadable, the wrong shape, or entries — with hostile
// values — for some of the scenario's players and for strangers.
std::string historyInput(std::mt19937_64& r, const std::vector<std::string>& keys) {
    const int kind = static_cast<int>(r() % 20);
    if (kind < 6) return {};
    if (kind < 8) return "{ not json";
    if (kind == 8) return "[1,2]";
    if (kind == 9) return "{\"format\":1,\"players\":[]}";
    json::Value root = json::Value::object();
    root["format"] = json::Value(1);
    json::Value& players = root["players"];
    players = json::Value::object();
    const double counts[] = {0, 1, 3, 17, 2147483647.0, -4, 1e300, 2.75};
    const double peaks[] = {0, 12.5, 99.25, 100, 1e300, -3, 1e-45, 0.1};
    const double seen[] = {1700000000000.0, 1650000000123.5, 0, -1, 1e300, 1764201600000.0};
    const int n = 1 + static_cast<int>(r() % 6);
    for (int k = 0; k < n + 2; ++k) {
        std::string key;
        if (k < n && !keys.empty()) key = keys[r() % keys.size()];
        else key = "00000000-0000-4000-8000-9999999999" + std::to_string(10 + r() % 90);
        json::Value e = json::Value::object();
        if (r() % 5) e["name"] = json::Value("Prior" + std::to_string(k));
        else e["name"] = json::Value(7);
        if (r() % 6) e["sessions"] = json::Value(counts[r() % 8]);
        else e["sessions"] = json::Value("x");
        if (r() % 6) e["detections"] = json::Value(counts[r() % 8]);
        if (r() % 6) e["peak"] = json::Value(peaks[r() % 8]);
        if (r() % 6) e["lastSeen"] = json::Value(seen[r() % 6]);
        players[key] = e;
    }
    return json::write(root, r() % 2 ? 1 : -1);
}

// A copy of `configs` with data.journal / data.history switched at a random frame in some
// scenarios (they only decide what the sinks write, never what the engine concludes).
std::vector<std::pair<long long, EngineConfig>> withSinkToggles(std::mt19937_64& r, std::vector<std::pair<long long, EngineConfig>> configs,
                                                                const std::shared_ptr<const ConfigSchema>& schema, std::size_t frames) {
    auto toggle = [&](const char* key, long long at) {
        EngineConfig c(schema);
        std::size_t pos = 0;
        for (std::size_t k = 0; k < configs.size(); ++k) {
            if (configs[k].first <= at) { c = configs[k].second; pos = k + 1; }
        }
        const ParamId id = schema->find(key);
        c.set(id, c.b(id) ? 0.0 : 1.0);
        configs.insert(configs.begin() + static_cast<std::ptrdiff_t>(pos), {at, c});
    };
    if (frames < 2) return configs;
    if (r() % 4 == 0) toggle("data.journal", static_cast<long long>(r() % frames));
    if (r() % 4 == 0) toggle("data.history", static_cast<long long>(r() % frames));
    if (r() % 10 == 0) toggle(r() % 2 ? "data.journal" : "data.history", -1);
    return configs;
}

// The sinks of one generated run, after the run: the "sinks" line and the full text behind it.
struct SinkCapture {
    std::string history;   // every flush, in order
    int flushes = 0;
};

std::string sinksText(const Engine& e, const std::filesystem::path& journalDir, const std::filesystem::path& logDir,
                      const std::filesystem::path& marksPath, const SinkCapture& cap, double lo, double hi, std::string& line,
                      const std::string& name) {
    std::string journal;
    int journalLines = 0;
    {
        std::vector<std::filesystem::path> files;
        std::error_code ec;
        if (std::filesystem::exists(journalDir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(journalDir, ec)) files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            const std::string t = readText(f);
            journal += "== journal file\n" + t;
            journalLines += static_cast<int>(std::count(t.begin(), t.end(), '\n'));
        }
    }
    int logLines = 0;
    const std::string log = stripLogStamps(readText(logDir / "detections.log.old") + readText(logDir / "detections.log"), logLines);
    const std::string marks = maskTimes(readText(marksPath), {"added", "confirmedAt", "exemptAt", "updated"}, lo, hi);
    std::string profiles;
    int captured = 0, roundTripFailures = 0;
    const CheckCatalog cat = CheckCatalog::of(e);
    const auto& players = e.session().players();
    for (std::size_t i = 0; i < players.size(); ++i) {
        const PlayerRecord& r = players[i];
        if (i >= 12 && r.detections.empty()) continue;
        ProfileSnapshot s = captureSnapshot(e, r);
        s.vigilVersion = "parity";
        s.savedAtEpochMs = 1764201600000.0;
        const std::string text = snapshotToJson(s, cat);
        ProfileSnapshot back;
        std::string err;
        if (!snapshotFromJson(text, cat, back, err) || snapshotToJson(back, cat) != text) ++roundTripFailures;
        profiles += text + "\n";
        ++captured;
    }
    std::string prior;
    int withHistory = 0;
    char buf[256];
    for (const PlayerRecord& r : players) {
        std::snprintf(buf, sizeof buf, "%d %d %.9g %.17g ", r.priorSessions, r.priorDetections, r.priorPeak, r.lastSeenEpochMs);
        prior += buf + r.key + "\n";
        if (r.priorSessions != 0 || r.priorDetections != 0 || r.priorPeak != 0.f || r.lastSeenEpochMs != 0.0) ++withHistory;
    }
    Out o;
    o.line("sinks %s journal=%016llx:%d history=%016llx:%d log=%016llx:%d marks=%016llx profiles=%016llx:%d:%d prior=%016llx:%d", name.c_str(),
           static_cast<unsigned long long>(fnv(journal)), journalLines, static_cast<unsigned long long>(fnv(cap.history)), cap.flushes,
           static_cast<unsigned long long>(fnv(log)), logLines, static_cast<unsigned long long>(fnv(marks)),
           static_cast<unsigned long long>(fnv(profiles)), captured, roundTripFailures, static_cast<unsigned long long>(fnv(prior)), withHistory);
    line = o.s;
    return "# journal\n" + journal + "# history\n" + cap.history + "# log\n" + log + "# marks\n" + marks + "\n# profiles\n" + profiles +
           "# prior\n" + prior;
}

// ── Marks scripts (P3B_PLAN.md §3.6) ────────────────────────────────────────────────────────
//
// Random action scripts over every operation of the user's marks — watch, confirm, unconfirm,
// exempt, notes, reasons, remove, remember name, add by name, bind, reload — starting from no file,
// format 1 and 2 files (with equal times and more than 32 marks, so std::sort's order of ties
// matters), a newer format, an unreadable file and the wrong shape. After every action the whole
// state is dumped: every mark in all() order, counts, revision, read-only state, searches,
// find-by-name and the file itself. Mark times come from the wall clock: this tool waits for a new
// millisecond before each action, so every action's time is unique and is written as "@<action>";
// the Rust tool injects one time per action and writes the same.

struct MarksAction {
    std::string op, key, name, note, uuid, knownKey;
    std::vector<std::string> reasons;
    bool on = false;
    int kind = 0;
    double confidence = -1.0;
};

std::string marksInitial(std::mt19937_64& r, const std::vector<std::string>& keys, const std::vector<std::string>& names) {
    const int kind = static_cast<int>(r() % 8);
    if (kind == 0) return {};
    if (kind == 1) return "{ not json";
    if (kind == 2) return "[1]";
    json::Value root = json::Value::object();
    root["format"] = json::Value(kind == 3 ? 3 : kind == 4 ? 1 : 2);
    json::Value& players = root["players"];
    players = json::Value::object();
    const int n = kind == 5 || kind == 6 ? 33 + static_cast<int>(r() % 90) : static_cast<int>(r() % 12);
    const double times[] = {1700000000000.0, 1700000000000.0, 1650000000000.0, 0.0, 1764201600000.0};
    const char* reasons[] = {"KillAura", "Reach", "Speed", "BHop"};
    for (int k = 0; k < n; ++k) {
        const std::string key = k < static_cast<int>(keys.size()) && r() % 2 ? keys[r() % keys.size()] : "00000000-0000-4000-8000-5" + std::to_string(10000000000ll + k);
        json::Value m = json::Value::object();
        m["name"] = json::Value(names[r() % names.size()]);
        if (r() % 3 == 0) m["uuid"] = json::Value(key);
        m["watch"] = json::Value(r() % 2 == 0);
        m["confirmed"] = json::Value(r() % 3 == 0);
        m["exempt"] = json::Value(r() % 4 == 0);
        if (r() % 3) m["added"] = json::Value(times[r() % 5]);
        if (r() % 2) m["confirmedAt"] = json::Value(times[r() % 5]);
        if (r() % 4 == 0) m["exemptAt"] = json::Value(times[r() % 5]);
        m["updated"] = json::Value(times[r() % 3]);
        if (r() % 3 == 0) m["confidenceAtConfirm"] = json::Value(r() % 2 ? 82.5 : 0.1);
        if (r() % 4 == 0) m["note"] = json::Value(r() % 2 ? "duo partner" : "Reach in every fight");
        if (r() % 3 == 0) m["origin"] = json::Value(r() % 2 ? "name" : "profile");
        json::Value rs = json::Value::array();
        for (unsigned j = 0, c = static_cast<unsigned>(r() % 3); j < c; ++j) rs.push(json::Value(reasons[r() % 4]));
        m["reasons"] = rs;
        players[key] = m;
    }
    return json::write(root, r() % 2 ? 1 : -1);
}

std::vector<MarksAction> marksScript(std::mt19937_64& r, const std::vector<std::string>& keys, const std::vector<std::string>& names) {
    const char* reasons[] = {"KillAura", "Reach", "Speed", "BHop"};
    const std::string longNote(600, 'n');
    const char* notes[] = {"", "my duo partner", "Aim snaps every fight", "KES note"};
    std::vector<MarksAction> out;
    const int n = 15 + static_cast<int>(r() % 50);
    for (int i = 0; i < n; ++i) {
        MarksAction a;
        static const char* ops[] = {"watch", "confirm", "unconfirm", "exempt", "note", "reasons", "remove", "remember", "add", "bind", "reload"};
        a.op = ops[r() % 11];
        a.key = keys[r() % keys.size()];
        a.name = names[r() % names.size()];
        a.on = r() % 3 != 0;
        a.kind = static_cast<int>(r() % 3);
        a.confidence = r() % 2 ? -1.0 : static_cast<double>(static_cast<float>((r() % 1000) / 10.0));
        for (unsigned j = 0, c = static_cast<unsigned>(r() % 4); j < c; ++j) a.reasons.push_back(reasons[r() % 4]);
        a.note = r() % 7 == 0 ? longNote : notes[r() % 4];
        a.uuid = r() % 4 == 0 ? std::string() : keys[r() % keys.size()];
        a.knownKey = r() % 4 == 0 ? keys[r() % keys.size()] : std::string();
        out.push_back(a);
    }
    return out;
}

// Mark times as "@<action>" when they lie in an action's window, else literally.
struct TimeMap {
    double start = 0.0;
    std::vector<std::pair<double, double>> windows;
    std::string of(double t) const {
        char buf[64];
        if (t >= start) {
            for (std::size_t k = 0; k < windows.size(); ++k) {
                if (t >= windows[k].first && t <= windows[k].second) { std::snprintf(buf, sizeof buf, "@%zu", k); return buf; }
            }
            return "@?";
        }
        std::snprintf(buf, sizeof buf, "%.17g", t);
        return buf;
    }
    std::string file(const std::string& text) const {
        // The numbers after the time keys, mapped like of().
        std::string out;
        std::size_t i = 0;
        static const char* keys[] = {"\"added\":", "\"confirmedAt\":", "\"exemptAt\":", "\"updated\":"};
        while (i < text.size()) {
            bool matched = false;
            for (const char* k : keys) {
                const std::size_t kl = std::strlen(k);
                if (text.compare(i, kl, k) != 0) continue;
                std::size_t j = i + kl;
                while (j < text.size() && text[j] == ' ') ++j;
                std::size_t e = j;
                while (e < text.size() && (std::isdigit(static_cast<unsigned char>(text[e])) || text[e] == '-' || text[e] == '+' || text[e] == '.' ||
                                           text[e] == 'e' || text[e] == 'E')) {
                    ++e;
                }
                out.append(text, i, j - i);
                out += of(std::strtod(text.substr(j, e - j).c_str(), nullptr));
                i = e;
                matched = true;
                break;
            }
            if (!matched) out += text[i++];
        }
        return out;
    }
};

std::string marksState(const Marks& m, const TimeMap& tm, const std::string& path, const std::vector<std::string>& names) {
    Out o;
    for (const Mark& k : m.all()) {
        std::string rs;
        for (const auto& x : k.reasons) rs += x + ";";
        o.line("m %s|%s|%s|%d%d%d|%s|%s|%s|%s|%.9g|%s|%s|%s", k.key.c_str(), k.name.c_str(), k.uuid.c_str(), k.watch, k.confirmed, k.exempt,
               tm.of(k.addedEpochMs).c_str(), tm.of(k.confirmedAtEpochMs).c_str(), tm.of(k.exemptAtEpochMs).c_str(), tm.of(k.updatedEpochMs).c_str(),
               k.confidenceAtConfirm, k.origin.c_str(), k.note.c_str(), rs.c_str());
    }
    o.line("counts c=%d w=%d e=%d ro=%d rev=%llu note=%s", m.confirmedCount(), m.watchedCount(), m.exemptCount(), m.readOnly() ? 1 : 0,
           static_cast<unsigned long long>(m.revision()), m.loadNote().c_str());
    for (const char* q : {"", "kes", "e", "duo", "reach"}) {
        for (int kind = -1; kind <= 2; ++kind) {
            std::string keys;
            for (const Mark& k : m.search(q, kind)) keys += k.key + ",";
            o.line("search %s %d %s", q, kind, keys.c_str());
        }
    }
    for (const std::string& n : names) {
        const Mark* f = m.findByName(n);
        o.line("byname %s %s", n.c_str(), f ? f->key.c_str() : "-");
    }
    const std::string file = tm.file(readText(path));
    o.line("file %zu %016llx", file.size(), static_cast<unsigned long long>(fnv(file)));
    return o.s;
}

int marksScripts(const std::filesystem::path& dir, int count, std::uint64_t seed, const std::string& fullScript, const std::string& fullPath) {
    std::string plan, expected = "# VigilParity marks format 1\n";
    {
        json::Value h = json::Value::object();
        h["kind"] = json::Value("vigil-parity-marks");
        h["format"] = json::Value(1);
        h["count"] = json::Value(count);
        h["seed"] = json::Value(static_cast<double>(seed));
        plan += json::write(h, -1) + "\n";
    }
    const std::vector<std::string> keys = {"00000000-0000-4000-8000-000000000001", "069a79f4-44e9-4726-a5be-fca90e38aaf5", "0b4f2ba3-6c5d-1e8f-9a0b-1c2d3e4f5a6b",
                                           "name:Kestrel", "user:kestrel", "user:notch", "", "k1", "00000000-0000-4000-8000-000000000002", "user:vale"};
    const std::vector<std::string> names = {"Kestrel", "kestrel", "KESTREL", "Vale", "Notch", "Quill_9", "not a name!", "", "seventeen_chars_x", "Kessler"};
    long long actions = 0;
    for (int s = 0; s < count; ++s) {
        std::mt19937_64 r(seed * 0xD6E8FEB86659FD93ull + static_cast<std::uint64_t>(s) * 0x9E3779B97F4A7C15ull + 3);
        char nameBuf[32];
        std::snprintf(nameBuf, sizeof nameBuf, "marks-%04d", s);
        const std::string name = nameBuf;
        const std::string initial = marksInitial(r, keys, names);
        const std::vector<MarksAction> script = marksScript(r, keys, names);
        json::Value run = json::Value::object();
        run["name"] = json::Value(name);
        if (!initial.empty()) {
            writeText(dir / "marks" / (name + ".initial.json"), initial);
            run["initial"] = json::Value("marks/" + name + ".initial.json");
        }
        json::Value acts = json::Value::array();
        for (const MarksAction& a : script) {
            json::Value j = json::Value::object();
            j["op"] = json::Value(a.op);
            j["key"] = json::Value(a.key);
            j["name"] = json::Value(a.name);
            j["on"] = json::Value(a.on);
            j["kind"] = json::Value(a.kind);
            j["confidence"] = json::Value(a.confidence);
            json::Value rs = json::Value::array();
            for (const auto& x : a.reasons) rs.push(json::Value(x));
            j["reasons"] = rs;
            j["note"] = json::Value(a.note);
            j["uuid"] = json::Value(a.uuid);
            j["knownKey"] = json::Value(a.knownKey);
            acts.push(j);
        }
        run["actions"] = acts;
        plan += json::write(run, -1) + "\n";

        const std::filesystem::path path = dir / "marks-work" / (name + ".json");
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::filesystem::remove(path, ec);
        if (!initial.empty()) writeText(path, initial);
        TimeMap tm;
        double last = epochNowMs();
        tm.start = last;
        Marks m;
        m.setPath(path.string());
        std::string dump = "== initial\n" + marksState(m, tm, path.string(), names);
        for (std::size_t i = 0; i < script.size(); ++i) {
            const MarksAction& a = script[i];
            while (epochNowMs() <= last) {}   // a new millisecond: this action's time is its own
            const double t0 = epochNowMs();
            std::string ret = "-";
            if (a.op == "watch") m.setWatch(a.key, a.name, a.on);
            else if (a.op == "confirm") m.confirm(a.key, a.name, a.reasons, static_cast<float>(a.confidence));
            else if (a.op == "unconfirm") m.unconfirm(a.key);
            else if (a.op == "exempt") m.setExempt(a.key, a.name, a.on);
            else if (a.op == "note") m.setNote(a.key, a.note);
            else if (a.op == "reasons") m.setReasons(a.key, a.reasons);
            else if (a.op == "remove") m.remove(a.key);
            else if (a.op == "remember") m.rememberName(a.key, a.name);
            else if (a.op == "add") ret = m.addByName(a.name, static_cast<MarkKind>(a.kind), a.knownKey);
            else if (a.op == "bind") ret = m.bindName(a.key, a.name, a.uuid) ? "bound" : "no";
            else if (a.op == "reload") m.setPath(path.string());
            last = epochNowMs();
            tm.windows.push_back({t0, last});
            dump += "== " + std::to_string(i) + " " + a.op + " -> " + ret + "\n" + marksState(m, tm, path.string(), names);
        }
        actions += static_cast<long long>(script.size());
        Out o;
        o.line("marks %s digest=%016llx actions=%zu marks=%zu ro=%d", name.c_str(), static_cast<unsigned long long>(fnv(dump)), script.size(),
               m.all().size(), m.readOnly() ? 1 : 0);
        expected += o.s;
        if (name == fullScript) writeText(fullPath, dump);
    }
    if (!writeText(dir / "plan.jsonl", plan) || !writeText(dir / "expected.txt", expected)) return 1;
    std::printf("%d marks scripts (%lld actions, seed %llu) -> %s\n", count, actions, static_cast<unsigned long long>(seed), dir.string().c_str());
    return 0;
}

int usage() {
    std::printf("usage: VigilParity <out.txt> [--quick] [--export <dir>] [--full <variant> <scenario> <dump.txt>]\n"
                "       VigilParity --generate <dir> [--count N] [--seed S] [--full generated <scenario> <dump.txt>]\n"
                "       VigilParity --marks <dir> [--count N] [--seed S] [--full marks <script> <dump.txt>]\n"
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
        // P3B sinks: a history file and journal / history toggles from a generator of their own.
        std::mt19937_64 srng(seed * 0xA24BAED4963EE407ull + static_cast<std::uint64_t>(i) * 0x9FB21C651E98DF25ull + 7);
        std::vector<std::string> keys;
        for (const WorldFrame& f : sc.frames) {
            for (const PlayerFrame& p : f.players) {
                const std::string k = p.uuid.empty() ? "name:" + p.name : p.uuid;
                if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
            }
        }
        const std::string historyIn = historyInput(srng, keys);
        sc.configs = withSinkToggles(srng, std::move(sc.configs), probe.schemaPtr(), sc.frames.size());

        json::Value run = json::Value::object();
        run["variant"] = json::Value("generated");
        run["scenario"] = json::Value(sc.name);
        run["trace"] = json::Value(trace);
        run["full"] = json::Value(false);
        const double wallStart = epochNowMs();
        Engine e;
        e.initialize();
        const std::filesystem::path work = dir / "work";
        const std::filesystem::path marksWork = work / (sc.name + ".marks.json");
        if (!sc.marks.empty()) {
            const std::string marks = "gen/" + sc.name + ".marks.json";
            // The engine may rewrite its marks file (binding); it gets a copy, the plan the original.
            writeText(dir / marks, sc.marks);
            writeText(marksWork, sc.marks);
            run["marks"] = json::Value(marks);
            e.setMarksPath(marksWork.string());
        }
        const std::filesystem::path historyWork = work / (sc.name + ".history.json");
        const std::filesystem::path journalDir = work / (sc.name + "-journal");
        const std::filesystem::path logDir = work / (sc.name + "-log");
        {
            std::error_code ec;
            if (sc.marks.empty()) std::filesystem::remove(marksWork, ec);   // no stale copy from an earlier run
            std::filesystem::remove(historyWork, ec);
            std::filesystem::remove_all(journalDir, ec);
            std::filesystem::remove_all(logDir, ec);
        }
        if (!historyIn.empty()) {
            const std::string history = "gen/" + sc.name + ".history.json";
            writeText(dir / history, historyIn);
            writeText(historyWork, historyIn);
            run["history"] = json::Value(history);
        }
        e.setHistoryPath(historyWork.string());
        e.setJournalDirectory(journalDir.string());
        log::init(logDir.string());
        std::vector<std::string> flushes;
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
        flushes.push_back(readText(historyWork));   // resetSession flushed (when data.history is on)
        double lastFlush = 0.0;                      // Engine::lastHistoryFlushMs_, mirrored
        for (std::size_t f = 0; f < sc.frames.size(); ++f) {
            while (next < sc.configs.size() && sc.configs[next].first == static_cast<long long>(f)) e.setConfig(sc.configs[next++].second);
            e.onFrame(sc.frames[f]);
            if (e.recording() && sc.frames[f].timeMs - lastFlush > 60000.0) {
                lastFlush = sc.frames[f].timeMs;
                flushes.push_back(readText(historyWork));
            }
        }
        e.flushHistory();   // what the host does when it stops
        flushes.push_back(readText(historyWork));
        const double wallEnd = epochNowMs();
        Out d;
        dumpEngine(e, d);
        Out o;
        o.line("variant generated %s digest=%016llx evidence=%d detections=%d alerts=%llu", sc.name.c_str(),
               static_cast<unsigned long long>(fnv(d.s)), e.session().totalEvidence(), e.session().totalDetections(),
               static_cast<unsigned long long>(e.alertSeq()));
        expected += o.s + summaryLine(e, sc.name);
        SinkCapture cap;
        for (const std::string& h : flushes) {
            cap.history += "== flush\n" + maskTimes(h, {"lastSeen"}, wallStart, wallEnd) + "\n";
            ++cap.flushes;
        }
        std::string sinks;
        const std::string sinksFull = sinksText(e, journalDir, logDir, marksWork, cap, wallStart, wallEnd, sinks, sc.name);
        expected += sinks;
        log::init("");
        {
            std::error_code ec;
            std::filesystem::remove_all(journalDir, ec);
            std::filesystem::remove_all(logDir, ec);
            std::filesystem::remove(historyWork, ec);
        }
        if (sc.name == fullScenario) {
            writeText(fullPath, d.s);
            writeText(fullPath + ".sinks.txt", sinksFull);
        }
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
    std::string outPath, exportDir, generateDir, marksDir, fullVariant, fullScenario, fullPath;
    bool quick = false;
    int count = 1000;
    std::uint64_t seed = 20261127;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--quick") quick = true;
        else if (a == "--export" && i + 1 < argc) exportDir = argv[++i];
        else if (a == "--generate" && i + 1 < argc) generateDir = argv[++i];
        else if (a == "--marks" && i + 1 < argc) marksDir = argv[++i];
        else if (a == "--count" && i + 1 < argc) count = std::atoi(argv[++i]);
        else if (a == "--seed" && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--full" && i + 3 < argc) { fullVariant = argv[++i]; fullScenario = argv[++i]; fullPath = argv[++i]; }
        else if (outPath.empty() && a.rfind("--", 0) != 0) outPath = a;
        else return usage();
    }
    if (!marksDir.empty()) return marksScripts(marksDir, count, seed, fullScenario, fullPath);
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
