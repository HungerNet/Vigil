//! `replay-check` reads a trace and an outcome (RUST_PLAN.md P2 scope).

use std::path::{Path, PathBuf};
use std::process::Command;

fn tests_dir() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../tests")
}

fn run(args: &[PathBuf], strict: bool) -> (i32, String) {
    let mut cmd = Command::new(env!("CARGO_BIN_EXE_replay-check"));
    cmd.args(args);
    if strict {
        cmd.arg("--strict");
    }
    let out = cmd.output().expect("replay-check runs");
    (
        out.status.code().unwrap_or(-1),
        String::from_utf8_lossy(&out.stdout).into_owned() + &String::from_utf8_lossy(&out.stderr),
    )
}

#[test]
fn golden_pair_is_consistent_and_canonical() {
    let t = tests_dir();
    let (code, out) = run(&[t.join("traces/golden-arena.vtrace"), t.join("traces/golden-arena.expect.json")], true);
    assert_eq!(code, 0, "{out}");
    assert!(out.contains("600 frames") && out.contains("LowHopper") && out.contains("bhop:2"), "{out}");
}

#[test]
fn mismatched_pair_is_reported() {
    let t = tests_dir();
    let other_outcome = t.join("fixtures/formats/inputs/outcome/rounding-and-order.json");
    let (code, out) = run(&[t.join("traces/golden-arena.vtrace"), other_outcome], false);
    assert_eq!(code, 1, "{out}");
    assert!(out.contains("the outcome covers 1200 frames, the trace holds 600"), "{out}");
    assert!(out.contains("who never appears in the trace"), "{out}");
}

#[test]
fn unreadable_files_are_refused() {
    let t = tests_dir();
    let bad = t.join("fixtures/formats/inputs/trace/refused-player-without-position.vtrace");
    let (code, out) = run(&[bad, t.join("traces/golden-arena.expect.json")], false);
    assert_eq!(code, 2, "{out}");
    assert!(out.contains("line 2: malformed player"), "{out}");
    let (code, _) = run(&[t.join("traces/golden-arena.vtrace")], false);
    assert_eq!(code, 2);
}

#[test]
fn non_canonical_input_fails_strict_mode() {
    let t = tests_dir();
    let dir = std::env::temp_dir().join("vigil-replay-check-test");
    std::fs::create_dir_all(&dir).unwrap();
    // The same frames with a cosmetic change (an extra space): still consistent, no longer canonical.
    let golden = std::fs::read(t.join("traces/golden-arena.vtrace")).unwrap();
    let mut changed = golden.clone();
    let at = changed.iter().position(|&b| b == b',').unwrap();
    changed.insert(at + 1, b' ');
    let path = dir.join("spaced.vtrace");
    std::fs::write(&path, &changed).unwrap();
    let expect = t.join("traces/golden-arena.expect.json");
    assert_eq!(run(&[path.clone(), expect.clone()], false).0, 0);
    let (code, out) = run(&[path, expect], true);
    assert_eq!(code, 1, "{out}");
    assert!(out.contains("not in canonical form"), "{out}");
}
