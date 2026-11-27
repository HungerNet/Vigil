//! Canonical forms: parse + write, per format. The C++ twin is `VigilFormats canon`
//! (tools/formats/formats.cpp); for every input both produce the same bytes or both refuse
//! (P2_PLAN.md §2.3).

use crate::catalog::CheckCatalog;
use crate::config::{EngineConfig, Schema};
use crate::json;
use crate::marks::MarksFile;
use crate::{outcome, profile, trace};

/// The formats `canon` knows.
pub const KINDS: [&str; 7] = ["json", "trace", "outcome", "marks", "profile", "config", "schema"];

/// The canonical bytes of `input` read as `kind`, or why it is refused. `schema` is the engine's
/// parameter declaration (needed by `profile`, through its check catalog, and by `config`).
pub fn canon(kind: &str, input: &[u8], schema: Option<&Schema>) -> Result<Vec<u8>, String> {
    let need_schema = || schema.ok_or_else(|| format!("{kind} needs the engine schema (--schema)"));
    match kind {
        "json" => json::parse(input).map(|v| json::write(&v, -1)).map_err(|e| e.0.to_string()),
        "trace" => trace::read(input).map(|t| trace::write(&t)),
        "outcome" => outcome::from_json(input).map(|o| outcome::to_json(&o)),
        "marks" => {
            let m = MarksFile::parse(input);
            m.to_json().ok_or_else(|| format!("read-only: {}", m.load_note.unwrap_or("not writable")))
        }
        "profile" => {
            let cat = CheckCatalog::from_schema(need_schema()?);
            profile::from_json(input, &cat).map(|p| profile::to_json(&p, &cat))
        }
        "config" => {
            let s = need_schema()?;
            EngineConfig::from_json(input, s).map(|(c, _)| c.to_json(s, 2))
        }
        "schema" => Schema::from_json(input).map(|s| s.to_json()),
        _ => Err(format!("unknown kind '{kind}' (one of {})", KINDS.join(", "))),
    }
}
