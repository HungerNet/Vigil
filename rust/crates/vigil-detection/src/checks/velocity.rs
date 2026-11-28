//! `checks/velocity.cpp` — players who do not move when hit (anti-knockback).

use super::prelude::*;
use crate::types::octant_of;

#[derive(Clone)]
struct Pending {
    hit_tick: Tick,
    pos0: Vec3,
    pre_velocity: Vec3,
    dir: Vec3,
    ceiling: bool,
    walls: u8,
    attacker: Text,
    sprint_hit: bool,
    delay: i32,
    ping: i32,
}

#[derive(Default)]
struct VelocityState {
    pending: Vec<Pending>,
}

const UNKNOWN_PING_DELAY: i32 = 6;
const MAX_DELAY: i32 = 12;

fn response_delay(ping_ms: i32) -> i32 {
    if ping_ms < 0 {
        return UNKNOWN_PING_DELAY;
    }
    cpp::clamp(cpp::to_int((f64::from(ping_ms) / 50.0).ceil()) + 3, 3, MAX_DELAY)
}

fn expected_push(along: f64, sprint_hit: bool, ticks: i32) -> f64 {
    let mut v = 0.5 * along + 0.4 + if sprint_hit { 0.5 } else { 0.0 };
    let mut d = 0.0;
    for _ in 0..ticks {
        d += v;
        v = v * mc::AIR_FRICTION - mc::AIR_CONTROL_SPRINT;
    }
    cpp::max(0.0, d)
}

#[derive(Debug, Default)]
struct Velocity {
    i_vert: i32,
    i_horiz: i32,
    eval_ticks: ParamId,
    min_rise: ParamId,
    min_push: ParamId,
}

pub fn make() -> Check {
    let mut b = CheckBase::new(
        "velocity",
        "Velocity",
        CheckCategory::Combat,
        "Reduced or cancelled knockback",
        "Knockback lifts a grounded player about 0.4 blocks per tick and pushes them away from the attacker. Vigil waits for the victim's response to arrive (their ping plus the server's relay) and compares the rise and push with vanilla physics. Hits that may not be melee, and victims who are burning, against a wall, under a ceiling, in water, cobwebs or on ladders, are skipped.",
    );
    b.def_alert = 70.0;
    b.def_half_life = 45.0;
    b.def_min_evidence = 3;
    Check::new(b, Box::new(Velocity::default()))
}

impl CheckImpl for Velocity {
    fn create_state(&self) -> Box<dyn Any> {
        Box::new(VelocityState::default())
    }
    fn supports_tick_sampling(&self) -> bool {
        false
    }
    fn requirements(&self) -> Vec<&'static str> {
        vec!["Server positions", "Hurt animations", "Player latency"]
    }

    fn register(&mut self, r: &mut Registrar<'_>) {
        self.i_vert = r.indicator(
            "vertical",
            "Vertical knockback",
            "The victim barely rose after a hit that lifts any grounded player.",
            1.5,
        );
        self.i_horiz =
            r.indicator("horizontal", "Horizontal knockback", "The victim was not pushed away from the attacker.", 1.0);
        self.eval_ticks = r.int(
            "evalTicks",
            "Observation window",
            5,
            range(3.0, 10.0, 1.0, 0, "ticks"),
            "How many ticks of the victim's response are observed, after their latency.",
        );
        self.min_rise = r.float(
            "minRise",
            "Minimum rise",
            0.12,
            range(0.02, 0.4, 0.01, 2, "blocks"),
            "A grounded victim rising less than this after a hit counts as evidence. Vanilla knockback rises over 0.6 blocks.",
        );
        r.preset(self.min_rise, 0.07, 0.12, 0.18);
        self.min_push = r.float(
            "minPush",
            "Minimum push",
            0.15,
            range(0.02, 0.6, 0.01, 2, "blocks"),
            "Displacement away from the attacker below which the push counts as cancelled.",
        );
        r.preset(self.min_push, 0.08, 0.15, 0.22);
    }

    fn on_hit_taken(
        &self,
        ctx: &mut Ctx<'_>,
        ev: &CombatEvent,
        attacker: &PlayerTrack,
        victim: &PlayerTrack,
        s: &mut dyn Any,
    ) {
        let st = state::<VelocityState>(s);
        if ev.candidates != 1 || !ev.victim_on_ground {
            return;
        }
        if ev.reach > 3.6 || ev.aim_outside > 15.0 {
            return;
        }
        let ex = ctx.exemptions(victim);
        if ex.liquid || ex.web || ex.climbing || ex.riding || ex.teleport || ex.dead || ex.fresh || ex.stall {
            return;
        }
        let k = (victim.tick_at(0) - ev.tick) as i32;
        if !victim.has(k + 1) {
            return;
        }
        let at_hit = victim.back(cpp::max(0, k));
        if at_hit.burning || victim.back(k + 1).burning {
            return;
        }
        st.pending.push(Pending {
            hit_tick: ev.tick,
            pos0: pos(victim.back(k + 1)),
            pre_velocity: victim.velocity_fresh(k + 1, 4).horizontal(),
            dir: ev.knockback_dir,
            ceiling: mc::is_solidish(at_hit.block_above_head),
            walls: at_hit.wall_mask,
            attacker: attacker.name().to_vec(),
            sprint_hit: ev.attacker_sprinting,
            ping: at_hit.ping_ms,
            delay: response_delay(at_hit.ping_ms),
        });
    }

    fn on_tick(&self, ctx: &mut Ctx<'_>, t: &PlayerTrack, s: &mut dyn Any) {
        let st = state::<VelocityState>(s);
        if st.pending.is_empty() {
            return;
        }
        let now = ctx.tick();
        let window = ctx.config().i(self.eval_ticks);
        let mut i = 0;
        while i < st.pending.len() {
            let p = &st.pending[i];
            if now - p.hit_tick < Tick::from(p.delay + window) {
                i += 1;
                continue;
            }
            let p = st.pending.remove(i);
            self.evaluate(ctx, t, &p);
        }
    }
}

impl Velocity {
    fn evaluate(&self, ctx: &mut Ctx<'_>, t: &PlayerTrack, p: &Pending) {
        let cfg = ctx.config();
        let now = ctx.tick();
        let k_hit = (now - p.hit_tick) as i32;
        if !t.has(k_hit + 1) {
            return;
        }
        let ex = ctx.exemptions(t);
        if ex.liquid || ex.web || ex.climbing || ex.teleport || ex.riding || ex.stall {
            return;
        }

        // Data from after the earliest possible response, without a lag gap.
        let mut late_updates = 0;
        for k in 0..k_hit {
            if t.update_tick_at(k) > p.hit_tick + Tick::from(p.delay) - 1
                && (k == 0 || t.update_tick_at(k) != t.update_tick_at(k - 1))
            {
                late_updates += 1;
            }
        }
        let gap = t.max_update_gap(k_hit);
        let quality = t.data_quality(k_hit);
        let (mut max_rise, mut push) = (-1e9, -1e9);
        let mut k = k_hit - 1;
        while k >= 0 {
            let bp = pos(t.back(k));
            max_rise = cpp::max(max_rise, bp.y - p.pos0.y);
            push = cpp::max(push, (bp - p.pos0).horizontal().dot(&p.dir));
            k -= 1;
        }
        let observed = k_hit;
        let expected = expected_push(p.pre_velocity.dot(&p.dir), p.sprint_hit, cpp::clamp(observed - p.delay, 1, 6));
        let ping_text = if p.ping >= 0 { fmt!("%d ms", p.ping) } else { text("unknown") };

        if ctx.debug_enabled() {
            let line = fmt!(
                "kb from %s rise=%.3f push=%.3f expected=%.3f delay=%d late=%d gap=%d q=%.2f ceiling=%d walls=%02x",
                &p.attacker,
                max_rise,
                push,
                expected,
                p.delay,
                late_updates,
                gap,
                quality,
                p.ceiling,
                u64::from(p.walls)
            );
            ctx.debug(t, line);
        }
        if late_updates < 2 || gap > 5 || quality < 0.4 {
            return;
        }

        let min_rise = cfg.value(self.min_rise);
        if !p.ceiling && max_rise < min_rise {
            let sev = cpp::clamp(1.0 - cpp::max(0.0, max_rise) / min_rise * 0.6, 0.4, 1.0) as f32
                * (0.6f32 + 0.4f32 * quality);
            ctx.emit(
                t,
                self.i_vert,
                sev,
                fmt!("Rose %.2f blocks after %s's hit", cpp::max(0.0, max_rise), &p.attacker),
                fmt!(
                    "Knockback lifts a grounded player over 0.6 blocks. %s rose %.2f within %d ticks of the response arriving (ping %s), with nothing above them.",
                    t.name(),
                    cpp::max(0.0, max_rise),
                    observed - p.delay,
                    &ping_text
                ),
                vec![
                    V::limited("Rise after hit", cpp::max(0.0, max_rise), "blocks", min_rise),
                    V::plain("Push away from attacker", cpp::max(0.0, push), "blocks"),
                    V::plain("Response window", f64::from(observed), "ticks"),
                    V::plain("Victim ping", f64::from(cpp::max(0, p.ping)), "ms"),
                    V::plain("Data quality", f64::from(quality) * 100.0, "%"),
                ],
                p.attacker.clone(),
            );
        }
        // A wall in the push direction (or either neighbouring direction) stops the push.
        let oct = octant_of(p.dir.x, p.dir.z);
        let blocking = ((1u32 << oct) | (1u32 << ((oct + 1) & 7)) | (1u32 << ((oct + 7) & 7))) as u8;
        let min_push = cfg.value(self.min_push);
        if (p.walls & blocking) == 0 && p.dir.horizontal_length() > 0.5 && expected >= 0.3 && push < min_push {
            let sev = cpp::clamp(1.0 - cpp::max(0.0, push) / expected, 0.35, 1.0) as f32 * (0.6f32 + 0.4f32 * quality);
            ctx.emit(
                t,
                self.i_horiz,
                sev,
                fmt!("Pushed %.2f blocks by %s's hit", cpp::max(0.0, push), &p.attacker),
                fmt!(
                    "This hit should push %s about %.2f blocks away; they moved %.2f, with no wall behind them.",
                    t.name(),
                    expected,
                    cpp::max(0.0, push)
                ),
                vec![
                    V::limited("Push away from attacker", cpp::max(0.0, push), "blocks", min_push),
                    V::expected("Vanilla push", expected, "blocks"),
                    V::plain("Rise after hit", cpp::max(0.0, max_rise), "blocks"),
                    V::plain("Victim ping", f64::from(cpp::max(0, p.ping)), "ms"),
                    V::plain("Data quality", f64::from(quality) * 100.0, "%"),
                ],
                p.attacker.clone(),
            );
        }
    }
}
