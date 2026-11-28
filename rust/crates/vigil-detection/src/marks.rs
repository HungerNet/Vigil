//! `src/detection/marks.{h,cpp}`: the user's own records about players — watching, confirmed by
//! you, exempt — function for function (P3B_PLAN.md §3.3).
//!
//! Marks are the user's decisions, kept apart from Vigil's confidence: no check reads them and they
//! never change evidence or confidence (exempt only decides whether checks run at all). Confirmed and
//! Exempt are mutually exclusive. Keys are record keys (the UUID, or `name:<Name>`), or
//! `user:<lowercase name>` for a player added by name before Vigil saw them; the first real
//! (version-4) account with that name takes the mark over (`bind_name`). Nicks are never bound.
//!
//! Persistence: `history/marks.json`, format 2 (format 1 loads), through
//! `vigil_formats::marks::MarksFile` (the same bytes as C++). A file written by a newer format or
//! one that cannot be read opens read-only and is never overwritten. Times come from the injected
//! [`Clock`] (the wall clock by default, as `epochNowMs()`); they never influence a decision.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use vigil_formats::marks::{Mark, MarksFile};

use crate::clock::Clock;
use crate::cpp;
use crate::evidence::Text;
use crate::msvcsort;
use crate::roster::{is_valid_player_name, uuid_version_of};

pub const MARKS_FORMAT: i32 = 2;

const UNREADABLE: &str = "marks.json could not be read; changes are not saved until it is fixed or removed";

/// `MarkKind` (the values are the C++ enumerators, which `search` takes as an `int`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MarkKind {
    Watch = 0,
    Confirm = 1,
    Exempt = 2,
}

/// `Mark::addedByName()`.
pub fn added_by_name(m: &Mark) -> bool {
    m.key.starts_with(b"user:")
}

/// `lowerName`: `std::tolower` in the "C" locale, byte by byte.
pub fn lower_name(s: &[u8]) -> Text {
    cpp::lower(s)
}

fn contains(hay: &[u8], needle: &[u8]) -> bool {
    needle.is_empty() || hay.windows(needle.len()).any(|w| w == needle)
}

#[derive(Clone, Debug)]
pub struct Marks {
    marks: BTreeMap<Text, Mark>,
    path: Option<PathBuf>,
    read_only: bool,
    load_note: Text,
    revision: u64,
    clock: Clock,
}

impl Default for Marks {
    fn default() -> Self {
        Marks {
            marks: BTreeMap::new(),
            path: None,
            read_only: false,
            load_note: Text::new(),
            revision: 1,
            clock: Clock::system(),
        }
    }
}

impl Marks {
    pub fn with_clock(clock: Clock) -> Marks {
        Marks { clock, ..Marks::default() }
    }
    pub fn set_clock(&mut self, clock: Clock) {
        self.clock = clock;
    }

    /// The marks a file with these contents holds, without a path (nothing is saved): what
    /// `setPath` loads. Kept for callers that only read marks (P3A).
    pub fn load(file: &MarksFile) -> Marks {
        let mut m = Marks::default();
        m.apply_file(file);
        m
    }

    fn apply_file(&mut self, file: &MarksFile) {
        self.marks.clear();
        self.read_only = file.read_only;
        self.load_note = file.load_note.map(|n| n.as_bytes().to_vec()).unwrap_or_default();
        if file.load_note == Some(UNREADABLE) {
            // Never overwrite a file we could not read; nothing was loaded.
            return;
        }
        self.marks = file.marks.clone();
        self.revision += 1;
    }

    /// `setPath`: loads immediately. A file that does not exist (or cannot be opened) leaves an
    /// empty, writable store.
    pub fn set_path(&mut self, path: &Path) {
        self.path = if path.as_os_str().is_empty() { None } else { Some(path.to_path_buf()) };
        self.marks.clear();
        self.read_only = false;
        self.load_note.clear();
        let Ok(bytes) = std::fs::read(path) else { return };
        self.apply_file(&MarksFile::parse(&bytes));
    }

    /// `save`: the marks file, written atomically (`<path>.tmp`, then renamed). Never for a
    /// read-only store or without a path.
    pub fn save(&self) -> bool {
        let Some(path) = &self.path else { return false };
        if self.read_only {
            return false;
        }
        let file = MarksFile { marks: self.marks.clone(), read_only: false, load_note: None };
        let Some(bytes) = file.to_json() else { return false };
        if let Some(parent) = path.parent() {
            let _ = std::fs::create_dir_all(parent);
        }
        let mut tmp = path.as_os_str().to_owned();
        tmp.push(".tmp");
        let tmp = PathBuf::from(tmp);
        {
            use std::io::Write;
            let Ok(mut out) = std::fs::File::create(&tmp) else { return false };
            // As `out << text`: a write error is not checked.
            let _ = out.write_all(&bytes);
        }
        if std::fs::rename(&tmp, path).is_ok() {
            return true;
        }
        let _ = std::fs::remove_file(path);
        std::fs::rename(&tmp, path).is_ok()
    }

    pub fn read_only(&self) -> bool {
        self.read_only
    }
    pub fn load_note(&self) -> &[u8] {
        &self.load_note
    }
    pub fn revision(&self) -> u64 {
        self.revision
    }
    /// The marks in key order (`std::map`).
    pub fn entries(&self) -> &BTreeMap<Text, Mark> {
        &self.marks
    }

    pub fn find(&self, key: &[u8]) -> Option<&Mark> {
        self.marks.get(key)
    }
    /// A mark for this name: a record key first (in key order), then the added-by-name key.
    pub fn find_by_name(&self, name: &[u8]) -> Option<&Mark> {
        let lower = lower_name(name);
        if let Some(m) = self.marks.values().find(|m| lower_name(&m.name) == lower && !added_by_name(m)) {
            return Some(m);
        }
        self.find(&cpp::cat(&[b"user:", &lower]))
    }
    /// Watched or confirmed.
    pub fn watched(&self, key: &[u8]) -> bool {
        self.find(key).is_some_and(|m| m.watch || m.confirmed)
    }
    pub fn confirmed(&self, key: &[u8]) -> bool {
        self.find(key).is_some_and(|m| m.confirmed)
    }
    pub fn exempt(&self, key: &[u8]) -> bool {
        self.find(key).is_some_and(|m| m.exempt)
    }

    fn changed(&mut self) {
        self.revision += 1;
        self.save();
    }

    /// `touch`: the mark for `key` (created on first use), its name and update time.
    fn touch(&mut self, key: &[u8], name: &[u8]) -> &mut Mark {
        let now = self.clock.now_ms();
        let m = self.marks.entry(key.to_vec()).or_default();
        if m.key.is_empty() {
            m.key = key.to_vec();
            m.added_epoch_ms = now;
            if !key.starts_with(b"user:") && !key.starts_with(b"name:") {
                m.uuid = key.to_vec();
            }
        }
        if !name.is_empty() {
            m.name = name.to_vec();
        }
        m.updated_epoch_ms = now;
        m
    }

    fn erase_if_empty(&mut self, key: &[u8]) {
        if self.marks.get(key).is_some_and(|m| !m.any()) {
            self.marks.remove(key);
        }
    }

    pub fn set_watch(&mut self, key: &[u8], name: &[u8], on: bool) {
        if key.is_empty() {
            return;
        }
        let m = self.touch(key, name);
        if m.origin.is_empty() {
            m.origin = b"profile".to_vec();
        }
        m.watch = on;
        if on {
            m.exempt = false; // watching someone means judging them
        }
        self.erase_if_empty(key);
        self.changed();
    }

    /// Explicit user action only. Confirming also watches the player and clears an exemption.
    /// `confidence` is Vigil's confidence at that moment (-1 when the player is not observed).
    pub fn confirm(&mut self, key: &[u8], name: &[u8], reasons: Vec<Text>, confidence: f32) {
        if key.is_empty() {
            return;
        }
        let m = self.touch(key, name);
        if m.origin.is_empty() {
            m.origin = b"profile".to_vec();
        }
        m.confirmed = true;
        m.watch = true;
        m.exempt = false;
        m.confirmed_at_epoch_ms = m.updated_epoch_ms;
        m.confidence_at_confirm = confidence;
        m.reasons = reasons;
        self.changed();
    }

    /// Removing a confirmation keeps the player watched.
    pub fn unconfirm(&mut self, key: &[u8]) {
        if !self.marks.contains_key(key) {
            return;
        }
        let now = self.clock.now_ms();
        let m = self.marks.get_mut(key).expect("present");
        m.confirmed = false;
        m.reasons.clear();
        m.confidence_at_confirm = -1.0;
        m.updated_epoch_ms = now;
        self.changed();
    }

    /// Explicit user action only. Exempting clears a confirmation and the watch.
    pub fn set_exempt(&mut self, key: &[u8], name: &[u8], on: bool) {
        if key.is_empty() {
            return;
        }
        let m = self.touch(key, name);
        if m.origin.is_empty() {
            m.origin = b"profile".to_vec();
        }
        m.exempt = on;
        if on {
            m.exempt_at_epoch_ms = m.updated_epoch_ms;
            m.confirmed = false;
            m.watch = false;
            m.reasons.clear();
            m.confidence_at_confirm = -1.0;
        }
        self.erase_if_empty(key);
        self.changed();
    }

    /// The user's note (cut at 500 bytes).
    pub fn set_note(&mut self, key: &[u8], note: &[u8]) {
        let Some(m) = self.marks.get(key) else { return };
        if m.note == note {
            return;
        }
        let now = self.clock.now_ms();
        let m = self.marks.get_mut(key).expect("present");
        m.note = note[..note.len().min(500)].to_vec();
        m.updated_epoch_ms = now;
        self.changed();
    }

    /// The reasons of a confirmation (only a confirmed mark has reasons).
    pub fn set_reasons(&mut self, key: &[u8], reasons: Vec<Text>) {
        if !self.marks.get(key).is_some_and(|m| m.confirmed) {
            return;
        }
        let now = self.clock.now_ms();
        let m = self.marks.get_mut(key).expect("present");
        m.reasons = reasons;
        m.updated_epoch_ms = now;
        self.changed();
    }

    pub fn remove(&mut self, key: &[u8]) {
        if self.marks.remove(key).is_some() {
            self.changed();
        }
    }

    /// Keeps the stored name current.
    pub fn remember_name(&mut self, key: &[u8], name: &[u8]) {
        let Some(m) = self.marks.get_mut(key) else { return };
        if name.is_empty() || m.name == name {
            return;
        }
        m.name = name.to_vec();
        self.changed();
    }

    /// Adds (or updates) a player by name. Returns the key used — `known_key` when given, the key
    /// of a record-keyed mark with this name, else `user:<lowercase name>` — or empty for a name
    /// that is not a valid Minecraft name.
    pub fn add_by_name(&mut self, name: &[u8], kind: MarkKind, known_key: &[u8]) -> Text {
        if !is_valid_player_name(name) {
            return Text::new();
        }
        let mut key = known_key.to_vec();
        if key.is_empty() {
            // Someone already marked under a real key with this name keeps that key.
            let lower = lower_name(name);
            if let Some((k, _)) = self.marks.iter().find(|(_, m)| !added_by_name(m) && lower_name(&m.name) == lower) {
                key = k.clone();
            }
            if key.is_empty() {
                key = cpp::cat(&[b"user:", &lower]);
            }
        }
        let fresh = !self.marks.contains_key(&key);
        match kind {
            MarkKind::Watch => self.set_watch(&key, name, true),
            MarkKind::Confirm => self.confirm(&key, name, Vec::new(), -1.0),
            MarkKind::Exempt => self.set_exempt(&key, name, true),
        }
        if fresh {
            if let Some(m) = self.marks.get_mut(&key) {
                m.origin = if known_key.is_empty() { b"name".to_vec() } else { b"profile".to_vec() };
                self.save();
            }
        }
        key
    }

    /// `bindName`: a player was seen; moves an added-by-name mark onto the account's key (real
    /// accounts only, or a record Vigil could only key by name). Returns true when a mark was bound.
    pub fn bind_name(&mut self, record_key: &[u8], name: &[u8], uuid: &[u8]) -> bool {
        if name.is_empty() || record_key.is_empty() {
            return false;
        }
        let user_key = cpp::cat(&[b"user:", &lower_name(name)]);
        if !self.marks.contains_key(&user_key) {
            return false;
        }
        let real = uuid_version_of(uuid) == 4;
        let name_only = uuid.is_empty() && record_key.starts_with(b"name:");
        if !real && !name_only {
            return false;
        }
        let mut m = self.marks.remove(&user_key).expect("present");
        m.key = record_key.to_vec();
        m.uuid = uuid.to_vec();
        m.name = name.to_vec();
        m.updated_epoch_ms = self.clock.now_ms();
        if let Some(e) = self.marks.get_mut(record_key) {
            // The account already had marks: merge, the stronger decision wins (confirm > exempt > watch).
            if m.confirmed && !e.confirmed {
                e.confirmed = true;
                e.exempt = false;
                e.confirmed_at_epoch_ms = m.confirmed_at_epoch_ms;
                e.reasons = m.reasons.clone();
            }
            if m.exempt && !e.confirmed {
                e.exempt = true;
            }
            e.watch = e.watch || m.watch || e.confirmed;
            if e.note.is_empty() {
                e.note = m.note.clone();
            }
            e.updated_epoch_ms = m.updated_epoch_ms;
        } else {
            self.marks.insert(record_key.to_vec(), m);
        }
        self.changed();
        true
    }

    /// Confirmed first, then watched, then exempt; newest first — with MSVC `std::sort`, so equal
    /// times keep the order the C++ build gives them.
    pub fn all(&self) -> Vec<Mark> {
        let mut out: Vec<Mark> = self.marks.values().cloned().collect();
        let rank = |m: &Mark| {
            if m.confirmed {
                0
            } else if m.watch {
                1
            } else {
                2
            }
        };
        msvcsort::sort_by(&mut out, |a, b| {
            if rank(a) != rank(b) {
                return rank(a) < rank(b);
            }
            a.updated_epoch_ms > b.updated_epoch_ms
        });
        out
    }

    /// Case-insensitive search over names, notes and reasons. `kind` filters (-1 = any mark;
    /// Watch excludes confirmed players).
    pub fn search(&self, query: &[u8], kind: i32) -> Vec<Mark> {
        let q = lower_name(query);
        let mut out = Vec::new();
        for m in self.all() {
            if kind == MarkKind::Watch as i32 && !(m.watch && !m.confirmed) {
                continue;
            }
            if kind == MarkKind::Confirm as i32 && !m.confirmed {
                continue;
            }
            if kind == MarkKind::Exempt as i32 && !m.exempt {
                continue;
            }
            if !q.is_empty() {
                let mut hit = contains(&lower_name(&m.name), &q) || contains(&lower_name(&m.note), &q);
                for r in &m.reasons {
                    hit = hit || contains(&lower_name(r), &q);
                }
                if !hit {
                    continue;
                }
            }
            out.push(m);
        }
        out
    }

    pub fn confirmed_count(&self) -> i32 {
        self.marks.values().filter(|m| m.confirmed).count() as i32
    }
    pub fn watched_count(&self) -> i32 {
        self.marks.values().filter(|m| m.watch || m.confirmed).count() as i32
    }
    pub fn exempt_count(&self) -> i32 {
        self.marks.values().filter(|m| m.exempt).count() as i32
    }
}
