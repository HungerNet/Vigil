//! `history/marks.json` format 2 (format 1 readable) — a mirror of the persistence half of
//! `src/detection/marks.{h,cpp}`: the user's decisions about players (watch, confirmed, exempt).
//!
//! A file Vigil cannot read, or one written by a newer format, opens **read-only**: it is never
//! overwritten, so [`MarksFile::to_json`] refuses to produce one.

use std::collections::BTreeMap;

use crate::json::{self, Text, Value};
use crate::{to_f32, to_i32};

pub const MARKS_FORMAT: i32 = 2;

#[derive(Clone, Debug, PartialEq)]
pub struct Mark {
    pub key: Text,
    pub name: Text,
    pub uuid: Text,
    pub watch: bool,
    pub confirmed: bool,
    pub exempt: bool,
    pub added_epoch_ms: f64,
    pub confirmed_at_epoch_ms: f64,
    pub exempt_at_epoch_ms: f64,
    pub updated_epoch_ms: f64,
    pub reasons: Vec<Text>,
    pub note: Text,
    /// Vigil's confidence when confirmed; -1 when the player was not observed then.
    pub confidence_at_confirm: f32,
    pub origin: Text,
}

impl Default for Mark {
    fn default() -> Self {
        Mark {
            key: Text::new(),
            name: Text::new(),
            uuid: Text::new(),
            watch: false,
            confirmed: false,
            exempt: false,
            added_epoch_ms: 0.0,
            confirmed_at_epoch_ms: 0.0,
            exempt_at_epoch_ms: 0.0,
            updated_epoch_ms: 0.0,
            reasons: Vec::new(),
            note: Text::new(),
            confidence_at_confirm: -1.0,
            origin: Text::new(),
        }
    }
}

impl Mark {
    pub fn any(&self) -> bool {
        self.watch || self.confirmed || self.exempt
    }
}

/// The contents of a marks file, keyed (and ordered) like `std::map<std::string, Mark>`.
#[derive(Clone, Debug, PartialEq, Default)]
pub struct MarksFile {
    pub marks: BTreeMap<Text, Mark>,
    /// The file must not be written back (unreadable, or a newer format).
    pub read_only: bool,
    pub load_note: Option<&'static str>,
}

impl MarksFile {
    /// `Marks::setPath` on a file with these bytes.
    pub fn parse(text: &[u8]) -> MarksFile {
        let mut out = MarksFile::default();
        let root = match json::parse(text) {
            Ok(v) if v.is_object() => v,
            _ => {
                out.read_only = true;
                out.load_note =
                    Some("marks.json could not be read; changes are not saved until it is fixed or removed");
                return out;
            }
        };
        let format = to_i32(root.get("format").as_number_or(1.0));
        if format > MARKS_FORMAT {
            out.read_only = true;
            out.load_note = Some("marks.json was written by a newer Vigil; changes are not saved");
        }
        for (key, v) in root.get("players").members() {
            let mut m = Mark {
                key: key.clone(),
                name: v.get("name").as_text(),
                uuid: v.get("uuid").as_text(),
                watch: v.get("watch").as_bool(),
                confirmed: v.get("confirmed").as_bool(),
                exempt: v.get("exempt").as_bool(),
                added_epoch_ms: v.get("added").as_number(),
                confirmed_at_epoch_ms: v.get("confirmedAt").as_number(),
                exempt_at_epoch_ms: v.get("exemptAt").as_number(),
                updated_epoch_ms: v.get("updated").as_number(),
                note: v.get("note").as_text(),
                confidence_at_confirm: to_f32(v.get("confidenceAtConfirm").as_number_or(-1.0)),
                origin: v.get("origin").as_text(),
                reasons: v.get("reasons").items().iter().map(Value::as_text).collect(),
            };
            if m.added_epoch_ms <= 0.0 {
                m.added_epoch_ms =
                    if m.confirmed_at_epoch_ms > 0.0 { m.confirmed_at_epoch_ms } else { m.updated_epoch_ms };
            }
            if m.any() {
                out.marks.insert(m.key.clone(), m);
            }
        }
        out
    }

    /// `Marks::save`'s bytes, or `None` for a read-only file (it is never overwritten).
    pub fn to_json(&self) -> Option<Vec<u8>> {
        if self.read_only {
            return None;
        }
        let mut root = Value::object();
        root.set("format", Value::from(MARKS_FORMAT));
        let mut players = Value::object();
        for (key, m) in &self.marks {
            let mut p = Value::object();
            p.set("name", Value::from(&m.name));
            if !m.uuid.is_empty() {
                p.set("uuid", Value::from(&m.uuid));
            }
            p.set("watch", Value::Bool(m.watch));
            p.set("confirmed", Value::Bool(m.confirmed));
            p.set("exempt", Value::Bool(m.exempt));
            p.set("added", Value::Number(m.added_epoch_ms));
            if m.confirmed {
                p.set("confirmedAt", Value::Number(m.confirmed_at_epoch_ms));
            }
            if m.confirmed && m.confidence_at_confirm >= 0.0 {
                p.set("confidenceAtConfirm", Value::Number(f64::from(m.confidence_at_confirm)));
            }
            if m.exempt {
                p.set("exemptAt", Value::Number(m.exempt_at_epoch_ms));
            }
            p.set("updated", Value::Number(m.updated_epoch_ms));
            if !m.note.is_empty() {
                p.set("note", Value::from(&m.note));
            }
            if !m.origin.is_empty() {
                p.set("origin", Value::from(&m.origin));
            }
            p.set("reasons", Value::Array(m.reasons.iter().map(Value::from).collect()));
            players.set(key, p);
        }
        root.set("players", players);
        Some(json::write(&root, 1))
    }
}
