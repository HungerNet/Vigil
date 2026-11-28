//! `checks/speed.cpp` — horizontal movement faster than the vanilla envelope.

use super::prelude::*;
use crate::types::speed_multiplier_source;

struct SpeedState {
    last_sustained: Tick,
    last_ground: Tick,
    last_burst: Tick,
}

impl Default for SpeedState {
    fn default() -> Self {
        SpeedState { last_sustained: -10000, last_ground: -10000, last_burst: -10000 }
    }
}

const CEILING_GRACE_TICKS: Tick = 20;
const STEP_UP_GRACE_TICKS: Tick = 20;
const STEP_UP_TOLERANCE: f64 = 1.12;
const MULTIPLIER_WINDOW: i32 = 30;

#[derive(Debug, Default)]
struct Speed {
    i_sustained: i32,
    i_ground: i32,
    i_burst: i32,
    air_limit: ParamId,
    ground_limit: ParamId,
    burst_limit: ParamId,
}

pub fn make() -> Check {
    let mut b = CheckBase::new(
        "speed",
        "Speed",
        CheckCategory::Movement,
        "Movement faster than vanilla allows",
        "Compares horizontal speed with the vanilla envelope: about 0.28 blocks per tick sprinting on the ground and 0.36 sprint-jumping, scaled by the player's movement-speed attribute and Speed effects. Speeds are measured between received positions, so lag catch-ups don't count. Knockback, teleports, ice, slime, liquids, ladders, cobwebs, riding, flight, low ceilings and server lag are skipped; stairs and slabs are tolerated.",
    );
    b.def_alert = 72.0;
    b.def_half_life = 25.0;
    b.def_min_evidence = 3;
    Check::new(b, Box::new(Speed::default()))
}

impl CheckImpl for Speed {
    fn create_state(&self) -> Box<dyn Any> {
        Box::new(SpeedState::default())
    }
    fn requirements(&self) -> Vec<&'static str> {
        vec!["Server positions", "Movement attribute or potion effects", "Surface blocks"]
    }

    fn register(&mut self, r: &mut Registrar<'_>) {
        self.i_sustained = r.indicator(
            "sustained",
            "Sustained speed",
            "Average speed over one second exceeds the sprint-jump envelope.",
            1.0,
        );
        self.i_ground =
            r.indicator("ground", "Ground speed", "Speed while staying on the ground exceeds sprinting.", 1.2);
        self.i_burst =
            r.indicator("burst", "Burst", "A short movement far beyond any vanilla speed without a teleport.", 0.6);
        self.air_limit = r.float(
            "limit",
            "Sprint-jump limit",
            0.40,
            range(0.36, 0.7, 0.005, 3, "b/tick"),
            "One-second average speed above which movement counts as evidence. Vanilla sprint-jumping averages 0.356 b/tick. Movement modifiers raise this limit automatically.",
        );
        r.preset(self.air_limit, 0.43, 0.40, 0.385);
        self.ground_limit = r.float(
            "groundLimit",
            "Ground limit",
            0.31,
            range(0.28, 0.6, 0.005, 3, "b/tick"),
            "Average speed while on the ground above which movement counts as evidence. Vanilla sprinting is 0.281 b/tick.",
        );
        r.preset(self.ground_limit, 0.33, 0.31, 0.30);
        self.burst_limit = r.float_in(
            "burst",
            "Burst limit",
            1.0,
            range(0.6, 3.0, 0.05, 2, "b/tick"),
            "Four-tick average speed treated as a burst. Movement above 4 b/tick is treated as a teleport and ignored.",
            "Detection",
            true,
        );
    }

    fn on_tick(&self, ctx: &mut Ctx<'_>, t: &PlayerTrack, s: &mut dyn Any) {
        let st = state::<SpeedState>(s);
        let cfg = ctx.config();
        let ex = ctx.exemptions(t);
        if ex.movement() || ex.ice || ex.slime {
            return;
        }
        if t.size() < 24 {
            return;
        }
        let f = t.cur();
        let now = ctx.tick();
        let mult = t.max_speed_multiplier(MULTIPLIER_WINDOW);
        let under_ceiling = now - t.last_ceiling_tick() <= CEILING_GRACE_TICKS;
        let stepping_up = now - t.last_step_up_tick() <= STEP_UP_GRACE_TICKS;
        let terrain = if stepping_up { STEP_UP_TOLERANCE } else { 1.0 };
        let modifier = speed_multiplier_source(f);

        // Sustained (20 ticks). Head-hitter jumping legitimately exceeds the envelope.
        let avg = t.horizontal_speed_fresh(20);
        let q20 = t.data_quality(20);
        let limit = cfg.value(self.air_limit) * mult * terrain;
        if !under_ceiling && q20 >= 0.3 && avg > limit && now - st.last_sustained >= 20 {
            st.last_sustained = now;
            let sev = cpp::clamp((avg / limit - 1.0) / 0.25 + 0.3, 0.3, 1.0) as f32 * (0.6f32 + 0.4f32 * q20);
            ctx.emit(
                t,
                self.i_sustained,
                sev,
                fmt!("Averaged %.2f b/tick for a second", avg),
                fmt!(
                    "%.1f m/s over one second; the limit with %.2fx movement (%s)%s is %.1f m/s.",
                    avg * 20.0,
                    mult,
                    modifier,
                    if stepping_up { " and stair tolerance" } else { "" },
                    limit * 20.0
                ),
                vec![
                    V::limited("Average speed", avg, "b/tick", limit),
                    V::expected("Vanilla sprint-jump", mc::SPRINT_JUMP_AVERAGE * mult, "b/tick"),
                    V::plain("Movement multiplier", mult, "x"),
                    V::plain("Data quality", f64::from(q20) * 100.0, "%"),
                ],
                none(),
            );
        }

        // Ground (12 ticks continuously on ground, not sneaking).
        if !under_ceiling && t.ground_ticks() >= 12 && !f.sneaking {
            let g = t.horizontal_speed_fresh(12);
            let q12 = t.data_quality(12);
            let glimit = cfg.value(self.ground_limit) * mult;
            if q12 >= 0.3 && g > glimit && now - st.last_ground >= 20 {
                st.last_ground = now;
                let sev = cpp::clamp((g / glimit - 1.0) / 0.25 + 0.35, 0.35, 1.0) as f32 * (0.6f32 + 0.4f32 * q12);
                ctx.emit(
                    t,
                    self.i_ground,
                    sev,
                    fmt!("%.2f b/tick without leaving the ground", g),
                    fmt!(
                        "Stayed on the ground for 12 ticks at %.2f b/tick. Sprinting tops out at 0.28; with %.2fx movement (%s) the limit is %.2f.",
                        g,
                        mult,
                        modifier,
                        glimit
                    ),
                    vec![
                        V::limited("Ground speed", g, "b/tick", glimit),
                        V::expected("Vanilla sprint", mc::SPRINT_GROUND_SPEED * mult, "b/tick"),
                        V::plain("Movement multiplier", mult, "x"),
                        V::plain("Data quality", f64::from(q12) * 100.0, "%"),
                    ],
                    none(),
                );
            }
        }

        // Burst (4 ticks), only on regular data: a burst is exactly what a lag catch-up looks like.
        let b = t.horizontal_speed_fresh(4);
        let blimit = cfg.value(self.burst_limit) * mult;
        if t.data_quality(8) >= 0.9 && b > blimit && b < 4.0 && now - st.last_burst >= 40 {
            st.last_burst = now;
            ctx.emit(
                t,
                self.i_burst,
                cpp::clamp(b / (blimit * 2.0), 0.3, 1.0) as f32,
                fmt!("Moved %.2f b/tick for 4 ticks", b),
                text("A short burst beyond any vanilla speed, too small to be a teleport, with regular position updates."),
                vec![V::limited("Burst speed", b, "b/tick", blimit), V::plain("Movement multiplier", mult, "x")],
                none(),
            );
        }
    }
}
