//! `checks/killaura.cpp` — automated aiming and target selection.

use super::prelude::*;

struct Hit {
    tick: Tick,
    victim: i32,
    bearing: f64,
    victim_name: Text,
}

struct KillAuraState {
    hits: VecDeque<Hit>,
    track_victim: i32,
    track_until: Tick,
    samples: i32,
    err_sum: f64,
    motion_sum: f64,
    last_bearing: f64,
    have_bearing: bool,
    last_tracking_evidence: Tick,
}

impl Default for KillAuraState {
    fn default() -> Self {
        KillAuraState {
            hits: VecDeque::new(),
            track_victim: -1,
            track_until: 0,
            samples: 0,
            err_sum: 0.0,
            motion_sum: 0.0,
            last_bearing: 0.0,
            have_bearing: false,
            last_tracking_evidence: -10000,
        }
    }
}

#[derive(Debug, Default)]
struct KillAura {
    i_outside: i32,
    i_multi: i32,
    i_snap: i32,
    i_track: i32,
    unambiguous: ParamId,
    outside_angle: ParamId,
    multi_window: ParamId,
    multi_separation: ParamId,
    snap_angle: ParamId,
    snap_error: ParamId,
    track_error: ParamId,
    track_motion: ParamId,
}

pub fn make() -> Check {
    let mut b = CheckBase::new(
        "killaura",
        "KillAura",
        CheckCategory::Combat,
        "Automated aiming and target selection",
        "For every attributed hit, Vigil reconstructs where the attacker was looking, allowing for latency and rotation precision, and looks for what a hand cannot do: hits outside the view, two targets in an instant, snaps that stop dead on the hitbox, and aim locked to a moving target.",
    );
    b.def_alert = 70.0;
    b.def_half_life = 45.0;
    b.def_min_evidence = 3;
    Check::new(b, Box::new(KillAura::default()))
}

impl CheckImpl for KillAura {
    fn create_state(&self) -> Box<dyn Any> {
        Box::new(KillAuraState::default())
    }
    fn supports_tick_sampling(&self) -> bool {
        false
    }

    fn register(&mut self, r: &mut Registrar<'_>) {
        self.i_outside = r.indicator(
            "outside_view",
            "Hit outside view",
            "The attacker's aim was far from the victim's hitbox when the hit landed.",
            2.0,
        );
        self.i_multi = r.indicator(
            "multi_target",
            "Multi-target",
            "Two different players were hit within a few ticks at widely separated angles.",
            2.0,
        );
        self.i_snap = r.indicator(
            "snap",
            "Snap to target",
            "A large rotation in the ticks before a hit that ends exactly on the target and stops.",
            0.8,
        );
        self.i_track = r.indicator(
            "tracking",
            "Robotic tracking",
            "Aim stays locked onto a moving target with near-zero error for a sustained period.",
            1.0,
        );
        self.unambiguous = r.boolean(
            "unambiguous",
            "Unambiguous hits only",
            true,
            "Only judge hits where exactly one swinging player could have caused the damage.",
        );
        r.warn(self.unambiguous, "Ambiguous hits can be attributed to the wrong player in crowded fights.", |s| !s.b());
        self.outside_angle = r.float(
            "outsideAngle",
            "View angle limit",
            40.0,
            range(15.0, 90.0, 1.0, 0, "°"),
            "How far outside the victim's hitbox the attacker's aim may be at the moment of a hit before it counts as evidence.",
        );
        r.preset(self.outside_angle, 55.0, 40.0, 30.0);
        self.multi_window = r.int(
            "multiWindow",
            "Multi-target window",
            3,
            range(0.0, 10.0, 1.0, 0, "ticks"),
            "Two hits on different players within this many ticks are compared for multi-targeting.",
        );
        self.multi_separation = r.float(
            "multiSeparation",
            "Target separation",
            60.0,
            range(20.0, 180.0, 5.0, 0, "°"),
            "Minimum angle between two victims (as seen by the attacker) for near-simultaneous hits to count.",
        );
        r.preset(self.multi_separation, 75.0, 60.0, 45.0);
        self.snap_angle = r.float_in(
            "snapAngle",
            "Snap rotation",
            45.0,
            range(15.0, 180.0, 5.0, 0, "°"),
            "Rotation within two ticks before a hit that qualifies as a snap.",
            "Detection",
            true,
        );
        self.snap_error = r.float_in(
            "snapError",
            "Snap landing error",
            3.0,
            range(0.5, 10.0, 0.5, 1, "°"),
            "A snap must end within this angle of the victim's center.",
            "Detection",
            true,
        );
        self.track_error = r.float_in(
            "trackError",
            "Tracking error limit",
            1.6,
            range(0.5, 5.0, 0.1, 1, "°"),
            "Average aim error to the victim's center below which tracking is considered robotic. Rotation packets are 1.4° coarse, so perfect tracking reads as roughly 0.7°.",
            "Detection",
            true,
        );
        self.track_motion = r.float_in(
            "trackMotion",
            "Target motion",
            4.0,
            range(1.0, 20.0, 0.5, 1, "°/tick"),
            "The victim must move across the attacker's view at least this fast for tracking to be judged.",
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
        let st = state::<KillAuraState>(s);
        let cfg = ctx.config();
        if cfg.b(self.unambiguous) && ev.candidates != 1 {
            return;
        }
        let ex = ctx.exemptions(attacker);
        if ex.teleport || ex.riding || ex.dead {
            return;
        }

        // 1) Hit outside view.
        let limit = cfg.value(self.outside_angle);
        if ev.aim_outside > limit {
            let sev = cpp::clamp((ev.aim_outside - limit) / 40.0 + 0.5, 0.4, 1.0) as f32;
            ctx.emit(
                attacker,
                self.i_outside,
                sev,
                fmt!("Hit %s while aiming %.0f° away", victim.name(), ev.aim_outside),
                fmt!(
                    "A vanilla hit needs the crosshair on the hitbox. Across the whole latency window, the closest aim was still %.0f° off.",
                    ev.aim_outside
                ),
                vec![
                    V::limited("Aim outside hitbox", ev.aim_outside, "°", limit),
                    V::plain("Yaw error to center", ev.yaw_error, "°"),
                    V::plain("Distance", ev.reach, "blocks"),
                ],
                victim.name().to_vec(),
            );
        }

        // 2) Multi-target.
        let bearing = yaw_towards(&ev.attacker_eye, &ev.victim_pos);
        for h in &st.hits {
            if h.victim == ev.victim {
                continue;
            }
            let gap = ev.tick - h.tick;
            if gap < 0 || gap > Tick::from(cfg.i(self.multi_window)) {
                continue;
            }
            let sep = wrap_degrees(bearing - h.bearing).abs();
            if sep < cfg.value(self.multi_separation) {
                continue;
            }
            let sev = cpp::clamp(0.55 + sep / 360.0 + if gap == 0 { 0.25 } else { 0.0 }, 0.5, 1.0) as f32;
            ctx.emit(
                attacker,
                self.i_multi,
                sev,
                fmt!(
                    "Hit %s and %s %lld tick%s apart, %.0f° apart",
                    &h.victim_name,
                    victim.name(),
                    gap,
                    if gap == 1 { "" } else { "s" },
                    sep
                ),
                fmt!(
                    "Two players %.0f° apart were hit %.0f ms apart. Turning that far and landing both hits that quickly is not achievable by hand.",
                    sep,
                    gap as f64 * 50.0
                ),
                vec![
                    V::limited("Angle between targets", sep, "°", cfg.value(self.multi_separation)),
                    V::plain("Time between hits", gap as f64 * 50.0, "ms"),
                ],
                victim.name().to_vec(),
            );
            break;
        }
        st.hits.push_back(Hit { tick: ev.tick, victim: ev.victim, bearing, victim_name: victim.name().to_vec() });
        while st.hits.len() > 12 {
            st.hits.pop_front();
        }

        // 3) Snap to target: rotation in the two ticks before the hit, then stop. The yaw
        // differences are float subtractions, promoted afterwards (as in C++).
        let k = (attacker.tick_at(0) - ev.tick) as i32;
        if attacker.has(k + 2) && k >= 2 {
            let before = wrap_degrees(f64::from(attacker.back(k).yaw - attacker.back(k + 2).yaw)).abs();
            let after = wrap_degrees(f64::from(attacker.back(k - 2).yaw - attacker.back(k).yaw)).abs();
            if before >= cfg.value(self.snap_angle) && ev.yaw_error <= cfg.value(self.snap_error) && after <= 3.0 {
                let sev = cpp::clamp(before / 120.0, 0.35, 1.0) as f32;
                ctx.emit(
                    attacker,
                    self.i_snap,
                    sev,
                    fmt!("Snapped %.0f° onto %s", before, victim.name()),
                    fmt!(
                        "Turned %.0f° in 100 ms, landed %.1f° from the hitbox center, then stopped dead. Human flicks overshoot or keep moving.",
                        before,
                        ev.yaw_error
                    ),
                    vec![
                        V::limited("Rotation before hit", before, "°", cfg.value(self.snap_angle)),
                        V::limited("Landing error", ev.yaw_error, "°", cfg.value(self.snap_error)),
                        V::plain("Rotation after hit", after, "°"),
                    ],
                    victim.name().to_vec(),
                );
            }
        }

        // Arm tracking analysis on this victim.
        if st.track_victim != ev.victim {
            st.track_victim = ev.victim;
            st.samples = 0;
            st.err_sum = 0.0;
            st.motion_sum = 0.0;
            st.have_bearing = false;
        }
        st.track_until = ev.tick + 20;
    }

    fn on_tick(&self, ctx: &mut Ctx<'_>, subject: &PlayerTrack, s: &mut dyn Any) {
        let st = state::<KillAuraState>(s);
        let cfg = ctx.config();
        if st.track_victim < 0 || ctx.tick() > st.track_until {
            st.track_victim = -1;
            return;
        }
        let Some(victim) = ctx.track(st.track_victim).filter(|v| !v.is_empty()) else {
            st.track_victim = -1;
            return;
        };
        let a = subject.cur();
        let eye = Vec3::new(a.pos[0], a.pos[1] + mc::EYE_HEIGHT, a.pos[2]);
        let center = BoxAabb::player(&pos(victim.cur()), 0.0).center();
        let dist = (center - eye).horizontal_length();
        if dist < 1.5 || dist > 6.0 {
            return;
        }
        let bearing = yaw_towards(&eye, &center);
        let err = wrap_degrees(bearing - f64::from(a.yaw)).abs();
        if st.have_bearing {
            st.err_sum += err;
            st.motion_sum += wrap_degrees(bearing - st.last_bearing).abs();
            st.samples += 1;
        }
        st.last_bearing = bearing;
        st.have_bearing = true;

        if st.samples >= 20 {
            let mean_err = st.err_sum / f64::from(st.samples);
            let mean_motion = st.motion_sum / f64::from(st.samples);
            if ctx.debug_enabled() {
                let line = fmt!(
                    "tracking %s meanErr=%.2f° motion=%.2f°/t n=%d",
                    victim.name(),
                    mean_err,
                    mean_motion,
                    st.samples
                );
                ctx.debug(subject, line);
            }
            if mean_motion >= cfg.value(self.track_motion)
                && mean_err <= cfg.value(self.track_error)
                && ctx.tick() - st.last_tracking_evidence > 40
            {
                st.last_tracking_evidence = ctx.tick();
                let te = cfg.value(self.track_error);
                let sev = cpp::clamp((te - mean_err) / te + 0.4, 0.4, 1.0) as f32;
                ctx.emit(
                    subject,
                    self.i_track,
                    sev,
                    fmt!("Locked onto %s (%.1f° average error)", victim.name(), mean_err),
                    fmt!(
                        "For a second, aim stayed within %.1f° of a target moving %.1f° per tick. Rotations are only 1.4° precise, so this is as close to perfect as the data can show.",
                        mean_err,
                        mean_motion
                    ),
                    vec![
                        V::limited("Average aim error", mean_err, "°", cfg.value(self.track_error)),
                        V::limited("Target angular speed", mean_motion, "°/tick", cfg.value(self.track_motion)),
                        V::plain("Samples", f64::from(st.samples), "ticks"),
                    ],
                    victim.name().to_vec(),
                );
            }
            st.samples = 0;
            st.err_sum = 0.0;
            st.motion_sum = 0.0;
        }
    }
}
