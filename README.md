<p align="center"><img src="assets/brand/vigil-app-icon.png" width="96" alt="Vigil"></p>

<h1 align="center">Vigil</h1>
<p align="center">Cheat detection and player analysis for Hypixel on Lunar Client 1.8.9</p>

![Evidence Explorer](docs/images/evidence.png)

Vigil observes the players around you, measures their behavior against what vanilla Minecraft
allows, and reports only what it can explain. Every detection is backed by recorded evidence:
the measured values, the limits they were compared against and the reasoning that connects them.

## Use

1. Download **`Vigil.exe`** from the latest release.
2. Run it. Vigil finds Lunar Client (start Minecraft 1.8.9 first, or it will wait for you),
   starts itself inside the game and confirms when it is active.
3. In game, press **Right Shift** (rebindable) or type `/vigil`.

That's all — there are no other files to keep track of. Vigil stores its settings and data in
`%LOCALAPPDATA%\Vigil` (Settings → Data opens it).

| In game | |
|---|---|
| Right Shift | Open Vigil. While a detection notification is on screen, opens that player. |
| `/vigil <player>` | Investigate a player (searches if they weren't observed this session) |
| `/vigil search <text>` | Search players, checks, settings and evidence |
| `/vigil status` · `/vigil reset` | Open Status · start a new session |
| Ctrl K or `/` (in Vigil) | Search everything |

## What's inside

- **Eleven checks on one framework:** KillAura, Reach, AutoClicker, Velocity, AutoBlock, Speed,
  BHop, NoSlow, Scaffold (Keep-Y), Scaffold (Flat) and LegitScaff, each with independent
  indicators, a confidence model with decay and an evidence gate. Movement limits follow each
  player's real movement modifiers.
- **Player profiles:** confidence over the session, what drives it and how it evolved; the
  evidence timeline with an inspector that explains every observation numerically; behavior;
  identity.
- **Denickers:** recognises nicked players, identifies the account behind a nick that kept its
  own skin, and (with your own Bordic key) matches Bed Wars final/bed counters.
- **Utilities:** Auto Who, Fast Buy, Party Detector and Party Queue Detector — off until you turn
  them on.
- **Status:** a live view of every part of Vigil, from injection to signals.
- **Yours:** nine themes plus a theme editor, rebindable shortcuts, motion settings.

## Privacy

No analytics, telemetry, usage tracking or automatic reports. The network is used only by the
denickers you enable, only for nicked players, and only with Mojang's public profile service and
(with your key) Bordic. Everything else stays on your computer.

## Antivirus

Vigil loads itself into the game process — the same technique every in-game mod loader uses — so
some antivirus products may warn about it. Vigil does not hide or obfuscate anything: it writes
one file, `%LOCALAPPDATA%\Vigil\runtime\Vigil-<version>.dll`, never asks for administrator
rights, and each release publishes the SHA-256 of `Vigil.exe` so you can verify your download.
See [docs/TRUST_AUDIT.md](docs/TRUST_AUDIT.md).

## Build

Requirements: Visual Studio 2022 Build Tools (x64), CMake ≥ 3.20, and a JDK for the JNI headers.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release           # detection + utility/identity tests
build\Release\VigilPreview.exe              # UI + engine with a simulated arena
build\Release\VigilPreview.exe --shot out.png --page players --player Kestrel --tab evidence --theme 7
```

`build\Release\Vigil.exe` is the distributable; it embeds `Vigil.dll`. The version comes from
`version.yaml`. The Java shims (`src/game/bytecode/`) are generated from `java/src` with
`python tools/build_java.py`; brand assets with `python tools/make_brand_assets.py`.

## Layout

```
src/core/        logging, JSON, paths, typed settings registry
src/detection/   detection framework and checks (pure C++, unit-tested)
src/utilities/   Auto Who, Fast Buy, party detectors (pure logic, unit-tested)
src/identity/    nick detection and denickers
src/net/         HTTPS client (WinHTTP) for the identity tools
src/game/        Minecraft adapter over JNI: sampler, context, chat, utilities, alerts, highlight
src/ui/          native OpenGL interface: renderer, fonts, icons, motion, themes, widgets, pages
src/platform/    DLL bootstrap, render/input hooks, Vigil.exe launcher
tests/           detection scenarios and feature tests
tools/           UI preview harness, Java and brand asset builders
docs/            design, audits and reports
```

## Documentation

[Plan](PLAN.md) · [Polish report](docs/POLISH_REPORT.md) · [Trust audit](docs/TRUST_AUDIT.md) ·
[Detection framework](docs/DETECTION_FRAMEWORK.md) · [Detection redesign](docs/DETECTION_REDESIGN.md) ·
[UI/UX](docs/UI_UX.md) · [Brand](docs/BRAND.md) · [2.0 plan](docs/history/PLAN-2.0.md) ·
[2.0 implementation summary](docs/IMPLEMENTATION_SUMMARY.md)

Inter typeface © The Inter Project Authors, SIL Open Font License 1.1 (`assets/fonts/LICENSE-Inter.txt`).
