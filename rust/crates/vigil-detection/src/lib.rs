//! Vigil's detection engine in Rust (RUST_PLAN.md P3A, P3A_PLAN.md).
//!
//! A behavior-preserving, expression-for-expression port of `src/detection`. Its contract is the
//! C++ engine's (docs/ARCHITECTURE.md §6): a sequence of `WorldFrame`s and one `EngineConfig` in,
//! session records, alerts and statistics out; deterministic. The port is proven by comparing the
//! **entire** engine state with the C++ engine's, byte for byte, in the `VigilParity` format.

#![forbid(unsafe_code)]
// Comparisons are written exactly as in C++: `x < a || x > b` and `(a..=b).contains(&x)` differ
// for NaN, and so would the engine's behavior on hostile input.
#![allow(clippy::manual_range_contains)]
// Expression-for-expression port (P3_PLAN.md §6.1): keep the C++ shapes until after P5.
#![allow(clippy::collapsible_if, clippy::collapsible_else_if, clippy::needless_range_loop)]

pub mod check;
pub mod checks;
pub mod combat;
pub mod config;
pub mod cpp;
pub mod engine;
pub mod evidence;
pub mod hashorder;
pub mod marks;
pub mod matchtracker;
pub mod parity;
pub mod printf;
pub mod replay;
pub mod roster;
pub mod session;
pub mod track;
pub mod types;
