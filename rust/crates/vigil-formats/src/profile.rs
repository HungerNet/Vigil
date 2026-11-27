//! `vigil-profile` format 1 (saved profiles) — a mirror of `src/detection/snapshot.{h,cpp}`.
//!
//! A profile references checks and indicators by **id**; reading resolves them against the
//! [`CheckCatalog`] of the current build (unknown checks are dropped and listed), clamps values to
//! their ranges and bounds every list, because imported files come from other people. Writing
//! emits checks in catalog order and only those with data — so the canonical form of a profile
//! depends on the catalog, exactly as in C++.

use crate::catalog::CheckCatalog;
use crate::json::{self, Text, Value};
use crate::{clamp, clip, to_f32, to_i32};

pub const PROFILE_FORMAT: i32 = 1;
pub const MAX_FILE_BYTES: usize = 8 * 1024 * 1024;
const MAX_EVIDENCE: usize = 600;
const MAX_DETECTIONS: usize = 200;
const MAX_TIMELINE: usize = 1500;
const MAX_HISTORY: usize = 1440;
const MAX_VALUES: usize = 16;
const MAX_STRING: usize = 2000;
const MAX_LIST: usize = 16;
const MAX_DROPPED: usize = 32;
const MAX_RECENT: usize = 120;

fn cl(t: &[u8]) -> Text {
    clip(t, MAX_STRING)
}

#[derive(Clone, Copy, Debug, PartialEq, Default)]
pub struct Point {
    pub time_ms: f64,
    pub value: f32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum Role {
    #[default]
    Measure,
    Expected,
    Context,
}

#[derive(Clone, Debug, PartialEq, Default)]
pub struct EvidenceValue {
    pub name: Text,
    pub value: f64,
    pub unit: Text,
    pub limit: f64,
    pub has_limit: bool,
    pub role: Role,
}

#[derive(Clone, Debug, PartialEq, Default)]
pub struct Evidence {
    pub id: u64,
    pub tick: i64,
    pub time_ms: f64,
    pub check: usize,
    pub indicator: usize,
    pub weight: f32,
    pub severity: f32,
    pub contribution: f32,
    pub confidence_after: f32,
    pub summary: Text,
    pub detail: Text,
    pub target: Text,
    pub values: Vec<EvidenceValue>,
}

#[derive(Clone, Debug, PartialEq, Default)]
pub struct Detection {
    pub id: u64,
    pub tick: i64,
    pub time_ms: f64,
    pub check: usize,
    pub confidence: f32,
    pub evidence_count: i32,
    pub distinct_indicators: i32,
    pub evidence: Vec<u64>,
    pub summary: Text,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum TimelineKind {
    #[default]
    Seen,
    Left,
    Evidence,
    Detection,
    Hit,
    Hurt,
    Carried,
}

const KINDS: [(&[u8], TimelineKind); 7] = [
    (b"seen", TimelineKind::Seen),
    (b"left", TimelineKind::Left),
    (b"evidence", TimelineKind::Evidence),
    (b"detection", TimelineKind::Detection),
    (b"hit", TimelineKind::Hit),
    (b"hurt", TimelineKind::Hurt),
    (b"carried", TimelineKind::Carried),
];

impl TimelineKind {
    fn name(self) -> &'static [u8] {
        KINDS.iter().find(|(_, k)| *k == self).map(|(n, _)| *n).unwrap_or(b"seen")
    }
    fn from_name(s: &[u8]) -> Option<TimelineKind> {
        KINDS.iter().find(|(n, _)| *n == s).map(|(_, k)| *k)
    }
}

#[derive(Clone, Debug, PartialEq, Default)]
pub struct TimelineEvent {
    pub kind: TimelineKind,
    pub time_ms: f64,
    pub tick: i64,
    /// Check index, -1 for none.
    pub check: i32,
    pub reference: u64,
    pub value: f32,
    pub text: Text,
}

#[derive(Clone, Debug, PartialEq, Default)]
pub struct CheckRecord {
    /// The confidence model's value (restored, clamped to 0..100).
    pub confidence: f32,
    pub peak: f32,
    pub evidence: i32,
    pub detections: i32,
    pub indicator_counts: Vec<i32>,
    pub history: Vec<Point>,
}

#[derive(Clone, Debug, PartialEq, Default)]
pub struct Stats {
    pub ticks_observed: i32,
    pub ticks_in_combat: i32,
    pub hits_landed: i32,
    pub hits_taken: i32,
    pub swings: i32,
    pub reach_sum: f64,
    pub reach_samples: i32,
    pub reach_max: f64,
    pub aim_outside_sum: f64,
    pub cps_peak: f64,
    pub speed_peak: f64,
    pub speed_sum: f64,
    pub speed_samples: i32,
    pub jumps: i32,
    pub air_ticks_sum: f64,
    pub reach_recent: Vec<f32>,
    pub speed_recent: Vec<f32>,
}

#[derive(Clone, Debug, PartialEq, Default)]
pub struct PlayerRecord {
    pub key: Text,
    pub uuid: Text,
    pub name: Text,
    pub local: bool,
    pub teammate: bool,
    pub first_seen_ms: f64,
    pub last_seen_ms: f64,
    pub suspicion: f32,
    pub peak_suspicion: f32,
    pub prior_sessions: i32,
    pub prior_detections: i32,
    pub prior_peak: f32,
    pub suspicion_history: Vec<Point>,
    /// One per catalog check, in catalog order.
    pub checks: Vec<CheckRecord>,
    pub evidence: Vec<Evidence>,
    pub detections: Vec<Detection>,
    pub timeline: Vec<TimelineEvent>,
    pub stats: Stats,
}

/// `ProfileSnapshot`.
#[derive(Clone, Debug, PartialEq, Default)]
pub struct Profile {
    pub vigil_version: Text,
    pub saved_at_epoch_ms: f64,
    pub session_start_ms: f64,
    pub captured_at_ms: f64,
    /// "saved" or "imported".
    pub origin: Text,
    pub note: Text,
    pub watched: bool,
    pub confirmed: bool,
    pub exempt: bool,
    pub reasons: Vec<Text>,
    pub nicked: bool,
    pub likely_account: Text,
    pub candidates: Vec<Text>,
    /// Check ids in the file that the catalog does not know (not written).
    pub dropped_checks: Vec<Text>,
    pub record: PlayerRecord,
}

fn points(pts: &[Point]) -> Value {
    Value::Array(
        pts.iter().map(|p| Value::Array(vec![Value::Number(p.time_ms), Value::Number(f64::from(p.value))])).collect(),
    )
}

fn read_points(a: &Value) -> Vec<Point> {
    let mut out = Vec::new();
    for p in a.items() {
        if out.len() >= MAX_HISTORY {
            break;
        }
        if !p.is_array() || p.size() < 2 {
            continue;
        }
        out.push(Point { time_ms: p.at(0).as_number(), value: to_f32(clamp(p.at(1).as_number(), 0.0, 100.0)) });
    }
    out
}

fn texts(v: &[Text]) -> Value {
    Value::Array(v.iter().map(Value::from).collect())
}

fn read_texts(a: &Value) -> Vec<Text> {
    let mut out = Vec::new();
    for x in a.items() {
        if out.len() < MAX_LIST {
            out.push(cl(x.as_bytes()));
        }
    }
    out
}

/// `snapshotToJson` (indent 1).
pub fn to_json(s: &Profile, cat: &CheckCatalog) -> Vec<u8> {
    let r = &s.record;
    let check_id = |i: usize| -> Value { Value::from(cat.checks.get(i).cloned().unwrap_or_default()) };
    let indicator_id =
        |c: usize, i: usize| -> Text { cat.indicators.get(c).and_then(|v| v.get(i)).cloned().unwrap_or_default() };
    let mut root = Value::object();
    root.set("format", Value::from(PROFILE_FORMAT));
    root.set("kind", Value::from("vigil-profile"));
    root.set("vigil", Value::from(&s.vigil_version));
    root.set("savedAt", Value::Number(s.saved_at_epoch_ms));
    root.set("sessionStart", Value::Number(s.session_start_ms));
    root.set("capturedAt", Value::Number(s.captured_at_ms));
    root.set("origin", Value::from(&s.origin));
    if !s.note.is_empty() {
        root.set("note", Value::from(&s.note));
    }
    let mut marks = Value::object();
    marks.set("watched", Value::Bool(s.watched));
    marks.set("confirmed", Value::Bool(s.confirmed));
    marks.set("exempt", Value::Bool(s.exempt));
    marks.set("reasons", texts(&s.reasons));
    root.set("marks", marks);
    let mut idn = Value::object();
    idn.set("nicked", Value::Bool(s.nicked));
    if !s.likely_account.is_empty() {
        idn.set("likelyAccount", Value::from(&s.likely_account));
    }
    idn.set("candidates", texts(&s.candidates));
    root.set("identity", idn);

    let mut p = Value::object();
    p.set("key", Value::from(&r.key));
    p.set("uuid", Value::from(&r.uuid));
    p.set("name", Value::from(&r.name));
    p.set("local", Value::Bool(r.local));
    p.set("teammate", Value::Bool(r.teammate));
    p.set("firstSeen", Value::Number(r.first_seen_ms));
    p.set("lastSeen", Value::Number(r.last_seen_ms));
    p.set("suspicion", Value::Number(f64::from(r.suspicion)));
    p.set("peakSuspicion", Value::Number(f64::from(r.peak_suspicion)));
    p.set("priorSessions", Value::from(r.prior_sessions));
    p.set("priorDetections", Value::from(r.prior_detections));
    p.set("priorPeak", Value::Number(f64::from(r.prior_peak)));
    p.set("suspicionHistory", points(&r.suspicion_history));
    root.set("player", p);

    let mut checks = Value::array();
    for (i, cr) in r.checks.iter().enumerate() {
        if cr.evidence == 0 && cr.detections == 0 && cr.peak <= 0.0 {
            continue;
        }
        let mut c = Value::object();
        c.set("id", check_id(i));
        c.set("confidence", Value::Number(f64::from(cr.confidence)));
        c.set("peak", Value::Number(f64::from(cr.peak)));
        c.set("evidence", Value::from(cr.evidence));
        c.set("detections", Value::from(cr.detections));
        let mut counts = Value::object();
        for (k, &n) in cr.indicator_counts.iter().enumerate() {
            let iid = indicator_id(i, k);
            if !iid.is_empty() && n > 0 {
                counts.set(iid, Value::from(n));
            }
        }
        c.set("indicators", counts);
        c.set("history", points(&cr.history));
        checks.push(c);
    }
    root.set("checks", checks);

    let mut ev = Value::array();
    for e in &r.evidence {
        let mut j = Value::object();
        j.set("id", Value::Number(e.id as f64));
        j.set("tick", Value::Number(e.tick as f64));
        j.set("time", Value::Number(e.time_ms));
        j.set("check", check_id(e.check));
        j.set("indicator", Value::from(indicator_id(e.check, e.indicator)));
        j.set("weight", Value::Number(f64::from(e.weight)));
        j.set("severity", Value::Number(f64::from(e.severity)));
        j.set("contribution", Value::Number(f64::from(e.contribution)));
        j.set("after", Value::Number(f64::from(e.confidence_after)));
        j.set("summary", Value::from(&e.summary));
        j.set("detail", Value::from(&e.detail));
        if !e.target.is_empty() {
            j.set("target", Value::from(&e.target));
        }
        let mut vals = Value::array();
        for v in &e.values {
            let mut x = Value::object();
            x.set("name", Value::from(&v.name));
            x.set("value", Value::Number(v.value));
            if !v.unit.is_empty() {
                x.set("unit", Value::from(&v.unit));
            }
            if v.has_limit {
                x.set("limit", Value::Number(v.limit));
            }
            match v.role {
                Role::Measure => {}
                Role::Expected => x.set("role", Value::from("expected")),
                Role::Context => x.set("role", Value::from("context")),
            }
            vals.push(x);
        }
        j.set("values", vals);
        ev.push(j);
    }
    root.set("evidence", ev);

    let mut dets = Value::array();
    for d in &r.detections {
        let mut j = Value::object();
        j.set("id", Value::Number(d.id as f64));
        j.set("tick", Value::Number(d.tick as f64));
        j.set("time", Value::Number(d.time_ms));
        j.set("check", check_id(d.check));
        j.set("confidence", Value::Number(f64::from(d.confidence)));
        j.set("observations", Value::from(d.evidence_count));
        j.set("indicators", Value::from(d.distinct_indicators));
        j.set("evidence", Value::Array(d.evidence.iter().map(|&id| Value::Number(id as f64)).collect()));
        j.set("summary", Value::from(&d.summary));
        dets.push(j);
    }
    root.set("detections", dets);

    let mut tl = Value::array();
    for t in &r.timeline {
        let mut j = Value::object();
        j.set("kind", Value::from(t.kind.name()));
        j.set("time", Value::Number(t.time_ms));
        j.set("tick", Value::Number(t.tick as f64));
        if t.check >= 0 {
            j.set("check", check_id(t.check as usize));
        }
        if t.reference != 0 {
            j.set("ref", Value::Number(t.reference as f64));
        }
        j.set("value", Value::Number(f64::from(t.value)));
        j.set("text", Value::from(&t.text));
        tl.push(j);
    }
    root.set("timeline", tl);

    let b = &r.stats;
    let mut st = Value::object();
    st.set("ticksObserved", Value::from(b.ticks_observed));
    st.set("ticksInCombat", Value::from(b.ticks_in_combat));
    st.set("hitsLanded", Value::from(b.hits_landed));
    st.set("hitsTaken", Value::from(b.hits_taken));
    st.set("swings", Value::from(b.swings));
    st.set("reachSum", Value::Number(b.reach_sum));
    st.set("reachSamples", Value::from(b.reach_samples));
    st.set("reachMax", Value::Number(b.reach_max));
    st.set("aimOutsideSum", Value::Number(b.aim_outside_sum));
    st.set("cpsPeak", Value::Number(b.cps_peak));
    st.set("speedPeak", Value::Number(b.speed_peak));
    st.set("speedSum", Value::Number(b.speed_sum));
    st.set("speedSamples", Value::from(b.speed_samples));
    st.set("jumps", Value::from(b.jumps));
    st.set("airTicksSum", Value::Number(b.air_ticks_sum));
    st.set("reachRecent", Value::Array(b.reach_recent.iter().map(|&x| Value::Number(f64::from(x))).collect()));
    st.set("speedRecent", Value::Array(b.speed_recent.iter().map(|&x| Value::Number(f64::from(x))).collect()));
    root.set("stats", st);
    json::write(&root, 1)
}

/// `snapshotFromJson`: same refusals and messages, same normalization.
pub fn from_json(text: &[u8], cat: &CheckCatalog) -> Result<Profile, String> {
    if text.len() > MAX_FILE_BYTES {
        return Err("The file is too large to be a Vigil profile.".into());
    }
    let root = match json::parse(text) {
        Ok(v) if v.is_object() => v,
        Ok(_) => return Err("This is not a Vigil profile ().".into()),
        Err(e) => return Err(format!("This is not a Vigil profile ({}).", e.0)),
    };
    if root.get("kind").as_bytes() != b"vigil-profile" {
        return Err("This is not a Vigil profile.".into());
    }
    let format = to_i32(root.get("format").as_number_or(0.0));
    if format < 1 {
        return Err("This profile has no format number.".into());
    }
    if format > PROFILE_FORMAT {
        return Err("This profile was saved by a newer Vigil. Update Vigil to open it.".into());
    }

    let mut s = Profile {
        vigil_version: cl(root.get("vigil").as_bytes()),
        saved_at_epoch_ms: root.get("savedAt").as_number(),
        session_start_ms: root.get("sessionStart").as_number(),
        captured_at_ms: root.get("capturedAt").as_number(),
        origin: if root.get("origin").as_bytes() == b"imported" { b"imported".to_vec() } else { b"saved".to_vec() },
        note: cl(root.get("note").as_bytes()),
        ..Profile::default()
    };
    let marks = root.get("marks");
    s.watched = marks.get("watched").as_bool();
    s.confirmed = marks.get("confirmed").as_bool();
    s.exempt = marks.get("exempt").as_bool();
    s.reasons = read_texts(marks.get("reasons"));
    let idn = root.get("identity");
    s.nicked = idn.get("nicked").as_bool();
    s.likely_account = cl(idn.get("likelyAccount").as_bytes());
    s.candidates = read_texts(idn.get("candidates"));

    let p = root.get("player");
    let r = &mut s.record;
    r.key = cl(p.get("key").as_bytes());
    r.uuid = cl(p.get("uuid").as_bytes());
    r.name = cl(p.get("name").as_bytes());
    if r.name.is_empty() {
        return Err("The profile names no player.".into());
    }
    if r.key.is_empty() {
        r.key = if r.uuid.is_empty() { [b"name:".as_slice(), &r.name].concat() } else { r.uuid.clone() };
    }
    r.local = p.get("local").as_bool();
    r.teammate = p.get("teammate").as_bool();
    r.first_seen_ms = p.get("firstSeen").as_number();
    r.last_seen_ms = p.get("lastSeen").as_number();
    r.suspicion = to_f32(clamp(p.get("suspicion").as_number(), 0.0, 100.0));
    r.peak_suspicion = to_f32(clamp(p.get("peakSuspicion").as_number(), 0.0, 100.0));
    r.prior_sessions = to_i32(p.get("priorSessions").as_number());
    r.prior_detections = to_i32(p.get("priorDetections").as_number());
    r.prior_peak = to_f32(p.get("priorPeak").as_number());
    r.suspicion_history = read_points(p.get("suspicionHistory"));

    r.checks = cat
        .indicators
        .iter()
        .map(|inds| CheckRecord { indicator_counts: vec![0; inds.len()], ..CheckRecord::default() })
        .collect();
    for c in root.get("checks").items() {
        let id = c.get("id").as_bytes();
        let Some(ci) = cat.check_index(id) else {
            if !id.is_empty() && !s.dropped_checks.iter().any(|d| d == id) && s.dropped_checks.len() < MAX_DROPPED {
                s.dropped_checks.push(cl(id));
            }
            continue;
        };
        let cr = &mut r.checks[ci];
        // ConfidenceModel::restore: clamped to 0..100 in float (NaN passes through, as in C++).
        cr.confidence = to_f32(c.get("confidence").as_number()).clamp(0.0, 100.0);
        cr.peak = to_f32(clamp(c.get("peak").as_number(), 0.0, 100.0));
        cr.evidence = to_i32(c.get("evidence").as_number());
        cr.detections = to_i32(c.get("detections").as_number());
        for (k, n) in c.get("indicators").members() {
            if let Some(ii) = cat.indicator_index(ci, k) {
                cr.indicator_counts[ii] = to_i32(n.as_number());
            }
        }
        cr.history = read_points(c.get("history"));
    }

    for j in root.get("evidence").items() {
        if r.evidence.len() >= MAX_EVIDENCE {
            break;
        }
        let Some(ci) = cat.check_index(j.get("check").as_bytes()) else { continue };
        let Some(ii) = cat.indicator_index(ci, j.get("indicator").as_bytes()) else { continue };
        let mut e = Evidence {
            id: j.get("id").as_number() as u64,
            tick: j.get("tick").as_number() as i64,
            time_ms: j.get("time").as_number(),
            check: ci,
            indicator: ii,
            weight: to_f32(j.get("weight").as_number_or(1.0)),
            severity: to_f32(clamp(j.get("severity").as_number(), 0.0, 1.0)),
            contribution: to_f32(j.get("contribution").as_number()),
            confidence_after: to_f32(clamp(j.get("after").as_number(), 0.0, 100.0)),
            summary: cl(j.get("summary").as_bytes()),
            detail: cl(j.get("detail").as_bytes()),
            target: cl(j.get("target").as_bytes()),
            values: Vec::new(),
        };
        for x in j.get("values").items() {
            if e.values.len() >= MAX_VALUES {
                break;
            }
            let limit = x.find("limit");
            let role = x.get("role").as_bytes();
            e.values.push(EvidenceValue {
                name: cl(x.get("name").as_bytes()),
                value: x.get("value").as_number(),
                unit: cl(x.get("unit").as_bytes()),
                limit: limit.map(Value::as_number).unwrap_or(0.0),
                has_limit: limit.is_some(),
                role: match role {
                    b"expected" => Role::Expected,
                    b"context" => Role::Context,
                    _ => Role::Measure,
                },
            });
        }
        r.evidence.push(e);
    }
    for j in root.get("detections").items() {
        if r.detections.len() >= MAX_DETECTIONS {
            break;
        }
        let Some(ci) = cat.check_index(j.get("check").as_bytes()) else { continue };
        let mut d = Detection {
            id: j.get("id").as_number() as u64,
            tick: j.get("tick").as_number() as i64,
            time_ms: j.get("time").as_number(),
            check: ci,
            confidence: to_f32(clamp(j.get("confidence").as_number(), 0.0, 100.0)),
            evidence_count: to_i32(j.get("observations").as_number()),
            distinct_indicators: to_i32(j.get("indicators").as_number()),
            evidence: Vec::new(),
            summary: Text::new(),
        };
        for id in j.get("evidence").items() {
            if d.evidence.len() >= MAX_EVIDENCE {
                break;
            }
            d.evidence.push(id.as_number() as u64);
        }
        d.summary = cl(j.get("summary").as_bytes());
        r.detections.push(d);
    }
    for j in root.get("timeline").items() {
        if r.timeline.len() >= MAX_TIMELINE {
            break;
        }
        let Some(kind) = TimelineKind::from_name(j.get("kind").as_bytes()) else { continue };
        let check = match j.find("check") {
            Some(c) => match cat.check_index(c.as_bytes()) {
                Some(i) => i as i32,
                None => continue, // an unknown check's entry
            },
            None => -1,
        };
        r.timeline.push(TimelineEvent {
            kind,
            time_ms: j.get("time").as_number(),
            tick: j.get("tick").as_number() as i64,
            check,
            reference: j.get("ref").as_number() as u64,
            value: to_f32(j.get("value").as_number()),
            text: cl(j.get("text").as_bytes()),
        });
    }
    let st = root.get("stats");
    let b = &mut r.stats;
    b.ticks_observed = to_i32(st.get("ticksObserved").as_number());
    b.ticks_in_combat = to_i32(st.get("ticksInCombat").as_number());
    b.hits_landed = to_i32(st.get("hitsLanded").as_number());
    b.hits_taken = to_i32(st.get("hitsTaken").as_number());
    b.swings = to_i32(st.get("swings").as_number());
    b.reach_sum = st.get("reachSum").as_number();
    b.reach_samples = to_i32(st.get("reachSamples").as_number());
    b.reach_max = st.get("reachMax").as_number();
    b.aim_outside_sum = st.get("aimOutsideSum").as_number();
    b.cps_peak = st.get("cpsPeak").as_number();
    b.speed_peak = st.get("speedPeak").as_number();
    b.speed_sum = st.get("speedSum").as_number();
    b.speed_samples = to_i32(st.get("speedSamples").as_number());
    b.jumps = to_i32(st.get("jumps").as_number());
    b.air_ticks_sum = st.get("airTicksSum").as_number();
    b.reach_recent = st.get("reachRecent").items().iter().take(MAX_RECENT).map(|x| to_f32(x.as_number())).collect();
    b.speed_recent = st.get("speedRecent").items().iter().take(MAX_RECENT).map(|x| to_f32(x.as_number())).collect();
    Ok(s)
}
