//! `checks/reach.cpp` — melee hits landed from farther than vanilla allows.

use super::prelude::*;

#[derive(Default)]
struct ReachState {
    recent: VecDeque<f64>,
    since_consistency: i32,
}

#[derive(Debug, Default)]
struct Reach {
    i_dist: i32,
    i_consistent: i32,
    threshold: ParamId,
    consistent_limit: ParamId,
    consistent_share: ParamId,
}

pub fn make() -> Check {
    let mut b = CheckBase::new(
        "reach",
        "Reach",
        CheckCategory::Combat,
        "Melee hits from beyond vanilla range",
        "Measures every attributed hit from the attacker's eyes to the nearest point of the victim's hitbox, taking the closest the two were over the latency window, so the reading is always the most charitable one. Vanilla reach is 3.00 blocks.",
    );
    b.def_alert = 70.0;
    b.def_half_life = 40.0;
    b.def_min_evidence = 3;
    Check::new(b, Box::new(Reach::default()))
}

impl CheckImpl for Reach {
    fn create_state(&self) -> Box<dyn Any> {
        Box::new(ReachState::default())
    }
    fn supports_tick_sampling(&self) -> bool {
        false
    }

    fn register(&mut self, r: &mut Registrar<'_>) {
        self.i_dist = r.indicator("distance", "Extended hit", "A hit landed beyond the reach threshold.", 1.0);
        self.i_consistent = r.indicator(
            "consistency",
            "Consistent extension",
            "Most recent hits sit slightly beyond vanilla range, a signature of small reach boosts.",
            0.8,
        );
        self.threshold = r.float(
            "threshold",
            "Reach threshold",
            3.30,
            range(3.0, 4.5, 0.05, 2, "blocks"),
            "Latency-compensated distance beyond which a single hit counts as evidence.",
        );
        r.warn(
            self.threshold,
            "Thresholds this close to 3.00 blocks leave almost no room for measurement error.",
            |s| s.value < 3.15,
        );
        r.preset(self.threshold, 3.45, 3.30, 3.20);
        self.consistent_limit = r.float_in(
            "consistentLimit",
            "Consistency limit",
            3.12,
            range(3.0, 3.6, 0.01, 2, "blocks"),
            "Distance used by the consistency indicator to decide whether a hit is extended.",
            "Detection",
            true,
        );
        self.consistent_share = r.float_in(
            "consistentShare",
            "Consistency share",
            0.7,
            range(0.3, 1.0, 0.05, 2, ""),
            "Fraction of the last 10 hits that must exceed the consistency limit.",
            "Detection",
            true,
        );
    }

    fn on_combat(
        &self,
        ctx: &mut Ctx<'_>,
        ev: &CombatEvent,
        attacker: &PlayerTrack,
        victim: &PlayerTrack,
        s: &mut dyn Any,
    ) {
        let st = state::<ReachState>(s);
        let cfg = ctx.config();
        if ev.candidates != 1 {
            return;
        }
        let ex = ctx.exemptions(attacker);
        if ex.teleport || ex.riding || ex.dead {
            return;
        }
        if ctx.exemptions(victim).teleport {
            return;
        }
        if victim.horizontal_speed(3) > 1.2 {
            return;
        }

        let limit = cfg.value(self.threshold);
        if ctx.debug_enabled() {
            let line =
                fmt!("hit %s reach=%.3f raw=%.3f aim=%.1f°", victim.name(), ev.reach, ev.reach_raw, ev.aim_outside);
            ctx.debug(attacker, line);
        }
        if ev.reach > limit {
            let sev = cpp::clamp((ev.reach - limit) / 0.5 + 0.35, 0.35, 1.0) as f32;
            ctx.emit(
                attacker,
                self.i_dist,
                sev,
                fmt!("Hit %s from %.2f blocks", victim.name(), ev.reach),
                fmt!(
                    "Vanilla reach is 3.00 blocks. Even the most favorable reading across the latency window (eyes to the nearest point of the hitbox) is %.2f blocks beyond it.",
                    ev.reach - mc::VANILLA_REACH
                ),
                vec![
                    V::limited("Compensated reach", ev.reach, "blocks", limit),
                    V::expected("Vanilla reach", mc::VANILLA_REACH, "blocks"),
                    V::plain("Uncompensated reach", ev.reach_raw, "blocks"),
                    V::plain("Aim outside hitbox", ev.aim_outside, "°"),
                ],
                victim.name().to_vec(),
            );
        }

        st.recent.push_back(ev.reach);
        while st.recent.len() > 10 {
            st.recent.pop_front();
        }
        st.since_consistency += 1;
        if st.recent.len() >= 10 && st.since_consistency >= 10 {
            let mut over = 0;
            let mut sorted: Vec<f64> = st.recent.iter().copied().collect();
            sorted.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
            for &r in &sorted {
                if r > cfg.value(self.consistent_limit) {
                    over += 1;
                }
            }
            let share = f64::from(over) / 10.0;
            let median = (sorted[4] + sorted[5]) * 0.5;
            if share >= cfg.value(self.consistent_share) && median > cfg.value(self.consistent_limit) {
                st.since_consistency = 0;
                ctx.emit(
                    attacker,
                    self.i_consistent,
                    cpp::clamp(share, 0.5, 1.0) as f32,
                    fmt!("%d of last 10 hits beyond %.2f blocks (median %.2f)", over, cfg.value(self.consistent_limit), median),
                    text(
                        "Legitimate players land most hits well inside 3 blocks; a distribution centered just past vanilla range is characteristic of a small, constant reach extension.",
                    ),
                    vec![
                        V::limited("Median reach", median, "blocks", cfg.value(self.consistent_limit)),
                        V::limited("Share beyond limit", share * 100.0, "%", cfg.value(self.consistent_share) * 100.0),
                    ],
                    none(),
                );
            }
        }
    }
}
