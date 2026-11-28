//! `src/detection/types.h`: vectors, vanilla 1.8.9 constants, geometry and movement multipliers.
//! Frames are `vigil_formats::trace` types (the same fields as `PlayerFrame` / `WorldFrame`).

use std::ops::{Add, Mul, Sub};

use crate::cpp;
pub use vigil_formats::trace::{GameContext, PlayerFrame, Potions, WorldFrame};

pub type Tick = i64;

/// `3.14159265358979323846` in the C++ source: the same double.
const PI: f64 = std::f64::consts::PI;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Vec3 {
    pub x: f64,
    pub y: f64,
    pub z: f64,
}

impl Vec3 {
    pub const fn new(x: f64, y: f64, z: f64) -> Vec3 {
        Vec3 { x, y, z }
    }
    pub fn dot(&self, o: &Vec3) -> f64 {
        self.x * o.x + self.y * o.y + self.z * o.z
    }
    pub fn length(&self) -> f64 {
        (self.x * self.x + self.y * self.y + self.z * self.z).sqrt()
    }
    pub fn horizontal_length(&self) -> f64 {
        (self.x * self.x + self.z * self.z).sqrt()
    }
    pub fn horizontal(&self) -> Vec3 {
        Vec3::new(self.x, 0.0, self.z)
    }
}

impl Add for Vec3 {
    type Output = Vec3;
    fn add(self, o: Vec3) -> Vec3 {
        Vec3::new(self.x + o.x, self.y + o.y, self.z + o.z)
    }
}
impl Sub for Vec3 {
    type Output = Vec3;
    fn sub(self, o: Vec3) -> Vec3 {
        Vec3::new(self.x - o.x, self.y - o.y, self.z - o.z)
    }
}
impl Mul<f64> for Vec3 {
    type Output = Vec3;
    fn mul(self, s: f64) -> Vec3 {
        Vec3::new(self.x * s, self.y * s, self.z * s)
    }
}

/// A frame's position.
pub fn pos(f: &PlayerFrame) -> Vec3 {
    Vec3::new(f.pos[0], f.pos[1], f.pos[2])
}

/// Vanilla 1.8.9 physics and protocol facts (`detection::mc`).
pub mod mc {
    pub const TICK_SECONDS: f64 = 0.05;
    pub const EYE_HEIGHT: f64 = 1.62;
    pub const EYE_HEIGHT_SNEAKING: f64 = 1.54;
    pub const PLAYER_HALF_WIDTH: f64 = 0.3;
    pub const PLAYER_HEIGHT: f64 = 1.8;
    pub const HITBOX_BORDER: f64 = 0.1;
    pub const VANILLA_REACH: f64 = 3.0;
    pub const POSITION_QUANTUM: f64 = 1.0 / 32.0;
    pub const ROTATION_QUANTUM: f64 = 360.0 / 256.0;
    pub const AIR_FRICTION: f64 = 0.91;
    pub const AIR_CONTROL_SPRINT: f64 = 0.026;
    pub const SPRINT_GROUND_SPEED: f64 = 0.2806;
    pub const SPRINT_JUMP_AVERAGE: f64 = 0.3563;
    pub const JUMP_VELOCITY: f64 = 0.42;
    pub const FLAT_JUMP_AIR_TICKS: i32 = 12;
    pub const HURT_TIME_MAX: i32 = 10;
    pub const SWING_ANIMATION_TICKS: i32 = 6;

    pub fn is_sword(id: i32) -> bool {
        id == 267 || id == 268 || id == 272 || id == 276 || id == 283
    }
    pub fn is_block_item(id: i32) -> bool {
        (1..=255).contains(&id)
    }
    pub fn is_ranged(id: i32) -> bool {
        id == 261 || id == 346 || id == 332 || id == 344 || id == 368
    }
    pub fn is_consumable_or_usable(id: i32) -> bool {
        matches!(id, 373 | 261 | 322 | 260 | 297 | 364 | 320 | 335 | 282 | 391 | 393 | 366)
    }
    pub fn is_ice(id: i32) -> bool {
        id == 79 || id == 174
    }
    pub fn is_slime(id: i32) -> bool {
        id == 165
    }
    pub fn is_soul_sand(id: i32) -> bool {
        id == 88
    }
    pub fn is_web(id: i32) -> bool {
        id == 30
    }
    pub fn is_climbable(id: i32) -> bool {
        id == 65 || id == 106
    }
    pub fn is_liquid(id: i32) -> bool {
        (8..=11).contains(&id)
    }
    pub fn is_stair_or_slab(id: i32) -> bool {
        matches!(id, 44 | 126 | 182 | 53 | 67 | 108 | 109 | 114 | 128 | 134 | 135 | 136 | 156 | 163 | 164 | 180)
    }
    pub fn is_solidish(id: i32) -> bool {
        id > 0
            && !is_liquid(id)
            && !matches!(
                id,
                30 | 31
                    | 32
                    | 37
                    | 38
                    | 39
                    | 40
                    | 50
                    | 51
                    | 55
                    | 59
                    | 63
                    | 65
                    | 66
                    | 68
                    | 69
                    | 70
                    | 72
                    | 75
                    | 76
                    | 77
                    | 78
                    | 83
                    | 90
                    | 106
                    | 111
                    | 115
                    | 132
                    | 141
                    | 142
                    | 143
                    | 171
                    | 175
            )
    }
}

/// Octant (0..7) a horizontal direction points to, matching `PlayerFrame::wallMask`.
pub fn octant_of(dx: f64, dz: f64) -> i32 {
    let mut yaw = -dx.atan2(dz) * 180.0 / PI;
    yaw = (yaw + 360.0 + 22.5) % 360.0;
    cpp::to_int(yaw / 45.0) & 7
}

pub fn attribute_multiplier(f: &PlayerFrame) -> f64 {
    if f.move_speed_attribute <= 0.0 {
        return 0.0;
    }
    let mut m = f.move_speed_attribute / 0.1;
    if f.sprint_modifier {
        m /= 1.3;
    }
    m
}

pub fn speed_multiplier(f: &PlayerFrame) -> f64 {
    let mut m = 1.0;
    let attr = attribute_multiplier(f);
    if attr > 0.0 {
        m = cpp::fmax(m, attr);
    }
    if !f.potions_known {
        if attr <= 0.0 {
            m = cpp::fmax(m, 1.45);
        }
    } else if f.potions.speed >= 0 {
        m = cpp::fmax(m, 1.0 + 0.2 * f64::from(f.potions.speed + 1));
    } else if f.potion_particles && f.potions.count == 0 && attr <= 0.0 {
        m = cpp::fmax(m, 1.45);
    }
    m
}

pub fn speed_multiplier_source(f: &PlayerFrame) -> &'static str {
    let attr = attribute_multiplier(f);
    if attr > 0.0 && (!f.potions_known || f.potions.speed < 0 || attr >= 1.0 + 0.2 * f64::from(f.potions.speed + 1)) {
        return if attr > 1.001 { "movement attribute" } else { "movement attribute (no modifiers)" };
    }
    if f.potions_known && f.potions.speed >= 0 {
        return "Speed effect";
    }
    if !f.potions_known || (f.potion_particles && f.potions.count == 0) {
        return "conservative (effects unknown)";
    }
    "no speed modifiers"
}

pub fn game_context_name(c: GameContext) -> &'static str {
    match c {
        GameContext::Lobby => "Lobby",
        GameContext::Pregame => "Pre-game",
        GameContext::InGame => "In game",
        GameContext::Replay => "Replay",
        GameContext::Unknown => "Idle",
    }
}

pub fn wrap_degrees(d: f64) -> f64 {
    let mut d = d % 360.0;
    if d >= 180.0 {
        d -= 360.0;
    }
    if d < -180.0 {
        d += 360.0;
    }
    d
}

pub fn look_vector(yaw_deg: f64, pitch_deg: f64) -> Vec3 {
    let yaw = yaw_deg * PI / 180.0;
    let pitch = pitch_deg * PI / 180.0;
    Vec3::new(-yaw.sin() * pitch.cos(), -pitch.sin(), yaw.cos() * pitch.cos())
}

pub fn yaw_towards(from: &Vec3, to: &Vec3) -> f64 {
    let (dx, dz) = (to.x - from.x, to.z - from.z);
    -dx.atan2(dz) * 180.0 / PI
}

pub fn pitch_towards(from: &Vec3, to: &Vec3) -> f64 {
    let (dx, dy, dz) = (to.x - from.x, to.y - from.y, to.z - from.z);
    -dy.atan2((dx * dx + dz * dz).sqrt()) * 180.0 / PI
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct BoxAabb {
    pub min: Vec3,
    pub max: Vec3,
}

impl BoxAabb {
    pub fn player(feet: &Vec3, grow: f64) -> BoxAabb {
        let h = mc::PLAYER_HALF_WIDTH + grow;
        BoxAabb {
            min: Vec3::new(feet.x - h, feet.y - grow, feet.z - h),
            max: Vec3::new(feet.x + h, feet.y + mc::PLAYER_HEIGHT + grow, feet.z + h),
        }
    }
    pub fn center(&self) -> Vec3 {
        Vec3::new((self.min.x + self.max.x) * 0.5, (self.min.y + self.max.y) * 0.5, (self.min.z + self.max.z) * 0.5)
    }
    pub fn distance_to(&self, p: &Vec3) -> f64 {
        let dx = cpp::fmax(cpp::fmax(self.min.x - p.x, 0.0), p.x - self.max.x);
        let dy = cpp::fmax(cpp::fmax(self.min.y - p.y, 0.0), p.y - self.max.y);
        let dz = cpp::fmax(cpp::fmax(self.min.z - p.z, 0.0), p.z - self.max.z);
        (dx * dx + dy * dy + dz * dz).sqrt()
    }
}

/// Angular distance (degrees) from a view direction to the nearest part of a box.
// The C++ source writes √2 as 1.41421356; the port keeps that exact value.
#[allow(clippy::approx_constant)]
pub fn angle_outside_box(eye: &Vec3, yaw_deg: f64, pitch_deg: f64, b: &BoxAabb) -> f64 {
    let c = b.center();
    let horiz = ((c.x - eye.x) * (c.x - eye.x) + (c.z - eye.z) * (c.z - eye.z)).sqrt();
    let half_w = (b.max.x - b.min.x) * 0.5 * 1.41421356;
    let half_h = (b.max.y - b.min.y) * 0.5;
    let yaw_span = half_w.atan2(cpp::fmax(horiz, 0.05)) * 180.0 / PI;
    let pitch_span = half_h.atan2(cpp::fmax(horiz, 0.05)) * 180.0 / PI;
    let dyaw = wrap_degrees(yaw_towards(eye, &c) - yaw_deg).abs();
    let dpitch = (pitch_towards(eye, &c) - pitch_deg).abs();
    let yo = cpp::fmax(0.0, dyaw - yaw_span);
    let po = cpp::fmax(0.0, dpitch - pitch_span);
    (yo * yo + po * po).sqrt()
}
