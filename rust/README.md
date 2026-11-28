# Vigil — Rust workspace

The Rust side of the migration in [RUST_PLAN.md](../RUST_PLAN.md). Nothing here is part of the
shipped `Vigil.exe` / `Vigil.dll` yet (that starts in P4).

| Crate | Since | Contents |
|---|---|---|
| `crates/vigil-formats` | P2 | Every Vigil file format, byte-compatible with the C++ code: JSON layer (`json`), traces (`trace`), outcomes and their comparison (`outcome`), saved profiles (`profile`), marks (`marks`), engine schema and configuration (`config`), check catalog (`catalog`). Binary: `vigil-fmt` (canonical forms, twin of `VigilFormats`) |
| `crates/vigil-detection` | P3A, P3B | All of `src/detection` except the registry binding: the engine, a behavior-preserving port whose **entire state** is byte-identical to the C++ engine's on the corpus under 416 configurations and on generated scenarios ([P3A_PLAN.md](../P3A_PLAN.md)); and the rest ([P3B_PLAN.md](../P3B_PLAN.md)) — the user's marks (`marks`, with the file), profile capture and the profile store (`snapshot`), the cross-session history and the session journal (`session`), the "detections" log channel (`sinks`), all writing the C++ bytes. Modules mirror the C++ files (`engine`, `check`, `checks/*`, `track`, `combat`, `matchtracker`, `roster`, `session`, `marks`, `snapshot`, `evidence`, `config`, `types`, `replay`) plus the C++ semantics they need (`cpp`, `printf`, `hashorder`, `msvcsort`), the injected wall clock and local time (`clock`) and the parity dump (`parity`). Binaries: `replay-check` (reads a trace and an outcome; `--engine` replays the trace through the Rust engine) and `vigil-parity` (twin of `VigilParity`: corpus and generated plans with their sinks, marks scripts) |

Tests of `vigil-detection`: `detection.rs`, `validation.rs`, `records.rs` mirror the C++ suites
test for test (same names, same expectation counts, same frames — `tools/ci/test_parity.py`), on a
port of the simulator (`tests/common`: `sim.rs`, `scenarios.rs`, the C++ tests' random number
generators in `rng.rs`); `properties.rs` property-tests the invariants of docs/VALIDATION.md §5;
`sinks.rs` checks the sinks against committed C++ files; `foundations.rs`, `declaration.rs`,
`engine.rs`, `replay_check.rs` are the P3A checks.

```powershell
cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo test --release
target\release\replay-check.exe ..\tests\traces\golden-arena.vtrace ..\tests\traces\golden-arena.expect.json --strict --engine
target\release\vigil-fmt.exe canon profile some.vigil-profile.json out.json --schema ..\tests\fixtures\engine-schema.json
# parity with the C++ engine and its tests (needs a C++ build; docs/VALIDATION.md §9–§10)
python ..\tools\ci\engine_parity.py --cpp ..\build\Release\VigilParity.exe --rust target\release\vigil-parity.exe --work parity-work
python ..\tools\ci\test_parity.py --cpp-dir ..\build\Release --rust-dir .
```

Rules (RUST_PLAN §5, §11): the toolchain is pinned (`rust-toolchain.toml`); `unsafe` is forbidden
in the workspace; no third-party crates without a reason recorded in the plan; parsers return
errors and never panic; release builds unwind (`panic = "unwind"`), so the future FFI boundary can
catch panics; integer arithmetic wraps in every profile, as the C++ engine's does. The engine is
ported expression for expression (same operation order, same `float`/`double` promotions, STL
`max`/`min`/`clamp` semantics, MSVC container and sort order); refactoring waits until after P5.
Wall-clock time and local time come from the host (`clock`). The C++ ↔ Rust contracts and their
fixtures are described in [docs/VALIDATION.md §8–§10](../docs/VALIDATION.md).
