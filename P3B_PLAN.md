# P3B — Test parity and the rest of `detection/` (plan)

Second half of RUST_PLAN P3, as split in [P3_PLAN.md](P3_PLAN.md) §5. Written before any P3B code,
after an audit of the completed P3A work (§1). **P4 is not part of this pass**: nothing is linked
into `Vigil.dll`, no C ABI is written, nothing runs in game.

| | |
|---|---|
| **Scope (P3_PLAN W7–W9)** | A Rust simulator and every C++ engine test mirrored by name; property tests for every confidence invariant of VALIDATION §5, each falsified; the rest of `detection/` in Rust: the full marks state machine and its file, profile capture and the profile store, the cross-session history file, the session journal and the "detections" log channel |
| **Exit criteria (P3_PLAN §5.1)** | Test parity matrix complete and green; every invariant property-tested and falsified; the sinks write the C++ bytes; P3A parity still holds |
| **Not in scope** | P4 (staticlib, C ABI, shadow mode); the settings registry and `config_binding` (host, stay C++, P3_PLAN §2); `core/log`'s in-memory "recent lines" ring (UI); any C++ engine change |
| **Baseline** | `git archive HEAD` (2bd3f48 = P3A committed as 7b4f98f/70b1af7, released as 1.0-dev.14), built in the scratchpad with the CI configuration (`/W4 /WX`, DLL, launcher, `-DVIGIL_BUILD_RUST=ON`) |

---

## 1. Audit of P3A

### 1.1 Exit criteria, re-verified on the baseline build

Every P3A exit criterion ([P3A_PLAN.md](P3A_PLAN.md) §6) was re-run on a clean build of HEAD, not
taken from the report.

| P3A criterion | Re-run | Result |
|---|---|---|
| Full engine state byte-identical on the golden trace and the corpus under all 416 configurations | `engine_parity.py` corpus half | see §1.3 |
| Byte-identical on ≥ 1 000 generated scenarios | `engine_parity.py --count 1000` (seed 20261127), generation run twice | see §1.3 |
| Golden outcome reproduced byte for byte | `replay-check --strict --engine` | see §1.3 |
| Declaration identical; deterministic; no panic on hostile input; gates green | `cargo test --release` (`declaration.rs`, `engine.rs`, `foundations.rs`), `cargo fmt --check`, `clippy -D warnings` | see §1.3 |
| No C++ behavior change | `git diff d7e1614 HEAD -- src`: only `src/platform/launcher.cpp` (the user's taskbar-title fix, 6fc70fa, not P3A); `src/detection` untouched. Suites 66 tests / 550 expectations; golden; `VigilParity` deterministic | ✅ |
| Tick cost measured | report V11 (0.94× on the golden trace); re-measured in P3B with the sinks present (V12 below) | — |
| Falsification recorded | report §5 (F11–F18); spot-checked again (§1.3) | see §1.3 |
| CI runs V3–V5; docs updated | `validate.yml` build job: `replay-check --strict --engine`, `engine_parity.py --count 1000`; CTest `engine_parity`; RUST_PLAN, VALIDATION, ARCHITECTURE, rust/README updated | ✅ |

### 1.2 Findings of the code audit

| # | Finding | Effect today | Action in P3B |
|---|---|---|---|
| A1 | `Engine::set_config` (Rust) accepts any snapshot of the right length; C++ also requires the same schema object or a *compatible* schema (same keys and types in order). A Rust `EngineConfig` carries no schema | None: every Rust snapshot is built against `Engine::schema()` (documents are parsed against it). A same-length snapshot of a different schema cannot be produced by any caller today | Recorded (expected implementation difference). P4 must pass configuration *documents* across the ABI, never raw arrays (noted for P4) |
| A2 | `Marks::default()` starts at revision 0 (C++ 1); `Marks::load` sets 2 (C++: 1 + one load) | Not observable (no dump prints it) | Fixed by the full marks port (revision mirrors C++) |
| A3 | History, journal and log sinks are absent; `Session::record` never applies history, so `priorSessions`/`priorDetections`/`priorPeak`/`lastSeenEpochMs` are never exercised — and the `VigilParity` dump does not print them either | The P3A proof says nothing about those four fields (they never influence a decision; profiles and the UI show them) | Ported, and a `prior` digest is added to the generated sink comparison (§4, V5) |
| A4 | Marks binding writes the marks file in C++ (`changed()` → `save()`); the Rust engine binds in memory only | The C++ harness has always rewritten its *work copy*; nothing compared it | The marks file is written and compared (V5) |
| A5 | 129 tracked files (most fixtures, several docs and some Rust sources) are stored with CRLF in the index since the P3A commit; `.gitattributes` (`tests/traces/** -text`) is still untracked | None: every reader strips `\r`, `VigilFormats check` compares without `\r`, rustfmt `newline_style = "Auto"` | Recorded with a recommendation (report §9); not changed (repository policy is the user's) |
| A6 | `vigil-formats::to_i32` saturates where MSVC `cvttsd2si` gives `INT_MIN` (documented P2 deviation for out-of-range JSON numbers) | Formats only; the engine uses `cpp::to_int` | History loading (new engine code) uses `cpp::to_int`, so hostile history files behave as in C++ |

### 1.3 Audit results

Run on a clean CI-equivalent build of HEAD in the scratchpad (CTest 9/9, `engine_parity` 299 s).

| Check | Result |
|---|---|
| C++ suites | detection 32 / 152, features 7 / 52, records 11 / 78, validation 11 / 208, ui_logic 5 / 60 — 66 tests, 550 expectations, 0 failed |
| Golden trace (`VigilReplay --expect`) | matches |
| `VigilParity` twice | byte-identical (SHA-256 `BD8432B1…`) |
| `replay-check --strict --engine` | the Rust outcome file = committed `golden-arena.expect.json` |
| `engine_parity.py --count 1000` | corpus IDENTICAL (2 912 full-state digests, registry declaration, default dumps; 1 978 496 frames); generated IDENTICAL (1 000 scenarios, 525 982 frames) and deterministic; coverage exactly as reported (evidence 723, detections 420, combat 627, stalls 321, filtered 247, pruned 4, suffixed 89, exempt 230, > 1 scene 465; 24 of 24 indicators) |
| Rust gates | `cargo fmt --check`, `clippy --all-targets -D warnings`, `cargo test --release` (29 tests) green; no `unsafe`; `Cargo.lock` holds only the two workspace crates |
| `validate_repo` | metadata (1.0-dev.14), links (31 files), golden, engine-globals clean |
| `Vigil.exe --self-check` | ok; embedded runtime = built DLL |
| Falsification spot checks | F12 (gain in `f64`): 2 889 corpus runs / 663 generated scenarios differ — exactly as reported. F15 (duplicate tie-break reversed): 0 / 75 — exactly as reported (flipping only the update-tick comparison gives 0 / 63: the report's fault reverses the whole decision) |

**Conclusion: every P3A exit criterion holds on the committed tree.** P3B may start.

## 2. Inventory

### 2.1 Tests to mirror

The engine-level C++ suites: `detection` (32 tests), `validation` (11), `records` (11) — 54 tests,
438 expectations. `features` and `ui_logic` test services and UI (later phases).

Classification, decided before porting:

| Class | Tests | Rule |
|---|---|---|
| **Full** | 48 | Every expectation mirrored; the Rust test must count the same number of expectations as the C++ test (counts can be data-dependent — loops over evidence — and then agree only if behavior agrees) |
| **Full, registry stand-in** | `presets keep defaults`, `config schema`, `config sync between frames` | Their expectations go through the C++ settings registry (`Setting::set` step snapping, `registerEngineSettings`, `SettingsSync`), which stays in C++ (host). The Rust test uses a stand-in of exactly those three behaviors (tests/common/registry.rs, ~40 lines) so the engine-facing claims — Balanced presets equal the defaults after the registry's snapping, the defaults survive the registry's conversions, snapshots reach the engine only between frames — are checked against the Rust engine. Same expectation count |
| **Partial (host-only expectations)** | `json & settings` (5 of 10 host), `config engines share nothing` (1 of 5 host) | The JSON half and the engine half are mirrored; the expectations about `core/settings` (persistence, step snapping of a free-standing setting) and "running engines never touch the settings registry" (the Rust engine cannot reach one: none exists in the crate) are listed as host-only |
| **Host** | `settings keep unknown keys` | Tests `core/settings` only; no Rust counterpart until the core layer is ported (RUST_PLAN §4 "grows with each phase") |

### 2.2 The rest of `detection/`

| C++ | What | Rust (new or completed) |
|---|---|---|
| `marks.{h,cpp}` | Watch / confirm / exempt rules, notes, reasons, remove, add by name, binding, remember name, `all()` order, search, counts, revision, persistence (format 2, atomic write, read-only guard) | `marks` completed; persistence through `vigil_formats::marks` |
| `snapshot.{h,cpp}` | `CheckCatalog::of`, `captureSnapshot`, `snapshotToJson`/`FromJson` (formats ported in P2), `SnapshotStore` (save, load, list/refresh, note, export, import, pending imports, delete, unique ids) | `snapshot` (new) |
| `session.{h,cpp}` | `loadHistory`, `applyHistory`, `saveHistory` (merge), `journal`, `startEpochMs` | `session` completed |
| `engine.cpp` | `flushHistory` (at `resetSession` and every 60 s of engine time while recording), journal path per session, evidence and detection journal lines, `log::writef("detections", …)` for detections and debug lines | `engine` completed |
| `core/log` (`write`, file part) | `<dir>/<channel>.log`, `YYYY-MM-DD HH:MM:SS  message\n`, rotation to `.old` above 4 MiB; `writef` cuts messages at 2 047 bytes | `sinks::LogFile` (the file half only; the recent-lines ring is UI) |
| `std::sort` (MSVC) | Order of `Marks::all()` and of the profile list on ties (`std::sort` is not stable above 32 elements) | `msvcsort` (introsort exactly as MSVC: insertion sort ≤ 32, median-of-9 fat partition, heap sort fallback), proven by a C++ fixture |
| wall clock, local time | `epochNowMs()`, `system_clock` (journal name, history `lastSeen`, mark times, profile `savedAt`), `localtime_s` + `strftime` (profile ids, log lines) | `clock`: an injectable epoch clock (default: the system clock) and an injectable local-time conversion (default: UTC — Rust `std` has no time zones and `unsafe`/dependencies are not allowed; the host supplies the conversion in P4). Wall-clock values never influence a decision (ARCHITECTURE §6) |

## 3. Design

### 3.1 Test infrastructure (rust/crates/vigil-detection/tests/common)

* `rng.rs` — `std::mt19937` and the MSVC `generate_canonical`, `uniform_real_distribution<double>`
  and `discrete_distribution<int>` the C++ tests use. Proven by a new C++-generated fixture
  `tests/fixtures/formats/random.txt`.
* `sim.rs` — `tests/sim.h` line for line: `Sim` (vanilla movement, animations, relay delay,
  cadence, freezes), `World` (feeds the real engine as the adapter does), `setup_local`.
* `scenarios.rs` — `goldenArena()`. **Proof of the simulator port:** the Rust golden arena must
  equal the committed `tests/traces/golden-arena.vtrace` frame for frame (the C++ test checks the
  same of the C++ simulator).
* `mod.rs` — an `Expect` collector mirroring `EXPECT`: counts expectations, reports every failure
  with its message and location, fails the test at the end; prints `expectations <n> <C++ name>`.
* `registry.rs` — the registry stand-in of §2.1.

Suites: `tests/detection.rs`, `tests/validation.rs`, `tests/records.rs`; one `#[test]` per C++
test, the function name derived from the C++ name (lowercase, non-alphanumerics → `_`) and the
exact C++ name passed to `Expect::new`.

### 3.2 Test parity matrix (CI-enforced)

* `tests/test_framework.h` gains `--counts` (runs every test, prints `expectations <n> <name>`);
  the default output is unchanged.
* `tests/test-parity.json` (new, committed): the classification of §2.1 with the number of
  host-only expectations per test and the reason.
* `tools/ci/test_parity.py` (new): runs the three C++ suites with `--counts` and the three Rust
  suites (`--nocapture`), then requires for every C++ test a Rust test of the same name that
  passed, with `rust + host-only == C++` expectations; prints the matrix. In the CI build job and in
  CTest (`test_parity`, `-DVIGIL_BUILD_RUST=ON`).

### 3.3 Marks

The C++ state machine function for function (`touch`, `setWatch`, `confirm`, `unconfirm`,
`setExempt`, `setNote` (500-byte cut), `setReasons`, `remove`, `rememberName`, `addByName`,
`bindName` with merge, `findByName`, `all`, `search`, counts, revision, `readOnly`, `loadNote`).
Persistence: `set_path` loads (`vigil_formats::marks::MarksFile`), every change saves through
`MarksFile::to_json` with the C++ write sequence (create directories, `<path>.tmp`, rename, on
failure remove + rename). Timestamps from the injected clock. `all()` uses `msvcsort`.

### 3.4 Profiles

`snapshot::catalog_of(&Engine)`, `capture(&Engine, &PlayerRecord, version, clock) → Profile`
(record copied with `present = false`, the user's marks, the engine clock and session start), JSON
through `vigil_formats::profile`, and `SnapshotStore` with the C++ file operations, bounds (8 MiB,
500 entries), unique ids (`safeStem(name)-YYYYmmdd-HHMMSS[-n]`, local time injected) and `refresh`
order (`msvcsort`, saved time descending). **Proof:** the Rust capture of *LowHopper* from the
committed golden trace reproduces the committed `inputs/profile/session-hopper.json` byte for byte.

### 3.5 Sinks

* **History** — `Session::load_history(bytes)` / `apply_history` (on record creation) /
  `save_history` (merge of the loaded history with this session's non-local players that have
  evidence; sessions +1, detections + n, peak = `std::max`, lastSeen = now; integer arithmetic
  wraps, conversions `cvttsd2si`); `Engine::set_history_path` loads, `flush_history` writes
  (`data.history` on and a path set) at `reset_session` and every 60 s of engine time.
* **Journal** — `Engine::set_journal_directory`; per frame the path
  `<dir>/session-<startEpochMs>.jsonl` (`data.journal` on); evidence and detection lines exactly as
  `emit()` builds them (`json::write(j, -1)`), appended with `\n`.
* **Log channel** — an optional `LogSink` on the engine receives `("detections", message)` for every
  detection and debug line, cut at 2 047 bytes like `writef`; `sinks::LogFile` writes them like
  `core/log`.

### 3.6 Differential instruments added to the harness

| Instrument | C++ | Rust | Compared |
|---|---|---|---|
| **Generated sinks** (extends `--generate`) | Every generated scenario also runs with a history file (drawn per scenario from a separate generator: absent, broken, hostile values, entries for the scenario's players), a journal directory, a log directory, its marks work file; `data.journal`/`data.history` toggled mid-session in some scenarios. After every frame at which a history flush can happen, the history file is read | `vigil-parity` does the same with the Rust sinks | One `sinks` line per scenario: digests of the journal (all files in name order), every history snapshot, the log lines without their timestamps, the final marks file, captured profiles (and their round trip), and the four history fields of every record (`prior`). Wall-clock values are masked identically on both sides (§3.7). The digest and summary lines of P3A are unchanged |
| **Marks scripts** (`VigilParity --marks`) | N random action scripts over every marks operation (valid/invalid/colliding names, nicks, real accounts, name-only records, notes over 500 bytes, files: none, v1, v2 with ties and > 32 marks, newer, unreadable, not an object), reloading the file mid-script; the full state after every action | `vigil-parity` replays the scripts | Every state dump (all marks in `all()` order, search results for several queries and kinds, find-by-name, counts, revision, read-only, the file bytes) |
| **MSVC sort** (`sort-order.txt`) | `std::sort` on random sequences with many ties (sizes 0–1 500), sorted/reversed/organ-pipe inputs and McIlroy "killer" inputs that drive the heap-sort fallback | `msvcsort` | The permutation; the Rust test also requires that the heap fallback was reached |
| **RNG** (`random.txt`) | `mt19937`, `uniform_real_distribution<double>`, `discrete_distribution<int>` for the seeds and ranges the tests use | `common/rng.rs` | Every value, bit for bit |

### 3.7 Wall clock in comparisons

The C++ engine reads the wall clock in four places (H10): the journal file name, history
`lastSeen`, mark times and profile `savedAt`. The C++ tool records the wall-clock window of each
run; the Rust tool injects a clock with a fixed base. Both replace values inside their window with
`NOW` (history, generated marks) or with the index of the action that produced them (marks
scripts: the C++ tool waits for a new millisecond before each action, so each action's time is
unique), journal files are compared in name order without their names, and log lines without their
timestamp prefix (whose shape is checked). Profiles are captured with a fixed `savedAt` and version.
Everything else is compared byte for byte.

## 4. Validation

| # | Requirement | Method | Pass |
|---|---|---|---|
| V1 | C++ unchanged | C++ changes only in tooling and test headers; 5 suites (66 tests, 550 expectations); golden; `format_fixtures`; default `VigilParity` output byte-identical to the baseline | Identical |
| V2 | P3A parity still holds | `engine_parity.py` (corpus 2 912 runs + 1 000 generated); the generated `variant`/`summary` lines byte-identical to the baseline's `expected.txt`; `replay-check --strict --engine` | Identical |
| V3 | Test parity | `test_parity.py`: 53 of 54 tests mirrored by name (1 host), all green, expectation counts equal (host-only remainders as committed) | Complete |
| V4 | Simulator | Rust golden arena = committed trace; `random.txt` | Identical |
| V5 | Sinks | Generated sinks (§3.6) on 1 000 scenarios | Every `sinks` line identical; coverage reported |
| V6 | Marks | Marks scripts (≥ 500 scripts) | Every state identical |
| V7 | Profiles | Golden-trace capture = committed fixture; generated profiles (V5); store test | Byte-identical |
| V8 | Sort | `sort-order.txt` incl. the heap fallback | All permutations equal |
| V9 | Properties | `tests/properties.rs`: every VALIDATION §5 invariant over seeded random sequences and simulated sessions | Hold; each falsified |
| V10 | Cross-falsification | The C++ falsification documented in VALIDATION §3 (exemption rule removed) applied to both engines | The same tests fail with the same number of failed expectations |
| V11 | Robustness | Mutated history, marks, profile files and hostile paths through the new code | No panic |
| V12 | Performance | Tick cost C++ vs Rust (alternating runs) with the sink code present but unset | Ratio as in P3A (≤ 1.0 on the golden trace) |
| V13 | Gates | `cargo fmt --check`, `clippy --all-targets -D warnings`, `cargo test --release`, `/W4 /WX`, no `unsafe`, no dependencies, CI-equivalent clean build with `--self-check` | Green |
| V14 | Falsification of the new instruments | §5 | Each fault caught |

## 5. Falsification plan

Each fault applied alone to the Rust side (or, for V10, to both), rebuilt, run, restored.

| # | Fault | Expected to be caught by |
|---|---|---|
| F21 | Journal evidence line with `confidence` before `severity` | V5 (journal digests) |
| F22 | History merge takes the session peak instead of `std::max(prior, peak)` | V5 (history) |
| F23 | Log messages cut at 4 095 instead of 2 047 bytes | V5 (log) only if a long line occurs — else a unit test; recorded either way |
| F24 | `setExempt` keeps the reasons | V6 and `mark semantics` |
| F25 | `all()` with a stable sort instead of MSVC `std::sort` | V8 and V6 (ties > 32 marks) |
| F26 | Capture copies `watch` instead of `watch || confirmed` | V7 / V5 (profiles) |
| F27 | History applied to records created before the history loaded | V5 (`prior`) |
| P1–P13 | One fault per property (bounds, monotonicity, half-life, exact zero, gate, carry-over, cooldown, queue judged, post-game judged, hold while away, halving, exempt/teammate judged, envelope side) | V9: the corresponding property fails |
| X1 | `should_analyze` ignores exemptions (C++ and Rust) | V10 |
| X2 | Confidence never snaps to 0 below 0.05 (C++ and Rust) | V10 |

## 6. Exit criteria

| Criterion (P3_PLAN §5.1) | Evidence |
|---|---|
| Every C++ engine test has a same-named Rust counterpart that passes (test parity matrix) | V3, V4 |
| Property tests for every confidence invariant in VALIDATION §5, each falsified once | V9, §5 |
| Persistence sinks write the C++ bytes | V5, V6, V7 |
| P3A parity still holds | V2 |
| No C++ behavior change; gates green; falsification recorded; docs updated (RUST_PLAN §7, ARCHITECTURE, VALIDATION, rust/README) | V1, V13, V14 |

When both P3A and P3B are met, RUST_PLAN's P3 is met. P4 starts in a later pass.
