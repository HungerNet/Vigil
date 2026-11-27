//! `vigil-outcome` format 1 and the outcome comparison — a mirror of `src/detection/replay.{h,cpp}`.
//!
//! An outcome is what an engine concluded from a frame sequence: per player and check, how many
//! observations and detections, and the final and peak confidence. Two engines agree on a trace
//! when [`compare`] finds no difference. This is the parity oracle the Rust engine (P3) answers to.

use std::collections::BTreeMap;

use crate::json::{self, Text, Value};
use crate::{show, to_f32, to_i32};

#[derive(Clone, Debug, PartialEq, Default)]
pub struct PlayerOutcome {
    pub name: Text,
    pub suspicion: f32,
    pub peak: f32,
    /// check id → observations (ordered like `std::map<std::string, int>`).
    pub evidence: BTreeMap<Text, i32>,
    /// check id → detections.
    pub detections: BTreeMap<Text, i32>,
}

#[derive(Clone, Debug, PartialEq, Default)]
pub struct Outcome {
    pub frames: i32,
    /// keyed by player name.
    pub players: BTreeMap<Text, PlayerOutcome>,
}

fn counts(m: &BTreeMap<Text, i32>) -> Value {
    let mut v = Value::object();
    for (k, n) in m {
        v.set(k, Value::from(*n));
    }
    v
}

// std::round(f * 100.0) / 100.0 with f promoted to double (round half away from zero, as std::round).
fn rounded(f: f32) -> f64 {
    (f64::from(f) * 100.0).round() / 100.0
}

/// `outcomeToJson` (indent 2).
pub fn to_json(o: &Outcome) -> Vec<u8> {
    let mut root = Value::object();
    root.set("kind", Value::from("vigil-outcome"));
    root.set("format", Value::from(1));
    root.set("frames", Value::from(o.frames));
    let mut players = Value::object();
    for (name, p) in &o.players {
        let mut j = Value::object();
        j.set("suspicion", Value::Number(rounded(p.suspicion)));
        j.set("peak", Value::Number(rounded(p.peak)));
        j.set("evidence", counts(&p.evidence));
        j.set("detections", counts(&p.detections));
        players.set(name, j);
    }
    root.set("players", players);
    json::write(&root, 2)
}

/// `outcomeFromJson`.
pub fn from_json(text: &[u8]) -> Result<Outcome, String> {
    let root = json::parse(text).map_err(|e| e.0.to_string())?;
    if root.get("kind").as_bytes() != b"vigil-outcome" {
        return Err("not a Vigil outcome file".into());
    }
    let mut out = Outcome { frames: to_i32(root.get("frames").as_number()), ..Outcome::default() };
    for (name, v) in root.get("players").members() {
        let read = |key: &str| -> BTreeMap<Text, i32> {
            v.get(key).members().iter().map(|(k, n)| (k.clone(), to_i32(n.as_number()))).collect()
        };
        let p = PlayerOutcome {
            name: name.clone(),
            suspicion: to_f32(v.get("suspicion").as_number()),
            peak: to_f32(v.get("peak").as_number()),
            evidence: read("evidence"),
            detections: read("detections"),
        };
        out.players.insert(name.clone(), p);
    }
    Ok(out)
}

/// `compareOutcomes`: differences between an expected and an actual outcome (empty when they
/// agree). Confidence compares within `tolerance` points, counts exactly. Same messages as C++.
pub fn compare(expected: &Outcome, actual: &Outcome, tolerance: f32) -> Vec<String> {
    let mut diffs = Vec::new();
    if expected.frames != actual.frames {
        diffs.push(format!("frames: expected {}, got {}", expected.frames, actual.frames));
    }
    for (name, e) in &expected.players {
        let n = show(name);
        let Some(a) = actual.players.get(name) else {
            diffs.push(format!("{n}: missing"));
            continue;
        };
        if (e.suspicion - a.suspicion).abs() > tolerance {
            diffs.push(format!("{n}: suspicion expected {:.2}, got {:.2}", e.suspicion, a.suspicion));
        }
        if (e.peak - a.peak).abs() > tolerance {
            diffs.push(format!("{n}: peak expected {:.2}, got {:.2}", e.peak, a.peak));
        }
        for (x, y, what) in [(&e.evidence, &a.evidence, "evidence"), (&e.detections, &a.detections, "detections")] {
            let mut keys: Vec<&Text> = x.keys().chain(y.keys()).collect();
            keys.sort();
            keys.dedup();
            for k in keys {
                let ex = x.get(k).copied().unwrap_or(0);
                let ac = y.get(k).copied().unwrap_or(0);
                if ex != ac {
                    diffs.push(format!("{n}: {} {what} expected {ex}, got {ac}", show(k)));
                }
            }
        }
    }
    for name in actual.players.keys() {
        if !expected.players.contains_key(name) {
            diffs.push(format!("{}: not expected", show(name)));
        }
    }
    diffs
}

/// The default tolerance of `compareOutcomes`.
pub const DEFAULT_TOLERANCE: f32 = 0.01;
