# Validation

For developers: how Vigil is tested, what each layer proves, how to add tests, and how CI uses
them. Live (in-game) validation is in [LIVE_VALIDATION.md](LIVE_VALIDATION.md); the release
pipeline in [RELEASE_PROCESS.md](RELEASE_PROCESS.md).

## 1. Layers

| Layer | Proves | Where | Runs |
|---|---|---|---|
| Unit & scenario suites | Detection, utilities, records, lifecycle and UI logic behave as specified | `tests/*.cpp` (5 executables) | Every build (CTest), CI |
| Golden trace | The engine still reaches the committed outcome on a recorded session | `tests/traces/` + `VigilReplay` | CI, `validation_tests` |
| Determinism | Same frames → same outcome (one engine or two; through the trace format) | `validation_tests` | CI |
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
| `validation` | 7 | 169 | Confidence invariants, match lifecycle, player-state table, envelopes, determinism, trace round trip, golden trace |
| `ui_logic` | 5 | 60 | Typed slider values, focus order, uncertain-finding wording, interface sounds, higher contrast |

Run them:

```powershell
ctest --test-dir build -C Release --output-on-failure
build\Release\validation_tests.exe            # one suite
build\Release\validation_tests.exe golden     # tests whose name contains "golden"
build\Release\validation_tests.exe --list
```

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

## 6. CI

`.github/workflows/validate.yml` runs on every pull request and push, and is called by
`release.yml` before anything is published:

| Job | Required | What |
|---|---|---|
| Workflow lint | ✅ | `actionlint` (with shellcheck) on every workflow |
| Version, release notes, docs, golden trace | ✅ | `tools/ci/validate_repo.py metadata · links · golden` |
| Build, test and self-check | ✅ | MSVC `/W4 /WX`; all suites; golden replay; `--self-check`; embedded runtime hash = built DLL hash; build ID = commit |
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
python tools\ci\validate_repo.py metadata; python tools\ci\validate_repo.py links
```

`--self-check` never extracts the runtime, opens a window or touches the game; it writes the
report and a temporary copy of the runtime in `%TEMP%` (deleted immediately).
