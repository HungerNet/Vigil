//! A stand-in for the C++ settings registry, reduced to what three engine-facing tests need
//! (P3B_PLAN.md §2.1): `registerEngineSettings` (how the registry stores each declared default),
//! `Setting::set` (rounding, step snapping, clamping), `isDefault`, `configFromSettings`,
//! `applyPreset` and `SettingsSync` (a snapshot reaches the engine only when the registry changed).
//! The real registry is host code and stays in C++ (`core/settings`, `config_binding`).

use vigil_detection::check::CheckBase;
use vigil_detection::engine::Engine;
use vigil_formats::config::{EngineConfig, ParamSpec, ParamType, Schema};

#[derive(Clone, Debug)]
pub struct Registry {
    pub specs: Vec<ParamSpec>,
    pub defs: Vec<f64>,
    pub values: Vec<f64>,
    pub revision: u64,
}

impl Registry {
    /// `registerEngineSettings`: Bool → `def != 0`, Int and Choice → `static_cast<int>(def)`.
    pub fn register(schema: &Schema) -> Registry {
        let defs: Vec<f64> = schema
            .params
            .iter()
            .map(|p| match p.ty {
                ParamType::Bool => {
                    if p.default != 0.0 {
                        1.0
                    } else {
                        0.0
                    }
                }
                ParamType::Int | ParamType::Choice => f64::from(vigil_detection::cpp::to_int(p.default)),
                ParamType::Float => p.default,
            })
            .collect();
        Registry { specs: schema.params.clone(), values: defs.clone(), defs, revision: 1 }
    }

    pub fn find(&self, key: &str) -> Option<usize> {
        self.specs.iter().position(|p| p.key == key.as_bytes())
    }

    pub fn touch(&mut self) {
        self.revision += 1;
    }

    /// `Setting::set`.
    pub fn set(&mut self, i: usize, v: f64) {
        let p = &self.specs[i];
        let mut v = match p.ty {
            ParamType::Bool => {
                if v != 0.0 {
                    1.0
                } else {
                    0.0
                }
            }
            ParamType::Int | ParamType::Choice => v.round(),
            ParamType::Float => v,
        };
        if matches!(p.ty, ParamType::Int | ParamType::Float) {
            // addInt normalizes a zero step to 1.
            let step = if p.ty == ParamType::Int && p.step <= 0.0 { 1.0 } else { p.step };
            if step > 0.0 {
                v = p.min + ((v - p.min) / step).round() * step;
            }
            v = vigil_detection::cpp::clamp(v, p.min, p.max);
        }
        if p.ty == ParamType::Choice && !p.choices.is_empty() {
            v = vigil_detection::cpp::clamp(v, 0.0, (p.choices.len() - 1) as f64);
        }
        if (v - self.values[i]).abs() < 1e-12 {
            return;
        }
        self.values[i] = v;
        self.touch();
    }

    pub fn set_key(&mut self, key: &str, v: f64) {
        let i = self.find(key).unwrap_or_else(|| panic!("no setting {key}"));
        self.set(i, v);
    }
    pub fn value(&self, key: &str) -> f64 {
        self.values[self.find(key).expect("setting")]
    }

    /// `Setting::isDefault`.
    pub fn is_default(&self, i: usize) -> bool {
        (self.values[i] - self.defs[i]).abs() < 1e-9
    }

    /// Every engine parameter back to the value a fresh install has (assignment, not `reset()`).
    pub fn reset_engine_settings(&mut self) {
        self.values = self.defs.clone();
        self.touch();
    }

    /// `configFromSettings`.
    pub fn config(&self) -> EngineConfig {
        EngineConfig { values: self.values.clone() }
    }

    /// `applyPreset`.
    pub fn apply_preset(&mut self, check: &CheckBase, preset: i32) {
        if !(0..=2).contains(&preset) {
            return;
        }
        for e in &check.presets {
            let v = match preset {
                0 => e.relaxed,
                1 => e.balanced,
                _ => e.strict,
            };
            self.set(e.param as usize, v);
        }
        self.set(check.preset_param() as usize, f64::from(preset));
    }

    /// `presetSetting(S, check)->i()`.
    pub fn preset_of(&self, check: &CheckBase) -> i32 {
        vigil_detection::cpp::to_int(self.values[check.preset_param() as usize])
    }
}

/// `SettingsSync`: hands the engine a snapshot when the registry changed since the last sync.
#[derive(Debug, Default)]
pub struct SettingsSync {
    revision: Option<u64>,
}

impl SettingsSync {
    pub fn sync(&mut self, e: &mut Engine, r: &Registry) -> bool {
        if self.revision == Some(r.revision) {
            return false;
        }
        e.set_config(&r.config());
        self.revision = Some(r.revision);
        true
    }
}
