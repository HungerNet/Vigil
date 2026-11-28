# Vigil — Rust Feasibility Study & Migration Roadmap

> Stage 2 of the 1.0-dev.9 pass. **No Rust code was written and no system was ported.** This
> document decides whether, how and in what order Vigil should become a 100 % Rust project, and
> what has been prepared so that the migration can be carried out safely — by Claude, in future
> passes — without losing detection quality, reliability or user experience.
>
> **Status:** P1 (engine configuration) and P2 (Rust toolchain and formats) are complete —
> [P1_PLAN.md](P1_PLAN.md), [P2_PLAN.md](P2_PLAN.md), [docs/RUST_P1_P2_REPORT.md](docs/RUST_P1_P2_REPORT.md).
> P3A (the engine in Rust, full-state parity with C++) is complete — [docs/RUST_P3A_REPORT.md](docs/RUST_P3A_REPORT.md).
> P3 is split into P3A and P3B ([P3_PLAN.md](P3_PLAN.md) §5: one phase would be more than twice
> P1 + P2 and would mix two kinds of proof). P3A (engine port, differential proof) is complete —
> [P3A_PLAN.md](P3A_PLAN.md), [docs/RUST_P3A_REPORT.md](docs/RUST_P3A_REPORT.md). Next: P3B (test
> parity and the rest of `detection/`).

Contents: §1 Summary · §2 Current architecture · §3 Why Rust (and why not) · §4 Layer-by-layer
feasibility · §5 JNI, injection, hooks and platform integration · §6 Migration strategy and
interop · §7 Roadmap · §8 Validation requirements · §9 Risks · §10 Preparation completed in
1.0-dev.9 · §11 Working agreements for future passes.

---

## 1. Summary

| Question | Answer |
|---|---|
| Is a full Rust Vigil feasible? | **Yes.** Every layer has a mature Rust equivalent (`jni`, `windows`, `gl`/`glow`, `serde`). Nothing in Vigil depends on a C++-only capability. |
| Is it worth it? | Yes for the detection core (largest correctness surface, most churn, best test leverage); yes eventually for the rest, mainly for one language, memory safety in an injected DLL, and Cargo's tooling. The value is lowest — and the risk highest — at the JNI and hook boundary. |
| Safest order | **Detection engine first, platform hooks last.** Not JNI first: the JNI adapter and hooks can only be validated inside Lunar Client, while the engine can be proven behaviorally equivalent offline using traces, replay outcomes, generated scenarios and parity validation. |
| Interop model | Strangler pattern behind a narrow C ABI: a Rust `staticlib` linked into the existing DLL, one subsystem at a time, each proven behaviorally equivalent before it replaces the C++ one. |
| Key enabler | The engine is a pure function of a `WorldFrame` sequence + settings (§10). Traces make C++ and Rust outputs directly comparable. |
| Highest risks | Floating-point parity of the engine; Rust panics crossing into the JVM/game; behavior of hooks and JNI under Lunar that can only be observed live; antivirus heuristics on a new binary shape. |
| Estimated scale | ~24 k lines of C++ (excluding tests/tools): detection 5.5 k, UI 10.5 k, game 5.5 k, platform 1.3 k, services 1.4 k, core 0.8 k. |

## 2. Current architecture (as of 1.0-dev.9)

Full description: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

| Layer | Lines | Files | Nature | External surface |
|---|---|---|---|---|
| `core/` (json, log, paths, settings) | 767 | 8 | Utilities + global settings registry | Filesystem, `SHGetKnownFolderPath` |
| `detection/` | 5 506 | 35 | Pure, deterministic, single-threaded | None (std only) |
| `utilities/` | 470 | 2 | Pure logic + small state machines | None (runtime glue lives in `game/`) |
| `identity/` + `net/` | 919 | 5 | Pure helpers + a worker thread + WinHTTP | HTTPS (Mojang, Bordic) |
| `ui/` | 10 461 | 35 | Immediate-mode UI over OpenGL 1.x, GDI glyph rasterization, winmm | OpenGL, GDI, clipboard, shell |
| `game/` | 5 531 | 22 | JNI adapter: ~1 300 JNI call sites, ~30 obfuscated-name fallbacks, bytecode shims | The JVM inside Lunar |
| `platform/` | 1 308 | 6 | DllMain, SwapBuffers IAT hook, WndProc subclass, launcher, runtime guard | Win32 process/module APIs |
| tests / tools | 3 358 | 13 | 5 suites (511 expectations), preview, replay, CI scripts | — |

**Maintainability today.** The layering is clean and now linker-enforced (`vigil_core` →
`vigil_detection` / `vigil_services` → `vigil_ui` → DLL/exe). Since P1 the engine reads its
parameters only from an `EngineConfig` snapshot (no globals); the remaining coupling that
complicates a port is that the UI reads `PlayerRecord`s directly from the engine.

## 3. Why Rust — and why not

**Benefits**

| Benefit | Where it matters most |
|---|---|
| Memory safety in an injected DLL: a use-after-free or overflow crashes the *user's game* | `game/` (global refs, string buffers), `ui/` (immediate-mode state), `platform/` |
| Fearless concurrency | Identity worker, any future background analysis |
| Cargo: tests, benches, fuzzing (`cargo fuzz`), property tests (`proptest`), clippy | Detection (property tests for confidence invariants), parsers (traces, snapshots, JSON from other people) |
| `serde` instead of a hand-written JSON layer | Settings, marks, snapshots, traces |
| Enums with data, exhaustive `match` | Phases, evidence roles, check categories, UI state machines |
| One language for the whole project | Long-term maintenance by Claude |

**Costs and honest limits**

* The C++ code is not unsafe by accident: it is small, tested and works in game. A port buys
  safety and tooling, not features.
* The JNI and hook layers are *inherently* `unsafe` in Rust too; the benefit there is smaller and
  the validation cost is the highest (live only).
* Two toolchains during the transition (MSVC + rustc), mixed builds, FFI glue.
* A new binary shape may change antivirus heuristics for an injected DLL (§9).
* The UI is the largest layer and has no correctness risk; porting it is mostly effort.

**Conclusion.** Migrate, but in the order that maximizes value per unit of risk: the engine first
(highest value, fully verifiable offline), the platform boundary last (lowest value, verifiable only
live).

## 4. Layer-by-layer feasibility

| Layer | Rust approach | Complexity | Risk | Verifiable offline? | Order |
|---|---|---|---|---|---|
| Detection engine | Plain Rust crate `vigil-detection`; same structs; `f64` math | M (5.5 k lines, 11 checks) | **Medium**: float parity, subtle state | Yes, via traces, replay outcomes, generated scenarios and parity validation | **1st** |
| Records & formats (marks, snapshots, traces, outcomes) | `serde` structs mirroring the formats | S | Low: formats are versioned and tested | Yes (round trips, fixtures) | 1st (with engine) |
| Utilities logic | Plain Rust | S | Low | Yes (feature tests) | 2nd |
| Identity + network | `ureq`/`reqwest` with rustls, or WinHTTP via `windows` | S–M | Low–medium: TLS stack choice, proxy behavior | Mostly (fetcher is injectable) | 2nd |
| Core (settings registry, log, paths) | `serde` + a typed registry; `directories`/`windows` for paths | S | Low, but cross-cutting | Yes | Grows with each phase |
| Launcher (`Vigil.exe`) | `windows` crate; same embed-and-extract design | M | Medium: injection path, AV heuristics | Partly (`--self-check`), injection only live | 4th |
| UI | Port the immediate-mode toolkit as is; `gl`/`glow`; keep GDI rasterization via `windows` first | **L** (10.5 k) | Medium: visual regressions | Yes, via preview screenshot diffs | 3rd |
| Game adapter (JNI) | `jni` crate; same bindings table, same fallbacks | **L** (1 300 call sites) | **High**: only observable in Lunar | No | 5th |
| Hooks + DllMain | `windows` crate; IAT patching and WndProc subclassing by hand | M | **Highest**: crashes the game if wrong; loader-lock rules | No | **Last** |

## 5. JNI, injection, rendering hooks and platform integration

| Piece | What it does today | Rust feasibility | Risk | Stay in C++ until |
|---|---|---|---|---|
| **Injection** (`launcher.cpp`) | `CreateRemoteThread(LoadLibraryW)` into the game, events + shared memory handshake | Straightforward with `windows` | Medium: AV heuristics may treat a Rust launcher differently; behavior otherwise identical | Phase 4 (after the UI, before JNI) |
| **DllMain / bootstrap** | Spawns the bootstrap thread, attaches to the JVM, defines the pump class | Feasible; `DllMain` must stay minimal (loader lock) | High: loader-lock deadlocks, thread attach | Last phase |
| **SwapBuffers IAT hook** | Patches import slots of loaded modules (skipping overlays) to call Vigil before present | Feasible with `windows` + `unsafe` | **Highest**: wrong patch = game crash; interaction with other overlays | Last phase |
| **WndProc subclass + probe** | Routes input while the menu is open; re-hooks only when out of the chain | Feasible | High: recursion with other tools (fixed in dev.8; must not regress) | Last phase |
| **JNI adapter** (`game/`) | Resolves classes/fields/methods by MCP name with confirmed obfuscated fallbacks; reads entities each tick; global refs; exception clearing | `jni` crate covers everything used (`GetFieldID`, `Call*Method`, global/weak refs, `DefineClass`, `RegisterNatives`) | **High**: 1 300 call sites, only verifiable live; `JNIEnv` is thread-bound; every exception must be cleared | Phase 5 |
| **Bytecode shims** (`java/` → `bytecode/*.h`) | `VigilClientPump`, menu host screen, render hooks defined into Lunar's loader | Unchanged: bytes are data; `include_bytes!` | Low | Moves with JNI |
| **Rendering** (`ui/gfx`, fonts) | OpenGL 1.x immediate drawing in the game's context; GDI glyph atlas | `gl` bindings work in a foreign context; GDI via `windows` | Medium: GL state leakage into the game (must save/restore exactly as today) | Phase 3 |
| **Clipboard, shell, PlaySound** | Win32 calls | Trivial with `windows` | Low | With the UI |

**Panic policy (non-negotiable).** Every `extern "C"`/`extern "system"` entry point (JNI natives,
hook callbacks, WndProc, exported functions) wraps its body in `std::panic::catch_unwind` and
converts a panic into "do nothing this frame" plus a log line. The crate is built with
`panic = "unwind"` so this is possible; `panic = "abort"` would turn any bug into a crash of the
user's game.

**Why not JNI first.** It is tempting because JNI is where C++ is most dangerous. But it is also
where a port can be checked least: there is no offline oracle for "Lunar returned the right
field". Porting it first would replace well-tested, live-confirmed code with code that can only be
confirmed by hand, one release at a time, while giving no leverage to the rest of the migration.
The engine, by contrast, can be proven equivalent on every commit.

## 6. Migration strategy and interop

### 6.1 Strangler pattern with a narrow C ABI

```
Vigil.dll (C++)                                   vigil_rs.lib (Rust staticlib)
┌──────────────────────────────┐   C ABI        ┌─────────────────────────────────┐
│ game/ sampler → WorldFrame   │──────────────▶ │ vigil_engine_frame(&FrameFfi)   │
│ ui/ reads records            │◀────────────── │ vigil_engine_records_since(rev) │
│ settings registry            │──────────────▶ │ vigil_engine_set_config(json)   │
└──────────────────────────────┘                └─────────────────────────────────┘
```

* **Boundary types are flat and versioned**: `FrameFfi`/`PlayerFrameFfi` are `#[repr(C)]` mirrors
  of `WorldFrame`/`PlayerFrame` (strings as `ptr+len`), generated headers via `cbindgen`, a
  `vigil_abi_version()` checked at startup.
* **Outputs cross as data, not pointers**: the UI keeps its C++ `PlayerRecord` mirror, updated
  incrementally from the engine's `Session::revision()` (only changed records are serialized). This
  keeps the UI port independent of the engine port.
* **Settings cross as one config document** (JSON or a flat key/value array) on change, replacing
  the `Setting*` reads inside checks (preparation step P1, §7). ✅ P1: `EngineConfig` +
  `configToJson` (`vigil-engine-config` format 1); the C++ host already hands the engine a new
  snapshot between frames (`SettingsSync`), which is exactly where `vigil_engine_set_config` will go.
* `cxx` was considered; a hand-written C ABI is preferred because the boundary is small, data-only
  and must be stable across many releases with two independent build systems.

### 6.2 Build integration

* Cargo workspace under `rust/` (`vigil-detection`, `vigil-formats`, later `vigil-ui`, `vigil-game`).
* CMake invokes `cargo build --release --target x86_64-pc-windows-msvc` (via `corrosion` or a
  custom command) and links the `staticlib` into `Vigil.dll`; the same CRT (`/MT`) on both sides.
* CI adds `cargo fmt --check`, `cargo clippy -D warnings`, `cargo test`, and the parity job (§8).

**Status after P2:** the workspace exists (`rust/`, toolchain pinned to 1.98.1 in
`rust/rust-toolchain.toml`, `unsafe_code = "forbid"`, `panic = "unwind"`, no third-party crates);
`-DVIGIL_BUILD_RUST=ON` builds it from CMake with a custom command and adds the Rust tests and the
C++ ↔ Rust round trips to CTest; CI runs fmt, clippy, tests, the round trips and `replay-check` in
the required build job. Linking a `staticlib` (and `corrosion` vs. the custom command) is decided
in P4, when there is something to link.

**Status after P3A:** `crates/vigil-detection` holds the engine (no dependencies besides
`vigil-formats`); `replay-check` moved there and gained `--engine`; `vigil-parity` is the twin of
`VigilParity`. CI adds `replay-check --strict --engine` and `tools/ci/engine_parity.py` (corpus ×
416 configurations and 1 000 generated scenarios, full state, byte for byte) to the required build
job; CTest runs the same with `-DVIGIL_BUILD_RUST=ON`.

### 6.3 Shadow mode before switching

For the engine (and later utilities), the DLL runs **both** implementations on every frame for at
least one release: C++ drives the product, Rust runs in the shadow, and every outcome difference is
counted and logged (Status → Diagnostics shows "engine parity: N differences"). Only a release with no 
meaningful behavioral differences and successful offline parity validation may switch the default.

Observed differences must be reviewed and classified as:

- Expected implementation differences
- Benign differences
- Actual regressions

Only actual regressions block promotion.

## 7. Roadmap

Each phase ends with a release. No phase starts before the previous one's exit criteria hold.

| Phase | Scope | Exit criteria |
|---|---|---|
| **P0 — Preparation** ✅ (this pass) | Library split; traces + replay + outcome comparison; golden trace; determinism tests; exact JSON numbers; engine contract documented | Done (§10) |
| **P1 — Decouple config** (C++) ✅ | Checks read an immutable `EngineConfig` snapshot built from the registry on change, instead of `Setting*`; engine owns no globals | All suites + golden unchanged; config snapshot serializable — met: [P1_PLAN.md](P1_PLAN.md), [report](docs/RUST_P1_P2_REPORT.md) |
| **P2 — Rust toolchain, formats** ✅ | Cargo workspace; `vigil-formats` (traces, outcomes, snapshots, marks) with fixtures generated by the C++ side; Rust `replay-check` that reads a trace and an outcome | Rust parses every committed fixture; C++ ↔ Rust round trips identical — met: [P2_PLAN.md](P2_PLAN.md), [report](docs/RUST_P1_P2_REPORT.md) |
| **P3 — Rust engine (offline)** | Port `detection/` check by check behind the same outcome; property tests for confidence invariants; differential testing | No meaningful behavioral differences on the golden trace and on ≥ 1 000 generated scenarios. Detection outcomes, evidence generation and confidence trends must remain equivalent. Every C++ detection test mirrored. Met when **P3A and P3B** are both met ([P3_PLAN.md](P3_PLAN.md) §5) |
| ↳ **P3A — Engine port, differential proof** ✅ | The complete per-frame engine in Rust; engine-facing marks; differential harness; ≥ 1 000 generated scenarios; `replay-check --engine`; tick-cost benchmark ([P3A_PLAN.md](P3A_PLAN.md)) | Full engine state byte-identical to C++ on the golden trace, the corpus under all 416 configurations and ≥ 1 000 generated scenarios; golden outcome reproduced; declaration identical; deterministic; no panic on hostile input; falsification recorded — met: [report](docs/RUST_P3A_REPORT.md) |
| ↳ **P3B — Test parity, rest of `detection/`** | Rust simulator; every C++ engine test mirrored by name; property tests for the confidence invariants; full marks state machine, profile capture, history/journal/log sinks | Test parity matrix complete and green; every invariant property-tested and falsified; sinks write the C++ bytes; P3A parity still holds |
| **P4 — Engine in the DLL (shadow)** | Link `vigil_rs.lib`; C ABI; run in shadow mode; parity counter in diagnostics | One release with no meaningful live differences across the live checklist sessions. All observed differences must be reviewed and classified. No unresolved regressions may remain. |
| **P5 — Engine switch** | Rust engine drives; C++ engine kept one release as fallback (hidden setting), then deleted | No regression reports for one release; C++ engine removed |
| **P6 — Services** | Utilities and identity (fetcher injectable, same tests) | Feature tests ported and green; network behavior identical (requests logged) |
| **P7 — UI** | Port widgets, pages, HUD, theme, fonts; preview harness in Rust | Preview screenshot diffs within tolerance on every page × 3 themes × 2 scales; keyboard and a11y tests ported. Minor visual differences that do not change functionality, accessibility, workflow, or overall visual identity are acceptable. |
| **P8 — Launcher** | `Vigil.exe` in Rust (self-check, extraction, handshake, injection) | `--self-check` parity; live checklist L13; AV spot checks |
| **P9 — Game adapter** | JNI layer in Rust, bindings table + fallbacks, binding report | Live checklist fully green on two Lunar versions; binding report identical |
| **P10 — Platform hooks** | DllMain, IAT and WndProc hooks in Rust; C++ removed | Live checklist + overlay compatibility (L12) green; no crashes over a week of use |

**Migrate first:** engine, formats, utilities (pure, offline-verifiable, highest leverage).
**Migrate later:** UI (large but verifiable by screenshots), launcher.
**Keep in C++ longest:** JNI adapter, then DllMain/hooks (only live-verifiable, highest crash
impact, lowest marginal benefit).
**Need more validation before migrating:** the JNI adapter (more live coverage of L3–L12 first:
several ⚠️ items in [docs/LIVE_VALIDATION.md](docs/LIVE_VALIDATION.md) must become ✅ before
porting code that depends on them), and hooks (an overlay compatibility matrix).

## 8. Validation requirements for every migration step

| Quality | Requirement | How |
|---|---|---|
| **Detection quality** | Equivalent outcomes on all traces. Evidence generation, confidence trends and final detection behavior must remain materially equivalent. Minor floating-point differences are acceptable if they do not meaningfully affect user-facing behavior. | Golden + generated traces through both engines (`compareOutcomes`) in CI; shadow mode live |
| **Testability** | Every C++ test has a Rust counterpart with the same name before the C++ code is removed; coverage never drops | Test parity matrix in the phase's report |
| **Reliability** | No panic can escape an FFI entry point; no new crash class | `catch_unwind` audit (grep for `extern`), fuzzing of every parser (`cargo fuzz` on traces, snapshots, JSON), live checklist |
| **Performance** | Engine tick cost ≤ C++ on the golden trace; UI frame time ≤ C++ | Bench harness on traces; Status tick cost in live sessions |
| **Determinism** | Same trace → same outcome in Rust, run twice and across builds | `validation` determinism test ported |
| **Formats** | Files written by either side read by the other | Fixture round trips in both directions |
| **User experience** | No visible change unless intended | Preview screenshot diffs; live checklist |
| **Documentation** | ARCHITECTURE, VALIDATION and this plan updated in the same pass as the code | Link check in CI |

**Floating-point parity.** C++ (MSVC, `/fp:precise`, no FMA contraction) and Rust (no implicit
FMA) evaluate the same `f64` expressions identically when the operation order is identical. Two
traps: (1) transcendental functions (`sin`, `atan2`, `pow`, `exp`) may come from different
implementations — the Rust MSVC target calls the same CRT, but this must be verified by a test that
compares a table of values; (2) `f32` vs `f64` promotion must be mirrored exactly (several fields
are `float` in C++). Port expression-by-expression first; refactor only after parity holds.

**Verified in P2:** `tests/fixtures/formats/math.txt` holds 2 000 results of `sin`, `cos`, `atan2`,
`pow`, `sqrt`, `fmod`, `floor`, `ceil`, `round` and `fmax` computed by the MSVC CRT on the engine's
input ranges (angles, decay and friction exponents, yaws); Rust's `f64` functions on
`x86_64-pc-windows-msvc` return the same bits for all of them (`cargo test`). The test stays in CI,
so a toolchain or CRT change that breaks this is caught before P3 relies on it.

**Verified in P3A:** `powf` (the confidence gain is `std::pow(float, float)`) and `pow(double, int)`
are bit-exact too, and — beyond arithmetic — the two other sources of output differences the audit
found are reproduced exactly: MSVC `std::unordered_map` iteration order and UCRT `printf` text
(P3_PLAN §2.1). The port needed no tolerance: the full engine state is byte-identical.

## 9. Risks

| Risk | L | I | Mitigation |
|---|---|---|---|
| Float differences flip a borderline threshold | M | H | Expression-identical port, CRT math table test, generated traces around thresholds, shadow mode |
| A Rust panic crosses into the JVM or a hook | M | H | `catch_unwind` at every entry; `panic = "unwind"`; audit in review |
| Two runtimes/allocators in one DLL (C++ and Rust heaps) | M | M | Never free across the boundary; ownership stays on the allocating side; data copied at the ABI |
| JNI port misses an exception check | M | H | Wrapper type that clears and logs exceptions on every call; binding report parity; live checklist |
| Hook port changes timing or GL state | M | H | Port last; identical state save/restore; overlay matrix (L12) |
| AV heuristics react to the new binary | M | M | Keep the same transparent behavior (no packing, plain resource, signed releases if possible); spot-check with common AV before switching the launcher |
| Long hybrid period doubles maintenance | H | M | Strict phase exit criteria; delete the C++ side one release after each switch |
| Build complexity (CMake + Cargo) | M | L | One documented build command; CI builds both; `corrosion` or a single custom command |
| Scope creep during porting | M | M | Ports are behavior-preserving; improvements happen before or after a phase, never during |

## 10. Preparation completed in 1.0-dev.9

| Preparation | Why it matters for the port |
|---|---|
| **Linker-enforced library split** (`vigil_core`, `vigil_detection`, `vigil_services`, `vigil_ui`) | The first port target (`vigil_detection`) is exactly one library with no Windows, UI or network dependency; tests and the replay tool prove it links alone |
| **Diagnostic trace format** (`detection/trace.*`) with exact number round trips | The common input language of both engines; real sessions become parity fixtures |
| **Outcome comparison** (`detection/replay.*`, `VigilReplay`) | The parity oracle: "do two engines agree?" is one function call |
| **Golden trace + committed outcome** in CI | A fixed reference the Rust engine must reproduce |
| **Determinism tests** (two engines; through the trace format) | Establishes that the C++ engine *is* a function of its input — the property the port relies on |
| **Exact JSON numbers** (`core/json`) | Formats written by C++ are bit-exact fixtures for Rust readers |
| **Versioned formats with guards** (marks v2, snapshots v1, traces v1, settings keep unknown keys) | Either implementation can read the other's files without destroying data |
| **Engine contract documented** ([docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) §6) | Defines precisely what "the same behavior" means |
| **Envelopes derived centrally** (`deriveEnvelope`) rather than per check | One function to port instead of eleven |
| **Shared test framework and simulator** (`tests/test_framework.h`, `tests/sim.h`, `tests/scenarios.h`) | Scenario generation for differential testing exists already |

### 10.1 Added by P1 and P2

| Preparation | Why it matters for P3 and later |
|---|---|
| **`EngineConfig` snapshot + schema** (P1) | The Rust engine receives one configuration document, exactly what the C++ engine runs on; `vigil-engine-config` documents round-trip in both languages |
| **Engine owns no globals** (P1, CI-enforced) | The engine is a pure function of frames + configuration, the property parity rests on |
| **Scenario corpus** (`tests/corpus.h`, 7 scenarios, all 11 checks and 24 indicators) | The starting set of the ≥ 1 000 generated scenarios P3 must agree on |
| **`VigilParity`** (full-state digests under 416 configurations) | The detailed C++ reference P3 compares a Rust engine against, beyond the outcome summary |
| **`vigil-formats`** (P2) | The Rust engine reads traces and configurations and writes outcomes without new format code |
| **`compare` (outcome oracle) in Rust** (P2) | `replay-check` only needs the engine added to become the P3 differential tester |
| **CRT math table** (P2) | Floating-point parity of the engine's transcendental functions is already proven on this toolchain |

### 10.2 Added by P3A

| Preparation | Why it matters for P3B and later |
|---|---|
| **`vigil-detection`** (the engine in Rust, full-state parity with C++) | P3B ports tests onto an engine already proven identical; P4 links it and needs only the C ABI around it |
| **Differential harness** (`VigilParity --export/--generate`, `vigil-parity`, `engine_parity.py`) | Any later engine change is checked against C++ on every build until the C++ engine is removed (P5); the shadow mode of P4 can reuse the dump to explain a live difference |
| **Generated scenarios** (`tests/generator.h`) | ≥ 1 000 random sessions per run that reach all 24 indicators, stalls, pruning, binding and name collisions — paths the hand-made corpus does not |
| **C++ semantics fixtures** (`hash-order.txt`, `printf.txt`, `powf`/`powi` rows, `checks.json`) | The facts about MSVC/UCRT behavior the engine's output depends on, re-checked whenever the toolchain changes |

## 11. Working agreements for future passes (Claude)

1. Read this document, [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and
   [docs/VALIDATION.md](docs/VALIDATION.md) before starting a phase.
2. One phase per pass by default. Write the phase's plan (audit + exit criteria) before code, as with every Vigil pass.

   Adjacent low-risk phases may be combined when:
   - Exit criteria remain independent
   - Validation remains clear
   - Risk remains acceptable

   Examples:
   - P1 + P2 may be combined
   - P3, P4 and P5 should remain separate

3. Port behavior first, improve later: no threshold, wording or layout changes inside a port.
4. Never remove the C++ implementation of a subsystem in the same release that switches to Rust.
5. Keep every file format and the C ABI versioned; bump versions instead of changing meaning.
6. Record falsification for new parity tests (break the Rust side on purpose, see the test fail).
7. Update the phase table in §7 (✅) and the live checklist when a phase lands.
