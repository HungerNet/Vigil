//! `checks/noslow.cpp` — moving at full speed while using an item.

use super::prelude::*;
use crate::types::PlayerFrame;

struct NoSlowState {
    sprint_using_ticks: i32,
    last_sprint_evidence: Tick,
    last_speed_evidence: Tick,
    use_start: Tick,
    speed_at_use_start: f64,
}

impl Default for NoSlowState {
    fn default() -> Self {
        NoSlowState {
            sprint_using_ticks: 0,
            last_sprint_evidence: -10000,
            last_speed_evidence: -10000,
            use_start: -10000,
            speed_at_use_start: 0.0,
        }
    }
}

const GROUND_FRICTION: f64 = 0.546;

fn slowing_item_in_use(f: &PlayerFrame) -> bool {
    if !f.using_item {
        return false;
    }
    mc::is_sword(f.held_item) || mc::is_consumable_or_usable(f.held_item) || f.blocking
}

#[derive(Debug, Default)]
struct NoSlow {
    i_sprint: i32,
    i_speed: i32,
    sprint_ticks: ParamId,
    speed_limit: ParamId,
}

pub fn make() -> Check {
    let mut b = CheckBase::new(
        "noslow",
        "NoSlow",
        CheckCategory::Movement,
        "No slowdown while eating, drinking, blocking or drawing a bow",
        "Using an item slows movement to a fifth and ends sprinting. Vigil flags players who keep sprinting for several ticks while using an item, or who keep moving fast on the ground after their momentum should have bled off. Lag catch-ups, ice, slime, knockback and server lag are skipped.",
    );
    b.def_alert = 70.0;
    b.def_half_life = 30.0;
    b.def_min_evidence = 3;
    Check::new(b, Box::new(NoSlow::default()))
}

impl CheckImpl for NoSlow {
    fn create_state(&self) -> Box<dyn Any> {
        Box::new(NoSlowState::default())
    }

    fn register(&mut self, r: &mut Registrar<'_>) {
        self.i_sprint =
            r.indicator("sprint", "Sprinting while using", "The sprint flag stayed on during item use.", 0.8);
        self.i_speed =
            r.indicator("speed", "Speed while using", "Ground speed during item use exceeds the vanilla limit.", 1.2);
        self.sprint_ticks = r.int(
            "sprintTicks",
            "Sprint overlap",
            8,
            range(3.0, 30.0, 1.0, 0, "ticks"),
            "Consecutive ticks of sprinting during item use before it counts as evidence. A legitimate player's sprint flag can lag item use by a tick or two.",
        );
        r.preset(self.sprint_ticks, 12.0, 8.0, 6.0);
        self.speed_limit = r.float(
            "speedLimit",
            "Speed limit",
            0.17,
            range(0.08, 0.3, 0.005, 3, "b/tick"),
            "Ground speed during item use above which it counts as evidence. Vanilla item-use walking is about 0.06 b/tick.",
        );
        r.preset(self.speed_limit, 0.20, 0.17, 0.14);
    }

    fn on_tick(&self, ctx: &mut Ctx<'_>, t: &PlayerTrack, s: &mut dyn Any) {
        let st = state::<NoSlowState>(s);
        let cfg = ctx.config();
        let f = t.cur();
        let ex = ctx.exemptions(t);
        if !slowing_item_in_use(f) || f.riding || ex.movement() || ex.ice || ex.slime {
            st.sprint_using_ticks = 0;
            return;
        }
        let now = ctx.tick();
        if t.last_use_start_tick() != st.use_start {
            st.use_start = t.last_use_start_tick();
            st.speed_at_use_start = t.horizontal_speed_fresh(4);
        }

        if f.sprinting {
            st.sprint_using_ticks += 1;
            if st.sprint_using_ticks >= cfg.i(self.sprint_ticks) && now - st.last_sprint_evidence > 40 {
                st.last_sprint_evidence = now;
                ctx.emit(
                    t,
                    self.i_sprint,
                    cpp::clamp(f64::from(st.sprint_using_ticks) / (cfg.value(self.sprint_ticks) * 2.0), 0.4, 1.0)
                        as f32,
                    fmt!("Sprinted %d ticks while using an item", st.sprint_using_ticks),
                    fmt!(
                        "Using an item ends sprinting within a tick or two; the sprint flag stayed on for %d ms.",
                        st.sprint_using_ticks.wrapping_mul(50)
                    ),
                    vec![
                        V::limited(
                            "Sprinting while using",
                            f64::from(st.sprint_using_ticks),
                            "ticks",
                            f64::from(cfg.i(self.sprint_ticks)),
                        ),
                        V::plain("Item", f64::from(f.held_item), ""),
                    ],
                    none(),
                );
            }
        } else {
            st.sprint_using_ticks = 0;
        }

        let using_for = now - t.last_use_start_tick();
        if using_for >= 6 && t.ground_ticks() >= 4 && now - st.last_speed_evidence > 20 {
            let sp = t.horizontal_speed_fresh(4);
            let q = t.data_quality(6);
            let mult = t.max_speed_multiplier(cpp::min::<Tick>(using_for, 30) as i32);
            // Momentum from before the use decays by ground friction each tick (4-tick window).
            let carry = st.speed_at_use_start * GROUND_FRICTION.powf(cpp::max::<Tick>(0, using_for - 4) as f64);
            let limit = cpp::max(cfg.value(self.speed_limit) * mult, carry);
            if q >= 0.6 && sp > limit {
                st.last_speed_evidence = now;
                ctx.emit(
                    t,
                    self.i_speed,
                    cpp::clamp((sp / limit - 1.0) / 0.8 + 0.35, 0.35, 1.0) as f32 * (0.6f32 + 0.4f32 * q),
                    fmt!("Moved %.2f b/tick while using an item", sp),
                    fmt!(
                        "%.1f s into using an item, on the ground, at %.2f b/tick. Item use slows walking to about 0.06; the limit here is %.2f.",
                        using_for as f64 * 0.05,
                        sp,
                        limit
                    ),
                    vec![
                        V::limited("Speed while using", sp, "b/tick", limit),
                        V::plain("Using for", using_for as f64, "ticks"),
                        V::plain("Data quality", f64::from(q) * 100.0, "%"),
                    ],
                    none(),
                );
            }
        }
    }
}
