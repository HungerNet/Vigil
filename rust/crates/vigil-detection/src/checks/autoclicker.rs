//! `checks/autoclicker.cpp` — machine-regular click timing.

use std::collections::BTreeMap;

use super::prelude::*;

struct ClickerState {
    intervals: VecDeque<i32>,
    last_swing: Tick,
    since_evidence: i32,
    last_rate_evidence: Tick,
}

impl Default for ClickerState {
    fn default() -> Self {
        ClickerState { intervals: VecDeque::new(), last_swing: -1, since_evidence: 0, last_rate_evidence: -10000 }
    }
}

#[derive(Debug, Default)]
struct AutoClicker {
    i_mode: i32,
    i_streak: i32,
    i_rate: i32,
    sample: ParamId,
    mode_share: ParamId,
    streak: ParamId,
    max_cps: ParamId,
    max_interval: ParamId,
    combat_only: ParamId,
}

pub fn make() -> Check {
    let mut b = CheckBase::new(
        "autoclicker",
        "AutoClicker",
        CheckCategory::Combat,
        "Machine-regular click timing",
        "Human clicking drifts across neighboring tick lengths; clicking software repeats one interval exactly. Vigil looks at the timing between swings in combat, not raw clicks per second, and ignores intervals at the server's relay limit, where every fast clicker looks the same.",
    );
    b.def_alert = 75.0;
    b.def_half_life = 60.0;
    b.def_min_evidence = 3;
    Check::new(b, Box::new(AutoClicker::default()))
}

impl CheckImpl for AutoClicker {
    fn create_state(&self) -> Box<dyn Any> {
        Box::new(ClickerState::default())
    }
    fn supports_tick_sampling(&self) -> bool {
        false
    }

    fn register(&mut self, r: &mut Registrar<'_>) {
        self.i_mode = r.indicator(
            "consistency",
            "Timing consistency",
            "Nearly every swing interval in a long sample is the same number of ticks.",
            1.0,
        );
        self.i_streak =
            r.indicator("streak", "Identical intervals", "A long unbroken run of identical swing intervals.", 1.4);
        self.i_rate =
            r.indicator("rate", "Rate ceiling", "Sustained swing rate above what human clicking sustains.", 0.8);
        self.sample = r.int(
            "sample",
            "Sample size",
            30,
            range(10.0, 80.0, 1.0, 0, "intervals"),
            "Number of swing intervals analyzed together for timing consistency.",
        );
        self.mode_share = r.float(
            "modeShare",
            "Consistency limit",
            0.92,
            range(0.6, 1.0, 0.01, 2, ""),
            "Fraction of intervals sharing the same tick length above which timing is considered robotic.",
        );
        r.preset(self.mode_share, 0.96, 0.92, 0.88);
        self.streak = r.int(
            "streak",
            "Identical run",
            20,
            range(6.0, 60.0, 1.0, 0, "intervals"),
            "Length of an unbroken run of identical intervals that counts as evidence.",
        );
        r.preset(self.streak, 26.0, 20.0, 15.0);
        self.max_cps = r.float_in(
            "maxCps",
            "Rate ceiling",
            18.0,
            range(8.0, 25.0, 0.5, 1, "swings/s"),
            "Swings per second, sustained over 40 swings, considered beyond human clicking. Only reachable when the server relays every swing.",
            "Detection",
            true,
        );
        self.max_interval = r.int(
            "maxInterval",
            "Clicking regime",
            6,
            range(3.0, 12.0, 1.0, 0, "ticks"),
            "Only swing intervals up to this length count as clicking. Slower, rhythmic hits (one per hurt cycle) are combat timing, not clicking, and are ignored.",
        );
        r.warn(
            self.max_interval,
            "Long intervals include hit timing; steady combo rhythm can then look machine-regular.",
            |s| s.i() > 8,
        );
        self.combat_only = r.boolean(
            "combatOnly",
            "Combat clicks only",
            true,
            "Only analyze swings while another player is within 6 blocks.",
        );
        r.warn(
            self.combat_only,
            "Swings while mining or idling will be analyzed and can mix unrelated rhythms.",
            |s| !s.b(),
        );
    }

    fn on_tick(&self, ctx: &mut Ctx<'_>, t: &PlayerTrack, s: &mut dyn Any) {
        let st = state::<ClickerState>(s);
        let cfg = ctx.config();
        if !t.swing_started() {
            return;
        }
        let f = t.cur();
        if mc::is_block_item(f.held_item) {
            return;
        }
        if cfg.b(self.combat_only) && ctx.exemptions(t).nearest_opponent > 6.0 {
            st.last_swing = -1;
            return;
        }

        let now = ctx.tick();
        if st.last_swing >= 0 {
            let interval = (now - st.last_swing) as i32;
            if interval > cfg.i(self.max_interval) {
                st.intervals.clear();
            } else if interval > 0 {
                st.intervals.push_back(interval);
                while st.intervals.len() as i32 > 80 {
                    st.intervals.pop_front();
                }
                st.since_evidence += 1;
            }
        }
        st.last_swing = now;

        let floor = cpp::max(1, ctx.swing_relay_floor());
        let iv = &st.intervals;

        // Identical run (excluding the relay floor, where fast humans saturate).
        let (mut run, mut best, mut best_value) = (0, 0, 0);
        for i in 1..iv.len() {
            run = if iv[i] == iv[i - 1] { run + 1 } else { 0 };
            if run > best && iv[i] > floor {
                best = run;
                best_value = iv[i];
            }
        }
        let streak = cfg.i(self.streak);
        if best + 1 >= streak && st.since_evidence >= streak {
            st.since_evidence = 0;
            let sev = cpp::clamp((f64::from(best) + 1.0) / (f64::from(streak) * 1.5), 0.5, 1.0) as f32;
            ctx.emit(
                t,
                self.i_streak,
                sev,
                fmt!("%d identical swing intervals of %d ticks", best + 1, best_value),
                fmt!(
                    "Exactly %.0f ms between swings, %d times in a row. Human clicking at this pace drifts by a tick every few clicks.",
                    f64::from(best_value) * 50.0,
                    best + 1
                ),
                vec![
                    V::limited("Run length", f64::from(best) + 1.0, "intervals", f64::from(streak)),
                    V::plain("Interval", f64::from(best_value) * 50.0, "ms"),
                    V::plain("Server relay floor", f64::from(floor) * 50.0, "ms"),
                ],
                none(),
            );
            return;
        }

        // Consistency over the sample.
        let n = cfg.i(self.sample);
        if iv.len() as i32 >= n && st.since_evidence >= n {
            let mut hist: BTreeMap<i32, i32> = BTreeMap::new();
            let mut mean = 0.0;
            for i in iv.len().wrapping_sub(n as usize)..iv.len() {
                *hist.entry(iv[i]).or_insert(0) += 1;
                mean += f64::from(iv[i]);
            }
            mean /= f64::from(n);
            let (mut mode_value, mut mode_count) = (0, 0);
            for (&k, &c) in &hist {
                if c > mode_count {
                    mode_count = c;
                    mode_value = k;
                }
            }
            let share = f64::from(mode_count) / f64::from(n);
            if ctx.debug_enabled() {
                let line =
                    fmt!("intervals n=%d mode=%d share=%.2f mean=%.2f floor=%d", n, mode_value, share, mean, floor);
                ctx.debug(t, line);
            }
            let ms = cfg.value(self.mode_share);
            if mode_value > floor && share >= ms {
                st.since_evidence = 0;
                let sev = cpp::clamp((share - ms) / (1.0 - ms + 1e-6) * 0.6 + 0.4, 0.4, 1.0) as f32;
                ctx.emit(
                    t,
                    self.i_mode,
                    sev,
                    fmt!("%.0f%% of swings exactly %d ticks apart", share * 100.0, mode_value),
                    fmt!(
                        "%d of the last %d swings were exactly %.0f ms apart. Human clicking spreads across two or three neighboring intervals.",
                        mode_count,
                        n,
                        f64::from(mode_value) * 50.0
                    ),
                    vec![
                        V::limited("Dominant interval share", share * 100.0, "%", ms * 100.0),
                        V::plain("Dominant interval", f64::from(mode_value) * 50.0, "ms"),
                        V::plain("Mean rate", 20.0 / cpp::max(1.0, mean), "swings/s"),
                    ],
                    none(),
                );
            }
        }

        // Rate ceiling over the last 40 swings.
        let sw = t.swing_ticks();
        if sw.len() >= 40 && now - st.last_rate_evidence > 100 {
            let span = (sw[sw.len() - 1] - sw[sw.len() - 40]) as f64 * mc::TICK_SECONDS;
            let cps = if span > 0.0 { 39.0 / span } else { 0.0 };
            if cps >= cfg.value(self.max_cps) {
                st.last_rate_evidence = now;
                ctx.emit(
                    t,
                    self.i_rate,
                    cpp::clamp(cps / (cfg.value(self.max_cps) * 1.3), 0.4, 1.0) as f32,
                    fmt!("Sustained %.1f swings per second", cps),
                    text("Forty consecutive swings at this rate exceed what human clicking sustains."),
                    vec![V::limited("Swing rate", cps, "swings/s", cfg.value(self.max_cps))],
                    none(),
                );
            }
        }
    }
}
