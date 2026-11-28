//! tests/scenarios.h: deterministic sessions recorded as frame sequences.

use vigil_detection::types::{Vec3, WorldFrame};

use super::rng::{Mt19937, UniformReal};
use super::sim::{PI, Sim, World, setup_local};

/// 30 seconds of a Bed Wars match seen by a stationary observer: a legitimate sprinter (quiet), a
/// low hopper at 0.55 b/tick (Speed and BHop evidence), a walker hidden for 5 s, and a game-end
/// announcement near the end. `tests/traces/golden-arena.vtrace` is this scenario as C++ generates
/// it; the Rust simulator must produce the same frames.
pub fn golden_arena() -> Vec<WorldFrame> {
    let mut w = World::new();
    w.recorded = Some(Vec::new());
    let mut local = Sim::new(1, "Observer");
    setup_local(&mut local);
    let mut runner = Sim::new(2, "Sprinter");
    let mut cheat = Sim::new(3, "LowHopper");
    let mut walker = Sim::new(4, "Walker");
    runner.f.pos = [0.0, 64.0, 0.0];
    cheat.f.pos = [20.0, 64.0, 0.0];
    walker.f.pos = [-20.0, 64.0, 0.0];
    let mut handles = Vec::new();
    for mut s in [local, runner, cheat, walker] {
        s.f.tab_known = true;
        s.f.tab_listed = true;
        s.f.ping_ms = 60;
        handles.push(w.add(s));
    }
    let (runner, cheat, walker) = (handles[1], handles[2], handles[3]);
    let mut rng = Mt19937::new(20_261_126);
    let turn = UniformReal::new(-6.0, 6.0);
    let total = 20 * 30;
    for i in 0..total {
        w[runner].move_yaw += turn.sample(&mut rng);
        w[runner].f.yaw = w[runner].move_yaw as f32;
        w[runner].step(1.0, true);

        let c = &mut w[cheat];
        let rad = c.move_yaw * PI / 180.0;
        let fwd = Vec3::new(-rad.sin(), 0.0, rad.cos());
        if c.f.on_ground {
            c.motion.y = 0.25;
            c.set_pos(c.pos() + Vec3::new(0.0, 0.01, 0.0));
            c.f.on_ground = false;
        }
        c.set_pos(c.pos() + fwd * 0.55);
        c.f.pos[1] += c.motion.y;
        if c.f.pos[1] <= c.ground_y {
            c.f.pos[1] = c.ground_y;
            c.motion.y = 0.0;
            c.f.on_ground = true;
        } else {
            c.motion.y = (c.motion.y - 0.08) * 0.98;
        }
        c.f.sprinting = true;
        c.tick_animations();

        w[walker].move_yaw += 4.0;
        w[walker].step_s(0.6, false, false);
        if i == 300 {
            w.hidden.push(walker);
        }
        if i == 400 {
            w.hidden.clear();
        }
        if i == total - 60 {
            w.chat.push(b"                                  1st Killer - Sprinter - 4".to_vec());
        }
        w.frame();
    }
    w.recorded.take().unwrap_or_default()
}
