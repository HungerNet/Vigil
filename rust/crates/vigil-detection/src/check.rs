//! `src/detection/check.{h,cpp}`: the check interface, the parameters every check declares,
//! indicators, Sensitivity presets, shared exemptions and the evaluation context.
//!
//! **Context (P3_PLAN.md H11).** In C++ a check calls `ctx.emit()` / `ctx.debug()` while it holds
//! references to tracks, and the engine mutates records inside the call. Checks never read
//! anything those calls change (the context exposes tick, time, configuration, exemptions,
//! tracks, the local track and the relay floor), so here the calls are queued in program order
//! and the engine applies them as soon as the check returns — observably identical.

use std::any::Any;

use vigil_formats::config::EngineConfig;

use crate::combat::CombatEvent;
use crate::config::{ConfigRead, NO_PARAM, ParamId, ParamRange, ParamView, SchemaBuilder, range};
use crate::cpp;
use crate::engine::Tracked;
use crate::evidence::{ConfidenceParams, EvidenceValue, Text};
use crate::hashorder::HashOrder;
use crate::track::PlayerTrack;
use crate::types::Tick;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CheckCategory {
    Combat,
    Movement,
    World,
}

pub fn category_name(c: CheckCategory) -> &'static str {
    match c {
        CheckCategory::Combat => "Combat",
        CheckCategory::Movement => "Movement",
        CheckCategory::World => "World",
    }
}

#[derive(Clone, Debug)]
pub struct Indicator {
    pub id: &'static str,
    pub name: &'static str,
    pub description: &'static str,
    pub default_weight: f32,
    pub enabled: ParamId,
    pub weight: ParamId,
}

/// A parameter the Sensitivity presets set together.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct PresetEntry {
    pub param: ParamId,
    pub relaxed: f64,
    pub balanced: f64,
    pub strict: f64,
}

/// Shared per-player, per-tick false-positive reducers.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Exemptions {
    pub knockback: bool,
    pub teleport: bool,
    pub riding: bool,
    pub liquid: bool,
    pub climbing: bool,
    pub web: bool,
    pub ice: bool,
    pub slime: bool,
    pub flying: bool,
    pub fresh: bool,
    pub dead: bool,
    pub stall: bool,
    pub nearest_opponent: f64,
}

impl Default for Exemptions {
    fn default() -> Self {
        Exemptions {
            knockback: false,
            teleport: false,
            riding: false,
            liquid: false,
            climbing: false,
            web: false,
            ice: false,
            slime: false,
            flying: false,
            fresh: false,
            dead: false,
            stall: false,
            nearest_opponent: 1e9,
        }
    }
}

impl Exemptions {
    pub fn movement(&self) -> bool {
        self.knockback
            || self.teleport
            || self.riding
            || self.liquid
            || self.climbing
            || self.web
            || self.flying
            || self.fresh
            || self.dead
            || self.stall
    }

    pub fn describe(&self) -> String {
        let mut s = String::new();
        for (on, what) in [
            (self.knockback, "recent knockback"),
            (self.teleport, "teleport"),
            (self.riding, "riding"),
            (self.liquid, "in liquid"),
            (self.climbing, "climbing"),
            (self.web, "in cobweb"),
            (self.ice, "on ice"),
            (self.slime, "slime"),
            (self.flying, "flying"),
            (self.fresh, "just appeared"),
            (self.dead, "dead"),
            (self.stall, "server lag"),
        ] {
            if !on {
                continue;
            }
            if !s.is_empty() {
                s.push_str(", ");
            }
            s.push_str(what);
        }
        s
    }
}

/// The part of `Check` every check shares: identity, common parameters, indicators, presets.
#[derive(Clone, Debug)]
pub struct CheckBase {
    pub id: &'static str,
    pub name: &'static str,
    pub summary: &'static str,
    pub about: &'static str,
    pub category: CheckCategory,
    pub index: i32,
    pub indicators: Vec<Indicator>,
    pub presets: Vec<PresetEntry>,
    // Defaults for the common confidence parameters (set by each check's constructor).
    pub def_alert: f32,
    pub def_half_life: f32,
    pub def_cooldown: f32,
    pub def_window: f32,
    pub def_min_evidence: i32,
    pub def_min_indicators: i32,
    enabled: ParamId,
    preset: ParamId,
    alert: ParamId,
    half_life: ParamId,
    min_evidence: ParamId,
    min_indicators: ParamId,
    window: ParamId,
    cooldown: ParamId,
    gain: ParamId,
    sampling_enabled: ParamId,
    sampling_interval: ParamId,
    sampling_radius: ParamId,
    debug: ParamId,
}

impl CheckBase {
    pub fn new(
        id: &'static str,
        name: &'static str,
        category: CheckCategory,
        summary: &'static str,
        about: &'static str,
    ) -> Self {
        CheckBase {
            id,
            name,
            summary,
            about,
            category,
            index: -1,
            indicators: Vec::new(),
            presets: Vec::new(),
            def_alert: 70.0,
            def_half_life: 30.0,
            def_cooldown: 20.0,
            def_window: 60.0,
            def_min_evidence: 3,
            def_min_indicators: 1,
            enabled: NO_PARAM,
            preset: NO_PARAM,
            alert: NO_PARAM,
            half_life: NO_PARAM,
            min_evidence: NO_PARAM,
            min_indicators: NO_PARAM,
            window: NO_PARAM,
            cooldown: NO_PARAM,
            gain: NO_PARAM,
            sampling_enabled: NO_PARAM,
            sampling_interval: NO_PARAM,
            sampling_radius: NO_PARAM,
            debug: NO_PARAM,
        }
    }

    pub fn enabled(&self, c: &EngineConfig) -> bool {
        self.enabled != NO_PARAM && c.b(self.enabled)
    }
    pub fn debug(&self, c: &EngineConfig) -> bool {
        self.debug != NO_PARAM && c.b(self.debug)
    }
    pub fn sampling_on(&self, c: &EngineConfig) -> bool {
        self.sampling_enabled != NO_PARAM && c.b(self.sampling_enabled)
    }
    pub fn sampling_interval(&self, c: &EngineConfig) -> i32 {
        if self.sampling_interval != NO_PARAM { cpp::max(1, c.i(self.sampling_interval)) } else { 1 }
    }
    pub fn sampling_radius(&self, c: &EngineConfig) -> f64 {
        if self.sampling_radius != NO_PARAM { c.value(self.sampling_radius) } else { 0.0 }
    }
    pub fn preset_param(&self) -> ParamId {
        self.preset
    }
    pub fn is_preset_controlled(&self, p: ParamId) -> bool {
        self.presets.iter().any(|e| e.param == p)
    }

    pub fn confidence_params(&self, c: &EngineConfig) -> ConfidenceParams {
        ConfidenceParams {
            alert_threshold: c.f(self.alert),
            half_life_sec: c.f(self.half_life),
            min_evidence: c.i(self.min_evidence),
            min_indicators: c.i(self.min_indicators),
            window_sec: c.f(self.window),
            cooldown_sec: c.f(self.cooldown),
            gain: c.f(self.gain),
        }
    }

    pub fn indicator_on(&self, c: &EngineConfig, i: i32) -> bool {
        i >= 0 && (i as usize) < self.indicators.len() && c.b(self.indicators[i as usize].enabled)
    }

    pub fn indicator_weight(&self, c: &EngineConfig, i: i32) -> f32 {
        if i >= 0 && (i as usize) < self.indicators.len() { c.f(self.indicators[i as usize].weight) } else { 0.0 }
    }

    pub fn settings_prefix(&self) -> String {
        format!("checks.{}.", self.id)
    }
}

/// What a check declares with: the engine's schema and the check's own base.
#[derive(Debug)]
pub struct Registrar<'a> {
    pub schema: &'a mut SchemaBuilder,
    pub base: &'a mut CheckBase,
}

const DETECTION: &str = "Detection";

impl Registrar<'_> {
    fn key(&self, key: &str) -> String {
        format!("{}{key}", self.base.settings_prefix())
    }

    /// `addFloat(key, label, def, range, tip)` (group "Detection", not advanced).
    pub fn float(&mut self, key: &str, label: &str, def: f64, r: ParamRange, tip: &str) -> ParamId {
        self.float_in(key, label, def, r, tip, DETECTION, false)
    }
    #[allow(clippy::too_many_arguments)]
    pub fn float_in(
        &mut self,
        key: &str,
        label: &str,
        def: f64,
        r: ParamRange,
        tip: &str,
        group: &str,
        advanced: bool,
    ) -> ParamId {
        let k = self.key(key);
        self.schema.add_float(&k, label, def, r, tip, group, advanced)
    }
    pub fn int(&mut self, key: &str, label: &str, def: i32, r: ParamRange, tip: &str) -> ParamId {
        self.int_in(key, label, def, r, tip, DETECTION, false)
    }
    #[allow(clippy::too_many_arguments)]
    pub fn int_in(
        &mut self,
        key: &str,
        label: &str,
        def: i32,
        r: ParamRange,
        tip: &str,
        group: &str,
        advanced: bool,
    ) -> ParamId {
        let k = self.key(key);
        self.schema.add_int(&k, label, def, r, tip, group, advanced)
    }
    pub fn boolean(&mut self, key: &str, label: &str, def: bool, tip: &str) -> ParamId {
        self.boolean_in(key, label, def, tip, DETECTION, false)
    }
    pub fn boolean_in(&mut self, key: &str, label: &str, def: bool, tip: &str, group: &str, advanced: bool) -> ParamId {
        let k = self.key(key);
        self.schema.add_bool(&k, label, def, tip, group, advanced)
    }
    pub fn warn(&mut self, p: ParamId, warning: &str, when: fn(ParamView) -> bool) {
        self.schema.warn(p, warning, when);
    }
    pub fn preset(&mut self, p: ParamId, relaxed: f64, balanced: f64, strict: f64) {
        self.base.presets.push(PresetEntry { param: p, relaxed, balanced, strict });
    }

    /// `addIndicator`: an enable switch and a weight per indicator.
    pub fn indicator(&mut self, id: &'static str, name: &'static str, description: &'static str, weight: f32) -> i32 {
        let en = self.boolean_in(&format!("ind.{id}.enabled"), name, true, description, "Indicators", false);
        self.warn(
            en,
            "Turning an indicator off removes that evidence source; detections will rely on the remaining indicators.",
            |st| !st.b(),
        );
        let w = self.float_in(
            &format!("ind.{id}.weight"),
            &format!("{name} weight"),
            f64::from(weight),
            range(0.0, 3.0, 0.05, 2, "x"),
            "How strongly one observation from this indicator raises confidence, relative to other indicators.",
            "Indicators",
            true,
        );
        self.base.indicators.push(Indicator { id, name, description, default_weight: weight, enabled: en, weight: w });
        self.base.indicators.len() as i32 - 1
    }
}

/// One check's behavior (`Check`'s virtual functions). Parameters are declared in `register`.
pub trait CheckImpl: std::fmt::Debug {
    fn register(&mut self, r: &mut Registrar<'_>);
    fn create_state(&self) -> Box<dyn Any> {
        Box::new(())
    }
    fn supports_tick_sampling(&self) -> bool {
        true
    }
    fn requirements(&self) -> Vec<&'static str> {
        Vec::new()
    }
    fn on_tick(&self, _ctx: &mut Ctx<'_>, _subject: &PlayerTrack, _state: &mut dyn Any) {}
    fn on_combat(
        &self,
        _ctx: &mut Ctx<'_>,
        _ev: &CombatEvent,
        _attacker: &PlayerTrack,
        _victim: &PlayerTrack,
        _state: &mut dyn Any,
    ) {
    }
    fn on_hit_taken(
        &self,
        _ctx: &mut Ctx<'_>,
        _ev: &CombatEvent,
        _attacker: &PlayerTrack,
        _victim: &PlayerTrack,
        _state: &mut dyn Any,
    ) {
    }
}

/// A check: its shared base and its behavior.
#[derive(Debug)]
pub struct Check {
    pub base: CheckBase,
    pub imp: Box<dyn CheckImpl>,
}

impl Check {
    pub fn new(base: CheckBase, imp: Box<dyn CheckImpl>) -> Check {
        Check { base, imp }
    }

    /// `Check::registerAll`: the common parameters, the check's own, sampling and debug.
    pub fn register_all(&mut self, schema: &mut SchemaBuilder) {
        let b = &mut self.base;
        let prefix = b.settings_prefix();
        let (def_alert, def_half_life, def_cooldown, def_window) =
            (b.def_alert, b.def_half_life, b.def_cooldown, b.def_window);
        let (def_min_evidence, def_min_indicators) = (b.def_min_evidence, b.def_min_indicators);
        let summary = b.summary;
        let mut r = Registrar { schema, base: b };
        let enabled = r.boolean_in("enabled", "Enabled", true, summary, "General", false);
        let preset = r.schema.add_choice(
            &format!("{prefix}preset"),
            "Sensitivity",
            1,
            &["Relaxed", "Balanced", "Strict", "Custom"],
            "Adjusts thresholds and evidence requirements together. Relaxed favors certainty; Strict flags earlier at a higher false-positive risk. Editing any controlled value switches to Custom.",
            "General",
            false,
        );
        let alert = r.float_in(
            "alert",
            "Alert threshold",
            f64::from(def_alert),
            range(20.0, 100.0, 1.0, 0, "%"),
            "Confidence a player must reach before Vigil reports a detection.",
            "Confidence",
            false,
        );
        r.preset(
            alert,
            f64::from(cpp::min(95.0f32, def_alert + 12.0)),
            f64::from(def_alert),
            f64::from(cpp::max(35.0f32, def_alert - 12.0)),
        );
        let min_evidence = r.int_in(
            "minEvidence",
            "Evidence required",
            def_min_evidence,
            range(1.0, 12.0, 1.0, 0, "items"),
            "Number of separate observations needed inside the evidence window before a detection may fire. Prevents single outliers from flagging anyone.",
            "Confidence",
            false,
        );
        r.warn(
            min_evidence,
            "With a single observation allowed, one anomalous measurement can produce a detection.",
            |st| st.i() <= 1,
        );
        r.preset(
            min_evidence,
            f64::from(def_min_evidence + 1),
            f64::from(def_min_evidence),
            f64::from(cpp::max(1, def_min_evidence - 1)),
        );
        let min_indicators = r.int_in(
            "minIndicators",
            "Distinct indicators",
            def_min_indicators,
            range(1.0, 4.0, 1.0, 0, ""),
            "How many different kinds of evidence must agree before a detection may fire.",
            "Confidence",
            false,
        );
        let half_life = r.float_in(
            "halfLife",
            "Confidence half-life",
            f64::from(def_half_life),
            range(5.0, 300.0, 1.0, 0, "s"),
            "Time for confidence to fall by half when no new evidence arrives. Longer values remember past behavior; shorter values forgive faster.",
            "Confidence",
            false,
        );
        let window = r.float_in(
            "window",
            "Evidence window",
            f64::from(def_window),
            range(10.0, 600.0, 5.0, 0, "s"),
            "How far back observations count toward the evidence requirement.",
            "Confidence",
            true,
        );
        let cooldown = r.float_in(
            "cooldown",
            "Alert cooldown",
            f64::from(def_cooldown),
            range(0.0, 300.0, 1.0, 0, "s"),
            "Minimum time between two detections of this check for the same player.",
            "Confidence",
            true,
        );
        let gain = r.float_in(
            "gain",
            "Evidence gain",
            0.25,
            range(0.05, 0.6, 0.01, 2, ""),
            "Fraction of the remaining confidence headroom that one full-strength, weight-1 observation adds.",
            "Confidence",
            true,
        );
        r.base.enabled = enabled;
        r.base.preset = preset;
        r.base.alert = alert;
        r.base.min_evidence = min_evidence;
        r.base.min_indicators = min_indicators;
        r.base.half_life = half_life;
        r.base.window = window;
        r.base.cooldown = cooldown;
        r.base.gain = gain;

        self.imp.register(&mut r);

        let sampling_enabled = r.boolean_in(
            "sampling.enabled",
            "Sampling",
            false,
            "Analyze a subset of ticks and only nearby players to reduce CPU usage in crowded lobbies.",
            "Sampling",
            false,
        );
        r.warn(
            sampling_enabled,
            "Sampling skips ticks: short behaviors (single bursts, brief hops) can be missed and detections arrive later.",
            |st| st.b(),
        );
        let sampling_interval = r.int_in(
            "sampling.interval",
            "Analyze every",
            2,
            range(1.0, 10.0, 1.0, 0, "ticks"),
            "When sampling is on, each player is analyzed once every N ticks.",
            "Sampling",
            false,
        );
        let sampling_radius = r.float_in(
            "sampling.radius",
            "Radius",
            48.0,
            range(8.0, 128.0, 4.0, 0, "blocks"),
            "When sampling is on, players farther than this from you are not analyzed.",
            "Sampling",
            false,
        );
        let debug = r.boolean_in(
            "debug",
            "Debug output",
            false,
            "Records the raw inputs of every evaluation into the Evidence Explorer debug log and detections.log.",
            "Debug",
            false,
        );
        r.base.sampling_enabled = sampling_enabled;
        r.base.sampling_interval = sampling_interval;
        r.base.sampling_radius = sampling_radius;
        r.base.debug = debug;
    }
}

/// An evaluation side effect, applied by the engine in order when the check returns.
#[derive(Debug)]
pub enum Action {
    Emit {
        subject: i32,
        indicator: i32,
        severity: f32,
        summary: Text,
        detail: Text,
        values: Vec<EvidenceValue>,
        target: Text,
    },
    Debug {
        subject_name: Text,
        line: Text,
    },
}

/// `CheckContext`: what a check may read, and its queued side effects.
#[derive(Debug)]
pub struct Ctx<'a> {
    pub(crate) tick: Tick,
    pub(crate) time_ms: f64,
    pub(crate) config: &'a EngineConfig,
    pub(crate) tracked: &'a HashOrder<i32, Tracked>,
    pub(crate) local_id: i32,
    pub(crate) relay_floor: i32,
    pub(crate) debug_on: bool,
    pub(crate) actions: Vec<Action>,
}

static NO_EXEMPTIONS: Exemptions = Exemptions {
    knockback: false,
    teleport: false,
    riding: false,
    liquid: false,
    climbing: false,
    web: false,
    ice: false,
    slime: false,
    flying: false,
    fresh: false,
    dead: false,
    stall: false,
    nearest_opponent: 1e9,
};

impl<'a> Ctx<'a> {
    pub fn tick(&self) -> Tick {
        self.tick
    }
    pub fn time_ms(&self) -> f64 {
        self.time_ms
    }
    pub fn config(&self) -> &'a EngineConfig {
        self.config
    }
    pub fn exemptions(&self, t: &PlayerTrack) -> &'a Exemptions {
        self.tracked.get(&t.entity_id()).map_or(&NO_EXEMPTIONS, |x| &x.ex)
    }
    pub fn track(&self, entity_id: i32) -> Option<&'a PlayerTrack> {
        self.tracked.get(&entity_id).map(|x| &x.track)
    }
    pub fn local_track(&self) -> Option<&'a PlayerTrack> {
        if self.local_id >= 0 { self.track(self.local_id) } else { None }
    }
    pub fn swing_relay_floor(&self) -> i32 {
        self.relay_floor
    }
    pub fn debug_enabled(&self) -> bool {
        self.debug_on
    }
    #[allow(clippy::too_many_arguments)]
    pub fn emit(
        &mut self,
        subject: &PlayerTrack,
        indicator: i32,
        severity: f32,
        summary: Text,
        detail: Text,
        values: Vec<EvidenceValue>,
        target: Text,
    ) {
        self.actions.push(Action::Emit {
            subject: subject.entity_id(),
            indicator,
            severity,
            summary,
            detail,
            values,
            target,
        });
    }
    pub fn debug(&mut self, subject: &PlayerTrack, line: Text) {
        if !self.debug_on {
            return;
        }
        self.actions.push(Action::Debug { subject_name: subject.name().to_vec(), line });
    }
}

/// Downcasts a check's per-player state.
pub fn state<T: 'static>(s: &mut dyn Any) -> &mut T {
    s.downcast_mut::<T>().expect("check state of the check's own type")
}
