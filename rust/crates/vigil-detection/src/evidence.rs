//! `src/detection/evidence.h` and `confidence.cpp`: evidence, the behavioral envelope, detections
//! and the confidence model. Confidence, weights and severities are `f32` as in C++; every
//! promotion to `f64` is written out where the C++ expression performs it.

use std::collections::{BTreeSet, VecDeque};

use crate::cpp;
use crate::printf;
use crate::types::Tick;

pub type Text = Vec<u8>;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
#[repr(u8)]
pub enum Role {
    #[default]
    Measure = 0,
    Expected = 1,
    Context = 2,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct EvidenceValue {
    pub name: Text,
    pub value: f64,
    pub unit: Text,
    pub limit: f64,
    pub has_limit: bool,
    pub role: Role,
}

impl EvidenceValue {
    /// `{name, value, unit}`.
    pub fn plain(name: &str, value: f64, unit: &str) -> Self {
        EvidenceValue { name: name.into(), value, unit: unit.into(), ..Default::default() }
    }
    /// `{name, value, unit, limit, true}`.
    pub fn limited(name: &str, value: f64, unit: &str, limit: f64) -> Self {
        EvidenceValue { name: name.into(), value, unit: unit.into(), limit, has_limit: true, role: Role::Measure }
    }
    /// `{name, value, unit, 0.0, false, Role::Expected}`.
    pub fn expected(name: &str, value: f64, unit: &str) -> Self {
        EvidenceValue { name: name.into(), value, unit: unit.into(), role: Role::Expected, ..Default::default() }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Envelope {
    pub quantity: Text,
    pub unit: Text,
    pub observed: f64,
    pub limit: f64,
    pub expected: f64,
    pub has_expected: bool,
    pub expected_label: Text,
    pub upper: bool,
    pub context: Vec<Text>,
}

impl Default for Envelope {
    fn default() -> Self {
        Envelope {
            quantity: Text::new(),
            unit: Text::new(),
            observed: 0.0,
            limit: 0.0,
            expected: 0.0,
            has_expected: false,
            expected_label: Text::new(),
            upper: true,
            context: Vec::new(),
        }
    }
}

impl Envelope {
    pub fn valid(&self) -> bool {
        !self.quantity.is_empty()
    }
    pub fn deviation(&self) -> f64 {
        if !self.valid() || self.limit == 0.0 {
            return 0.0;
        }
        let d = if self.upper {
            (self.observed - self.limit) / self.limit
        } else {
            (self.limit - self.observed) / self.limit
        };
        if d > 0.0 { d } else { 0.0 }
    }
}

/// `deriveEnvelope`.
pub fn derive_envelope(values: &[EvidenceValue]) -> Envelope {
    let mut e = Envelope::default();
    let Some(mi) = values.iter().position(|v| v.role == Role::Measure && v.has_limit) else {
        return e;
    };
    let measure = &values[mi];
    e.quantity = measure.name.clone();
    e.unit = measure.unit.clone();
    e.observed = measure.value;
    e.limit = measure.limit;
    for (i, v) in values.iter().enumerate() {
        if i == mi {
            continue;
        }
        if v.role == Role::Expected && !e.has_expected {
            e.expected = v.value;
            e.has_expected = true;
            e.expected_label = v.name.clone();
            continue;
        }
        if v.has_limit {
            continue;
        }
        let a = v.value.abs();
        let f = if a >= 100.0 {
            "%.0f"
        } else if a >= 10.0 {
            "%.1f"
        } else {
            "%.2f"
        };
        let buf = printf::format_into(96, f, &[printf::Arg::F(v.value)]);
        let mut text = cpp::cat(&[&v.name, b" ", &buf]);
        if !v.unit.is_empty() {
            if v.unit == b"%" || v.unit == b"x" || v.unit == "\u{B0}".as_bytes() {
                text.extend_from_slice(&v.unit);
            } else {
                text.push(b' ');
                text.extend_from_slice(&v.unit);
            }
        }
        e.context.push(text);
    }
    e.upper = if e.has_expected { e.expected <= e.limit } else { e.observed >= e.limit };
    e
}

#[derive(Clone, Debug, PartialEq)]
pub struct Evidence {
    pub id: u64,
    pub tick: Tick,
    pub time_ms: f64,
    pub check: i32,
    pub indicator: i32,
    pub weight: f32,
    pub severity: f32,
    pub contribution: f32,
    pub confidence_after: f32,
    pub summary: Text,
    pub detail: Text,
    pub target: Text,
    pub values: Vec<EvidenceValue>,
    pub envelope: Envelope,
}

#[derive(Clone, Debug, PartialEq, Default)]
pub struct Detection {
    pub id: u64,
    pub tick: Tick,
    pub time_ms: f64,
    pub check: i32,
    pub confidence: f32,
    pub evidence_count: i32,
    pub distinct_indicators: i32,
    pub evidence: Vec<u64>,
    pub summary: Text,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ConfidenceParams {
    pub alert_threshold: f32,
    pub half_life_sec: f32,
    pub min_evidence: i32,
    pub min_indicators: i32,
    pub window_sec: f32,
    pub cooldown_sec: f32,
    pub gain: f32,
}

impl Default for ConfidenceParams {
    fn default() -> Self {
        ConfidenceParams {
            alert_threshold: 70.0,
            half_life_sec: 30.0,
            min_evidence: 3,
            min_indicators: 1,
            window_sec: 60.0,
            cooldown_sec: 20.0,
            gain: 0.25,
        }
    }
}

/// `ConfidenceModel`: diminishing-returns accumulation, exponential decay, an evidence gate.
#[derive(Clone, Debug, PartialEq)]
pub struct ConfidenceModel {
    value: f32,
    last_ms: f64,
    last_detection_ms: f64,
    recent: VecDeque<(f64, i32)>,
}

impl Default for ConfidenceModel {
    fn default() -> Self {
        ConfidenceModel { value: 0.0, last_ms: -1.0, last_detection_ms: -1e18, recent: VecDeque::new() }
    }
}

impl ConfidenceModel {
    pub fn value(&self) -> f32 {
        self.value
    }
    pub fn last_detection_ms(&self) -> f64 {
        self.last_detection_ms
    }
    pub fn mark_detected(&mut self, now_ms: f64) {
        self.last_detection_ms = now_ms;
    }
    /// A saved profile shows the value it had (`value < 0 ? 0 : value > 100 ? 100 : value`).
    #[allow(clippy::manual_clamp)]
    pub fn restore(&mut self, value: f32) {
        self.value = if value < 0.0 {
            0.0
        } else if value > 100.0 {
            100.0
        } else {
            value
        };
    }
    pub fn reset(&mut self) {
        *self = ConfidenceModel::default();
    }

    pub fn decay_to(&mut self, now_ms: f64, half_life_sec: f32) {
        if self.last_ms < 0.0 {
            self.last_ms = now_ms;
            return;
        }
        let dt = (now_ms - self.last_ms) / 1000.0;
        self.last_ms = now_ms;
        if dt <= 0.0 || self.value <= 0.0 {
            return;
        }
        let hl = cpp::max(1.0, f64::from(half_life_sec));
        self.value = (f64::from(self.value) * 0.5f64.powf(dt / hl)) as f32;
        if self.value < 0.05 {
            self.value = 0.0;
        }
    }

    pub fn resume(&mut self, now_ms: f64, carry: f32) {
        self.last_ms = now_ms;
        if carry < 1.0 {
            self.value *= cpp::clamp(carry, 0.0, 1.0);
            if self.value < 0.05 {
                self.value = 0.0;
            }
            self.recent.clear();
        }
    }

    fn window_ms(p: &ConfidenceParams) -> f64 {
        f64::from(cpp::max(1.0f32, p.window_sec)) * 1000.0
    }

    /// Returns the contribution in points.
    pub fn add(&mut self, now_ms: f64, weight: f32, severity: f32, indicator: i32, p: &ConfidenceParams) -> f32 {
        self.decay_to(now_ms, p.half_life_sec);
        let strength = cpp::clamp(weight, 0.0, 4.0) * cpp::clamp(severity, 0.0, 1.0);
        let headroom = 100.0f32 - self.value;
        let fraction = 1.0f32 - (1.0f32 - cpp::clamp(p.gain, 0.01, 0.9)).powf(strength);
        let points = headroom * fraction;
        self.value = cpp::min(100.0f32, self.value + points);
        self.recent.push_back((now_ms, indicator));
        let window = Self::window_ms(p);
        while self.recent.front().is_some_and(|f| now_ms - f.0 > window) {
            self.recent.pop_front();
        }
        while self.recent.len() > 256 {
            self.recent.pop_front();
        }
        points
    }

    /// `gate`: observations and distinct indicators inside the window.
    pub fn gate(&self, now_ms: f64, p: &ConfidenceParams) -> (i32, i32) {
        let window = Self::window_ms(p);
        let mut kinds = BTreeSet::new();
        let mut count = 0;
        for e in &self.recent {
            if now_ms - e.0 > window {
                continue;
            }
            count += 1;
            kinds.insert(e.1);
        }
        (count, kinds.len() as i32)
    }

    pub fn can_detect(&self, now_ms: f64, p: &ConfidenceParams) -> bool {
        if self.value < p.alert_threshold {
            return false;
        }
        if now_ms - self.last_detection_ms < f64::from(p.cooldown_sec) * 1000.0 {
            return false;
        }
        let (count, distinct) = self.gate(now_ms, p);
        count >= p.min_evidence && distinct >= p.min_indicators
    }
}
