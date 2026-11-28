//! `checks/scaffold.cpp` — Keep-Y, flat scaffold and LegitScaff (automated edge sneaking).

use super::prelude::*;
use crate::types::speed_multiplier;

fn placement_swing(t: &PlayerTrack, min_pitch: f64) -> bool {
    let f = t.cur();
    t.swing_started() && mc::is_block_item(f.held_item) && f64::from(f.pitch) >= min_pitch
}

// ── Keep-Y ───────────────────────────────────────────────────────────────────────────────────

#[derive(Default)]
struct KeepYState {
    active: bool,
    placements: i32,
    start_y: f64,
    start: Tick,
}

#[derive(Debug, Default)]
struct KeepY {
    i_jump: i32,
    pitch: ParamId,
    placed: ParamId,
    speed: ParamId,
    max_gain: ParamId,
}

pub fn make_keep_y() -> Check {
    let mut b = CheckBase::new(
        "keepy",
        "Scaffold (Keep-Y)",
        CheckCategory::World,
        "Bridging while jumping without gaining height",
        "Watches jumps taken while looking down with blocks in hand. Placing several blocks during one jump and landing back at the starting height, at sprinting speed, is the signature of scaffolds that keep the player's Y level fixed. Legitimate jump-bridging is slower; the speed requirement and the evidence gate separate the two.",
    );
    b.def_alert = 72.0;
    b.def_half_life = 30.0;
    b.def_min_evidence = 4;
    Check::new(b, Box::new(KeepY::default()))
}

impl CheckImpl for KeepY {
    fn create_state(&self) -> Box<dyn Any> {
        Box::new(KeepYState::default())
    }
    fn supports_tick_sampling(&self) -> bool {
        false
    }

    fn register(&mut self, r: &mut Registrar<'_>) {
        self.i_jump = r.indicator(
            "flat_jump",
            "Flat jump bridge",
            "Several placements in one jump that landed at the takeoff height.",
            1.0,
        );
        self.pitch = r.float(
            "pitch",
            "Look-down angle",
            55.0,
            range(30.0, 90.0, 1.0, 0, "°"),
            "Minimum pitch (looking down) for a jump to be considered a bridging jump.",
        );
        self.placed = r.int(
            "placements",
            "Placements per jump",
            3,
            range(2.0, 8.0, 1.0, 0, "blocks"),
            "Blocks that must be placed during one jump.",
        );
        r.preset(self.placed, 4.0, 3.0, 2.0);
        self.speed = r.float(
            "minSpeed",
            "Minimum speed",
            0.24,
            range(0.1, 0.5, 0.01, 2, "b/tick"),
            "Average horizontal speed during the jump. Separates scaffolds from legitimate jump-bridging.",
        );
        self.max_gain = r.float_in(
            "maxGain",
            "Height tolerance",
            0.3,
            range(0.05, 1.0, 0.05, 2, "blocks"),
            "Maximum height difference between takeoff and landing.",
            "Detection",
            true,
        );
    }

    fn on_tick(&self, ctx: &mut Ctx<'_>, t: &PlayerTrack, s: &mut dyn Any) {
        let st = state::<KeepYState>(s);
        let cfg = ctx.config();
        let f = t.cur();
        let ex = ctx.exemptions(t);
        if ex.movement() {
            st.active = false;
            return;
        }
        if t.took_off()
            && t.current_air().jumped
            && mc::is_block_item(f.held_item)
            && f64::from(f.pitch) >= cfg.value(self.pitch)
        {
            st.active = true;
            st.placements = 0;
            st.start_y = t.current_air().takeoff_y;
            st.start = ctx.tick();
        }
        if !st.active {
            return;
        }
        if placement_swing(t, cfg.value(self.pitch)) {
            st.placements += 1;
        }
        if ctx.tick() - st.start > 40 {
            st.active = false;
            return;
        }
        if !t.landed() {
            return;
        }
        st.active = false;
        let seg = t.air_segments().back().expect("landed");
        let gain = seg.land_y - st.start_y;
        let speed = if seg.air_ticks > 0 { seg.horizontal_distance / f64::from(seg.air_ticks) } else { 0.0 };
        if ctx.debug_enabled() {
            let line = fmt!("keepy placed=%d gain=%.2f speed=%.3f air=%d", st.placements, gain, speed, seg.air_ticks);
            ctx.debug(t, line);
        }
        if st.placements >= cfg.i(self.placed)
            && gain.abs() <= cfg.value(self.max_gain)
            && speed >= cfg.value(self.speed)
        {
            ctx.emit(
                t,
                self.i_jump,
                cpp::clamp(f64::from(st.placements) / (cfg.value(self.placed) * 1.6), 0.4, 1.0) as f32,
                fmt!("Placed %d blocks in one jump at %.2f b/tick", st.placements, speed),
                fmt!(
                    "Placed %d blocks during one jump and landed level (%+.2f), moving %.2f b/tick while looking %.0f° down. Keeping height and pace while placing mid-jump needs instant, perfect aim.",
                    st.placements,
                    gain,
                    speed,
                    f.pitch
                ),
                vec![
                    V::limited("Placements", f64::from(st.placements), "", f64::from(cfg.i(self.placed))),
                    V::limited("Speed", speed, "b/tick", cfg.value(self.speed)),
                    V::plain("Height change", gain, "blocks"),
                ],
                none(),
            );
        }
    }
}

// ── Flat scaffold ────────────────────────────────────────────────────────────────────────────

struct FlatState {
    placements: VecDeque<Tick>,
    last_evidence: Tick,
}

impl Default for FlatState {
    fn default() -> Self {
        FlatState { placements: VecDeque::new(), last_evidence: -10000 }
    }
}

#[derive(Debug, Default)]
struct FlatScaffold {
    i_fast: i32,
    pitch: ParamId,
    speed: ParamId,
    placements: ParamId,
}

pub fn make_flat() -> Check {
    let mut b = CheckBase::new(
        "flatscaff",
        "Scaffold (Flat)",
        CheckCategory::World,
        "Fast bridging without sneaking or jumping",
        "Looks for players placing blocks under themselves while looking steeply down, without sneaking or jumping, at a speed only reachable while sprinting forward. Legitimate bridging is done backwards, which caps speed at walking pace.",
    );
    b.def_alert = 72.0;
    b.def_half_life = 30.0;
    b.def_min_evidence = 3;
    Check::new(b, Box::new(FlatScaffold::default()))
}

impl CheckImpl for FlatScaffold {
    fn create_state(&self) -> Box<dyn Any> {
        Box::new(FlatState::default())
    }

    fn register(&mut self, r: &mut Registrar<'_>) {
        self.i_fast = r.indicator(
            "fast_bridge",
            "Fast flat bridge",
            "Placing blocks underfoot faster than backward walking allows.",
            1.0,
        );
        self.pitch =
            r.float("pitch", "Look-down angle", 65.0, range(40.0, 90.0, 1.0, 0, "°"), "Minimum pitch while bridging.");
        self.speed = r.float(
            "minSpeed",
            "Speed limit",
            0.25,
            range(0.15, 0.5, 0.005, 3, "b/tick"),
            "Average speed above which flat bridging counts. Walking backwards tops out at 0.216 b/tick.",
        );
        r.preset(self.speed, 0.27, 0.25, 0.235);
        self.placements = r.int(
            "placements",
            "Placements",
            3,
            range(2.0, 10.0, 1.0, 0, "blocks"),
            "Blocks placed within one second.",
        );
    }

    fn on_tick(&self, ctx: &mut Ctx<'_>, t: &PlayerTrack, s: &mut dyn Any) {
        let st = state::<FlatState>(s);
        let cfg = ctx.config();
        let f = t.cur();
        if placement_swing(t, cfg.value(self.pitch)) {
            st.placements.push_back(ctx.tick());
        }
        while st.placements.front().is_some_and(|&p| ctx.tick() - p > 20) {
            st.placements.pop_front();
        }
        let ex = ctx.exemptions(t);
        if ex.movement() || ex.ice || f.sneaking || !f.on_ground || t.ground_ticks() < 16 {
            return;
        }
        if (st.placements.len() as i32) < cfg.i(self.placements) || ctx.tick() - st.last_evidence < 20 {
            return;
        }
        let mut pitch_sum = 0.0;
        for k in 0..16 {
            if t.back(k).sneaking {
                return;
            }
            pitch_sum += f64::from(t.back(k).pitch);
        }
        let pitch = pitch_sum / 16.0;
        if pitch < cfg.value(self.pitch) {
            return;
        }
        let sp = t.horizontal_speed(16);
        if sp <= cfg.value(self.speed) * speed_multiplier(f) {
            return;
        }
        st.last_evidence = ctx.tick();
        let count = st.placements.len() as i32;
        ctx.emit(
            t,
            self.i_fast,
            cpp::clamp((sp / cfg.value(self.speed) - 1.0) / 0.3 + 0.4, 0.4, 1.0) as f32,
            fmt!("Bridged %d blocks at %.2f b/tick", count, sp),
            fmt!(
                "%d blocks in one second at %.2f b/tick, on the ground and never sneaking, looking %.0f° down. Bridging this fast by hand needs sneaking or jump timing at the edge.",
                count,
                sp,
                pitch
            ),
            vec![
                V::limited("Speed", sp, "b/tick", cfg.value(self.speed)),
                V::plain("Placements per second", st.placements.len() as f64, ""),
                V::plain("Average pitch", pitch, "°"),
            ],
            none(),
        );
    }
}

// ── LegitScaff (automated edge sneaking) ─────────────────────────────────────────────────────

struct EagleState {
    sneak_start: Tick,
    taps: VecDeque<Tick>,
    durations: VecDeque<i32>,
    last_placement: Tick,
}

impl Default for EagleState {
    fn default() -> Self {
        EagleState { sneak_start: -1, taps: VecDeque::new(), durations: VecDeque::new(), last_placement: -10000 }
    }
}

#[derive(Debug, Default)]
struct LegitScaff {
    i_taps: i32,
    taps: ParamId,
    max_tap: ParamId,
    max_cv: ParamId,
    pitch: ParamId,
}

pub fn make_legit() -> Check {
    let mut b = CheckBase::new(
        "legitscaff",
        "LegitScaff",
        CheckCategory::World,
        "Automated edge sneaking while bridging (eagle)",
        "Edge-sneaking scaffolds tap sneak for a tick or two exactly at each block edge. Vigil records the sneak taps a bridging player makes next to block placements and looks for taps that are both minimal in duration and metronome-regular, which skilled humans approach but do not sustain.",
    );
    b.def_alert = 75.0;
    b.def_half_life = 40.0;
    b.def_min_evidence = 3;
    Check::new(b, Box::new(LegitScaff::default()))
}

impl CheckImpl for LegitScaff {
    fn create_state(&self) -> Box<dyn Any> {
        Box::new(EagleState::default())
    }
    fn supports_tick_sampling(&self) -> bool {
        false
    }

    fn register(&mut self, r: &mut Registrar<'_>) {
        self.i_taps = r.indicator(
            "edge_taps",
            "Edge-perfect taps",
            "Minimal, perfectly regular sneak taps at each placement.",
            1.0,
        );
        self.taps =
            r.int("taps", "Taps analyzed", 6, range(4.0, 20.0, 1.0, 0, "taps"), "Consecutive taps analyzed together.");
        r.preset(self.taps, 8.0, 6.0, 5.0);
        self.max_tap = r.int(
            "maxTapTicks",
            "Tap duration",
            2,
            range(1.0, 5.0, 1.0, 0, "ticks"),
            "Sneak taps no longer than this are considered minimal.",
        );
        self.max_cv = r.float_in(
            "maxCv",
            "Regularity limit",
            0.10,
            range(0.02, 0.4, 0.01, 2, ""),
            "Coefficient of variation of the interval between taps below which tapping is considered machine-regular.",
            "Detection",
            true,
        );
        self.pitch = r.float_in(
            "pitch",
            "Look-down angle",
            70.0,
            range(40.0, 90.0, 1.0, 0, "°"),
            "Minimum pitch while bridging.",
            "Detection",
            true,
        );
    }

    fn on_tick(&self, ctx: &mut Ctx<'_>, t: &PlayerTrack, s: &mut dyn Any) {
        let st = state::<EagleState>(s);
        let cfg = ctx.config();
        if !t.has(1) {
            return;
        }
        let f = t.cur();
        let p = t.back(1);
        let now = ctx.tick();
        if placement_swing(t, cfg.value(self.pitch)) {
            st.last_placement = now;
        }
        if ctx.exemptions(t).movement() {
            st.taps.clear();
            st.durations.clear();
            return;
        }

        if f.sneaking && !p.sneaking {
            st.sneak_start = now;
        }
        if !f.sneaking && p.sneaking && st.sneak_start >= 0 {
            let dur = (now - st.sneak_start) as i32;
            let bridging = mc::is_block_item(f.held_item)
                && f64::from(f.pitch) >= cfg.value(self.pitch)
                && (now - st.last_placement).wrapping_abs() <= 4;
            if !bridging || dur > cfg.i(self.max_tap) {
                st.taps.clear();
                st.durations.clear();
            } else {
                if st.taps.back().is_some_and(|&b| st.sneak_start - b > 30) {
                    st.taps.clear();
                    st.durations.clear();
                }
                st.taps.push_back(st.sneak_start);
                st.durations.push_back(dur);
            }
            st.sneak_start = -1;
        }

        let n = cfg.i(self.taps);
        if (st.taps.len() as i32) < n {
            return;
        }
        let mut mean = 0.0;
        let mut iv: Vec<f64> = Vec::new();
        // `taps.size() - n + 1` in size_t arithmetic.
        let start = st.taps.len().wrapping_sub(n as usize).wrapping_add(1);
        let mut i = start;
        while i < st.taps.len() {
            iv.push((st.taps[i] - st.taps[i - 1]) as f64);
            mean += *iv.last().expect("pushed");
            i += 1;
        }
        mean /= iv.len() as f64;
        let mut var = 0.0;
        for &v in &iv {
            var += (v - mean) * (v - mean);
        }
        let cv = if mean > 0.0 { (var / iv.len() as f64).sqrt() / mean } else { 1.0 };
        if ctx.debug_enabled() {
            let line = fmt!("eagle taps=%d mean=%.2f cv=%.3f", n, mean, cv);
            ctx.debug(t, line);
        }
        if cv <= cfg.value(self.max_cv) {
            ctx.emit(
                t,
                self.i_taps,
                cpp::clamp(1.0 - cv / (cfg.value(self.max_cv) * 2.0), 0.4, 1.0) as f32,
                fmt!("%d minimal sneak taps, %.1f%% timing variation", n, cv * 100.0),
                fmt!(
                    "%d sneak taps of at most %d ticks, each right at a placement, with timing varying only %.1f%%. Human sneak-bridging varies far more.",
                    n,
                    cfg.i(self.max_tap),
                    cv * 100.0
                ),
                vec![
                    V::limited("Interval variation", cv * 100.0, "%", cfg.value(self.max_cv) * 100.0),
                    V::plain("Mean interval", mean, "ticks"),
                    V::plain("Taps", f64::from(n), ""),
                ],
                none(),
            );
            st.taps.clear();
            st.durations.clear();
        }
    }
}
