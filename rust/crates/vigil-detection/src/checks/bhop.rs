//! `checks/bhop.cpp` — airborne-segment physics.

use super::prelude::*;

#[derive(Default)]
struct BHopState {
    chain: i32,
    chain_start: Tick,
    last_chain_evidence: Tick,
}

#[derive(Debug, Default)]
struct BHop {
    i_short: i32,
    i_chain: i32,
    i_strafe: i32,
    i_accel: i32,
    max_air: ParamId,
    hop_limit: ParamId,
    tolerance: ParamId,
}

pub fn make() -> Check {
    let mut b = CheckBase::new(
        "bhop",
        "BHop",
        CheckCategory::Movement,
        "Jump physics that vanilla cannot produce",
        "A vanilla jump on flat ground stays airborne for about 12 ticks, and in the air a player can add only 0.026 blocks per tick while momentum decays by 9% per tick. Vigil flags low hops, hop chains faster than vanilla, and mid-air turns or acceleration beyond air control. Jumps under ceilings (anywhere over the hitbox), with Jump Boost, after knockback, into walls or with lag gaps are skipped.",
    );
    b.def_alert = 70.0;
    b.def_half_life = 30.0;
    b.def_min_evidence = 3;
    Check::new(b, Box::new(BHop::default()))
}

fn axis_stopped(a0: f64, a1: f64) -> bool {
    a0.abs() > 0.08 && a1.abs() < 0.012
}

impl CheckImpl for BHop {
    fn create_state(&self) -> Box<dyn Any> {
        Box::new(BHopState { last_chain_evidence: -10000, ..BHopState::default() })
    }
    fn supports_tick_sampling(&self) -> bool {
        false
    }
    fn requirements(&self) -> Vec<&'static str> {
        vec!["Server positions", "Ground state", "Surface blocks"]
    }

    fn register(&mut self, r: &mut Registrar<'_>) {
        self.i_short = r.indicator(
            "short_air",
            "Short air time",
            "A jump on flat ground landed several ticks earlier than the vanilla arc allows.",
            1.2,
        );
        self.i_chain =
            r.indicator("hop_speed", "Hop speed", "A chain of hops faster than vanilla sprint-jumping.", 1.0);
        self.i_strafe = r.indicator(
            "air_strafe",
            "Air strafe",
            "Mid-air change of direction beyond vanilla air control while keeping speed.",
            1.3,
        );
        self.i_accel =
            r.indicator("air_accel", "Air acceleration", "Speed gained in the air beyond vanilla air control.", 1.0);
        self.max_air = r.int(
            "maxAirTicks",
            "Short hop limit",
            8,
            range(5.0, 11.0, 1.0, 0, "ticks"),
            "Flat-ground jumps that land within this many ticks count as low hops. Vanilla is 12 ticks; packet timing can shorten an observed jump by up to two.",
        );
        r.preset(self.max_air, 7.0, 8.0, 9.0);
        self.hop_limit = r.float(
            "hopLimit",
            "Hop speed limit",
            0.40,
            range(0.36, 0.7, 0.005, 3, "b/tick"),
            "Average speed across three or more consecutive hops above which the chain counts as evidence. Scaled by Speed potions.",
        );
        r.preset(self.hop_limit, 0.43, 0.40, 0.385);
        self.tolerance = r.float_in(
            "airTolerance",
            "Air control tolerance",
            0.06,
            range(0.02, 0.2, 0.005, 3, "b/tick"),
            "Extra velocity change allowed in the air on top of vanilla air control, covering the 1/32-block precision of position packets.",
            "Detection",
            true,
        );
        r.warn(self.tolerance, "Low tolerance lets position-packet rounding look like air control.", |s| {
            s.value < 0.04
        });
    }

    fn on_tick(&self, ctx: &mut Ctx<'_>, t: &PlayerTrack, s: &mut dyn Any) {
        let st = state::<BHopState>(s);
        let cfg = ctx.config();
        if !t.landed() || t.air_segments().is_empty() {
            return;
        }
        let seg = t.air_segments().back().expect("non-empty");
        let ex = ctx.exemptions(t);
        if !seg.clean || ex.movement() || ex.ice || ex.slime {
            st.chain = 0;
            return;
        }
        if !seg.jumped {
            st.chain = 0;
            return;
        }
        if seg.data_gaps > seg.air_ticks / 2 + 1 {
            return;
        }
        if seg.max_gap > 4 {
            st.chain = 0;
            return;
        }

        if ctx.debug_enabled() {
            let line = fmt!(
                "jump air=%d dy=%.3f apex=%.3f dist=%.2f v0=%.3f v1=%.3f gaps=%d maxgap=%d ground=%d",
                seg.air_ticks,
                seg.land_y - seg.takeoff_y,
                seg.apex_y - seg.takeoff_y,
                seg.horizontal_distance,
                seg.start_velocity.horizontal_length(),
                seg.end_velocity.horizontal_length(),
                seg.data_gaps,
                seg.max_gap,
                seg.ground_ticks_before
            );
            ctx.debug(t, line);
        }

        // 1) Short air time on flat ground.
        let flat = (seg.land_y - seg.takeoff_y).abs() < 0.1;
        let max_air = cfg.i(self.max_air);
        if flat && !seg.ceiling_at_takeoff && seg.jump_boost < 0 && seg.air_ticks <= max_air && seg.air_ticks >= 2 {
            let sev = cpp::clamp((f64::from(max_air) + 1.0 - f64::from(seg.air_ticks)) / 4.0, 0.35, 1.0) as f32;
            ctx.emit(
                t,
                self.i_short,
                sev,
                fmt!("Landed after %d ticks on flat ground", seg.air_ticks),
                fmt!(
                    "A vanilla jump stays up 12 ticks and peaks at 1.25 blocks; this one lasted %d and peaked at %.2f, with nothing overhead.",
                    seg.air_ticks,
                    seg.apex_y - seg.takeoff_y
                ),
                vec![
                    V::limited("Air time", f64::from(seg.air_ticks), "ticks", f64::from(max_air)),
                    V::expected("Vanilla flat jump", f64::from(mc::FLAT_JUMP_AIR_TICKS), "ticks"),
                    V::plain("Apex", seg.apex_y - seg.takeoff_y, "blocks"),
                    V::plain("Height change", seg.land_y - seg.takeoff_y, "blocks"),
                ],
                none(),
            );
        }

        // 2) Hop chains.
        if seg.ground_ticks_before <= 2 {
            if st.chain == 0 {
                st.chain_start = seg.takeoff_tick;
            }
            st.chain += 1;
        } else {
            st.chain = 1;
            st.chain_start = seg.takeoff_tick;
        }
        let ceiling_chain = seg.ceiling_at_takeoff || ctx.tick() - t.last_ceiling_tick() <= 20;
        let terrain = if ctx.tick() - t.last_step_up_tick() <= 20 { 1.12 } else { 1.0 };
        if st.chain >= 3 && !ceiling_chain && ctx.tick() - st.last_chain_evidence > 40 {
            let window = cpp::min::<Tick>(ctx.tick() - st.chain_start, Tick::from(t.size() - 1)) as i32;
            if window >= 20 {
                let avg = t.horizontal_speed_fresh(window);
                let mult = t.max_speed_multiplier(window + 10);
                let q = t.data_quality(window);
                let limit = cfg.value(self.hop_limit) * mult * terrain;
                if q >= 0.3 && avg > limit {
                    st.last_chain_evidence = ctx.tick();
                    let sev = cpp::clamp((avg / limit - 1.0) / 0.25 + 0.3, 0.3, 1.0) as f32 * (0.6f32 + 0.4f32 * q);
                    ctx.emit(
                        t,
                        self.i_chain,
                        sev,
                        fmt!("%d hops at %.2f b/tick", st.chain, avg),
                        fmt!(
                            "%d jumps with almost no ground time at %.1f m/s. Vanilla sprint-jumping averages 7.1 m/s; the limit here is %.1f m/s.",
                            st.chain,
                            avg * 20.0,
                            limit * 20.0
                        ),
                        vec![
                            V::limited("Average speed", avg, "b/tick", limit),
                            V::expected("Vanilla sprint-jump", mc::SPRINT_JUMP_AVERAGE * mult, "b/tick"),
                            V::plain("Hops in chain", f64::from(st.chain), ""),
                            V::plain("Movement multiplier", mult, "x"),
                            V::plain("Data quality", f64::from(q) * 100.0, "%"),
                        ],
                        none(),
                    );
                }
            }
        }

        // 3) Air control between the start and end velocity samples.
        let n = seg.air_ticks - 6;
        let v0 = seg.start_velocity.horizontal_length();
        let collided = axis_stopped(seg.start_velocity.x, seg.end_velocity.x)
            || axis_stopped(seg.start_velocity.z, seg.end_velocity.z);
        if n >= 3 && v0 >= 0.18 && seg.end_velocity.horizontal_length() > 0.0 && !collided {
            // std::pow(double, int) promotes the exponent.
            let decay = mc::AIR_FRICTION.powf(f64::from(n));
            let control = mc::AIR_CONTROL_SPRINT * (1.0 - decay) / (1.0 - mc::AIR_FRICTION);
            let dir = seg.start_velocity * (1.0 / v0);
            let v1 = seg.end_velocity;
            let along = v1.dot(&dir);
            let lateral = cpp::max(0.0, v1.dot(&v1) - along * along).sqrt();
            let tol = cfg.value(self.tolerance);
            let max_lateral = control + tol;
            let max_speed = v0 * decay + control + tol;
            let v1len = v1.horizontal_length();
            if lateral > max_lateral && v1len >= 0.85 * v0 {
                let turn = lateral.atan2(cpp::max(1e-6, along)) * 180.0 / std::f64::consts::PI;
                ctx.emit(
                    t,
                    self.i_strafe,
                    cpp::clamp((lateral - max_lateral) / 0.15 + 0.4, 0.4, 1.0) as f32,
                    fmt!("Turned %.0f° mid-air at %.0f%% speed", turn, v1len / v0 * 100.0),
                    fmt!(
                        "Air control allows at most %.2f b/tick of sideways velocity over %d ticks; this jump gained %.2f while keeping its speed.",
                        max_lateral,
                        n,
                        lateral
                    ),
                    vec![
                        V::limited("Sideways velocity gained", lateral, "b/tick", max_lateral),
                        V::plain("Speed retained", v1len / v0 * 100.0, "%"),
                        V::plain("Ticks", f64::from(n), ""),
                    ],
                    none(),
                );
            } else if v1len > max_speed {
                ctx.emit(
                    t,
                    self.i_accel,
                    cpp::clamp((v1len - max_speed) / 0.15 + 0.4, 0.4, 1.0) as f32,
                    fmt!("Sped up to %.2f b/tick in the air", v1len),
                    fmt!(
                        "Left the ground at %.2f b/tick. With 9%% drag and 0.026 b/tick of air control, vanilla allows at most %.2f after %d ticks.",
                        v0,
                        max_speed,
                        n
                    ),
                    vec![V::limited("Speed before landing", v1len, "b/tick", max_speed), V::plain("Speed after takeoff", v0, "b/tick")],
                    none(),
                );
            }
        }
    }
}
