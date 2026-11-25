// Vigil detection tests — synthetic traces built from vanilla 1.8.9 movement physics.
//
// Each scenario drives the real Engine with WorldFrames, exactly as the game adapter does, and
// asserts that legitimate behavior stays quiet while cheating behavior produces detections.

#include "../src/core/json.h"
#include "../src/core/settings.h"
#include "../src/detection/engine.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace vigil;
using namespace vigil::detection;

static int g_failures = 0;
static int g_checks = 0;

#define EXPECT(cond, ...)                                                                  \
    do {                                                                                   \
        ++g_checks;                                                                        \
        if (!(cond)) {                                                                     \
            ++g_failures;                                                                  \
            std::printf("    FAIL %s:%d  %s  — ", __FILE__, __LINE__, #cond);              \
            std::printf(__VA_ARGS__);                                                      \
            std::printf("\n");                                                             \
        }                                                                                  \
    } while (0)

// ── Minimal vanilla movement simulator ───────────────────────────────────────────────────────

struct Sim {
    PlayerFrame f;
    Vec3 motion;
    double groundY = 64.0;
    double moveYaw = 0.0;      // movement direction (degrees, MC convention)
    int swingTicks = -1;

    Sim(int id, const std::string& name) {
        f.entityId = id;
        f.name = name;
        f.uuid = "00000000-0000-4000-8000-" + std::to_string(100000000000LL + id);
        f.pos = {0.0, groundY, 0.0};
        f.onGround = true;
        f.serverPosition = true;
        f.potionsKnown = true;
        f.blockBelow = 2;
        f.blockAboveHead = 0;
        f.heldItem = 276;
        f.health = 20.f;
    }

    // One tick of vanilla physics. `input` = forward input (0..1); `jump` jumps if on ground.
    void step(double input, bool jump, bool sprint = true, double airControl = mc::kAirControlSprint) {
        const double rad = moveYaw * 3.14159265358979323846 / 180.0;
        const Vec3 fwd{-std::sin(rad), 0.0, std::cos(rad)};
        f.sprinting = sprint && input > 0.0;
        if (f.onGround) {
            if (jump) {
                motion.y = mc::kJumpVelocity;
                if (f.sprinting) motion = motion + fwd * 0.2;
            }
            const double accel = (f.sprinting ? 0.13 : 0.1) * input;
            motion = motion + fwd * accel;
            f.pos = f.pos + Vec3{motion.x, 0.0, motion.z};
            motion.x *= 0.546;
            motion.z *= 0.546;
        } else {
            motion = motion + fwd * (airControl * input);
            f.pos = f.pos + Vec3{motion.x, 0.0, motion.z};
            motion.x *= mc::kAirFriction;
            motion.z *= mc::kAirFriction;
        }
        f.pos.y += motion.y;
        if (f.pos.y <= groundY) {
            f.pos.y = groundY;
            motion.y = 0.0;
            f.onGround = true;
        } else {
            f.onGround = false;
            motion.y = (motion.y - 0.08) * 0.98;
        }
        tickAnimations();
    }

    void tickAnimations() {
        if (swingTicks >= 0) {
            ++swingTicks;
            f.swingProgress = swingTicks;
            if (swingTicks >= mc::kSwingAnimationTicks) {
                swingTicks = -1;
                f.swingInProgress = false;
                f.swingProgress = 0;
            }
        }
        if (f.hurtTime > 0) --f.hurtTime;
    }

    void swing() {
        swingTicks = 0;
        f.swingInProgress = true;
        f.swingProgress = 0;
    }
    void hurt() { f.hurtTime = mc::kHurtTimeMax; }
    void lookAt(const Sim& o) {
        const Vec3 eye{f.pos.x, f.pos.y + mc::kEyeHeight, f.pos.z};
        const Vec3 c{o.f.pos.x, o.f.pos.y + 0.9, o.f.pos.z};
        f.yaw = static_cast<float>(yawTowards(eye, c));
        f.pitch = static_cast<float>(pitchTowards(eye, c));
    }

    PlayerFrame frame() const {
        PlayerFrame out = f;
        // Server positions are relayed at 1/32 block precision.
        out.pos.x = std::round(f.pos.x * 32.0) / 32.0;
        out.pos.y = std::round(f.pos.y * 32.0) / 32.0;
        out.pos.z = std::round(f.pos.z * 32.0) / 32.0;
        // Rotations are relayed at 360/256 precision.
        out.yaw = static_cast<float>(std::round(f.yaw / mc::kRotationQuantum) * mc::kRotationQuantum);
        out.pitch = static_cast<float>(std::round(f.pitch / mc::kRotationQuantum) * mc::kRotationQuantum);
        return out;
    }
};

struct World {
    Engine engine;
    std::vector<Sim*> sims;
    Tick tick = 1000;

    World() {
        engine.initialize();
        engine.resetSession(tick * 50.0);
    }
    void add(Sim& s) { sims.push_back(&s); }
    void frame() {
        WorldFrame w;
        w.tick = ++tick;
        w.timeMs = tick * 50.0;
        w.context = GameContext::InGame;
        for (Sim* s : sims) w.players.push_back(s->frame());
        engine.onFrame(w);
    }
    const PlayerRecord* rec(const Sim& s) const { return engine.session().find(s.f.uuid); }
    int detections(const Sim& s, const char* checkId) const {
        const PlayerRecord* r = rec(s);
        const Check* c = engine.checkById(checkId);
        if (!r || !c) return 0;
        return r->checks[static_cast<std::size_t>(c->index())].detections;
    }
    int evidence(const Sim& s, const char* checkId) const {
        const PlayerRecord* r = rec(s);
        const Check* c = engine.checkById(checkId);
        if (!r || !c) return 0;
        return r->checks[static_cast<std::size_t>(c->index())].evidence;
    }
    int indicatorCount(const Sim& s, const char* checkId, const char* indicator) const {
        const PlayerRecord* r = rec(s);
        const Check* c = engine.checkById(checkId);
        if (!r || !c) return 0;
        const auto& cr = r->checks[static_cast<std::size_t>(c->index())];
        for (std::size_t i = 0; i < c->indicators().size(); ++i) {
            if (c->indicators()[i].id == indicator) return i < cr.indicatorCounts.size() ? cr.indicatorCounts[i] : 0;
        }
        return 0;
    }
    int totalDetections(const Sim& s) const {
        const PlayerRecord* r = rec(s);
        return r ? static_cast<int>(r->detections.size()) : 0;
    }
};

// Places `s` on a far-away observer position so the local player exists.
static void setupLocal(Sim& local) {
    local.f.isLocal = true;
    local.f.pos = {200.0, 64.0, 200.0};
}

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
    for (const auto& c : e.checks()) {
        EXPECT(c->presetSetting()->i() == 1, "%s preset should default to Balanced", c->id().c_str());
        c->applyPreset(1);
        for (Setting* s : Settings::instance().withPrefix(c->settingsPrefix())) {
            if (c->isPresetControlled(s)) EXPECT(s->isDefault(), "%s: Balanced value differs from default", s->key.c_str());
        }
    }
}

int main() {
    struct Test { const char* name; void (*fn)(); };
    const Test tests[] = {
        {"physics baseline", testPhysicsBaseline},
        {"json & settings", testJsonAndSettings},
        {"confidence model", testConfidenceModel},
        {"presets keep defaults", testPresetsKeepDefaults},
        {"legit movement is quiet", testLegitMovementIsQuiet},
        {"speed potion legit", testSpeedPotionLegit},
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
    };
    for (const Test& t : tests) {
        const int before = g_failures;
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
        std::printf("[ %s ] %s\n", g_failures == before ? " OK " : "FAIL", t.name);
    }
    std::printf("\n%d expectations, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
