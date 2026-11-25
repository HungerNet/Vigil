# Vigil — Transformation Plan

> Status: **phases 0–9 implemented.** This plan was written before any code changed; the
> outcome, QA results and the in-game validation checklist are in
> [`docs/IMPLEMENTATION_SUMMARY.md`](docs/IMPLEMENTATION_SUMMARY.md). Companion reports live in
> [`docs/`](docs/).

---

## 0. Executive summary

Vigil today is a Lunar Client (Minecraft 1.8.9) injected DLL that grew out of a Frenchify-era
**stats overlay**. Roughly 70% of the code exists to fetch, cache and render third-party player
statistics (Hypixel, Seraph, Aurora, Winstreak APIs), automate Bedwars chores (auto-who, auto-GL,
quick-buy macros, fast-buy click replacement) and draw cosmetic HUDs. The anticheat — the part the
product is now supposed to *be* — is one 1,500-line function with hard-coded thresholds and two
checks (KillAura and BHop) that are not merely imprecise but **logically broken** (§5).

The transformation:

1. **Delete the overlay.** All API integrations, API keys, the stats tab, denickers, Bedwars
   macros, session HUD, party detectors and the network stack (libcurl) are removed. Vigil makes
   **zero network requests** — a trust feature, not a limitation.
2. **Build a detection framework** (`src/detection/`) that is pure C++ (no JNI, no OpenGL), so it
   is unit-testable on synthetic movement traces. Every check produces *evidence* with
   human-readable reasoning and raw values, feeds a *confidence model* with decay, and records
   a *timeline* per player.
3. **Re-implement every check on that framework**, fixing KillAura/BHop from first principles and
   adding AutoClicker, Reach, Speed and Velocity.
4. **Replace the Java GUI with a native, GPU-drawn interface** (`src/ui/`) rendered inside the
   existing SwapBuffers hook: custom anti-aliased typography (embedded Inter), vector iconography,
   a spring/easing animation system, a theme system, and four core experiences — Overview,
   Checks, Evidence Explorer, Players.
5. **Rebrand completely** — Frenchify disappears from source, strings, binaries, bytecode,
   comments and build outputs; a new Vigil identity and logo are introduced.
6. **Make it verifiable** — a detection test suite and a standalone UI preview harness that
   renders the real UI code outside Minecraft and writes screenshots, so design and logic can be
   validated without a game client.

---

## 1. Product audit

### 1.1 What exists (feature inventory)

| # | Feature | Where | What it does |
|---|---------|-------|--------------|
| 1 | Anticheat: AutoBlock, NoSlow, Keep-Y (ScaffoldA), LegitScaff, FlatScaff (ScaffoldB), BHop, KillAura | `anticheat.cpp` | Per-entity heuristics, VL counters, chat flag with `/wdr` click |
| 2 | Flag sound, ignore teammates | `anticheat.cpp` | Alert sound; team-color exemption |
| 3 | Atlas / Replay mode | `dllmain.cpp` (scoreboard) | Runs checks while reviewing Hypixel Atlas/replays |
| 4 | Tab stats overlay (5 gamemodes, 18 columns, column editor) | `tab_overlay.cpp` (4.6k lines) | Replaces TAB with stats table |
| 5 | Hypixel / Seraph / Aurora / Winstreak API + Vega proxy | `player_api.cpp`, `number_denicker.cpp` | Fetches stats, blacklist tags |
| 6 | API key settings (4 keys, show/hide) | Java GUI "Settings" | Key entry |
| 7 | Skin-hash denicker + known-nicks list | `denick.cpp`, `config/known-nicks.json` | Resolves nicked players' real names |
| 8 | Number denicker (Aurora) | `number_denicker.cpp` | Resolves nicks from finals/beds chat |
| 9 | Chat overlay (stat thresholds in chat) | `tab_overlay.cpp` | Announces "dangerous" players |
| 10 | `/stats <player>` | `dllmain.cpp` | Chat stat lookup |
| 11 | Auto /who, Auto GL, "Want to bet" (George), Nick QoL | `dllmain.cpp` | Chat automation |
| 12 | Fast Buy (left click → middle click replay) | `dllmain.cpp` | Rewrites inventory clicks |
| 13 | Quickbuy hotkeys w/ turbo, Hold render distance ("Blackchify") | Java GUI, `hud_edit.cpp` | Bedwars macros |
| 14 | Session stats HUD + HUD edit mode | `session_stats.cpp`, `hud_edit.cpp` | Bedwars session counters |
| 15 | Trade indicator | `trade_indicator.cpp` | Bedwars resource trade HUD |
| 16 | Party detector (chat) + party queue detector (packet hook) | `dllmain.cpp`, `party_queue_detector.cpp` | Detects parties in lobbies |
| 17 | Bed disconnect timer, resource timers | `tab_overlay.cpp` | Bedwars timers |
| 18 | Colored hitboxes (players by team, dragons, fireballs) | `colored_hitboxes.cpp` | Cosmetic in-world boxes |
| 19 | Bow distance tracker | `utils.cpp` | Appends distance to bow-hit chat |
| 20 | Java config GUI (8 tabs) | `java_src/VigilGui4*.java` | Settings UI (MC font, `drawRect`) |
| 21 | Inject toast | `jni_helpers.cpp` | "Client injected" toast |
| 22 | Injector | `injector.cpp` | LoadLibrary injection into Lunar |
| 23 | Hypixel server whitelist | `utils.cpp` | Features only on `*.hypixel.net` |

### 1.2 Categorization

Legend — **CORE**: central to Vigil's identity; **USEFUL**: supports the vision, keep/redesign;
**LEGACY**: overlay-era, no longer fits; **REMOVE**: delete.

| Feature | Category | Fits vision? | Decision | Rationale |
|---|---|---|---|---|
| Anticheat checks | CORE | Yes — *is* the product | **Redesign** onto framework | Hard-coded, unexplainable, two broken checks |
| Chat alerts + `/wdr` click | CORE | Detection → action | **Redesign** | Keep click-to-report; add hover explanation + confidence |
| Alert sound | USEFUL | Detection awareness | Keep (setting) | |
| Ignore teammates | USEFUL | False-positive control | Keep, generalize as *exemption* | |
| Atlas / Replay mode | CORE | Investigation of reported players | **Elevate** to first-class "Replay analysis" context | Atlas reviewers are a key audience |
| Hypixel server whitelist | USEFUL | Scope/safety | Keep | Hypixel focus |
| Game-state detection (scoreboard) | USEFUL | Context for checks | **Rewrite** as `GameContext` (lobby/pregame/in-game/replay) | Current impl entangled with overlay state |
| Inject toast | USEFUL | Feedback | **Replace** with native notification system | |
| Injector | CORE (delivery) | Yes | Keep, rename `VigilLauncher.exe`, English messages | |
| Colored hitboxes | LEGACY | Cosmetic | **Redesign** into *Suspect Highlight* (boxes only around suspicious players, colored by confidence) | Reuses the proven RenderGlobal hook; becomes a visualization tool |
| Java config GUI | LEGACY | No (MC font, flat rectangles) | **Remove**, replace with native UI | §7 |
| Tab stats overlay | LEGACY | No — overlay mentality | **Remove** | 4.6k lines, network dependent |
| Hypixel/Seraph/Aurora/Winstreak API, Vega proxy | LEGACY | No | **Remove** | API keys, network, privacy, 3rd-party dependency |
| API key settings/UI/management | LEGACY | No | **Remove** | Nothing consumes keys anymore |
| Denick (skin hash) + known-nicks.json | LEGACY | Marginal (identity, not behavior) | **Remove** | Requires Mojang/Hypixel APIs; privacy-sensitive; not behavior analysis |
| Number denicker | LEGACY | No | **Remove** | Aurora API |
| Chat overlay (stat thresholds) | LEGACY | No | **Remove** | Judges by stats, not behavior |
| `/stats` | LEGACY | No | **Remove** | |
| Auto /who, Auto GL, George, Nick QoL | REMOVE | No | **Remove** | Chat automation, unrelated |
| Fast Buy click replacement | REMOVE | No — rewrites game input | **Remove** | Input automation is inappropriate for a detection product |
| Quickbuy hotkeys/turbo, Hold RD | REMOVE | No — macros | **Remove** | |
| Session stats HUD, HUD edit, trade indicator, timers | LEGACY | No | **Remove** | Bedwars cosmetics |
| Party detectors (+ packet hook) | LEGACY | No | **Remove** | Lobby intel, unrelated to behavior |
| Bow distance tracker | LEGACY | Marginal | **Remove** | Superseded by Reach evidence |

**Net result:** ~22k lines removed; a focused product of detection, analysis, evidence,
visualization and configuration.

### 1.3 New product surface

- **Overview** — live monitoring status, detection feed, watchlist (most suspicious players with
  confidence rings), session metrics.
- **Checks** — module manager: categories, per-check detail with sensitivity presets, thresholds,
  confidence behavior, indicator weights, sampling, debug.
- **Evidence Explorer** — investigation workspace: player list → timeline (per-check lanes,
  detection markers, confidence curve) → inspector (reasoning, indicator contribution, raw
  values, debug).
- **Players** — per-player analysis: session statistics, behavior metrics (observed CPS, reach
  distribution, speed profile), confidence history per check, pattern summary, cross-session
  history.
- **Settings** — alerts, contexts (where checks run), exemptions, appearance/themes, data.
- **In-game** — premium toast notifications for detections, Suspect Highlight boxes, chat alerts
  whose hover text explains the reasoning.

---

## 2. Architecture audit

### 2.1 Current architecture

```
Injector.exe ──LoadLibrary──▶ ChatReaderLunar.dll
   MainThread: attach JVM → DefineClass(VigilGui4, ClientPump…) → hooks (WndProc, SwapBuffers IAT)
   Loop (20 ms): schedule VigilClientPump on MC thread
   PumpMinecraftClientLogic (MC thread, throttled 20 ms) — 760-line god function:
       scoreboard poll → chat poll → automation → read ~30 Java static fields of the GUI via JNI
       → API keys → overlay config → write toggles file every 500 ms → anticheat every 50 ms
       → hitboxes → bow → session HUD → fast buy …
   Java GUI (VigilGui4): 2.3k lines; all settings are public static fields read by C++ via JNI
```

### 2.2 Problems

| Area | Problem | Impact |
|---|---|---|
| Separation | `dllmain.cpp` (4.1k lines) mixes bootstrap, hooks, chat parsing, automation, config IO, scoreboard parsing | Unmaintainable; every feature touches it |
| State | ~60 mutable globals across modules (`g_redState` strings as enums, etc.) | Hidden coupling, ordering bugs |
| Settings | Source of truth is Java static fields; C++ polls them by name each pump, writes JSON every 500 ms | Stringly-typed, silent failures (misspelt field = default), constant disk writes |
| Duplication | `VigilGui4.java` and `VigilGui4Lunar.java` are near-copies; `make_lunar_gui.py` patches text | Drift, fragile codegen |
| Lost sources | `VigilClientPump`, `VigilRenderGlobal`, `VigilHitboxInstaller`, `VigilTabOverlayNative`, session HUD classes exist **only** as bytecode in headers; `compile_lunar_headers.ps1` references sources that are not in the repo | Cannot rebuild from source |
| Rebrand by patching | CI runs `rename_embedded_java.py` to rewrite "Frenchify" inside compiled bytecode | Rebrand was cosmetic and build-time only |
| Detection timing | Checks sample on a **50 ms wall clock**, not on game ticks | Double/missed samples; time-based thresholds jitter |
| Detection data | Uses interpolated client positions (`posX`) for other players | Smoothed motion hides/creates patterns |
| Testability | Zero tests; all logic requires a live JVM | Every tweak is a guess |
| Network | Blocking HTTP via libcurl on worker threads, API keys in plaintext JSON | Attack surface, privacy, dependency weight |
| Language | Comments, logs and user-facing errors in French | Brand and maintainability |

### 2.3 What is retained (proven, low-level runtime)

- JVM attach + retry loop, class loader resolution (`class_finder`), JNI resolve helpers.
- `VigilClientPump` scheduling on the Minecraft thread.
- SwapBuffers IAT hook (proven render point on Lunar) and WndProc subclassing (proven input
  interception — the old Fast Buy/Enter interception validated swallowing messages).
- RenderGlobal subclass hook (proven in-world draw point).
- Team-color resolution and Hypixel host whitelist (utility code).
- Chat printing helpers (`PrintLocalChat`, `PrintJsonChat`) and alert sound.

---

## 3. New architecture proposal

### 3.1 Layers

```
┌──────────────────────────────────────────────────────────────────────────┐
│ platform/   dllmain (bootstrap), hooks (SwapBuffers, WndProc), launcher   │
├──────────────┬─────────────────────────────┬─────────────────────────────┤
│ game/  (JNI) │ detection/  (pure C++)      │ ui/  (OpenGL + Win32)       │
│  sampler     │  types, track, combat       │  gfx: renderer, font, icons │
│  game context│  evidence, confidence       │  anim: springs, tweens      │
│  chat/alerts │  check base + registry      │  theme system               │
│  world render│  engine, session, history   │  widgets, screens, toasts   │
│  commands    │  checks/*                   │  app shell + navigation     │
├──────────────┴─────────────────────────────┴─────────────────────────────┤
│ core/  log, json, paths, settings registry (typed, persisted)            │
└──────────────────────────────────────────────────────────────────────────┘
tests/       detection unit tests on synthetic traces (no JVM)
tools/preview  standalone UI harness (Win32 + WGL) with mock engine → screenshots
java/        recovered sources of the 3 retained shim classes + build script
```

**Dependency rule:** `detection/` and `core/` depend on nothing else in the project. `ui/` reads
the engine and settings through `core/` and `detection/` public APIs and never calls JNI.
`game/` is the only JNI consumer. UI actions that need the game (close screen, run `/wdr`,
play a sound) are posted to a command queue consumed by `game/` on the Minecraft thread.

**Threading:** the pump, SwapBuffers and WndProc all run on the Minecraft main thread, so engine
↔ UI access is single-threaded by construction. The only cross-thread data is the input queue
(guarded by a mutex for safety).

### 3.2 Data flow

```
MC tick ──▶ Sampler (once per new client tick; detects skipped ticks)
              reads all players: server positions (1/32 precision), head yaw, pitch, flags,
              item, swing, hurtTime, potions, contact surfaces …
         ──▶ WorldFrame ──▶ DetectionEngine.onTick
                               ├─ PlayerTrack update (history ring buffer, derived kinematics,
                               │   swing/hurt edge events, air/ground segments)
                               ├─ CombatResolver (hit attribution: who hit whom)
                               ├─ Exemptions (knockback, teleport, riding, liquids, surfaces…)
                               ├─ Checks (each with its own per-player state)
                               │     └─ emit Evidence{indicator, weight, values, reasoning}
                               ├─ ConfidenceModel per (player, check): accumulate, decay, gate
                               ├─ Detection when gated threshold crossed (with supporting evidence)
                               └─ Session timeline + confidence history + alerts queue
UI (SwapBuffers) ──▶ reads engine/session ──▶ renders menu / toasts
game/alerts ──▶ consumes alerts queue → chat (hover = reasoning), sound, toasts
```

### 3.3 Settings registry

Every configurable value is declared once with metadata:

```cpp
s.addFloat("checks.reach.threshold", "Reach threshold", 3.30f, {2.9f, 4.5f, 0.05f, "blocks"},
           "Distance from the attacker's eyes to the target's hitbox beyond which a hit counts as evidence.");
```

The registry is the single source of truth for: defaults, ranges, units, tooltips, advanced
flags, quality warnings ("Disabling this reduces detection quality"), JSON persistence
(debounced), "modified from default" state, and UI generation. Checks declare their settings in
code; the Checks screen renders them automatically, so a new check gets a complete settings UI
with zero UI code.

---

## 4. Detection framework design

Full design: [`docs/DETECTION_FRAMEWORK.md`](docs/DETECTION_FRAMEWORK.md). Summary:

### 4.1 Core concepts

| Concept | Description |
|---|---|
| `PlayerFrame` | One tick of one player: server position, rotation (head yaw/pitch), flags (ground, sprint, sneak, use, block, invisible, riding), item, swing state, hurt time, potions, contact surfaces |
| `PlayerTrack` | Ring buffer (~10 s) of frames + derived signals: velocity estimates over robust windows, air/ground segments (takeoff, apex, landing, airtime), swing starts, hurt starts |
| `CombatEvent` | Attributed hit: attacker, victim, tick, attacker eye position, victim hitbox history, aim error, attribution certainty |
| `Indicator` | A named sub-signal of a check (e.g. KillAura → *Multi-target*), with configurable enable + weight |
| `Evidence` | `{tick, time, check, indicator, weight, severity, values[], reasoning}` — the explanation is generated at the moment of observation from real numbers |
| `ConfidenceModel` | Per (player, check): evidence accumulates into a score; exponential decay with configurable half-life; *evidence gate* requires N evidence items from ≥M distinct indicators before a detection can fire; alert threshold and cooldown |
| `Detection` | Confidence crossed the threshold with the gate satisfied; references the evidence that caused it |
| `Exemption` | Shared false-positive reducers computed once per player-tick (recent knockback, teleport, riding, liquid/ladder/web, slippery/bouncy surfaces, potion effects, spectator/invisible, stale data) |
| `Sampling` | Optional per-check *analyze every Nth tick* / *only players within R blocks*; off by default where it would reduce quality, and the UI says so |
| `Debug` | Per-check debug toggle records indicator inputs each evaluation into a bounded debug log visible in the Evidence Explorer and `vigil/logs/detections.log` |

### 4.2 Confidence math

```
score += weight × severity × (1 − score/100)^k        (diminishing returns; k configurable)
score *= 0.5 ^ (Δt / halfLife)                         (decay; half-life per check)
detection ⇔ score ≥ alertThreshold ∧ evidence ≥ minEvidence ∧ distinctIndicators ≥ minIndicators
```

This prevents a single outlier from producing a flag, makes sustained behavior dominate, and
lets confidence fall back naturally when behavior normalizes. All parameters are per-check
settings with presets (Relaxed / Balanced / Strict).

### 4.3 Client-side realities (Hypixel focus) the framework encodes

- Other players' positions arrive at **1/32 block** precision; per-tick velocity has ±0.03 error →
  velocities are estimated over multi-tick windows and thresholds carry quantization tolerance.
- Rotations arrive at **360/256 = 1.40625°** steps; aim checks never demand sub-quantum precision.
- Position packets can bunch (0 or 2 per tick) → segment-based analysis (air time, displacement
  over windows) instead of single-tick deltas.
- Arm swings are relayed by the server only past half of the previous swing (≈ every 3 ticks) →
  AutoClicker measures timing structure, not raw CPS, and says so in the UI.
- `onGround` is the suspect's own claim relayed by the server → movement checks cross-check it
  against vertical motion.

---

## 5. Detection audit (existing checks)

| Check | Verdict | Findings |
|---|---|---|
| **KillAura** | **Broken — rewrite** | (1) Computes `yawDelta` between **the local player's** yaw and the direction from the local player to the suspect — it measures whether *you* are looking at *them*, not whether they aim suspiciously. (2) "Swing" is detected as `swingProgress != lastSwingProgress`, which fires on **every animation tick** (~5× per swing), so "rapid swing (<260 ms)" is always true. (3) Only works within 3.2 blocks of the local player. Net effect: anyone swinging near you while you face them accrues VL. |
| **BHop** | **Flawed — rewrite** | Samples 50 ms wall-clock (not ticks); "jump started" needs a ≥0.15 Y step between interpolated samples; "fast cycle" = jumps < 350 ms apart; no handling of stairs/slabs, ceilings, jump boost, knockback, ice, slime. Pattern is close to normal sprint-jumping on uneven terrain. |
| **AutoBlock** | Keep logic, refine | Correct core idea (vanilla 1.8 cannot swing while blocking). Uses "swing in progress" instead of a *new* swing start, so a hit followed by block within 300 ms can count. Redesign: count swing **starts** during a block held ≥ N ticks. |
| **NoSlow** | Keep logic, refine | Sprint flag + item use with 700 ms grace. Add the stronger signal: horizontal speed while using an item vs. the vanilla item-use limit, and require sustained overlap in ticks. |
| **Keep-Y (ScaffoldA)** | Keep, re-tune | Same swing-edge bug inflates "placed" counts ~5×; thresholds were tuned against the bug. Ported with real swing starts and re-tuned defaults. |
| **LegitScaff** | Keep | Sneak-cadence heuristic; ported as-is with settings exposed. |
| **FlatScaff (ScaffoldB)** | Keep | Ported; thresholds exposed. |

New checks: **AutoClicker, Reach, Speed, Velocity**. Redesign details:
[`docs/DETECTION_REDESIGN.md`](docs/DETECTION_REDESIGN.md).

### 5.1 New/redesigned check designs (summary)

- **KillAura** — indicators: *Hit outside view* (angle between look vector and target hitbox at
  attributed hit), *Multi-target* (distinct victims at wide angular separation within a short
  window), *Snap-to-target* (large rotation in the tick before a hit ending on the target),
  *Robotic tracking* (sustained near-zero aim error while the target moves fast, quantization-
  aware).
- **Reach** — distance from attacker eye to victim hitbox (+0.1 border) at attributed melee hits,
  latency-compensated by taking the minimum over recent victim positions; only unambiguous
  attributions count.
- **AutoClicker** — swing-start interval statistics: tick-mode dominance, coefficient of
  variation, identical-interval run length; rate-ceiling indicator if the server relays more
  than vanilla allows.
- **Velocity (Anti-KB)** — after an attributed melee hit on a grounded victim, compare vertical
  rise and horizontal displacement over the following ticks with vanilla knockback; exempt
  ceilings, liquids, webs, ladders.
- **Speed** — windowed horizontal speed vs. a vanilla envelope adjusted for sprint, Speed
  potion amplifier and surface (ice/slime/soul sand); knockback/teleport exempt.
- **BHop** — air-segment physics: impossible short air time on flat landings, horizontal speed
  across consecutive hops above vanilla, and *air strafe* (mid-air direction change with speed
  retention exceeding vanilla air control of 0.026 b/t²).

---

## 6. Technical debt audit

| Debt | Severity | Resolution |
|---|---|---|
| God function `PumpMinecraftClientLogic` (760 lines) | High | Replaced by `game::Runtime::tick` orchestrating small modules |
| `dllmain.cpp` 4.1k lines | High | Split into `platform/` + `game/` modules (~300 lines bootstrap) |
| `RunAnticheatChecks` 1.0k-line single function | High | Framework + one file per check |
| Settings via Java static fields + JNI polling | High | Native settings registry |
| Missing Java sources for embedded bytecode | High | Sources recovered (javap) into `java/`, reproducible build script |
| Duplicate Java GUI sources + text patch codegen | Medium | Deleted (native UI) |
| Build-time bytecode string rewriting | Medium | Deleted (no Frenchify left to rewrite) |
| libcurl FetchContent (download + build on every CI run) | Medium | Removed with the network features |
| Stringly-typed state (`g_redState == "Ingame"`) | Medium | `enum class GameContext` |
| French comments/logs/errors | Medium | English throughout |
| Silent `catch (...)` around checks | Medium | Engine isolates check failures and reports them in the UI ("Check suspended") |
| Logs written unbounded to DLL folder | Low | `vigil/logs/` with size cap/rotation |
| No tests | High | `tests/detection_tests.cpp` + CTest |
| Output names `ChatReaderLunar.dll`, `InjectorLunar.exe` | Low | `Vigil.dll`, `VigilLauncher.exe` |

Full report: [`docs/TECH_DEBT.md`](docs/TECH_DEBT.md).

---

## 7. UI/UX audit and redesign proposal

### 7.1 Audit of the current UI

- Minecraft bitmap font (`FontRenderer`) and `Gui.drawRect` flat rectangles only — no AA, no
  radii, no shadows, no motion.
- 8 tabs named after implementation history ("Blackchify", "Experimental"), settings grouped by
  module origin rather than user intent.
- Hard-coded pixel layout constants (`UTILS_FIREBALL_COLOR_Y = 218`), manual scroll math per tab.
- No explanation of any setting; anticheat is a column of on/off toggles with no thresholds.
- No way to see *why* a player was flagged.

### 7.2 Design principles

1. **The investigation is the interface.** Evidence and reasoning are first-class, not logs.
2. **Calm by default, loud when it matters.** Quiet surfaces; color is reserved for meaning
   (confidence, severity, focus).
3. **Every control explains itself.** Info affordance on every setting; defaults shown
   separately as a marker, never inside the tooltip.
4. **Motion with purpose.** Each interaction class has its own motion signature (§7.5).
5. **Original.** Workflow lessons from Vape V4 / Prestige (fast category → module → settings
   drill-down, searchable modules, inline toggles) — visual language entirely Vigil's own.

### 7.3 Workflow lessons (not visuals)

| Inspiration | Strength | Vigil adaptation |
|---|---|---|
| Vape V4 | Category → module list → inline settings; keyboard-first; search | Checks screen: category rail + searchable list + detail pane; `/` focuses search |
| Prestige | Clean grouping, discoverable advanced options | Sections with progressive disclosure ("Advanced" expanders), presets before knobs |

### 7.4 Information architecture

```
┌ Nav rail ┬ Top bar: logo · context pill (In game · Bed Wars) · session timer · search ┐
│ Overview │                                                                           │
│ Checks   │  Content (animated page transitions)                                      │
│ Evidence │                                                                           │
│ Players  │                                                                           │
│ ──────── │                                                                           │
│ Settings │                                                                           │
└──────────┴───────────────────────────────────────────────────────────────────────────┘
```

### 7.5 Animation system

- Primitives: critically-damped and under-damped **springs** (position/scale/opacity), **tweens**
  with curated curves (`standard`, `emphasized`, `decelerate`, `exit`), **staggered** entrance
  for lists, value smoothing for numbers and charts.
- Distinct signatures:
  - *Navigation* — content cross-fades with an 8 px directional slide; the nav indicator glides
    on a spring.
  - *Open/close* — window scales 0.96 → 1 with emphasized easing while the backdrop blur fades in.
  - *Toggle* — knob travels on an under-damped spring (slight overshoot), track color blends.
  - *Hover* — 120 ms tint/elevation lift; no movement (quiet).
  - *Detection arrival* — feed item slides in, a confidence ring sweeps to its value, a single
    soft pulse on the nav "Evidence" badge.
  - *Configuration change* — slider value bubble springs; "modified" dot fades in; reset button
    slides out.
  - *Tooltips* — 350 ms intent delay, 140 ms fade + 4 px rise.
- Global "Motion" setting: Full / Reduced / Off.

### 7.6 Theme system

Themes are complete design specifications, not palettes: surface ladder, elevation strategy
(shadow vs. hairline), corner radius scale, backdrop dim/blur, accent + accent glow, severity
scale, chart palette and typography contrast. Shipped themes:

| Theme | Character |
|---|---|
| **Midnight** (default) | Ink graphite, iris accent, soft shadows, frosted backdrop |
| **Vesper** | Warm espresso dark, candle-gold accent — the literal vigil light |
| **Obsidian** | True black, hairline borders, no shadows, mint accent; maximum contrast |
| **Glacier** | Premium light theme, cool paper tones, deep-iris accent |
| **Aurora** | Deep blue-violet gradient surfaces, cyan accent, stronger glow |

User customization: accent override, UI scale, backdrop blur strength, motion level — all
within the theme's constraints so quality is preserved.

### 7.7 Iconography

A custom stroke icon set drawn as vector geometry (1.5 px strokes on a 20 px grid, round caps)
so icons are crisp at any UI scale and consistent: overview, shield-less checks (a target
reticle), evidence (layers), players, settings, combat, movement, world, search, info, reset,
chevrons, clock, sparkline, warning, check, close, pin, export, filter. No emoji anywhere.

### 7.8 Typography

Embedded **Inter** (SIL OFL) rendered through a GDI glyph atlas with grayscale anti-aliasing at
exact pixel sizes per UI scale; Consolas for raw values. Type scale: 11 / 12 / 13 / 15 / 18 /
24 / 32.

Full plan: [`docs/UI_UX.md`](docs/UI_UX.md).

---

## 8. Branding audit and proposal

### 8.1 Audit

- "Frenchify" appears in chat prefixes as a tri-color (§1 blue / §f white / §c red — the French
  flag), in toasts, flag JSON, Java UI ("Blackchify" tab), CI artifact names (`ChatReaderLunar`),
  and as patched bytecode strings. Logs and errors are French.
- No logo, no icon, no defined colors, no voice.

### 8.2 Identity

- **Name meaning:** a *vigil* is a period of staying awake to keep watch. Vigil watches so the
  player does not have to.
- **Keywords:** vigilance, observation, awareness, investigation, intelligence, precision,
  confidence, trust.
- **Voice:** precise, calm, evidential. Vigil never accuses; it *reports what it observed*.
  ("Hit at 3.62 blocks — 0.62 beyond vanilla reach" not "HACKER DETECTED").
- **Color:** Midnight ink `#0E1014`, Iris `#8FA8FF` (brand accent), Vigil Gold `#E2B866`
  (secondary/brand warmth), severity scale Clear `#5CC8B2` → Notice `#E8B35A` → Alert `#F0766A`
  → Critical `#E5484D`.
- **Chat signature:** `§8[§9Vigil§8]` — restrained, legible, no flag colors.

### 8.3 Logo concept

A geometric **V** whose outer contour is strictly straight (precision) and whose inner notch is a
gentle arc — the lower lid of an attentive eye — cradling a single point of light (the focus
point / the vigil candle). Primary read: **V**. Secondary, quiet: observation. No shields,
swords, mascots or literal eyes. Works as a solid mark at 16 px (dot merges cleanly), as an
outline, and with the logotype "VIGIL" in widely tracked Inter SemiBold.

Deliverables: `assets/brand/vigil-mark.svg`, `vigil-mark-mono.svg`, `vigil-lockup.svg`,
`vigil-app-icon.svg`, the same geometry drawn natively in the UI, and
[`docs/BRAND.md`](docs/BRAND.md).

---

## 9. Refactor strategy

1. **Strangler, not patch.** New modules are written beside the old ones; once a capability is
   reproduced, the old module is deleted outright (no dead flags).
2. **Bottom-up:** core → detection (with tests) → game adapter → UI → shell → delete legacy.
3. **Keep the proven runtime path untouched** (attach, class loader, pump scheduling, IAT hook,
   WndProc subclass, RenderGlobal hook) except for renames/translation and the removal of
   overlay calls. These cannot be integration-tested outside a live Lunar client, so they change
   minimally.
4. **Everything new that can be tested off-game is tested off-game** (detection tests, UI
   preview harness).

## 10. Migration strategy

- **Settings:** the old `vigil_toggles.json` keys that still have meaning (`anticheat`,
  `ac_*`, `ignoreTeammates`, `ac_flagsound`) are imported once into the new `vigil/config.json`;
  everything else (API keys included) is discarded. API keys are **not** migrated or retained.
- **Binary names:** `ChatReaderLunar.dll` → `Vigil.dll`, `InjectorLunar.exe` →
  `VigilLauncher.exe`; release zip `Vigil-<version>-win-x64.zip` contains only the launcher,
  DLL and README.
- **Data directory:** `<dll dir>/vigil/` — `config.json`, `history/players.json`,
  `sessions/*.jsonl`, `logs/*.log`.
- **Version:** bumped to `2.0.0` (breaking: product redefinition).

## 11. Risk assessment

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Native UI rendering conflicts with Lunar's GL state (shaders, FBOs, texture units) | Medium | High | Full `glPushAttrib`/matrix save; explicitly save/restore program, framebuffer binding, active texture unit; same render point the old toast used successfully |
| Input interception differs between LWJGL2 / GLFW backends | Medium | High | WndProc swallowing is already proven on this client (Fast Buy / Enter); menu also opens via `/vigil` command as fallback; menu hides if any other screen replaces ours |
| Field names (e.g. `serverPosX`, `otherPlayerMPYaw`) unavailable | Low–Medium | Medium | Sampler resolves each field independently with fallbacks (interpolated pos, body yaw); checks declare required signals and auto-suspend with a visible reason |
| False positives from new checks | Medium | High | Conservative defaults, evidence gates, exemptions, self-calibration mode (analyze yourself), synthetic tests of legit traces |
| AutoClicker limited by swing relay rate | High (by nature) | Medium | Honest UI copy; default weight/thresholds conservative |
| No live client in the build environment | Certain | Medium | Off-game tests + preview harness + full MSVC/CMake build; runtime-path changes minimized |
| Removing features users liked (stats overlay) | Medium | Low | Documented in removal report; product direction is explicit |
| Embedded font licensing | Low | Low | Inter is SIL OFL 1.1; license shipped in `assets/fonts/` |

## 12. Ordered implementation roadmap

| Phase | Deliverable | Verification | Status |
|---|---|---|---|
| 0 | PLAN.md (this) | — | ✅ |
| 1 | `core/`: log, json, settings registry | compiled into tests | ✅ |
| 2 | `detection/`: types, track, combat, evidence, confidence, check base, engine, session, history | unit tests | ✅ |
| 3 | Checks: KillAura, Reach, AutoClicker, Velocity, Speed, BHop, NoSlow, AutoBlock, Scaffold (Keep-Y, Flat, Legit) | synthetic legit vs. cheat traces | ✅ 16 scenarios pass |
| 4 | `game/`: sampler, block probes, game context, alerts/chat, commands, suspect highlight, runtime orchestration | full DLL build | ✅ builds; in-game checklist pending |
| 5 | `ui/`: GL layer, renderer, font atlas, icons, animation, themes, widgets | preview harness screenshots | ✅ |
| 6 | Screens: Overview, Checks, Evidence Explorer, Players, Settings, toasts | preview screenshots per screen & theme | ✅ |
| 7 | Platform: new bootstrap, hooks wiring, menu open/close, launcher; delete legacy modules, Java GUI, scripts, curl | full build, grep for legacy refs | ✅ |
| 8 | Brand assets (logo SVGs, app icon), docs, CI workflow | grep audit (zero Frenchify) | ✅ |
| 9 | QA pass: build, tests, screenshots review, persistence round-trip, performance budget | reports in `docs/` | ✅ (see implementation summary) |

### 12.1 Post-2.0 roadmap (future)

- Ground-spoof / NoFall check (onGround claim vs. vertical motion).
- Timer / blink check (movement update cadence anomalies).
- Aim analysis v2 (per-player rotation fingerprinting, GCD of rotation deltas on local-player
  calibration).
- Replay export: save a suspect's tracked frames to a file and re-run checks offline in the
  preview harness (the framework already supports offline frames).
- Report bundle export (evidence timeline as shareable HTML/JSON for Atlas reviewers).
- Per-gamemode profiles (Bed Wars vs. Duels vs. SkyWars threshold sets).
