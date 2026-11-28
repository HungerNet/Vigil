//! `src/detection/snapshot.{h,cpp}`: saved profiles — capture from a live record, and the store
//! the Records page saves to, exports from and imports into (P3B_PLAN.md §3.4).
//!
//! The file format itself (`vigil-profile` 1, bounded parsing, checks by id) is
//! `vigil_formats::profile` (P2). This module adds what the engine side does: the engine's check
//! catalog, [`capture`] (a record frozen at the moment the user saves it, with the user's marks),
//! and [`SnapshotStore`] with the C++ file operations, bounds and ordering.
//!
//! **One deliberate difference** (P3B report §7): C++ names a saved profile after its save time with
//! `localtime_s`, which terminates the process for a time it cannot represent (a hostile imported
//! file with `"savedAt": 1e300`). Here such a time is treated like a missing one — the current time
//! is used, as C++ already does for a time ≤ 0.

use std::path::{Path, PathBuf};

use vigil_formats::catalog::CheckCatalog;
use vigil_formats::profile::{self, Profile};

use crate::clock::{Clock, LocalTime, stamp_compact, to_time_t};
use crate::engine::Engine;
use crate::evidence::{Role, Text};
use crate::msvcsort;
use crate::session::{ConfidencePoint, PlayerRecord, TimelineKind};

/// `ProfileSnapshot::kMaxFileBytes`.
pub const MAX_FILE_BYTES: u64 = profile::MAX_FILE_BYTES as u64;
/// `refresh` lists at most this many profiles.
pub const MAX_LISTED: usize = 500;

/// `CheckCatalog::of`: check and indicator ids in index order.
pub fn catalog_of(e: &Engine) -> CheckCatalog {
    CheckCatalog {
        checks: e.checks().iter().map(|c| c.base.id.as_bytes().to_vec()).collect(),
        indicators: e
            .checks()
            .iter()
            .map(|c| c.base.indicators.iter().map(|i| i.id.as_bytes().to_vec()).collect())
            .collect(),
    }
}

fn points(p: &[ConfidencePoint]) -> Vec<profile::Point> {
    p.iter().map(|p| profile::Point { time_ms: p.time_ms, value: p.value }).collect()
}

fn kind(k: TimelineKind) -> profile::TimelineKind {
    match k {
        TimelineKind::Seen => profile::TimelineKind::Seen,
        TimelineKind::Left => profile::TimelineKind::Left,
        TimelineKind::Evidence => profile::TimelineKind::Evidence,
        TimelineKind::Detection => profile::TimelineKind::Detection,
        TimelineKind::Hit => profile::TimelineKind::Hit,
        TimelineKind::Hurt => profile::TimelineKind::Hurt,
        TimelineKind::Carried => profile::TimelineKind::Carried,
    }
}

/// The engine's record as a profile record (what `snapshotToJson` reads from a `PlayerRecord`).
pub fn profile_record(r: &PlayerRecord) -> profile::PlayerRecord {
    let s = &r.stats;
    profile::PlayerRecord {
        key: r.key.clone(),
        uuid: r.uuid.clone(),
        name: r.name.clone(),
        local: r.local,
        teammate: r.teammate,
        first_seen_ms: r.first_seen_ms,
        last_seen_ms: r.last_seen_ms,
        suspicion: r.suspicion,
        peak_suspicion: r.peak_suspicion,
        prior_sessions: r.prior_sessions,
        prior_detections: r.prior_detections,
        prior_peak: r.prior_peak,
        suspicion_history: points(&r.suspicion_history),
        checks: r
            .checks
            .iter()
            .map(|c| profile::CheckRecord {
                confidence: c.model.value(),
                peak: c.peak,
                evidence: c.evidence,
                detections: c.detections,
                indicator_counts: c.indicator_counts.clone(),
                history: points(&c.history),
            })
            .collect(),
        evidence: r
            .evidence
            .iter()
            .map(|e| profile::Evidence {
                id: e.id,
                tick: e.tick,
                time_ms: e.time_ms,
                check: e.check as usize,
                indicator: e.indicator as usize,
                weight: e.weight,
                severity: e.severity,
                contribution: e.contribution,
                confidence_after: e.confidence_after,
                summary: e.summary.clone(),
                detail: e.detail.clone(),
                target: e.target.clone(),
                values: e
                    .values
                    .iter()
                    .map(|v| profile::EvidenceValue {
                        name: v.name.clone(),
                        value: v.value,
                        unit: v.unit.clone(),
                        limit: v.limit,
                        has_limit: v.has_limit,
                        role: match v.role {
                            Role::Measure => profile::Role::Measure,
                            Role::Expected => profile::Role::Expected,
                            Role::Context => profile::Role::Context,
                        },
                    })
                    .collect(),
            })
            .collect(),
        detections: r
            .detections
            .iter()
            .map(|d| profile::Detection {
                id: d.id,
                tick: d.tick,
                time_ms: d.time_ms,
                check: d.check as usize,
                confidence: d.confidence,
                evidence_count: d.evidence_count,
                distinct_indicators: d.distinct_indicators,
                evidence: d.evidence.clone(),
                summary: d.summary.clone(),
            })
            .collect(),
        timeline: r
            .timeline
            .iter()
            .map(|t| profile::TimelineEvent {
                kind: kind(t.kind),
                time_ms: t.time_ms,
                tick: t.tick,
                check: t.check,
                reference: t.reference,
                value: t.value,
                text: t.text.clone(),
            })
            .collect(),
        stats: profile::Stats {
            ticks_observed: s.ticks_observed,
            ticks_in_combat: s.ticks_in_combat,
            hits_landed: s.hits_landed,
            hits_taken: s.hits_taken,
            swings: s.swings,
            reach_sum: s.reach_sum,
            reach_samples: s.reach_samples,
            reach_max: s.reach_max,
            aim_outside_sum: s.aim_outside_sum,
            cps_peak: s.cps_peak,
            speed_peak: s.speed_peak,
            speed_sum: s.speed_sum,
            speed_samples: s.speed_samples,
            jumps: s.jumps,
            air_ticks_sum: s.air_ticks_sum,
            reach_recent: s.reach_recent.iter().copied().collect(),
            speed_recent: s.speed_recent.iter().copied().collect(),
        },
    }
}

/// A profile observation's values as engine values (what `snapshotFromJson` derives the envelope
/// from).
pub fn evidence_values(e: &profile::Evidence) -> Vec<crate::evidence::EvidenceValue> {
    e.values
        .iter()
        .map(|v| crate::evidence::EvidenceValue {
            name: v.name.clone(),
            value: v.value,
            unit: v.unit.clone(),
            limit: v.limit,
            has_limit: v.has_limit,
            role: match v.role {
                profile::Role::Measure => Role::Measure,
                profile::Role::Expected => Role::Expected,
                profile::Role::Context => Role::Context,
            },
        })
        .collect()
}

/// The envelope `snapshotFromJson` re-derives for a profile observation.
pub fn envelope_of(e: &profile::Evidence) -> crate::evidence::Envelope {
    crate::evidence::derive_envelope(&evidence_values(e))
}

/// `captureSnapshot`: the record as Vigil saw it at this moment (`present` cleared), the session
/// and engine clocks, the user's marks, `version` (`VIGIL_VERSION_STRING`) and the save time from
/// the engine's clock. Identity fields (nick, likely account, candidates) are the caller's.
pub fn capture(e: &Engine, r: &PlayerRecord, version: &[u8]) -> Profile {
    let mut s = Profile {
        vigil_version: version.to_vec(),
        saved_at_epoch_ms: e.clock().now_ms(),
        session_start_ms: e.session().start_ms(),
        captured_at_ms: e.last_time_ms(),
        origin: b"saved".to_vec(),
        record: profile_record(r),
        ..Profile::default()
    };
    if let Some(m) = e.marks().find(&r.key) {
        s.watched = m.watch || m.confirmed;
        s.confirmed = m.confirmed;
        s.exempt = m.exempt;
        s.reasons = m.reasons.clone();
    }
    s
}

/// `SnapshotSummary`: what the Records page lists.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct SnapshotSummary {
    pub id: String,
    pub name: Text,
    pub uuid: Text,
    pub saved_at_epoch_ms: f64,
    pub peak_suspicion: f32,
    pub suspicion: f32,
    pub detections: i32,
    pub evidence: i32,
    pub confirmed: bool,
    pub origin: Text,
    pub note: Text,
}

/// `safeStem`: letters, digits, `_` and `-` of a name (C locale), at most 24, `player` if none.
pub fn safe_stem(name: &[u8]) -> String {
    let out: String =
        name.iter().filter(|c| c.is_ascii_alphanumeric() || **c == b'_' || **c == b'-').map(|&c| c as char).collect();
    let out = if out.is_empty() { "player".to_string() } else { out };
    out.chars().take(24).collect()
}

fn read_file(path: &Path) -> Result<Vec<u8>, String> {
    let meta = std::fs::metadata(path).map_err(|_| "The file could not be read.".to_string())?;
    if !meta.is_file() {
        // fs::file_size reports an error for a directory.
        return Err("The file could not be read.".into());
    }
    if meta.len() > MAX_FILE_BYTES {
        return Err("The file is too large to be a Vigil profile.".into());
    }
    std::fs::read(path).map_err(|_| "The file could not be opened.".to_string())
}

fn write_file(path: &Path, text: &[u8]) -> bool {
    if let Some(parent) = path.parent() {
        let _ = std::fs::create_dir_all(parent);
    }
    let mut tmp = path.as_os_str().to_owned();
    tmp.push(".tmp");
    let tmp = PathBuf::from(tmp);
    {
        use std::io::Write;
        let Ok(mut out) = std::fs::File::create(&tmp) else { return false };
        if out.write_all(text).is_err() {
            return false;
        }
    }
    if std::fs::rename(&tmp, path).is_ok() {
        return true;
    }
    let _ = std::fs::remove_file(path);
    std::fs::rename(&tmp, path).is_ok()
}

fn has_json_extension(p: &Path) -> bool {
    // `path.extension() == ".json"`: case-sensitive.
    p.extension().is_some_and(|e| e == "json")
}

/// `SnapshotStore`: profiles, exports and pending imports in three folders.
#[derive(Clone, Debug, Default)]
pub struct SnapshotStore {
    profiles: PathBuf,
    exports: PathBuf,
    imports: PathBuf,
    list: Vec<SnapshotSummary>,
    revision: u64,
    clock: Clock,
    local: LocalTime,
}

impl SnapshotStore {
    pub fn new(clock: Clock, local: LocalTime) -> SnapshotStore {
        SnapshotStore { revision: 1, clock, local, ..SnapshotStore::default() }
    }

    pub fn set_directories(&mut self, profiles: &Path, exports: &Path, imports: &Path) {
        self.profiles = profiles.to_path_buf();
        self.exports = exports.to_path_buf();
        self.imports = imports.to_path_buf();
    }
    pub fn profiles_dir(&self) -> &Path {
        &self.profiles
    }
    pub fn exports_dir(&self) -> &Path {
        &self.exports
    }
    pub fn imports_dir(&self) -> &Path {
        &self.imports
    }
    pub fn list(&self) -> &[SnapshotSummary] {
        &self.list
    }
    pub fn revision(&self) -> u64 {
        self.revision
    }

    fn profile_path(&self, id: &str) -> PathBuf {
        self.profiles.join(format!("{id}.json"))
    }

    /// `stampOf` in the injected local time.
    fn stamp(&self, epoch_ms: f64) -> String {
        let t = to_time_t(epoch_ms / 1000.0);
        let c = match self.local.at(t) {
            Some(c) => c,
            // C++ terminates here (localtime_s); use the current time instead (module docs).
            None => self.local.at(to_time_t(self.clock.now_ms() / 1000.0)).unwrap_or_default(),
        };
        stamp_compact(&c)
    }

    /// `uniqueId`: `<safe name>-<local save time>`, with `-2`, `-3`… if taken.
    fn unique_id(&self, name: &[u8], epoch_ms: f64) -> String {
        let base = format!("{}-{}", safe_stem(name), self.stamp(epoch_ms));
        let mut id = base.clone();
        let mut n = 2;
        while self.profile_path(&id).exists() {
            id = format!("{base}-{n}");
            n += 1;
        }
        id
    }

    fn save_time(&self, s: &Profile) -> f64 {
        if s.saved_at_epoch_ms > 0.0 { s.saved_at_epoch_ms } else { self.clock.now_ms() }
    }

    /// Rescans the profiles folder: at most 500 readable profiles, newest first (MSVC `std::sort`).
    pub fn refresh(&mut self, cat: &CheckCatalog) {
        self.list.clear();
        if self.profiles.as_os_str().is_empty() || !self.profiles.exists() {
            self.revision += 1;
            return;
        }
        if let Ok(dir) = std::fs::read_dir(&self.profiles) {
            for entry in dir.flatten() {
                let p = entry.path();
                if !has_json_extension(&p) {
                    continue;
                }
                let Ok(text) = read_file(&p) else { continue };
                let Ok(s) = profile::from_json(&text, cat) else { continue };
                let r = &s.record;
                self.list.push(SnapshotSummary {
                    id: p.file_stem().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default(),
                    name: r.name.clone(),
                    uuid: r.uuid.clone(),
                    saved_at_epoch_ms: s.saved_at_epoch_ms,
                    peak_suspicion: r.peak_suspicion,
                    suspicion: r.suspicion,
                    detections: r.detections.len() as i32,
                    evidence: r.checks.iter().fold(0i32, |a, c| a.wrapping_add(c.evidence)),
                    confirmed: s.confirmed,
                    origin: s.origin.clone(),
                    note: s.note.clone(),
                });
                if self.list.len() >= MAX_LISTED {
                    break;
                }
            }
        }
        msvcsort::sort_by(&mut self.list, |a, b| a.saved_at_epoch_ms > b.saved_at_epoch_ms);
        self.revision += 1;
    }

    /// Saves a captured profile; returns its id.
    pub fn save(&mut self, s: &Profile, cat: &CheckCatalog) -> Result<String, String> {
        if self.profiles.as_os_str().is_empty() {
            return Err("No profiles folder.".into());
        }
        let id = self.unique_id(&s.record.name, self.save_time(s));
        if !write_file(&self.profile_path(&id), &profile::to_json(s, cat)) {
            return Err(format!("Vigil could not write to {}.", self.profiles.display()));
        }
        self.refresh(cat);
        Ok(id)
    }

    pub fn load(&self, id: &str, cat: &CheckCatalog) -> Result<Profile, String> {
        let text = read_file(&self.profile_path(id))?;
        profile::from_json(&text, cat)
    }

    pub fn remove(&mut self, id: &str) -> bool {
        let ok = std::fs::remove_file(self.profile_path(id)).is_ok();
        if ok {
            self.list.retain(|s| s.id != id);
            self.revision += 1;
        }
        ok
    }

    /// The user's note on a saved profile (cut at 500 bytes).
    pub fn set_note(&mut self, id: &str, note: &[u8], cat: &CheckCatalog) -> bool {
        let Ok(mut s) = self.load(id, cat) else { return false };
        s.note = note[..note.len().min(500)].to_vec();
        if !write_file(&self.profile_path(id), &profile::to_json(&s, cat)) {
            return false;
        }
        for sum in &mut self.list {
            if sum.id == id {
                sum.note = s.note.clone();
            }
        }
        self.revision += 1;
        true
    }

    /// Writes a shareable copy (`<exports>/<id>.vigil-profile.json`); returns its path.
    pub fn export_to(&self, id: &str) -> Result<PathBuf, String> {
        let text = read_file(&self.profile_path(id))?;
        let _ = std::fs::create_dir_all(&self.exports);
        let path = self.exports.join(format!("{id}.vigil-profile.json"));
        if !write_file(&path, &text) {
            return Err(format!("Vigil could not write to {}.", self.exports.display()));
        }
        Ok(path)
    }

    /// Validates a file and adds it to the library as "imported"; returns the new id. The copy is
    /// re-serialized from what this version understood: nothing from the file is kept verbatim.
    pub fn import_file(&mut self, path: &Path, cat: &CheckCatalog) -> Result<String, String> {
        let text = read_file(path)?;
        let mut s = profile::from_json(&text, cat)?;
        s.origin = b"imported".to_vec();
        let id = self.unique_id(&s.record.name, self.save_time(&s));
        if !write_file(&self.profile_path(&id), &profile::to_json(&s, cat)) {
            return Err(format!("Vigil could not write to {}.", self.profiles.display()));
        }
        self.refresh(cat);
        Ok(id)
    }

    /// Imports every `.json` file waiting in the imports folder; imported files are removed,
    /// refused ones stay (with `<file>: <reason>` in `errors`). Returns how many were added.
    pub fn import_pending(&mut self, cat: &CheckCatalog, errors: &mut Vec<String>) -> i32 {
        if self.imports.as_os_str().is_empty() || !self.imports.exists() {
            return 0;
        }
        let mut files = Vec::new();
        if let Ok(dir) = std::fs::read_dir(&self.imports) {
            for entry in dir.flatten() {
                let p = entry.path();
                if entry.file_type().is_ok_and(|t| t.is_file()) && has_json_extension(&p) {
                    files.push(p);
                }
            }
        }
        let mut added = 0;
        for f in files {
            match self.import_file(&f, cat) {
                Ok(_) => {
                    added += 1;
                    let _ = std::fs::remove_file(&f);
                }
                Err(e) => {
                    errors.push(format!("{}: {e}", f.file_name().map(|n| n.to_string_lossy()).unwrap_or_default()))
                }
            }
        }
        added
    }
}
