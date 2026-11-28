<p align="center"><img src="assets/brand/vigil-app-icon.png" width="96" alt="Vigil"></p>

<h1 align="center">Vigil</h1>
<p align="center">Cheat detection and player analysis for Hypixel on Lunar Client 1.8.9</p>

![Evidence Explorer](docs/images/evidence.png)

Vigil observes the players around you, measures their behavior against what vanilla Minecraft
allows, and reports only what it can explain. Every detection is backed by recorded evidence:
the measured values, the limits they were compared against and the reasoning that connects them.

## Use

1. Download **`Vigil-<version>.exe`** (for example `Vigil-1.0-dev.9.exe`) from the latest
   release. `SHA256SUMS` next to it lets you verify the download.
2. Run it. Vigil finds Lunar Client (start Minecraft 1.8.9 first, or it will wait for you),
   starts itself inside the game and confirms when it is active. If an older Vigil is already
   running in the game, it asks you to restart Lunar Client instead of loading a second one.
3. In game, press **Right Shift** (rebindable) or type `/vigil`.

That's all — there are no other files to keep track of. Vigil stores its settings and data in
`%LOCALAPPDATA%\Vigil` (Settings → Data opens it).

| In game | |
|---|---|
| Right Shift | Open Vigil. While a detection notification is on screen, opens that player. |
| `/vigil <player>` | Investigate a player (searches if they weren't observed this session) |
| `/vigil search <text>` | Search players, checks, settings and evidence |
| `/vigil status` · `/vigil records` · `/vigil reset` | Open Status · open Records · start a new session |
| Ctrl K or `/` (in Vigil) | Search everything |
| Tab · Enter · arrows (in Vigil) | Move between controls · activate · adjust sliders, choices and lists |

## What's inside

- **Eleven checks on one framework:** KillAura, Reach, AutoClicker, Velocity, AutoBlock, Speed,
  BHop, NoSlow, Scaffold (Keep-Y), Scaffold (Flat) and LegitScaff, each with independent
  indicators, a confidence model with decay and an evidence gate. Movement limits follow each
  player's real movement modifiers; measurements account for latency, lag catch-ups and server
  stalls.
- **Focused on your game:** Vigil knows lobbies, queues, matches and post-game. The roster shows
  the current game first; NPCs, bots and duplicate entities never appear or get analyzed. A short
  match briefing names the players worth knowing about.
- **Confidence you can read at a glance:** every player wears a halo — a ring that fills and grows
  heavier with Vigil's confidence — in the roster, profiles, notifications and the world.
- **Your conclusions, kept separate (Records):** confirm, watch or exempt anyone — a player in
  your game or any username. Search and review confirmations with reasons and notes; exempt friends
  so Vigil never judges them. Vigil never does any of this on its own, and your records never
  change its confidence.
- **Player profiles:** confidence over the session, what drives it and how it evolved; the
  evidence timeline with an inspector that shows, for every observation, what vanilla play would
  have produced, what was observed and why the difference is suspicious; behavior; identity.
  Save a profile, export it to share, import one you received.
- **Utilities:** Auto Who, Fast Buy, Party Detector, Party Queue Detector and the identity tools
  (skin and number denickers) — off until you turn them on.
- **Status:** a live view of every part of Vigil, plus a diagnostics report and trace recording for
  bug reports.
- **Yours:** nine themes plus a theme editor, rebindable shortcuts, full keyboard navigation,
  higher-contrast text, motion settings and optional quiet interface sounds.

## Privacy

No analytics, telemetry, usage tracking or automatic reports. The network is used only by the
denickers you enable, only for nicked players, and only with Mojang's public profile service and
(with your key) Bordic. Everything else stays on your computer.

## Antivirus

Vigil loads itself into the game process — the same technique every in-game mod loader uses — so
some antivirus products may warn about it. Vigil does not hide or obfuscate anything: it writes
one file, `%LOCALAPPDATA%\Vigil\runtime\Vigil-<version>.dll`, never asks for administrator
rights, and each release publishes `SHA256SUMS` so you can verify your download
(`Get-FileHash Vigil-<version>.exe`). `Vigil-<version>.exe --self-check report.json` checks that
the file and the runtime inside it belong together, without starting anything.
See [docs/TRUST_AUDIT.md](docs/TRUST_AUDIT.md).

## Build

Requirements: Visual Studio 2022 Build Tools (x64), CMake ≥ 3.20, and a JDK for the JNI headers.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release           # five suites + format fixtures
build\Release\VigilPreview.exe              # UI + engine with a simulated arena
build\Release\VigilPreview.exe --shot out.png --page players --player Kestrel --tab evidence --theme 7
build\Release\VigilReplay.exe tests\traces\golden-arena.vtrace --expect tests\traces\golden-arena.expect.json
build\Release\Vigil.exe --self-check report.json   # verifies the exe and its embedded runtime; starts nothing
build\Release\VigilParity.exe parity.txt           # full engine state over the scenario corpus (compare two builds)

# Rust (rust/, not part of the shipped binaries yet; toolchain pinned in rust/rust-toolchain.toml)
cd rust; cargo fmt --check; cargo clippy --all-targets -- -D warnings; cargo test --release; cd ..
python tools\ci\cross_formats.py --cpp build\Release\VigilFormats.exe --rust rust\target\release\vigil-fmt.exe --fixtures tests\fixtures --work build\round-trips
```

`-DVIGIL_BUILD_RUST=ON` makes `cmake --build` build the Rust workspace and adds the Rust tests and
the C++ ↔ Rust format round trips to CTest.

`build\Release\Vigil.exe` is the distributable (published as `Vigil-<version>.exe`); it embeds
`Vigil.dll`. The version comes from `version.yaml`. The Java shims (`src/game/bytecode/`) are
generated from `java/src` with `python tools/build_java.py`; brand assets with
`python tools/make_brand_assets.py`. CI and releases: [docs/RELEASE_PROCESS.md](docs/RELEASE_PROCESS.md).

## Layout

```
src/core/        logging, JSON, paths, typed settings registry                  (vigil_core)
src/detection/   engine, checks, engine configuration, records formats, traces   (vigil_detection)
src/utilities/   Auto Who, Fast Buy, party detectors (pure logic)                (vigil_services)
src/identity/    nick detection and denickers; src/net/ HTTPS for them           (vigil_services)
src/ui/          native OpenGL interface: renderer, fonts, icons, themes, widgets, pages, records
src/game/        Minecraft adapter over JNI: sampler, context, chat, utilities, alerts, highlight
src/platform/    DLL bootstrap, render/input hooks, runtime version guard, Vigil.exe launcher
tests/           five suites, shared framework, simulator, scenario corpus and generator,
                 golden trace, fixtures shared with Rust (tests/fixtures)
tools/           preview harness, trace replay, parity probe, format fixtures, CI checks,
                 Java and brand asset builders
rust/            Cargo workspace: vigil-formats (every file format, byte-compatible) and
                 vigil-detection (the engine and the rest of src/detection — marks, profiles,
                 history, journal, log channel — proven identical to C++; every C++ engine test
                 mirrored by name; replay-check, vigil-parity)
docs/            architecture, validation, release process, audits and reports
```

## Documentation

**Current:** [Plan](PLAN.md) · [Final report](docs/FINAL_REPORT.md) ·
[Architecture](docs/ARCHITECTURE.md) · [Validation](docs/VALIDATION.md) ·
[Live validation](docs/LIVE_VALIDATION.md) · [Release process](docs/RELEASE_PROCESS.md) ·
[Rust plan](RUST_PLAN.md) ([P1](P1_PLAN.md), [P2](P2_PLAN.md), [report](docs/RUST_P1_P2_REPORT.md);
[P3](P3_PLAN.md), [P3A](P3A_PLAN.md), [report](docs/RUST_P3A_REPORT.md);
[P3B](P3B_PLAN.md), [report](docs/RUST_P3B_REPORT.md)) ·
[Tech debt](docs/TECH_DEBT.md)

**Design and earlier passes:** [Quality report](docs/QUALITY_REPORT.md) ·
[Polish report](docs/POLISH_REPORT.md) · [Trust audit](docs/TRUST_AUDIT.md) ·
[Detection framework](docs/DETECTION_FRAMEWORK.md) · [Detection redesign](docs/DETECTION_REDESIGN.md) ·
[UI/UX](docs/UI_UX.md) · [Brand](docs/BRAND.md) · [Plans](docs/history/) ·
[2.0 implementation summary](docs/IMPLEMENTATION_SUMMARY.md)

Inter typeface © The Inter Project Authors, SIL Open Font License 1.1 (`assets/fonts/LICENSE-Inter.txt`).
