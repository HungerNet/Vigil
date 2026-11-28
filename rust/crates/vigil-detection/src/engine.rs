//! `src/detection/engine.{h,cpp}`: the per-frame pipeline.
//!
//! Per frame: phase & scene → tracks → who is a player (NPCs, duplicates) → records (relevance,
//! presence) → exemptions (incl. server stalls) → checks → combat → history. The engine reads
//! nothing but its frames and its `EngineConfig`; entities are visited in MSVC
//! `std::unordered_map` order (`HashOrder`), exactly as the C++ engine visits them.
//!
//! Persistence sinks (P3B), set by the host as in C++: the user's marks file (`set_marks_path`),
//! the cross-session history (`set_history_path`, flushed at `reset_session` and every 60 s of
//! engine time), the session journal (`set_journal_directory`) and the "detections" log channel
//! (`set_log`). They never influence a decision. Wall-clock reads go through the injected
//! [`Clock`].

use std::any::Any;
use std::collections::VecDeque;
use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::time::Instant;

use vigil_formats::config::{EngineConfig, Schema};
use vigil_formats::json::{self, Value};

use crate::check::{Action, Check, Ctx, Exemptions};
use crate::checks::create_all_checks;
use crate::clock::Clock;
use crate::combat::{CombatEvent, CombatResolver, CombatSettings};
use crate::config::{ConfigRead, NO_PARAM, ParamId, SchemaBuilder, WarnFn, range};
use crate::cpp;
use crate::evidence::{Detection, Evidence, EvidenceValue, Text, derive_envelope};
use crate::fmt;
use crate::hashorder::{FastMap, HashOrder};
use crate::marks::Marks;
use crate::matchtracker::{GamePhase, MatchTracker};
use crate::printf::Arg;
use crate::roster::{EntityKind, EntityObservation, EntityVerdict, UNLISTED_GRACE_TICKS, classify_entity};
use crate::session::{Alert, ConfidencePoint, DebugLine, PlayerRecord, Session, TimelineEvent, TimelineKind};
use crate::sinks::{LogSink, LogSlot, writef_message};
use crate::track::PlayerTrack;
use crate::types::{GameContext, Tick, Vec3, WorldFrame, pos};

#[derive(Clone, Debug, Default, PartialEq)]
pub struct EngineStats {
    pub avg_tick_micros: f64,
    pub peak_tick_micros: f64,
    pub tracked_players: i32,
    pub filtered_entities: i32,
    pub ticks_processed: u64,
    pub skipped_ticks: i32,
    pub combat_events: i32,
    pub stalls: i32,
    pub pruned_records: i32,
}

/// How Vigil treats a listed player right now.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct PlayerState {
    pub listed: bool,
    pub local: bool,
    pub teammate: bool,
    pub exempt: bool,
    pub in_this_game: bool,
    pub judged: bool,
}

/// An entity in the player list that Vigil does not treat as a player.
#[derive(Clone, Debug, PartialEq)]
pub struct FilteredEntity {
    pub entity_id: i32,
    pub name: Text,
    pub kind: EntityKind,
    pub reason: Text,
}

/// One entity the engine tracks (`Engine::Tracked`).
#[derive(Debug, Default)]
pub struct Tracked {
    pub track: PlayerTrack,
    pub(crate) states: Vec<Box<dyn Any>>,
    pub ex: Exemptions,
    pub last_seen: Tick,
    pub record_key: Text,
    pub obs: EntityObservation,
    pub first_pos: Vec3,
    pub verdict: EntityVerdict,
    pub pending: bool,
}

impl Tracked {
    pub fn is_player(&self) -> bool {
        self.verdict.kind == EntityKind::Player && !self.pending
    }
}

fn fresh_states(t: &mut Tracked, checks: &[Check]) {
    t.states = checks.iter().map(|c| c.imp.create_state()).collect();
}

#[derive(Debug, Default)]
struct GlobalParams {
    monitoring: ParamId,
    run_in_game: ParamId,
    run_pregame: ParamId,
    run_lobby: ParamId,
    run_replay: ParamId,
    analyze_self: ParamId,
    ignore_teammates: ParamId,
    kb_ticks: ParamId,
    teleport_ticks: ParamId,
    ice_ticks: ParamId,
    fresh_ticks: ParamId,
    swing_window: ParamId,
    settle_ticks: ParamId,
    comp_ticks: ParamId,
    combat_range: ParamId,
    journal: ParamId,
    history: ParamId,
}

#[derive(Debug)]
pub struct Engine {
    checks: Vec<Check>,
    tracked: HashOrder<i32, Tracked>,
    combat: CombatResolver,
    session: Session,
    match_: MatchTracker,
    marks: Marks,
    alerts: VecDeque<Alert>,
    debug_log: VecDeque<DebugLine>,
    recent_combat: Vec<CombatEvent>,
    filtered: Vec<FilteredEntity>,
    stats: EngineStats,
    tick: Tick,
    time_ms: f64,
    context: GameContext,
    context_detail: Text,
    active: bool,
    recording: bool,
    local_id: i32,
    alert_seq: u64,
    relay_floor: i32,
    last_history_flush_ms: f64,
    stall_until: Tick,
    no_update_run: i32,
    schema: SchemaBuilder,
    config: EngineConfig,
    p: GlobalParams,
    clock: Clock,
    history_path: Option<PathBuf>,
    journal_dir: Option<OsString>,
    log: LogSlot,
}

impl Default for Engine {
    fn default() -> Self {
        Self::new()
    }
}

impl Engine {
    /// `Engine()` + `initialize()`: creates the checks, declares every parameter, starts on the
    /// schema's defaults.
    pub fn new() -> Engine {
        let mut schema = SchemaBuilder::default();
        let p = register_global_params(&mut schema);
        let mut checks = create_all_checks();
        for (i, c) in checks.iter_mut().enumerate() {
            c.base.index = i as i32;
            c.register_all(&mut schema);
        }
        let config = EngineConfig::defaults(&schema.schema);
        let clock = Clock::system();
        let mut session = Session::default();
        session.reset(0.0, checks.len(), clock.now_ms());
        Engine {
            checks,
            tracked: HashOrder::new(),
            combat: CombatResolver::default(),
            session,
            match_: MatchTracker::default(),
            marks: Marks::default(),
            alerts: VecDeque::new(),
            debug_log: VecDeque::new(),
            recent_combat: Vec::new(),
            filtered: Vec::new(),
            stats: EngineStats::default(),
            tick: 0,
            time_ms: 0.0,
            context: GameContext::Unknown,
            context_detail: Text::new(),
            active: false,
            recording: false,
            local_id: -1,
            alert_seq: 0,
            relay_floor: 0,
            last_history_flush_ms: 0.0,
            stall_until: -1,
            no_update_run: 0,
            schema,
            config,
            p,
            clock,
            history_path: None,
            journal_dir: None,
            log: LogSlot::default(),
        }
    }

    // ── Configuration ────────────────────────────────────────────────────────────────────────

    pub fn schema(&self) -> &Schema {
        &self.schema.schema
    }
    /// The warning predicate of each parameter (`ParamSpec::warnWhen`).
    pub fn warn_predicates(&self) -> &[Option<WarnFn>] {
        &self.schema.warn
    }
    pub fn config(&self) -> &EngineConfig {
        &self.config
    }
    /// `setConfig`: takes effect from the next frame; refuses a snapshot of another schema.
    pub fn set_config(&mut self, config: &EngineConfig) -> bool {
        if config.values.len() != self.schema.schema.params.len() {
            return false;
        }
        self.config = config.clone();
        true
    }
    /// Replaces the user's marks (they take the engine's clock).
    pub fn set_marks(&mut self, mut marks: Marks) {
        marks.set_clock(self.clock.clone());
        self.marks = marks;
    }
    pub fn marks(&self) -> &Marks {
        &self.marks
    }
    pub fn marks_mut(&mut self) -> &mut Marks {
        &mut self.marks
    }

    // ── Persistence hooks (set by the host) ──────────────────────────────────────────────────

    /// The wall clock the engine and its marks read (default: the system clock).
    pub fn set_clock(&mut self, clock: Clock) {
        self.marks.set_clock(clock.clone());
        self.clock = clock;
    }
    pub fn clock(&self) -> &Clock {
        &self.clock
    }
    /// `setMarksPath`: loads the marks file; every change is saved to it.
    pub fn set_marks_path(&mut self, path: &Path) {
        self.marks.set_path(path);
    }
    /// `setHistoryPath`: remembers the path and loads the history.
    pub fn set_history_path(&mut self, path: &Path) {
        self.history_path = Some(path.to_path_buf());
        self.session.load_history(std::fs::read(path).ok().as_deref());
    }
    /// `setJournalDirectory`.
    pub fn set_journal_directory(&mut self, dir: &Path) {
        self.journal_dir = if dir.as_os_str().is_empty() { None } else { Some(dir.as_os_str().to_owned()) };
    }
    /// Where the "detections" log lines go (`None`: nowhere, as C++ before `log::init`).
    pub fn set_log(&mut self, sink: Option<Box<dyn LogSink>>) {
        self.log = LogSlot(sink);
    }

    // ── Reads ────────────────────────────────────────────────────────────────────────────────

    pub fn checks(&self) -> &[Check] {
        &self.checks
    }
    pub fn check_by_id(&self, id: &str) -> Option<&Check> {
        self.checks.iter().find(|c| c.base.id == id)
    }
    pub fn session(&self) -> &Session {
        &self.session
    }
    pub fn alerts(&self) -> &VecDeque<Alert> {
        &self.alerts
    }
    pub fn alert_seq(&self) -> u64 {
        self.alert_seq
    }
    pub fn debug_log(&self) -> &VecDeque<DebugLine> {
        &self.debug_log
    }
    pub fn stats(&self) -> &EngineStats {
        &self.stats
    }
    pub fn context(&self) -> GameContext {
        self.context
    }
    pub fn context_detail(&self) -> &[u8] {
        &self.context_detail
    }
    pub fn active(&self) -> bool {
        self.active
    }
    pub fn recording(&self) -> bool {
        self.recording
    }
    pub fn monitoring_enabled(&self) -> bool {
        self.monitoring_enabled_in(&self.config)
    }
    pub fn monitoring_enabled_in(&self, c: &EngineConfig) -> bool {
        self.p.monitoring != NO_PARAM && !c.values.is_empty() && c.b(self.p.monitoring)
    }
    pub fn last_tick(&self) -> Tick {
        self.tick
    }
    pub fn last_time_ms(&self) -> f64 {
        self.time_ms
    }
    pub fn recent_combat(&self) -> &[CombatEvent] {
        &self.recent_combat
    }
    pub fn match_tracker(&self) -> &MatchTracker {
        &self.match_
    }
    pub fn phase(&self) -> GamePhase {
        self.match_.phase()
    }
    pub fn scene(&self) -> i32 {
        self.match_.scene()
    }
    pub fn in_this_game(&self, r: &PlayerRecord) -> bool {
        r.scene == self.match_.scene()
    }
    pub fn filtered(&self) -> &[FilteredEntity] {
        &self.filtered
    }
    pub fn stalled(&self) -> bool {
        self.tick < self.stall_until
    }
    pub fn track(&self, entity_id: i32) -> Option<&PlayerTrack> {
        self.tracked.get(&entity_id).map(|t| &t.track)
    }

    pub fn player_state(&self, r: &PlayerRecord) -> PlayerState {
        self.player_state_in(r, &self.config)
    }
    pub fn player_state_in(&self, r: &PlayerRecord, c: &EngineConfig) -> PlayerState {
        let exempt = self.marks.exempt(&r.key);
        PlayerState {
            listed: true,
            local: r.local,
            teammate: r.teammate,
            exempt,
            in_this_game: self.in_this_game(r),
            judged: !exempt
                && (!r.local || c.b(self.p.analyze_self))
                && (!r.teammate || !c.b(self.p.ignore_teammates) || self.context == GameContext::Replay),
        }
    }

    // ── Session lifecycle ────────────────────────────────────────────────────────────────────

    fn phase_analyzed(&self, p: GamePhase) -> bool {
        match p {
            GamePhase::Match => self.config.b(self.p.run_in_game),
            GamePhase::Replay => self.config.b(self.p.run_replay),
            GamePhase::Queue => self.config.b(self.p.run_pregame),
            GamePhase::Lobby => self.config.b(self.p.run_lobby),
            _ => false,
        }
    }

    fn phase_recorded(&self, p: GamePhase) -> bool {
        match p {
            GamePhase::Queue | GamePhase::Match | GamePhase::PostGame | GamePhase::Replay => true,
            GamePhase::Lobby => self.config.b(self.p.run_lobby),
            GamePhase::Idle => false,
        }
    }

    /// Entity ids become invalid; the session continues.
    pub fn on_world_changed(&mut self) {
        self.tracked.clear();
        self.combat.reset();
        self.recent_combat.clear();
        self.filtered.clear();
        self.local_id = -1;
        self.no_update_run = 0;
        self.stall_until = -1;
        self.match_.on_world_changed();
        let (t, tick) = (self.time_ms, self.tick);
        for r in self.session.players_mut() {
            if r.present {
                r.present = false;
                r.timeline.push_back(TimelineEvent::new(
                    TimelineKind::Left,
                    t,
                    tick,
                    -1,
                    0,
                    0.0,
                    b"Left view (world changed)",
                ));
            }
        }
        self.session.bump();
    }

    pub fn reset_session(&mut self, now_ms: f64) {
        self.flush_history();
        self.session.reset(now_ms, self.checks.len(), self.clock.now_ms());
        self.tracked.clear();
        self.combat.reset();
        self.alerts.clear();
        self.debug_log.clear();
        self.recent_combat.clear();
        self.stats = EngineStats::default();
        self.relay_floor = 0;
    }

    /// `flushHistory`: writes history/players.json (`data.history` on and a path set).
    pub fn flush_history(&mut self) {
        if self.p.history != NO_PARAM && self.config.b(self.p.history) {
            if let Some(path) = &self.history_path {
                self.session.save_history(path, self.clock.now_ms());
            }
        }
    }

    fn log_line(&mut self, f: &str, args: &[Arg<'_>]) {
        if let Some(sink) = self.log.0.as_mut() {
            sink.write("detections", &writef_message(f, args));
        }
    }

    // ── Per tick ─────────────────────────────────────────────────────────────────────────────

    fn compute_exemptions(t: &mut Tracked, tick: Tick, cfg: &EngineConfig, p: &GlobalParams, stall_until: Tick) {
        let tr = &t.track;
        let f = tr.cur();
        let e = &mut t.ex;
        e.knockback = tick - tr.last_hurt_tick() <= Tick::from(cfg.i(p.kb_ticks));
        e.teleport = tick - tr.last_teleport_tick() <= Tick::from(cfg.i(p.teleport_ticks));
        e.riding = tick - tr.last_ride_tick() <= 10;
        e.liquid = tick - tr.last_liquid_tick() <= 10;
        e.climbing = tick - tr.last_climb_tick() <= 6;
        e.web = tick - tr.last_web_tick() <= 6;
        e.ice = tick - tr.last_ice_tick() <= Tick::from(cfg.i(p.ice_ticks));
        e.slime = tick - tr.last_slime_tick() <= 20;
        e.fresh = tick - tr.first_tick() < Tick::from(cfg.i(p.fresh_ticks));
        e.dead = f.dead || f.health == 0.0;
        e.stall = tick < stall_until;
        e.flying = false;
        let flight_ticks = if f.invisible { 10 } else { 40 };
        let look_back = if f.invisible { 10 } else { 20 };
        if tr.airborne() && tr.air_ticks() > flight_ticks && tr.size() > look_back {
            e.flying = tr.back(0).pos[1] >= tr.back(look_back).pos[1] - if f.invisible { 0.3 } else { 0.5 };
        }
    }

    /// World-level stall: nearly every moving player stops receiving positions at once, then jumps.
    fn detect_stall(&mut self) {
        let (mut movers, mut updated) = (0, 0);
        for t in self.tracked.values() {
            if t.last_seen != self.tick || !t.is_player() || t.track.cur().is_local || t.track.size() < 14 {
                continue;
            }
            if t.track.horizontal_speed(12) < 0.06 {
                continue;
            }
            movers += 1;
            if t.track.position_updated() {
                updated += 1;
            }
        }
        if movers < 2 {
            self.no_update_run = 0;
            return;
        }
        if updated == 0 {
            self.no_update_run += 1;
        } else {
            if self.no_update_run >= 4 {
                self.stall_until = cpp::max(self.stall_until, self.tick + 25);
                self.stats.stalls += 1;
            }
            self.no_update_run = 0;
        }
    }

    /// Who is a player: NPC rules per entity, then duplicates and name collisions.
    fn classify_all(&mut self, local_name: &[u8]) {
        let replay = self.context == GameContext::Replay;
        let tick = self.tick;
        let order = self.tracked.keys();
        for id in &order {
            let t = self.tracked.get_mut(id).expect("tracked");
            if t.last_seen != tick {
                continue;
            }
            let f = t.track.cur();
            t.verdict = classify_entity(f, &t.obs, local_name, replay);
            t.pending = !replay
                && !f.is_local
                && f.tab_known
                && !f.tab_listed
                && t.obs.ticks_unlisted < UNLISTED_GRACE_TICKS
                && t.verdict.kind == EntityKind::Player;
        }
        // Same UUID on several entities: the one updated most recently is the player.
        let mut by_uuid: FastMap<Text, i32> = FastMap::default();
        for &id in &order {
            let t = self.tracked.get(&id).expect("tracked");
            if t.last_seen != tick || !t.is_player() || t.track.uuid().is_empty() {
                continue;
            }
            let uuid = t.track.uuid().to_vec();
            let Some(&other) = by_uuid.get(&uuid) else {
                by_uuid.insert(uuid, id);
                continue;
            };
            let o = self.tracked.get(&other).expect("tracked");
            let newer = t.track.update_tick_at(0) > o.track.update_tick_at(0)
                || (t.track.update_tick_at(0) == o.track.update_tick_at(0)
                    && t.obs.ticks_tracked > o.obs.ticks_tracked);
            let loser = if newer { other } else { id };
            self.tracked.get_mut(&loser).expect("tracked").verdict =
                EntityVerdict::new(EntityKind::Duplicate, "Second entity for the same player");
            by_uuid.insert(uuid, if newer { id } else { other });
        }
        // Same name, different UUIDs: a listed player beats an unlisted entity wearing their name.
        let mut by_name: HashOrder<Text, Vec<i32>> = HashOrder::new();
        for &id in &order {
            let t = self.tracked.get(&id).expect("tracked");
            if t.last_seen != tick || !t.is_player() {
                continue;
            }
            by_name.entry_or_insert_with(cpp::lower(t.track.name()), Vec::new).push(id);
        }
        for (_, group) in by_name.iter() {
            if group.len() < 2 {
                if let Some(&first) = group.first() {
                    let key = &self.tracked.get(&first).expect("tracked").record_key;
                    if let Some(ri) = self.session.find(key) {
                        self.session.player_mut(ri).display_suffix.clear();
                    }
                }
                continue;
            }
            let any_listed = group.iter().any(|id| {
                let f = self.tracked.get(id).expect("tracked").track.cur();
                f.tab_known && f.tab_listed
            });
            for id in group {
                let (collides, key, suffix) = {
                    let t = self.tracked.get(id).expect("tracked");
                    let f = t.track.cur();
                    let suffix =
                        if f.uuid.len() >= 4 { f.uuid[..4].to_vec() } else { cpp::int_text(i64::from(f.entity_id)) };
                    (any_listed && f.tab_known && !f.tab_listed, t.record_key.clone(), suffix)
                };
                if collides {
                    self.tracked.get_mut(id).expect("tracked").verdict =
                        EntityVerdict::new(EntityKind::Npc, "Name collision with a listed player");
                } else if let Some(ri) = self.session.find(&key) {
                    // Two genuine players with one name (rare): keep both, tell them apart.
                    self.session.player_mut(ri).display_suffix = cpp::cat(&[" \u{B7} ".as_bytes(), &suffix]);
                }
            }
        }
        self.filtered.clear();
        for t in self.tracked.values() {
            if t.last_seen != tick || t.is_player() || t.pending {
                continue;
            }
            self.filtered.push(FilteredEntity {
                entity_id: t.track.entity_id(),
                name: t.track.name().to_vec(),
                kind: t.verdict.kind,
                reason: t.verdict.reason.clone(),
            });
        }
        self.stats.filtered_entities = self.filtered.len() as i32;
    }

    /// Records of earlier scenes with nothing to show are dropped once the session grows large.
    fn prune_records(&mut self) {
        if self.session.players().len() <= 400 {
            return;
        }
        let scene = self.match_.scene();
        let marks = &self.marks;
        self.stats.pruned_records += self.session.prune(|r| {
            !r.present
                && !r.local
                && r.scene < scene - 1
                && r.evidence.is_empty()
                && r.detections.is_empty()
                && marks.find(&r.key).is_none()
        });
    }

    fn should_analyze(&self, id: i32) -> bool {
        let t = self.tracked.get(&id).expect("tracked");
        let f = t.track.cur();
        if f.is_local && !self.config.b(self.p.analyze_self) {
            return false;
        }
        if f.is_teammate && self.config.b(self.p.ignore_teammates) && self.context != GameContext::Replay {
            return false;
        }
        if !t.record_key.is_empty() && self.marks.exempt(&t.record_key) {
            return false;
        }
        true
    }

    fn refresh_suspicion(r: &mut PlayerRecord) {
        let mut combined = 1.0f32;
        for cr in &r.checks {
            combined *= 1.0f32 - cr.model.value() / 100.0;
        }
        r.suspicion = 100.0f32 * (1.0f32 - combined);
    }

    /// `recordFor`: the record of a tracked entity, with relevance and presence bookkeeping.
    fn record_for(&mut self, id: i32) -> usize {
        let (key, uuid, name) = {
            let f = self.tracked.get(&id).expect("tracked").track.cur();
            let key = if !f.uuid.is_empty() { f.uuid.clone() } else { cpp::cat(&[b"name:", &f.name]) };
            (key, f.uuid.clone(), f.name.clone())
        };
        self.tracked.get_mut(&id).expect("tracked").record_key = key.clone();
        let ri = self.session.record(&key, &uuid, &name, self.time_ms);
        let scene = self.match_.scene();
        let (time_ms, tick) = (self.time_ms, self.tick);
        let r = self.session.player_mut(ri);
        if r.scene != scene {
            // A queue and the match it became are one game.
            let same_game = r.scene >= 0 && r.scene == self.match_.queue_scene() && scene == self.match_.match_scene();
            if r.scene >= 0 {
                let mut carried = false;
                for cr in &mut r.checks {
                    if cr.model.value() > 0.0 {
                        carried = true;
                    }
                    cr.model.resume(time_ms, if same_game { 1.0 } else { 0.5 });
                }
                Self::refresh_suspicion(r);
                if carried && !same_game {
                    let s = r.suspicion;
                    r.timeline.push_back(TimelineEvent::new(
                        TimelineKind::Carried,
                        time_ms,
                        tick,
                        -1,
                        0,
                        s,
                        b"Carried over from an earlier game (halved)",
                    ));
                }
            }
            r.scene = scene;
            r.participant = false;
            r.scenes_seen += 1;
            // A player the user added by name before Vigil saw them: the mark now follows this account.
            self.marks.bind_name(&r.key, &r.name, &r.uuid);
            self.marks.remember_name(&r.key, &r.name);
        }
        if !r.present {
            r.present = true;
            r.timeline.push_back(TimelineEvent::new(TimelineKind::Seen, time_ms, tick, -1, 0, 0.0, b"Entered view"));
            for cr in &mut r.checks {
                cr.model.resume(time_ms, 1.0);
            }
        }
        let ph = self.match_.phase();
        if ph == GamePhase::Queue || ph == GamePhase::Match || ph == GamePhase::PostGame {
            r.participant = true;
            if ph != GamePhase::Queue {
                r.match_scene = scene;
            }
        }
        let f = self.tracked.get(&id).expect("tracked").track.cur();
        r.entity_id = f.entity_id;
        r.local = f.is_local;
        r.teammate = f.is_teammate;
        r.last_seen_ms = time_ms;
        ri
    }

    fn update_relay_floor(relay_floor: &mut i32, t: &PlayerTrack) {
        let sw = t.swing_ticks();
        if sw.len() < 2 || !t.swing_started() {
            return;
        }
        let interval = (sw[sw.len() - 1] - sw[sw.len() - 2]) as i32;
        if interval <= 0 {
            return;
        }
        if *relay_floor == 0 || interval < *relay_floor {
            *relay_floor = interval;
        }
    }

    fn update_stats(&mut self, id: i32, ri: usize) {
        let tick = self.tick;
        let t = self.tracked.get(&id).expect("tracked");
        let tr = &t.track;
        let s = &mut self.session.player_mut(ri).stats;
        s.ticks_observed += 1;
        if tick - tr.last_swing_tick() < 40 || tick - tr.last_hurt_tick() < 40 {
            s.ticks_in_combat += 1;
        }
        if tr.swing_started() {
            s.swings += 1;
            let count = tr.swing_ticks().iter().rev().take_while(|&&w| tick - w < 20).count() as i32;
            s.cps_recent = f64::from(count);
            s.cps_peak = cpp::max(s.cps_peak, f64::from(count));
        }
        if tr.landed() {
            let seg = tr.air_segments().back().expect("landed");
            if seg.jumped {
                s.jumps += 1;
                s.air_ticks_sum += f64::from(seg.air_ticks);
            }
        }
        if tick % 10 == 0 && tr.size() > 10 && !t.ex.movement() {
            let sp = tr.horizontal_speed(10);
            s.speed_peak = cpp::max(s.speed_peak, sp);
            s.speed_sum += sp;
            s.speed_samples += 1;
            s.speed_recent.push_back(sp as f32);
            while s.speed_recent.len() > 120 {
                s.speed_recent.pop_front();
            }
        }
    }

    fn push_point(h: &mut Vec<ConfidencePoint>, t: f64, v: f32) {
        h.push(ConfidencePoint { time_ms: t, value: v });
        if h.len() > 720 {
            // Keep the whole session visible: halve resolution of the older half.
            let half = h.len() / 2;
            let compact: Vec<ConfidencePoint> =
                h.iter().enumerate().filter(|(i, _)| !(*i < half && i % 2 == 1)).map(|(_, p)| *p).collect();
            *h = compact;
        }
    }

    /// `observing`: checks judge this player, so time without evidence lets confidence decay.
    fn sample_history(&mut self, ri: usize, observing: bool) {
        let (time_ms, tick) = (self.time_ms, self.tick);
        let half_lives: Vec<f32> =
            self.checks.iter().map(|c| c.base.confidence_params(&self.config).half_life_sec).collect();
        let r = self.session.player_mut(ri);
        let mut combined = 1.0f32;
        for (i, cr) in r.checks.iter_mut().enumerate() {
            if observing {
                cr.model.decay_to(time_ms, half_lives[i]);
            } else {
                cr.model.resume(time_ms, 1.0);
            }
            let v = cr.model.value();
            combined *= 1.0f32 - v / 100.0;
            if v > 0.0 || cr.last_sampled > 0.0 {
                if (v - cr.last_sampled).abs() > 0.2f32 || tick % 40 == 0 {
                    Self::push_point(&mut cr.history, time_ms, v);
                    cr.last_sampled = v;
                }
            }
        }
        r.suspicion = 100.0f32 * (1.0f32 - combined);
        r.peak_suspicion = cpp::max(r.peak_suspicion, r.suspicion);
        if r.suspicion > 0.0 || !r.suspicion_history.is_empty() {
            let s = r.suspicion;
            Self::push_point(&mut r.suspicion_history, time_ms, s);
        }
    }

    /// Runs one check call with a fresh context and applies what it queued.
    fn run_check(&mut self, i: usize, call: impl FnOnce(&Check, &mut Ctx<'_>, &HashOrder<i32, Tracked>)) {
        let actions = {
            let c = &self.checks[i];
            let mut ctx = Ctx {
                tick: self.tick,
                time_ms: self.time_ms,
                config: &self.config,
                tracked: &self.tracked,
                local_id: self.local_id,
                relay_floor: self.relay_floor,
                debug_on: c.base.debug(&self.config),
                actions: Vec::new(),
            };
            call(c, &mut ctx, &self.tracked);
            ctx.actions
        };
        for a in actions {
            match a {
                Action::Emit { subject, indicator, severity, summary, detail, values, target } => {
                    self.emit(i, subject, indicator, severity, summary, detail, values, target);
                }
                Action::Debug { subject_name, line } => self.debug(i, &subject_name, line),
            }
        }
    }

    fn take_states(&mut self, id: i32) -> Vec<Box<dyn Any>> {
        std::mem::take(&mut self.tracked.get_mut(&id).expect("tracked").states)
    }

    fn put_states(&mut self, id: i32, states: Vec<Box<dyn Any>>) {
        self.tracked.get_mut(&id).expect("tracked").states = states;
    }

    pub fn on_frame(&mut self, frame: &WorldFrame) {
        let t0 = Instant::now();
        self.tick = frame.tick;
        self.time_ms = frame.time_ms;
        if self.session.start_ms() == 0.0 {
            self.session.reset(frame.time_ms, self.checks.len(), self.clock.now_ms());
        }
        match &self.journal_dir {
            Some(dir) if self.config.b(self.p.journal) => {
                let mut path = dir.clone();
                // snprintf(name, 64, "/session-%llu.jsonl", …)
                path.push(format!("/session-{}.jsonl", self.session.start_epoch_ms()));
                self.session.set_journal(Some(PathBuf::from(path)));
            }
            _ => self.session.set_journal(None),
        }

        if frame.world_changed {
            self.on_world_changed();
        }
        if frame.context != self.context {
            self.context = frame.context;
            self.session.bump();
        }
        self.context_detail = frame.context_detail.clone();
        let phase_update = self.match_.on_frame(frame.context, &frame.chat, self.time_ms);
        if phase_update.scene_changed {
            self.session.bump();
            self.prune_records();
        }
        let phase = self.match_.phase();
        self.recording = self.monitoring_enabled() && self.phase_recorded(phase);
        self.active = self.monitoring_enabled() && self.phase_analyzed(phase);
        self.stats.skipped_ticks += frame.skipped_ticks;
        if frame.skipped_ticks >= 5 {
            self.stall_until = cpp::max(self.stall_until, self.tick + 20);
        }

        // Update tracks (every entity in the player list; classification decides who is a player).
        self.local_id = -1;
        let mut local_name = Text::new();
        let (tick, time_ms) = (self.tick, self.time_ms);
        for pf in &frame.players {
            if pf.entity_id < 0 {
                continue;
            }
            if pf.is_local {
                local_name = pf.name.clone();
            }
            let checks = &self.checks;
            let t = self.tracked.entry_or_insert_with(pf.entity_id, Tracked::default);
            if t.states.is_empty() {
                fresh_states(t, checks);
            }
            // Entity id reuse with a different identity → start fresh.
            if !t.track.is_empty() && !pf.uuid.is_empty() && t.track.cur().uuid != pf.uuid {
                *t = Tracked::default();
                fresh_states(t, checks);
            }
            if t.track.is_empty() {
                t.first_pos = pos(pf);
            }
            t.track.push(pf, tick, time_ms);
            t.last_seen = tick;
            t.obs.ticks_tracked += 1;
            if pf.tab_known {
                t.obs.ticks_unlisted = if pf.tab_listed { 0 } else { t.obs.ticks_unlisted + 1 };
            }
            t.obs.max_displacement = cpp::max(t.obs.max_displacement, (pos(pf) - t.first_pos).length());
            if pf.invisible {
                t.obs.ever_invisible = true;
            }
            if pf.is_local {
                self.local_id = pf.entity_id;
            }
        }
        // Drop entities no longer in the world.
        for id in self.tracked.keys() {
            let t = self.tracked.get(&id).expect("tracked");
            if tick - t.last_seen > 40 {
                if let Some(ri) = self.session.find(&t.record_key) {
                    let r = self.session.player_mut(ri);
                    if r.present {
                        r.present = false;
                        r.timeline.push_back(TimelineEvent::new(
                            TimelineKind::Left,
                            time_ms,
                            tick,
                            -1,
                            0,
                            0.0,
                            b"Left view",
                        ));
                        self.session.bump();
                    }
                }
                self.tracked.remove(&id);
            }
        }
        self.classify_all(&local_name);
        // An entity reclassified as a non-player (e.g. a name collision) leaves the roster.
        for t in self.tracked.values() {
            if t.last_seen != tick || t.is_player() || t.record_key.is_empty() {
                continue;
            }
            if let Some(ri) = self.session.find(&t.record_key) {
                let r = self.session.player_mut(ri);
                if r.present && r.entity_id == t.track.entity_id() {
                    r.present = false;
                    self.session.bump();
                }
            }
        }
        self.stats.tracked_players =
            self.tracked.values().filter(|t| t.last_seen == tick && t.is_player()).count() as i32;

        if !self.recording {
            // Tracks keep updating (cheap, keeps history warm) but nothing is judged or recorded.
            self.recent_combat.clear();
            return;
        }

        self.detect_stall();
        let all: Vec<i32> =
            self.tracked.iter().filter(|(_, t)| t.last_seen == tick && t.is_player()).map(|(&id, _)| id).collect();
        for &id in &all {
            let t = self.tracked.get_mut(&id).expect("tracked");
            Self::compute_exemptions(t, tick, &self.config, &self.p, self.stall_until);
            Self::update_relay_floor(&mut self.relay_floor, &t.track);
        }
        let nearest: Vec<f64> = all
            .iter()
            .map(|id| {
                let p = pos(self.tracked.get(id).expect("tracked").track.cur());
                let mut best = 1e9;
                for o in &all {
                    if o == id {
                        continue;
                    }
                    best = cpp::min(best, (pos(self.tracked.get(o).expect("tracked").track.cur()) - p).length());
                }
                best
            })
            .collect();
        for (id, best) in all.iter().zip(nearest) {
            self.tracked.get_mut(id).expect("tracked").ex.nearest_opponent = best;
        }

        // Records, then checks (only where the phase allows judging).
        for &id in &all {
            let ri = self.record_for(id);
            self.update_stats(id, ri);
            let judged = self.active && self.should_analyze(id);
            if judged {
                let mut states = self.take_states(id);
                for i in 0..self.checks.len() {
                    let c = &self.checks[i];
                    if !c.base.enabled(&self.config) {
                        continue;
                    }
                    if c.base.sampling_on(&self.config) && c.imp.supports_tick_sampling() {
                        let t = &self.tracked.get(&id).expect("tracked").track;
                        if (tick + i64::from(t.entity_id())) % i64::from(c.base.sampling_interval(&self.config)) != 0 {
                            continue;
                        }
                        let local = if self.local_id >= 0 { self.track(self.local_id) } else { None };
                        if let Some(local) = local {
                            if (pos(t.cur()) - pos(local.cur())).length() > c.base.sampling_radius(&self.config) {
                                continue;
                            }
                        }
                    }
                    let state = states[i].as_mut();
                    self.run_check(i, |c, ctx, tracked| {
                        c.imp.on_tick(ctx, &tracked.get(&id).expect("tracked").track, state);
                    });
                }
                self.put_states(id, states);
            }
            if tick % 10 == 0 {
                self.sample_history(ri, judged);
            }
        }

        // Combat events (players only: an NPC never attacks, and hits on NPCs are not evidence).
        let cs = CombatSettings {
            swing_window_ticks: self.config.i(self.p.swing_window),
            settle_ticks: self.config.i(self.p.settle_ticks),
            compensation_ticks: self.config.i(self.p.comp_ticks),
            candidate_range: self.config.value(self.p.combat_range),
        };
        if self.active {
            let tracks: Vec<&PlayerTrack> =
                all.iter().map(|id| &self.tracked.get(id).expect("tracked").track).collect();
            self.combat.update(tick, time_ms, &tracks, &cs);
        } else {
            self.recent_combat.clear();
        }
        let events: Vec<CombatEvent> = if self.active { self.combat.ready().to_vec() } else { Vec::new() };
        for ev in events {
            if !self.tracked.contains_key(&ev.attacker) || !self.tracked.contains_key(&ev.victim) {
                continue;
            }
            self.stats.combat_events += 1;
            self.recent_combat.push(ev.clone());
            if self.recent_combat.len() > 64 {
                self.recent_combat.remove(0);
            }
            let ra = self.record_for(ev.attacker);
            let rv = self.record_for(ev.victim);
            self.session.player_mut(ra).stats.hits_landed += 1;
            self.session.player_mut(rv).stats.hits_taken += 1;
            if ev.certainty >= 0.99 {
                let s = &mut self.session.player_mut(ra).stats;
                s.reach_sum += ev.reach;
                s.reach_samples += 1;
                s.reach_max = cpp::max(s.reach_max, ev.reach);
                s.aim_outside_sum += ev.aim_outside;
                s.reach_recent.push_back(ev.reach as f32);
                while s.reach_recent.len() > 120 {
                    s.reach_recent.pop_front();
                }
            }
            let victim_name = self.tracked.get(&ev.victim).expect("tracked").track.name().to_vec();
            let attacker_name = self.tracked.get(&ev.attacker).expect("tracked").track.name().to_vec();
            self.session.player_mut(ra).timeline.push_back(TimelineEvent::new(
                TimelineKind::Hit,
                ev.time_ms,
                ev.tick,
                -1,
                ev.id,
                ev.reach as f32,
                &cpp::cat(&[b"Hit ", &victim_name]),
            ));
            self.session.player_mut(rv).timeline.push_back(TimelineEvent::new(
                TimelineKind::Hurt,
                ev.time_ms,
                ev.tick,
                -1,
                ev.id,
                ev.reach as f32,
                &cpp::cat(&[b"Hit by ", &attacker_name]),
            ));
            let judge_attacker = self.should_analyze(ev.attacker);
            let judge_victim = self.should_analyze(ev.victim);
            let mut att_states = self.take_states(ev.attacker);
            let mut vic_states = self.take_states(ev.victim);
            for i in 0..self.checks.len() {
                if !self.checks[i].base.enabled(&self.config) {
                    continue;
                }
                if judge_attacker {
                    let state = att_states[i].as_mut();
                    self.run_check(i, |c, ctx, tracked| {
                        let a = &tracked.get(&ev.attacker).expect("tracked").track;
                        let v = &tracked.get(&ev.victim).expect("tracked").track;
                        c.imp.on_combat(ctx, &ev, a, v, state);
                    });
                }
                // Some checks (Velocity) judge the victim's response to the hit.
                if judge_victim {
                    let state = vic_states[i].as_mut();
                    self.run_check(i, |c, ctx, tracked| {
                        let a = &tracked.get(&ev.attacker).expect("tracked").track;
                        let v = &tracked.get(&ev.victim).expect("tracked").track;
                        c.imp.on_hit_taken(ctx, &ev, a, v, state);
                    });
                }
            }
            self.put_states(ev.victim, vic_states);
            self.put_states(ev.attacker, att_states);
        }

        // Bound timelines.
        for r in self.session.players_mut() {
            while r.timeline.len() > 1500 {
                r.timeline.pop_front();
            }
        }

        if self.time_ms - self.last_history_flush_ms > 60000.0 {
            self.last_history_flush_ms = self.time_ms;
            self.flush_history();
        }

        let micros = t0.elapsed().as_secs_f64() * 1e6;
        self.stats.ticks_processed += 1;
        self.stats.avg_tick_micros =
            if self.stats.avg_tick_micros == 0.0 { micros } else { self.stats.avg_tick_micros * 0.98 + micros * 0.02 };
        self.stats.peak_tick_micros = cpp::max(self.stats.peak_tick_micros * 0.999, micros);
    }

    /// `Engine::emit`, applied when the emitting check returns (check.rs, H11).
    #[allow(clippy::too_many_arguments)]
    fn emit(
        &mut self,
        check: usize,
        subject: i32,
        indicator: i32,
        severity: f32,
        summary: Text,
        detail: Text,
        values: Vec<EvidenceValue>,
        target: Text,
    ) {
        if !self.checks[check].base.indicator_on(&self.config, indicator) {
            return;
        }
        if !self.tracked.contains_key(&subject) {
            return;
        }
        let ri = self.record_for(subject);
        let (time_ms, tick) = (self.time_ms, self.tick);
        let base = &self.checks[check].base;
        let params = base.confidence_params(&self.config);
        let weight = base.indicator_weight(&self.config, indicator);
        let indicators = base.indicators.len();
        let c_index = base.index;
        let c_debug = base.debug(&self.config);
        let ind_id = base.indicators[indicator as usize].id;
        let (c_name, c_summary) = (base.name, base.summary);
        let ev_id = self.session.next_evidence_id();

        let r = self.session.player_mut(ri);
        let cr = &mut r.checks[check];
        if cr.indicator_counts.len() < indicators {
            cr.indicator_counts.resize(indicators, 0);
        }
        let severity = cpp::clamp(severity, 0.0, 1.0);
        let contribution = cr.model.add(time_ms, weight, severity, indicator, &params);
        let confidence_after = cr.model.value();
        let envelope = derive_envelope(&values);
        let ev = Evidence {
            id: ev_id,
            tick,
            time_ms,
            check: c_index,
            indicator,
            weight,
            severity,
            contribution,
            confidence_after,
            summary,
            detail,
            target,
            values,
            envelope,
        };
        cr.evidence += 1;
        cr.indicator_counts[indicator as usize] += 1;
        cr.last_evidence_ms = time_ms;
        cr.peak = cpp::max(cr.peak, cr.model.value());
        r.timeline.push_back(TimelineEvent::new(
            TimelineKind::Evidence,
            time_ms,
            tick,
            c_index,
            ev.id,
            confidence_after,
            &ev.summary,
        ));
        self.session.count_evidence();

        if c_debug {
            let subject_name = self.tracked.get(&subject).expect("tracked").track.name().to_vec();
            let line = fmt!(
                "evidence %s sev=%.2f +%.1f → %.1f%% | %s",
                ind_id,
                severity,
                contribution,
                confidence_after,
                &ev.summary
            );
            self.debug(check, &subject_name, line);
        }

        if self.session.journal_path().is_some() {
            let r = self.session.player(ri);
            let mut j = Value::object();
            j.set("type", Value::from("evidence"));
            j.set("time", Value::Number(time_ms));
            j.set("player", Value::from(&r.name));
            j.set("uuid", Value::from(&r.uuid));
            j.set("check", Value::from(self.checks[check].base.id));
            j.set("indicator", Value::from(ind_id));
            j.set("severity", Value::Number(f64::from(severity)));
            j.set("confidence", Value::Number(f64::from(confidence_after)));
            j.set("summary", Value::from(&ev.summary));
            let mut vals = Value::object();
            for v in &ev.values {
                vals.set(&v.name, Value::Number(v.value));
            }
            j.set("values", vals);
            self.session.journal(&json::write(&j, -1));
        }

        let r = self.session.player_mut(ri);
        r.evidence.push_back(ev);
        while r.evidence.len() > 600 {
            r.evidence.pop_front();
        }

        let cr = &mut r.checks[check];
        if cr.model.can_detect(time_ms, &params) {
            cr.model.mark_detected(time_ms);
            let confidence = cr.model.value();
            let (count, distinct) = cr.model.gate(time_ms, &params);
            let window_ms = f64::from(params.window_sec) * 1000.0;
            let mut ids = Vec::new();
            let mut reasons: Vec<Text> = Vec::new();
            for e in r.evidence.iter().rev() {
                if time_ms - e.time_ms > window_ms {
                    break;
                }
                if e.check != c_index {
                    continue;
                }
                ids.push(e.id);
                if reasons.len() < 4 {
                    reasons.push(e.summary.clone());
                }
            }
            let summary_text = cpp::cat(&[
                c_name.as_bytes(),
                " \u{B7} ".as_bytes(),
                &fmt!("%.0f%%", confidence),
                " \u{B7} ".as_bytes(),
                &fmt!("%d observations, %d indicator%s", count, distinct, if distinct == 1 { "" } else { "s" }),
            ]);
            r.checks[check].detections += 1;
            let det_id = self.session.next_detection_id();
            self.session.count_detection();
            let r = self.session.player_mut(ri);
            let d = Detection {
                id: det_id,
                tick,
                time_ms,
                check: c_index,
                confidence,
                evidence_count: count,
                distinct_indicators: distinct,
                evidence: ids,
                summary: summary_text,
            };
            r.timeline.push_back(TimelineEvent::new(
                TimelineKind::Detection,
                time_ms,
                tick,
                c_index,
                d.id,
                d.confidence,
                &d.summary,
            ));
            r.detections.push(d);
            self.alert_seq += 1;
            let a = Alert {
                seq: self.alert_seq,
                time_ms,
                player_key: r.key.clone(),
                name: r.name.clone(),
                check: c_index,
                confidence,
                summary: reasons.first().cloned().unwrap_or_else(|| c_summary.as_bytes().to_vec()),
                reasons,
            };
            let (name, uuid, alert_summary) = (r.name.clone(), r.uuid.clone(), a.summary.clone());
            self.alerts.push_back(a);
            while self.alerts.len() > 64 {
                self.alerts.pop_front();
            }
            let c_id = self.checks[check].base.id;
            self.log_line(
                "DETECTION %s %s %.0f%% (%d evidence, %d indicators) \u{2014} %s",
                &[
                    Arg::S(&name),
                    Arg::S(c_id.as_bytes()),
                    Arg::from(confidence),
                    Arg::from(count),
                    Arg::from(distinct),
                    Arg::S(&alert_summary),
                ],
            );
            if self.session.journal_path().is_some() {
                let mut j = Value::object();
                j.set("type", Value::from("detection"));
                j.set("time", Value::Number(time_ms));
                j.set("player", Value::from(&name));
                j.set("uuid", Value::from(&uuid));
                j.set("check", Value::from(c_id));
                j.set("confidence", Value::Number(f64::from(confidence)));
                j.set("evidence", Value::from(count));
                self.session.journal(&json::write(&j, -1));
            }
        }
        self.session.bump();
    }

    /// `Engine::debug`.
    fn debug(&mut self, check: usize, player: &[u8], line: Text) {
        if !self.checks[check].base.debug(&self.config) {
            return;
        }
        self.debug_log.push_back(DebugLine {
            time_ms: self.time_ms,
            tick: self.tick,
            check: self.checks[check].base.index,
            player: player.to_vec(),
            text: line,
        });
        while self.debug_log.len() > 2000 {
            self.debug_log.pop_front();
        }
        if self.log.0.is_some() {
            let id = self.checks[check].base.id;
            let text = self.debug_log.back().expect("just pushed").text.clone();
            self.log_line("[%s] %s: %s", &[Arg::S(id.as_bytes()), Arg::S(player), Arg::S(&text)]);
        }
    }
}

/// `Engine::registerGlobalParams`.
fn register_global_params(s: &mut SchemaBuilder) -> GlobalParams {
    let mut p = GlobalParams {
        monitoring: s.add_bool(
            "general.monitoring",
            "Monitoring",
            true,
            "Master switch. When off, Vigil observes nothing and all checks are idle.",
            "Monitoring",
            false,
        ),
        run_in_game: s.add_bool(
            "general.runIn.game",
            "In games",
            true,
            "Analyze players during matches (Bed Wars, Duels, SkyWars and similar).",
            "Where checks run",
            false,
        ),
        run_replay: s.add_bool(
            "general.runIn.replay",
            "Replays & Atlas",
            true,
            "Analyze players while watching Hypixel replays or reviewing Atlas reports.",
            "Where checks run",
            false,
        ),
        ..GlobalParams::default()
    };
    p.run_pregame = s.add_bool(
        "general.runIn.pregame",
        "Pre-game queues",
        false,
        "Analyze players while waiting for a match to start. They are always listed under This game; this decides whether checks judge them before the match.",
        "Where checks run",
        false,
    );
    p.run_lobby = s.add_bool(
        "general.runIn.lobby",
        "Main lobbies",
        false,
        "Analyze players in main lobbies. Lobbies contain parkour, launch pads and other mechanics that resemble cheats.",
        "Where checks run",
        false,
    );
    s.warn(
        p.run_lobby,
        "Lobby mechanics (launch pads, parkour, double jump) produce movement that resembles cheats.",
        |v| v.b(),
    );
    p.analyze_self = s.add_bool(
        "general.analyzeSelf",
        "Analyze yourself",
        false,
        "Runs every check on your own player. Use it to calibrate thresholds: anything Vigil reports about you while playing legitimately is a false positive.",
        "Calibration",
        false,
    );
    p.ignore_teammates = s.add_bool(
        "exempt.teammates",
        "Ignore teammates",
        true,
        "Skip players on your team (resolved from scoreboard team colors).",
        "Exemptions",
        false,
    );
    p.kb_ticks = s.add_int(
        "exempt.knockbackTicks",
        "Knockback grace",
        30,
        range(0.0, 100.0, 1.0, 0, "ticks"),
        "After a player takes damage, movement checks ignore them for this long. Fireballs and TNT can launch players far beyond normal speed.",
        "Exemptions",
        false,
    );
    s.warn(p.kb_ticks, "A short grace lets fireball/TNT knockback count as speed.", |v| v.i() < 15);
    p.teleport_ticks = s.add_int(
        "exempt.teleportTicks",
        "Teleport grace",
        20,
        range(0.0, 100.0, 1.0, 0, "ticks"),
        "Movement checks ignore a player for this long after a teleport (respawn, pearl, server correction).",
        "Exemptions",
        true,
    );
    p.ice_ticks = s.add_int(
        "exempt.iceTicks",
        "Ice grace",
        40,
        range(0.0, 200.0, 1.0, 0, "ticks"),
        "Speed-related checks ignore a player for this long after touching ice or packed ice.",
        "Exemptions",
        true,
    );
    p.fresh_ticks = s.add_int(
        "exempt.freshTicks",
        "Warm-up",
        20,
        range(0.0, 100.0, 1.0, 0, "ticks"),
        "Players are observed for this long before any check judges them.",
        "Exemptions",
        true,
    );
    p.swing_window = s.add_int(
        "combat.swingWindow",
        "Swing \u{2192} hurt window",
        2,
        range(0.0, 6.0, 1.0, 0, "ticks"),
        "How many ticks may separate an attacker's swing from the victim's hurt animation for the hit to be attributed.",
        "Hit attribution",
        true,
    );
    p.settle_ticks = s.add_int(
        "combat.settleTicks",
        "Settle delay",
        3,
        range(0.0, 10.0, 1.0, 0, "ticks"),
        "Hits are evaluated after this delay so late position and rotation packets are included.",
        "Hit attribution",
        true,
    );
    p.comp_ticks = s.add_int(
        "combat.compensationTicks",
        "Latency compensation",
        5,
        range(0.0, 12.0, 1.0, 0, "ticks"),
        "How far back the victim's positions are considered when measuring reach and aim. Covers the attacker's ping and entity interpolation.",
        "Hit attribution",
        true,
    );
    s.warn(
        p.comp_ticks,
        "Low compensation measures against positions the attacker may never have seen; laggy players will look like they reach.",
        |v| v.i() < 3,
    );
    p.combat_range = s.add_float(
        "combat.range",
        "Candidate range",
        6.0,
        range(3.5, 10.0, 0.5, 1, "blocks"),
        "Swinging players farther than this from the victim are not considered as the attacker.",
        "Hit attribution",
        true,
    );
    p.journal = s.add_bool(
        "data.journal",
        "Session journal",
        true,
        "Write every detection and its evidence to sessions/ as JSON lines.",
        "Data",
        false,
    );
    p.history = s.add_bool(
        "data.history",
        "Player history",
        true,
        "Remember players with evidence across sessions (name, sessions, detections, peak suspicion) in history/players.json.",
        "Data",
        false,
    );
    p
}
