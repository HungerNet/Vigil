//! `src/detection/track.{h,cpp}`: per-player history and derived signals.

use std::collections::VecDeque;
use std::sync::OnceLock;

use crate::cpp;
use crate::types::{PlayerFrame, Tick, Vec3, mc, pos, speed_multiplier};

/// A completed (or in-progress) airborne segment between two ground contacts.
#[derive(Clone, Debug, PartialEq)]
pub struct AirSegment {
    pub takeoff_tick: Tick,
    pub land_tick: Tick,
    pub takeoff_y: f64,
    pub land_y: f64,
    pub apex_y: f64,
    pub air_ticks: i32,
    pub horizontal_distance: f64,
    pub start_velocity: Vec3,
    pub end_velocity: Vec3,
    pub max_gap: i32,
    pub ground_ticks_before: i32,
    pub jumped: bool,
    pub clean: bool,
    pub ceiling_at_takeoff: bool,
    pub jump_boost: i32,
    pub speed_amp: i32,
    pub data_gaps: i32,
}

impl Default for AirSegment {
    fn default() -> Self {
        AirSegment {
            takeoff_tick: 0,
            land_tick: 0,
            takeoff_y: 0.0,
            land_y: 0.0,
            apex_y: 0.0,
            air_ticks: 0,
            horizontal_distance: 0.0,
            start_velocity: Vec3::default(),
            end_velocity: Vec3::default(),
            max_gap: 0,
            ground_ticks_before: 0,
            jumped: false,
            clean: true,
            ceiling_at_takeoff: false,
            jump_boost: -1,
            speed_amp: -1,
            data_gaps: 0,
        }
    }
}

pub const CAPACITY: usize = 200;

#[derive(Clone, Debug)]
pub struct PlayerTrack {
    entity_id: i32,
    frames: VecDeque<PlayerFrame>,
    ticks: VecDeque<Tick>,
    update_ticks: VecDeque<Tick>,
    swing_ticks: VecDeque<Tick>,
    air_segments: VecDeque<AirSegment>,
    current_air: AirSegment,
    airborne: bool,
    ground_ticks: i32,
    air_ticks: i32,
    swing_started: bool,
    hurt_started: bool,
    took_off: bool,
    landed: bool,
    teleported: bool,
    position_updated: bool,
    last_swing_tick: Tick,
    last_hurt_tick: Tick,
    last_teleport_tick: Tick,
    first_tick: Tick,
    last_ice_tick: Tick,
    last_slime_tick: Tick,
    last_liquid_tick: Tick,
    last_climb_tick: Tick,
    last_web_tick: Tick,
    last_ride_tick: Tick,
    last_ceiling_tick: Tick,
    last_step_up_tick: Tick,
    block_start_tick: Tick,
    use_start_tick: Tick,
}

impl Default for PlayerTrack {
    fn default() -> Self {
        PlayerTrack {
            entity_id: -1,
            frames: VecDeque::new(),
            ticks: VecDeque::new(),
            update_ticks: VecDeque::new(),
            swing_ticks: VecDeque::new(),
            air_segments: VecDeque::new(),
            current_air: AirSegment::default(),
            airborne: false,
            ground_ticks: 0,
            air_ticks: 0,
            swing_started: false,
            hurt_started: false,
            took_off: false,
            landed: false,
            teleported: false,
            position_updated: false,
            last_swing_tick: -1000,
            last_hurt_tick: -1000,
            last_teleport_tick: -1000,
            first_tick: 0,
            last_ice_tick: -1000,
            last_slime_tick: -1000,
            last_liquid_tick: -1000,
            last_climb_tick: -1000,
            last_web_tick: -1000,
            last_ride_tick: -1000,
            last_ceiling_tick: -1000,
            last_step_up_tick: -1000,
            block_start_tick: -1000,
            use_start_tick: -1000,
        }
    }
}

fn empty_frame() -> &'static PlayerFrame {
    static EMPTY: OnceLock<PlayerFrame> = OnceLock::new();
    EMPTY.get_or_init(PlayerFrame::default)
}

impl PlayerTrack {
    pub fn entity_id(&self) -> i32 {
        self.entity_id
    }
    pub fn uuid(&self) -> &[u8] {
        &self.cur().uuid
    }
    pub fn name(&self) -> &[u8] {
        &self.cur().name
    }
    pub fn size(&self) -> i32 {
        self.frames.len() as i32
    }
    pub fn is_empty(&self) -> bool {
        self.frames.is_empty()
    }
    pub fn has(&self, k: i32) -> bool {
        k < self.size()
    }

    fn at(&self, k: i32) -> usize {
        let k = cpp::clamp(k, 0, self.size() - 1);
        self.frames.len() - 1 - k as usize
    }

    /// `back(k)`: k ticks back (clamped), 0 = current.
    pub fn back(&self, k: i32) -> &PlayerFrame {
        if self.frames.is_empty() {
            return empty_frame();
        }
        &self.frames[self.at(k)]
    }
    pub fn cur(&self) -> &PlayerFrame {
        self.back(0)
    }
    pub fn tick_at(&self, k: i32) -> Tick {
        if self.ticks.is_empty() {
            return 0;
        }
        self.ticks[self.at(k)]
    }
    pub fn update_tick_at(&self, k: i32) -> Tick {
        if self.update_ticks.is_empty() {
            return 0;
        }
        self.update_ticks[self.at(k)]
    }

    pub fn horizontal_speed(&self, window: i32) -> f64 {
        if self.size() < 2 {
            return 0.0;
        }
        let window = cpp::clamp(window, 1, self.size() - 1);
        let ticks = (self.tick_at(0) - self.tick_at(window)) as f64;
        if ticks <= 0.0 {
            return 0.0;
        }
        (pos(self.back(0)) - pos(self.back(window))).horizontal_length() / ticks
    }

    pub fn displacement(&self, window: i32) -> Vec3 {
        if self.size() < 2 {
            return Vec3::default();
        }
        let window = cpp::clamp(window, 1, self.size() - 1);
        pos(self.back(0)) - pos(self.back(window))
    }

    pub fn velocity(&self, k: i32, span: i32) -> Vec3 {
        if self.size() < 2 {
            return Vec3::default();
        }
        let span = cpp::max(1, span);
        let a = cpp::clamp(k, 0, self.size() - 1);
        let b = cpp::clamp(k + span, 0, self.size() - 1);
        let ticks = (self.tick_at(a) - self.tick_at(b)) as f64;
        if ticks <= 0.0 {
            return Vec3::default();
        }
        (pos(self.back(a)) - pos(self.back(b))) * (1.0 / ticks)
    }

    pub fn horizontal_speed_fresh(&self, window: i32) -> f64 {
        if self.size() < 2 {
            return 0.0;
        }
        let window = cpp::clamp(window, 1, self.size() - 1);
        let ue = self.update_tick_at(0);
        let us = self.update_tick_at(window);
        let elapsed = (ue - us) as f64;
        if elapsed <= 0.0 {
            return 0.0;
        }
        (pos(self.back(0)) - pos(self.back(window))).horizontal_length() / elapsed
    }

    pub fn velocity_fresh(&self, k: i32, span: i32) -> Vec3 {
        if self.size() < 2 {
            return Vec3::default();
        }
        let a = cpp::clamp(k, 0, self.size() - 1);
        let mut b = cpp::clamp(k + cpp::max(1, span), 0, self.size() - 1);
        let ua = self.update_tick_at(a);
        while b < self.size() - 1 && b < k + span + 4 && self.update_tick_at(b) == ua {
            b += 1;
        }
        let ub = self.update_tick_at(b);
        if ua <= ub {
            return Vec3::default();
        }
        (pos(self.back(a)) - pos(self.back(b))) * (1.0 / (ua - ub) as f64)
    }

    pub fn max_update_gap(&self, window: i32) -> i32 {
        if self.size() < 2 {
            return 0;
        }
        let window = cpp::clamp(window, 1, self.size() - 1);
        let mut prev = self.update_tick_at(0);
        let mut gap = (self.tick_at(0) - prev) as i32;
        for k in 1..=window {
            let u = self.update_tick_at(k);
            if u != prev {
                gap = cpp::max(gap, (prev - u) as i32);
                prev = u;
            }
        }
        gap
    }

    pub fn data_quality(&self, window: i32) -> f32 {
        let g = self.max_update_gap(window);
        if g <= 3 {
            return 1.0;
        }
        cpp::clamp(1.0f32 - (g - 3) as f32 / 9.0, 0.0, 1.0)
    }

    pub fn max_speed_multiplier(&self, window: i32) -> f64 {
        let mut m = 1.0;
        let n = cpp::min(window, self.size() - 1);
        let mut k = 0;
        while k <= n {
            m = cpp::max(m, speed_multiplier(self.back(k)));
            k += 1;
        }
        m
    }

    pub fn swing_started(&self) -> bool {
        self.swing_started
    }
    pub fn hurt_started(&self) -> bool {
        self.hurt_started
    }
    pub fn took_off(&self) -> bool {
        self.took_off
    }
    pub fn landed(&self) -> bool {
        self.landed
    }
    pub fn teleported(&self) -> bool {
        self.teleported
    }
    pub fn position_updated(&self) -> bool {
        self.position_updated
    }
    pub fn last_swing_tick(&self) -> Tick {
        self.last_swing_tick
    }
    pub fn last_hurt_tick(&self) -> Tick {
        self.last_hurt_tick
    }
    pub fn last_teleport_tick(&self) -> Tick {
        self.last_teleport_tick
    }
    pub fn first_tick(&self) -> Tick {
        self.first_tick
    }
    pub fn last_ice_tick(&self) -> Tick {
        self.last_ice_tick
    }
    pub fn last_slime_tick(&self) -> Tick {
        self.last_slime_tick
    }
    pub fn last_liquid_tick(&self) -> Tick {
        self.last_liquid_tick
    }
    pub fn last_climb_tick(&self) -> Tick {
        self.last_climb_tick
    }
    pub fn last_web_tick(&self) -> Tick {
        self.last_web_tick
    }
    pub fn last_ride_tick(&self) -> Tick {
        self.last_ride_tick
    }
    pub fn last_ceiling_tick(&self) -> Tick {
        self.last_ceiling_tick
    }
    pub fn last_step_up_tick(&self) -> Tick {
        self.last_step_up_tick
    }
    pub fn last_block_start_tick(&self) -> Tick {
        self.block_start_tick
    }
    pub fn last_use_start_tick(&self) -> Tick {
        self.use_start_tick
    }
    pub fn swing_ticks(&self) -> &VecDeque<Tick> {
        &self.swing_ticks
    }
    pub fn air_segments(&self) -> &VecDeque<AirSegment> {
        &self.air_segments
    }
    pub fn airborne(&self) -> bool {
        self.airborne
    }
    pub fn current_air(&self) -> &AirSegment {
        &self.current_air
    }
    pub fn ground_ticks(&self) -> i32 {
        self.ground_ticks
    }
    pub fn air_ticks(&self) -> i32 {
        self.air_ticks
    }

    pub fn push(&mut self, f: &PlayerFrame, tick: Tick, _time_ms: f64) {
        let had_prev = !self.frames.is_empty();
        let prev_tick = self.ticks.back().copied().unwrap_or(tick);
        if self.frames.is_empty() {
            self.first_tick = tick;
        }
        self.entity_id = f.entity_id;

        // `prev` is the previous frame; it stays valid in C++ across the push (deque references
        // survive push_back and the front pop). Here the fields it is read for are copied first.
        let (prev_swing, prev_progress, prev_hurt, prev_pos, prev_blocking, prev_using) = match self.frames.back() {
            Some(p) => (p.swing_in_progress, p.swing_progress, p.hurt_time, pos(p), p.blocking, p.using_item),
            None => (false, 0, 0, Vec3::default(), false, false),
        };

        self.swing_started = false;
        if had_prev {
            let began = f.swing_in_progress && !prev_swing;
            let restarted = f.swing_in_progress && prev_swing && f.swing_progress < prev_progress;
            self.swing_started = began || restarted;
        }
        if self.swing_started {
            self.last_swing_tick = tick;
            self.swing_ticks.push_back(tick);
            while self.swing_ticks.len() > 120 {
                self.swing_ticks.pop_front();
            }
        }

        self.hurt_started = had_prev && f.hurt_time > prev_hurt && f.hurt_time >= mc::HURT_TIME_MAX - 2;
        if self.hurt_started {
            self.last_hurt_tick = tick;
        }

        let fp = pos(f);
        self.position_updated = had_prev
            && ((fp.x - prev_pos.x).abs() > 1e-9
                || (fp.y - prev_pos.y).abs() > 1e-9
                || (fp.z - prev_pos.z).abs() > 1e-9);

        self.teleported = false;
        if had_prev {
            let jump = (fp - prev_pos).length();
            let per_tick = jump / cpp::max::<Tick>(1, tick - prev_tick) as f64;
            if jump > 8.0 || per_tick > 4.0 {
                self.teleported = true;
                self.last_teleport_tick = tick;
            }
        }

        if mc::is_ice(f.block_below) {
            self.last_ice_tick = tick;
        }
        if mc::is_slime(f.block_below) {
            self.last_slime_tick = tick;
        }
        if f.in_water || f.in_lava {
            self.last_liquid_tick = tick;
        }
        if f.on_ladder {
            self.last_climb_tick = tick;
        }
        if f.in_web {
            self.last_web_tick = tick;
        }
        if f.riding {
            self.last_ride_tick = tick;
        }
        if mc::is_solidish(f.block_above_head) {
            self.last_ceiling_tick = tick;
        }
        if f.blocking && (!had_prev || !prev_blocking) {
            self.block_start_tick = tick;
        }
        if f.using_item && (!had_prev || !prev_using) {
            self.use_start_tick = tick;
        }

        let received = if !had_prev || self.position_updated || self.update_ticks.is_empty() {
            tick
        } else {
            *self.update_ticks.back().expect("non-empty")
        };
        // updateAir reads only prev->pos and prev->blockAboveHead.
        let prev = self.frames.back().map(|p| (pos(p), p.block_above_head));
        self.frames.push_back(f.clone());
        self.ticks.push_back(tick);
        self.update_ticks.push_back(received);
        while self.frames.len() > CAPACITY {
            self.frames.pop_front();
            self.ticks.pop_front();
            self.update_ticks.pop_front();
        }

        self.update_air(f, prev, tick);
    }

    /// `prev`: the previous frame's position and block above the head.
    fn update_air(&mut self, f: &PlayerFrame, prev: Option<(Vec3, i32)>, tick: Tick) {
        self.took_off = false;
        self.landed = false;
        let Some((pp, prev_above)) = prev else {
            self.airborne = !f.on_ground;
            return;
        };
        if self.teleported || self.hurt_started || f.riding || f.in_water || f.in_lava || f.on_ladder || f.in_web {
            self.current_air.clean = false;
        }
        let fp = pos(f);

        if self.airborne {
            self.air_ticks += 1;
            self.current_air.air_ticks = (tick - self.current_air.takeoff_tick) as i32;
            self.current_air.apex_y = cpp::max(self.current_air.apex_y, fp.y);
            self.current_air.horizontal_distance += (fp - pp).horizontal_length();
            if !self.position_updated {
                self.current_air.data_gaps += 1;
            }
            let last_update = *self.update_ticks.back().expect("pushed");
            self.current_air.max_gap = cpp::max(self.current_air.max_gap, (tick - last_update) as i32);
            if self.current_air.air_ticks == 5 {
                self.current_air.start_velocity = self.velocity_fresh(0, 4).horizontal();
            }
            if f.potions.jump_boost > self.current_air.jump_boost {
                self.current_air.jump_boost = f.potions.jump_boost;
            }
            if mc::is_solidish(f.block_above_head) && fp.y - self.current_air.takeoff_y < 0.6 {
                self.current_air.ceiling_at_takeoff = true;
            }
            if f.on_ground {
                self.airborne = false;
                self.landed = true;
                self.current_air.land_tick = tick;
                self.current_air.land_y = fp.y;
                self.current_air.end_velocity = if self.current_air.air_ticks >= 9 {
                    self.velocity_fresh(1, 4).horizontal()
                } else {
                    Vec3::default()
                };
                if self.current_air.land_y > self.current_air.takeoff_y + 0.4 {
                    self.last_step_up_tick = tick;
                }
                self.air_segments.push_back(self.current_air.clone());
                while self.air_segments.len() > 24 {
                    self.air_segments.pop_front();
                }
                self.ground_ticks = 1;
                self.air_ticks = 0;
            }
        } else if !f.on_ground {
            self.airborne = true;
            self.took_off = true;
            self.air_ticks = 1;
            self.current_air = AirSegment {
                takeoff_tick: tick - 1,
                takeoff_y: pp.y,
                apex_y: cpp::max(pp.y, fp.y),
                ground_ticks_before: self.ground_ticks,
                jumped: fp.y > pp.y + 0.1,
                ceiling_at_takeoff: mc::is_solidish(prev_above),
                jump_boost: f.potions.jump_boost,
                speed_amp: f.potions.speed,
                clean: !(self.hurt_started || self.teleported),
                ..AirSegment::default()
            };
            self.ground_ticks = 0;
        } else {
            self.ground_ticks += 1;
        }
    }
}
