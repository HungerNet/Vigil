//! `src/detection/combat.{h,cpp}`: melee hit attribution.

use crate::cpp;
use crate::track::PlayerTrack;
use crate::types::{BoxAabb, PlayerFrame, Tick, Vec3, angle_outside_box, mc, pos, wrap_degrees, yaw_towards};

#[derive(Clone, Debug, PartialEq, Default)]
pub struct CombatEvent {
    pub id: u64,
    pub tick: Tick,
    pub time_ms: f64,
    pub attacker: i32,
    pub victim: i32,
    pub candidates: i32,
    pub certainty: f32,
    pub reach: f64,
    pub reach_raw: f64,
    pub aim_outside: f64,
    pub yaw_error: f64,
    pub attacker_yaw: f32,
    pub attacker_pitch: f32,
    pub attacker_eye: Vec3,
    pub victim_pos: Vec3,
    pub knockback_dir: Vec3,
    pub victim_on_ground: bool,
    pub attacker_sprinting: bool,
    pub attacker_item: i32,
}

#[derive(Clone, Copy, Debug)]
pub struct CombatSettings {
    pub swing_window_ticks: i32,
    pub candidate_range: f64,
    pub settle_ticks: i32,
    pub compensation_ticks: i32,
}

#[derive(Clone, Debug)]
struct Pending {
    ev: CombatEvent,
    resolve_at: Tick,
}

#[derive(Clone, Debug)]
pub struct CombatResolver {
    pending: Vec<Pending>,
    ready: Vec<CombatEvent>,
    next_id: u64,
}

impl Default for CombatResolver {
    fn default() -> Self {
        CombatResolver { pending: Vec::new(), ready: Vec::new(), next_id: 1 }
    }
}

fn find_track<'a>(tracks: &[&'a PlayerTrack], id: i32) -> Option<&'a PlayerTrack> {
    tracks.iter().copied().find(|t| t.entity_id() == id)
}

fn eye_of(f: &PlayerFrame) -> Vec3 {
    Vec3::new(f.pos[0], f.pos[1] + if f.sneaking { mc::EYE_HEIGHT_SNEAKING } else { mc::EYE_HEIGHT }, f.pos[2])
}

fn index_for_tick(t: &PlayerTrack, tick: Tick) -> i32 {
    let now = t.tick_at(0);
    let k = (now - tick) as i32;
    cpp::clamp(k, 0, cpp::max(0, t.size() - 1))
}

impl CombatResolver {
    pub fn ready(&self) -> &[CombatEvent] {
        &self.ready
    }

    pub fn reset(&mut self) {
        self.pending.clear();
        self.ready.clear();
    }

    /// Once per tick after all tracks were updated; `tracks` holds every player (C++ order).
    pub fn update(&mut self, tick: Tick, time_ms: f64, tracks: &[&PlayerTrack], s: &CombatSettings) {
        self.ready.clear();

        // 1) New hurts become pending events with their attribution decided now.
        for &victim in tracks {
            if victim.is_empty() || !victim.hurt_started() {
                continue;
            }
            let vf = victim.cur();
            let vbox = BoxAabb::player(&pos(vf), mc::HITBOX_BORDER);
            let mut best: Option<&PlayerTrack> = None;
            let mut best_angle = f64::MAX;
            let mut candidates = 0;
            for &att in tracks {
                if std::ptr::eq(att, victim) || att.is_empty() {
                    continue;
                }
                if tick - att.last_swing_tick() > Tick::from(s.swing_window_ticks) {
                    continue;
                }
                let af = att.cur();
                if af.dead || af.riding {
                    continue;
                }
                if mc::is_ranged(af.held_item) {
                    continue;
                }
                let eye = eye_of(af);
                if vbox.distance_to(&eye) > s.candidate_range {
                    continue;
                }
                candidates += 1;
                let angle = angle_outside_box(&eye, f64::from(af.yaw), f64::from(af.pitch), &vbox);
                let center_err = wrap_degrees(yaw_towards(&eye, &vbox.center()) - f64::from(af.yaw)).abs();
                let score = angle * 4.0 + center_err;
                if score < best_angle {
                    best_angle = score;
                    best = Some(att);
                }
            }
            let Some(best) = best else { continue };
            let id = self.next_id;
            self.next_id += 1;
            let ev = CombatEvent {
                id,
                tick,
                time_ms,
                attacker: best.entity_id(),
                victim: victim.entity_id(),
                candidates,
                certainty: if candidates == 1 { 1.0 } else { 0.45 },
                victim_on_ground: if victim.has(1) { victim.back(1).on_ground } else { vf.on_ground },
                attacker_sprinting: best.cur().sprinting,
                attacker_item: best.cur().held_item,
                ..CombatEvent::default()
            };
            self.pending.push(Pending { ev, resolve_at: tick + Tick::from(cpp::max(0, s.settle_ticks)) });
        }

        // 2) Finalize events whose settle window elapsed.
        let mut i = 0;
        while i < self.pending.len() {
            if tick >= self.pending[i].resolve_at {
                let mut p = self.pending.remove(i);
                finalize(&mut p, tracks, s);
                if p.ev.attacker >= 0 {
                    self.ready.push(p.ev);
                }
            } else {
                i += 1;
            }
        }
    }
}

fn finalize(p: &mut Pending, tracks: &[&PlayerTrack], s: &CombatSettings) {
    let (Some(att), Some(vic)) = (find_track(tracks, p.ev.attacker), find_track(tracks, p.ev.victim)) else {
        p.ev.attacker = -1;
        return;
    };
    if att.is_empty() || vic.is_empty() {
        p.ev.attacker = -1;
        return;
    }
    let a_hit = index_for_tick(att, p.ev.tick);
    let v_hit = index_for_tick(vic, p.ev.tick);
    let af_hit = att.back(a_hit);
    let vf_hit = vic.back(v_hit);

    p.ev.attacker_yaw = af_hit.yaw;
    p.ev.attacker_pitch = af_hit.pitch;
    p.ev.attacker_eye = eye_of(af_hit);
    p.ev.victim_pos = pos(vf_hit);
    p.ev.reach_raw = BoxAabb::player(&pos(vf_hit), mc::HITBOX_BORDER).distance_to(&p.ev.attacker_eye);
    p.ev.yaw_error = wrap_degrees(
        yaw_towards(&p.ev.attacker_eye, &BoxAabb::player(&pos(vf_hit), 0.0).center()) - f64::from(af_hit.yaw),
    )
    .abs();

    let mut min_reach = f64::MAX;
    let mut min_aim = f64::MAX;
    let mut ka = cpp::max(0, a_hit - s.settle_ticks);
    while ka <= cpp::min(att.size() - 1, a_hit + 1) {
        let af = att.back(ka);
        let eye = eye_of(af);
        let mut kv = cpp::max(0, v_hit - 1);
        while kv <= cpp::min(vic.size() - 1, v_hit + s.compensation_ticks) {
            let b = BoxAabb::player(&pos(vic.back(kv)), mc::HITBOX_BORDER);
            min_reach = cpp::min(min_reach, b.distance_to(&eye));
            min_aim = cpp::min(min_aim, angle_outside_box(&eye, f64::from(af.yaw), f64::from(af.pitch), &b));
            kv += 1;
        }
        ka += 1;
    }
    p.ev.reach = min_reach;
    p.ev.aim_outside = min_aim;

    let dir = (pos(vf_hit) - pos(af_hit)).horizontal();
    let len = dir.horizontal_length();
    p.ev.knockback_dir = if len > 1e-6 { dir * (1.0 / len) } else { Vec3::default() };
}
