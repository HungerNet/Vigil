#pragma once
// Vigil scenario corpus — deterministic frame sequences that together exercise every check, every
// game phase and every engine parameter. Used by VigilParity (tools/parity) to compare the full
// engine state between two builds (P1: before/after the EngineConfig refactor; P3: C++ vs Rust),
// and reusable by any suite that needs realistic multi-player sessions.
//
// Frames depend only on the simulator (tests/sim.h), never on the engine that happens to be
// listening, so the same corpus is produced by any build.

#include "scenarios.h"

#include <functional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace vigil::test {

struct CorpusScenario {
    std::string name;
    std::vector<WorldFrame> frames;
};

namespace corpus {

// One tick of the low-hop cheat (motionY 0.25, 0.55 b/tick), as in the detection suite.
inline void lowHop(Sim& s, double speed = 0.55) {
    const double rad = s.moveYaw * 3.14159265358979323846 / 180.0;
    const Vec3 fwd{-std::sin(rad), 0.0, std::cos(rad)};
    if (s.f.onGround) {
        s.motion.y = 0.25;
        s.f.pos = s.f.pos + Vec3{0, 0.01, 0};
        s.f.onGround = false;
    }
    s.f.pos = s.f.pos + fwd * speed;
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

// One duel pair driven like the detection suite's runDuel: the attacker hits every 12 ticks.
struct Duel {
    Sim* attacker;
    Sim* victim;
    double distance;
    bool aimAway;
    bool victimAntiKb;
    bool legitKb;
    int hurtDelay = 1;   // ticks between the swing and the victim's hurt animation
    void tick(int t) {
        if (t == 0) {
            victim->f.pos = {attacker->f.pos.x, victim->groundY, attacker->f.pos.z + distance};
            victim->f.onGround = true;
            victim->motion = {};
        }
        attacker->lookAt(*victim);
        if (aimAway) attacker->f.yaw += 180.f;
        if (t == 0) attacker->swing();
        if (t == hurtDelay) {
            victim->hurt();
            if (!victimAntiKb && legitKb) {
                victim->motion.y = 0.3608;
                victim->motion.z += 0.4;
                victim->f.onGround = false;
                victim->f.pos.y += 0.0001;
            }
        }
        attacker->tickAnimations();
        if (!victim->f.onGround || victim->motion.horizontalLength() > 0) {
            victim->f.pos = victim->f.pos + Vec3{victim->motion.x, victim->motion.y, victim->motion.z};
            victim->motion.x *= victim->f.onGround ? 0.546 : 0.91;
            victim->motion.z *= victim->f.onGround ? 0.546 : 0.91;
            if (victim->f.pos.y <= victim->groundY) {
                victim->f.pos.y = victim->groundY;
                victim->motion.y = 0;
                victim->f.onGround = true;
            } else {
                victim->f.onGround = false;
                victim->motion.y = (victim->motion.y - 0.08) * 0.98;
            }
        }
        if (victim->f.hurtTime > 0) --victim->f.hurtTime;
        victim->f.pos.x += (t % 2 == 0 ? 0.04 : -0.04);
    }
};

inline void listed(std::initializer_list<Sim*> sims, int ping = -1) {
    for (Sim* s : sims) {
        s->f.tabKnown = s->f.tabListed = true;
        s->f.pingMs = ping;
    }
}

// Phases and roles: the same low-hop cheat seen in a lobby, a queue, a match and a replay, by a
// teammate and by the local player, with ice, a knockback launch, a lag freeze and a teleport.
inline std::vector<WorldFrame> phasesAndRoles() {
    std::vector<WorldFrame> frames;
    World w;
    w.recorded = &frames;
    Sim local(1, "Observer"), cheat(2, "Hopper"), mate(3, "Mate"), runner(4, "Runner");
    local.f.isLocal = true;
    local.f.pos = {0, 64, -30};
    cheat.f.pos = {10, 64, 0};
    mate.f.pos = {-10, 64, 0};
    mate.f.isTeammate = true;
    runner.f.pos = {30, 64, 0};
    listed({&local, &cheat, &mate, &runner}, 80);
    for (Sim* s : {&local, &cheat, &mate, &runner}) w.add(*s);
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> turn(-5.0, 5.0);
    const GameContext order[] = {GameContext::Lobby, GameContext::Pregame, GameContext::InGame, GameContext::Replay};
    int i = 0;
    for (GameContext ctx : order) {
        w.context = ctx;
        if (ctx == GameContext::Replay) w.worldChanged = true;
        for (int k = 0; k < 260; ++k, ++i) {
            lowHop(cheat);
            lowHop(mate, 0.5);
            lowHop(local, 0.5);
            runner.moveYaw += turn(rng);
            runner.f.yaw = static_cast<float>(runner.moveYaw);
            runner.step(1.0, true);
            // Ice for a moment, then a knockback launch, a lag freeze and a teleport.
            cheat.f.blockBelow = (k >= 60 && k < 80) ? 79 : 2;
            if (k == 120) { cheat.hurt(); cheat.f.pos = cheat.f.pos + Vec3{0, 0, 1.6}; }
            cheat.delay = 1;
            cheat.freeze = (k >= 150 && k < 158);
            if (k == 200) cheat.f.pos = cheat.f.pos + Vec3{0, 0, 12.0};
            w.frame();
        }
    }
    return frames;
}

// Duels side by side (far apart): aim away (KillAura), long reach (Reach), no knockback
// (Velocity), a legitimate fight, a hit from the edge of the candidate range, a hurt animation two
// ticks after the swing (attribution window), and a crowded hit with two swinging candidates.
inline std::vector<WorldFrame> duels() {
    std::vector<WorldFrame> frames;
    World w;
    w.recorded = &frames;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim aura(2, "Aura"), v1(3, "AuraVictim"), arm(4, "LongArm"), v2(5, "ArmVictim");
    Sim hitter(6, "Hitter"), nokb(7, "NoKb"), fighter(8, "Fighter"), target(9, "Target");
    Sim edge(10, "EdgeArm"), v3(11, "EdgeVictim"), late(12, "LateHitter"), v4(13, "LateVictim");
    Sim crowdAway(14, "CrowdAura"), crowdOther(15, "CrowdSwinger"), v5(16, "CrowdVictim");
    aura.f.pos = {0, 64, 0};
    arm.f.pos = {100, 64, 0};
    hitter.f.pos = {200, 64, 0};
    fighter.f.pos = {300, 64, 0};
    edge.f.pos = {400, 64, 0};
    late.f.pos = {500, 64, 0};
    crowdAway.f.pos = {600, 64, 0};
    v1.f.pos = {0, 64, 2.6};
    v2.f.pos = {100, 64, 4.3};
    nokb.f.pos = {200, 64, 2.6};
    target.f.pos = {300, 64, 2.6};
    v3.f.pos = {400, 64, 5.8};
    v4.f.pos = {500, 64, 2.6};
    v5.f.pos = {600, 64, 2.6};
    crowdOther.f.pos = {601.5, 64, 4.6};
    for (Sim* s : {&local, &aura, &v1, &arm, &v2, &hitter, &nokb, &fighter, &target, &edge, &v3, &late, &v4, &crowdAway,
                   &crowdOther, &v5}) {
        w.add(*s);
    }
    Duel pairs[] = {{&aura, &v1, 2.6, true, false, true},
                    {&arm, &v2, 4.3, false, false, false},
                    {&hitter, &nokb, 2.6, false, true, true},
                    {&fighter, &target, 2.6, false, false, true},
                    {&edge, &v3, 5.8, false, false, false},
                    {&late, &v4, 2.6, false, false, true, 2},
                    {&crowdAway, &v5, 2.6, true, false, true}};
    for (int i = 0; i < 30; ++i) w.frame();
    for (int h = 0; h < 24; ++h) {
        for (int t = 0; t < 12; ++t) {
            for (Duel& d : pairs) d.tick(t);
            // A second player swings at the crowd victim at the same moment: the hit is ambiguous.
            crowdOther.lookAt(v5);
            if (t == 0) crowdOther.swing();
            crowdOther.tickAnimations();
            w.frame();
        }
    }
    return frames;
}

// KillAura multi-targeting plus snaps: one attacker alternating between two victims.
inline std::vector<WorldFrame> multiTarget() {
    std::vector<WorldFrame> frames;
    World w;
    w.recorded = &frames;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim a(2, "MultiAura"), v1(3, "Left"), v2(4, "Right");
    a.f.pos = {0, 64, 0};
    v1.f.pos = {-2.2, 64, 0.5};
    v2.f.pos = {2.2, 64, 0.5};
    // Robotic tracking: aim locked onto a victim circling the attacker at 5° per tick.
    Sim tracker(5, "Tracker"), circler(6, "Circler");
    tracker.f.pos = {100, 64, 0};
    circler.f.pos = {100, 64, 3};
    // Multi-targeting with a gap: two victims on opposite sides hit three ticks apart.
    Sim splitter(7, "Splitter"), left(8, "FarLeft"), right(9, "FarRight");
    splitter.f.pos = {200, 64, 0};
    left.f.pos = {197.8, 64, 0.5};
    right.f.pos = {202.2, 64, 0.5};
    for (Sim* s : {&local, &a, &v1, &v2, &tracker, &circler, &splitter, &left, &right}) w.add(*s);
    double angle = 0.0;
    for (int i = 0; i < 30; ++i) w.frame();
    for (int h = 0; h < 14; ++h) {
        for (int t = 0; t < 12; ++t) {
            splitter.lookAt(t < 3 ? left : right);
            if (t == 0 || t == 3) splitter.swing();
            if (t == 1) left.hurt();
            if (t == 4) right.hurt();
            splitter.tickAnimations();
            for (Sim* s : {&left, &right}) {
                if (s->f.hurtTime > 0) --s->f.hurtTime;
                s->f.pos.z += (t % 2 ? 0.03 : -0.03);
            }
            angle += 5.0;
            const double rad = angle * 3.14159265358979323846 / 180.0;
            circler.f.pos = {100.0 + 3.0 * std::sin(rad), 64.0, 3.0 * std::cos(rad)};
            tracker.lookAt(circler);
            if (t == 0) tracker.swing();
            if (t == 1) circler.hurt();
            tracker.tickAnimations();
            if (circler.f.hurtTime > 0) --circler.f.hurtTime;
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
    return frames;
}

// Click timing: a machine clicker, a human, a relay-floor setter and a 12-tick comboer.
inline std::vector<WorldFrame> clickers() {
    std::vector<WorldFrame> frames;
    World w;
    w.recorded = &frames;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim bot(2, "Clicker"), human(3, "Human"), floorSetter(4, "Floor"), dummy(5, "Dummy"), dummy2(6, "Dummy2"),
        comboer(7, "Comboer"), fast(8, "FastClicker"), dummy3(9, "Dummy3");
    bot.f.pos = {0, 64, 0};
    human.f.pos = {50, 64, 0};
    floorSetter.f.pos = {100, 64, 0};
    dummy.f.pos = {0, 64, 3};
    dummy2.f.pos = {50, 64, 3};
    comboer.f.pos = {52, 64, 3};
    fast.f.pos = {150, 64, 0};
    dummy3.f.pos = {150, 64, 3};
    for (Sim* s : {&comboer, &local, &bot, &human, &floorSetter, &dummy, &dummy2, &fast, &dummy3}) w.add(*s);
    // A machine clicker with nobody near (only judged when combat-only is off), and one clicking
    // every 7 ticks next to a dummy (outside the default clicking regime of 6).
    Sim lonely(10, "AirClicker"), slow(11, "SlowClicker"), dummy4(12, "Dummy4");
    lonely.f.pos = {250, 64, 0};
    slow.f.pos = {300, 64, 0};
    dummy4.f.pos = {300, 64, 3};
    for (Sim* s : {&lonely, &slow, &dummy4}) w.add(*s);
    std::mt19937 rng(3);
    std::discrete_distribution<int> humanIntervals({0, 0, 0, 2, 5, 4, 2, 1});
    int nextHuman = 4;
    int jitterCount = 0, nextJitter = 0;
    for (int i = 0; i < 20 * 40; ++i) {
        if (i % 4 == 0) bot.swing();
        if (i % 12 == 0) comboer.swing();
        // Nearly constant timing that never runs 20 identical intervals: 18 × 4 ticks, then 5
        // (timing consistency without a streak).
        if (i == nextJitter) {
            fast.swing();
            nextJitter = i + (++jitterCount % 19 == 0 ? 5 : 4);
        }
        if (--nextHuman <= 0) { human.swing(); nextHuman = humanIntervals(rng); if (nextHuman < 3) nextHuman = 3; }
        if (i % 3 == 0 && i < 60) floorSetter.swing();
        if (i % 4 == 0) lonely.swing();
        if (i % 7 == 0) slow.swing();
        w.frame();
        for (Sim* s : {&bot, &human, &floorSetter, &comboer, &fast, &lonely, &slow}) s->tickAnimations();
    }
    return frames;
}

// Movement under item use (NoSlow) and mid-air turns (BHop air strafe / acceleration).
inline std::vector<WorldFrame> movement() {
    std::vector<WorldFrame> frames;
    World w;
    w.recorded = &frames;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim cheat(2, "NoSlower"), legit(3, "Eater"), strafer(4, "Strafer"), glider(5, "Glider"), booster(6, "Booster");
    Sim spam(7, "Spammer"), spamTarget(8, "SpamTarget");
    cheat.f.pos = {0, 64, 0};
    legit.f.pos = {40, 64, 0};
    strafer.f.pos = {80, 64, 0};
    glider.f.pos = {120, 64, 0};
    booster.f.pos = {160, 64, 0};
    spam.f.pos = {-60, 64, 0};
    spamTarget.f.pos = {-60, 64, 3};
    for (Sim* s : {&local, &cheat, &legit, &strafer, &glider, &booster, &spam, &spamTarget}) w.add(*s);
    int strafeState = 0;   // 0 ground run-up, 1 airborne turning
    int groundRun = 0;
    int boostState = 0, boostRun = 0;
    for (int i = 0; i < 20 * 45; ++i) {
        const bool eating = (i % 60) > 10;
        cheat.f.heldItem = legit.f.heldItem = 322;
        cheat.f.usingItem = eating;
        legit.f.usingItem = eating;
        cheat.step(1.0, false, true);
        legit.step(eating ? 0.2 : 1.0, false, !eating);
        // Strafer: run up, jump, rotate velocity while airborne, land, repeat.
        if (strafeState == 0) {
            strafer.step(1.0, groundRun >= 8);
            ++groundRun;
            if (!strafer.f.onGround) { strafeState = 1; groundRun = 0; }
        } else {
            const double speed = std::max(0.30, strafer.motion.horizontalLength());
            strafer.moveYaw += 9.0;
            const double rad = strafer.moveYaw * 3.14159265358979323846 / 180.0;
            strafer.motion.x = -std::sin(rad) * speed;
            strafer.motion.z = std::cos(rad) * speed;
            strafer.step(0.0, false);
            if (strafer.f.onGround) strafeState = 0;
        }
        // Booster: accelerates straight ahead while airborne (BHop air acceleration).
        if (boostState == 0) {
            booster.step(1.0, boostRun >= 8);
            ++boostRun;
            if (!booster.f.onGround) { boostState = 1; boostRun = 0; }
        } else {
            booster.motion.z += 0.06;
            booster.step(0.0, false);
            if (booster.f.onGround) boostState = 0;
        }
        // Glider: ground speed far beyond sprinting, with a short burst every 7.5 s (Speed ground
        // and burst indicators).
        const bool burst = (i % 150) >= 140;
        glider.f.pos = glider.f.pos + Vec3{0.0, 0.0, burst ? 1.6 : ((i / 100) % 2 ? 0.6 : 0.33)};
        glider.f.onGround = true;
        glider.f.sprinting = true;
        // Spammer: a new swing on 20 of every 21 ticks (the animation restarts each tick), next to a
        // target: beyond the rate ceiling.
        spam.f.heldItem = 276;
        spam.f.swingInProgress = true;
        spam.f.swingProgress = 20 - (i % 21);
        w.frame();
    }
    return frames;
}

// Blocking and bridging: AutoBlock, Scaffold (Flat), Scaffold (Keep-Y) and LegitScaff (eagle).
inline std::vector<WorldFrame> blocksAndBridges() {
    std::vector<WorldFrame> frames;
    World w;
    w.recorded = &frames;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim blocker(2, "Blocker"), dummy(3, "BlockDummy"), flat(4, "FlatBridger"), keepy(5, "KeepYBridger"),
        eagle(6, "Eagle"), legitBridger(7, "SlowBridger"), stairs(8, "StairBridger");
    blocker.f.pos = {0, 64, 0};
    dummy.f.pos = {0, 64, 2.5};
    flat.f.pos = {60, 64, 0};
    keepy.f.pos = {120, 64, 0};
    eagle.f.pos = {180, 64, 0};
    legitBridger.f.pos = {240, 64, 0};
    stairs.f.pos = {300, 64, 0};
    for (Sim* s : {&local, &blocker, &dummy, &flat, &keepy, &eagle, &legitBridger, &stairs}) w.add(*s);
    for (Sim* s : {&flat, &keepy, &eagle, &legitBridger, &stairs}) {
        s->f.heldItem = 1;   // stone
        s->f.pitch = 80.f;
    }
    keepy.f.pitch = 70.f;
    stairs.f.pitch = 70.f;
    int keepyAir = 0, stairsAir = 0;
    for (int i = 0; i < 20 * 45; ++i) {
        // AutoBlock: sword raised continuously, swinging every 5 ticks.
        blocker.f.heldItem = 276;
        blocker.f.blocking = blocker.f.usingItem = (i % 100) < 90;
        blocker.lookAt(dummy);
        if (i % 5 == 0) blocker.swing();
        blocker.tickAnimations();
        // Flat scaffold: sprinting forward on the ground while placing every 7 ticks, looking 70°
        // down (inside the default look-down angle of 65°, outside a stricter one).
        if (i % 7 == 0) flat.swing();
        flat.step(1.0, false, true);
        flat.f.pitch = 70.f;
        // Keep-Y on rising ground: each jump lands a quarter block higher than it took off.
        if (stairs.f.onGround) {
            stairsAir = 0;
            stairs.groundY += 0.25;
        } else {
            ++stairsAir;
        }
        if (!stairs.f.onGround && (stairsAir == 1 || stairsAir == 5 || stairsAir == 9)) stairs.swing();
        stairs.step(1.0, true, true);
        stairs.f.pitch = 70.f;
        // Keep-Y: sprint-jumps with three placements per jump, landing level.
        if (keepy.f.onGround) keepyAir = 0; else ++keepyAir;
        if (!keepy.f.onGround && (keepyAir == 1 || keepyAir == 5 || keepyAir == 9)) keepy.swing();
        keepy.step(1.0, true, true);
        keepy.f.pitch = 70.f;
        // Eagle: walking backwards slowly, a one-tick sneak tap exactly every 8 ticks at a placement.
        eagle.moveYaw = 180.0;
        eagle.f.sneaking = (i % 8) == 0;
        if (i % 8 == 0) eagle.swing();
        eagle.step(0.4, false, false);
        eagle.f.pitch = 78.f;
        // Legitimate sneak-bridging: long, irregular sneaks.
        legitBridger.moveYaw = 180.0;
        legitBridger.f.sneaking = (i % 13) < 6;
        if (i % 13 == 0) legitBridger.swing();
        legitBridger.step(0.3, false, false);
        legitBridger.f.pitch = 78.f;
        w.frame();
    }
    return frames;
}

}  // namespace corpus

// Every corpus scenario, in a fixed order.
inline std::vector<CorpusScenario> scenarioCorpus() {
    std::vector<CorpusScenario> out;
    out.push_back({"golden-arena", goldenArena()});
    out.push_back({"phases-and-roles", corpus::phasesAndRoles()});
    out.push_back({"duels", corpus::duels()});
    out.push_back({"multi-target", corpus::multiTarget()});
    out.push_back({"clickers", corpus::clickers()});
    out.push_back({"movement", corpus::movement()});
    out.push_back({"blocks-and-bridges", corpus::blocksAndBridges()});
    return out;
}

}  // namespace vigil::test
