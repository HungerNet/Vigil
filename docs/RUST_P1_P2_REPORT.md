# Rust migration — P1 and P2 implementation report

Phases P1 and P2 of [RUST_PLAN.md](../RUST_PLAN.md) §7, carried out in one pass as §11.2 allows
("P1 + P2 may be combined"), strictly in sequence: P1 was implemented, validated, documented and
its exit criteria met before any P2 work began. Plans: [P1_PLAN.md](../P1_PLAN.md),
[P2_PLAN.md](../P2_PLAN.md).

| | P1 — Decouple config (C++) | P2 — Rust toolchain, formats |
|---|---|---|
| Status | ✅ exit criteria met | ✅ exit criteria met |
| User-visible change | None (screens, settings, outcomes, files) | None (no Rust in the shipped binaries; no product source changed) |
| Evidence | Full-state parity over 416 configurations × 7 scenarios byte-identical to the pre-P1 engine | 64 inputs round-trip identically in both directions (or are refused with identical reasons); 4 600+ numbers and 2 000 CRT math results bit-exact |

---

## 1. P1 — Decouple engine configuration

### 1.1 What changed

| Area | Change |
|---|---|
| `src/detection/config.h/.cpp` (new) | `ConfigSchema` (the engine's declaration of its 248 parameters), `EngineConfig` (immutable snapshot, bit-exact equality), JSON documents `vigil-engine-config` 1 and `vigil-engine-schema` 1 |
| `src/detection/check.*`, `checks/*.cpp` | `Setting*` → `ParamId`; every read converted expression-for-expression (`x_->value` → `cfg.value(x_)`, `->i()` → `cfg.i()`, `->b()` → `cfg.b()`); `check.h` no longer includes the registry |
| `src/detection/engine.*` | Owns its schema and an `EngineConfig`; `setConfig()` between frames; `CheckContext::config()`; no `Settings::instance()` anywhere |
| `src/detection/config_binding.*` (new) | The only detection code that knows the registry: registration (same keys, metadata, order), snapshots, `ConfigCache`, `SettingsSync`, preset helpers |
| `src/game/runtime.*` | Registers engine parameters right after `initialize()` (registration order unchanged); syncs after `Settings::load()`, at the top of every pump, and before the shutdown history flush |
| `src/ui/*` | Reads the registry's live values (`ui::liveConfig`) for everything it shows, so toggles still read back on the same frame; preset and indicator controls go through the binding |
| `tools/preview`, `tools/replay` | Preview syncs before each frame like the runtime; `VigilReplay --config`, `--engine-schema`, `--engine-config` |
| `tools/parity/parity.cpp`, `tests/corpus.h` (new) | `VigilParity`: full-state parity probe; a 7-scenario corpus that fires all 11 checks and all 24 indicators |
| Tests | 4 new validation tests (+39 expectations); preset test moved to the binding API; fixtures `tests/fixtures/engine-schema.json`, `engine-config.default.json` |
| CI | `validate_repo.py engine-globals` (no registry or mutable statics in `src/detection`); `VigilParity` run twice per build (determinism under every parameter) |
| Docs | ARCHITECTURE §2/§5/§6/§7, VALIDATION §1/§2/§4.1/§6/§7, TECH_DEBT, LIVE_VALIDATION (L19), RUST_PLAN §2/§6.1/§7 |

### 1.2 Why the change is behavior-preserving

1. **Same values.** The snapshot copies `Setting::value` bit for bit; accessors apply the same
   casts as `Setting` (`static_cast<int>`, `!= 0.0`, `static_cast<float>`).
2. **Same moment.** The UI, window procedure and pump share the Minecraft client thread, so no
   setting can change while `Engine::onFrame` runs; the snapshot is taken at the top of each pump,
   so every read in that pump sees what a live read would have seen.
3. **Same registry.** Identical keys, labels, tooltips, ranges, warnings and registration order
   (so `config.json` and the Settings pages are unchanged) — verified by dump (V4).
4. **Same reads.** Each check body was converted expression for expression; proven per parameter (V3).

### 1.3 Validation results

Baseline = the untouched tree (`git archive HEAD`), built with the same toolchain.

| # | Requirement | Result |
|---|---|---|
| V1 | Suites unchanged | detection 32/152, features 7/52, records 11/78, ui_logic 5/60 — identical to baseline; validation 7/169 → **11/208** (4 new tests). **550 expectations, 0 failures** |
| V2 | Golden trace | `VigilReplay --expect` passes; outcome JSON **byte-identical** to baseline |
| V3 | Full-state parity | `VigilParity`: 416 configurations (defaults, Relaxed, Strict, each of 248 parameters up and down, 7 mid-session change sequences) × 7 scenarios = 2,912 full-state digests + full default dumps: **byte-identical** to baseline. 483 distinct states; 195 of 248 parameters change the state on their own. Of the other 53, 42 cannot change it in any one-at-a-time run (11 preset choices the engine never reads, 22 sampling sub-settings while sampling is off — exercised jointly by a mid-session variant —, 7 sampling switches on checks that never sample, 2 data-file switches without paths) and 11 are inert in this corpus only (10 evidence windows longer than the scenarios' observation spacing; `keepy.minEvidence`, where the confidence threshold binds first) |
| V4 | Registry unchanged | Dump of all 248 settings (order, key, type, label, group, tooltip, warning, range, choices, flags, default, value, warning predicate probed at 46 values each): **byte-identical**; declaration committed as a fixture and tested |
| V5 | Engine owns no globals | `engine-globals`: 0 problems on P1 (104 on the baseline tree); "engines share nothing" test: two engines with different configurations interleaved frame by frame equal their solo runs, and the registry revision does not move |
| V6 | Snapshot serializable | Defaults, Strict and every-parameter-moved configurations round-trip exactly (text and bits, indented and compact); 14 hostile documents refused with the target untouched; unknown/missing keys reported |
| V7 | Falsification | See §1.4 |
| V8 | No visible change | `/W4 /WX` build of every target; clean single-configure build: `--self-check` ok, embedded runtime = built DLL; 12 preview views (7 pages, profile, evidence tab, HUD, light theme, 1.25× scale): every differing pixel lies in regions where the baseline differs from itself between runs (the animated Monitoring dot, session timers, live sparklines) |

### 1.4 Falsification (break on purpose, see it caught, restore)

| # | Deliberate bug | Suites | Golden | `VigilParity` |
|---|---|---|---|---|
| F1 | BHop air tolerance hard-coded to its default instead of read from the config | pass | pass | **caught**: exactly `up/down:checks.bhop.airTolerance` |
| F2 | `SettingsSync` delivers only its first snapshot | **3 failures** (sync test) | pass | **caught**: all 19 mid-session digests that change settings |
| F3 | Snapshot stores values through a `float` cast | **2 failures** (defaults = registry; bit-exact sync) | pass | **caught**: 2,897 of 2,912 digests |
| F4 | Config writer emits switches as numbers | **8 failures** (fixture, round trips) | — | — |
| F5 | The pre-P1 tree (registry reads everywhere) | — | — | `engine-globals`: **104 problems** |

F1 is the argument for V3: a realistic refactor slip that every suite and the golden trace miss.

### 1.5 Findings

* **Pre-existing, left unchanged** (recorded in [TECH_DEBT.md](TECH_DEBT.md)): `Setting::reset()`
  snaps to the step while registration stores the unsnapped default, so float-declared indicator
  weights (0.8f = 0.80000001192…) read as "modified" after "Reset check"; `Settings::load()`
  accepts a boolean for any setting type and `nan`/`inf` numbers without range checks.
* **Probe bug fixed during validation:** the first `VigilParity` reset settings once per
  configuration instead of once per scenario, so Bool perturbations alternated between scenarios.
  It affected both builds identically (parity still held) but reduced coverage from 195 to 170
  parameters; fixed before the final runs.
* **Local-only artifact:** reconfiguring before incremental builds stamps the exe and DLL with
  different build IDs, which `--self-check` rightly reports; a single-configure build (as in CI)
  passes.

### 1.6 Exit criteria

| Criterion | Evidence | Status |
|---|---|---|
| All suites + golden unchanged (RUST_PLAN) | V1, V2 | ✅ |
| Config snapshot serializable (RUST_PLAN) | V6; `VigilReplay --config` | ✅ |
| Checks read `EngineConfig`, no `Setting*` in the engine | `engine-globals` = 0; F5 | ✅ |
| Engine owns no globals | V5 | ✅ |
| Zero differences in full-state and registry parity; no visible change | V3, V4, V8 | ✅ |
| Falsification recorded | §1.4 | ✅ |
| ARCHITECTURE, VALIDATION, RUST_PLAN updated | §1.1 | ✅ |

P1 is complete; RUST_PLAN §7 marks it ✅. The live checklist gains L19 (settings applied on the
next tick) for the next in-game validation.

---

## 2. P2 — Rust toolchain and formats

### 2.1 What was built

| Area | Contents |
|---|---|
| `rust/` workspace | Edition 2024, toolchain pinned to 1.98.1 (`rust-toolchain.toml`, with rustfmt and clippy), `unsafe_code = "forbid"`, `panic = "unwind"` in release, **no third-party crates** |
| `vigil-formats` (≈ 2 450 lines + 350 lines of integration tests) | `json` (byte-exact mirror of `core/json`: byte strings, insertion-ordered objects, the `strtod` decimal grammar incl. `inf`/`nan`, C `%g` number writer), `trace`, `outcome` (+ `compare`, the parity oracle), `profile`, `marks`, `config` (schema and configuration documents), `catalog` (derived from the engine schema), `canon` |
| Binaries | `vigil-fmt canon <kind> …` (twin of the C++ tool); `replay-check <trace> <outcome> [--strict]` |
| C++ `VigilFormats` (`tools/formats`) | `canon`, `generate`, `canon-all`, `check` (CTest `format_fixtures`), `corpus` |
| Fixtures (`tests/fixtures/formats`) | 56 inputs across 6 kinds (50 hand-written edge cases + 6 generated real files: two saved profiles from corpus players, strict/odd configurations, an every-field trace, all 256 bytes in strings), their C++ canonical forms or refusal reasons, a 4 646-number table, a 2 000-entry math table, the check catalog, a divergence list |
| Build and CI | `-DVIGIL_BUILD_RUST=ON` (cargo from CMake, Rust tests and round trips in CTest); CI build job: `rustup toolchain install`, fmt, clippy `-D warnings`, `cargo test`, `cross_formats.py`, `replay-check --strict` |

### 2.2 Design decisions

* **Strings are bytes, numbers are printf.** The audit found that real traces can contain bytes that
  are not UTF-8 (JNI modified UTF-8 in chat lines; profiles clip strings by byte count) and that
  the C++ writer's number text (`%.0f` / `%.15g` / `%.17g`) differs from Rust's shortest form. A
  `String`/`serde_json` design would have refused or rewritten real files, so the JSON layer is a
  hand-written mirror. With no dependencies there is also nothing to download, audit or update.
* **Canonical form as the contract.** Every C++ reader normalizes (defaults, clamps, bounds,
  unknown ids dropped), so "round trip" is defined as parse + write producing identical bytes in
  both languages, or identical refusals — checked per input, not per format.
* **Catalog from the schema.** Profiles reference checks by id; the Rust side derives the check
  and indicator catalog from the P1 engine schema and verifies it equals the catalog the C++
  engine reports.
* **One deliberate divergence.** Hexadecimal float literals (`0x10`) are read by C's `strtod` but
  refused by Rust: classified *expected implementation difference* (no writer produces them),
  recorded in `divergences.txt`, and checked to still diverge exactly that way.

### 2.3 Validation results

| # | Requirement | Result |
|---|---|---|
| W1 | P1 still holds | Clean single-configure build with every target (`/W4 /WX`); 8/8 CTest tests; golden outcome byte-identical to the pre-P1 baseline; `VigilParity` byte-identical to the pre-P1 baseline and deterministic; `engine-globals` clean; `--self-check` ok |
| W2 | Rust parses every committed fixture | All 56 fixture inputs, every canonical form, the golden trace (600 frames) and outcome, the engine schema (248 parameters), the default configuration, the catalog (11 checks, 24 indicators), the number and math tables |
| W3 | Round trips identical | **20** inputs with byte-identical canonical forms and **35** refused with identical messages (`cargo test`); live in both directions over **64** inputs incl. the 7 corpus traces generated fresh (≈ 5 000 frames) and the golden pair: **29 identical round trips (C++→Rust→C++ and Rust→C++→Rust), 35 identical refusals, 0 problems** (`cross_formats.py`); the C++-written golden trace re-serializes byte for byte from Rust |
| W4 | Numbers | 4 646 doubles written identically and read back bit-exact |
| W5 | Hostile input | 35 hostile/invalid inputs refused by both sides with the same message; 3 360 mutated inputs: no panic, every accepted one canonicalizes to a fixed point |
| W6 | `replay-check` | Golden pair consistent and canonical (exit 0); mismatched pair reported (exit 1); unreadable trace refused (exit 2); a non-canonical but consistent trace fails `--strict` only |
| W7 | Quality gates | `cargo fmt --check` (also with CRLF checkouts), `cargo clippy --all-targets -D warnings`, 14 Rust tests — all green; no `unsafe`, no dependencies |
| W8 | Falsification | §2.4 |
| W9 | No product change | No file under `src/` changed in P2; shipped targets build as before; no format version changed |

**Floating-point parity (RUST_PLAN §8), answered early:** Rust's `sin`, `cos`, `atan2`, `pow`,
`sqrt`, `fmod`, `floor`, `ceil`, `round` and `fmax` return the same bits as the MSVC CRT for all
2 000 table entries on the engine's input ranges. The test stays in CI.

### 2.4 Falsification

| # | Deliberate fault (Rust side) | Caught by |
|---|---|---|
| F6 | Number writer uses 16 significant digits instead of 15 | numbers (1 440 of 4 646 differ), golden re-serialization, canonical forms |
| F7 | Trace default `health` 0 instead of -1 | canonical forms (`trace/edge.vtrace`, byte 524) and the mutation test's fixed-point check |
| F8 | Profile severity clamped to 0..2 instead of 0..1 | canonical forms (`profile/foreign.json`) |
| F9 | Marks write `confidenceAtConfirm` for unconfirmed marks | canonical forms (`marks/v2.json`) |
| F10 | JSON escapes DEL (0x7F) | canonical forms (4 inputs) |

On the C++ side, `VigilFormats check` fails on any committed fixture that no longer matches the
code (it compares, and reports DRIFT / MISSING / STALE / NOT A FIXED POINT).

### 2.5 Findings

* `.gitattributes` (which keeps `tests/traces` byte-exact) is untracked in this checkout. Every
  reader and test tolerates `\r`, so nothing breaks, but it should be committed, ideally with
  `tests/fixtures/** -text` added (TECH_DEBT).
* `rustfmt` must respect each file's line endings (`newline_style = "Auto"`): with
  `core.autocrlf=true`, Windows checkouts — including CI runners — give `.rs` files CRLF.
* Inputs that are undefined behavior in C++ (negative numbers cast to unsigned ids, out-of-range
  integer casts) have no C++ result to match and are kept out of the fixtures; Rust saturates.

### 2.6 Exit criteria

| Criterion | Evidence | Status |
|---|---|---|
| Rust parses every committed fixture (RUST_PLAN) | W2 | ✅ |
| C++ ↔ Rust round trips identical (RUST_PLAN) | W3, W4 | ✅ |
| `replay-check` reads a trace and an outcome (RUST_PLAN scope) | W6 | ✅ |
| P1 intact, hostile input safe, gates green, no product change; divergence classified | W1, W5, W7, W9, §2.2 | ✅ |
| CI runs the Rust gates and round trips; docs updated | §2.1; VALIDATION §1/§6/§8, ARCHITECTURE §1, RUST_PLAN §6.2/§7/§8/§10.1, README, RELEASE_PROCESS, TECH_DEBT | ✅ |

---

## 3. State of the migration and what comes next

| Phase | State |
|---|---|
| P0 Preparation | ✅ (1.0-dev.9) |
| P1 Decouple config | ✅ this pass |
| P2 Rust toolchain, formats | ✅ this pass |
| P3 Rust engine (offline) | Ready to start; not begun (by instruction) |

**What P3 inherits:** the engine contract is now frames + one configuration document; the Rust
side already reads traces and configurations, writes outcomes and compares them; `VigilParity`
provides full-state C++ references under 416 configurations; the scenario corpus exercises every
check and indicator; math parity with the CRT is proven on this toolchain.

**Recommendations**

1. Commit `.gitattributes` with `tests/fixtures/** -text` added.
2. Release this pass as 1.0-dev.12 when convenient (no user-visible change; `version.yaml` and the
   release notes were left untouched, since releases are yours to cut), and run live checklist
   item L19 (settings applied on the next tick) in that release.
3. In P3, port check by check behind `replay-check`, comparing against `VigilParity` digests as
   well as outcomes, and grow the corpus toward the ≥ 1 000 generated scenarios P3 requires.

**Commands to reproduce everything** — see [VALIDATION.md](VALIDATION.md) §4.1 (parity), §7 (local
CI) and §8 (Rust formats).
