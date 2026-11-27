//! Engine configuration documents (RUST_PLAN.md P1) — a mirror of `src/detection/config.{h,cpp}`.
//!
//! * [`Schema`]: the engine's declaration of its parameters (`vigil-engine-schema` 1), as written
//!   by `schemaToJson` (`VigilReplay --engine-schema`, tests/fixtures/engine-schema.json).
//! * [`EngineConfig`]: one value per parameter (`vigil-engine-config` 1), read and written exactly
//!   like `configFromJson` / `configToJson`. This is the document a Rust engine receives across
//!   the C ABI (RUST_PLAN.md §6.1).

use crate::json::{self, Text, Value};
use crate::{show, to_i32};

pub const ENGINE_CONFIG_FORMAT: i32 = 1;
pub const ENGINE_CONFIG_MAX_BYTES: usize = 1 << 20;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ParamType {
    Bool,
    Int,
    Float,
    Choice,
}

impl ParamType {
    pub fn name(self) -> &'static str {
        match self {
            ParamType::Bool => "bool",
            ParamType::Int => "int",
            ParamType::Float => "float",
            ParamType::Choice => "choice",
        }
    }
    fn from_name(s: &[u8]) -> Option<ParamType> {
        match s {
            b"bool" => Some(ParamType::Bool),
            b"int" => Some(ParamType::Int),
            b"float" => Some(ParamType::Float),
            b"choice" => Some(ParamType::Choice),
            _ => None,
        }
    }
}

/// One declared parameter.
#[derive(Clone, Debug, PartialEq)]
pub struct ParamSpec {
    pub key: Text,
    pub ty: ParamType,
    pub default: f64,
    // Int and Float.
    pub min: f64,
    pub max: f64,
    pub step: f64,
    pub decimals: i32,
    pub unit: Text,
    // Choice.
    pub choices: Vec<Text>,
    // Presentation.
    pub label: Text,
    pub group: Text,
    pub advanced: bool,
    pub tooltip: Text,
    pub warning: Text,
    /// The C++ declaration carries a warning predicate (code; not serializable).
    pub warns: bool,
}

/// The parameter declaration of one engine build, in registration order.
#[derive(Clone, Debug, PartialEq, Default)]
pub struct Schema {
    pub params: Vec<ParamSpec>,
}

impl Schema {
    pub fn find(&self, key: &[u8]) -> Option<usize> {
        self.params.iter().position(|p| p.key == key)
    }

    /// Reads a `vigil-engine-schema` document.
    pub fn from_json(text: &[u8]) -> Result<Schema, String> {
        let root = json::parse(text).map_err(|e| e.0.to_string())?;
        if root.get("kind").as_bytes() != b"vigil-engine-schema" {
            return Err("not a Vigil engine schema".into());
        }
        let format = root.get("format").as_number_or(0.0);
        if format != f64::from(ENGINE_CONFIG_FORMAT) {
            return Err(format!("unsupported engine schema format {}", json::format_number(format)));
        }
        let mut schema = Schema::default();
        for (n, p) in root.get("parameters").items().iter().enumerate() {
            let ty =
                ParamType::from_name(p.get("type").as_bytes()).ok_or_else(|| format!("parameter {n}: unknown type"))?;
            let default = match ty {
                ParamType::Bool => {
                    if p.get("default").as_bool() {
                        1.0
                    } else {
                        0.0
                    }
                }
                _ => p.get("default").as_number(),
            };
            let key = p.get("key").as_text();
            if key.is_empty() || schema.find(&key).is_some() {
                return Err(format!("parameter {n}: missing or repeated key"));
            }
            schema.params.push(ParamSpec {
                key,
                ty,
                default,
                min: p.get("min").as_number(),
                max: p.get("max").as_number_or(if matches!(ty, ParamType::Int | ParamType::Float) { 0.0 } else { 1.0 }),
                step: p.get("step").as_number(),
                decimals: to_i32(p.get("decimals").as_number_or(2.0)),
                unit: p.get("unit").as_text(),
                choices: p.get("choices").items().iter().map(Value::as_text).collect(),
                label: p.get("label").as_text(),
                group: p.get("group").as_text(),
                advanced: p.get("advanced").as_bool(),
                tooltip: p.get("tooltip").as_text(),
                warning: p.get("warning").as_text(),
                warns: p.get("warns").as_bool(),
            });
        }
        Ok(schema)
    }

    /// `schemaToJson` (indent 2).
    pub fn to_json(&self) -> Vec<u8> {
        let mut root = Value::object();
        root.set("kind", Value::from("vigil-engine-schema"));
        root.set("format", Value::from(ENGINE_CONFIG_FORMAT));
        let mut params = Value::array();
        for p in &self.params {
            let mut j = Value::object();
            j.set("key", Value::from(&p.key));
            j.set("type", Value::from(p.ty.name()));
            j.set(
                "default",
                if p.ty == ParamType::Bool { Value::Bool(p.default != 0.0) } else { Value::Number(p.default) },
            );
            if matches!(p.ty, ParamType::Int | ParamType::Float) {
                j.set("min", Value::Number(p.min));
                j.set("max", Value::Number(p.max));
                j.set("step", Value::Number(p.step));
                j.set("decimals", Value::from(p.decimals));
                j.set("unit", Value::from(&p.unit));
            }
            if p.ty == ParamType::Choice {
                j.set("choices", Value::Array(p.choices.iter().map(Value::from).collect()));
            }
            j.set("label", Value::from(&p.label));
            j.set("group", Value::from(&p.group));
            j.set("advanced", Value::Bool(p.advanced));
            j.set("tooltip", Value::from(&p.tooltip));
            if !p.warning.is_empty() {
                j.set("warning", Value::from(&p.warning));
            }
            if p.warns {
                j.set("warns", Value::Bool(true));
            }
            params.push(j);
        }
        root.set("parameters", params);
        json::write(&root, 2)
    }
}

/// One value per parameter of a schema.
#[derive(Clone, Debug, PartialEq)]
pub struct EngineConfig {
    pub values: Vec<f64>,
}

/// Keys a document had that the schema does not declare, and parameters it did not mention.
#[derive(Clone, Debug, PartialEq, Default)]
pub struct ReadReport {
    pub unknown: Vec<Text>,
    pub missing: Vec<Text>,
}

impl EngineConfig {
    /// The schema's defaults.
    pub fn defaults(schema: &Schema) -> EngineConfig {
        EngineConfig { values: schema.params.iter().map(|p| p.default).collect() }
    }

    /// `configFromJson`: same refusals, same messages.
    pub fn from_json(text: &[u8], schema: &Schema) -> Result<(EngineConfig, ReadReport), String> {
        if text.len() > ENGINE_CONFIG_MAX_BYTES {
            return Err("document too large".into());
        }
        let root = json::parse(text).map_err(|e| e.0.to_string())?;
        if !root.is_object() {
            return Err("not a JSON object".into());
        }
        if root.get("kind").as_bytes() != b"vigil-engine-config" {
            return Err("not a Vigil engine configuration".into());
        }
        let format = root.get("format");
        if !format.is_number() || format.as_number().floor() != format.as_number() || format.as_number() < 1.0 {
            return Err("missing or invalid format".into());
        }
        if format.as_number() > f64::from(ENGINE_CONFIG_FORMAT) {
            return Err(format!("unsupported engine configuration format {}", format_17g(format.as_number())));
        }
        let values = root.get("values");
        if !values.is_object() {
            return Err("missing values".into());
        }
        let mut cfg = EngineConfig::defaults(schema);
        let mut seen = vec![false; schema.params.len()];
        let mut report = ReadReport::default();
        for (key, v) in values.members() {
            let Some(id) = schema.find(key) else {
                report.unknown.push(key.clone());
                continue;
            };
            let p = &schema.params[id];
            let x = if p.ty == ParamType::Bool {
                match v {
                    Value::Bool(b) => f64::from(u8::from(*b)),
                    _ => return Err(format!("{}: expected true or false", show(&p.key))),
                }
            } else {
                match v {
                    Value::Number(n) if n.is_finite() => {
                        if matches!(p.ty, ParamType::Int | ParamType::Choice) && n.floor() != *n {
                            return Err(format!("{}: expected a whole number", show(&p.key)));
                        }
                        *n
                    }
                    _ => return Err(format!("{}: expected a number", show(&p.key))),
                }
            };
            cfg.values[id] = x;
            seen[id] = true;
        }
        report.missing = schema.params.iter().zip(&seen).filter(|(_, s)| !**s).map(|(p, _)| p.key.clone()).collect();
        Ok((cfg, report))
    }

    /// `configToJson`.
    pub fn to_json(&self, schema: &Schema, indent: i32) -> Vec<u8> {
        let mut root = Value::object();
        root.set("kind", Value::from("vigil-engine-config"));
        root.set("format", Value::from(ENGINE_CONFIG_FORMAT));
        let mut values = Value::object();
        for (p, &v) in schema.params.iter().zip(&self.values) {
            values.set(&p.key, if p.ty == ParamType::Bool { Value::Bool(v != 0.0) } else { Value::Number(v) });
        }
        root.set("values", values);
        json::write(&root, indent)
    }
}

// The "%.17g" of config.cpp's error message.
fn format_17g(d: f64) -> String {
    json::format_g(d, 17)
}
