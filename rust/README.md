# Vigil — Rust workspace

The Rust side of the migration in [RUST_PLAN.md](../RUST_PLAN.md). Nothing here is part of the
shipped `Vigil.exe` / `Vigil.dll` yet (that starts in P4).

| Crate | Since | Contents |
|---|---|---|
| `crates/vigil-formats` | P2 | Every Vigil file format, byte-compatible with the C++ code: JSON layer (`json`), traces (`trace`), outcomes and their comparison (`outcome`), saved profiles (`profile`), marks (`marks`), engine schema and configuration (`config`), check catalog (`catalog`). Binaries: `vigil-fmt` (canonical forms, twin of `VigilFormats`) and `replay-check` (reads a trace and an outcome) |

```powershell
cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo test --release
target\release\replay-check.exe ..\tests\traces\golden-arena.vtrace ..\tests\traces\golden-arena.expect.json --strict
target\release\vigil-fmt.exe canon profile some.vigil-profile.json out.json --schema ..\tests\fixtures\engine-schema.json
```

Rules (RUST_PLAN §5, §11): the toolchain is pinned (`rust-toolchain.toml`); `unsafe` is forbidden
in the workspace; no third-party crates without a reason recorded in the plan; parsers return
errors and never panic; release builds unwind (`panic = "unwind"`), so the future FFI boundary can
catch panics. The C++ ↔ Rust contract and its fixtures are described in
[docs/VALIDATION.md §8](../docs/VALIDATION.md).
