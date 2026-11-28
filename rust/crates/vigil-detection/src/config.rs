//! The declaration side of `src/detection/config.{h,cpp}`: the engine declares its parameters
//! into a [`SchemaBuilder`], which produces the same `vigil-engine-schema` document as the C++
//! `ConfigSchema` (checked against tests/fixtures/engine-schema.json) plus the warning predicates.
//! The engine reads values from a `vigil_formats::config::EngineConfig` with the conversions of
//! C++ `Setting` / `EngineConfig` ([`ConfigRead`]).

use std::collections::HashMap;

use vigil_formats::config::{EngineConfig, ParamSpec, ParamType, Schema};

use crate::cpp;
use crate::evidence::Text;

pub type ParamId = i32;
pub const NO_PARAM: ParamId = -1;

/// `ParamRange` (mirrors `SettingRange`).
#[derive(Clone, Copy, Debug)]
pub struct ParamRange {
    pub min: f64,
    pub max: f64,
    pub step: f64,
    pub decimals: i32,
    pub unit: &'static str,
}

/// `{min, max, step, decimals, unit}`.
pub const fn range(min: f64, max: f64, step: f64, decimals: i32, unit: &'static str) -> ParamRange {
    ParamRange { min, max, step, decimals, unit }
}

/// `ParamView`: one value with `Setting`'s conversions, as warning predicates see it.
#[derive(Clone, Copy, Debug)]
pub struct ParamView {
    pub value: f64,
}

impl ParamView {
    pub fn b(&self) -> bool {
        self.value != 0.0
    }
    pub fn i(&self) -> i32 {
        cpp::to_int(self.value)
    }
    pub fn f(&self) -> f32 {
        self.value as f32
    }
}

pub type WarnFn = fn(ParamView) -> bool;

/// Reads with the exact conversions of C++ `EngineConfig` (`!= 0.0`, `static_cast<int>`,
/// `static_cast<float>`).
pub trait ConfigRead {
    fn value(&self, id: ParamId) -> f64;
    fn b(&self, id: ParamId) -> bool {
        self.value(id) != 0.0
    }
    fn i(&self, id: ParamId) -> i32 {
        cpp::to_int(self.value(id))
    }
    fn f(&self, id: ParamId) -> f32 {
        self.value(id) as f32
    }
}

impl ConfigRead for EngineConfig {
    fn value(&self, id: ParamId) -> f64 {
        self.values[id as usize]
    }
}

/// `ConfigSchema` while the engine declares itself.
#[derive(Clone, Debug, Default)]
pub struct SchemaBuilder {
    pub schema: Schema,
    pub warn: Vec<Option<WarnFn>>,
    index: HashMap<Text, ParamId>,
}

fn spec(key: &str, label: &str, tooltip: &str, group: &str, advanced: bool, ty: ParamType) -> ParamSpec {
    ParamSpec {
        key: key.into(),
        ty,
        default: 0.0,
        // The C++ ParamRange defaults, which is also what a schema document implies for Bool and
        // Choice parameters (it writes no range for them).
        min: 0.0,
        max: 1.0,
        step: 0.0,
        decimals: 2,
        unit: Text::new(),
        choices: Vec::new(),
        label: label.into(),
        group: group.into(),
        advanced,
        tooltip: tooltip.into(),
        warning: Text::new(),
        warns: false,
    }
}

impl SchemaBuilder {
    fn add(&mut self, s: ParamSpec) -> ParamId {
        // Keys are unique; declaring one twice returns the first declaration.
        if let Some(&id) = self.index.get(&s.key) {
            return id;
        }
        let id = self.schema.params.len() as ParamId;
        self.index.insert(s.key.clone(), id);
        self.schema.params.push(s);
        self.warn.push(None);
        id
    }

    pub fn find(&self, key: &[u8]) -> ParamId {
        self.index.get(key).copied().unwrap_or(NO_PARAM)
    }

    pub fn add_bool(
        &mut self,
        key: &str,
        label: &str,
        def: bool,
        tooltip: &str,
        group: &str,
        advanced: bool,
    ) -> ParamId {
        let mut s = spec(key, label, tooltip, group, advanced, ParamType::Bool);
        s.default = if def { 1.0 } else { 0.0 };
        self.add(s)
    }

    #[allow(clippy::too_many_arguments)]
    pub fn add_int(
        &mut self,
        key: &str,
        label: &str,
        def: i32,
        mut r: ParamRange,
        tooltip: &str,
        group: &str,
        advanced: bool,
    ) -> ParamId {
        let mut s = spec(key, label, tooltip, group, advanced, ParamType::Int);
        // The normalization Settings::addInt applies.
        if r.step <= 0.0 {
            r.step = 1.0;
        }
        r.decimals = 0;
        set_range(&mut s, r);
        s.default = f64::from(def);
        self.add(s)
    }

    #[allow(clippy::too_many_arguments)]
    pub fn add_float(
        &mut self,
        key: &str,
        label: &str,
        def: f64,
        r: ParamRange,
        tooltip: &str,
        group: &str,
        advanced: bool,
    ) -> ParamId {
        let mut s = spec(key, label, tooltip, group, advanced, ParamType::Float);
        set_range(&mut s, r);
        s.default = def;
        self.add(s)
    }

    #[allow(clippy::too_many_arguments)]
    pub fn add_choice(
        &mut self,
        key: &str,
        label: &str,
        def: i32,
        choices: &[&str],
        tooltip: &str,
        group: &str,
        advanced: bool,
    ) -> ParamId {
        let mut s = spec(key, label, tooltip, group, advanced, ParamType::Choice);
        s.choices = choices.iter().map(|c| c.as_bytes().to_vec()).collect();
        s.default = f64::from(def);
        self.add(s)
    }

    /// `spec(p).warning = …; spec(p).warnWhen = …`.
    pub fn warn(&mut self, p: ParamId, warning: &str, when: WarnFn) {
        let s = &mut self.schema.params[p as usize];
        s.warning = warning.into();
        s.warns = true;
        self.warn[p as usize] = Some(when);
    }
}

fn set_range(s: &mut ParamSpec, r: ParamRange) {
    s.min = r.min;
    s.max = r.max;
    s.step = r.step;
    s.decimals = r.decimals;
    s.unit = r.unit.into();
}
