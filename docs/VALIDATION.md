# Validation

For developers: how Vigil is tested, what each layer proves, how to add tests, and how CI uses
them. Live (in-game) validation is in [LIVE_VALIDATION.md](LIVE_VALIDATION.md); the release
pipeline in [RELEASE_PROCESS.md](RELEASE_PROCESS.md).

## 1. Layers

| Layer | Proves | Where | Runs |
|---|---|---|---|
| Unit & scenario suites | Detection, utilities, records, lifecycle and UI logic behave as specified | `tests/*.cpp` (5 executables) | Every build (CTest), CI |
| Golden trace | The engine still reaches the committed outcome on a recorded session | `tests/traces/` + `VigilReplay` | CI, `validation_tests` |
| Determinism | Same frames → same outcome (one engine or two; through the trace format; the whole corpus under 416 configurations, run twice) | `validation_tests`, `VigilParity` | CI |
| Full-state parity | Two builds of the engine produce identical state — every evidence value, contribution, timeline entry, alert — on a scenario corpus covering all 24 indicators, under every parameter | `VigilParity` (tools/parity) | Every engine refactor or port, by hand against a baseline build (§4.1) |
| Engine owns no globals | Nothing in `src/detection` reads the settings registry or keeps mutable static state | `validate_repo.py engine-globals` | CI |
| Format fixtures | The committed format fixtures are exactly what the C++ code writes, and every canonical form is a fixed point | `VigilFormats check` (CTest `format_fixtures`) | Every build, CI |
| Rust formats | The Rust readers/writers produce the C++ bytes for every fixture, number and refusal; the engine's math functions match the CRT | `cargo test` (rust/), `cross_formats.py` | CI (§8) |
| Rust engine parity | The Rust engine's entire state equals the C++ engine's on the corpus under 416 configurations and on ≥ 1 000 generated scenarios, and so do its persistence sinks (journal, history, log channel, marks file, profiles) and the marks state machine; it reproduces the golden outcome byte for byte | `engine_parity.py`, `replay-check --engine`, `cargo test` | CI (§9) |
| Rust test parity | Every C++ engine test has a Rust counterpart of the same name that passes, counts the same expectations and feeds the engine the same frames; property tests of every §5 invariant | `test_parity.py`, `cargo test` | CI (§10) |
| Self-check | `Vigil.exe` embeds the runtime of the same version and build | `Vigil.exe --self-check` | CI (build + publish) |
| UI smoke | Every page renders | `VigilPreview --shot` | CI (informational) |
| Preview review | Layout, language, motion look right | `VigilPreview` screenshots | Each UI change, by hand |
| Live validation | JNI paths, hooks and timing in Lunar Client | [LIVE_VALIDATION.md](LIVE_VALIDATION.md) | Each release, by hand |

## 2. Suites (current)

| Suite | Tests | Expectations | Covers |
|---|---|---|---|
| `detection` | 32 | 152 | Every check against vanilla physics: legitimate play stays quiet, cheating traces are detected; latency, lag catch-ups, stalls, potions, ceilings, walls; NPCs and duplicates; match tracker |
| `features` | 7 | 52 | Auto Who (incl. "disabled means disabled"), party detectors, Fast Buy, identity helpers, denicker service |
| `records` | 11 | 78 | Marks (confirm/watch/exempt rules, notes, reasons, search), add-by-name binding, engine exemption, marks/settings format guards, JSON round trip, saved profiles (round trip, check remapping, hostile files, store) |
| `validation` | 11 | 208 | Confidence invariants, match lifecycle, player-state table, envelopes, determinism, trace round trip, golden trace; engine configuration (declaration = committed fixture, registry changes only between frames, engines share nothing, exact config documents, hostile documents refused) |
| `ui_logic` | 5 | 60 | Typed slider values, focus order, uncertain-finding wording, interface sounds, higher contrast |

Run them:

```powershell
ctest --test-dir build -C Release --output-on-failure
build\Release\validation_tests.exe            # one suite
build\Release\validation_tests.exe golden     # tests whose name contains "golden"
build\Release\validation_tests.exe --list
build\Release\validation_tests.exe --counts   # also expectations and frame digest per test (§10)
```

The engine-level suites (`detection`, `validation`, `records`) have Rust twins of the same names in
`rust/crates/vigil-detection/tests` (§10): adding or changing a test there means doing the same on
the other side in the same pull request.

All suites share [`tests/test_framework.h`](../tests/test_framework.h) (`EXPECT`, filters,
summary) and engine-level suites share the simulator in [`tests/sim.h`](../tests/sim.h): a vanilla
1.8.9 movement model plus a `World` that feeds the real `Engine` exactly as the game adapter does
(server relay delay, cadence and freezes included).

## 3. Writing a test

1. Pick the suite by subject (above). Engine behavior → build a `World`, add `Sim`s, step them,
   call `w.frame()`; assert on `w.rec(sim)`, `w.detections(sim, "check")`, `w.engine.alerts()`.
2. Assert **both sides**: the legitimate variant stays quiet *and* the cheating variant is caught.
   A false-positive fix without a cheat-still-detected assertion is not accepted.
3. State the behavior in the test name; explain the scenario in a comment.
4. **Falsify it once**: revert the fix (or remove the rule), rebuild, see the test fail, restore.
   Record the falsification in the change's report. Examples from this pass: removing the
   exemption rule in `Engine::shouldAnalyze` fails 2 expectations in `records` and 2 in
   `validation`.

## 4. Traces and the golden test

A trace (`*.vtrace`, [`src/detection/trace.h`](../src/detection/trace.h)) is the exact sequence of
`WorldFrame`s the engine received, as JSON lines. Numbers round-trip exactly, so replaying a trace
reproduces the session bit-for-bit.

```powershell
VigilReplay trace.vtrace                              # what the engine concludes
VigilReplay trace.vtrace --expect expected.json       # compare (exit 1 on a difference)
VigilReplay trace.vtrace --write-expect expected.json # record the current outcome
VigilReplay --synthesize tests\traces\golden-arena.vtrace   # regenerate the golden scenario
```

`tests/traces/golden-arena.vtrace` is generated from `goldenArena()` in
[`tests/scenarios.h`](../tests/scenarios.h): 30 s of a match with a legitimate sprinter, a
low-hopping cheater, a player who leaves view and returns, and a game-end announcement. Its
committed outcome is `golden-arena.expect.json`.

**When the golden test fails:** decide whether the change in outcome is intended. If it is,
regenerate the expectation (`--write-expect`), review the JSON diff in the pull request, and say why
it changed. If the simulator changed, regenerate the trace too. The files are marked `-text` in
`.gitattributes` so line endings never change.

**Real sessions as regression tests:** a trace recorded in game (Status → Diagnostics → Record a
trace) can be added the same way: commit `name.vtrace` + `name.expect.json` and list it in
`validation_tests`. Traces contain player names; only commit traces from sessions where that is
acceptable, or synthetic ones.

**Other settings than the defaults:** a trace replays under any engine configuration document
(`VigilReplay trace.vtrace --config cfg.json`). `VigilReplay --engine-config out.json` writes the
defaults to start from; `--engine-schema out.json` writes the declaration of every parameter. The
committed `tests/fixtures/engine-schema.json` and `engine-config.default.json` are checked by
`validation_tests`: adding or changing a parameter means regenerating them and reviewing the diff.

### 4.1 Full-state parity (`VigilParity`)

`VigilParity out.txt` replays the scenario corpus ([`tests/corpus.h`](../tests/corpus.h): the
golden arena plus six scenarios that make all 11 checks and all 24 indicators fire, through every
game phase, teammates, the local player, ice, knockback, lag and teleports) through fresh engines
under 416 configurations: defaults, Relaxed and Strict presets, **each of the 248 parameters moved
up and down on its own**, and settings changed mid-session the way the UI changes them. For each it
writes a digest of the engine's entire state, and it dumps the settings registry the engine
declares (order, labels, ranges, warning predicates). Two builds agree when the files are
byte-identical. Use it for every behavior-preserving engine change:

```powershell
# before the change: build the untouched tree (e.g. git archive HEAD into a scratch folder)
base\VigilParity.exe before.txt
# after the change
build\Release\VigilParity.exe after.txt
fc /b before.txt after.txt
```

It catches what the suites cannot: in P1, hard-coding one parameter to its default (`airTolerance`)
passed every suite and the golden trace, and `VigilParity` pinpointed exactly the two
`airTolerance` configurations. CI runs it twice per build to prove determinism.

## 5. Invariants the suites enforce

* Confidence stays within 0–100, never drops when evidence arrives, halves per half-life, reaches
  exactly 0 after long quiet, and needs the evidence gate (≥ N observations in the window) to
  detect. Carried confidence (a new game) cannot complete a detection by itself.
* Queue players are listed, never judged; post-game is never judged; confidence holds while a
  player is away and is halved (per check) in a new game.
* Teammates, the local player and exempt players are listed but not judged; NPCs are never listed;
  only judged players can produce alerts.
* Every observation carries an envelope, sits at or beyond its limit (never inside the legitimate
  envelope), and its vanilla expectation lies on the legitimate side.
* A marks file in a newer or unreadable format is never overwritten; settings from another version
  are kept.
* Saved profiles survive a round trip; unknown checks are dropped and listed; hostile files are
  bounded and refused cleanly.

Since P3B each invariant is also **property-tested** on the Rust side
(`rust/crates/vigil-detection/tests/properties.rs`): thousands of seeded random operation sequences
on the confidence model (any parameters, weights, severities, gaps, carry-overs), random simulated
sessions through the engine (random roles, phases, world changes, players leaving and returning),
random marks actions on read-only files and random mutations of real profiles. Each property was
falsified once (docs/RUST_P3B_REPORT.md §5).

## 6. CI

`.github/workflows/validate.yml` runs on every pull request and push, and is called by
`release.yml` before anything is published:

| Job | Required | What |
|---|---|---|
| Workflow lint | ✅ | `actionlint` (with shellcheck) on every workflow |
| Version, release notes, docs, golden trace, engine globals | ✅ | `tools/ci/validate_repo.py metadata · links · golden · engine-globals` |
| Build, test and self-check | ✅ | MSVC `/W4 /WX`; all suites and the format fixtures; golden replay; corpus determinism (`VigilParity` twice); Rust `fmt --check`, `clippy -D warnings`, `cargo test`; C++ ↔ Rust round trips; `replay-check --strict --engine` on the golden pair; C++ ↔ Rust engine parity with sinks and marks scripts (§9); test parity (§10); `--self-check`; embedded runtime hash = built DLL hash; build ID = commit |
| UI smoke renders | informational | Every page rendered on the runner; screenshots uploaded |
| Java shims reproducible | informational | javac output differs across JDK patches |
| Validation complete | ✅ | One status for branch protection |

The UI smoke job is informational because hosted runners provide only a software OpenGL 1.1
driver. Promote it to required once it has passed reliably for several runs (move it into the
`complete` job's `needs`).

## 7. Local equivalent of CI

```powershell
cmake -S . -B build-ci -A x64 -DVIGIL_WARNINGS_AS_ERRORS=ON
cmake --build build-ci --config Release
ctest --test-dir build-ci -C Release --output-on-failure
build-ci\Release\VigilReplay.exe tests\traces\golden-arena.vtrace --expect tests\traces\golden-arena.expect.json
Start-Process build-ci\Release\Vigil.exe -ArgumentList '--self-check','selfcheck.json' -Wait
python tools\ci\validate_repo.py metadata; python tools\ci\validate_repo.py links; python tools\ci\validate_repo.py engine-globals
```

`--self-check` never extracts the runtime, opens a window or touches the game; it writes the
report and a temporary copy of the runtime in `%TEMP%` (deleted immediately).

## 8. Rust formats (RUST_PLAN P2)

`rust/crates/vigil-formats` reads and writes every Vigil file format — traces, outcomes, saved
profiles, marks, engine schema and configuration — **byte for byte like the C++ code**, with no
third-party crates and no `unsafe`. Strings are bytes (real traces can hold JNI modified UTF-8),
numbers are written with a mirror of the C++ `%.0f` / `%.15g` / `%.17g` rule, and every reader
normalizes exactly like its C++ counterpart (defaults, clamps, bounds, unknown ids dropped).

**The contract.** For any input, the *canonical form* (parse + write) is the same bytes in both
languages, or both refuse it with the same reason. [`tests/fixtures/formats`](../tests/fixtures/formats/README.md)
holds the inputs (hand-written edge cases and C++-generated files), the C++ canonical forms, a table
of 4 000+ doubles and a table of the engine's math functions on its input ranges.

| Check | Where | Proves |
|---|---|---|
| `format_fixtures` (CTest) | `VigilFormats check` | The committed fixtures are what this C++ build writes; C++ canonical forms are fixed points |
| `cargo test` | `rust/crates/vigil-formats/tests` | Rust canonical form = committed C++ form for every input (identical refusal messages); numbers and math bit-exact; 3 000+ mutated inputs never panic; `replay-check` behavior |
| `cross_formats.py` | CI, CTest with `-DVIGIL_BUILD_RUST=ON` | Live round trips in both directions (C++ → Rust → C++, Rust → C++ → Rust) over every fixture, the golden pair and the 7 corpus traces generated fresh |
| `replay-check --strict` | CI | Rust reads the golden trace and outcome, they agree, and both are canonical |

**Deliberate differences** are listed in `tests/fixtures/formats/divergences.txt`, each with its
classification; the tests check that each still diverges exactly as stated and that nothing else
does. Today there is one: hexadecimal float literals, which C's `strtod` reads and Rust refuses.

**Changing a format or adding a fixture:** change both sides in the same pull request, then
regenerate and review the diff:

```powershell
build\Release\VigilFormats.exe generate tests\fixtures\formats    # generated inputs and tables
build\Release\VigilFormats.exe canon-all tests\fixtures\formats   # C++ canonical forms
cd rust; cargo test --release
```

A format change bumps its format number (RUST_PLAN §11.5); readers keep accepting older formats.

## 9. Rust engine parity (RUST_PLAN P3A, P3B)

`rust/crates/vigil-detection` is the engine ported expression for expression. It is proven by one
claim, checked on every build: **for the same frames, marks and configuration snapshots, its
entire state is byte-identical to the C++ engine's**, in the `VigilParity` format (every evidence
value and text, contribution, envelope, detection, timeline entry, confidence history, statistic,
alert, debug line, combat event, filtered entity and player state) — **and so are the files it
writes** (P3B): the session journal, the cross-session history, the "detections" log, the marks
file and saved profiles, and the marks state after every user action.

```
VigilParity out.txt --export plan/      C++: as §4.1, plus the traces and, per run, every
                                        configuration snapshot that reached the engine and when
vigil-parity plan/ rust.txt             Rust: replays the plan, writes the same format
VigilParity --generate gen/ --count N   C++: N generated scenarios (tests/generator.h) + results
vigil-parity gen/ rust-gen.txt          Rust: the same N
```

`tools/ci/engine_parity.py` runs all four and compares (CI, and CTest `engine_parity` with
`-DVIGIL_BUILD_RUST=ON`). On a difference it names the first differing run and writes that run's
full state from both engines, so the first differing line is visible.

| Instrument | Scope | Compared |
|---|---|---|
| Corpus × configurations | 7 scenarios × 416 configurations (defaults, presets, every parameter up and down, mid-session changes) = 2 912 runs | The whole `VigilParity` output: registry declaration (incl. every warning predicate probed across its range), corpus shapes, every digest, full default dumps |
| Generated scenarios | ≥ 1 000 scenarios from `tests/generator.h` (random behaviors, identities, phases, lag, marks, configurations changed mid-session; ≈ 0.5 M frames per 1 000) | One full-state digest and one summary line per scenario; the summary lines also report what was exercised (all 24 indicators fire) |
| Generated sinks (P3B) | The same scenarios, each with a history file (absent, unreadable, wrong shape, or hostile entries for its players), a journal directory, a log directory and its marks file; `data.journal` / `data.history` switched mid-session in some | One `sinks` line per scenario: the journal (all files, in name order), the history file after every flush, the "detections" log without timestamps, the final marks file, the profiles captured from the records (and their own round trip), the records' history fields |
| Marks scripts (P3B) | `VigilParity --marks`: 300 random action scripts over every marks operation, from no file, format 1 and 2 files (ties, > 32 marks), newer and unreadable files | The whole state after every action: every mark in `all()` order, counts, revision, read-only state, searches, find-by-name, the file |
| Golden trace | `replay-check --strict --engine` | Rust outcome file = committed `golden-arena.expect.json`, byte for byte |
| Declaration | `cargo test` (`tests/declaration.rs`) | Rust schema = `engine-schema.json`, defaults = `engine-config.default.json`, catalog, check metadata and presets = `checks.json` |
| C++ semantics | `cargo test` (`tests/foundations.rs`), fixtures from `VigilFormats generate` | `std::unordered_map` iteration order (`hash-order.txt`), `snprintf` (`printf.txt`), `powf` and `pow(double, int)` (`math.txt`), `std::sort` on ties incl. the heap fallback (`sort-order.txt`), the test suites' random number generators (`random.txt`) |
| Committed C++ files (P3B) | `cargo test` (`tests/sinks.rs`) | The Rust capture of the golden arena's low hopper = `inputs/profile/session-hopper.json` byte for byte; the Rust simulator's golden arena = the committed trace, frame for frame |
| Engine properties | `cargo test` (`tests/engine.rs`) | Determinism, two engines share nothing, hostile frames and configurations never panic |

**What "the same" means.** RUST_PLAN §8 would accept minor floating-point differences that do not
change user-facing behavior; the port does not need that allowance — every result is bit-exact —
so any difference is a bug until classified otherwise (RUST_PLAN §6.3).

**When the parity check fails:** a difference in a run is either a port bug (fix the Rust side), an
intended C++ change that was not ported (port it in the same pull request), or a change to the
tooling. Never regenerate an expectation to make it pass: there is none — both sides are computed
fresh every time.

**Wall-clock values** (a history flush's `lastSeen`, a binding's `updated`, the journal's file
name, a profile's save time, log timestamps) are the only things not compared byte for byte: the C++
tool records its run's wall-clock window and the Rust tool injects a clock (2100-01-01, one
millisecond per read); both write `NOW` (or, in marks scripts, the index of the action that set the
time) for values inside their window, compare journal files in name order without their names and
log lines without their timestamp prefix (whose shape is checked). None of these values
influences a decision (ARCHITECTURE §6).

## 10. Rust test parity (RUST_PLAN P3B)

Every C++ engine test (`detection`, `validation`, `records`: 54 tests) has a Rust counterpart in
`rust/crates/vigil-detection/tests/<suite>.rs` that carries the C++ name (`Expect::new("<name>")`)
and counts its expectations like `EXPECT`. `tools/ci/test_parity.py` (CI, and CTest `test_parity`
with `-DVIGIL_BUILD_RUST=ON`) runs both sides and requires, for every C++ test:

* a Rust test of the same name that passes;
* the same number of expectations — counts can depend on behavior (a loop over evidence), so equal
  counts are themselves evidence of equal behavior;
* the same frames: `--counts` prints a digest of every frame the test fed to an engine through
  `tests/sim.h` (`World::frame`), and the Rust simulator (`tests/common/sim.rs`) prints the same
  digest. With the engines proven identical (§9), identical frames mean the Rust test exercises
  exactly what its C++ original does.

```powershell
python tools\ci\test_parity.py --cpp-dir build\Release --rust-dir rust --markdown matrix.md
```

Exceptions are listed, with their reason, in [`tests/test-parity.json`](../tests/test-parity.json):
tests that touch the C++ settings registry (host code) either use a stand-in of the registry
(`tests/common/registry.rs`), or leave those expectations to the C++ suite (`host`), and one test
(`settings keep unknown keys`) tests the registry alone. The simulator port is itself proven: its
golden arena equals the committed trace frame for frame, and its random number generators
reproduce `random.txt`.

**Adding a C++ engine test** means adding its Rust twin in the same pull request (the matrix fails
otherwise); a test that changes its expectations or frames on one side only fails it too.
