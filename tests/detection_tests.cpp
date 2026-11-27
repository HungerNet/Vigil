// Vigil detection tests — synthetic traces built from vanilla 1.8.9 movement physics.
//
// Each scenario drives the real Engine with WorldFrames, exactly as the game adapter does, and
// asserts that legitimate behavior stays quiet while cheating behavior produces detections.

#include "../src/core/json.h"
#include "../src/core/settings.h"
#include "../src/detection/config_binding.h"
#include "../src/detection/engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace vigil;
using namespace vigil::detection;

#include "sim.h"
#include "test_framework.h"

using namespace vigil::test;

// ── Tests ─────────────────────────────────────────────────────────────────────────────────────

static void testPhysicsBaseline() {
    Sim s(1, "Baseline");
    int air = 0;
    double startX = 0.0;
    bool counting = false;
    double dist = 0.0;
    int ticks = 0;
    // Warm up sprinting.
    for (int i = 0; i < 40; ++i) s.step(1.0, false);
    startX = s.f.pos.z;
    for (int i = 0; i < 200; ++i) {
        const bool wasGround = s.f.onGround;
        s.step(1.0, true);
        if (wasGround && !s.f.onGround) { counting = true; air = 0; }
        if (counting && !s.f.onGround) ++air;
        if (counting && s.f.onGround && !wasGround) { counting = false; }
        ++ticks;
    }
    dist = s.f.pos.z - startX;
    const double avg = dist / ticks;
    EXPECT(std::fabs(avg - mc::kSprintJumpAverage) < 0.03, "sprint-jump average %.4f b/t (expected ~%.4f)", avg, mc::kSprintJumpAverage);
    // Air time of a flat jump.
    Sim j(2, "Jump");
    for (int i = 0; i < 10; ++i) j.step(1.0, false);
    j.step(1.0, true);
    int airTicks = 0;
    while (!j.f.onGround && airTicks < 40) { j.step(1.0, false); ++airTicks; }
    EXPECT(airTicks + 1 >= 11 && airTicks + 1 <= 13, "flat jump air ticks = %d", airTicks + 1);
}

static void testLegitMovementIsQuiet() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim runner(2, "Sprinter");
    w.add(local);
    w.add(runner);
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> turn(-6.0, 6.0);
    for (int i = 0; i < 20 * 120; ++i) {  // two minutes of sprint-jumping with turns
        runner.moveYaw += turn(rng);
        runner.f.yaw = static_cast<float>(runner.moveYaw);
        runner.step(1.0, true);
        w.frame();
    }
    EXPECT(w.detections(runner, "bhop") == 0, "bhop detections=%d evidence=%d", w.detections(runner, "bhop"), w.evidence(runner, "bhop"));
    EXPECT(w.detections(runner, "speed") == 0, "speed detections=%d evidence=%d", w.detections(runner, "speed"), w.evidence(runner, "speed"));
    EXPECT(w.totalDetections(runner) == 0, "total detections=%d", w.totalDetections(runner));
    EXPECT(w.evidence(runner, "bhop") <= 1, "bhop evidence=%d", w.evidence(runner, "bhop"));
}

static void testSpeedPotionLegit() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim runner(2, "Potion");
    runner.f.potions.speed = 1;  // Speed II
    w.add(local);
    w.add(runner);
    for (int i = 0; i < 20 * 60; ++i) {
        // Speed II scales ground acceleration by 1.4.
        const double rad = runner.moveYaw * 3.14159265358979323846 / 180.0;
        const Vec3 fwd{-std::sin(rad), 0.0, std::cos(rad)};
        if (runner.f.onGround) runner.motion = runner.motion + fwd * (0.13 * 0.4);
        runner.step(1.0, true);
        w.frame();
    }
    EXPECT(w.detections(runner, "speed") == 0, "speed detections with Speed II=%d", w.detections(runner, "speed"));
    EXPECT(w.detections(runner, "bhop") == 0, "bhop detections with Speed II=%d", w.detections(runner, "bhop"));
}

// Speed II drunk while already in view: the effect list is stale (vanilla only sends effects when
// tracking starts), but the movement attribute and potion particles are current.
static int runStaleSpeedPotion(bool attribute, bool particles, const char* checkId) {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim runner(2, "StalePotion");
    runner.f.potions.speed = -1;   // what the client believes
    runner.f.potionParticles = particles;
    if (attribute) {
        runner.f.moveSpeedAttribute = 0.1 * 1.3 * 1.4;   // sprinting with Speed II
        runner.f.sprintModifier = true;
    }
    w.add(local);
    w.add(runner);
    // Ground sprinting: Speed II raises it from 0.28 to ~0.39 b/tick, well past the 0.31 ground
    // limit, so this is where a stale effect list produces false positives.
    for (int i = 0; i < 20 * 60; ++i) {
        const double rad = runner.moveYaw * 3.14159265358979323846 / 180.0;
        const Vec3 fwd{-std::sin(rad), 0.0, std::cos(rad)};
        if (runner.f.onGround) runner.motion = runner.motion + fwd * (0.13 * 0.4);
        runner.step(1.0, false);
        w.frame();
    }
    return w.evidence(runner, checkId);
}

static void testStalePotionUsesAttribute() {
    EXPECT(runStaleSpeedPotion(false, false, "speed") > 0,
           "sanity: Speed II movement with no modifier data should exceed the unmodified limit");
    EXPECT(runStaleSpeedPotion(true, false, "speed") == 0, "speed evidence with the movement attribute present");
    EXPECT(runStaleSpeedPotion(false, true, "speed") == 0, "speed evidence with potion particles but a stale effect list");
}

// Sprint-jumping under a two-block ceiling: each jump is ~4 ticks and adds the sprint-jump boost.
static void runHeadHitter(bool ceilingVisible, int& speedEvidence, int& bhopEvidence) {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim runner(2, "HeadHitter");
    runner.ceilingY = runner.groundY + 2.0;
    runner.f.blockAboveHead = ceilingVisible ? 1 : 0;
    w.add(local);
    w.add(runner);
    for (int i = 0; i < 20 * 60; ++i) {
        runner.step(1.0, true);
        w.frame();
    }
    speedEvidence = w.evidence(runner, "speed");
    bhopEvidence = w.evidence(runner, "bhop");
}

static void testHeadHitterIsQuiet() {
    int speed = 0, bhop = 0;
    runHeadHitter(false, speed, bhop);
    EXPECT(speed > 0, "sanity: head-hitter speed without a visible ceiling should look like speed (evidence=%d)", speed);
    runHeadHitter(true, speed, bhop);
    EXPECT(speed == 0, "speed evidence under a ceiling=%d", speed);
    EXPECT(bhop == 0, "bhop evidence under a ceiling=%d", bhop);
}

static void testCheatBhop() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim cheat(2, "LowHopper");
    w.add(local);
    w.add(cheat);
    // Low hops (motionY 0.25) with speed held at 0.55 b/tick.
    for (int i = 0; i < 20 * 40; ++i) {
        const double rad = cheat.moveYaw * 3.14159265358979323846 / 180.0;
        const Vec3 fwd{-std::sin(rad), 0.0, std::cos(rad)};
        if (cheat.f.onGround) {
            cheat.motion.y = 0.25;
            cheat.f.pos = cheat.f.pos + Vec3{0, 0.01, 0};
            cheat.f.onGround = false;
        }
        cheat.f.pos = cheat.f.pos + fwd * 0.55;
        cheat.f.pos.y += cheat.motion.y;
        if (cheat.f.pos.y <= cheat.groundY) {
            cheat.f.pos.y = cheat.groundY;
            cheat.motion.y = 0.0;
            cheat.f.onGround = true;
        } else {
            cheat.motion.y = (cheat.motion.y - 0.08) * 0.98;
        }
        cheat.f.sprinting = true;
        cheat.tickAnimations();
        w.frame();
    }
    EXPECT(w.detections(cheat, "bhop") >= 1, "bhop detections=%d evidence=%d short=%d chain=%d", w.detections(cheat, "bhop"),
           w.evidence(cheat, "bhop"), w.indicatorCount(cheat, "bhop", "short_air"), w.indicatorCount(cheat, "bhop", "hop_speed"));
    EXPECT(w.detections(cheat, "speed") >= 1, "speed detections=%d evidence=%d", w.detections(cheat, "speed"), w.evidence(cheat, "speed"));
}

static void testAirStrafe() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim cheat(2, "Strafer");
    w.add(local);
    w.add(cheat);
    for (int i = 0; i < 40; ++i) { cheat.step(1.0, false); w.frame(); }
    for (int hop = 0; hop < 12; ++hop) {
        cheat.step(1.0, true);  // takeoff
        w.frame();
        // In the air, rotate velocity 90° over the jump while keeping its magnitude.
        while (!cheat.f.onGround) {
            const double speed = std::max(0.30, cheat.motion.horizontalLength());
            cheat.moveYaw += 9.0;
            const double rad = cheat.moveYaw * 3.14159265358979323846 / 180.0;
            cheat.motion.x = -std::sin(rad) * speed;
            cheat.motion.z = std::cos(rad) * speed;
            cheat.step(0.0, false);
            w.frame();
        }
        for (int g = 0; g < 2; ++g) { cheat.step(1.0, false); w.frame(); }
    }
    EXPECT(w.indicatorCount(cheat, "bhop", "air_strafe") >= 3, "air strafe evidence=%d", w.indicatorCount(cheat, "bhop", "air_strafe"));
    EXPECT(w.detections(cheat, "bhop") >= 1, "bhop detections=%d", w.detections(cheat, "bhop"));
}

// Legit duel: attacker faces victim and hits every 12 ticks from ~2.6 blocks.
static void runDuel(World& w, Sim& attacker, Sim& victim, double distance, int hits, bool aimAway, bool victimAntiKb,
                    bool legitKb = true) {
    for (int h = 0; h < hits; ++h) {
        for (int t = 0; t < 12; ++t) {
            // Keep distance: victim stands at `distance` blocks along +Z.
            if (t == 0) {
                victim.f.pos = {attacker.f.pos.x, victim.groundY, attacker.f.pos.z + distance};
                victim.f.onGround = true;
                victim.motion = {};
            }
            attacker.lookAt(victim);
            if (aimAway) attacker.f.yaw += 180.f;
            if (t == 0) { attacker.swing(); }
            if (t == 1) {
                victim.hurt();
                if (!victimAntiKb && legitKb) {
                    victim.motion.y = 0.3608;
                    victim.motion.z += 0.4;
                    victim.f.onGround = false;
                    victim.f.pos.y += 0.0001;
                }
            }
            attacker.tickAnimations();
            // Victim physics (no input).
            if (!victim.f.onGround || victim.motion.horizontalLength() > 0) {
                victim.f.pos = victim.f.pos + Vec3{victim.motion.x, victim.motion.y, victim.motion.z};
                victim.motion.x *= victim.f.onGround ? 0.546 : 0.91;
                victim.motion.z *= victim.f.onGround ? 0.546 : 0.91;
                if (victim.f.pos.y <= victim.groundY) {
                    victim.f.pos.y = victim.groundY;
                    victim.motion.y = 0;
                    victim.f.onGround = true;
                } else {
                    victim.f.onGround = false;
                    victim.motion.y = (victim.motion.y - 0.08) * 0.98;
                }
            }
            if (victim.f.hurtTime > 0) --victim.f.hurtTime;
            // Small idle sway so the victim always has position updates.
            victim.f.pos.x += (t % 2 == 0 ? 0.04 : -0.04);
            w.frame();
        }
    }
}

static void testKillAuraLegitDuelIsQuiet() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim a(2, "Fighter"), v(3, "Target");
    a.f.pos = {0, 64, 0};
    w.add(local);
    w.add(a);
    w.add(v);
    for (int i = 0; i < 30; ++i) w.frame();
    runDuel(w, a, v, 2.6, 25, false, false);
    EXPECT(w.evidence(a, "killaura") == 0, "killaura evidence=%d", w.evidence(a, "killaura"));
    EXPECT(w.evidence(a, "reach") == 0, "reach evidence=%d", w.evidence(a, "reach"));
    EXPECT(w.evidence(v, "velocity") == 0, "velocity evidence on legit victim=%d", w.evidence(v, "velocity"));
}

static void testOldKillAuraBugRegression() {
    // The old check flagged a swinging player whenever the LOCAL player faced them. A player
    // swinging at the air in front of an observer must produce no KillAura evidence.
    World w;
    Sim local(1, "Observer");
    local.f.isLocal = true;
    local.f.pos = {0, 64, 0};
    Sim swinger(2, "AirSwinger");
    swinger.f.pos = {0, 64, 2.5};
    w.add(local);
    w.add(swinger);
    for (int i = 0; i < 400; ++i) {
        local.lookAt(swinger);
        if (i % 4 == 0) swinger.swing();
        swinger.tickAnimations();
        w.frame();
    }
    EXPECT(w.evidence(swinger, "killaura") == 0, "killaura evidence=%d", w.evidence(swinger, "killaura"));
}

static void testKillAuraOutsideView() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim a(2, "Aura"), v(3, "Victim");
    w.add(local);
    w.add(a);
    w.add(v);
    for (int i = 0; i < 30; ++i) w.frame();
    runDuel(w, a, v, 2.6, 8, true, false);
    EXPECT(w.indicatorCount(a, "killaura", "outside_view") >= 6, "outside view evidence=%d", w.indicatorCount(a, "killaura", "outside_view"));
    EXPECT(w.detections(a, "killaura") >= 1, "killaura detections=%d", w.detections(a, "killaura"));
}

static void testKillAuraMultiTarget() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim a(2, "MultiAura"), v1(3, "Left"), v2(4, "Right");
    a.f.pos = {0, 64, 0};
    v1.f.pos = {-2.2, 64, 0.5};
    v2.f.pos = {2.2, 64, 0.5};
    w.add(local);
    w.add(a);
    w.add(v1);
    w.add(v2);
    for (int i = 0; i < 30; ++i) w.frame();
    for (int h = 0; h < 8; ++h) {
        for (int t = 0; t < 12; ++t) {
            a.lookAt(t < 6 ? v1 : v2);
            if (t == 0) a.swing();
            if (t == 1) { v1.hurt(); v2.hurt(); }
            a.tickAnimations();
            if (v1.f.hurtTime > 0) --v1.f.hurtTime;
            if (v2.f.hurtTime > 0) --v2.f.hurtTime;
            v1.f.pos.z += (t % 2 ? 0.03 : -0.03);
            v2.f.pos.z += (t % 2 ? -0.03 : 0.03);
            w.frame();
        }
    }
    EXPECT(w.indicatorCount(a, "killaura", "multi_target") >= 5, "multi-target evidence=%d", w.indicatorCount(a, "killaura", "multi_target"));
    EXPECT(w.detections(a, "killaura") >= 1, "killaura detections=%d", w.detections(a, "killaura"));
}

static void testReach() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim a(2, "LongArm"), v(3, "Victim");
    w.add(local);
    w.add(a);
    w.add(v);
    for (int i = 0; i < 30; ++i) w.frame();
    runDuel(w, a, v, 4.3, 10, false, false, false);  // feet 4.3 apart → ~3.9 to the hitbox
    EXPECT(w.indicatorCount(a, "reach", "distance") >= 8, "reach evidence=%d", w.indicatorCount(a, "reach", "distance"));
    EXPECT(w.detections(a, "reach") >= 1, "reach detections=%d", w.detections(a, "reach"));
}

static void testVelocity() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim a(2, "Hitter"), v(3, "NoKb");
    w.add(local);
    w.add(a);
    w.add(v);
    for (int i = 0; i < 30; ++i) w.frame();
    runDuel(w, a, v, 2.6, 8, false, true);
    EXPECT(w.indicatorCount(v, "velocity", "vertical") >= 5, "vertical evidence=%d", w.indicatorCount(v, "velocity", "vertical"));
    EXPECT(w.detections(v, "velocity") >= 1, "velocity detections=%d", w.detections(v, "velocity"));
}

static void testAutoClicker() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim bot(2, "Clicker"), human(3, "Human"), floorSetter(4, "Floor"), dummy(5, "Dummy");
    bot.f.pos = {0, 64, 0};
    human.f.pos = {50, 64, 0};
    floorSetter.f.pos = {100, 64, 0};
    dummy.f.pos = {0, 64, 3};
    Sim dummy2(6, "Dummy2");
    dummy2.f.pos = {50, 64, 3};
    // A legitimate fighter hitting once per hurt cycle (exactly every 12 ticks) is combat
    // rhythm, not clicking: it must never count as autoclicking (found via the preview arena).
    Sim comboer(7, "Comboer");
    comboer.f.pos = {52, 64, 3};
    w.add(comboer);
    w.add(local);
    w.add(bot);
    w.add(human);
    w.add(floorSetter);
    w.add(dummy);
    w.add(dummy2);
    std::mt19937 rng(3);
    std::discrete_distribution<int> humanIntervals({0, 0, 0, 2, 5, 4, 2, 1});  // 3..7 ticks
    int nextHuman = 4;
    for (int i = 0; i < 20 * 30; ++i) {
        if (i % 4 == 0) bot.swing();
        if (i % 12 == 0) comboer.swing();
        if (--nextHuman <= 0) { human.swing(); nextHuman = humanIntervals(rng); if (nextHuman < 3) nextHuman = 3; }
        if (i % 3 == 0 && i < 60) floorSetter.swing();  // establishes a relay floor of 3 ticks
        bot.f.heldItem = human.f.heldItem = 276;
        w.frame();
        bot.tickAnimations();
        human.tickAnimations();
        floorSetter.tickAnimations();
        comboer.tickAnimations();
    }
    EXPECT(w.evidence(comboer, "autoclicker") == 0, "12-tick combo rhythm produced autoclicker evidence=%d",
           w.evidence(comboer, "autoclicker"));
    EXPECT(w.evidence(bot, "autoclicker") >= 3, "bot autoclicker evidence=%d", w.evidence(bot, "autoclicker"));
    EXPECT(w.detections(bot, "autoclicker") >= 1, "bot autoclicker detections=%d", w.detections(bot, "autoclicker"));
    EXPECT(w.detections(human, "autoclicker") == 0, "human autoclicker detections=%d evidence=%d", w.detections(human, "autoclicker"),
           w.evidence(human, "autoclicker"));
}

static void testNoSlow() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim cheat(2, "NoSlower"), legit(3, "Eater");
    legit.f.pos = {40, 64, 0};
    w.add(local);
    w.add(cheat);
    w.add(legit);
    for (int i = 0; i < 20 * 40; ++i) {
        const bool eating = (i % 60) > 10;
        cheat.f.heldItem = legit.f.heldItem = 322;
        cheat.f.usingItem = eating;
        legit.f.usingItem = eating;
        cheat.step(1.0, false, true);  // keeps sprinting at full speed
        legit.step(eating ? 0.2 : 1.0, false, !eating);
        w.frame();
    }
    EXPECT(w.detections(cheat, "noslow") >= 1, "noslow detections=%d evidence=%d", w.detections(cheat, "noslow"), w.evidence(cheat, "noslow"));
    EXPECT(w.detections(legit, "noslow") == 0, "legit noslow detections=%d evidence=%d", w.detections(legit, "noslow"),
           w.evidence(legit, "noslow"));
}

static void testConfidenceModel() {
    ConfidenceParams p;
    p.halfLifeSec = 10.f;
    p.minEvidence = 3;
    p.alertThreshold = 50.f;
    ConfidenceModel m;
    m.add(0.0, 1.f, 1.f, 0, p);
    EXPECT(std::fabs(m.value() - 25.f) < 0.01f, "first evidence %.2f", m.value());
    m.add(0.0, 1.f, 1.f, 0, p);
    m.add(0.0, 1.f, 1.f, 0, p);
    EXPECT(m.value() > 50.f && m.canDetect(0.0, p), "after three: %.2f", m.value());
    m.decayTo(10000.0, p.halfLifeSec);
    const float after = m.value();
    m.decayTo(20000.0, p.halfLifeSec);
    EXPECT(std::fabs(m.value() - after * 0.5f) < 0.05f, "half-life decay %.2f -> %.2f", after, m.value());
    ConfidenceModel single;
    ConfidenceParams strong = p;
    single.add(0.0, 4.f, 1.f, 0, strong);
    EXPECT(!single.canDetect(0.0, strong), "single strong observation must not detect (%.1f)", single.value());
}

static void testJsonAndSettings() {
    json::Value v = json::Value::object();
    v["name"] = json::Value("Vigil \"quoted\" \xE2\x80\xA2");
    v["n"] = json::Value(3.25);
    v["arr"] = json::Value::array();
    v["arr"].push(json::Value(true));
    v["arr"].push(json::Value());
    const std::string text = json::write(v, 2);
    json::Value back;
    EXPECT(json::parse(text, back), "parse failed");
    EXPECT(back.get("name").asString() == v.get("name").asString(), "string roundtrip");
    EXPECT(std::fabs(back.get("n").asNumber() - 3.25) < 1e-9, "number roundtrip");
    EXPECT(back.get("arr").size() == 2 && back.get("arr").at(0).asBool(), "array roundtrip");
    json::Value bad;
    EXPECT(!json::parse("{\"a\": [1, 2,}", bad), "malformed input rejected");

    auto& S = Settings::instance();
    Setting& f = S.addFloat("test.float", "Float", 1.5, {0.0, 10.0, 0.5, 1, ""}, "");
    f.set(3.3);
    EXPECT(std::fabs(f.value - 3.5) < 1e-9, "step snapping %.3f", f.value);
    f.set(99.0);
    EXPECT(std::fabs(f.value - 10.0) < 1e-9, "clamping %.3f", f.value);
    char tmp[512];
    std::snprintf(tmp, sizeof(tmp), "%s\\vigil_test_config.json", std::getenv("TEMP") ? std::getenv("TEMP") : ".");
    S.setPath(tmp);
    EXPECT(S.save(), "save");
    f.set(0.5);
    EXPECT(S.load(), "load");
    EXPECT(std::fabs(f.value - 10.0) < 1e-9, "persisted value %.3f", f.value);
    std::remove(tmp);
}

static void testPresetsKeepDefaults() {
    Engine e;
    e.initialize();
    Settings& S = Settings::instance();
    registerEngineSettings(S, e.schema());
    for (const auto& c : e.checks()) {
        EXPECT(presetSetting(S, *c)->i() == 1, "%s preset should default to Balanced", c->id().c_str());
        applyPreset(S, *c, 1);
        for (Setting* s : S.withPrefix(c->settingsPrefix())) {
            if (isPresetControlled(*c, *s)) EXPECT(s->isDefault(), "%s: Balanced value differs from default", s->key.c_str());
        }
    }
}

// ── Quality pass (1.0-dev.8): root-cause regressions ─────────────────────────────────────────

// One tick of the low-hop cheat used by several scenarios (motionY 0.25, 0.55 b/tick).
static void lowHopStep(Sim& c) {
    const double rad = c.moveYaw * 3.14159265358979323846 / 180.0;
    const Vec3 fwd{-std::sin(rad), 0.0, std::cos(rad)};
    if (c.f.onGround) {
        c.motion.y = 0.25;
        c.f.pos = c.f.pos + Vec3{0, 0.01, 0};
        c.f.onGround = false;
    }
    c.f.pos = c.f.pos + fwd * 0.55;
    c.f.pos.y += c.motion.y;
    if (c.f.pos.y <= c.groundY) {
        c.f.pos.y = c.groundY;
        c.motion.y = 0.0;
        c.f.onGround = true;
    } else {
        c.motion.y = (c.motion.y - 0.08) * 0.98;
    }
    c.f.sprinting = true;
    c.tickAnimations();
}

// Prints a player's evidence when VIGIL_TEST_VERBOSE is set (diagnosing a failing scenario).
static void dumpEvidence(const World& w, const Sim& s) {
    if (!std::getenv("VIGIL_TEST_VERBOSE")) return;
    if (const PlayerRecord* r = w.rec(s)) {
        for (const auto& e : r->evidence) {
            std::printf("      [%s] t=%lld %s |", w.engine.checks()[static_cast<std::size_t>(e.check)]->id().c_str(),
                        static_cast<long long>(e.tick), e.summary.c_str());
            for (const auto& v : e.values) std::printf(" %s=%.3f", v.name.c_str(), v.value);
            std::printf("\n");
        }
    }
}

static float suspicionOf(const World& w, const Sim& s) {
    const PlayerRecord* r = w.rec(s);
    return r ? r->suspicion : -1.f;
}

// V1/V2: the victim's knockback reaches the observer only after their latency. A fixed 5-tick
// window read every high-ping victim as "did not move". Hits are spaced so the victim lands and
// settles; the attacker follows the victim instead of the victim being reset.
static int runLatencyVelocity(int relayDelay, int pingMs, bool antiKb, int& detections) {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim a(2, "Hitter"), v(3, "HighPing");
    v.delay = relayDelay;
    v.f.pingMs = pingMs;
    v.f.pos = {0, 64, 2.6};
    w.add(local);
    w.add(a);
    w.add(v);
    for (int i = 0; i < 30; ++i) w.frame();
    for (int h = 0; h < 12; ++h) {
        for (int t = 0; t < 30; ++t) {
            if (t == 0) a.f.pos = {v.f.pos.x, 64.0, v.f.pos.z - 2.6};
            a.lookAt(v);
            if (t == 0) a.swing();
            if (t == 1) {
                v.hurt();
                if (!antiKb) {
                    v.motion.y = 0.3608;
                    v.motion.z += 0.4;
                    v.f.onGround = false;
                    v.f.pos.y += 0.0001;
                }
            }
            a.tickAnimations();
            if (!v.f.onGround || v.motion.horizontalLength() > 0) {
                v.f.pos = v.f.pos + v.motion;
                v.motion.x *= v.f.onGround ? 0.546 : 0.91;
                v.motion.z *= v.f.onGround ? 0.546 : 0.91;
                if (v.f.pos.y <= v.groundY) {
                    v.f.pos.y = v.groundY;
                    v.motion.y = 0;
                    v.f.onGround = true;
                } else {
                    v.f.onGround = false;
                    v.motion.y = (v.motion.y - 0.08) * 0.98;
                }
            }
            if (v.f.hurtTime > 0) --v.f.hurtTime;
            v.f.pos.x += (t % 2 == 0 ? 0.04 : -0.04);
            w.frame();
        }
    }
    detections = w.detections(v, "velocity");
    dumpEvidence(w, v);
    return w.evidence(v, "velocity");
}

static void testVelocityLatency() {
    int det = 0;
    EXPECT(runLatencyVelocity(7, 250, false, det) == 0, "legit 250 ms victim produced velocity evidence");
    EXPECT(runLatencyVelocity(6, -1, false, det) == 0, "legit victim with unknown ping produced velocity evidence");
    const int cheat = runLatencyVelocity(7, 250, true, det);
    EXPECT(cheat >= 5 && det >= 1, "anti-knockback behind 250 ms is still detected (evidence=%d detections=%d)", cheat, det);
}

// S1: after a lag catch-up, a window that starts inside the frozen period counted more movement
// than its ticks. Speeds between received positions do not.
static void testSpeedLagCatchUp() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim runner(2, "Laggy"), cheat(3, "LaggyCheat");
    cheat.f.pos = {60, 64, 0};
    runner.cadence = cheat.cadence = 2;
    w.add(local);
    w.add(runner);
    w.add(cheat);
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> turn(-4.0, 4.0);
    for (int i = 0; i < 20 * 90; ++i) {
        // 10-tick connection hiccups: the catch-up (~3.5 blocks) is too small to be a teleport.
        const bool lag = (i % 80) >= 70;
        runner.freeze = cheat.freeze = lag;
        runner.moveYaw += turn(rng);
        runner.step(1.0, true);
        lowHopStep(cheat);
        w.frame();
    }
    EXPECT(w.evidence(runner, "speed") == 0, "legit runner with lag catch-ups: speed evidence=%d", w.evidence(runner, "speed"));
    EXPECT(w.evidence(runner, "bhop") == 0, "legit runner with lag catch-ups: bhop evidence=%d", w.evidence(runner, "bhop"));
    EXPECT(w.detections(cheat, "speed") >= 1, "speed cheat with the same lag is still detected (evidence=%d)", w.evidence(cheat, "speed"));
}

// S2: Speed II ends; the previous second at 1.4× must not be judged against 1.0×. Ground
// sprinting with Speed II (~0.39 b/tick) is well past the 0.31 ground limit, so the ticks right
// after it ends are where judging with the current multiplier goes wrong.
static void testExpiringSpeedEffect() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim runner(2, "Expiring");
    w.add(local);
    w.add(runner);
    for (int i = 0; i < 20 * 90; ++i) {
        const bool potion = (i % 300) < 220;
        runner.f.moveSpeedAttribute = potion ? 0.1 * 1.3 * 1.4 : 0.1 * 1.3;
        runner.f.sprintModifier = true;
        const double rad = runner.moveYaw * 3.14159265358979323846 / 180.0;
        const Vec3 fwd{-std::sin(rad), 0.0, std::cos(rad)};
        if (potion && runner.f.onGround) runner.motion = runner.motion + fwd * (0.13 * 0.4);
        runner.step(1.0, false);
        w.frame();
    }
    dumpEvidence(w, runner);
    EXPECT(w.evidence(runner, "speed") == 0, "speed evidence after Speed II expired=%d", w.evidence(runner, "speed"));
    EXPECT(w.evidence(runner, "bhop") == 0, "bhop evidence after Speed II expired=%d", w.evidence(runner, "bhop"));
}

// B3: jumping diagonally into a wall zeroes one axis mid-air — a collision, not a strafe.
static void testWallCollisionIsNotStrafe() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim runner(2, "WallJumper");
    runner.moveYaw = 45.0;   // −X, +Z
    w.add(local);
    w.add(runner);
    for (int i = 0; i < 20 * 60; ++i) {
        if (runner.f.onGround) runner.f.pos.x = -1.2;   // back off the wall before each jump
        runner.step(1.0, true);
        if (runner.f.pos.x < -3.0) {
            runner.f.pos.x = -3.0;
            runner.motion.x = 0.0;
        }
        w.frame();
    }
    EXPECT(w.indicatorCount(runner, "bhop", "air_strafe") == 0, "wall collision read as air strafe (%d)",
           w.indicatorCount(runner, "bhop", "air_strafe"));
    EXPECT(w.indicatorCount(runner, "bhop", "air_accel") == 0, "wall collision read as air acceleration (%d)",
           w.indicatorCount(runner, "bhop", "air_accel"));
}

// S3: the server stalls; everyone's positions arrive in bursts at once.
static void testServerStall() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    std::vector<std::unique_ptr<Sim>> runners;
    for (int i = 0; i < 4; ++i) {
        runners.push_back(std::make_unique<Sim>(10 + i, "Runner" + std::to_string(i)));
        runners.back()->f.pos = {i * 20.0, 64, 0};
        runners.back()->cadence = 2;
    }
    w.add(local);
    for (auto& r : runners) w.add(*r);
    for (int i = 0; i < 20 * 60; ++i) {
        const bool stall = (i % 90) >= 82;
        for (auto& r : runners) {
            r->freeze = stall;
            r->moveYaw += 1.5;
            r->step(1.0, true);
        }
        w.frame();
    }
    int speed = 0;
    for (auto& r : runners) speed += w.evidence(*r, "speed") + w.evidence(*r, "bhop");
    EXPECT(speed == 0, "server stalls produced movement evidence=%d", speed);
    EXPECT(w.engine.stats().stalls >= 5, "stalls detected=%d", w.engine.stats().stalls);
}

// N1: a lag catch-up while eating is not "moving fast while using an item".
static void testNoSlowLagCatchUp() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim eater(2, "LaggyEater");
    eater.cadence = 2;
    w.add(local);
    w.add(eater);
    for (int i = 0; i < 20 * 60; ++i) {
        const bool eating = (i % 60) > 10;
        eater.f.heldItem = 322;
        eater.f.usingItem = eating;
        eater.freeze = (i % 60) >= 30 && (i % 60) < 42;
        eater.step(eating ? 0.2 : 1.0, false, !eating);
        w.frame();
    }
    EXPECT(w.evidence(eater, "noslow") == 0, "noslow evidence from lag while eating=%d", w.evidence(eater, "noslow"));
}

// Phases: queue, match, post-game, new world.
static void testMatchTracker() {
    MatchTracker m;
    std::vector<std::string> none;
    m.onFrame(GameContext::Lobby, none, 0);
    EXPECT(m.phase() == GamePhase::Lobby, "lobby");
    m.onWorldChanged();
    auto u = m.onFrame(GameContext::Pregame, none, 100);
    EXPECT(m.phase() == GamePhase::Queue && u.sceneChanged, "queue in a new world");
    const int queueScene = m.scene();
    u = m.onFrame(GameContext::Unknown, none, 150);
    EXPECT(m.phase() == GamePhase::Queue && !u.sceneChanged, "scoreboard flicker holds the phase");
    u = m.onFrame(GameContext::InGame, none, 200);
    EXPECT(m.phase() == GamePhase::Match && u.matchStarted && m.scene() == queueScene + 1, "queue → match starts a match");
    EXPECT(m.queueScene() == queueScene, "the queue belongs to this match");
    u = m.onFrame(GameContext::InGame, {"[MVP+] Someone: winner: me lol"}, 300);
    EXPECT(m.phase() == GamePhase::Match && !u.matchEnded, "player chat does not end the match");
    u = m.onFrame(GameContext::InGame, {"                  1st Killer - Kestrel - 7"}, 400);
    EXPECT(m.phase() == GamePhase::PostGame && u.matchEnded, "end announcement → post-game");
    u = m.onFrame(GameContext::InGame, none, 500);
    EXPECT(m.phase() == GamePhase::PostGame && !u.matchStarted, "post-game holds");
    m.onWorldChanged();
    u = m.onFrame(GameContext::Lobby, none, 600);
    EXPECT(m.phase() == GamePhase::Lobby && u.sceneChanged && !u.matchEnded, "new world after post-game: one end only");
    m.onWorldChanged();
    u = m.onFrame(GameContext::InGame, none, 700);
    EXPECT(u.matchStarted && m.queueScene() == -1, "rejoin straight into a game starts a match without a queue");
    EXPECT(matchesGameEnd("Reward Summary") && !matchesGameEnd("Kestrel: reward summary pls"), "end trigger matching");
}

static void testPostGameIsQuiet() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim cheat(2, "Celebrating");
    w.add(local);
    w.add(cheat);
    for (int i = 0; i < 20 * 20; ++i) { lowHopStep(cheat); w.frame(); }
    const int before = w.evidence(cheat, "bhop") + w.evidence(cheat, "speed");
    EXPECT(before > 0, "sanity: the low hop produces evidence during the match (%d)", before);
    w.chat.push_back("                  1st Killer - Celebrating - 9");
    for (int i = 0; i < 20 * 20; ++i) { lowHopStep(cheat); w.frame(); }
    const int after = w.evidence(cheat, "bhop") + w.evidence(cheat, "speed");
    EXPECT(w.engine.phase() == GamePhase::PostGame, "phase after the end announcement = %s", gamePhaseName(w.engine.phase()));
    EXPECT(after == before, "post-game movement was judged (%d → %d)", before, after);
}

// Confidence holds while a player is out of view and resumes when they return.
static void testConfidenceHoldsWhileAway() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim cheat(2, "Wanderer");
    w.add(local);
    w.add(cheat);
    for (int i = 0; i < 20 * 15; ++i) { lowHopStep(cheat); w.frame(); }
    w.hidden.push_back(&cheat);
    for (int i = 0; i < 20 * 3; ++i) w.frame();    // leaves view (dropped after 2 s)
    const float left = suspicionOf(w, cheat);
    for (int i = 0; i < 20 * 120; ++i) w.frame();  // two minutes away
    w.hidden.clear();
    cheat.f.pos = {0, 64, 0};
    w.frame();
    const float back = suspicionOf(w, cheat);
    EXPECT(left > 30.f, "sanity: suspicion before leaving %.1f", left);
    EXPECT(std::fabs(back - left) < 1.5f, "suspicion collapsed or grew while away (%.1f → %.1f)", left, back);
}

// A new game halves carried confidence and empties the evidence gate.
static void testCarryOverToNextGame() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim cheat(2, "Returning");
    w.add(local);
    w.add(cheat);
    for (int i = 0; i < 20 * 15; ++i) { lowHopStep(cheat); w.frame(); }
    const float first = suspicionOf(w, cheat);
    w.worldChanged = true;
    cheat.f.pos = {0, 64, 0};
    cheat.motion = {};
    cheat.f.onGround = true;
    w.frame();
    const PlayerRecord* r = w.rec(cheat);
    const float carried = suspicionOf(w, cheat);
    EXPECT(r && w.engine.inThisGame(*r), "the player is in this game after reappearing");
    bool noted = false;
    if (r) for (const auto& e : r->timeline) if (e.kind == TimelineKind::Carried) noted = true;
    EXPECT(noted, "carry-over is noted on the timeline");
    // Two models combine: halving each lowers the combined value by at least a third.
    EXPECT(first > 30.f && carried < first * 0.8f && carried > 0.f, "carried suspicion %.1f from %.1f", carried, first);
}

// In the queue, players are recorded (they are your opponents) but not judged.
static void testQueueRecordsWithoutJudging() {
    World w;
    w.context = GameContext::Pregame;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim cheat(2, "Waiting");
    w.add(local);
    w.add(cheat);
    for (int i = 0; i < 20 * 20; ++i) { lowHopStep(cheat); w.frame(); }
    const PlayerRecord* r = w.rec(cheat);
    EXPECT(r != nullptr && r->participant, "queue players are recorded as participants");
    EXPECT(w.evidence(cheat, "bhop") + w.evidence(cheat, "speed") == 0, "queue players are not judged by default");
}

// NPCs, bots and duplicates never reach records, checks or the roster.
static void testEntityClassification() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim npc(2, "Shopkeeper");
    npc.f.uuid = "3c4d5e6f-1a2b-2c3d-8e9f-0a1b2c3d4e5f";   // version 2
    Sim prop(3, "Quest_Giver");
    prop.f.tabKnown = true;
    prop.f.tabListed = false;                                // unlisted and never moves
    Sim mirror(4, "Observer");                               // the "your profile" NPC
    mirror.f.uuid = "00000000-0000-4000-8000-999999999999";
    Sim bot(5, "Wd_Bot");
    bot.f.tabKnown = true;
    bot.f.invisible = true;                                  // hidden and unlisted
    Sim real(6, "Listed");
    real.f.tabKnown = real.f.tabListed = true;
    Sim nick(7, "Nicked");
    nick.f.uuid = "0b4f2ba3-6c5d-1e8f-9a0b-1c2d3e4f5a6b";   // version 1 (Hypixel nick)
    nick.f.tabKnown = nick.f.tabListed = true;
    Sim walker(8, "NotInTab");
    walker.f.tabKnown = true;                                // unlisted but visibly playing
    Sim badName(9, "[NPC] Bob");
    for (Sim* s : {&npc, &prop, &mirror, &bot, &real, &nick, &walker, &badName}) s->f.pos = {s->f.entityId * 6.0, 64, 30};
    w.add(local);
    for (Sim* s : {&npc, &prop, &mirror, &bot, &real, &nick, &walker, &badName}) w.add(*s);
    for (int i = 0; i < 120; ++i) {
        walker.moveYaw += 3.0;
        walker.step(1.0, false);
        bot.moveYaw -= 7.0;
        bot.step(1.0, true);
        w.frame();
    }
    EXPECT(!w.rec(npc), "v2 NPC was recorded");
    EXPECT(!w.rec(prop), "unlisted prop was recorded");
    EXPECT(!w.rec(mirror), "an entity using your name was recorded");
    EXPECT(!w.rec(bot), "hidden unlisted bot was recorded");
    EXPECT(!w.rec(badName), "an invalid player name was recorded");
    EXPECT(w.rec(real) != nullptr, "listed player missing");
    EXPECT(w.rec(nick) != nullptr, "nicked (v1) player missing");
    EXPECT(w.rec(walker) != nullptr, "unlisted but active player missing after the grace period");
    EXPECT(w.engine.filtered().size() == 5, "filtered entities = %zu", w.engine.filtered().size());
}

static void testDuplicates() {
    World w;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim a(2, "Twin"), b(3, "Twin");
    b.f.uuid = a.f.uuid;              // the same player on two entities (respawn overlap, replay copy)
    b.f.pos = {0.5, 64, 0.5};
    Sim real(4, "Kestrel"), imposter(5, "Kestrel");
    real.f.tabKnown = real.f.tabListed = true;
    imposter.f.uuid = "00000000-0000-4000-8000-777777777777";
    imposter.f.tabKnown = true;       // unlisted, wearing a listed player's name
    real.f.pos = {20, 64, 0};
    imposter.f.pos = {24, 64, 0};
    w.add(local);
    w.add(a);
    w.add(b);
    w.add(real);
    w.add(imposter);
    const int frames = 200;
    for (int i = 0; i < frames; ++i) {
        a.moveYaw += 2.0;
        a.step(1.0, false);
        real.step(0.3, false);
        w.frame();
    }
    const PlayerRecord* r = w.rec(a);
    EXPECT(r && std::abs(r->stats.ticksObserved - frames) <= 2, "duplicate entities double-counted (%d ticks for %d frames)",
           r ? r->stats.ticksObserved : -1, frames);
    EXPECT(w.rec(real) != nullptr && !w.rec(imposter), "name collision: the listed player is kept, the unlisted twin is not");
}

static void testMarks() {
    char tmp[512];
    std::snprintf(tmp, sizeof(tmp), "%s\\vigil_test_marks.json", std::getenv("TEMP") ? std::getenv("TEMP") : ".");
    std::remove(tmp);
    {
        Marks m;
        m.setPath(tmp);
        EXPECT(!m.watched("k1") && !m.confirmed("k1"), "empty store");
        m.setWatch("k1", "Kestrel", true);
        m.confirm("k2", "Vale", {"KillAura", "Reach"});
        EXPECT(m.watched("k1") && !m.confirmed("k1"), "watch");
        EXPECT(m.confirmed("k2") && m.watched("k2"), "confirming also watches");
        m.unconfirm("k2");
        EXPECT(!m.confirmed("k2") && m.watched("k2"), "removing a confirmation keeps the player watched");
        m.confirm("k2", "Vale", {"KillAura"});
    }
    Marks back;
    back.setPath(tmp);
    const Mark* k2 = back.find("k2");
    EXPECT(back.watched("k1") && k2 && k2->confirmed && k2->name == "Vale" && k2->reasons.size() == 1, "marks persist");
    EXPECT(back.all().front().key == "k2", "confirmed players list first");
    back.setWatch("k1", "Kestrel", false);
    EXPECT(!back.find("k1"), "unwatching an unconfirmed player removes the mark");
    std::remove(tmp);
}

int main(int argc, char** argv) {
    const Test tests[] = {
        {"physics baseline", testPhysicsBaseline},
        {"json & settings", testJsonAndSettings},
        {"confidence model", testConfidenceModel},
        {"presets keep defaults", testPresetsKeepDefaults},
        {"legit movement is quiet", testLegitMovementIsQuiet},
        {"speed potion legit", testSpeedPotionLegit},
        {"stale potion uses movement attribute", testStalePotionUsesAttribute},
        {"head-hitter jumping is quiet", testHeadHitterIsQuiet},
        {"cheat bhop / lowhop", testCheatBhop},
        {"air strafe", testAirStrafe},
        {"old KillAura bug regression", testOldKillAuraBugRegression},
        {"legit duel is quiet", testKillAuraLegitDuelIsQuiet},
        {"killaura outside view", testKillAuraOutsideView},
        {"killaura multi-target", testKillAuraMultiTarget},
        {"reach", testReach},
        {"velocity", testVelocity},
        {"autoclicker", testAutoClicker},
        {"noslow", testNoSlow},
        {"velocity waits for the victim's latency", testVelocityLatency},
        {"speed ignores lag catch-ups", testSpeedLagCatchUp},
        {"speed effect that just ended", testExpiringSpeedEffect},
        {"wall collision is not air strafe", testWallCollisionIsNotStrafe},
        {"server stall", testServerStall},
        {"noslow ignores lag catch-ups", testNoSlowLagCatchUp},
        {"match tracker phases", testMatchTracker},
        {"post-game is not judged", testPostGameIsQuiet},
        {"confidence holds while away", testConfidenceHoldsWhileAway},
        {"carry-over to the next game", testCarryOverToNextGame},
        {"queue records without judging", testQueueRecordsWithoutJudging},
        {"NPC and bot classification", testEntityClassification},
        {"duplicates and name collisions", testDuplicates},
        {"marks", testMarks},
    };
    return runSuite(argc, argv, tests, "detection");
}
