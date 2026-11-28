# P3 — Rust engine (offline): audit and feasibility

Phase P3 of [RUST_PLAN.md](RUST_PLAN.md) §7. Written before any P3 code, as §11.2 requires. This
document audits the C++ engine as it stands after P1 and P2, reviews what those phases delivered,
lists every hazard a behavior-preserving port must handle, and re-evaluates whether P3 can be done
as one phase. **Conclusion (§5): it cannot be done safely as one phase. It is split into P3A
(engine port, proven by differential parity) and P3B (Rust-native test parity and the rest of
`detection/`).** The detailed plan for P3A is [P3A_PLAN.md](P3A_PLAN.md).

| | |
|---|---|
| **Scope (RUST_PLAN §7)** | Port `detection/` check by check behind the same outcome; property tests for confidence invariants; differential testing |
| **Exit criteria (RUST_PLAN §7)** | No meaningful behavioral differences on the golden trace and on ≥ 1 000 generated scenarios. Detection outcomes, evidence generation and confidence trends equivalent. Every C++ detection test mirrored |
| **Constraints** | Offline only (no Rust in `Vigil.dll` before P4); no C++ engine behavior change; formats unchanged (§11.5); no threshold, wording or layout changes inside a port (§11.3) |

---

## 1. Review of P1 and P2

Both phases are committed (`d7e1614 Rust rewrite P1-P2`); the report is
[docs/RUST_P1_P2_REPORT.md](docs/RUST_P1_P2_REPORT.md).

| Inherited from | What P3 gets | Status checked at the start of P3 |
|---|---|---|
| P1 | The engine is a pure function of `WorldFrame`s and one `EngineConfig`; 248 parameters declared in a `ConfigSchema`; no globals (CI: `engine-globals`) | Baseline tree builds (`/W4 /WX`); all five suites, the golden trace and the format fixtures pass |
| P1 | `VigilParity`: a digest of the *entire* engine state per (configuration, scenario): 416 configurations × 7 corpus scenarios, plus full default dumps and the registry declaration | Baseline output recorded; byte-identical to the P1/P2 final run |
| P1 | Scenario corpus (`tests/corpus.h`): 7 scenarios, every check and all 24 indicators | Used as is |
| P2 | `vigil-formats`: traces, outcomes, configs, schema, marks, profiles, JSON with C++ number text; `compare` (the outcome oracle); `replay-check` | Reused: the Rust engine reads traces and configurations and writes outcomes without new format code |
| P2 | CRT math parity: `sin cos atan2 pow sqrt fmod floor ceil round fmax` bit-exact on engine input ranges | Holds for `f64`; **`powf` (the confidence gain) is not covered yet** (§3, H4) |
| P2 | Cargo workspace, toolchain 1.98.1, no dependencies, `unsafe` forbidden, `panic = "unwind"` | Unchanged |

Findings carried forward from the P1/P2 report: `.gitattributes` is still untracked in this
checkout (every reader tolerates `\r`, so nothing breaks); the registry's pre-existing quirks
(TECH_DEBT) are host-side and do not affect the engine.

## 2. Audit: what `detection/` contains

| Part | Files | Lines | Role in P3 |
|---|---|---|---|
| Per-frame pipeline | `engine.*` | 931 | **Port**: phases, tracks, classification, records, exemptions, checks, combat, history sampling |
| Framework | `check.*`, `evidence.h`, `confidence.cpp`, `config.*` | 875 | **Port**: check interface, parameter declaration, indicators, envelopes, confidence model |
| State | `track.*`, `combat.*`, `match.*`, `roster.*`, `session.*` | 1 067 | **Port** |
| 11 checks | `checks/*.cpp` | 1 371 | **Port**, expression for expression |
| Marks | `marks.*` | 409 | Engine reads exemptions and binds names: **port the engine-facing part**; the UI-facing actions and persistence belong to the records feature |
| Formats | `trace.*`, `replay.*`, file halves of `marks`/`snapshot` | ≈ 500 | **Already ported** in P2 (`vigil-formats`) |
| Profiles | `snapshot.*` (capture, store, import) | 657 | Not part of the per-frame engine; built *from* records |
| Host persistence | `Session::loadHistory/saveHistory/journal`, `Marks::save`, `log::writef` | ≈ 150 | Write-only side channels (ARCHITECTURE §6): never influence a decision |
| Registry binding | `config_binding.*` | 139 | Stays in C++ (the host owns the registry) |

### 2.1 Parity hazards found

Every item below changes the full engine state if the port gets it wrong. Most would not show in
an outcome summary; all of them show in a `VigilParity` digest.

| # | Hazard | Where | Consequence if missed | Port rule |
|---|---|---|---|---|
| H1 | **Iteration order of `std::unordered_map<int, Tracked>`** | `Engine::onFrame`, `classifyAll`, `detectStall`, nearest-opponent loop | Records are created, evidence and detection ids assigned, alerts queued and combat candidates tie-broken in *MSVC hash-table order*. A different order renumbers evidence, reorders alerts and can change which of two equal candidates is the attacker | Emulate the MSVC container exactly: FNV-1a hash of the key bytes, bucket mask, 8 initial buckets, ×8 growth below 512 buckets, ×2 above, insertion at the front of a non-empty bucket, order-preserving rehash, no shrink on erase or clear. Prove it against a C++-generated fixture |
| H2 | Iteration order of `std::unordered_map<std::string, …>` | `classifyAll` name groups | Display suffixes of colliding names in a rare case | Same emulation over string keys |
| H3 | **printf text** in evidence summaries, details, detection summaries, debug lines | `fmt()` (`vsnprintf`, 512-byte buffer) in every check | Different text = different state; `%.2f` rounding of ties, `%+.2f`, `%02x`, `%lld`, truncation at 511 bytes | A byte-exact `printf` subset (`d i u x f g s %`, flags, width, precision), validated against a C++-generated table, including ties, negative zero, NaN and infinities |
| H4 | **`float` arithmetic**: confidence, severity, weights, suspicion are `float`; `std::pow(float, float)` is `powf` | `confidence.cpp`, `engine.cpp`, every severity | Off-by-one-ulp confidence changes contributions, detections and peaks | Mirror every `float`/`double` promotion exactly; prove `powf` against the CRT (extend the math table) |
| H5 | `std::max`/`std::min`/`std::clamp` semantics (`a < b ? b : a`) differ from Rust's `f64::max` for NaN and signed zeros | everywhere | Rare, but a NaN from a hostile trace would propagate differently | Helpers that mirror the STL definitions |
| H6 | `static_cast<int>(double)` out of range is `INT_MIN` on MSVC x64 (`cvttsd2si`); Rust saturates | config reads, casts | Hostile configuration documents | Helper that mirrors `cvttsd2si` |
| H7 | Strings are bytes; `tolower`/`isalnum` in the C locale | names, uuids, chat | Non-UTF-8 names (JNI modified UTF-8) | `Vec<u8>` everywhere (as `vigil-formats`), ASCII-only case mapping |
| H8 | `summarize` keys players by **name** (`o.players[r.name] = p`), last record wins | `replay.cpp` | Outcome of two records with one name depends on record order | Same rule, same order (H1) |
| H9 | Signed overflow on hostile tick values (`tick + 25`, `tick - lastSwing`) | engine, tracks | C++ wraps in practice; Rust debug builds panic | Wrapping arithmetic on tick math |
| H10 | Wall clock | tick cost statistics, `Session::startEpochMs` (journal file name), mark timestamps | None on decisions (ARCHITECTURE §6) | Kept out of the comparable state, as in C++ |
| H11 | Evaluation context: `emit()` is called *inside* a check and mutates the session, while the check holds references to tracks | `Engine::emit`, `CheckContext` | — (a Rust borrow problem, not a behavior one) | Checks never read anything `emit` or `debug` change (audited: the context offers tick, time, configuration, exemptions, tracks, relay floor). Actions are queued in program order and applied when the check returns, which is observably identical |
| H12 | Checks never mutate tracks (`markSegmentDirty` has no caller) | checks | — | Checks receive tracks by shared reference |
| H13 | `std::pow(0.91, n)` with an `int` exponent | `bhop.cpp`, `velocity.cpp` | Pre-2015 MSVC used repeated multiplication | Modern MSVC promotes to `pow(double, double)`; covered by the math table (extended with integer exponents taken through the same overload) |
| H14 | Bounded containers and their eviction order (evidence 600, timeline 1 500, history compaction at 720, alerts 64, debug 2 000, combat 64) | session, engine | Only long sessions reach them | Ported as written; long generated sessions exercise them |

## 3. What "the same behavior" will be measured against

| Instrument | What it compares | Strength |
|---|---|---|
| `compareOutcomes` / `replay-check` | Per player and check: observations, detections, final and peak confidence | Weak (a summary); exactly what users see |
| `VigilParity` digests | Every evidence value and text, contribution, envelope, detection, timeline entry, confidence history, statistic, alert, debug line, combat event, filtered entity and player state | **Exact**. In P1 it caught a slip every suite and the golden trace missed |
| Scenario corpus × 416 configurations | Every check, indicator and parameter | Broad but hand-made |
| Generated scenarios | Random mixes of behaviors, identities, phases, lag, marks and configurations | **New in P3**: the ≥ 1 000 the exit criterion asks for |

Decision: **the target is byte-identical full state, not "within tolerance".** RUST_PLAN §8 allows
minor floating-point differences that do not affect user-facing behavior; the audit found no
reason the port should need that allowance (P2 proved the CRT functions bit-exact), so any
difference is treated as a bug until shown otherwise. A difference that cannot be removed would be
recorded and classified (§6.3) — none is expected.

## 4. Work inventory

| # | Work | Estimate | Verified by |
|---|---|---|---|
| W1 | Rust engine: types, geometry, evidence/envelope, confidence, check framework and the 248-parameter declaration, track, combat, match, roster, session, engine-facing marks, engine, 11 checks, MSVC-order container, printf mirror | ≈ 4 500 lines | Differential parity (W2, W3) |
| W2 | Differential harness: C++ export of the configurations each run applied; Rust replay that writes the `VigilParity` format; comparison script; CI job | ≈ 900 lines | Itself (falsification) |
| W3 | Generated scenarios: a C++ generator over the simulator (behaviors, identities, phases, lag, marks, configurations), ≥ 1 000 per run | ≈ 700 lines | Coverage counts; parity |
| W4 | New fixtures: hash order, printf table, `powf` and integer-exponent `pow` rows, presets | ≈ 250 lines | C++ `format_fixtures` + Rust tests |
| W5 | `replay-check` with the Rust engine; golden outcome reproduced | ≈ 150 lines | CI |
| W6 | Benchmark (tick cost C++ vs Rust) | ≈ 100 lines | Report |
| W7 | Rust simulator (`sim.h`) and **every C++ engine test mirrored by name**: 32 detection tests, the engine-level validation tests, the engine-facing records tests | ≈ 2 500 lines | Test parity matrix |
| W8 | Property tests for the confidence invariants (no `proptest`: a seeded generator, as P2's mutation test) | ≈ 300 lines | Falsification |
| W9 | The rest of `detection/` in Rust: the full marks state machine (watch, confirm, exempt, notes, search, add-by-name), profile capture, history and journal sinks, the detections log channel | ≈ 1 200 lines | Ported tests (W7) + file parity |

## 5. Feasibility as a single phase — re-evaluated

**Not feasible as one phase, by the criteria of §11.2 (exit criteria independent, validation clear,
risk acceptable).** Reasons:

1. **Size.** W1–W9 are roughly 10 000 lines of new Rust plus C++ tooling — more than twice P1 and
   P2 together, and larger than the C++ engine itself. One pass of that size cannot keep every
   line reviewed against its C++ original.
2. **Two different kinds of proof.** W1–W6 are proven *differentially*: C++ is the oracle and the
   question is "identical state, yes or no". W7–W9 are proven *by their own tests*: the question is
   "does the Rust suite say what the C++ suite says". Mixing them in one pass blurs which evidence
   supports which claim.
3. **Risk ordering.** The highest risks (H1, H3, H4) can only be retired by the differential
   harness. Porting 2 500 lines of tests onto an engine whose parity is not yet established would
   be wasted if the engine's structure has to change to reach parity.
4. **Independent exit criteria.** "Identical on the golden trace and ≥ 1 000 generated scenarios"
   does not depend on the Rust test port, and "every C++ detection test mirrored" does not depend
   on the generator.

### 5.1 The split

| Phase | Scope | Exit criteria |
|---|---|---|
| **P3A — Engine port, differential proof** | W1–W6: the complete per-frame engine (everything that decides), engine-facing marks, the differential harness, generated scenarios, new fixtures, `replay-check --engine`, benchmark | Full engine state **byte-identical** to C++ (`VigilParity` format) on the golden trace, the corpus under all 416 configurations, and **≥ 1 000 generated scenarios**; the golden outcome reproduced byte for byte; Rust schema = committed schema fixture; deterministic; never panics on hostile input; tick cost measured; falsification recorded; no C++ behavior change |
| **P3B — Test parity and the rest of `detection/`** | W7–W9: Rust simulator, every C++ detection test mirrored by name, property tests for confidence invariants, full marks state machine, profile capture, history/journal/log sinks | Every C++ engine test has a same-named Rust counterpart that passes (test parity matrix); property tests for every confidence invariant in VALIDATION §5, each falsified once; persistence sinks write the C++ bytes; P3A parity still holds |

RUST_PLAN's P3 exit criteria are met when **both** are met. P4 does not start before P3B.

Why this cut and not "combat checks first, movement checks later": every check runs inside the same
pipeline and shares records, ids and evidence numbering, so a partial engine cannot be compared by
full-state digest; a by-check split would weaken the strongest instrument the project has. Why not
"engine without marks": marks decide exemption and the binding of added-by-name players, which are
engine decisions; leaving them out would leave a decision unported.

## 6. Principles for both sub-phases

1. **Expression for expression.** Each Rust function mirrors one C++ function, with the same
   operation order and the same `float`/`double` promotions; refactoring waits until after P5.
2. **No C++ engine change.** C++ changes are limited to tooling (`VigilParity`, `VigilFormats`,
   test headers). The default `VigilParity` output must stay byte-identical to the baseline.
3. **Every difference classified** (RUST_PLAN §6.3): expected implementation difference, benign, or
   regression. Only regressions block; P3A's target is that the list is empty.
4. **Falsification** for every new parity instrument (§11.6).
5. **Docs in the same pass**: ARCHITECTURE, VALIDATION, RUST_PLAN §7, rust/README.
