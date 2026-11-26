// Vigil validation tests — properties that must hold for every release (docs/VALIDATION.md):
//
//   confidence    invariants of the confidence model (bounds, decay, gate, carry-over)
//   lifecycle     queue → match → post-game → new world, and what each phase may do
//   player state  every kind of entity and player: listed? judged? alerted?
//   envelopes     every observation says what was expected and what happened
//   determinism   the same frames produce the same outcome, in one engine or two
//   traces        frames survive the trace format exactly; the golden trace replays to its
//                 committed outcome (tests/traces)

#include "../src/detection/replay.h"
#include "../src/detection/trace.h"
#include "scenarios.h"
#include "test_framework.h"

#include <fstream>
#include <sstream>

using namespace vigil;
using namespace vigil::test;

#ifndef VIGIL_TRACE_DIR
#define VIGIL_TRACE_DIR "tests/traces"
#endif

namespace {

ConfidenceParams params() {
    ConfidenceParams p;
    p.alertThreshold = 70.f;
    p.halfLifeSec = 30.f;
    p.minEvidence = 3;
    p.minIndicators = 1;
    p.windowSec = 60.f;
    p.cooldownSec = 20.f;
    p.gain = 0.25f;
    return p;
}

// One low-hopping step at 0.55 b/tick (Speed and BHop evidence).
void lowHop(Sim& s) {
    const double rad = s.moveYaw * 3.14159265358979323846 / 180.0;
    const Vec3 fwd{-std::sin(rad), 0.0, std::cos(rad)};
    if (s.f.onGround) {
        s.motion.y = 0.25;
        s.f.pos = s.f.pos + Vec3{0, 0.01, 0};
        s.f.onGround = false;
    }
    s.f.pos = s.f.pos + fwd * 0.55;
    s.f.pos.y += s.motion.y;
    if (s.f.pos.y <= s.groundY) {
        s.f.pos.y = s.groundY;
        s.motion.y = 0.0;
        s.f.onGround = true;
    } else {
        s.motion.y = (s.motion.y - 0.08) * 0.98;
    }
    s.f.sprinting = true;
    s.tickAnimations();
}

std::string readAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

// ── Confidence ──────────────────────────────────────────────────────────────────────────────

static void testConfidenceInvariants() {
    const ConfidenceParams p = params();
    ConfidenceModel m;
    float last = 0.f;
    bool monotone = true, bounded = true;
    for (int i = 0; i < 200; ++i) {
        m.add(i * 10.0, 4.f, 1.f, i % 3, p);   // maximal evidence, 10 ms apart (no decay)
        monotone = monotone && m.value() >= last;
        bounded = bounded && m.value() >= 0.f && m.value() <= 100.f;
        last = m.value();
    }
    EXPECT(monotone, "evidence never lowers confidence");
    EXPECT(bounded && m.value() <= 100.f, "confidence stays within 0..100 (%.2f)", m.value());

    ConfidenceModel d;
    d.add(0.0, 2.f, 1.f, 0, p);
    const float v0 = d.value();
    d.decayTo(30000.0, p.halfLifeSec);
    EXPECT(std::fabs(d.value() - v0 * 0.5f) < 0.05f, "one half-life halves confidence (%.2f → %.2f)", v0, d.value());
    d.decayTo(30000.0 + 30000.0 * 20, p.halfLifeSec);
    EXPECT(d.value() == 0.f, "long quiet periods return to exactly 0");

    // The gate: a single huge observation can never detect.
    ConfidenceModel g;
    g.add(0.0, 4.f, 1.f, 0, p);
    g.add(10.0, 4.f, 1.f, 0, p);
    EXPECT(!g.canDetect(20.0, p) || g.value() < p.alertThreshold, "two observations are below the evidence gate");
    g.add(20.0, 4.f, 1.f, 0, p);
    EXPECT(g.canDetect(30.0, p), "the third observation completes the gate (%.1f%%)", g.value());
    // Carried confidence (a new game) empties the gate: no detection without new evidence.
    g.resume(40.0, 0.5f);
    EXPECT(!g.canDetect(50.0, p), "carried confidence cannot complete a detection");
    // Cooldown.
    ConfidenceModel c;
    for (int i = 0; i < 6; ++i) c.add(i * 10.0, 4.f, 1.f, 0, p);
    c.markDetected(60.0);
    c.add(70.0, 4.f, 1.f, 0, p);
    EXPECT(!c.canDetect(80.0, p), "cooldown between detections");
    EXPECT(c.canDetect(60.0 + p.cooldownSec * 1000.0 + 1.0, p), "after the cooldown");
}

// ── Lifecycle ───────────────────────────────────────────────────────────────────────────────

static void testMatchLifecycle() {
    World w;
    Sim local(1, "Observer"), cheat(2, "Hopper");
    setupLocal(local);
    w.add(local);
    w.add(cheat);
    // Queue: listed, not judged.
    w.context = GameContext::Pregame;
    for (int i = 0; i < 200; ++i) { lowHop(cheat); w.frame(); }
    EXPECT(w.engine.phase() == GamePhase::Queue, "queue phase");
    EXPECT(w.rec(cheat) && w.rec(cheat)->evidence.empty(), "queue players are listed, never judged");
    // Match: judged; the queue and the match are one game.
    w.context = GameContext::InGame;
    const int queueScene = w.engine.scene();
    for (int i = 0; i < 400; ++i) { lowHop(cheat); w.frame(); }
    EXPECT(w.engine.phase() == GamePhase::Match && w.engine.match().matchCount() == 1, "match started once");
    EXPECT(w.engine.scene() != queueScene, "a match is a new scene");
    EXPECT(w.engine.inThisGame(*w.rec(cheat)), "queue players are in this game");
    EXPECT(!w.rec(cheat)->evidence.empty(), "match players are judged");
    // Post-game: nothing judged.
    w.chat.push_back("                         1st Killer - Hopper - 7");
    w.frame();
    const std::size_t before = w.rec(cheat)->evidence.size();
    for (int i = 0; i < 300; ++i) { lowHop(cheat); w.frame(); }
    EXPECT(w.engine.phase() == GamePhase::PostGame, "post-game");
    EXPECT(w.rec(cheat)->evidence.size() == before, "post-game is never judged");
    // New world: a new scene, the player is from an earlier game until seen again, confidence halved.
    const float held = w.rec(cheat)->suspicion;
    std::vector<float> heldByCheck;
    for (const auto& c : w.rec(cheat)->checks) heldByCheck.push_back(c.model.value());
    w.worldChanged = true;
    w.hidden.push_back(&cheat);
    w.context = GameContext::Lobby;
    for (int i = 0; i < 60; ++i) w.frame();
    EXPECT(!w.engine.inThisGame(*w.rec(cheat)), "after the world changes the player is from an earlier game");
    EXPECT(std::fabs(w.rec(cheat)->suspicion - held) < 0.5f, "confidence holds while away (%.1f vs %.1f)", w.rec(cheat)->suspicion, held);
    EXPECT(!w.engine.stalled(), "a world change ends any stall window");
    w.hidden.clear();
    w.context = GameContext::InGame;
    for (int i = 0; i < 2; ++i) w.frame();
    bool halved = true;
    for (std::size_t i = 0; i < heldByCheck.size(); ++i) {
        halved = halved && w.rec(cheat)->checks[i].model.value() <= heldByCheck[i] * 0.5f + 0.01f;
    }
    EXPECT(halved && w.rec(cheat)->suspicion < held, "each check's confidence carries into the new game at half (%.1f%% overall, was %.1f%%)",
           w.rec(cheat)->suspicion, held);
    EXPECT(w.engine.match().matchCount() == 2, "second match");
}

// ── Player state ────────────────────────────────────────────────────────────────────────────

static void testPlayerStateTable() {
    TempFile marks("vigil_test_state_marks.json");
    World w;
    w.engine.setMarksPath(marks.path);
    Sim local(1, "Observer"), plain(2, "Plain"), mate(3, "Mate"), exempt(4, "Buddy"), npc(5, "Shopkeeper");
    setupLocal(local);
    mate.f.isTeammate = true;
    npc.f.uuid = "00000000-0000-2000-8000-000000000005";   // version 2: server NPC
    for (Sim* s : {&local, &plain, &mate, &exempt, &npc}) w.add(*s);
    w.engine.marks().setExempt(exempt.f.uuid, "Buddy", true);
    for (int i = 0; i < 300; ++i) {
        for (Sim* s : {&plain, &mate, &exempt}) lowHop(*s);
        w.frame();
    }
    struct Row { const char* who; const PlayerRecord* r; bool listed, judged, evidence; };
    const Row rows[] = {
        {"plain player", w.rec(plain), true, true, true},
        {"teammate", w.rec(mate), true, false, false},
        {"exempt player", w.rec(exempt), true, false, false},
        {"local player", w.rec(local), true, false, false},
        {"server NPC", w.rec(npc), false, false, false},
    };
    for (const Row& row : rows) {
        EXPECT((row.r != nullptr) == row.listed, "%s: listed=%d", row.who, row.r != nullptr);
        if (!row.r) continue;
        const PlayerState s = w.engine.playerState(*row.r);
        EXPECT(s.judged == row.judged, "%s: judged=%d expected %d", row.who, s.judged, row.judged);
        EXPECT(!row.r->evidence.empty() == row.evidence, "%s: evidence=%zu", row.who, row.r->evidence.size());
    }
    int alertsForUnjudged = 0;
    for (const auto& a : w.engine.alerts()) if (a.name != "Plain") ++alertsForUnjudged;
    EXPECT(alertsForUnjudged == 0, "alerts only for judged players (%d others)", alertsForUnjudged);
}

// ── Envelopes ───────────────────────────────────────────────────────────────────────────────

static void testEnvelopes() {
    World w;
    Sim local(1, "Observer"), cheat(2, "Hopper");
    setupLocal(local);
    w.add(local);
    w.add(cheat);
    for (int i = 0; i < 600; ++i) { lowHop(cheat); w.frame(); }
    const PlayerRecord* r = w.rec(cheat);
    EXPECT(r && !r->evidence.empty(), "sanity: evidence");
    if (!r) return;
    int withEnvelope = 0, withExpected = 0, outside = 0;
    for (const Evidence& e : r->evidence) {
        if (!e.envelope.valid()) continue;
        ++withEnvelope;
        if (e.envelope.hasExpected) ++withExpected;
        // Limits are inclusive thresholds: evidence sits at or beyond the edge, never inside.
        if (e.envelope.upper ? e.envelope.observed >= e.envelope.limit : e.envelope.observed <= e.envelope.limit) ++outside;
        if (e.envelope.hasExpected) {
            // The vanilla expectation sits on the legitimate side of the limit.
            EXPECT(e.envelope.upper ? e.envelope.expected <= e.envelope.limit : e.envelope.expected >= e.envelope.limit,
                   "expected %.3f on the legitimate side of %.3f (%s)", e.envelope.expected, e.envelope.limit, e.summary.c_str());
        }
    }
    EXPECT(withEnvelope == static_cast<int>(r->evidence.size()), "every observation has an envelope (%d of %zu)", withEnvelope,
           r->evidence.size());
    EXPECT(withExpected > 0, "physics checks state the vanilla expectation (%d)", withExpected);
    EXPECT(outside == withEnvelope, "evidence is never recorded inside the legitimate envelope (%d of %d)", outside, withEnvelope);

    // A lower-bound envelope (knockback taken) derives the right side.
    std::vector<EvidenceValue> v = {{"Push away from attacker", 0.05, "blocks", 0.3, true},
                                    {"Vanilla push", 1.4, "blocks", 0.0, false, EvidenceValue::Role::Expected},
                                    {"Victim ping", 80.0, "ms"}};
    const Envelope e = deriveEnvelope(v);
    EXPECT(e.valid() && !e.upper && e.hasExpected && e.expected == 1.4, "lower-bound envelope");
    EXPECT(std::fabs(e.deviation() - (0.3 - 0.05) / 0.3) < 1e-9, "deviation below a lower bound");
    EXPECT(e.context.size() == 1 && e.context[0] == "Victim ping 80.0 ms", "context (%s)", e.context.empty() ? "" : e.context[0].c_str());
}

// ── Determinism and traces ──────────────────────────────────────────────────────────────────

static void testDeterminism() {
    const auto frames = goldenArena();
    Engine a, b;
    a.initialize();
    b.initialize();
    const ReplayOutcome oa = replayFrames(a, frames);
    const ReplayOutcome ob = replayFrames(b, frames);
    const auto diffs = compareOutcomes(oa, ob, 0.f);
    EXPECT(diffs.empty(), "two engines, same frames, same outcome (%zu differences: %s)", diffs.size(),
           diffs.empty() ? "" : diffs.front().c_str());
    // The scenario must actually exercise the engine.
    EXPECT(oa.players.count("LowHopper") && !oa.players.at("LowHopper").detections.empty(), "the cheater is detected");
    EXPECT(oa.players.count("Sprinter") && oa.players.at("Sprinter").detections.empty(), "the legitimate player is not");
}

static void testTraceRoundTrip() {
    const auto frames = goldenArena();
    TraceHeader h;
    h.vigil = "test";
    std::string error;
    TraceHeader hb;
    EXPECT(parseTraceHeader(traceHeaderLine(h), hb, error) && hb.vigil == "test", "header (%s)", error.c_str());
    std::vector<WorldFrame> back;
    for (const auto& f : frames) {
        WorldFrame g;
        if (!parseTraceFrame(traceFrameLine(f), g, error)) { EXPECT(false, "frame parse: %s", error.c_str()); return; }
        back.push_back(std::move(g));
    }
    bool exact = back.size() == frames.size();
    for (std::size_t i = 0; exact && i < frames.size(); ++i) {
        exact = traceFrameLine(frames[i]) == traceFrameLine(back[i]) && frames[i].players.size() == back[i].players.size();
        for (std::size_t k = 0; exact && k < frames[i].players.size(); ++k) {
            const auto& p = frames[i].players[k];
            const auto& q = back[i].players[k];
            exact = p.pos.x == q.pos.x && p.pos.y == q.pos.y && p.pos.z == q.pos.z && p.yaw == q.yaw && p.onGround == q.onGround &&
                    p.uuid == q.uuid && p.potions.speed == q.potions.speed && p.tabListed == q.tabListed && p.pingMs == q.pingMs;
        }
    }
    EXPECT(exact, "frames survive the trace format exactly");
    Engine a, b;
    a.initialize();
    b.initialize();
    const auto diffs = compareOutcomes(replayFrames(a, frames), replayFrames(b, back), 0.f);
    EXPECT(diffs.empty(), "a replayed trace reproduces the session (%s)", diffs.empty() ? "" : diffs.front().c_str());
    WorldFrame bad;
    EXPECT(!parseTraceFrame("{\"pl\":[{\"e\":1}]}", bad, error), "a player without a position is refused");
    EXPECT(!parseTraceHeader("{\"kind\":\"vigil-trace\",\"format\":99}", hb, error), "a newer trace format is refused");
}

// The committed golden trace replays to its committed outcome. When an intended engine change
// moves the outcome, regenerate both files (docs/VALIDATION.md) and review the diff.
static void testGoldenTrace() {
    const std::string dir = VIGIL_TRACE_DIR;
    TraceHeader header;
    std::vector<WorldFrame> frames;
    std::string error;
    EXPECT(readTrace(dir + "/golden-arena.vtrace", header, frames, error), "golden trace readable (%s)", error.c_str());
    ReplayOutcome expected;
    EXPECT(outcomeFromJson(readAll(dir + "/golden-arena.expect.json"), expected, error), "expectation readable (%s)", error.c_str());
    Engine e;
    e.initialize();
    const auto diffs = compareOutcomes(expected, replayFrames(e, frames));
    for (const auto& d : diffs) std::printf("      %s\n", d.c_str());
    EXPECT(diffs.empty(), "golden trace outcome (%zu differences)", diffs.size());
    // The committed trace is the scenario as written today: if the simulator changes, regenerate.
    const auto now = goldenArena();
    EXPECT(now.size() == frames.size() && (frames.empty() || traceFrameLine(now.back()) == traceFrameLine(frames.back())),
           "the committed trace matches tests/scenarios.h");
}

int main(int argc, char** argv) {
    const Test tests[] = {
        {"confidence invariants", testConfidenceInvariants},
        {"match lifecycle", testMatchLifecycle},
        {"player state table", testPlayerStateTable},
        {"envelopes", testEnvelopes},
        {"determinism", testDeterminism},
        {"trace round trip", testTraceRoundTrip},
        {"golden trace", testGoldenTrace},
    };
    return runSuite(argc, argv, tests, "validation");
}
