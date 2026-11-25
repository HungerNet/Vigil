# Implementation summary

## What changed

| Area | Result |
|---|---|
| Product | Vigil is a detection and analysis platform. Overlay, API, denick, macro and HUD features were removed (~38.5k lines; see `FEATURE_REMOVAL.md`). |
| Architecture | Layers: `core` → `detection` (pure C++) → `game` (JNI adapter) / `ui` (OpenGL) → `platform`. Engine, settings and UI share the Minecraft thread; the UI never calls JNI. |
| Detection | New framework (tracks, hit attribution, exemptions, evidence, confidence, sessions, history, journal). Eleven checks, four of them new; KillAura and BHop rewritten from first principles. |
| Configuration | Typed settings registry; every check exposes presets, thresholds, indicator weights, confidence behavior, sampling and debug, with tooltips, defaults and quality warnings. |
| UI | Native interface: Overview, Checks, Evidence Explorer, Players, Settings; in-game toasts; chat alerts with hover evidence; Suspect Highlight in world. |
| Brand | New name usage throughout, logo, app icon, palette, typography, voice. Frenchify removed from source, strings, binaries and build outputs. |
| Build | CMake targets `Vigil` (DLL), `VigilLauncher`, `VigilPreview`, `detection_tests`; no network dependencies; static CRT. CI builds, tests and packages. |
| Sources | Lost Java shims recovered (`java/src`) and rebuilt reproducibly (`tools/build_java.py`). |

New code by area: core 0.7k lines, detection 3.1k, game adapter 4.2k (2.4k of it the retained
team-color and highlight code), platform 0.4k, UI 4.4k, tests 0.6k, tools 0.7k.

## Quality assurance performed

| Check | Result |
|---|---|
| Full build (MSVC 19.44, `/W4 /permissive-`) of all four targets | ✅ 0 errors, 0 warnings |
| Detection test suite (16 scenarios, 94 expectations; legit vs. cheating traces at server precision) | ✅ all pass |
| Regression: legacy KillAura false-flag scenario | ✅ no evidence |
| Regression: 12-tick combo rhythm vs. AutoClicker (found with the preview arena) | ✅ no evidence |
| Preset invariant (Balanced = defaults for every check) | ✅ unit test |
| Settings persistence round-trip (save → modify → load), clamping, step snapping | ✅ unit test |
| JSON parser/writer round-trip and malformed-input rejection | ✅ unit test |
| UI: every page in every theme rendered with live engine data and reviewed | ✅ layout defects found and fixed |
| UI interaction: simulated clicks (Settings → Appearance), hover tooltips, toasts | ✅ verified in preview |
| Java shims: recompiled sources vs. original bytecode (`javap -c`) | ✅ same instructions (one loop uses a different local slot) |
| Binary dependencies: `Vigil.dll` imports only system DLLs (KERNEL32, USER32, GDI32, OPENGL32, SHELL32) | ✅ |
| Byte scan of `Vigil.dll` / `VigilLauncher.exe` for legacy names, API names | ✅ none |
| Engine cost (preview arena, 14 players) | ✅ ≈0.1–0.2 ms per tick |

### Not verifiable in this environment

No Lunar Client was available, so the JNI and hook paths compile but are **untested in game**.
They reuse the legacy runtime's proven techniques (JVM attach, class loader, scheduled-task pump,
SwapBuffers IAT hook, WndProc subclassing, RenderGlobal replacement), and changes there were kept
minimal. Before release, run this checklist in game:

1. Inject on the main menu and in a lobby; `vigil/logs/vigil.log` shows "Vigil ready" and
   `runtime.log` shows `sampler: resolved=1` with all signals `=1`.
2. Right Shift opens the menu with the blurred backdrop; the mouse is free; Esc closes it and
   returns mouse control; `/vigil` from chat opens it too.
3. Hovering, clicking, scrolling, typing in search fields and binding a key all work.
4. Join a Bed Wars game: the status card shows "Monitoring · BED WARS"; players appear in
   Overview and Evidence as they come into view.
5. Enable **Analyze yourself** and play normally for ten minutes: no detections should fire
   (calibration). Repeat in Duels and SkyWars.
6. A detection produces a toast, a chat line with hover evidence and a sound; Suspect Highlight
   outlines that player.
7. Switch themes, scale and motion; restart the game; settings persist in `vigil/config.json`.
8. Watch an Atlas replay: the context switches to "Replay" and checks run.
9. Press F2: screenshots still work; F11 still toggles fullscreen.

## Final roadmap

| Priority | Item |
|---|---|
| Now | Run the in-game checklist above; tune thresholds with **Analyze yourself** across game modes |
| Next | Recorded-frame capture: save a suspect's `WorldFrame`s to a file and replay them through the engine and preview (turns real sessions into regression tests and shareable evidence bundles) |
| Next | Report bundle export for Atlas reviewers (timeline + evidence as HTML/JSON) |
| Next | Ground-spoof / NoFall check (onGround claims vs. vertical motion) |
| Later | Timer / blink check (movement update cadence) |
| Later | Aim analysis v2: rotation fingerprinting and GCD analysis, calibrated on the local player |
| Later | Per-game-mode profiles (Bed Wars / Duels / SkyWars threshold sets) |
| Later | Font subsetting (−70% DLL size), slimmer team resolver, scroll-state garbage collection |
