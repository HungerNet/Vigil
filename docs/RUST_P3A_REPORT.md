# Rust migration — P3A implementation report

Phase P3A of [RUST_PLAN.md](../RUST_PLAN.md) §7: the first half of P3, as split in
[P3_PLAN.md](../P3_PLAN.md) §5 after the audit found that P3 is not feasible as one phase. Plan:
[P3A_PLAN.md](../P3A_PLAN.md). P3B and P4 were not started.

| | |
|---|---|
| Status | ✅ exit criteria met |
| User-visible change | None. No shipped binary contains Rust; no C++ engine code changed; no format changed |
| Evidence | The Rust engine's **entire state** is byte-identical to the C++ engine's on the golden trace, on the scenario corpus under all 416 configurations (2 912 runs) and on 1 000 generated scenarios (526 k frames); it reproduces the golden outcome file byte for byte; all 8 deliberate faults are caught; tick cost 0.94× C++ on the golden trace |

---

## 1. Why P3 was split

P3 as written in RUST_PLAN (port the engine, property tests, differential testing, every C++
detection test mirrored) is roughly 10 000 lines of new Rust plus tooling — more than twice P1 and
P2 together — and combines two different kinds of proof: *differential* (C++ is the oracle) and
*test parity* (the Rust suite says what the C++ suite says). Its exit criteria are independent and
its highest risks (container order, printf text, `float` arithmetic) can only be retired by the
differential harness. Hence (P3_PLAN §5):

| | P3A (this pass) | P3B (next) |
|---|---|---|
| Scope | The complete per-frame engine; engine-facing marks; differential harness; ≥ 1 000 generated scenarios; `replay-check --engine`; tick-cost benchmark | Rust simulator and every C++ engine test mirrored by name; property tests for the confidence invariants; full marks state machine, profile capture, history/journal/log sinks |
| Proof | Full state byte-identical to C++ | Test parity matrix, property tests, sink bytes |

RUST_PLAN's P3 is complete when both are; P4 does not start before P3B.

## 2. What was built

| Area | Contents |
|---|---|
| `rust/crates/vigil-detection` (7 850 lines in 29 files as `rustfmt` lays them out — declarations and argument lists one item per line — plus 536 lines of integration tests) | The engine, one module per C++ file: `types`, `evidence` (envelope, confidence model), `config` (the 248-parameter declaration), `check` (common parameters, indicators, presets, the evaluation context), `checks/*` (all 11 checks), `track`, `combat`, `matchtracker`, `roster`, `session`, `marks` (engine-facing: exemption, lookup, name binding), `engine`, `replay`; the C++ semantics they need: `cpp` (STL `max`/`min`/`clamp`, `cvttsd2si` casts), `printf` (UCRT `vsnprintf` subset), `hashorder` (MSVC `std::unordered_map` iteration order); `parity` (the `VigilParity` format) |
| Binaries | `replay-check` (moved from `vigil-formats`; `--engine` replays the trace through the Rust engine, `--strict` then also requires a byte-identical outcome file, `--write` saves it); `vigil-parity` (twin of `VigilParity`: replays a plan, writes the same format; `--bench`) |
| `tools/parity/parity.cpp` | `--export <dir>` (the corpus traces and, per run, every configuration snapshot that reached the engine and the frame before which it arrived); `--generate <dir> --count N --seed S`; `--full <variant> <scenario>`; `--bench`. Default output unchanged |
| `tests/generator.h` (new, 638 lines) | Generated scenarios: 22 behaviors re-drawn every few seconds, identities (invalid names, NPC/nick/missing UUIDs, duplicates, collisions, unlisted entities, id reuse), phases and world changes, lag, stalls, frame drops, tick gaps, surfaces, potions, raw (interpolated) rotations, marks, random configurations changed mid-session; marathon and churn sessions |
| `tools/formats/formats.cpp` | New generated fixtures `hash-order.txt`, `printf.txt`, `checks.json`; `math.txt` gains `powf` and `powi` rows |
| `tools/ci/engine_parity.py` (new) | Runs both engines on the corpus and on N generated scenarios, requires byte-identical results, checks that generation is deterministic, names the first differing run and dumps its full state from both engines, reports coverage |
| Rust tests | `tests/foundations.rs` (container order, printf), `tests/declaration.rs` (schema, defaults, catalog, check metadata), `tests/engine.rs` (determinism, engines share nothing, hostile input), `tests/replay_check.rs` (+2 engine tests); `vigil-formats` math test (+`powf`, `powi`) |
| CI / CMake | `replay-check --strict --engine` and `engine_parity.py --count 1000` in the required build job; CTest `engine_parity` with `-DVIGIL_BUILD_RUST=ON` |
| Docs | P3_PLAN, P3A_PLAN, this report; RUST_PLAN §1 status, §6.2, §7 (split, P3A ✅), §8, §10.2; ARCHITECTURE §1, §6; VALIDATION §1, §6, §9; TECH_DEBT; rust/README; tests/fixtures/formats/README; README |

## 3. Design decisions

1. **Expression for expression.** Each Rust function mirrors one C++ function in the same operation
   order with the same `float`/`double` promotions: confidence, weights, severities and suspicion
   are `f32`; `std::pow(float, float)` is `powf`; yaw differences are subtracted in `float` and then
   promoted, exactly where C++ does. `std::max(a, b)` is `a < b ? b : a`, not Rust's `f64::max`
   (they differ for NaN).
2. **The container order is reproduced, not avoided.** The C++ engine visits entities in
   `std::unordered_map<int, Tracked>` order (FNV-1a buckets, 8 → 64 → 512 → ×2 growth, new keys in
   front of their bucket, rehash reverses each bucket's group). That order decides record order,
   evidence and detection ids, alert order and combat tie-breaks. Changing the C++ container
   instead would have been a behavior change inside a port (RUST_PLAN §11.3). The emulation was
   derived and then confirmed against `std::unordered_map` itself (14 400 operations, int and
   string keys); one first guess about the rehash was wrong and the fixture showed it.
3. **Text is reproduced.** Evidence summaries, details and debug lines go through a mirror of UCRT
   `vsnprintf` (512-byte buffer). The fixture shows that the UCRT rounds exact ties half to even —
   as Rust's own formatter does — and how it prints `-nan(ind)`.
4. **The context queues side effects.** In C++ a check calls `emit()` while holding track
   references and the engine mutates records inside the call. The audit established that checks
   never read anything `emit`/`debug` change, so the Rust context queues them in program order and
   the engine applies them when the check returns: identical, and borrow-checked.
5. **The engine declares itself.** All 248 parameters (keys, defaults with the C++ `float →
   double` promotions, ranges, labels, tooltips, warnings, warning predicates, presets) are
   declared in Rust and checked against the committed C++ fixtures and `VigilParity`'s registry
   probes, rather than read from the fixture.
6. **The harness transports configurations, not registry actions.** `VigilParity --export`
   records each `EngineConfig` snapshot that reached the C++ engine and when; the Rust side applies
   the same snapshots at the same frames. The settings registry stays in C++ (host side).
7. **Integer arithmetic wraps in every profile**, as the C++ engine's does on hostile tick values;
   otherwise debug and release builds would behave differently.
8. **No dependencies, no `unsafe`.** Lookup indexes use a small FxHash-style hasher (std's SipHash
   dominated entity lookups); it is used only for lookups, never for iteration, so it cannot affect
   behavior — and parity was re-run after the change to prove it.

## 4. Validation results

Baseline = `git archive HEAD` of the starting tree, built with the same toolchain.

| # | Requirement | Result |
|---|---|---|
| V1 | C++ unchanged | 5 suites: 66 tests, 550 expectations, 0 failures (= baseline); `format_fixtures` match; golden outcome byte-identical to baseline; default `VigilParity` output (also with `--export`) byte-identical to baseline; `validate_repo` metadata, links, golden, engine-globals clean; `/W4 /WX` |
| V2 | Declaration | Rust schema JSON = `engine-schema.json`; defaults = `engine-config.default.json`; catalog = `check-catalog.json`; names, categories, summaries, "about" texts, requirements, sampling support and presets = `checks.json` — all byte-identical; every warning predicate reproduces the 46 registry probes of each of the 248 parameters (V3) |
| V3 | Corpus parity | Rust `vigil-parity` output = C++ `VigilParity` output, **byte-identical**: 19 126 lines — registry declaration, 7 corpus shapes, **2 912 full-state digests**, full default dumps (1 978 496 frames replayed) |
| V4 | Generated parity | **1 000 generated scenarios** (seed 20261127, ≈ 0.5 M frames): every full-state digest and summary line identical; generation deterministic (two runs identical). Coverage: §4.1 |
| V5 | Golden | `replay-check --strict --engine`: the Rust outcome file is byte-identical to the committed `golden-arena.expect.json` |
| V6 | Container order | `hash-order.txt`: 12 scripts, 14 400 operations, int and string keys, through every growth step — all orders equal |
| V7 | printf | `printf.txt`: 9 000+ rows (`%.0f`…`%.3f`, `%+.2f`, `%.17g`, `%.9g`, `%5.1f`, `%g`, `%f`, `%d`, `%lld`, `%llu`, `%zu`, `%016llx`, `%02x`, `%5d`, `%s`, `%-18s`) over ties, signed zeros, NaN, infinities, subnormals, 1e300, engine ranges — all equal |
| V8 | Math | `powf` (400 rows) and `pow(double, int)` (122 rows) bit-exact, with the P2 functions |
| V9 | Determinism | Same frames → same state; two engines with different configurations interleaved frame by frame equal their solo runs |
| V10 | Robustness | Hostile frames (NaN/∞/1e300 positions and rotations, `INT_MIN`/`INT_MAX` ids, items, hurt times, pings, potions, random-byte names of 600 bytes and UUIDs, `INT64_MIN/MAX` ticks, NaN times, duplicated entities) under configurations outside every range: no panic |
| V11 | Performance | Tick cost of `onFrame` alone, best of 5 alternating runs × 30 repetitions on the corpus under the defaults, same machine: golden trace **0.94×** C++ (5.46 vs 5.84 µs/frame), whole corpus **0.98×** (12.02 vs 12.32 µs/frame); per scenario 0.87×–1.12× — only *clickers* is above C++ (1.12×; *phases-and-roles* 1.01×). Absolute times vary by up to ±40 % between sessions on this machine; the ratios of alternating runs are stable (an earlier session: 0.93× and 0.95×). Before the lookup hasher (§3.8) the Rust engine was ≈ 1.2× C++ |
| V12 | Gates | `cargo fmt --check`, `cargo clippy --all-targets -D warnings`, `cargo test --release` (all suites) green; no `unsafe`, no dependencies |
| V13 | Falsification | §5 |
| V14 | CI-equivalent run | One clean configure of every target (`/W4 /WX`, DLL and launcher, `-DVIGIL_BUILD_RUST=ON`): CTest 9/9 (five suites, `format_fixtures`, `rust_tests`, `format_round_trips`, `engine_parity`); golden outcome and `VigilParity` byte-identical to the baseline and deterministic; `Vigil.exe --self-check` ok, embedded runtime = built DLL; `validate_repo` metadata, links (31 files), golden, engine-globals clean; `cargo fmt --check`, `clippy -D warnings`, `cargo test --release`; `replay-check --strict --engine` |

### 4.1 What the generated set exercises

From the final run (seed 20261127; identical in both engines), the number of the 1 000 scenarios
in which each situation occurs:

| Evidence | Detections | Attributed combat | Server stalls | Filtered entities (NPCs, duplicates) | Exempt players | Name collisions (suffixed) | > 1 scene | Record pruning |
|---|---|---|---|---|---|---|---|---|
| 723 | 420 | 627 | 321 | 247 | 230 | 89 | 465 | 4 |

**All 24 indicators fire**; the rarest are KillAura robotic tracking (38 scenarios), Reach
consistency (7), AutoClicker timing consistency (30) and LegitScaff edge taps (29).
`engine_parity.py` prints these figures on every run.

## 5. Falsification (break the Rust side on purpose, see it caught, restore)

Each fault applied alone, the Rust engine rebuilt, both plans replayed, the file restored (after
the last restore the corpus is identical again).

| # | Deliberate fault | Corpus (2 912 runs) | Generated (1 000) | Also caught by |
|---|---|---|---|---|
| F11 | New keys appended to the list instead of placed in front of their bucket (container order) | **0** | 283 | `hash-order.txt` |
| F12 | Confidence gain in `f64` instead of `powf` | 2 889 | 663 | — |
| F13 | BHop air tolerance hard-coded to its default (P1's F1, in Rust) | 2 — exactly `up/down:checks.bhop.airTolerance` | 11 | — |
| F14 | printf ignores the `+` flag (`%+.2f`) | 408 | 99 | `printf.txt` |
| F15 | Duplicate-UUID tie-break reversed | **0** | 75 | — |
| F16 | Added-by-name marks never bind | **0** | 119 | — |
| F17 | Velocity response delay one tick short | **0** | 179 | — |
| F18 | KillAura snap rotation subtracted in `double` instead of `float` (added during the pass) | **0** | 29 (0 before the generator fix of §6) | — |

**Five realistic faults (F11, F15–F18) pass the whole hand-made corpus under all 416
configurations and are caught only by the generated scenarios** — the corpus has small,
consecutive entity ids, no duplicate entities, no marks, no known victim pings and only
grid-aligned rotations. That is what the ≥ 1 000 generated scenarios add.

## 6. Findings

* **A generator bug, found by the harness.** The first full run reported one generated scenario
  where the engines differed. Two generations of the same seed also differed: a clicker without a
  partner fell back to another clicker behavior and read `actors_[-1]` (undefined memory), so the
  C++ engine, fed the in-memory frames, saw a non-finite position that the trace stores as `0`.
  Fixed in the generator; and closed by construction: `--generate` now replays the frames *as read
  back from the trace it wrote* and stops on any non-finite number, and `engine_parity.py` checks
  that two generations of a seed are identical. Neither engine was at fault.
* **A coverage gap, found by falsification (F18).** The simulator relays rotations on the 360/256
  grid, and differences of grid values are exact in `float`, so a `float`/`double` slip in rotation
  arithmetic was invisible to both corpus and generated set. Real clients report interpolated
  rotations; the generator now gives a third of its players unquantized rotations, and F18 is
  caught in 29 scenarios. Parity still holds with the new generator (all 1 000 identical).
* **Pre-existing, left unchanged** (TECH_DEBT): `VigilParity`'s FNV offset is one digit short of
  the standard basis (a digest, harmless); `matchCount()` is printed with `%d`; outcome files key
  players by name, so of two records with one name the later wins. The Rust side mirrors all three.
* **The UCRT rounds exact printf ties half to even** (0.125 → "0.12", 0.375 → "0.38") — the same as
  Rust's formatter, so formatting needed no rounding code of its own; non-finite values and the
  `%s`-stops-at-NUL rule did.

## 7. Differences and their classification (RUST_PLAN §6.3)

**None.** Every compared byte is identical; no tolerance was needed or used. (The generator bug of
§6 was a difference between two *inputs*, not between the engines, and is fixed.)

## 8. Exit criteria

| Criterion (P3A_PLAN §6) | Evidence | Status |
|---|---|---|
| Full engine state byte-identical on the golden trace and the corpus under all 416 configurations | V3, V5 | ✅ |
| Byte-identical on ≥ 1 000 generated scenarios; outcomes, evidence and confidence trends therefore equivalent | V4, §4.1 | ✅ |
| Golden outcome reproduced byte for byte by the Rust engine | V5 | ✅ |
| Declaration identical; deterministic; no panic on hostile input; gates green | V2, V9, V10, V12 | ✅ |
| No C++ behavior change | V1 | ✅ |
| Tick cost measured (RUST_PLAN §8: ≤ C++ on the golden trace) | V11: 0.94× (whole corpus 0.98×) | ✅ |
| Falsification recorded | §5 | ✅ |
| CI runs V3–V5 on every build; docs updated | §2 | ✅ |

## 9. State of the migration and next steps

| Phase | State |
|---|---|
| P0, P1, P2 | ✅ |
| **P3A** Engine port, differential proof | ✅ this pass |
| P3B Test parity, rest of `detection/` | Not started (by instruction) |
| P4 Engine in the DLL (shadow) | Not started; requires P3B |

Nothing in P3A runs in game, so the live checklist ([LIVE_VALIDATION.md](LIVE_VALIDATION.md)) gains no
item; its first Rust item belongs to P4 (shadow-mode differences in live sessions).

**Recommendations**

1. Release this pass as a dev build when convenient — no user-visible change; `version.yaml` and
   the release notes were left untouched (releases are yours).
2. Commit `.gitattributes` (still untracked) with `tests/fixtures/** -text` added.
3. P3B next: port `tests/sim.h` to Rust and mirror the 32 detection tests and the engine-level
   validation and records tests by name; property tests for the VALIDATION §5 invariants (seeded
   generator, no new dependencies), each falsified once; the remaining marks actions, profile
   capture and the history/journal/log sinks with byte-level checks against the C++ files.
4. Keep `engine_parity.py` required in CI until the C++ engine is removed (P5): any engine change
   made in only one language fails the build.

**Reproduce** (after a C++ build with the tools and `cargo build --release` in `rust/`):

```powershell
python tools\ci\engine_parity.py --cpp build\Release\VigilParity.exe --rust rust\target\release\vigil-parity.exe --work parity-work --count 1000
rust\target\release\replay-check.exe tests\traces\golden-arena.vtrace tests\traces\golden-arena.expect.json --strict --engine
build\Release\VigilParity.exe --bench 30 ; rust\target\release\vigil-parity.exe --bench parity-work\corpus 30
```
