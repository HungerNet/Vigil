//! tests/sim.h in Rust: a minimal vanilla 1.8.9 movement model and a `World` that feeds the real
//! engine with `WorldFrame`s exactly as the game adapter does (server relay delay, cadence and
//! freezes included). Expression for expression, so a mirrored test produces the frames its C++
//! original produces — proven by the golden arena (`scenarios.rs`), which must equal the committed
//! `tests/traces/golden-arena.vtrace` frame for frame.
//!
//! The C++ `World` holds pointers to `Sim`s the test mutates between frames; here the world owns
//! its sims and a test addresses them by the index `World::add` returns (`w[runner].step(…)`).

use std::collections::VecDeque;
use std::ops::{Index, IndexMut};

use vigil_detection::engine::Engine;
use vigil_detection::session::PlayerRecord;
use vigil_detection::types::{GameContext, PlayerFrame, Tick, Vec3, WorldFrame, mc, pitch_towards, yaw_towards};

/// `3.14159265358979323846`.
pub const PI: f64 = std::f64::consts::PI;

pub fn v3(x: f64, y: f64, z: f64) -> Vec3 {
    Vec3::new(x, y, z)
}

#[derive(Clone, Debug)]
pub struct Sim {
    pub f: PlayerFrame,
    pub motion: Vec3,
    pub ground_y: f64,
    /// Bottom of a ceiling block (0 = open sky).
    pub ceiling_y: f64,
    /// Movement direction (degrees, MC convention).
    pub move_yaw: f64,
    pub swing_ticks: i32,
    // Server relay.
    pub delay: i32,
    pub cadence: i32,
    pub freeze: bool,
    pub past: VecDeque<(Vec3, bool)>,
    pub shown: (Vec3, bool),
    pub relay_count: i64,
}

impl Sim {
    pub fn new(id: i32, name: &str) -> Sim {
        let ground_y = 64.0;
        let f = PlayerFrame {
            entity_id: id,
            name: name.as_bytes().to_vec(),
            uuid: format!("00000000-0000-4000-8000-{}", 100_000_000_000i64 + i64::from(id)).into_bytes(),
            pos: [0.0, ground_y, 0.0],
            on_ground: true,
            server_position: true,
            potions_known: true,
            block_below: 2,
            block_above_head: 0,
            held_item: 276,
            health: 20.0,
            ..PlayerFrame::default()
        };
        Sim {
            f,
            motion: Vec3::default(),
            ground_y,
            ceiling_y: 0.0,
            move_yaw: 0.0,
            swing_ticks: -1,
            delay: 0,
            cadence: 1,
            freeze: false,
            past: VecDeque::new(),
            shown: (Vec3::default(), true),
            relay_count: 0,
        }
    }

    pub fn pos(&self) -> Vec3 {
        Vec3::new(self.f.pos[0], self.f.pos[1], self.f.pos[2])
    }
    pub fn set_pos(&mut self, p: Vec3) {
        self.f.pos = [p.x, p.y, p.z];
    }

    /// One tick of vanilla physics. `input` = forward input (0..1); `jump` jumps if on ground.
    pub fn step_with(&mut self, input: f64, jump: bool, sprint: bool, air_control: f64) {
        let rad = self.move_yaw * PI / 180.0;
        let fwd = Vec3::new(-rad.sin(), 0.0, rad.cos());
        self.f.sprinting = sprint && input > 0.0;
        if self.f.on_ground {
            if jump {
                self.motion.y = mc::JUMP_VELOCITY;
                if self.f.sprinting {
                    self.motion = self.motion + fwd * 0.2;
                }
            }
            let accel = (if self.f.sprinting { 0.13 } else { 0.1 }) * input;
            self.motion = self.motion + fwd * accel;
            self.set_pos(self.pos() + Vec3::new(self.motion.x, 0.0, self.motion.z));
            self.motion.x *= 0.546;
            self.motion.z *= 0.546;
        } else {
            self.motion = self.motion + fwd * (air_control * input);
            self.set_pos(self.pos() + Vec3::new(self.motion.x, 0.0, self.motion.z));
            self.motion.x *= mc::AIR_FRICTION;
            self.motion.z *= mc::AIR_FRICTION;
        }
        self.f.pos[1] += self.motion.y;
        if self.ceiling_y > 0.0 && self.f.pos[1] + mc::PLAYER_HEIGHT > self.ceiling_y {
            self.f.pos[1] = self.ceiling_y - mc::PLAYER_HEIGHT; // head hits the ceiling
            self.motion.y = 0.0;
        }
        if self.f.pos[1] <= self.ground_y {
            self.f.pos[1] = self.ground_y;
            self.motion.y = 0.0;
            self.f.on_ground = true;
        } else {
            self.f.on_ground = false;
            self.motion.y = (self.motion.y - 0.08) * 0.98;
        }
        self.tick_animations();
    }

    /// `step(input, jump)` with the defaults `sprint = true`, `airControl = kAirControlSprint`.
    pub fn step(&mut self, input: f64, jump: bool) {
        self.step_with(input, jump, true, mc::AIR_CONTROL_SPRINT);
    }
    /// `step(input, jump, sprint)`.
    pub fn step_s(&mut self, input: f64, jump: bool, sprint: bool) {
        self.step_with(input, jump, sprint, mc::AIR_CONTROL_SPRINT);
    }

    pub fn tick_animations(&mut self) {
        if self.swing_ticks >= 0 {
            self.swing_ticks += 1;
            self.f.swing_progress = self.swing_ticks;
            if self.swing_ticks >= mc::SWING_ANIMATION_TICKS {
                self.swing_ticks = -1;
                self.f.swing_in_progress = false;
                self.f.swing_progress = 0;
            }
        }
        if self.f.hurt_time > 0 {
            self.f.hurt_time -= 1;
        }
    }

    pub fn swing(&mut self) {
        self.swing_ticks = 0;
        self.f.swing_in_progress = true;
        self.f.swing_progress = 0;
    }
    pub fn hurt(&mut self) {
        self.f.hurt_time = mc::HURT_TIME_MAX;
    }
    pub fn look_at(&mut self, o: &Sim) {
        let p = self.pos();
        let eye = Vec3::new(p.x, p.y + mc::EYE_HEIGHT, p.z);
        let q = o.pos();
        let c = Vec3::new(q.x, q.y + 0.9, q.z);
        self.f.yaw = yaw_towards(&eye, &c) as f32;
        self.f.pitch = pitch_towards(&eye, &c) as f32;
    }

    fn quantize(p: Vec3) -> [f64; 3] {
        [(p.x * 32.0).round() / 32.0, (p.y * 32.0).round() / 32.0, (p.z * 32.0).round() / 32.0]
    }

    /// The frame as the server relays it: positions at 1/32 block, rotations at 360/256.
    pub fn frame(&self) -> PlayerFrame {
        let mut out = self.f.clone();
        out.pos = Self::quantize(self.pos());
        out.yaw = ((f64::from(self.f.yaw) / mc::ROTATION_QUANTUM).round() * mc::ROTATION_QUANTUM) as f32;
        out.pitch = ((f64::from(self.f.pitch) / mc::ROTATION_QUANTUM).round() * mc::ROTATION_QUANTUM) as f32;
        out
    }

    /// Server relay: positions (and the ground flag that travels with them) reach the observer
    /// `delay` ticks late, every `cadence` ticks, and not at all while `freeze` is set.
    pub fn relay_frame(&mut self) -> PlayerFrame {
        let mut out = self.frame();
        if self.delay == 0 && self.cadence == 1 && !self.freeze && self.past.is_empty() {
            return out;
        }
        self.past.push_back((self.pos(), self.f.on_ground));
        while self.past.len() as i32 > self.delay + 1 {
            self.past.pop_front();
        }
        let first = self.relay_count == 0;
        self.relay_count += 1;
        if first || (!self.freeze && self.relay_count % i64::from(self.cadence) == 0) {
            self.shown = *self.past.front().expect("pushed");
        }
        out.pos = Self::quantize(self.shown.0);
        out.on_ground = self.shown.1;
        out
    }
}

pub struct World {
    pub engine: Engine,
    pub sims: Vec<Sim>,
    pub tick: Tick,
    pub context: GameContext,
    /// Delivered with the next frame.
    pub chat: Vec<Vec<u8>>,
    pub world_changed: bool,
    /// Out of view this frame (indices into `sims`).
    pub hidden: Vec<usize>,
    /// When set, every frame is also appended here.
    pub recorded: Option<Vec<WorldFrame>>,
}

impl Default for World {
    fn default() -> Self {
        Self::new()
    }
}

impl World {
    pub fn new() -> World {
        let tick = 1000;
        let mut engine = Engine::new();
        engine.reset_session(tick as f64 * 50.0);
        World {
            engine,
            sims: Vec::new(),
            tick,
            context: GameContext::InGame,
            chat: Vec::new(),
            world_changed: false,
            hidden: Vec::new(),
            recorded: None,
        }
    }

    /// `add`: returns the sim's handle.
    pub fn add(&mut self, s: Sim) -> usize {
        self.sims.push(s);
        self.sims.len() - 1
    }

    pub fn frame(&mut self) {
        self.tick += 1;
        let mut w = WorldFrame {
            tick: self.tick,
            time_ms: self.tick as f64 * 50.0,
            context: self.context,
            chat: std::mem::take(&mut self.chat),
            world_changed: self.world_changed,
            ..WorldFrame::default()
        };
        self.world_changed = false;
        for i in 0..self.sims.len() {
            if self.hidden.contains(&i) {
                continue;
            }
            w.players.push(self.sims[i].relay_frame());
        }
        if let Some(r) = &mut self.recorded {
            r.push(w.clone());
        }
        super::feed_frame(&vigil_formats::trace::frame_line(&w)); // test parity (common::Expect)
        self.engine.on_frame(&w);
    }

    /// `a.lookAt(b)` for two sims of this world.
    pub fn look_at(&mut self, a: usize, b: usize) {
        let o = self.sims[b].clone();
        self.sims[a].look_at(&o);
    }

    pub fn rec(&self, s: usize) -> Option<&PlayerRecord> {
        let ri = self.engine.session().find(&self.sims[s].f.uuid)?;
        Some(self.engine.session().player(ri))
    }
    fn check_index(&self, check_id: &str) -> Option<usize> {
        self.engine.check_by_id(check_id).map(|c| c.base.index as usize)
    }
    pub fn detections(&self, s: usize, check_id: &str) -> i32 {
        match (self.rec(s), self.check_index(check_id)) {
            (Some(r), Some(c)) => r.checks[c].detections,
            _ => 0,
        }
    }
    pub fn evidence(&self, s: usize, check_id: &str) -> i32 {
        match (self.rec(s), self.check_index(check_id)) {
            (Some(r), Some(c)) => r.checks[c].evidence,
            _ => 0,
        }
    }
    pub fn indicator_count(&self, s: usize, check_id: &str, indicator: &str) -> i32 {
        let (Some(r), Some(c)) = (self.rec(s), self.engine.check_by_id(check_id)) else { return 0 };
        let cr = &r.checks[c.base.index as usize];
        for (i, ind) in c.base.indicators.iter().enumerate() {
            if ind.id == indicator {
                return cr.indicator_counts.get(i).copied().unwrap_or(0);
            }
        }
        0
    }
    pub fn total_detections(&self, s: usize) -> i32 {
        self.rec(s).map_or(0, |r| r.detections.len() as i32)
    }
}

impl Index<usize> for World {
    type Output = Sim;
    fn index(&self, i: usize) -> &Sim {
        &self.sims[i]
    }
}

impl IndexMut<usize> for World {
    fn index_mut(&mut self, i: usize) -> &mut Sim {
        &mut self.sims[i]
    }
}

/// `setupLocal`: places `s` on a far-away observer position so the local player exists.
pub fn setup_local(local: &mut Sim) {
    local.f.is_local = true;
    local.f.pos = [200.0, 64.0, 200.0];
}

/// The low-hop cheat several suites share (motionY 0.25, 0.55 b/tick): `lowHopStep` / `lowHop`.
pub fn low_hop_step(c: &mut Sim) {
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
}
