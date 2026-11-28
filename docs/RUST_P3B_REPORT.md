# Rust migration — P3B implementation report

Phase P3B of [RUST_PLAN.md](../RUST_PLAN.md) §7: the second half of P3 as split in
[P3_PLAN.md](../P3_PLAN.md) §5. Plan: [P3B_PLAN.md](../P3B_PLAN.md). With P3A
([report](RUST_P3A_REPORT.md)) this completes P3. **P4 was not started.**

| | |
|---|---|
| Status | ✅ exit criteria met; **RUST_PLAN P3 is complete** |
| User-visible change | None. No shipped binary contains Rust; no C++ engine code changed (C++ changes are limited to test headers and tools); no format changed |
| Evidence | Every C++ engine test (54) has a Rust twin of the same name — 53 mirrored, 1 tests the C++ settings registry only — with the same expectations (427 + 11 host-only = 438) and the **same frames**; 8 property tests cover every invariant of VALIDATION §5 and every one was falsified; the Rust marks, profiles, history, journal and log channel write the C++ bytes on 1 000 generated scenarios and 300 marks scripts; P3A parity still holds (corpus 2 912 runs, 1 000 generated scenarios) with the P3A lines byte-identical to the baseline |

---

## 1. Audit of P3A

Before any P3B code, every P3A exit criterion was re-run on a clean CI-equivalent build of the
committed tree (P3A was committed by the user as 7b4f98f/70b1af7 and released as 1.0-dev.14).
Details in [P3B_PLAN.md](../P3B_PLAN.md) §1; summary:

| P3A criterion | Re-verified |
|---|---|
| Full state byte-identical, corpus × 416 configurations | ✅ 2 912 digests, registry declaration and default dumps identical |
| Byte-identical on ≥ 1 000 generated scenarios | ✅ 1 000 scenarios (525 982 frames) identical, generation deterministic, coverage exactly as reported |
| Golden outcome reproduced | ✅ `replay-check --strict --engine` |
| Declaration, determinism, robustness, gates | ✅ `cargo test` (29 tests), fmt, clippy `-D warnings`; no `unsafe`, no dependencies |
| No C++ behavior change | ✅ `src/detection` untouched since P1/P2; suites 66 / 550 green; `VigilParity` deterministic |
| Falsification | ✅ F12 and F15 re-run: 2 889 / 663 and 0 / 75 differing runs — exactly the reported figures |

Findings of the code audit (all recorded, none blocking): the Rust `set_config` checks only the
number of values where C++ also checks the schema (no caller can tell the difference today; P4 must
pass documents); the P3A marks started at revision 0 instead of 1 (fixed by the full port); the
history fields `prior*` were never exercised because no history was loaded and the parity dump does
not print them (now compared, §4); the C++ harness rewrote its marks work file on binding while the
Rust engine did not (now compared); 129 files are stored with CRLF in the index since the P3A commit
(harmless; recommendation in §9).

## 2. What was built

| Area | Contents |
|---|---|
| `vigil-detection` library (+≈ 1 500 lines) | `marks`: the full state machine (watch, confirm, unconfirm, exempt, notes with the 500-byte cut, reasons, remove, remember name, add by name, bind with merge, find by name, `all()`, search, counts, revision, read-only and load note) with its file (atomic write through `vigil_formats::marks`). `snapshot`: the check catalog, profile capture, the profile store (save, load, list, note, export, import, pending imports, delete, unique ids). `session`: history load / apply / merge / save, the journal, the start epoch. `engine`: marks, history and journal paths, the log sink, the 60-second flush, the journal and log lines of `emit()`/`debug()`. `sinks`: the "detections" channel (`writef`'s 2 047-byte cut) and `LogFile` (`core/log`'s format and 4 MiB rotation). `clock`: injected wall clock and local time. `msvcsort`: MSVC's `std::sort` |
| Rust tests (+≈ 3 800 lines, 29 → 101 tests) | `detection.rs` (32), `validation.rs` (11), `records.rs` (10): the C++ suites test for test. `tests/common`: the simulator (`sim.rs`, `scenarios.rs`), the C++ tests' generators (`rng.rs`), the `EXPECT` collector with frame digests, a registry stand-in. `properties.rs` (8 properties). `sinks.rs` (7). `foundations.rs` +2 (sort order, generators) |
| `vigil-parity` | Generated plans: every scenario also runs with its sinks and writes a `sinks` line. Marks plans (`vigil-parity-marks`): replays the scripts |
| `tools/parity/parity.cpp` | `--generate`: a history file and journal/history switches per scenario from a generator of their own (frames and the engine's configurations unchanged), the sinks of every run, the `sinks` line and its full text (`--full … .sinks.txt`). `--marks`: random marks scripts, the C++ state after every action |
| `tools/formats/formats.cpp` | Two generated fixtures: `sort-order.txt`, `random.txt` |
| `tests/test_framework.h`, `tests/sim.h` | `--counts`: per test, the expectation count and the digest of every frame fed to an engine (default output unchanged) |
| `tools/ci/test_parity.py`, `tests/test-parity.json` (new) | The test parity matrix: C++ and Rust suites side by side, same names, same counts, same frames, with the listed exceptions |
| `tools/ci/engine_parity.py` | Sinks lines explained and reported, marks scripts, sink coverage |
| CI / CMake | `engine_parity.py --marks-count 300` and `test_parity.py` in the required build job; CTest `test_parity` |
| Docs | P3B_PLAN, this report; RUST_PLAN (status, §6.2, §7 P3 ✅ and P3B ✅, §8, §10.3); ARCHITECTURE §1, §6; VALIDATION §1, §2, §5, §6, §9, §10 (new); TECH_DEBT; rust/README; tests/fixtures/formats/README; README |

## 3. Design decisions

1. **Mirror the tests, then prove the mirror.** "Same name, passes" is a weak claim on its own: a
   Rust test that drew different random numbers or built its scenario slightly differently could
   pass while testing something else. So the matrix compares, per test, the expectation count (data
   dependent where the C++ test loops over evidence: `envelopes` counts 121 on both sides) and a
   digest of every frame the test fed to an engine. Because P3A proved the engines identical,
   identical frames mean identical engine behavior in every mirrored test.
2. **The simulator port proves itself.** Its golden arena equals the committed C++ trace frame for
   frame; its random number generators reproduce `random.txt`. That fixture showed that MSVC's
   `generate_canonical` takes the top 53 bits of two `mt19937` draws (P0952R2) — the reading "sum in
   `double`, divide by 2⁶⁴" matched only 11 of 32 values.
3. **The registry stays C++.** Three tests reach the engine through the C++ settings registry;
   their Rust twins use a small stand-in (`tests/common/registry.rs`) with the same rules (`Setting::set` snapping, defaults as
   `registerEngineSettings` stores them, sync on change), so the engine-facing claims are checked
   against the Rust engine. Expectations about the registry alone (11) stay with the C++ suite and
   are listed with reasons in `tests/test-parity.json`.
4. **Wall clock and local time are inputs.** Four C++ places read the wall clock, two read local
   time; none influences a decision. The Rust side takes both from the host. The differential
   harness masks the C++ run's wall-clock window and the Rust run's injected window the same way,
   and the marks scripts make every action's time unique (the C++ tool waits for a new millisecond)
   so times compare as "set by action k".
5. **MSVC `std::sort` is reproduced, not avoided** — the same decision P3A made for
   `std::unordered_map`. Marks and profile lists order equal times as MSVC's introsort does above
   32 elements; the fixture includes McIlroy adversarial inputs, and the Rust test requires that the
   heap-sort fallback was reached with ties (it was).
6. **Sinks are added to the generated scenarios without touching them.** History inputs and
   journal/history switches come from a separate generator; the generator, its frames and every
   configuration the engine decides with are unchanged — and the P3A `variant`/`summary` lines of
   the 1 000 scenarios are byte-identical to the baseline's.
7. **A crash is not mirrored.** C++ terminates on an imported profile whose save time `localtime_s`
   cannot represent; the Rust store uses the current time instead (classified in §7, recorded in
   TECH_DEBT with a one-line fix for C++).

## 4. Validation results

| # | Requirement | Result |
|---|---|---|
| V1 | C++ unchanged | C++ changes only in `tests/test_framework.h`, `tests/sim.h` (test headers) and `tools/` · suites 66 tests / 550 expectations, 0 failed (= baseline) · golden ✅ · `format_fixtures` ✅ (14 generated files incl. the two new ones, 56 canonical files) · default `VigilParity` output byte-identical to the baseline (SHA-256 `BD8432B1…`, the same as the P3A audit run) ✅ |
| V2 | P3A parity holds | **Holds.** Corpus: IDENTICAL (2 912 full-state digests, registry declaration, default dumps; 1 978 496 frames) and the C++ corpus output is byte-identical to the baseline's. Generated: IDENTICAL (1 000 scenarios, 525 982 frames, seed 20261127), deterministic, and the 2 000 `variant`/`summary` lines are byte-identical to the baseline's `expected.txt` — adding the sinks changed nothing the engine concludes. Coverage unchanged (24 of 24 indicators). `replay-check --strict --engine` ✅ |
| V3 | Test parity | **54 C++ tests: 53 mirrored, 1 host-only; expectations C++ 438 = Rust 427 + host-only 11; every mirrored test passes and feeds the same frames** (matrix §4.1) |
| V4 | Simulator | golden arena = committed trace, frame for frame; `random.txt`: 15 generator rows, every value identical |
| V5 | Sinks | **Every `sinks` line identical** on the 1 000 generated scenarios: 654 scenarios wrote a journal (15 536 lines), 521 wrote "detections" log lines, 311 had a marks file, 449 had records that received history, every scenario flushed history at least twice, 6 659 profiles captured — each identical and a fixed point of its own round trip |
| V6 | Marks | **IDENTICAL**: 300 scripts, 11 515 actions (seed 20261128), the full state after every action; 100 scripts on read-only files, 66 ending with more than 32 marks (MSVC sort order of ties exercised) |
| V7 | Profiles | the Rust capture of the golden arena's LowHopper = committed `session-hopper.json` byte for byte; every profile captured in the 1 000 generated scenarios identical and a fixed point of its own round trip (V5); `snapshot store` mirrored |
| V8 | Sort | `sort-order.txt`: 218 cases (sizes 0–2 000, ties, adversarial), every order identical; heap fallback reached, also with ties |
| V9 | Properties | 8 properties, all green; coverage: 65 897 detections checked against the gate and cooldown; random sessions: 27 276 frames (queue 9 311, post-game 3 102, players away 25 241, returns in the same game 126, carry-overs to a new game 279, envelopes 2 238); 116 captured profiles read back, 464 hostile variants (453 refused, the rest bounded) |
| V10 | Cross-falsification | The same fault in both engines fails the same mirrored tests with the same number of failed expectations, for all three faults (§4.3) |
| V11 | Robustness | `hostile_sink_inputs_never_panic` (random-byte history and marks files, a journal "directory" that is a file, unwritable paths), 464 mutated real profiles (`profiles_round_trip_and_hostile_files_are_bounded`), read-only marks under random actions: no panic, every bound held |
| V12 | Performance | Tick cost of `onFrame` alone, best of 3 alternating runs × 30 repetitions of the corpus under the defaults, sink code present but unset: golden trace **0.98×** C++ (13.15 vs 13.40 µs/frame), whole corpus **0.98×** (26.73 vs 27.20); per scenario 0.88×–1.04× (only *clickers* 1.04× and *movement* 1.01× above C++). Absolute times were ≈ 2.4× those of the P3A measurement on this machine that day (machine noise, as recorded before); the ratios are what compares. RUST_PLAN §8 (Rust ≤ C++ on the golden trace) holds |
| V13 | Gates | `cargo fmt --check`, `clippy --all-targets -D warnings`, `cargo test --release` (101 tests) green; no `unsafe` (`forbid`), no dependencies; CI-equivalent clean build of the working tree (one configure, `/W4 /WX`, DLL, launcher, preview, `-DVIGIL_BUILD_RUST=ON`): **CTest 10/10** — five suites, `format_fixtures`, `rust_tests`, `format_round_trips`, `engine_parity` (corpus, 1 000 generated scenarios with sinks, 300 marks scripts), `test_parity`; `Vigil.exe --self-check` ok, embedded runtime = built DLL; `validate_repo` metadata, links (33 files), golden, engine-globals clean |
| V14 | Falsification of the new instruments | §5 |

### 4.1 Test parity matrix

Columns: expectations counted by the C++ test, by the Rust test, and left to the C++ suite (host-only, `tests/test-parity.json`); frames: how many frames the test fed to engines, and whether the frame digests are equal. Generated by `tools/ci/test_parity.py --markdown`.

| Suite | Test | Class | C++ | Rust | Host-only | Frames | Status |
|---|---|---|---|---|---|---|---|
| detection | physics baseline | full | 2 | 2 | 0 | 0 (same) | ok |
| detection | json & settings | partial | 10 | 5 | 5 | 0 (same) | ok |
| detection | confidence model | full | 4 | 4 | 0 | 0 (same) | ok |
| detection | presets keep defaults | stand-in | 50 | 50 | 0 | 0 (same) | ok |
| detection | legit movement is quiet | full | 4 | 4 | 0 | 2400 (same) | ok |
| detection | speed potion legit | full | 2 | 2 | 0 | 1200 (same) | ok |
| detection | stale potion uses movement attribute | full | 3 | 3 | 0 | 3600 (same) | ok |
| detection | head-hitter jumping is quiet | full | 3 | 3 | 0 | 2400 (same) | ok |
| detection | cheat bhop / lowhop | full | 2 | 2 | 0 | 800 (same) | ok |
| detection | air strafe | full | 2 | 2 | 0 | 208 (same) | ok |
| detection | old KillAura bug regression | full | 1 | 1 | 0 | 400 (same) | ok |
| detection | legit duel is quiet | full | 3 | 3 | 0 | 330 (same) | ok |
| detection | killaura outside view | full | 2 | 2 | 0 | 126 (same) | ok |
| detection | killaura multi-target | full | 2 | 2 | 0 | 126 (same) | ok |
| detection | reach | full | 2 | 2 | 0 | 150 (same) | ok |
| detection | velocity | full | 2 | 2 | 0 | 126 (same) | ok |
| detection | autoclicker | full | 4 | 4 | 0 | 600 (same) | ok |
| detection | noslow | full | 2 | 2 | 0 | 800 (same) | ok |
| detection | velocity waits for the victim's latency | full | 3 | 3 | 0 | 1170 (same) | ok |
| detection | speed ignores lag catch-ups | full | 3 | 3 | 0 | 1800 (same) | ok |
| detection | speed effect that just ended | full | 2 | 2 | 0 | 1800 (same) | ok |
| detection | wall collision is not air strafe | full | 2 | 2 | 0 | 1200 (same) | ok |
| detection | server stall | full | 2 | 2 | 0 | 1200 (same) | ok |
| detection | noslow ignores lag catch-ups | full | 1 | 1 | 0 | 1200 (same) | ok |
| detection | match tracker phases | full | 11 | 11 | 0 | 0 (same) | ok |
| detection | post-game is not judged | full | 3 | 3 | 0 | 800 (same) | ok |
| detection | confidence holds while away | full | 2 | 2 | 0 | 2761 (same) | ok |
| detection | carry-over to the next game | full | 3 | 3 | 0 | 301 (same) | ok |
| detection | queue records without judging | full | 2 | 2 | 0 | 400 (same) | ok |
| detection | NPC and bot classification | full | 9 | 9 | 0 | 120 (same) | ok |
| detection | duplicates and name collisions | full | 2 | 2 | 0 | 200 (same) | ok |
| detection | marks | full | 7 | 7 | 0 | 0 (same) | ok |
| validation | confidence invariants | full | 9 | 9 | 0 | 0 (same) | ok |
| validation | match lifecycle | full | 13 | 13 | 0 | 963 (same) | ok |
| validation | player state table | full | 14 | 14 | 0 | 300 (same) | ok |
| validation | envelopes | full | 121 | 121 | 0 | 600 (same) | ok |
| validation | determinism | full | 3 | 3 | 0 | 600 (same) | ok |
| validation | trace round trip | full | 5 | 5 | 0 | 600 (same) | ok |
| validation | golden trace | full | 4 | 4 | 0 | 600 (same) | ok |
| validation | config schema | stand-in | 17 | 17 | 0 | 0 (same) | ok |
| validation | config sync between frames | stand-in | 11 | 11 | 0 | 0 (same) | ok |
| validation | config engines share nothing | partial | 5 | 4 | 1 | 600 (same) | ok |
| validation | config documents | full | 6 | 6 | 0 | 0 (same) | ok |
| records | mark semantics | full | 8 | 8 | 0 | 0 (same) | ok |
| records | search marks | full | 6 | 6 | 0 | 0 (same) | ok |
| records | add by name and binding | full | 8 | 8 | 0 | 0 (same) | ok |
| records | engine: exempt and binding | full | 6 | 6 | 0 | 1000 (same) | ok |
| records | marks format guards | full | 5 | 5 | 0 | 0 (same) | ok |
| records | settings keep unknown keys | host | 5 | - | 5 | - | host only |
| records | json number round trip | full | 8 | 8 | 0 | 0 (same) | ok |
| records | snapshot round trip | full | 10 | 10 | 0 | 600 (same) | ok |
| records | snapshot check remapping | full | 4 | 4 | 0 | 600 (same) | ok |
| records | snapshot rejects bad files | full | 6 | 6 | 0 | 0 (same) | ok |
| records | snapshot store | full | 12 | 12 | 0 | 600 (same) | ok |

**Totals:** 54 C++ tests, 53 mirrored, 1 host-only; expectations C++ 438 = Rust 427 + host-only 11; 37 tests feed frames (33 281 frames in all), every digest equal.

### 4.2 What the generated sinks exercised

| Sink | Scenarios (of 1 000) | Volume |
|---|---|---|
| Session journal | 654 | 15 536 lines |
| History file | 1 000 (at reset, every 60 s while recording, at the end) | inputs: none, unreadable, wrong shape, or hostile entries (huge, negative, fractional, text, missing) |
| Records that received history (`prior*`) | 449 | — |
| "detections" log | 521 | debug and detection lines |
| Marks file (rewritten on binding) | 311 | — |
| Captured profiles | all | 6 659 profiles, 0 round-trip failures |

### 4.3 Cross-falsification

Three faults applied to **both** engines; the mirrored suites fail identically:

| Fault | C++ | Rust |
|---|---|---|
| X1 — exempt players judged (the exemption rule in `shouldAnalyze` removed) | `engine: exempt and binding` 2, `player state table` 2 | the same two tests, 2 and 2 |
| X2 — confidence never snaps to 0 below 0.05 | `confidence invariants` 1 | `confidence invariants` 1 |
| X3 — carry-over to a new game at 0.75 instead of 0.5 | `carry-over to the next game` 1, `match lifecycle` 1 | the same two tests, 1 and 1 |

X1 also reproduces what VALIDATION §3 records for the C++ suites ("fails 2 expectations in
`records` and 2 in `validation`").

## 5. Falsification

Each fault applied alone to a sandbox copy, rebuilt, the instrument run, the file restored.

**Properties (V9).** Every invariant of VALIDATION §5 was broken in the Rust engine; the column
"suites" lists what the mirrored C++ tests catch of the same fault — the property tests catch every
one, including two (P6, P10) no suite test notices.

| # | Fault | Property that fails | Mirrored suites that fail (failed expectations) |
|---|---|---|---|
| P1 | Severity no longer clamped to 0..1 | bounds; evidence never lowers confidence | — |
| P2 | Negative weights allowed | bounds; evidence never lowers confidence | — |
| P3 | Decay uses 0.9 of the elapsed time | halves per half-life; exactly zero after quiet | `confidence invariants` (1), `golden trace` (1), `confidence model` (1) |
| P4 | Decay never snaps to exactly 0 | halves per half-life; exactly zero after quiet | `confidence invariants` (1) |
| P5 | Evidence gate one observation short | evidence gate and cooldown | `confidence invariants` (1) |
| P6 | Carry-over keeps the evidence gate filled | evidence gate (carried confidence alone) | **none** |
| P7 | No cooldown between detections | evidence gate and cooldown | `confidence invariants` (1), `golden trace` (1) |
| P8 | Queue judged regardless of the setting | lifecycle and roles | `match lifecycle` (1), `queue records without judging` (1) |
| P9 | Post-game judged | lifecycle and roles | `golden trace` (1), `match lifecycle` (1), `post-game is not judged` (1) |
| P10 | Re-entering view charges the absence | lifecycle and roles (return in the same game) | **none** |
| P11 | A new game carries confidence in full | lifecycle and roles (halving) | `match lifecycle` (1), `carry-over to the next game` (1) |
| P12 | Teammates judged | lifecycle and roles | `player state table` (2) |
| P13 | Envelope side inverted when an expectation is stated | lifecycle and roles (envelopes) | `envelopes` (117) |
| P14 | A read-only marks store saves anyway | read-only marks never overwritten | `marks format guards` (1) |
| P15 | Unknown checks of a profile not listed | profiles round trip, dropped checks | `snapshot check remapping` (1) |

P10 was first *not* caught: the property checked only frames in which a player stays away. The
property now also checks the return within the same game, and catches it.

**Cross-falsification (V10):** X1–X3 in §4.3.

**Sinks, marks, profiles and sort order (V5–V8).** Counts are differing `sinks` lines (of 1 000
generated scenarios) and differing marks scripts (of 300); the P3A `variant`/`summary` lines never
moved (0 in every row), as expected for faults outside the engine's decisions.

| # | Fault | Generated `sinks` lines | Marks scripts | Rust tests that fail |
|---|---|---|---|---|
| F21 | Journal evidence line: confidence before severity | 654 | 0 | `journal_lines_and_file_name` |
| F22 | History merge takes the session peak instead of max(prior, session) | 87 | 0 | `history_load_apply_and_merge` |
| F23 | Log messages cut at 4 095 instead of 2 047 bytes | 0 | 0 | `detections_log_channel` |
| F24 | Exempting keeps the confirmation reasons | 0 | 98 | — |
| F25 | `Marks::all()` with a stable sort instead of MSVC `std::sort` | 0 | 66 | — |
| F25b | MSVC sort with a 16-element insertion-sort cut-off | 0 | 74 | `std_sort_order_matches_msvc` |
| F26 | Profile capture drops the exemption | 227 | 0 | — |
| F27 | History never applied to new records | 449 | 0 | `history_load_apply_and_merge` |
| F28 | Journal written although `data.journal` is off | 180 | 0 | `journal_lines_and_file_name` |
| F29 | History flushed although `data.history` is off | 168 | 0 | — |
| F30 | Binding a name-added mark forgets its note on merge | 0 | 6 | — |

Every fault is caught. Five (F24, F25, F26, F29, F30) are caught **only** by the new differential
instruments — no test, C++ or Rust, notices them; F23 only by a unit test, because no engine
message reaches 2 047 bytes (names are at most 16 bytes, evidence texts at most 511). F25 shows what
the MSVC sort mirror is for: a stable sort changes the order of 66 scripts' mark lists.

## 6. Findings

* **A crash in C++, found by porting it.** Mirroring `stampOf` meant asking what `localtime_s` does
  outside its range. A probe showed it accepts only `0 ≤ t ≤ 32 536 799 999` and otherwise calls
  the CRT's invalid-parameter handler, which fail-fasts (`0xC0000409`). `SnapshotStore` reaches it
  with the save time of an *imported* file: `"savedAt": 1e300` (or anything from about year 3001)
  terminates Vigil — inside the game, Lunar. Not changed here (no C++ behavior change in a port);
  recorded in TECH_DEBT with a one-line fix and the Rust port's safe behavior (§7).
* **MSVC's `generate_canonical` is not what the standard's older wording suggests.** The test
  suites' random numbers come from `uniform_real_distribution<double>`; summing two draws in
  `double` (the C++11 wording) reproduced 11 of 32 values, the top 53 bits of `g0 + g1·2³²` (the
  P0952R2 algorithm current MSVC ships) all of them. Without the fixture, mirrored tests would have
  passed on different scenarios — which the frame digests (§3.1) would also have exposed.
* **Property tests catch what the suites do not.** Two realistic faults pass every C++-derived
  test: carried confidence keeping the evidence gate filled (P6: the suites' carry-over case falls
  below the alert threshold anyway) and charging a player's absence when they return (P10). Both
  fail a property test. P10 was first missed by the property too: it only checked frames in which a
  player stays away; falsification showed the gap and the property now also checks the return.
* **The MSVC sort mirror and the sinks matched on the first comparison.** No Rust-side difference
  had to be fixed in P3B's differential instruments; every fault in §5 was nevertheless caught,
  which is what shows the instruments look where differences would be.
* **Tooling lessons** (for future passes): `VigilFormats generate` rewrites every fixture with LF
  line endings while the index holds CRLF copies of most of them (P3A commit) — restore unchanged
  files instead of committing churn; restoring a file with its old timestamp (robocopy) makes cargo
  keep a stale build — one falsification run was contaminated this way, detected and re-run.
* **Pre-existing and left as is:** `Marks::save` does not check that its temporary file was fully
  written; the Rust `set_config` checks only the number of values (P3A audit A1). Both in TECH_DEBT.

## 7. Differences and their classification (RUST_PLAN §6.3)

| Difference | Classification | Notes |
|---|---|---|
| An imported profile with a save time `localtime_s` cannot represent: C++ terminates the process, Rust names the profile after the current time | **Expected implementation difference** (C++ behavior is a crash; no non-crashing behavior can equal it) | Pre-existing C++ defect, recorded in TECH_DEBT with the matching one-line fix |
| Local time defaults to UTC in Rust (profile ids, log timestamps) | **Expected implementation difference**, until P4 supplies the host's conversion | No decision, file content or comparison depends on it; ids and timestamps are masked or format-checked |
| Expectations about the C++ settings registry (11) have no Rust counterpart | Not a difference: host code, not ported in P3 | `tests/test-parity.json` |

No behavioral difference was found between the engines, their sinks or their tests.

## 8. Exit criteria

| Criterion (P3B_PLAN §6) | Evidence | Status |
|---|---|---|
| Every C++ engine test has a same-named Rust counterpart that passes | V3, V4, §4.1 | ✅ |
| Property tests for every VALIDATION §5 invariant, each falsified once | V9, §5 | ✅ |
| Persistence sinks write the C++ bytes | V5, V6, V7 | ✅ |
| P3A parity still holds | V2 | ✅ |
| No C++ behavior change; gates green; falsification recorded; docs updated | V1, V13, §5, §2 | ✅ |

## 9. State of the migration and next steps

| Phase | State |
|---|---|
| P0, P1, P2 | ✅ |
| P3A Engine port, differential proof | ✅ |
| **P3B** Test parity, rest of `detection/` | ✅ this pass |
| **P3** (RUST_PLAN) | ✅ — P3A and P3B both met |
| P4 Engine in the DLL (shadow) | Not started (by instruction) |

Nothing in P3B runs in game, so the live checklist ([LIVE_VALIDATION.md](LIVE_VALIDATION.md)) gains
no item; its first Rust item belongs to P4.

**Recommendations**

1. Release this pass as a dev build when convenient — no user-visible change; `version.yaml` and
   the release notes were left untouched (releases are yours).
2. Fix the C++ profile-import crash in a separate, small change (TECH_DEBT: treat an
   unrepresentable save time like a missing one), with a records test — it is a robustness bug
   reachable with a file from another person.
3. Commit `.gitattributes` with `* text=auto` and `tests/fixtures/** -text`, then renormalize in a
   commit of its own (the index holds 129 CRLF files today).
4. For P4: pass configuration documents (not value arrays) across the C ABI; pass the host's
   local-time conversion and wall clock; keep `engine_parity.py` and `test_parity.py` required
   until the C++ engine is removed (P5); every `extern` entry point under `catch_unwind`.

**Reproduce** (after a C++ build with the tools and `cargo build --release` in `rust/`):

```powershell
python tools\ci\engine_parity.py --cpp build\Release\VigilParity.exe --rust rust\target\release\vigil-parity.exe --work parity-work --count 1000 --marks-count 300
python tools\ci\test_parity.py --cpp-dir build\Release --rust-dir rust --markdown matrix.md
cd rust; cargo test --release --test properties -- --nocapture
```
