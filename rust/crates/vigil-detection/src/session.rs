//! `src/detection/session.{h,cpp}`: player records, timelines, behavior statistics.
//!
//! P3A ports the in-memory session the engine decides with. Cross-session history files and the
//! journal are host persistence sinks (write-only, never read by a decision) and come in P3B; the
//! engine is driven without them, exactly as `VigilReplay` and `VigilParity` drive the C++ engine.

use std::collections::VecDeque;

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

#[derive(Clone, Debug)]
pub struct Session {
    players: Vec<PlayerRecord>,
    index: FastMap<Text, usize>,
    check_count: usize,
    start_ms: f64,
    total_evidence: i32,
    total_detections: i32,
    next_evidence_id: u64,
    next_detection_id: u64,
    revision: u64,
}

impl Default for Session {
    fn default() -> Self {
        Session {
            players: Vec::new(),
            index: FastMap::default(),
            check_count: 0,
            start_ms: 0.0,
            total_evidence: 0,
            total_detections: 0,
            next_evidence_id: 1,
            next_detection_id: 1,
            revision: 1,
        }
    }
}

impl Session {
    /// `reset` (the epoch stamp of the C++ session only names its journal file: P3B).
    pub fn reset(&mut self, now_ms: f64, check_count: usize) {
        self.players.clear();
        self.index.clear();
        self.check_count = check_count;
        self.start_ms = now_ms;
        self.total_evidence = 0;
        self.total_detections = 0;
        self.revision += 1;
    }

    /// The record for `key` (created on first sight). Returns its index.
    pub fn record(&mut self, key: &[u8], uuid: &[u8], name: &[u8], now_ms: f64) -> usize {
        if let Some(&i) = self.index.get(key) {
            if !name.is_empty() {
                self.players[i].name = name.to_vec();
            }
            return i;
        }
        let r = PlayerRecord {
            key: key.to_vec(),
            uuid: uuid.to_vec(),
            name: if name.is_empty() { key.to_vec() } else { name.to_vec() },
            first_seen_ms: now_ms,
            last_seen_ms: now_ms,
            checks: vec![CheckRecord::default(); self.check_count],
            ..PlayerRecord::default()
        };
        // applyHistory: no history file is loaded in P3A (P3B adds the history sink).
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
