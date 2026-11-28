//! The engine-facing half of `src/detection/marks.{h,cpp}`: the user's marks as the engine uses
//! them — exemption (listed, never judged), lookup (records with marks are never pruned), binding
//! an added-by-name mark to the account it names, and keeping a mark's stored name current.
//!
//! Marks are loaded from a `history/marks.json` (`vigil_formats::marks::MarksFile`, the same
//! normalization as `Marks::setPath`). The UI-facing actions (watch, confirm, exempt, notes,
//! search, add by name) and writing the file back are P3B. Timestamps (`updated`) come from the
//! wall clock in C++ and never influence a decision.

use std::collections::BTreeMap;

use vigil_formats::marks::{Mark, MarksFile};

use crate::cpp;
use crate::evidence::Text;
use crate::roster::uuid_version_of;

#[derive(Clone, Debug, Default)]
pub struct Marks {
    marks: BTreeMap<Text, Mark>,
    read_only: bool,
    revision: u64,
}

fn epoch_now_ms() -> f64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0.0, |d| d.as_millis() as f64)
}

impl Marks {
    /// `Marks::setPath` on a file with these contents.
    pub fn load(file: &MarksFile) -> Marks {
        Marks { marks: file.marks.clone(), read_only: file.read_only, revision: 2 }
    }

    pub fn read_only(&self) -> bool {
        self.read_only
    }
    pub fn revision(&self) -> u64 {
        self.revision
    }
    pub fn all(&self) -> &BTreeMap<Text, Mark> {
        &self.marks
    }

    pub fn find(&self, key: &[u8]) -> Option<&Mark> {
        self.marks.get(key)
    }
    pub fn exempt(&self, key: &[u8]) -> bool {
        self.find(key).is_some_and(|m| m.exempt)
    }
    pub fn watched(&self, key: &[u8]) -> bool {
        self.find(key).is_some_and(|m| m.watch || m.confirmed)
    }
    pub fn confirmed(&self, key: &[u8]) -> bool {
        self.find(key).is_some_and(|m| m.confirmed)
    }

    fn changed(&mut self) {
        // `save()` writes the file when a path is set (P3B).
        self.revision += 1;
    }

    pub fn remember_name(&mut self, key: &[u8], name: &[u8]) {
        let Some(m) = self.marks.get_mut(key) else { return };
        if name.is_empty() || m.name == name {
            return;
        }
        m.name = name.to_vec();
        self.changed();
    }

    /// `bindName`: moves a mark added by name onto the account's record key (real accounts only).
    pub fn bind_name(&mut self, record_key: &[u8], name: &[u8], uuid: &[u8]) -> bool {
        if name.is_empty() || record_key.is_empty() {
            return false;
        }
        let user_key = cpp::cat(&[b"user:", &cpp::lower(name)]);
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
        m.updated_epoch_ms = epoch_now_ms();
        if let Some(e) = self.marks.get_mut(record_key) {
            // The account already had marks: merge, the stronger decision wins.
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
}
