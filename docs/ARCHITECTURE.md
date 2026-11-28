# Vigil architecture

For developers working on Vigil. How the pieces fit, where data flows, which thread owns what,
what is persisted, and which boundaries must not be crossed. Testing is in
[VALIDATION.md](VALIDATION.md); releases in [RELEASE_PROCESS.md](RELEASE_PROCESS.md); the
long-term Rust plan in [../RUST_PLAN.md](../RUST_PLAN.md).

## 1. Shape

```
                 Vigil.exe (launcher)                         Lunar Client (javaw.exe)
            ┌──────────────────────────┐            ┌────────────────────────────────────────┐
            │ self-check · extract     │  LoadLibrary│ Vigil-<ver>.dll                        │
            │ runtime · find game ·    │────────────▶│  platform/  dllmain · hooks · guard   │
            │ version handshake        │  events +  │  game/      JNI adapter               │
            └──────────────────────────┘  shared mem│  ui/        OpenGL interface + HUD    │
                                                     │  detection/ engine (pure C++)         │
                                                     │  identity/ utilities/ net/ core/      │
                                                     └────────────────────────────────────────┘
```

| Layer | Library (CMake) | Depends on | May use | Must never use |
|---|---|---|---|---|
| `core/` | `vigil_core` | std, shell32 (paths) | JSON, logging, settings registry | JNI, OpenGL, game types |
| `detection/` | `vigil_detection` | `vigil_core` | std only | Windows, JNI, OpenGL, WinHTTP, UI |
| `identity/`, `utilities/`, `net/` | `vigil_services` | `vigil_core`, WinHTTP | worker threads (identity) | JNI, OpenGL |
| `ui/` | `vigil_ui` | all of the above | OpenGL 1.x, GDI fonts, winmm | JNI |
| `game/`, `platform/` (DLL) | `Vigil` (shared) | `vigil_ui` | JNI, Win32 hooks | — |
| launcher | `VigilApp` → `Vigil.exe` | `vigil_ui` | Win32, BCrypt, version.lib | JNI |

The library split is enforced by the linker: `detection_tests`, `records_tests`,
`validation_tests`, `VigilReplay`, `VigilParity` and `VigilFormats` link **only** `vigil_detection`,
so any accidental dependency on Windows, the UI or the network fails the build.

**Rust (`rust/`, RUST_PLAN P2–P3).** A Cargo workspace beside the C++ tree, not linked into any
shipped binary yet. `vigil-formats` reads and writes every persisted format and the trace/outcome
pair byte-for-byte like `core/json` and the C++ readers (VALIDATION.md §8); the C++ tool
`VigilFormats` is its twin and generates the shared fixtures in `tests/fixtures/formats`.
`vigil-detection` is all of `src/detection` except the registry binding (host): the engine (P3A),
ported expression for expression, whose entire state is byte-identical to the C++ engine's on the
scenario corpus under every configuration and on generated scenarios; and (P3B) the user's marks,
profile capture and the profile store, the cross-session history, the session journal and the
"detections" log channel, which write the C++ bytes (VALIDATION.md §9). `VigilParity` and
`vigil-parity` are twins. Every C++ engine test has a Rust counterpart of the same name that counts
the same expectations and feeds the engine the same frames (VALIDATION.md §10). The DLL link (P4)
comes later; until then the C++ engine drives the product, a file's meaning is defined by the C++
reader, and the Rust side is proven to agree.

## 2. Data flow (one client tick)

```
 Minecraft client thread (scheduled VigilClientPump, ~20 Hz)
   Runtime::pump
     settingsSync.sync ────────── registry changed? → engine.setConfig(EngineConfig snapshot)
     context.poll ─────────────── scoreboard → GameContext
     chat.poll ────────────────── chat lines → utilities, identity, next frame
     sampler.sample ───────────── WorldFrame { tick, time, context, chat, players[PlayerFrame] }
        │                          (+ optional TraceWriter: the same frame to traces/*.vtrace)
        ▼
     Engine::onFrame(WorldFrame)
        match tracker ─ phase & scene (Lobby · Queue · Match · Post-game · Replay)
        tracks ──────── per entity history (positions, swings, air segments, data quality)
        classify ────── Player / NPC / Duplicate (roster.h)
        records ─────── PlayerRecord per person (session.h): relevance, presence, carry-over
        exemptions ──── per-tick situations (knockback, liquids, stalls …) + user exemptions
        checks ──────── evidence (with behavioral envelope) → ConfidenceModel → Detection/Alert
        combat ──────── attributed hits → onCombat / onHitTaken
     dispatchAlerts ── chat line, Minecraft sound
     handleMenu ────── opens/closes the host screen
 Render thread (SwapBuffers hook)
   App::render ─────── menu (pages) or HUD (notifications, briefing, notices)
```

`WorldFrame` is the **only input** to detection besides its configuration. Everything Vigil
concludes is a function of the frame sequence and one `EngineConfig` snapshot, which is what makes
traces, golden tests and a future Rust port checkable (§6).

**Configuration (RUST_PLAN P1).** The engine declares its 248 parameters in a `ConfigSchema`
(`detection/config.h`) and reads them only from an immutable `EngineConfig` snapshot. The settings
registry never reaches the engine directly: `detection/config_binding.*` registers the parameters
in the registry (labels, ranges, warnings, presets — what the UI shows) and, at the top of every
pump, hands the engine a new snapshot if anything changed. The UI reads the registry itself
(`ui::liveConfig`), so a toggle shows its new state on the frame it is clicked; the engine applies
it from the next pump. Since the UI and the pump run on the same client thread, no setting can
change in the middle of `Engine::onFrame`.

## 3. Threads

| Thread | Owns | Notes |
|---|---|---|
| Bootstrap (DLL main thread) | JVM attach, class lookup, hook installation, pump scheduling every 50 ms | Exits only with the process |
| Minecraft client thread | `Runtime::pump`, the engine, sampler, utilities, chat, menu screen | Every JNI call happens here |
| Render thread (Lunar: the same thread) | `App::render`, HUD, fonts, GL objects | Reads engine state; the UI mutates only marks, settings and records |
| Window procedure | input routing into `App::onKey/onMouse…` | Queues; never touches GL or JNI |
| Identity worker | HTTPS lookups (Mojang, Bordic) | Results read under a mutex |

## 4. Persistence (`%LOCALAPPDATA%\Vigil`)

| Path | Owner | Format | Version rules |
|---|---|---|---|
| `config.json` | `Settings` | `{format:1, writtenBy, settings{}}` | Unknown keys are kept and written back |
| `history/players.json` | `Session` | cross-session player history | — |
| `history/marks.json` | `Marks` | format 2 (watch, confirmed, exempt, notes) | Format 1 loads; newer formats open read-only |
| `profiles/*.json` | `SnapshotStore` | `vigil-profile` format 1 | Checks referenced by id; newer formats refused with a message |
| `exports/`, `imports/` | `RecordsService` | profile files in/out | Imports are re-serialized, never copied verbatim |
| `sessions/*.jsonl` | `Session::journal` | evidence/detection lines | Optional (Settings → Data) |
| `traces/*.vtrace` | `TraceWriter` | `vigil-trace` format 1 | Off by default; capped 15 min / 64 MB |
| `diagnostics/*.txt` | Status → Diagnostics | plain text | No player names |
| `logs/<channel>.log` | `log` | text, 4 MB rotation | — |
| `runtime/Vigil-<ver>.dll` | launcher | the extracted runtime | SHA-256 + version verified before use |

## 5. Key modules

| Module | Responsibility |
|---|---|
| `detection/engine.*` | The per-frame pipeline; owns tracks, records, checks, combat, match tracker, marks |
| `detection/config.*` | `ConfigSchema` (the engine's parameter declaration), `EngineConfig` (the snapshot it runs on), their JSON documents |
| `detection/config_binding.*` | The only detection code that knows the settings registry: registration, snapshots, sync, presets |
| `detection/check.*`, `checks/*` | One file per check; each emits evidence through indicators, never decides |
| `detection/evidence.h`, `confidence.cpp` | Evidence, the behavioral envelope, the confidence model and its gate |
| `detection/match.*`, `roster.*` | Game phases and scenes; who is a player |
| `detection/marks.*` | The user's decisions (watch, confirmed, exempt), name binding |
| `detection/snapshot.*` | Saved profiles: format, capture, store, export/import |
| `detection/trace.*`, `replay.*` | Diagnostic traces and outcome comparison |
| `ui/app.cpp` | Shell: rail, top bar, keyboard routing, settings for appearance/accessibility, match cleanup |
| `ui/widgets.*` | Immediate-mode controls, keyboard focus, numeric entry, scrolling |
| `ui/page_*.cpp` | One file per page; `page_records.cpp` hosts the add-player picker |
| `ui/records.*` | Save/export/import actions, clipboard import, result messages |
| `ui/suspicion.*` | The suspicion language: tiers, halos, marks, category wording, briefing |
| `ui/sound.*` | Synthesized interface sounds (optional) |
| `game/sampler.*` | JNI reads into `PlayerFrame`, binding report |
| `game/runtime.*` | Orchestration, commands, alerts, highlight, trace recording, `Host` for the UI |
| `platform/runtime_guard.*` | Version/build checks between exe, embedded, extracted and running runtimes |
| `platform/launcher.cpp` | Vigil.exe: self-check, extraction, injection, handshake window |

## 6. The engine contract (what a port must preserve)

* **Input:** a sequence of `WorldFrame`s (types.h) and an `EngineConfig` (config.h), which may be
  replaced between frames. Nothing else: the engine reads no global state (enforced by
  `validate_repo.py engine-globals` and the "engines share nothing" test).
* **Configuration document:** `{"kind":"vigil-engine-config","format":1,"values":{key: value}}`,
  in schema order, switches as JSON booleans, numbers exact. Unknown keys are ignored and missing
  ones keep their default; wrong types and newer formats are refused. The declaration itself is
  `tests/fixtures/engine-schema.json` (`VigilReplay --engine-schema`).
* **Output:** `Session` records (per player: evidence with values and envelopes, per-check
  confidence history, detections, timeline, behavior stats), alerts, engine stats; plus write-only
  diagnostic lines to `core/log` ("detections" channel), which never influence a decision.
* **Determinism:** the same frames and configuration produce the same outcome. Wall-clock time is
  used only for statistics (tick cost) and epoch stamps on marks/history, never for decisions.
* **Comparison:** `detection/replay.h` reduces an engine's state to an outcome (per player and
  check: observations, detections, confidence) that two implementations must agree on;
  `VigilParity` compares the *full* state (every evidence value, contribution, timeline entry and
  alert) across a scenario corpus and every parameter (VALIDATION.md §4).
* **Order is part of the contract:** the engine visits entities in `std::unordered_map<int, …>`
  order, which decides the order records are created, evidence and detection ids are assigned,
  alerts are queued and equal combat candidates are tie-broken. A port reproduces MSVC's iteration
  order (the Rust engine does: `hashorder`, proven against `tests/fixtures/formats/hash-order.txt`);
  changing the C++ container would be a behavior change in its own right.
* **Text is part of the contract:** evidence summaries, details and debug lines are `vsnprintf`
  output (512-byte buffer); a port reproduces the UCRT's formatting, including ties, signed zeros
  and non-finite values (`printf`, proven against `printf.txt`). The "detections" log lines are
  `log::writef` messages (2 048-byte buffer).
* **Sort order of ties is part of the contract:** `Marks::all()` and the saved-profile list are
  sorted with `std::sort`, which is not stable above 32 elements; a port reproduces MSVC's
  introsort (`msvcsort`, proven against `sort-order.txt`).
* **Wall clock and local time are host inputs:** the journal file name, history `lastSeen`, mark
  times and a profile's save time read the wall clock; profile ids and log lines read local time.
  None of them influences a decision. The Rust side takes both from the host (`clock`) and the
  harness masks them when comparing.

## 7. Boundaries worth keeping

1. Nothing below `ui/` knows the UI exists; the UI reaches the game only through `Host`.
2. The engine never reads marks to judge anyone; it only skips players the user exempted.
3. The engine never reads the settings registry or any other global; parameters arrive as an
   `EngineConfig` between frames (only `detection/config_binding.*` may include `core/settings.h`).
4. JNI calls happen on the Minecraft thread only, through `game/`.
5. The UI never blocks: file and network work is small (records) or on the identity worker.
6. Persisted formats carry a format number; readers never destroy what they don't understand.
