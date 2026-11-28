//! tests/detection_tests.cpp, mirrored test for test (P3B_PLAN.md §3.1): synthetic traces built
//! from vanilla 1.8.9 movement physics drive the real engine exactly as the game adapter does;
//! legitimate behavior stays quiet, cheating behavior produces detections. Each test carries its
//! C++ name and counts its expectations like `EXPECT`; the scenarios draw the same random numbers
//! (tests/common/rng.rs) and so produce the same frames.
//!
//! `json & settings` mirrors its JSON half; the settings-registry half (step snapping, clamping and
//! persistence of a free-standing setting) is host code (`core/settings`, tests/test-parity.json).

// The expectations are written as the C++ tests write them (`airTicks + 1 >= 11`).
#![allow(clippy::int_plus_one)]

#[macro_use]
mod common;

use common::registry::Registry;
use common::rng::{Discrete, Mt19937, UniformReal};
use common::sim::{PI, Sim, World, low_hop_step, setup_local, v3};
use common::{Expect, TempFile};
use vigil_detection::cpp;
use vigil_detection::engine::Engine;
use vigil_detection::evidence::{ConfidenceModel, ConfidenceParams};
use vigil_detection::marks::Marks;
use vigil_detection::matchtracker::{GamePhase, MatchTracker, game_phase_name, matches_game_end};
use vigil_detection::session::TimelineKind;
use vigil_detection::types::{GameContext, Vec3, mc};
use vigil_formats::json::{self, Value};

fn fwd_of(yaw: f64) -> Vec3 {
    let rad = yaw * PI / 180.0;
    Vec3::new(-rad.sin(), 0.0, rad.cos())
}

fn observer(w: &mut World) -> usize {
    let mut local = Sim::new(1, "Observer");
    setup_local(&mut local);
    w.add(local)
}

/// The low hop written out where the C++ test writes it inline (identical to `lowHopStep`).
fn low_hop_inline(c: &mut Sim) {
    low_hop_step(c);
}

#[test]
fn physics_baseline() {
    let mut x = Expect::new("physics baseline");
    let mut s = Sim::new(1, "Baseline");
    let mut counting = false;
    let mut ticks = 0;
    // Warm up sprinting.
    for _ in 0..40 {
        s.step(1.0, false);
    }
    let start = s.f.pos[2];
    let mut air = 0;
    for _ in 0..200 {
        let was_ground = s.f.on_ground;
        s.step(1.0, true);
        if was_ground && !s.f.on_ground {
            counting = true;
            air = 0;
        }
        if counting && !s.f.on_ground {
            air += 1;
        }
        if counting && s.f.on_ground && !was_ground {
            counting = false;
        }
        ticks += 1;
    }
    let _ = air;
    let avg = (s.f.pos[2] - start) / f64::from(ticks);
    expect!(
        x,
        (avg - mc::SPRINT_JUMP_AVERAGE).abs() < 0.03,
        "sprint-jump average {avg:.4} b/t (expected ~{:.4})",
        mc::SPRINT_JUMP_AVERAGE
    );
    // Air time of a flat jump.
    let mut j = Sim::new(2, "Jump");
    for _ in 0..10 {
        j.step(1.0, false);
    }
    j.step(1.0, true);
    let mut air_ticks = 0;
    while !j.f.on_ground && air_ticks < 40 {
        j.step(1.0, false);
        air_ticks += 1;
    }
    expect!(x, air_ticks + 1 >= 11 && air_ticks + 1 <= 13, "flat jump air ticks = {}", air_ticks + 1);
    x.finish();
}

#[test]
fn json_settings() {
    let mut x = Expect::new("json & settings");
    let mut v = Value::object();
    v.set("name", Value::str(b"Vigil \"quoted\" \xE2\x80\xA2"));
    v.set("n", Value::Number(3.25));
    let mut arr = Value::array();
    arr.push(Value::Bool(true));
    arr.push(Value::Null);
    v.set("arr", arr);
    let text = json::write(&v, 2);
    let back = json::parse(&text);
    expect!(x, back.is_ok(), "parse failed");
    let back = back.unwrap_or_default();
    expect!(x, back.get("name").as_bytes() == v.get("name").as_bytes(), "string roundtrip");
    expect!(x, (back.get("n").as_number() - 3.25).abs() < 1e-9, "number roundtrip");
    expect!(x, back.get("arr").size() == 2 && back.get("arr").at(0).as_bool(), "array roundtrip");
    expect!(x, json::parse(b"{\"a\": [1, 2,}").is_err(), "malformed input rejected");
    // The settings half (Setting::set snapping and clamping, Settings::save / load) is host code.
    x.finish();
}

#[test]
fn confidence_model() {
    let mut x = Expect::new("confidence model");
    let p =
        ConfidenceParams { half_life_sec: 10.0, min_evidence: 3, alert_threshold: 50.0, ..ConfidenceParams::default() };
    let mut m = ConfidenceModel::default();
    m.add(0.0, 1.0, 1.0, 0, &p);
    expect!(x, (m.value() - 25.0).abs() < 0.01, "first evidence {:.2}", m.value());
    m.add(0.0, 1.0, 1.0, 0, &p);
    m.add(0.0, 1.0, 1.0, 0, &p);
    expect!(x, m.value() > 50.0 && m.can_detect(0.0, &p), "after three: {:.2}", m.value());
    m.decay_to(10000.0, p.half_life_sec);
    let after = m.value();
    m.decay_to(20000.0, p.half_life_sec);
    expect!(x, (m.value() - after * 0.5).abs() < 0.05, "half-life decay {after:.2} -> {:.2}", m.value());
    let mut single = ConfidenceModel::default();
    let strong = p;
    single.add(0.0, 4.0, 1.0, 0, &strong);
    expect!(x, !single.can_detect(0.0, &strong), "single strong observation must not detect ({:.1})", single.value());
    x.finish();
}

/// Balanced presets equal the defaults after the registry's snapping (registry stand-in).
#[test]
fn presets_keep_defaults() {
    let mut x = Expect::new("presets keep defaults");
    let e = Engine::new();
    let mut s = Registry::register(e.schema());
    for c in e.checks() {
        expect!(x, s.preset_of(&c.base) == 1, "{} preset should default to Balanced", c.base.id);
        s.apply_preset(&c.base, 1);
        let prefix = c.base.settings_prefix();
        for (i, p) in e.schema().params.iter().enumerate() {
            if !p.key.starts_with(prefix.as_bytes()) {
                continue;
            }
            if c.base.is_preset_controlled(i as i32) {
                expect!(x, s.is_default(i), "{}: Balanced value differs from default", String::from_utf8_lossy(&p.key));
            }
        }
    }
    x.finish();
}

#[test]
fn legit_movement_is_quiet() {
    let mut x = Expect::new("legit movement is quiet");
    let mut w = World::new();
    observer(&mut w);
    let runner = w.add(Sim::new(2, "Sprinter"));
    let mut rng = Mt19937::new(7);
    let turn = UniformReal::new(-6.0, 6.0);
    for _ in 0..20 * 120 {
        // two minutes of sprint-jumping with turns
        w[runner].move_yaw += turn.sample(&mut rng);
        w[runner].f.yaw = w[runner].move_yaw as f32;
        w[runner].step(1.0, true);
        w.frame();
    }
    expect!(
        x,
        w.detections(runner, "bhop") == 0,
        "bhop detections={} evidence={}",
        w.detections(runner, "bhop"),
        w.evidence(runner, "bhop")
    );
    expect!(
        x,
        w.detections(runner, "speed") == 0,
        "speed detections={} evidence={}",
        w.detections(runner, "speed"),
        w.evidence(runner, "speed")
    );
    expect!(x, w.total_detections(runner) == 0, "total detections={}", w.total_detections(runner));
    expect!(x, w.evidence(runner, "bhop") <= 1, "bhop evidence={}", w.evidence(runner, "bhop"));
    x.finish();
}

#[test]
fn speed_potion_legit() {
    let mut x = Expect::new("speed potion legit");
    let mut w = World::new();
    observer(&mut w);
    let mut r = Sim::new(2, "Potion");
    r.f.potions.speed = 1; // Speed II
    let runner = w.add(r);
    for _ in 0..20 * 60 {
        // Speed II scales ground acceleration by 1.4.
        let fwd = fwd_of(w[runner].move_yaw);
        if w[runner].f.on_ground {
            w[runner].motion = w[runner].motion + fwd * (0.13 * 0.4);
        }
        w[runner].step(1.0, true);
        w.frame();
    }
    expect!(x, w.detections(runner, "speed") == 0, "speed detections with Speed II={}", w.detections(runner, "speed"));
    expect!(x, w.detections(runner, "bhop") == 0, "bhop detections with Speed II={}", w.detections(runner, "bhop"));
    x.finish();
}

/// Speed II drunk while already in view: the effect list is stale, the attribute and particles
/// are current.
fn run_stale_speed_potion(attribute: bool, particles: bool, check: &str) -> i32 {
    let mut w = World::new();
    observer(&mut w);
    let mut r = Sim::new(2, "StalePotion");
    r.f.potions.speed = -1; // what the client believes
    r.f.potion_particles = particles;
    if attribute {
        r.f.move_speed_attribute = 0.1 * 1.3 * 1.4; // sprinting with Speed II
        r.f.sprint_modifier = true;
    }
    let runner = w.add(r);
    for _ in 0..20 * 60 {
        let fwd = fwd_of(w[runner].move_yaw);
        if w[runner].f.on_ground {
            w[runner].motion = w[runner].motion + fwd * (0.13 * 0.4);
        }
        w[runner].step(1.0, false);
        w.frame();
    }
    w.evidence(runner, check)
}

#[test]
fn stale_potion_uses_movement_attribute() {
    let mut x = Expect::new("stale potion uses movement attribute");
    expect!(
        x,
        run_stale_speed_potion(false, false, "speed") > 0,
        "sanity: Speed II movement with no modifier data should exceed the unmodified limit"
    );
    expect!(x, run_stale_speed_potion(true, false, "speed") == 0, "speed evidence with the movement attribute present");
    expect!(
        x,
        run_stale_speed_potion(false, true, "speed") == 0,
        "speed evidence with potion particles but a stale effect list"
    );
    x.finish();
}

/// Sprint-jumping under a two-block ceiling.
fn run_head_hitter(ceiling_visible: bool) -> (i32, i32) {
    let mut w = World::new();
    observer(&mut w);
    let mut r = Sim::new(2, "HeadHitter");
    r.ceiling_y = r.ground_y + 2.0;
    r.f.block_above_head = if ceiling_visible { 1 } else { 0 };
    let runner = w.add(r);
    for _ in 0..20 * 60 {
        w[runner].step(1.0, true);
        w.frame();
    }
    (w.evidence(runner, "speed"), w.evidence(runner, "bhop"))
}

#[test]
fn head_hitter_jumping_is_quiet() {
    let mut x = Expect::new("head-hitter jumping is quiet");
    let (speed, _) = run_head_hitter(false);
    expect!(
        x,
        speed > 0,
        "sanity: head-hitter speed without a visible ceiling should look like speed (evidence={speed})"
    );
    let (speed, bhop) = run_head_hitter(true);
    expect!(x, speed == 0, "speed evidence under a ceiling={speed}");
    expect!(x, bhop == 0, "bhop evidence under a ceiling={bhop}");
    x.finish();
}

#[test]
fn cheat_bhop_lowhop() {
    let mut x = Expect::new("cheat bhop / lowhop");
    let mut w = World::new();
    observer(&mut w);
    let cheat = w.add(Sim::new(2, "LowHopper"));
    // Low hops (motionY 0.25) with speed held at 0.55 b/tick.
    for _ in 0..20 * 40 {
        low_hop_inline(&mut w[cheat]);
        w.frame();
    }
    expect!(
        x,
        w.detections(cheat, "bhop") >= 1,
        "bhop detections={} evidence={} short={} chain={}",
        w.detections(cheat, "bhop"),
        w.evidence(cheat, "bhop"),
        w.indicator_count(cheat, "bhop", "short_air"),
        w.indicator_count(cheat, "bhop", "hop_speed")
    );
    expect!(
        x,
        w.detections(cheat, "speed") >= 1,
        "speed detections={} evidence={}",
        w.detections(cheat, "speed"),
        w.evidence(cheat, "speed")
    );
    x.finish();
}

#[test]
fn air_strafe() {
    let mut x = Expect::new("air strafe");
    let mut w = World::new();
    observer(&mut w);
    let cheat = w.add(Sim::new(2, "Strafer"));
    for _ in 0..40 {
        w[cheat].step(1.0, false);
        w.frame();
    }
    for _ in 0..12 {
        w[cheat].step(1.0, true); // takeoff
        w.frame();
        // In the air, rotate velocity 90° over the jump while keeping its magnitude.
        while !w[cheat].f.on_ground {
            let c = &mut w[cheat];
            let speed = cpp::max(0.30, c.motion.horizontal_length());
            c.move_yaw += 9.0;
            let rad = c.move_yaw * PI / 180.0;
            c.motion.x = -rad.sin() * speed;
            c.motion.z = rad.cos() * speed;
            c.step(0.0, false);
            w.frame();
        }
        for _ in 0..2 {
            w[cheat].step(1.0, false);
            w.frame();
        }
    }
    expect!(
        x,
        w.indicator_count(cheat, "bhop", "air_strafe") >= 3,
        "air strafe evidence={}",
        w.indicator_count(cheat, "bhop", "air_strafe")
    );
    expect!(x, w.detections(cheat, "bhop") >= 1, "bhop detections={}", w.detections(cheat, "bhop"));
    x.finish();
}

/// Legit duel: the attacker faces the victim and hits every 12 ticks.
#[allow(clippy::too_many_arguments)]
fn run_duel(
    w: &mut World,
    attacker: usize,
    victim: usize,
    distance: f64,
    hits: i32,
    aim_away: bool,
    victim_anti_kb: bool,
    legit_kb: bool,
) {
    for _ in 0..hits {
        for t in 0..12 {
            // Keep distance: the victim stands at `distance` blocks along +Z.
            if t == 0 {
                let a = w[attacker].pos();
                let gy = w[victim].ground_y;
                w[victim].set_pos(v3(a.x, gy, a.z + distance));
                w[victim].f.on_ground = true;
                w[victim].motion = Vec3::default();
            }
            w.look_at(attacker, victim);
            if aim_away {
                w[attacker].f.yaw += 180.0f32;
            }
            if t == 0 {
                w[attacker].swing();
            }
            if t == 1 {
                let v = &mut w[victim];
                v.hurt();
                if !victim_anti_kb && legit_kb {
                    v.motion.y = 0.3608;
                    v.motion.z += 0.4;
                    v.f.on_ground = false;
                    v.f.pos[1] += 0.0001;
                }
            }
            w[attacker].tick_animations();
            // Victim physics (no input).
            let v = &mut w[victim];
            if !v.f.on_ground || v.motion.horizontal_length() > 0.0 {
                v.set_pos(v.pos() + Vec3::new(v.motion.x, v.motion.y, v.motion.z));
                v.motion.x *= if v.f.on_ground { 0.546 } else { 0.91 };
                v.motion.z *= if v.f.on_ground { 0.546 } else { 0.91 };
                if v.f.pos[1] <= v.ground_y {
                    v.f.pos[1] = v.ground_y;
                    v.motion.y = 0.0;
                    v.f.on_ground = true;
                } else {
                    v.f.on_ground = false;
                    v.motion.y = (v.motion.y - 0.08) * 0.98;
                }
            }
            if v.f.hurt_time > 0 {
                v.f.hurt_time -= 1;
            }
            // Small idle sway so the victim always has position updates.
            v.f.pos[0] += if t % 2 == 0 { 0.04 } else { -0.04 };
            w.frame();
        }
    }
}

fn duel_world(a_name: &str, v_name: &str, a_at_origin: bool) -> (World, usize, usize) {
    let mut w = World::new();
    observer(&mut w);
    let mut a = Sim::new(2, a_name);
    if a_at_origin {
        a.f.pos = [0.0, 64.0, 0.0];
    }
    let a = w.add(a);
    let v = w.add(Sim::new(3, v_name));
    for _ in 0..30 {
        w.frame();
    }
    (w, a, v)
}

#[test]
fn old_killaura_bug_regression() {
    // A player swinging at the air in front of an observer must produce no KillAura evidence.
    let mut x = Expect::new("old KillAura bug regression");
    let mut w = World::new();
    let mut local = Sim::new(1, "Observer");
    local.f.is_local = true;
    local.f.pos = [0.0, 64.0, 0.0];
    let local = w.add(local);
    let mut s = Sim::new(2, "AirSwinger");
    s.f.pos = [0.0, 64.0, 2.5];
    let swinger = w.add(s);
    for i in 0..400 {
        w.look_at(local, swinger);
        if i % 4 == 0 {
            w[swinger].swing();
        }
        w[swinger].tick_animations();
        w.frame();
    }
    expect!(x, w.evidence(swinger, "killaura") == 0, "killaura evidence={}", w.evidence(swinger, "killaura"));
    x.finish();
}

#[test]
fn legit_duel_is_quiet() {
    let mut x = Expect::new("legit duel is quiet");
    let (mut w, a, v) = duel_world("Fighter", "Target", true);
    run_duel(&mut w, a, v, 2.6, 25, false, false, true);
    expect!(x, w.evidence(a, "killaura") == 0, "killaura evidence={}", w.evidence(a, "killaura"));
    expect!(x, w.evidence(a, "reach") == 0, "reach evidence={}", w.evidence(a, "reach"));
    expect!(x, w.evidence(v, "velocity") == 0, "velocity evidence on legit victim={}", w.evidence(v, "velocity"));
    x.finish();
}

#[test]
fn killaura_outside_view() {
    let mut x = Expect::new("killaura outside view");
    let (mut w, a, v) = duel_world("Aura", "Victim", false);
    run_duel(&mut w, a, v, 2.6, 8, true, false, true);
    expect!(
        x,
        w.indicator_count(a, "killaura", "outside_view") >= 6,
        "outside view evidence={}",
        w.indicator_count(a, "killaura", "outside_view")
    );
    expect!(x, w.detections(a, "killaura") >= 1, "killaura detections={}", w.detections(a, "killaura"));
    x.finish();
}

#[test]
fn killaura_multi_target() {
    let mut x = Expect::new("killaura multi-target");
    let mut w = World::new();
    observer(&mut w);
    let mut a = Sim::new(2, "MultiAura");
    a.f.pos = [0.0, 64.0, 0.0];
    let mut v1 = Sim::new(3, "Left");
    v1.f.pos = [-2.2, 64.0, 0.5];
    let mut v2 = Sim::new(4, "Right");
    v2.f.pos = [2.2, 64.0, 0.5];
    let a = w.add(a);
    let v1 = w.add(v1);
    let v2 = w.add(v2);
    for _ in 0..30 {
        w.frame();
    }
    for _ in 0..8 {
        for t in 0..12 {
            w.look_at(a, if t < 6 { v1 } else { v2 });
            if t == 0 {
                w[a].swing();
            }
            if t == 1 {
                w[v1].hurt();
                w[v2].hurt();
            }
            w[a].tick_animations();
            for v in [v1, v2] {
                if w[v].f.hurt_time > 0 {
                    w[v].f.hurt_time -= 1;
                }
            }
            w[v1].f.pos[2] += if t % 2 != 0 { 0.03 } else { -0.03 };
            w[v2].f.pos[2] += if t % 2 != 0 { -0.03 } else { 0.03 };
            w.frame();
        }
    }
    expect!(
        x,
        w.indicator_count(a, "killaura", "multi_target") >= 5,
        "multi-target evidence={}",
        w.indicator_count(a, "killaura", "multi_target")
    );
    expect!(x, w.detections(a, "killaura") >= 1, "killaura detections={}", w.detections(a, "killaura"));
    x.finish();
}

#[test]
fn reach() {
    let mut x = Expect::new("reach");
    let (mut w, a, v) = duel_world("LongArm", "Victim", false);
    run_duel(&mut w, a, v, 4.3, 10, false, false, false); // feet 4.3 apart → ~3.9 to the hitbox
    expect!(
        x,
        w.indicator_count(a, "reach", "distance") >= 8,
        "reach evidence={}",
        w.indicator_count(a, "reach", "distance")
    );
    expect!(x, w.detections(a, "reach") >= 1, "reach detections={}", w.detections(a, "reach"));
    x.finish();
}

#[test]
fn velocity() {
    let mut x = Expect::new("velocity");
    let (mut w, a, v) = duel_world("Hitter", "NoKb", false);
    run_duel(&mut w, a, v, 2.6, 8, false, true, true);
    expect!(
        x,
        w.indicator_count(v, "velocity", "vertical") >= 5,
        "vertical evidence={}",
        w.indicator_count(v, "velocity", "vertical")
    );
    expect!(x, w.detections(v, "velocity") >= 1, "velocity detections={}", w.detections(v, "velocity"));
    x.finish();
}

#[test]
fn autoclicker() {
    let mut x = Expect::new("autoclicker");
    let mut w = World::new();
    let mut local = Sim::new(1, "Observer");
    setup_local(&mut local);
    let at = |id: i32, name: &str, p: [f64; 3]| {
        let mut s = Sim::new(id, name);
        s.f.pos = p;
        s
    };
    // A legitimate fighter hitting once per hurt cycle (exactly every 12 ticks) is combat rhythm,
    // not clicking: it must never count as autoclicking.
    let comboer = w.add(at(7, "Comboer", [52.0, 64.0, 3.0]));
    w.add(local);
    let bot = w.add(at(2, "Clicker", [0.0, 64.0, 0.0]));
    let human = w.add(at(3, "Human", [50.0, 64.0, 0.0]));
    let floor_setter = w.add(at(4, "Floor", [100.0, 64.0, 0.0]));
    w.add(at(5, "Dummy", [0.0, 64.0, 3.0]));
    w.add(at(6, "Dummy2", [50.0, 64.0, 3.0]));
    let mut rng = Mt19937::new(3);
    let human_intervals = Discrete::new(&[0.0, 0.0, 0.0, 2.0, 5.0, 4.0, 2.0, 1.0]); // 3..7 ticks
    let mut next_human = 4;
    for i in 0..20 * 30 {
        if i % 4 == 0 {
            w[bot].swing();
        }
        if i % 12 == 0 {
            w[comboer].swing();
        }
        next_human -= 1;
        if next_human <= 0 {
            w[human].swing();
            next_human = human_intervals.sample(&mut rng);
            if next_human < 3 {
                next_human = 3;
            }
        }
        if i % 3 == 0 && i < 60 {
            w[floor_setter].swing(); // establishes a relay floor of 3 ticks
        }
        w[bot].f.held_item = 276;
        w[human].f.held_item = 276;
        w.frame();
        for s in [bot, human, floor_setter, comboer] {
            w[s].tick_animations();
        }
    }
    expect!(
        x,
        w.evidence(comboer, "autoclicker") == 0,
        "12-tick combo rhythm produced autoclicker evidence={}",
        w.evidence(comboer, "autoclicker")
    );
    expect!(x, w.evidence(bot, "autoclicker") >= 3, "bot autoclicker evidence={}", w.evidence(bot, "autoclicker"));
    expect!(
        x,
        w.detections(bot, "autoclicker") >= 1,
        "bot autoclicker detections={}",
        w.detections(bot, "autoclicker")
    );
    expect!(
        x,
        w.detections(human, "autoclicker") == 0,
        "human autoclicker detections={} evidence={}",
        w.detections(human, "autoclicker"),
        w.evidence(human, "autoclicker")
    );
    x.finish();
}

#[test]
fn noslow() {
    let mut x = Expect::new("noslow");
    let mut w = World::new();
    observer(&mut w);
    let cheat = w.add(Sim::new(2, "NoSlower"));
    let mut l = Sim::new(3, "Eater");
    l.f.pos = [40.0, 64.0, 0.0];
    let legit = w.add(l);
    for i in 0..20 * 40 {
        let eating = (i % 60) > 10;
        for s in [cheat, legit] {
            w[s].f.held_item = 322;
            w[s].f.using_item = eating;
        }
        w[cheat].step_s(1.0, false, true); // keeps sprinting at full speed
        w[legit].step_s(if eating { 0.2 } else { 1.0 }, false, !eating);
        w.frame();
    }
    expect!(
        x,
        w.detections(cheat, "noslow") >= 1,
        "noslow detections={} evidence={}",
        w.detections(cheat, "noslow"),
        w.evidence(cheat, "noslow")
    );
    expect!(
        x,
        w.detections(legit, "noslow") == 0,
        "legit noslow detections={} evidence={}",
        w.detections(legit, "noslow"),
        w.evidence(legit, "noslow")
    );
    x.finish();
}

/// V1/V2: the victim's knockback reaches the observer only after their latency.
fn run_latency_velocity(relay_delay: i32, ping_ms: i32, anti_kb: bool) -> (i32, i32) {
    let mut w = World::new();
    observer(&mut w);
    let a = w.add(Sim::new(2, "Hitter"));
    let mut vs = Sim::new(3, "HighPing");
    vs.delay = relay_delay;
    vs.f.ping_ms = ping_ms;
    vs.f.pos = [0.0, 64.0, 2.6];
    let v = w.add(vs);
    for _ in 0..30 {
        w.frame();
    }
    for _ in 0..12 {
        for t in 0..30 {
            if t == 0 {
                let p = w[v].pos();
                w[a].set_pos(v3(p.x, 64.0, p.z - 2.6));
            }
            w.look_at(a, v);
            if t == 0 {
                w[a].swing();
            }
            if t == 1 {
                let vv = &mut w[v];
                vv.hurt();
                if !anti_kb {
                    vv.motion.y = 0.3608;
                    vv.motion.z += 0.4;
                    vv.f.on_ground = false;
                    vv.f.pos[1] += 0.0001;
                }
            }
            w[a].tick_animations();
            let vv = &mut w[v];
            if !vv.f.on_ground || vv.motion.horizontal_length() > 0.0 {
                vv.set_pos(vv.pos() + vv.motion);
                vv.motion.x *= if vv.f.on_ground { 0.546 } else { 0.91 };
                vv.motion.z *= if vv.f.on_ground { 0.546 } else { 0.91 };
                if vv.f.pos[1] <= vv.ground_y {
                    vv.f.pos[1] = vv.ground_y;
                    vv.motion.y = 0.0;
                    vv.f.on_ground = true;
                } else {
                    vv.f.on_ground = false;
                    vv.motion.y = (vv.motion.y - 0.08) * 0.98;
                }
            }
            if vv.f.hurt_time > 0 {
                vv.f.hurt_time -= 1;
            }
            vv.f.pos[0] += if t % 2 == 0 { 0.04 } else { -0.04 };
            w.frame();
        }
    }
    (w.evidence(v, "velocity"), w.detections(v, "velocity"))
}

#[test]
fn velocity_waits_for_the_victims_latency() {
    let mut x = Expect::new("velocity waits for the victim's latency");
    expect!(x, run_latency_velocity(7, 250, false).0 == 0, "legit 250 ms victim produced velocity evidence");
    expect!(x, run_latency_velocity(6, -1, false).0 == 0, "legit victim with unknown ping produced velocity evidence");
    let (cheat, det) = run_latency_velocity(7, 250, true);
    expect!(
        x,
        cheat >= 5 && det >= 1,
        "anti-knockback behind 250 ms is still detected (evidence={cheat} detections={det})"
    );
    x.finish();
}

#[test]
fn speed_ignores_lag_catch_ups() {
    let mut x = Expect::new("speed ignores lag catch-ups");
    let mut w = World::new();
    observer(&mut w);
    let mut r = Sim::new(2, "Laggy");
    r.cadence = 2;
    let mut c = Sim::new(3, "LaggyCheat");
    c.f.pos = [60.0, 64.0, 0.0];
    c.cadence = 2;
    let runner = w.add(r);
    let cheat = w.add(c);
    let mut rng = Mt19937::new(11);
    let turn = UniformReal::new(-4.0, 4.0);
    for i in 0..20 * 90 {
        // 10-tick connection hiccups: the catch-up (~3.5 blocks) is too small to be a teleport.
        let lag = (i % 80) >= 70;
        w[runner].freeze = lag;
        w[cheat].freeze = lag;
        w[runner].move_yaw += turn.sample(&mut rng);
        w[runner].step(1.0, true);
        low_hop_step(&mut w[cheat]);
        w.frame();
    }
    expect!(
        x,
        w.evidence(runner, "speed") == 0,
        "legit runner with lag catch-ups: speed evidence={}",
        w.evidence(runner, "speed")
    );
    expect!(
        x,
        w.evidence(runner, "bhop") == 0,
        "legit runner with lag catch-ups: bhop evidence={}",
        w.evidence(runner, "bhop")
    );
    expect!(
        x,
        w.detections(cheat, "speed") >= 1,
        "speed cheat with the same lag is still detected (evidence={})",
        w.evidence(cheat, "speed")
    );
    x.finish();
}

#[test]
fn speed_effect_that_just_ended() {
    let mut x = Expect::new("speed effect that just ended");
    let mut w = World::new();
    observer(&mut w);
    let runner = w.add(Sim::new(2, "Expiring"));
    for i in 0..20 * 90 {
        let potion = (i % 300) < 220;
        let r = &mut w[runner];
        r.f.move_speed_attribute = if potion { 0.1 * 1.3 * 1.4 } else { 0.1 * 1.3 };
        r.f.sprint_modifier = true;
        let fwd = fwd_of(r.move_yaw);
        if potion && r.f.on_ground {
            r.motion = r.motion + fwd * (0.13 * 0.4);
        }
        r.step(1.0, false);
        w.frame();
    }
    expect!(
        x,
        w.evidence(runner, "speed") == 0,
        "speed evidence after Speed II expired={}",
        w.evidence(runner, "speed")
    );
    expect!(x, w.evidence(runner, "bhop") == 0, "bhop evidence after Speed II expired={}", w.evidence(runner, "bhop"));
    x.finish();
}

#[test]
fn wall_collision_is_not_air_strafe() {
    let mut x = Expect::new("wall collision is not air strafe");
    let mut w = World::new();
    observer(&mut w);
    let mut r = Sim::new(2, "WallJumper");
    r.move_yaw = 45.0; // −X, +Z
    let runner = w.add(r);
    for _ in 0..20 * 60 {
        let r = &mut w[runner];
        if r.f.on_ground {
            r.f.pos[0] = -1.2; // back off the wall before each jump
        }
        r.step(1.0, true);
        if r.f.pos[0] < -3.0 {
            r.f.pos[0] = -3.0;
            r.motion.x = 0.0;
        }
        w.frame();
    }
    expect!(
        x,
        w.indicator_count(runner, "bhop", "air_strafe") == 0,
        "wall collision read as air strafe ({})",
        w.indicator_count(runner, "bhop", "air_strafe")
    );
    expect!(
        x,
        w.indicator_count(runner, "bhop", "air_accel") == 0,
        "wall collision read as air acceleration ({})",
        w.indicator_count(runner, "bhop", "air_accel")
    );
    x.finish();
}

#[test]
fn server_stall() {
    let mut x = Expect::new("server stall");
    let mut w = World::new();
    observer(&mut w);
    let mut runners = Vec::new();
    for i in 0..4 {
        let mut r = Sim::new(10 + i, &format!("Runner{i}"));
        r.f.pos = [f64::from(i) * 20.0, 64.0, 0.0];
        r.cadence = 2;
        runners.push(w.add(r));
    }
    for i in 0..20 * 60 {
        let stall = (i % 90) >= 82;
        for &r in &runners {
            w[r].freeze = stall;
            w[r].move_yaw += 1.5;
            w[r].step(1.0, true);
        }
        w.frame();
    }
    let speed: i32 = runners.iter().map(|&r| w.evidence(r, "speed") + w.evidence(r, "bhop")).sum();
    expect!(x, speed == 0, "server stalls produced movement evidence={speed}");
    expect!(x, w.engine.stats().stalls >= 5, "stalls detected={}", w.engine.stats().stalls);
    x.finish();
}

#[test]
fn noslow_ignores_lag_catch_ups() {
    let mut x = Expect::new("noslow ignores lag catch-ups");
    let mut w = World::new();
    observer(&mut w);
    let mut e = Sim::new(2, "LaggyEater");
    e.cadence = 2;
    let eater = w.add(e);
    for i in 0..20 * 60 {
        let eating = (i % 60) > 10;
        let e = &mut w[eater];
        e.f.held_item = 322;
        e.f.using_item = eating;
        e.freeze = (i % 60) >= 30 && (i % 60) < 42;
        e.step_s(if eating { 0.2 } else { 1.0 }, false, !eating);
        w.frame();
    }
    expect!(
        x,
        w.evidence(eater, "noslow") == 0,
        "noslow evidence from lag while eating={}",
        w.evidence(eater, "noslow")
    );
    x.finish();
}

#[test]
fn match_tracker_phases() {
    let mut x = Expect::new("match tracker phases");
    let mut m = MatchTracker::default();
    let none: Vec<Vec<u8>> = Vec::new();
    m.on_frame(GameContext::Lobby, &none, 0.0);
    expect!(x, m.phase() == GamePhase::Lobby, "lobby");
    m.on_world_changed();
    let u = m.on_frame(GameContext::Pregame, &none, 100.0);
    expect!(x, m.phase() == GamePhase::Queue && u.scene_changed, "queue in a new world");
    let queue_scene = m.scene();
    let u = m.on_frame(GameContext::Unknown, &none, 150.0);
    expect!(x, m.phase() == GamePhase::Queue && !u.scene_changed, "scoreboard flicker holds the phase");
    let u = m.on_frame(GameContext::InGame, &none, 200.0);
    expect!(
        x,
        m.phase() == GamePhase::Match && u.match_started && m.scene() == queue_scene + 1,
        "queue → match starts a match"
    );
    expect!(x, m.queue_scene() == queue_scene, "the queue belongs to this match");
    let u = m.on_frame(GameContext::InGame, &[b"[MVP+] Someone: winner: me lol".to_vec()], 300.0);
    expect!(x, m.phase() == GamePhase::Match && !u.match_ended, "player chat does not end the match");
    let u = m.on_frame(GameContext::InGame, &[b"                  1st Killer - Kestrel - 7".to_vec()], 400.0);
    expect!(x, m.phase() == GamePhase::PostGame && u.match_ended, "end announcement → post-game");
    let u = m.on_frame(GameContext::InGame, &none, 500.0);
    expect!(x, m.phase() == GamePhase::PostGame && !u.match_started, "post-game holds");
    m.on_world_changed();
    let u = m.on_frame(GameContext::Lobby, &none, 600.0);
    expect!(
        x,
        m.phase() == GamePhase::Lobby && u.scene_changed && !u.match_ended,
        "new world after post-game: one end only"
    );
    m.on_world_changed();
    let u = m.on_frame(GameContext::InGame, &none, 700.0);
    expect!(x, u.match_started && m.queue_scene() == -1, "rejoin straight into a game starts a match without a queue");
    expect!(
        x,
        matches_game_end(b"Reward Summary") && !matches_game_end(b"Kestrel: reward summary pls"),
        "end trigger matching"
    );
    x.finish();
}

fn suspicion_of(w: &World, s: usize) -> f32 {
    w.rec(s).map_or(-1.0, |r| r.suspicion)
}

#[test]
fn post_game_is_not_judged() {
    let mut x = Expect::new("post-game is not judged");
    let mut w = World::new();
    observer(&mut w);
    let cheat = w.add(Sim::new(2, "Celebrating"));
    for _ in 0..20 * 20 {
        low_hop_step(&mut w[cheat]);
        w.frame();
    }
    let before = w.evidence(cheat, "bhop") + w.evidence(cheat, "speed");
    expect!(x, before > 0, "sanity: the low hop produces evidence during the match ({before})");
    w.chat.push(b"                  1st Killer - Celebrating - 9".to_vec());
    for _ in 0..20 * 20 {
        low_hop_step(&mut w[cheat]);
        w.frame();
    }
    let after = w.evidence(cheat, "bhop") + w.evidence(cheat, "speed");
    expect!(
        x,
        w.engine.phase() == GamePhase::PostGame,
        "phase after the end announcement = {}",
        game_phase_name(w.engine.phase())
    );
    expect!(x, after == before, "post-game movement was judged ({before} → {after})");
    x.finish();
}

/// Confidence holds while a player is out of view and resumes when they return.
#[test]
fn confidence_holds_while_away() {
    let mut x = Expect::new("confidence holds while away");
    let mut w = World::new();
    observer(&mut w);
    let cheat = w.add(Sim::new(2, "Wanderer"));
    for _ in 0..20 * 15 {
        low_hop_step(&mut w[cheat]);
        w.frame();
    }
    w.hidden.push(cheat);
    for _ in 0..20 * 3 {
        w.frame(); // leaves view (dropped after 2 s)
    }
    let left = suspicion_of(&w, cheat);
    for _ in 0..20 * 120 {
        w.frame(); // two minutes away
    }
    w.hidden.clear();
    w[cheat].f.pos = [0.0, 64.0, 0.0];
    w.frame();
    let back = suspicion_of(&w, cheat);
    expect!(x, left > 30.0, "sanity: suspicion before leaving {left:.1}");
    expect!(x, (back - left).abs() < 1.5, "suspicion collapsed or grew while away ({left:.1} → {back:.1})");
    x.finish();
}

/// A new game halves carried confidence and empties the evidence gate.
#[test]
fn carry_over_to_the_next_game() {
    let mut x = Expect::new("carry-over to the next game");
    let mut w = World::new();
    observer(&mut w);
    let cheat = w.add(Sim::new(2, "Returning"));
    for _ in 0..20 * 15 {
        low_hop_step(&mut w[cheat]);
        w.frame();
    }
    let first = suspicion_of(&w, cheat);
    w.world_changed = true;
    w[cheat].f.pos = [0.0, 64.0, 0.0];
    w[cheat].motion = Vec3::default();
    w[cheat].f.on_ground = true;
    w.frame();
    let carried = suspicion_of(&w, cheat);
    let r = w.rec(cheat);
    expect!(x, r.is_some_and(|r| w.engine.in_this_game(r)), "the player is in this game after reappearing");
    let noted = r.is_some_and(|r| r.timeline.iter().any(|e| e.kind == TimelineKind::Carried));
    expect!(x, noted, "carry-over is noted on the timeline");
    // Two models combine: halving each lowers the combined value by at least a third.
    expect!(
        x,
        first > 30.0 && carried < first * 0.8 && carried > 0.0,
        "carried suspicion {carried:.1} from {first:.1}"
    );
    x.finish();
}

/// In the queue, players are recorded (they are your opponents) but not judged.
#[test]
fn queue_records_without_judging() {
    let mut x = Expect::new("queue records without judging");
    let mut w = World::new();
    w.context = GameContext::Pregame;
    observer(&mut w);
    let cheat = w.add(Sim::new(2, "Waiting"));
    for _ in 0..20 * 20 {
        low_hop_step(&mut w[cheat]);
        w.frame();
    }
    expect!(x, w.rec(cheat).is_some_and(|r| r.participant), "queue players are recorded as participants");
    expect!(x, w.evidence(cheat, "bhop") + w.evidence(cheat, "speed") == 0, "queue players are not judged by default");
    x.finish();
}

/// NPCs, bots and duplicates never reach records, checks or the roster.
#[test]
fn npc_and_bot_classification() {
    let mut x = Expect::new("NPC and bot classification");
    let mut w = World::new();
    observer(&mut w);
    let mut npc = Sim::new(2, "Shopkeeper");
    npc.f.uuid = b"3c4d5e6f-1a2b-2c3d-8e9f-0a1b2c3d4e5f".to_vec(); // version 2
    let mut prop = Sim::new(3, "Quest_Giver");
    prop.f.tab_known = true;
    prop.f.tab_listed = false; // unlisted and never moves
    let mut mirror = Sim::new(4, "Observer"); // the "your profile" NPC
    mirror.f.uuid = b"00000000-0000-4000-8000-999999999999".to_vec();
    let mut bot = Sim::new(5, "Wd_Bot");
    bot.f.tab_known = true;
    bot.f.invisible = true; // hidden and unlisted
    let mut real = Sim::new(6, "Listed");
    real.f.tab_known = true;
    real.f.tab_listed = true;
    let mut nick = Sim::new(7, "Nicked");
    nick.f.uuid = b"0b4f2ba3-6c5d-1e8f-9a0b-1c2d3e4f5a6b".to_vec(); // version 1 (Hypixel nick)
    nick.f.tab_known = true;
    nick.f.tab_listed = true;
    let mut walker = Sim::new(8, "NotInTab");
    walker.f.tab_known = true; // unlisted but visibly playing
    let bad_name = Sim::new(9, "[NPC] Bob");
    let mut ids = Vec::new();
    for mut s in [npc, prop, mirror, bot, real, nick, walker, bad_name] {
        s.f.pos = [f64::from(s.f.entity_id) * 6.0, 64.0, 30.0];
        ids.push(w.add(s));
    }
    let [npc, prop, mirror, bot, real, nick, walker, bad_name] = ids[..] else { unreachable!() };
    for _ in 0..120 {
        w[walker].move_yaw += 3.0;
        w[walker].step(1.0, false);
        w[bot].move_yaw -= 7.0;
        w[bot].step(1.0, true);
        w.frame();
    }
    expect!(x, w.rec(npc).is_none(), "v2 NPC was recorded");
    expect!(x, w.rec(prop).is_none(), "unlisted prop was recorded");
    expect!(x, w.rec(mirror).is_none(), "an entity using your name was recorded");
    expect!(x, w.rec(bot).is_none(), "hidden unlisted bot was recorded");
    expect!(x, w.rec(bad_name).is_none(), "an invalid player name was recorded");
    expect!(x, w.rec(real).is_some(), "listed player missing");
    expect!(x, w.rec(nick).is_some(), "nicked (v1) player missing");
    expect!(x, w.rec(walker).is_some(), "unlisted but active player missing after the grace period");
    expect!(x, w.engine.filtered().len() == 5, "filtered entities = {}", w.engine.filtered().len());
    x.finish();
}

#[test]
fn duplicates_and_name_collisions() {
    let mut x = Expect::new("duplicates and name collisions");
    let mut w = World::new();
    observer(&mut w);
    let a = Sim::new(2, "Twin");
    let mut b = Sim::new(3, "Twin");
    b.f.uuid = a.f.uuid.clone(); // the same player on two entities (respawn overlap, replay copy)
    b.f.pos = [0.5, 64.0, 0.5];
    let mut real = Sim::new(4, "Kestrel");
    real.f.tab_known = true;
    real.f.tab_listed = true;
    let mut imposter = Sim::new(5, "Kestrel");
    imposter.f.uuid = b"00000000-0000-4000-8000-777777777777".to_vec();
    imposter.f.tab_known = true; // unlisted, wearing a listed player's name
    real.f.pos = [20.0, 64.0, 0.0];
    imposter.f.pos = [24.0, 64.0, 0.0];
    let a = w.add(a);
    w.add(b);
    let real = w.add(real);
    let imposter = w.add(imposter);
    let frames = 200;
    for _ in 0..frames {
        w[a].move_yaw += 2.0;
        w[a].step(1.0, false);
        w[real].step(0.3, false);
        w.frame();
    }
    let r = w.rec(a);
    expect!(
        x,
        r.is_some_and(|r| (r.stats.ticks_observed - frames).abs() <= 2),
        "duplicate entities double-counted ({} ticks for {frames} frames)",
        r.map_or(-1, |r| r.stats.ticks_observed)
    );
    expect!(
        x,
        w.rec(real).is_some() && w.rec(imposter).is_none(),
        "name collision: the listed player is kept, the unlisted twin is not"
    );
    x.finish();
}

#[test]
fn marks() {
    let mut x = Expect::new("marks");
    let tmp = TempFile::new("vigil_test_marks.json");
    {
        let mut m = Marks::default();
        m.set_path(&tmp.path);
        expect!(x, !m.watched(b"k1") && !m.confirmed(b"k1"), "empty store");
        m.set_watch(b"k1", b"Kestrel", true);
        m.confirm(b"k2", b"Vale", vec![b"KillAura".to_vec(), b"Reach".to_vec()], -1.0);
        expect!(x, m.watched(b"k1") && !m.confirmed(b"k1"), "watch");
        expect!(x, m.confirmed(b"k2") && m.watched(b"k2"), "confirming also watches");
        m.unconfirm(b"k2");
        expect!(x, !m.confirmed(b"k2") && m.watched(b"k2"), "removing a confirmation keeps the player watched");
        m.confirm(b"k2", b"Vale", vec![b"KillAura".to_vec()], -1.0);
    }
    let mut back = Marks::default();
    back.set_path(&tmp.path);
    let k2 = back.find(b"k2");
    expect!(
        x,
        back.watched(b"k1") && k2.is_some_and(|k| k.confirmed && k.name == b"Vale" && k.reasons.len() == 1),
        "marks persist"
    );
    expect!(x, back.all().first().is_some_and(|m| m.key == b"k2"), "confirmed players list first");
    back.set_watch(b"k1", b"Kestrel", false);
    expect!(x, back.find(b"k1").is_none(), "unwatching an unconfirmed player removes the mark");
    x.finish();
}
