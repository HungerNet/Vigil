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
`validation_tests` and `VigilReplay` link **only** `vigil_detection`, so any accidental dependency
on Windows, the UI or the network fails the build.

## 2. Data flow (one client tick)

```
 Minecraft client thread (scheduled VigilClientPump, ~20 Hz)
   Runtime::pump
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

`WorldFrame` is the **only input** to detection. Everything Vigil concludes is a function of the
frame sequence and the settings, which is what makes traces, golden tests and a future Rust port
checkable (§6).

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

* **Input:** a sequence of `WorldFrame`s (types.h) and the settings registry's values.
* **Output:** `Session` records (per player: evidence with values and envelopes, per-check
  confidence history, detections, timeline, behavior stats), alerts, engine stats.
* **Determinism:** the same frames and settings produce the same outcome. Wall-clock time is used
  only for statistics (tick cost) and epoch stamps on marks/history, never for decisions.
* **Comparison:** `detection/replay.h` reduces an engine's state to an outcome (per player and
  check: observations, detections, confidence) that two implementations must agree on.

## 7. Boundaries worth keeping

1. Nothing below `ui/` knows the UI exists; the UI reaches the game only through `Host`.
2. The engine never reads marks to judge anyone; it only skips players the user exempted.
3. JNI calls happen on the Minecraft thread only, through `game/`.
4. The UI never blocks: file and network work is small (records) or on the identity worker.
5. Persisted formats carry a format number; readers never destroy what they don't understand.
