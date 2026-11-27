//! Vigil's file formats in Rust, byte-for-byte compatible with the C++ implementation
//! (RUST_PLAN.md P2, P2_PLAN.md).
//!
//! | Module | Format | C++ counterpart |
//! |---|---|---|
//! | [`json`] | JSON values, parser, writer, number text | `src/core/json.*` |
//! | [`trace`] | `vigil-trace` 1 (JSON lines of `WorldFrame`s) | `src/detection/trace.*` |
//! | [`outcome`] | `vigil-outcome` 1 and the outcome comparison | `src/detection/replay.*` |
//! | [`marks`] | `history/marks.json` 2 (1 readable) | `src/detection/marks.*` |
//! | [`profile`] | `vigil-profile` 1 (saved profiles) | `src/detection/snapshot.*` |
//! | [`config`] | `vigil-engine-schema` 1, `vigil-engine-config` 1 | `src/detection/config.*` |
//! | [`catalog`] | check and indicator ids in index order | `CheckCatalog` |
//!
//! Every reader *normalizes* exactly like its C++ counterpart (defaults, clamps, bounds, unknown
//! ids dropped), and every writer produces the C++ bytes. "Canonical form" = parse + write; the
//! canonical forms of the two implementations are identical (tests/fixtures/formats).
//!
//! Casts from JSON numbers mirror C++ `static_cast` for every in-range value; out of range C++ is
//! undefined and Rust saturates.

#![forbid(unsafe_code)]

pub mod canon;
pub mod catalog;
pub mod config;
pub mod json;
pub mod marks;
pub mod outcome;
pub mod profile;
pub mod trace;

use json::Text;

/// Bytes as readable text (lossy only for bytes that are not UTF-8).
pub fn show(t: &[u8]) -> String {
    String::from_utf8_lossy(t).into_owned()
}

/// `static_cast<int>(double)` for in-range values (truncation toward zero; Rust saturates where
/// C++ is undefined).
pub(crate) fn to_i32(d: f64) -> i32 {
    d as i32
}

/// `static_cast<float>(double)`.
pub(crate) fn to_f32(d: f64) -> f32 {
    d as f32
}

/// `std::clamp(v, lo, hi)` on doubles, NaN passing through as in C++.
pub(crate) fn clamp(v: f64, lo: f64, hi: f64) -> f64 {
    if v < lo {
        lo
    } else if hi < v {
        hi
    } else {
        v
    }
}

/// `s.substr(0, n)` on bytes.
pub(crate) fn clip(t: &[u8], n: usize) -> Text {
    t[..t.len().min(n)].to_vec()
}
