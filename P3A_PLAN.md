# P3A — Rust engine port, proven by differential parity (plan)

First half of RUST_PLAN P3, as split in [P3_PLAN.md](P3_PLAN.md) §5. Written before any P3A code.
P3B (Rust-native test parity, property tests, the rest of `detection/`) is **not** part of this
pass.

| | |
|---|---|
| **Scope** | The complete per-frame engine in Rust (`rust/crates/vigil-detection`); engine-facing marks; a differential harness that compares the *entire* engine state with C++; ≥ 1 000 generated scenarios; new cross-language fixtures; `replay-check --engine`; a tick-cost benchmark |
| **Not in scope** | Linking Rust into any shipped binary (P4); history, journal and log sinks, the UI-facing marks actions, profile capture, the Rust simulator and test port (P3B); any change to C++ engine behavior |
| **Baseline** | `git archive HEAD` (d7e1614 + 6fc70fa + f5e353a), built with the same toolchain: suites 66 tests / 550 expectations green; golden outcome matches; `VigilParity` output recorded (byte-identical to the P1/P2 final run) |

## 1. Design

### 1.1 Crate

`rust/crates/vigil-detection` — library + binaries, depends only on `vigil-formats` (traces,
configurations, schema, marks file, outcome, JSON). No third-party crates, `unsafe` forbidden.

| Module | Mirrors | Notes |
|---|---|---|
| `cpp` | STL/CRT semantics | `max`/`min`/`clamp` as the STL defines them, `cvttsd2si` int casts, wrapping tick math, ASCII `tolower` |
| `printf` | `vsnprintf` subset | `d i u x X f F g G e s c %`, flags `- + space 0 #`, width, precision, `l ll z h` ignored; MSVC `inf`/`nan`/`-nan(ind)` |
| `hashorder` | MSVC `std::unordered_map` iteration order | Arena + doubly linked list + bucket table; lookups through a `HashMap` index |
| `types` | `types.h` | `Vec3`, `mc::*`, geometry, multipliers; frames are `vigil_formats::trace` types |
| `evidence` | `evidence.h`, `confidence.cpp` | `EvidenceValue`, `Envelope`, `derive_envelope`, `ConfidenceModel` (`f32` state) |
| `config` | `config.*` (declaration side) | `SchemaBuilder` producing a `vigil_formats::config::Schema` + warning predicates; reads via `EngineConfig` with C++ conversions |
| `check` | `check.*` | `Check` trait, `CheckBase` (common parameters, indicators, presets), `Exemptions`, the queued `Ctx` |
| `track`, `combat`, `matchtracker`, `roster`, `session`, `marks` | same names | `marks`: load from `MarksFile`, `exempt`, `find`, `bind_name`, `remember_name` |
| `checks/*` | `checks/*.cpp` | One file per C++ file, expression for expression |
| `engine` | `engine.*` | `Engine` with the same public reads the parity dump needs |
| `replay` | `replay.cpp` | `summarize`, `replay_frames` → `vigil_formats::outcome::Outcome` |
| `parity` | `tools/parity/parity.cpp` | The full-state dump and registry dump, byte for byte |

### 1.2 Engine structure (borrowing without changing behavior)

* `tracked: HashOrder<i32, Tracked>` iterates exactly like the C++ map (H1).
* Check states live in each `Tracked` as `Vec<Box<dyn Any>>`; for a check call they are taken out
  of the entry, so the context can hold shared references to every track.
* `Ctx` gives checks the C++ `CheckContext` reads (tick, time, configuration, exemptions, tracks,
  local track, relay floor, `debug_enabled`). `emit` and `debug` append an action to an ordered
  queue that the engine applies as soon as the check returns — the same order as C++, and
  unobservable to checks (P3_PLAN H11).
* Numbers: every C++ `float` is `f32`, every promotion explicit, every `std::pow(float, float)` is
  `f32::powf`.

### 1.3 Parameter declaration

The Rust engine declares its 248 parameters itself (keys, types, defaults with the same
`float → double` promotions, ranges, labels, tooltips, groups, warnings, warning predicates,
presets). Checked three ways: `schema.to_json()` equals `tests/fixtures/engine-schema.json` byte for
byte; the warning predicates reproduce `VigilParity`'s registry probes; presets — and, as built,
all other check metadata (names, categories, summaries, "about" texts, requirements, sampling
support) — equal a new C++-generated fixture (`checks.json`).

## 2. Differential harness

```
VigilParity out.txt --export <dir>        # C++: as today, plus dir/plan.jsonl + dir/corpus/*.vtrace
vigil-parity <dir> rust.txt               # Rust: replays the plan, writes the VigilParity format
fc /b out.txt rust.txt                    # identical = parity

VigilParity --generate <dir> --count N --seed S     # C++: N generated scenarios, plan + expected.txt
vigil-parity <dir> actual.txt                       # Rust
tools/ci/engine_parity.py                           # runs both, compares, reports coverage
```

* **The plan** records what reached the C++ engine, not how: for each run, the trace, an optional
  marks file and every configuration snapshot `setConfig` received, with the frame before which it
  arrived (`-1` = before `resetSession`), as differences from the schema defaults with exact
  numbers. The Rust side therefore receives the identical configuration sequence without
  re-implementing the settings registry (which stays in C++).
* **The output** is the `VigilParity` format: registry dump (from the Rust declaration through a
  mirror of `registerEngineSettings`), corpus shapes, one digest line per run, full default dumps.
  Generated runs add one summary line per scenario (counts per check and indicator, filtered
  entities, stalls, combat events, pruned records) used for coverage.
* **On a difference**, both tools can write the full dump of a single run (`--full <variant>
  <scenario>`) so the first differing line is visible.

## 3. Generated scenarios (`tests/generator.h`)

Built on `tests/sim.h`; frames depend only on the seed. Each scenario draws:

| Dimension | Values |
|---|---|
| Players | 2–10 sims (+ local, sometimes absent or itself analyzed), teammates |
| Behaviors (re-drawn every 80–300 ticks) | idle, walking, legitimate sprint-jumping, low hops, fast ground glide and bursts, air strafe, air acceleration, eating/blocking while moving (legit and NoSlow), block-hitting, machine/human/jittered clicking, flat / Keep-Y / eagle / legitimate bridging, duels (random reach 2–5.5, aim away, no knockback, legitimate knockback, hurt delay 0–3, crowd), multi-target, robotic tracking |
| Movement context | potions (speed, jump boost, stale particles), movement attribute and sprint modifier, ice, slime, soul sand, liquids, ladders, cobwebs, riding, ceilings, wall masks, burning, dead, invisible, sneaking, knockback launches, teleports |
| Network | relay delay, cadence, freezes (lag), skipped ticks (≥ 5 = hitch), tick gaps, time jitter, ping |
| Identity | invalid names, `#id` names, v2 (NPC) UUIDs, empty UUIDs, duplicate UUIDs, name collisions (listed/unlisted), unlisted for a while, entity id reuse, leaving view (< and > 40 ticks), negative ids |
| Phases | Unknown flicker, Lobby, Queue, Match, Replay; world changes; game-end chat (and player chat that must not match) |
| Marks | exempt by UUID, added-by-name (`user:`) exempt/watch marks that bind, name-keyed marks, confirmed marks |
| Configuration | 0–12 random parameters (typed, in range, stepped), debug output often on, sampling, 0–3 mid-session changes including monitoring off/on |
| Long runs | a few marathon scenarios (history compaction at 720 points, evidence cap 600, timeline cap 1 500) and a churn scenario (> 400 records → pruning) |

## 4. Validation

| # | Requirement | Method | Pass |
|---|---|---|---|
| V1 | C++ unchanged | Suites, golden, `format_fixtures`, default `VigilParity` output vs baseline | Identical / byte-identical |
| V2 | Declaration | Rust schema JSON vs fixture; default config vs fixture; catalog vs `check-catalog.json`; check metadata and presets vs `checks.json`; predicates vs registry probes | Byte-identical |
| V3 | Corpus parity | Rust `vigil-parity` output vs C++ `VigilParity` output (registry, shapes, 2 912 digests, full default dumps) | Byte-identical |
| V4 | Generated parity | ≥ 1 000 generated scenarios (CI: one seed; locally: several seeds) | Every digest and summary identical |
| V5 | Golden | `replay-check --engine --strict`: Rust outcome JSON = committed `golden-arena.expect.json` byte for byte | Exit 0 |
| V6 | Container order | C++-generated `hash-order.txt` (random insert/erase/clear scripts, int and string keys) reproduced by `hashorder` | All orders equal |
| V7 | printf | C++-generated `printf.txt` (every format the engine and the dump use × edge values) reproduced by `printf` | All bytes equal |
| V8 | Math | `math.txt` extended with `powf` and integer-exponent `pow` rows | Bit-exact |
| V9 | Determinism | Rust run twice; two Rust engines interleaved frame by frame equal their solo runs | Identical |
| V10 | Robustness | Mutated corpus traces and random frames through the Rust engine | No panic |
| V11 | Performance | Tick cost on the golden trace and the corpus, C++ vs Rust, same machine | Measured and reported (RUST_PLAN §8: Rust ≤ C++) |
| V12 | Gates | `cargo fmt --check`, `clippy -D warnings`, `cargo test`, `/W4 /WX` C++ build | Green |
| V13 | Falsification | §5 | Each fault caught |

## 5. Falsification plan (break the Rust side on purpose, see it caught, restore)

| # | Fault | Expected to be caught by |
|---|---|---|
| F11 | Iterate tracked entities in insertion order instead of MSVC order | V3 / V4 digests |
| F12 | Confidence gain computed in `f64` instead of `powf` | V3 digests |
| F13 | BHop air tolerance read as its default instead of from the configuration (P1's F1, in Rust) | V3: exactly the `airTolerance` variants |
| F14 | `%.2f` written with Rust's own formatting where it differs (or a dropped `%+` flag) | V7 and V3 |
| F15 | Duplicate-UUID tie-break reversed | V4 only (the corpus has no duplicates) — shows what generation adds |
| F16 | Added-by-name marks never bind | V4 only |
| F17 | A Velocity response delay off by one tick | V3 |

## 6. Exit criteria

| Criterion | Evidence |
|---|---|
| Full engine state byte-identical on the golden trace and the corpus under all 416 configurations | V3 |
| Byte-identical on ≥ 1 000 generated scenarios; outcomes, evidence and confidence trends therefore equivalent | V4 |
| Golden outcome reproduced byte for byte by the Rust engine | V5 |
| Declaration identical; deterministic; no panic on hostile input; gates green | V2, V9, V10, V12 |
| No C++ behavior change | V1 |
| Tick cost measured | V11 |
| Falsification recorded | V13 |
| CI runs V3–V5 on every build; ARCHITECTURE, VALIDATION, RUST_PLAN, rust/README updated | — |

Any difference that cannot be removed is listed with its classification (RUST_PLAN §6.3) in the
report; only "expected implementation difference" or "benign" may remain, and the target is none.
