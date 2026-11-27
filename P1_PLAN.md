# P1 — Decouple engine configuration (plan)

Phase P1 of [RUST_PLAN.md](RUST_PLAN.md) §7. Written before any P1 code, per working agreement §11.2.
Outcome and evidence: [docs/RUST_P1_P2_REPORT.md](docs/RUST_P1_P2_REPORT.md).

| | |
|---|---|
| **Scope (RUST_PLAN §7)** | Checks read an immutable `EngineConfig` snapshot built from the registry on change, instead of `Setting*`; the engine owns no globals |
| **Exit criteria (RUST_PLAN §7)** | All suites + golden unchanged; config snapshot serializable |
| **Rule (§11.3)** | Behavior-preserving: no threshold, wording or layout change |

## 1. Audit of the current implementation

### 1.1 How configuration reaches detection today

* `Engine::initialize()` calls `registerGlobalSettings()` and `Check::registerAll()` for each of the
  11 checks. Both write **directly into the process-wide `Settings::instance()` registry** and keep
  raw `Setting*` pointers into it.
* Every read is a live dereference of such a pointer at the moment of use (`threshold_->value`,
  `minBlock_->i()`, `combatOnly_->b()`, `alert_->f()`).
* `check.h` includes `core/settings.h`; `Indicator` carries `Setting* enabled, *weight`;
  `PresetEntry` carries a `Setting*`; `Check::applyPreset()` writes the registry.

| Where | `Setting*` held | Reads |
|---|---|---|
| `Engine` (globals) | 17: monitoring, 4 × run-in, analyze-self, ignore-teammates, 4 exemption graces, 4 hit-attribution, journal, history | `phaseAnalyzed/Recorded`, `computeExemptions`, `shouldAnalyze`, `playerState`, `onFrame` (journal, combat settings), `flushHistory`, `monitoringEnabled` |
| `Check` base (× 11) | 13 each: enabled, preset, alert, halfLife, minEvidence, minIndicators, window, cooldown, gain, sampling ×3, debug | `enabled()`, `confidenceParams()`, `debug()`, `sampling*()` |
| Indicators (24) | 2 each: enabled, weight | `indicatorOn()`, `indicatorWeight()` |
| Check-specific | 40 (KillAura 8, AutoClicker 6, Reach 3, Velocity 3, AutoBlock 1, Speed 3, BHop 3, NoSlow 2, Keep-Y 4, Flat 3, LegitScaff 4) | inside `onTick` / `onCombat` / `onHitTaken` |
| **Total** | **248 engine parameters** | ≈ 120 read sites |

Read conversions in use (all must be preserved exactly): `->value` (double), `->i()`
(`static_cast<int>`), `->b()` (`!= 0.0`), `->f()` (`static_cast<float>`). Int parameters are read
both ways (`placed_->i()` and `placed_->value * 1.6`).

### 1.2 Consumers outside detection

| Consumer | Uses | Notes |
|---|---|---|
| `ui/page_checks.cpp` | `enabled()`, `debug()`, `presetSetting()`, `applyPreset()`, `isPresetControlled()`, `ind.enabled/weight->…` | Displays and edits the registry; must keep showing the registry's value the same frame it changes |
| `ui/page_status.cpp`, `ui/page_common.cpp`, `ui/hud.cpp` | `enabled()`, `monitoringEnabled()` | Same frame as the toggle |
| `ui/page_evidence.cpp` | `confidenceParams()` | Explains an observation with the current parameters |
| `game/runtime.cpp` | `engine_.initialize()` before `Settings::load()` | Registration order defines `config.json` key order and the Settings page order |
| `tools/preview`, `tools/replay`, tests | `initialize()`; `detection_tests` uses presets through the registry | No test changes a detection parameter except `applyPreset(1)` (= defaults) |

### 1.3 Threads and timing

The engine runs on the Minecraft client thread (`Runtime::pump`). The UI renders in the
SwapBuffers hook and receives input from the window procedure; in Lunar (LWJGL 2) both are on the
same client thread, so **no setting can change while `Engine::onFrame` runs**. Every value the
engine reads during one `onFrame` equals the registry value at the start of that pump. This is the
property that lets a per-pump snapshot be exactly equivalent to live reads.

### 1.4 Other global state in `detection/`

* No mutable `static` variables (only `static const`/`constexpr` tables).
* `core/log` (`log::writef("detections", …)` in `emit`/`debug`) is a **write-only** diagnostic sink:
  it never influences a decision. It stays, documented as part of the engine contract's outputs.
* Wall-clock is used only for tick-cost statistics (already documented).

### 1.5 Quirks found (pre-existing, not changed by P1)

* `Settings::load()` assigns a JSON boolean to *any* setting type (`"checks.reach.threshold": true`
  → 1.0, bypassing range clamping); a lenient `strtod` also accepts `nan`/`inf` in `config.json`.
  The snapshot mirrors whatever the registry holds; serialization documents the limits (§2.4).
* Registering a setting does not bump `Settings::revision()`; only `set()`/`load()` do.

## 2. Design

### 2.1 New types (`src/detection/config.h`, pure C++, no registry)

* **`ConfigSchema`** — the engine's declaration of its parameters, in registration order:
  key, type (Bool/Int/Float/Choice), default, range, choices, and presentation metadata (label,
  tooltip, group, advanced, warning text and predicate). Built by `Engine::initialize()`: globals
  first, then each check (the same order as today). Owned by the engine (`shared_ptr<const>`).
* **`ParamId`** — index of a parameter in the schema. Checks keep `ParamId`s instead of `Setting*`.
* **`EngineConfig`** — an immutable value: one `double` per parameter + the schema. Accessors mirror
  `Setting` exactly: `value(id)`, `i(id)` = `static_cast<int>`, `b(id)` = `!= 0.0`, `f(id)` =
  `static_cast<float>`. Default-constructed from the schema's defaults.
* **Serialization** — `configToJson` / `configFromJson` (`{"kind":"vigil-engine-config","format":1,
  "values":{…}}`, schema order, booleans as JSON booleans, exact numbers via `core/json`), and
  `schemaToJson` (the full declaration, for the Rust side in P2).

### 2.2 Engine and checks

* `Engine` holds `EngineConfig config_`; `setConfig(const EngineConfig&)` replaces it between frames
  (refused if the schema differs). `CheckContext` gains `config()`.
* `Check` methods take the config: `enabled(cfg)`, `confidenceParams(cfg)`, `indicatorOn(cfg, i)`,
  `indicatorWeight(cfg, i)`, `debug(cfg)`, `sampling*(cfg)`. Check bodies change
  expression-for-expression (`threshold_->value` → `cfg.value(threshold_)`), nothing else.
* `Engine::initialize()` no longer touches `Settings::instance()`. `check.h` no longer includes
  `core/settings.h`.

### 2.3 Registry binding (`src/detection/config_binding.h`, the only detection file that knows `Settings`)

* `registerEngineSettings(Settings&, const ConfigSchema&)` — declares every parameter in the
  registry with exactly the metadata it has today, in schema order (called by each host right
  after `engine.initialize()`, so registration order is unchanged).
* `configFromSettings(const Settings&, schema)` — one pass over the registry.
* `ConfigCache` — rebuilds a snapshot only when `Settings::revision()` changes.
* `SettingsSync::sync(Engine&, const Settings&)` — hosts call it at frame boundaries.
* Preset helpers for the UI: `presetSetting`, `applyPreset`, `isPresetControlled`, `settingFor`
  (same writes, same order as `Check::applyPreset` today).

### 2.4 Sync points (where the snapshot is taken)

| Host | Sync |
|---|---|
| `Runtime` | after `Settings::load()` in `initialize`; at the top of every `pump` (before any engine call); before `flushHistory` in `shutdown` |
| `VigilPreview` | before every `engine.onFrame` |
| `VigilReplay`, tests | engines run on their schema defaults (identical to registry defaults); tests that change parameters sync explicitly |

The UI keeps reading the **registry** for everything it displays (via a registry-backed
`ConfigCache`), so a toggle shows its new state on the same frame, as today.

### 2.5 Why this is behavior-preserving

1. Same values: the snapshot copies `Setting::value` bit-for-bit; accessors apply the same casts.
2. Same moment: the registry cannot change during `onFrame` (§1.3); the snapshot is taken at the
   start of each pump, so every read in that pump sees the value a live read would have seen.
3. Same registry: identical keys, metadata, defaults and registration order (verified by a dump).
4. Same reads: each check body is converted expression-for-expression.

## 3. Validation requirements

| # | Requirement | Method |
|---|---|---|
| V1 | All 5 suites pass with the same tests and expectations (plus new ones) | CTest, before/after counts |
| V2 | Golden trace outcome unchanged; outcome JSON byte-identical | `VigilReplay --expect`, file compare |
| V3 | **Detailed engine parity** under every parameter | New `VigilParity` tool: full engine-state dump (every evidence value, contribution, confidence, detection, timeline entry, alert, debug line, history point) over a corpus covering all 11 checks, for: defaults, Relaxed/Strict presets, **each of the 248 parameters perturbed one at a time**, and mid-session changes. Pre-P1 and post-P1 output byte-identical |
| V4 | Registry unchanged | Dump of every registered setting (order, key, label, tooltip, warning, group, type, range, choices, flags, default, warning predicate probed at several values) byte-identical pre/post; committed as a fixture with a test |
| V5 | Engine owns no globals | Repo check (no `Settings`/`settings.h` in `detection/` outside the binding); test: two engines with different configs, interleaved frame by frame in one process, each equal its solo run |
| V6 | Snapshot serializable | Round trips exact (text and bits) for defaults, presets and perturbed configs; hostile documents refused |
| V7 | Tests are falsifiable | Break a parameter read and the sync on purpose; parity and tests fail; restore |
| V8 | No user-visible change | `/W4 /WX` build of every target (DLL, exe, preview); `--self-check`; preview screenshots of every page compared pixel-for-pixel pre/post |

## 4. Exit criteria

1. All suites and the golden trace unchanged (V1, V2) — RUST_PLAN.
2. Config snapshot serializable with exact round trips (V6) — RUST_PLAN.
3. No `Setting*` and no `Settings::instance()` in the engine and checks; checks read `EngineConfig`.
4. The engine owns no globals (V5).
5. Zero differences in V3 and V4; V8 shows no visible change.
6. Falsification recorded (V7).
7. ARCHITECTURE (§6 contract), VALIDATION, RUST_PLAN §7 updated in the same pass.

## 5. Roadmap

1. **Baseline**: build the untouched tree; record suites, golden outcome, registry dump, `VigilParity`
   output and preview screenshots.
2. Add `config.h/.cpp` (schema, snapshot, JSON).
3. Convert `Check` and the 11 checks; then `Engine` (+ `CheckContext::config()`).
4. Add `config_binding.h/.cpp`; wire hosts (runtime, preview, replay) and the UI call sites.
5. Tests: new validation tests (config round trip, hostile config, isolation, sync), updated preset
   test, registry fixture test; repo check in `validate_repo.py`.
6. Run V1–V8 against the baseline; falsify; fix until zero differences.
7. Documentation; P1 marked ✅ in RUST_PLAN §7.

## 6. Risks

| Risk | Mitigation |
|---|---|
| A read converted with the wrong parameter or cast | V3 perturbs each parameter alone and compares full state |
| Registration order/metadata drift changes the Settings page or `config.json` | V4 dump fixture |
| Snapshot taken at the wrong moment (stale values) | Sync at pump start; mid-session-change variants in V3; sync test |
| UI shows a stale value for a frame | UI reads the registry-backed cache, not the engine snapshot |
| Hosts forget to register/sync | Every host updated; preset/registry tests fail if registration is missing |

## 7. Out of scope

Any Rust code (P2), threshold or wording changes, the C ABI (P4), routing `core/log` through a
callback (P3/P4, when the Rust engine needs it), fixing the §1.5 quirks.
