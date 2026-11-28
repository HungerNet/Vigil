//! `src/detection/session.{h,cpp}`: player records, timelines, behavior statistics, the
//! cross-session history and the session journal.
//!
//! The history (`history/players.json`) and the journal (`sessions/session-<epoch>.jsonl`) are
//! persistence sinks: the history only fills a record's `prior*` fields (shown by the UI and saved
//! in profiles), the journal is write-only. Neither influences a decision. Both write the C++
//! bytes (P3B_PLAN.md §3.5).

use std::collections::{BTreeMap, VecDeque};
use std::path::{Path, PathBuf};

use vigil_formats::json::{self, Value};

use crate::cpp;
use crate::evidence::{ConfidenceModel, Detection, Evidence, Text};
use crate::hashorder::FastMap;
use crate::types::Tick;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ConfidencePoint {
    pub time_ms: f64,
    pub value: f32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
#[repr(u8)]
pub enum TimelineKind {
    #[default]
    Seen = 0,
    Left = 1,
    Evidence = 2,
    Detection = 3,
    Hit = 4,
    Hurt = 5,
    Carried = 6,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct TimelineEvent {
    pub kind: TimelineKind,
    pub time_ms: f64,
    pub tick: Tick,
    pub check: i32,
    pub reference: u64,
    pub value: f32,
    pub text: Text,
}

impl TimelineEvent {
    pub fn new(
        kind: TimelineKind,
        time_ms: f64,
        tick: Tick,
        check: i32,
        reference: u64,
        value: f32,
        text: &[u8],
    ) -> Self {
        TimelineEvent { kind, time_ms, tick, check, reference, value, text: text.to_vec() }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct CheckRecord {
    pub model: ConfidenceModel,
    pub peak: f32,
    pub evidence: i32,
    pub detections: i32,
    pub last_evidence_ms: f64,
    pub indicator_counts: Vec<i32>,
    pub history: Vec<ConfidencePoint>,
    pub last_sampled: f32,
}

impl Default for CheckRecord {
    fn default() -> Self {
        CheckRecord {
            model: ConfidenceModel::default(),
            peak: 0.0,
            evidence: 0,
            detections: 0,
            last_evidence_ms: 0.0,
            indicator_counts: Vec::new(),
            history: Vec::new(),
            last_sampled: -1.0,
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct BehaviorStats {
    pub ticks_observed: i32,
    pub ticks_in_combat: i32,
    pub hits_landed: i32,
    pub hits_taken: i32,
    pub swings: i32,
    pub reach_sum: f64,
    pub reach_samples: i32,
    pub reach_max: f64,
    pub reach_recent: VecDeque<f32>,
    pub aim_outside_sum: f64,
    pub cps_peak: f64,
    pub cps_recent: f64,
    pub speed_peak: f64,
    pub speed_sum: f64,
    pub speed_samples: i32,
    pub jumps: i32,
    pub air_ticks_sum: f64,
    pub speed_recent: VecDeque<f32>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct PlayerRecord {
    pub key: Text,
    pub uuid: Text,
    pub name: Text,
    pub entity_id: i32,
    pub present: bool,
    pub local: bool,
    pub teammate: bool,
    pub first_seen_ms: f64,
    pub last_seen_ms: f64,
    pub checks: Vec<CheckRecord>,
    pub evidence: VecDeque<Evidence>,
    pub detections: Vec<Detection>,
    pub timeline: VecDeque<TimelineEvent>,
    pub stats: BehaviorStats,
    pub suspicion: f32,
    pub peak_suspicion: f32,
    pub suspicion_history: Vec<ConfidencePoint>,
    pub scene: i32,
    pub match_scene: i32,
    pub scenes_seen: i32,
    pub participant: bool,
    pub display_suffix: Text,
    pub prior_sessions: i32,
    pub prior_detections: i32,
    pub prior_peak: f32,
    pub last_seen_epoch_ms: f64,
}

impl Default for PlayerRecord {
    fn default() -> Self {
        PlayerRecord {
            key: Text::new(),
            uuid: Text::new(),
            name: Text::new(),
            entity_id: -1,
            present: false,
            local: false,
            teammate: false,
            first_seen_ms: 0.0,
            last_seen_ms: 0.0,
            checks: Vec::new(),
            evidence: VecDeque::new(),
            detections: Vec::new(),
            timeline: VecDeque::new(),
            stats: BehaviorStats::default(),
            suspicion: 0.0,
            peak_suspicion: 0.0,
            suspicion_history: Vec::new(),
            scene: -1,
            match_scene: -1,
            scenes_seen: 0,
            participant: false,
            display_suffix: Text::new(),
            prior_sessions: 0,
            prior_detections: 0,
            prior_peak: 0.0,
            last_seen_epoch_ms: 0.0,
        }
    }
}

impl PlayerRecord {
    /// Index of the highest-confidence check (above 0.5), -1 if none.
    pub fn top_check(&self) -> i32 {
        let mut best = -1;
        let mut best_value = 0.5f32;
        for (i, c) in self.checks.iter().enumerate() {
            if c.model.value() > best_value {
                best_value = c.model.value();
                best = i as i32;
            }
        }
        best
    }
}

#[derive(Clone, Debug, PartialEq, Default)]
pub struct DebugLine {
    pub time_ms: f64,
    pub tick: Tick,
    pub check: i32,
    pub player: Text,
    pub text: Text,
}

#[derive(Clone, Debug, PartialEq, Default)]
pub struct Alert {
    pub seq: u64,
    pub time_ms: f64,
    pub player_key: Text,
    pub name: Text,
    pub check: i32,
    pub confidence: f32,
    pub summary: Text,
    pub reasons: Vec<Text>,
}

/// One player's entry in the cross-session history (`Session::HistoryEntry`).
#[derive(Clone, Debug, Default, PartialEq)]
pub struct HistoryEntry {
    pub name: Text,
    pub sessions: i32,
    pub detections: i32,
    pub peak: f32,
    pub last_seen: f64,
}

#[derive(Clone, Debug)]
pub struct Session {
    players: Vec<PlayerRecord>,
    index: FastMap<Text, usize>,
    /// `std::map<std::string, HistoryEntry>`: key order.
    history: BTreeMap<Text, HistoryEntry>,
    check_count: usize,
    start_ms: f64,
    start_epoch_ms: u64,
    total_evidence: i32,
    total_detections: i32,
    next_evidence_id: u64,
    next_detection_id: u64,
    revision: u64,
    journal_path: Option<PathBuf>,
}

impl Default for Session {
    fn default() -> Self {
        Session {
            players: Vec::new(),
            index: FastMap::default(),
            history: BTreeMap::new(),
            check_count: 0,
            start_ms: 0.0,
            start_epoch_ms: 0,
            total_evidence: 0,
            total_detections: 0,
            next_evidence_id: 1,
            next_detection_id: 1,
            revision: 1,
            journal_path: None,
        }
    }
}

impl Session {
    /// `reset`. `epoch_ms` is the wall clock at the reset (`system_clock` in C++), which names the
    /// session's journal file.
    pub fn reset(&mut self, now_ms: f64, check_count: usize, epoch_ms: f64) {
        self.players.clear();
        self.index.clear();
        self.check_count = check_count;
        self.start_ms = now_ms;
        // static_cast<std::uint64_t>(milliseconds.count()): an int64 count, taken modulo 2^64.
        self.start_epoch_ms = epoch_ms as i64 as u64;
        self.total_evidence = 0;
        self.total_detections = 0;
        self.revision += 1;
    }

    pub fn start_epoch_ms(&self) -> u64 {
        self.start_epoch_ms
    }

    // ── Cross-session history ────────────────────────────────────────────────────────────────

    /// `loadHistory` on a file with these bytes (`None`: the file could not be opened). Anything
    /// that does not parse leaves the history empty; entries convert as C++ (`static_cast`).
    pub fn load_history(&mut self, bytes: Option<&[u8]>) {
        self.history.clear();
        let Some(bytes) = bytes else { return };
        let Ok(root) = json::parse(bytes) else { return };
        for (key, v) in root.get("players").members() {
            let e = HistoryEntry {
                name: v.get("name").as_text(),
                sessions: cpp::to_int(v.get("sessions").as_number()),
                detections: cpp::to_int(v.get("detections").as_number()),
                peak: v.get("peak").as_number() as f32,
                last_seen: v.get("lastSeen").as_number(),
            };
            self.history.insert(key.clone(), e);
        }
    }

    pub fn history(&self) -> &BTreeMap<Text, HistoryEntry> {
        &self.history
    }

    /// `applyHistory`: a new record learns what earlier sessions saw of the same key.
    fn apply_history(&self, r: &mut PlayerRecord) {
        let Some(e) = self.history.get(&r.key) else { return };
        r.prior_sessions = e.sessions;
        r.prior_detections = e.detections;
        r.prior_peak = e.peak;
        r.last_seen_epoch_ms = e.last_seen;
    }

    /// `saveHistory`'s document: the loaded history merged with this session's players that have
    /// evidence (the local player excluded), `now_epoch_ms` as their last sighting.
    pub fn history_json(&self, now_epoch_ms: f64) -> Vec<u8> {
        let mut merged = self.history.clone();
        for r in &self.players {
            if r.local || r.evidence.is_empty() {
                continue;
            }
            let base = self.history.get(&r.key).cloned().unwrap_or_default();
            let e = merged.entry(r.key.clone()).or_default();
            e.name = r.name.clone();
            e.sessions = base.sessions.wrapping_add(1);
            e.detections = base.detections.wrapping_add(r.detections.len() as i32);
            e.peak = cpp::max(base.peak, r.peak_suspicion);
            e.last_seen = now_epoch_ms;
        }
        let mut root = Value::object();
        root.set("format", Value::from(1));
        let mut players = Value::object();
        for (key, e) in &merged {
            let mut p = Value::object();
            p.set("name", Value::from(&e.name));
            p.set("sessions", Value::from(e.sessions));
            p.set("detections", Value::from(e.detections));
            p.set("peak", Value::Number(f64::from(e.peak)));
            p.set("lastSeen", Value::Number(e.last_seen));
            players.set(key, p);
        }
        root.set("players", players);
        json::write(&root, 1)
    }

    /// `saveHistory`: writes [`Session::history_json`] (directories created; not atomic, as C++).
    pub fn save_history(&self, path: &Path, now_epoch_ms: f64) {
        if let Some(parent) = path.parent() {
            let _ = std::fs::create_dir_all(parent);
        }
        let _ = std::fs::write(path, self.history_json(now_epoch_ms));
    }

    // ── Journal ──────────────────────────────────────────────────────────────────────────────

    /// `setJournal` (`None`: no journal).
    pub fn set_journal(&mut self, path: Option<PathBuf>) {
        self.journal_path = path;
    }
    pub fn journal_path(&self) -> Option<&Path> {
        self.journal_path.as_deref()
    }

    /// `journal`: appends one JSON line.
    pub fn journal(&self, line: &[u8]) {
        let Some(path) = &self.journal_path else { return };
        if let Some(parent) = path.parent() {
            let _ = std::fs::create_dir_all(parent);
        }
        use std::io::Write;
        if let Ok(mut out) = std::fs::OpenOptions::new().create(true).append(true).open(path) {
            let mut l = line.to_vec();
            l.push(b'\n');
            let _ = out.write_all(&l);
        }
    }

    /// The record for `key` (created on first sight). Returns its index.
    pub fn record(&mut self, key: &[u8], uuid: &[u8], name: &[u8], now_ms: f64) -> usize {
        if let Some(&i) = self.index.get(key) {
            if !name.is_empty() {
                self.players[i].name = name.to_vec();
            }
            return i;
        }
        let mut r = PlayerRecord {
            key: key.to_vec(),
            uuid: uuid.to_vec(),
            name: if name.is_empty() { key.to_vec() } else { name.to_vec() },
            first_seen_ms: now_ms,
            last_seen_ms: now_ms,
            checks: vec![CheckRecord::default(); self.check_count],
            ..PlayerRecord::default()
        };
        self.apply_history(&mut r);
        self.players.push(r);
        let i = self.players.len() - 1;
        self.index.insert(key.to_vec(), i);
        self.revision += 1;
        i
    }

    pub fn find(&self, key: &[u8]) -> Option<usize> {
        self.index.get(key).copied()
    }

    pub fn player(&self, i: usize) -> &PlayerRecord {
        &self.players[i]
    }
    pub fn player_mut(&mut self, i: usize) -> &mut PlayerRecord {
        &mut self.players[i]
    }
    pub fn players(&self) -> &[PlayerRecord] {
        &self.players
    }
    pub fn players_mut(&mut self) -> &mut [PlayerRecord] {
        &mut self.players
    }

    /// Removes records for which `drop` returns true; returns how many were removed.
    pub fn prune(&mut self, drop: impl Fn(&PlayerRecord) -> bool) -> i32 {
        let before = self.players.len();
        self.players.retain(|r| !drop(r));
        if self.players.len() == before {
            return 0;
        }
        self.index.clear();
        for (i, r) in self.players.iter().enumerate() {
            self.index.insert(r.key.clone(), i);
        }
        self.revision += 1;
        (before - self.players.len()) as i32
    }

    pub fn start_ms(&self) -> f64 {
        self.start_ms
    }
    pub fn total_evidence(&self) -> i32 {
        self.total_evidence
    }
    pub fn total_detections(&self) -> i32 {
        self.total_detections
    }
    pub fn count_evidence(&mut self) {
        self.total_evidence += 1;
    }
    pub fn count_detection(&mut self) {
        self.total_detections += 1;
    }
    pub fn next_evidence_id(&mut self) -> u64 {
        let id = self.next_evidence_id;
        self.next_evidence_id += 1;
        id
    }
    pub fn next_detection_id(&mut self) -> u64 {
        let id = self.next_detection_id;
        self.next_detection_id += 1;
        id
    }
    pub fn revision(&self) -> u64 {
        self.revision
    }
    pub fn bump(&mut self) {
        self.revision += 1;
    }
}
