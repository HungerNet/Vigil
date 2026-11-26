#pragma once
// Vigil test scenarios — deterministic sessions recorded as frame sequences. The golden trace in
// tests/traces is generated from goldenArena() (VigilReplay --synthesize) and its expected outcome
// is committed next to it; validation_tests replays the committed file and compares.

#include "sim.h"

#include <random>
#include <vector>

namespace vigil::test {

// 30 seconds of a Bed Wars match seen by a stationary observer:
//   Sprinter   legitimate sprint-jumping with random turns            (must stay quiet)
//   LowHopper  low hops at 0.55 b/tick                                  (Speed and BHop evidence)
//   Walker     walking in circles, then hidden for 5 s, then back      (presence handling)
// plus a game-end announcement in chat near the end (post-game: nothing judged afterwards).
inline std::vector<WorldFrame> goldenArena() {
    std::vector<WorldFrame> frames;
    World w;
    w.recorded = &frames;
    Sim local(1, "Observer");
    setupLocal(local);
    Sim runner(2, "Sprinter");
    Sim cheat(3, "LowHopper");
    Sim walker(4, "Walker");
    runner.f.pos = {0, 64, 0};
    cheat.f.pos = {20, 64, 0};
    walker.f.pos = {-20, 64, 0};
    for (Sim* s : {&local, &runner, &cheat, &walker}) {
        s->f.tabKnown = s->f.tabListed = true;
        s->f.pingMs = 60;
        w.add(*s);
    }
    std::mt19937 rng(20261126);
    std::uniform_real_distribution<double> turn(-6.0, 6.0);
    const int total = 20 * 30;
    for (int i = 0; i < total; ++i) {
        runner.moveYaw += turn(rng);
        runner.f.yaw = static_cast<float>(runner.moveYaw);
        runner.step(1.0, true);

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

        walker.moveYaw += 4.0;
        walker.step(0.6, false, false);
        if (i == 300) w.hidden.push_back(&walker);
        if (i == 400) w.hidden.clear();
        if (i == total - 60) w.chat.push_back("                                  1st Killer - Sprinter - 4");
        w.frame();
    }
    return frames;
}

}  // namespace vigil::test
