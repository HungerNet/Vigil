//! Properties of the Rust engine itself (P3A_PLAN.md V9, V10): determinism, independence of two
//! engines, and robustness on hostile input. Parity with the C++ engine is proven separately and
//! exhaustively by `tools/ci/engine_parity.py` (corpus × 416 configurations, generated scenarios).

use std::path::{Path, PathBuf};

use vigil_detection::engine::Engine;
use vigil_detection::parity::{Out, dump_engine};
use vigil_formats::config::{EngineConfig, ParamType};
use vigil_formats::trace::{self, GameContext, PlayerFrame, WorldFrame};

fn tests_dir() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../tests")
}

fn golden() -> Vec<WorldFrame> {
    trace::read(&std::fs::read(tests_dir().join("traces/golden-arena.vtrace")).unwrap()).unwrap().frames
}

fn state(e: &Engine) -> Vec<u8> {
    let mut o = Out::default();
    dump_engine(e, &mut o);
    o.s
}

fn replay(frames: &[WorldFrame], config: Option<&EngineConfig>) -> Engine {
    let mut e = Engine::new();
    if let Some(c) = config {
        assert!(e.set_config(c));
    }
    e.reset_session(frames[0].time_ms);
    for f in frames {
        e.on_frame(f);
    }
    e
}

/// A tiny deterministic generator (xorshift64*), so the tests need no dependency.
struct Rng(u64);
impl Rng {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 >> 12;
        self.0 ^= self.0 << 25;
        self.0 ^= self.0 >> 27;
        self.0.wrapping_mul(0x2545_F491_4F6C_DD1D)
    }
    fn below(&mut self, n: u64) -> u64 {
        self.next() % n.max(1)
    }
    fn unit(&mut self) -> f64 {
        (self.next() >> 11) as f64 / (1u64 << 53) as f64
    }
}

#[test]
fn the_same_frames_give_the_same_state() {
    let frames = golden();
    let a = replay(&frames, None);
    let b = replay(&frames, None);
    assert_eq!(state(&a), state(&b));
    assert!(a.session().total_detections() > 0, "the golden trace has detections");
}

#[test]
fn two_engines_share_nothing() {
    // Two engines with different configurations, fed frame by frame in alternation, end in exactly
    // the states they reach alone.
    let frames = golden();
    let probe = Engine::new();
    let mut strict = EngineConfig::defaults(probe.schema());
    for c in probe.checks() {
        for p in &c.base.presets {
            strict.values[p.param as usize] = p.strict;
        }
    }
    let solo_default = state(&replay(&frames, None));
    let solo_strict = state(&replay(&frames, Some(&strict)));
    assert_ne!(solo_default, solo_strict, "the configurations must matter for this test to mean anything");
    let (mut a, mut b) = (Engine::new(), Engine::new());
    assert!(b.set_config(&strict));
    a.reset_session(frames[0].time_ms);
    b.reset_session(frames[0].time_ms);
    for f in &frames {
        a.on_frame(f);
        b.on_frame(f);
    }
    assert_eq!(state(&a), solo_default);
    assert_eq!(state(&b), solo_strict);
}

#[test]
fn a_configuration_of_another_shape_is_refused() {
    let mut e = Engine::new();
    let wrong = EngineConfig { values: vec![1.0; 3] };
    assert!(!e.set_config(&wrong));
    assert_eq!(e.config(), &EngineConfig::defaults(e.schema()));
}

fn hostile_number(r: &mut Rng) -> f64 {
    match r.below(10) {
        0 => f64::NAN,
        1 => f64::INFINITY,
        2 => f64::NEG_INFINITY,
        3 => 1e300,
        4 => -1e300,
        5 => 0.0,
        6 => -0.0,
        7 => f64::MIN_POSITIVE,
        _ => (r.unit() - 0.5) * 1e6,
    }
}

fn mutate_player(p: &mut PlayerFrame, r: &mut Rng) {
    match r.below(16) {
        0 => p.pos = [hostile_number(r), hostile_number(r), hostile_number(r)],
        1 => p.yaw = hostile_number(r) as f32,
        2 => p.pitch = hostile_number(r) as f32,
        3 => p.entity_id = [i32::MIN, i32::MAX, -1, 0, 7][r.below(5) as usize],
        4 => p.held_item = [i32::MIN, i32::MAX, 276, 1, 261][r.below(5) as usize],
        5 => p.hurt_time = [i32::MIN, i32::MAX, 10, 9][r.below(4) as usize],
        6 => p.swing_progress = [i32::MIN, i32::MAX, 0, 5][r.below(4) as usize],
        7 => p.ping_ms = [i32::MIN, i32::MAX, -1, 5000][r.below(4) as usize],
        8 => p.health = hostile_number(r) as f32,
        9 => p.move_speed_attribute = hostile_number(r),
        10 => p.potions.speed = [i32::MIN, i32::MAX, 1000][r.below(3) as usize],
        11 => p.uuid = (0..r.below(80)).map(|_| r.next() as u8).collect(),
        12 => p.name = (0..r.below(600)).map(|_| r.next() as u8).collect(),
        13 => p.block_above_head = [i32::MIN, i32::MAX, 1][r.below(3) as usize],
        14 => p.wall_mask = r.next() as u8,
        _ => {
            p.on_ground = !p.on_ground;
            p.swing_in_progress = !p.swing_in_progress;
            p.blocking = !p.blocking;
            p.using_item = !p.using_item;
        }
    }
}

#[test]
fn hostile_frames_never_panic() {
    let base = golden();
    let mut r = Rng(0x5EED_0000_2026_1127);
    let mut runs = 0;
    for round in 0..24 {
        let mut frames = base[..200 + (round * 17) % 400].to_vec();
        for f in &mut frames {
            match r.below(40) {
                0 => f.tick = [i64::MIN, i64::MAX, 0, -5][r.below(4) as usize],
                1 => f.time_ms = hostile_number(&mut r),
                2 => f.skipped_ticks = [i32::MIN, i32::MAX, 9][r.below(3) as usize],
                3 => f.world_changed = true,
                4 => f.context = [GameContext::Unknown, GameContext::Lobby, GameContext::Replay][r.below(3) as usize],
                5 => f.chat.push(b"VICTORY!".to_vec()),
                6 => {
                    if let Some(p) = f.players.first().cloned() {
                        f.players.push(p);
                    }
                }
                _ => {}
            }
            for p in &mut f.players {
                if r.below(8) == 0 {
                    mutate_player(p, &mut r);
                }
            }
        }
        let mut e = Engine::new();
        // Configurations outside every declared range, too.
        let mut c = EngineConfig::defaults(e.schema());
        for _ in 0..r.below(40) {
            let i = r.below(c.values.len() as u64) as usize;
            c.values[i] = match e.schema().params[i].ty {
                ParamType::Bool => (r.below(2)) as f64,
                _ => [0.0, -1.0, 1e9, -1e9, 1.0, 2.0, 0.5][r.below(7) as usize],
            };
        }
        assert!(e.set_config(&c));
        e.reset_session(frames[0].time_ms);
        for f in &frames {
            e.on_frame(f);
        }
        let _ = state(&e);
        runs += 1;
    }
    assert_eq!(runs, 24);
}
