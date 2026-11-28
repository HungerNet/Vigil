//! The Rust engine declares itself exactly like the C++ engine (P3A_PLAN.md V2): its parameter
//! schema, default configuration, check catalog and check metadata (names, texts, requirements,
//! sampling support, presets) are byte-identical to the fixtures the C++ side writes.

use std::path::{Path, PathBuf};

use vigil_detection::check::category_name;
use vigil_detection::engine::Engine;
use vigil_formats::config::{EngineConfig, Schema};
use vigil_formats::json::{self, Value};

fn tests_dir() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../tests")
}

/// A committed text file without `\r`.
fn committed(rel: &str) -> Vec<u8> {
    let bytes = std::fs::read(tests_dir().join(rel)).unwrap_or_else(|e| panic!("{rel}: {e}"));
    bytes.into_iter().filter(|&b| b != b'\r').collect()
}

fn with_newline(mut v: Vec<u8>) -> Vec<u8> {
    v.push(b'\n');
    v
}

#[test]
fn the_schema_is_the_committed_declaration() {
    let e = Engine::new();
    assert_eq!(e.schema().params.len(), 248);
    let fixture = committed("fixtures/engine-schema.json");
    assert_eq!(String::from_utf8_lossy(&with_newline(e.schema().to_json())), String::from_utf8_lossy(&fixture));
    assert_eq!(&Schema::from_json(&fixture).unwrap(), e.schema());
    // Every parameter with a warning text declares a predicate and vice versa (VigilParity probes
    // the predicates themselves across each range).
    let warns = e.warn_predicates();
    for (p, w) in e.schema().params.iter().zip(warns) {
        assert_eq!(p.warns, w.is_some(), "{}", String::from_utf8_lossy(&p.key));
    }
}

#[test]
fn the_defaults_are_the_committed_configuration() {
    let e = Engine::new();
    let defaults = EngineConfig::defaults(e.schema());
    assert_eq!(&defaults, e.config());
    assert_eq!(
        String::from_utf8_lossy(&with_newline(defaults.to_json(e.schema(), 2))),
        String::from_utf8_lossy(&committed("fixtures/engine-config.default.json"))
    );
}

#[test]
fn the_check_catalog_matches_cpp() {
    let e = Engine::new();
    let mut root = Value::object();
    root.set("kind", Value::from("vigil-check-catalog"));
    let checks = e
        .checks()
        .iter()
        .map(|c| {
            let mut j = Value::object();
            j.set("id", Value::from(c.base.id));
            j.set("indicators", Value::Array(c.base.indicators.iter().map(|i| Value::from(i.id)).collect()));
            j
        })
        .collect();
    root.set("checks", Value::Array(checks));
    assert_eq!(
        String::from_utf8_lossy(&with_newline(json::write(&root, 2))),
        String::from_utf8_lossy(&committed("fixtures/formats/check-catalog.json"))
    );
}

#[test]
fn check_metadata_and_presets_match_cpp() {
    let e = Engine::new();
    let mut root = Value::object();
    root.set("kind", Value::from("vigil-checks"));
    let mut checks = Vec::new();
    for c in e.checks() {
        let b = &c.base;
        let mut j = Value::object();
        j.set("id", Value::from(b.id));
        j.set("name", Value::from(b.name));
        j.set("category", Value::from(category_name(b.category)));
        j.set("summary", Value::from(b.summary));
        j.set("about", Value::from(b.about));
        j.set("requirements", Value::Array(c.imp.requirements().into_iter().map(Value::from).collect()));
        j.set("tickSampling", Value::Bool(c.imp.supports_tick_sampling()));
        j.set("preset", Value::from(&e.schema().params[b.preset_param() as usize].key));
        let entries = b
            .presets
            .iter()
            .map(|p| {
                let mut pj = Value::object();
                pj.set("key", Value::from(&e.schema().params[p.param as usize].key));
                pj.set("relaxed", Value::Number(p.relaxed));
                pj.set("balanced", Value::Number(p.balanced));
                pj.set("strict", Value::Number(p.strict));
                pj
            })
            .collect();
        j.set("entries", Value::Array(entries));
        checks.push(j);
    }
    root.set("checks", Value::Array(checks));
    assert_eq!(
        String::from_utf8_lossy(&with_newline(json::write(&root, 2))),
        String::from_utf8_lossy(&committed("fixtures/formats/checks.json"))
    );
    // Invariant of the C++ registration: every preset's Balanced value is the parameter's default.
    for c in e.checks() {
        for p in &c.base.presets {
            assert_eq!(p.balanced, e.schema().params[p.param as usize].default, "{}", c.base.id);
        }
    }
}
