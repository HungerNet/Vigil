# Vigil — Polish & Maturity Plan

> Archived (1.0-dev.7). Written before any code changed in that pass. The 2.0 transformation plan
> is preserved in [`PLAN-2.0.md`](PLAN-2.0.md). Outcomes are recorded in
> [`docs/POLISH_REPORT.md`](../POLISH_REPORT.md) and [`docs/TRUST_AUDIT.md`](../TRUST_AUDIT.md).

The 2.0 overhaul gave Vigil its identity: a cheat-detection client and a player-analysis
platform. This pass does not reinvent that. It makes Vigil feel finished: restores what was lost
along the way, removes friction, and gives the product the confidence, discoverability and
finish of a mature tool.

---

## 1. Current state audit

**Method.** Every source file in `src/`, the preview harness, tests, CI and docs were read. The
legacy implementation (commit `1d2a4b5`) was extracted and read for everything the overhaul
removed. All four targets were built with MSVC 19.44 (0 warnings), the 94 detection expectations
pass, and every page was rendered in the preview harness and reviewed as screenshots. The two
post-overhaul commits ("Fix GUI bug") show the menu-open path was debugged in a live client, so
the hook and screen-host paths are now known to work in game.

### 1.1 What works well and stays

| Area | Why it stays |
|---|---|
| Layering (`core` → `detection` → `game`/`ui` → `platform`) | Clean dependency rule; detection and UI are testable outside the game |
| Detection framework (tracks, attribution, exemptions, evidence, confidence, gate) | Sound design; every detection is explainable from recorded numbers |
| Typed settings registry | Single source of truth for defaults, ranges, tooltips, persistence and UI |
| Native renderer, Inter typography, icon set | High-quality foundation; anti-aliased, resolution independent |
| Evidence timeline + inspector | The strongest investigation surface in the product |
| Preview harness and synthetic tests | Make UI and detection work verifiable without a client |
| Privacy posture (no telemetry, no analytics, no tracking) | Core to trust; network use stays limited to explicit identity tools |
| Proven runtime path (JVM attach, pump, SwapBuffers IAT hook, WndProc subclass, menu host screen) | Validated in game; changed only where a feature requires it |

### 1.2 Inventory after 2.0

Overview · Checks · Evidence · Players · Settings pages; corner detection toasts; chat alerts with
hover evidence; Suspect Highlight; `/vigil`, `/vigil <player>`, `/vigil reset`; `VigilLauncher.exe`
+ `Vigil.dll` distributed as a zip.

---

## 2. Regression audit

The overhaul removed ~38k lines. Most removals were correct (stats overlay, API stat fetching,
Bed Wars macros). Some removed things users rely on or made workflows slower.

| # | Regression | Impact | Decision |
|---|---|---|---|
| R1 | **Auto Who, Fast Buy, Party Detector, Party Queue Detector removed** | Wanted features gone | **Restore** as a Utilities section (legacy logic reused, re-hosted on the new runtime) |
| R2 | **Denickers removed** (skin denicker, number denicker, known-nick-skin list) | Investigations can't see through nicks | **Restore** as identity tools inside player profiles |
| R3 | Chat reading pipeline removed | Nothing can react to chat (needed by R1/R2) | **Restore** as a small `ChatReader` |
| R4 | Per-feature hotkeys removed (legacy `hkAutoWho`, `hkFastBuy`, …) | Slower toggling | **Restore** inside a unified keybind system |
| R5 | In-game "injected" toast replaced by a launcher `MessageBox` that only says the DLL loaded | No confirmation that Vigil actually started; OS popup | **Replace** with an in-game activation card and a launcher that waits for the real ready signal |
| R6 | Evidence and Players are two pages over the same players, each with its own list, header and per-check confidence | Duplicate navigation, lost context when switching | **Merge** into one player profile with tabs |
| R7 | Live activity lists every evidence item (one cheater fills the feed) | Signal drowned in repetition | **Group** by player and check |
| R8 | Only checks are searchable | Settings, players, evidence hard to find | **Global search** |
| R9 | Detection toast is informational only; no path from toast to investigation | Detections don't lead anywhere | **Redesign** toasts with a deep link (menu key opens that player) |
| R10 | Known-nick list shipped as a loose file the user had to keep next to the DLL | Fragile setup | **Embed** in the binary |
| R11 | Sliders have no keyboard/wheel fine-tuning; checks have no "reset this check" | Slower configuration | **Add** step keys and per-check reset |
| R12 | Evidence badge counts every detection and pulses forever | Noise, attention drain | **Count players with new detections**, single pulse on arrival |

**Intentionally not restored** (they contradict the product identity or are covered better):
tab stats overlay and stat APIs, Auto GL, "Want to bet", quick-buy turbo macros, hold render
distance, session/trade HUDs, bow distance chat, cosmetic team hitboxes.

---

## 3. Navigation & information-architecture audit

### 3.1 Findings

1. **Duplicate views.** Evidence and Players both list players, both show a suspicion ring
   header, both show per-check confidence. Users must decide which page to open for "this player".
2. **Icon collision.** The Evidence icon (three lanes with dots) and the Settings icon (three
   sliders with knobs) read as the same glyph at 18 px.
3. **No sense of place.** Page headers carry a title only; deep links (`/vigil <player>`, feed
   rows) jump without a way back.
4. **Flat rail.** Monitoring, investigation and configuration are mixed in one list.
5. **Status is a footnote.** Runtime health is a 3-line card; signal availability is buried in
   About.
6. **Discoverability.** The menu key, `/vigil` commands and Ctrl-shortcuts are never shown.

### 3.2 New structure

```
┌ Rail ─────────────┬ Top bar: ‹ Back · Breadcrumb (Investigate › Players › Kestrel) · Search ⌘K ┐
│ VIGIL             │                                                                         │
│ MONITOR           │                                                                         │
│   Overview        │   Page content (directional transitions)                                │
│ INVESTIGATE       │                                                                         │
│   Players  ●3     │                                                                         │
│ CONFIGURE         │                                                                         │
│   Checks          │                                                                         │
│   Utilities       │                                                                         │
│   Settings        │                                                                         │
│ ───────────────── │                                                                         │
│ [● Active · Status]  health card → Status page                                              │
└───────────────────┴─────────────────────────────────────────────────────────────────────────┘
```

| Page | Purpose (one sentence shown under the title) |
|---|---|
| **Overview** | What is happening right now: status, grouped live activity, watchlist |
| **Players** | Everyone Vigil has observed; open a profile to investigate (Summary · Evidence · Behavior · Identity) |
| **Checks** | What Vigil looks for and how much evidence it needs |
| **Utilities** | Optional Bed Wars helpers: Auto Who, Fast Buy, party detectors |
| **Settings** | Alerts, scope, keybinds, appearance, data |
| **Status** | Is Vigil healthy? Injection, hooks, engine, signals, services |

All former "Evidence" deep links open the profile's Evidence tab. Keyboard: Ctrl+1…6 pages,
Ctrl+K or `/` search, Alt+← back, Esc closes overlays then the menu.

---

## 4. UI/UX audit

| Area | Finding | Change |
|---|---|---|
| Overview feed | Repeats identical rows | Group by player+check with count, latest summary, trend |
| Confidence | Shown as a number and ring; no direction, no story | Trend arrows (Δ60 s), session history chart, milestones, "how it formed" breakdown |
| Player identity | Letter avatars | Real player faces (skin texture in game, procedural in preview) |
| Inspector | Says "+0.3 → 100%" without explaining | Formula breakdown: weight × severity × gain × headroom |
| Settings | Long uniform lists | Keybinds section, conflict warnings, search deep-links that scroll to and flash the row |
| Toasts | Dense text, no action | Redesigned (§9) |
| Empty states | Good | Keep |
| About | Version string hard-coded (2.0.0 vs version.yaml 1.0-dev.6) | Single version source generated from `version.yaml` |

---

## 5. Animation audit

**Findings.** Menu open takes 420 ms (feels sluggish for a frequently toggled tool) while close is
220 ms. Nearly every hover uses the same exponential approach (rate 16–18), page transitions are
a 280 ms fade + 10 px slide regardless of direction, the Evidence badge pulses continuously, feed
rows fade in with the same curve as everything else, and tooltips use one rate for in and out.

**Motion language** (`src/ui/motion.h`, the only place durations and springs are defined):

| Interaction | Behavior | Rationale |
|---|---|---|
| Hover | Fast approach (≈90 ms), color/elevation only, never position | Responsive, quiet |
| Press | Stiff critically-damped spring (inset 1 px) | Tactile without wobble |
| Toggle knob | Under-damped spring (slight overshoot) | Physical switch |
| Selection indicators (rail, tabs, segmented) | Spring 420/32 glides between targets | Shows where focus moved |
| Page change | 200 ms emphasized-decelerate, 12 px slide in the direction of travel | Spatial model of the rail |
| Tab change (in-page) | 160 ms cross-fade, 6 px | Lighter than page change |
| Menu open / close | 260 ms scale 0.975→1 + fade / 150 ms accelerate out | Fast in, faster out |
| Modal (search, onboarding) | 200 ms rise 8 px + backdrop fade; 120 ms out | Focus shift |
| Popover / dropdown | 140 ms decelerate, 4 px from anchor | Anchored |
| Tooltip | 400 ms intent delay, 120 ms in, 70 ms out | Never in the way |
| List entrance | 30 ms stagger, max 8 items, 180 ms | Rhythm without delay |
| Toast | Spring in from the edge, 180 ms accelerate out, reflow springs | Arrival matters, departure doesn't |
| Numbers | Count-up 600 ms out-expo | Values feel measured |
| Charts | Value smoothing; new points ease in | Live but calm |
| Attention | Single pulse on new detection, never perpetual | Respect attention |

"Reduced" keeps state changes but drops travel/overshoot; "Off" snaps.

---

## 6. Branding audit

- **Logo.** The mark's concept (straight V silhouette, curved inner notch cradling a point of
  light) is right, but the geometry is ad hoc: the inner notch is a free quadratic curve unrelated
  to the dot, arm widths differ along their length, and the dot floats. **Refinement:** construct
  on explicit geometry — arms of constant perpendicular width, inner edges parallel to the outer
  ones and joined by an arc *concentric with the dot* (the lid literally cradles the pupil), dot
  and gap sized from the arm width, small optical rounding on the outer corners. Regenerate SVG,
  ICO and PNG from the same construction used by the native renderer.
- **Version and naming.** One version string everywhere; user-facing executable becomes
  `Vigil.exe`.
- **Voice.** Keep precise, calm, evidential. Utilities use the same voice ("Party of 3 joined",
  not "PARTY DETECTED!!").
- **Chat signature** `§8[§9Vigil§8]` stays.

---

## 7. Detection audit

| Check | Finding | Fix |
|---|---|---|
| **Speed / BHop / NoSlow** | Speed multiplier reads potion effects only. Vanilla servers send a player's potion effects to other clients **only when tracking starts**; a potion drunk mid-fight is invisible, so Speed II users can look like Speed cheats (**false positive**). | Read the server-synced `generic.movementSpeed` attribute (sent whenever it changes; includes potions, abilities and server modifiers) and divide out the sprint modifier; use the larger of attribute- and potion-derived multipliers. Treat "particles present but effects unknown" as unknown (conservative) |
| Speed | Head-hitter sprint-jumping (2-block ceilings) legitimately exceeds the sprint-jump envelope (**false positive**) | Exempt sustained/ground indicators for 20 ticks after a jump under a ceiling |
| Speed | Repeated step-up jumps (stairs, slabs, 1-block climbs) shorten air time and raise average speed | Apply a step-up tolerance when recent air segments landed higher than they took off |
| Speed | Ground indicator trusts `onGround` claim | Keep (cross-checked with 12 consecutive ground ticks) |
| BHop | Already skips ceiling/jump-boost segments | Use attribute multiplier for speed-retention indicator |
| NoSlow | Item-use speed limit ignores speed modifiers | Use the same multiplier |
| All | Maintainability | Movement modifiers centralized in one `movementMultiplier()` with documented sources |

New tests: Speed II via attribute only (no visible potion) → no evidence; head-hitter sprint
jumps → no evidence; existing cheat traces still detected.

---

## 8. Antivirus & trust audit (summary; full report in `docs/TRUST_AUDIT.md`)

| Factor | Current | Plan |
|---|---|---|
| Distribution | Zip with launcher + DLL | Single `Vigil.exe` with the DLL as a plain (uncompressed, unencrypted) resource |
| Extraction | n/a | Fixed, versioned location `%LOCALAPPDATA%\Vigil\runtime\Vigil-<ver>.dll`; written once, hash-verified, never into Temp, never randomized, never deleted-after-use |
| Injection | `CreateRemoteThread(LoadLibraryA)` with minimal rights, RW memory | Keep the documented technique (it is what the product is); `LoadLibraryW`, minimal access rights, no RWX, no shellcode, no manual mapping |
| Binary metadata | Version info present | Full VERSIONINFO + application manifest (asInvoker, DPI aware) on every binary |
| Behavior | No packing, no anti-debug | Keep; no obfuscation, no string encryption, readable logs |
| Unavoidable | Process injection, IAT patching, window subclassing | Documented, explained in README |
| Recommendations | — | Authenticode signing, Microsoft false-positive submission, published SHA-256 hashes, reproducible CI builds |

---

## 9. Feature designs

**Injection confirmation.** The launcher shows a small native window (same renderer and fonts as
the menu) that walks through *Finding Lunar Client → Starting Vigil → Waiting for the game →
Vigil is active*, completing only when the DLL signals a named event after its runtime, hooks and
UI are up. In game, an activation card slides in at the top center: the mark draws in, four
checks tick in sequence (Attached · Detection engine (11 checks) · Monitoring context · Interface
ready), and a key hint shows how to open Vigil. A chat line repeats the hint.

**Runtime status.** Status page with a health hero (Active / Degraded / Standing by), cards for
Injection & hooks, Detection engine, Signals (with which checks each signal feeds), Monitoring,
Services (utilities, identity, network), Data, and a live runtime log tail. The rail footer
becomes a compact health indicator that opens Status.

**Onboarding.** First menu open shows a five-step sheet over the menu: What Vigil is · How
detections work · Confidence (animated live example) · Investigations · Make it yours (menu key,
theme, search). Skippable at every step; replayable from Settings, About and search.

**Hotkeys.** Keybinds section listing every binding (menu, search, utility toggles). Capture
mode with "Esc cancels · Backspace clears", conflict detection against other Vigil bindings and
Minecraft defaults with a one-click resolution, per-binding and global reset.

**Global search.** Ctrl+K command palette over pages, players, checks, settings, evidence,
themes, utilities, investigation tools and actions; ranked prefix/word/substring matching;
keyboard first; results deep-link and flash their target.

**Detection toasts.** Player face + severity ring, check and confidence with trend, the most
telling evidence line, a confidence sparkline, and "Right Shift · Investigate" (opening the menu
while a toast is visible jumps to that player). Repeat detections update their card (count bump)
instead of stacking; critical detections stay longer; position is configurable.

**Confidence & investigation.** Trend arrows everywhere confidence appears; profile Summary with
session confidence chart, per-check drivers, milestones (first evidence, 50 %, detections, peak,
recovery) and top evidence; inspector explains each contribution numerically.

**Player profiles.** One page: roster (faces, trend, filters: All · Suspicious · In view ·
Nicked) + profile (header with face, chips, actions: Report, Watch, Denick, Copy name) with
Summary · Evidence · Behavior · Identity tabs.

**Denickers.** Identity service (worker thread, WinHTTP): nick detection by UUID version, skin
analysis from the signed `textures` property, embedded Hypixel nick-skin list, skin-owner lookup
through Mojang's public session server, optional number denicker (final-kill/bed numbers through
the user's own Bordic key). Results appear in the Identity tab, the profile header, the roster
and search. No analytics, no telemetry, no reporting; only the documented lookups are made, and
only for nicked players.

**Utilities.** Auto Who (game-start triggers → `/who`), Fast Buy (Quick Buy clicks → instant
buy), Party Detector (lobby join/quit batching), Party Queue Detector (sequential entity-id runs
sized by the queue mode). Each has a card with toggle, hotkey, options and an activity log.
Pure logic lives in `src/utilities/` with unit tests; JNI glue in `src/game/`.

**Themes.** Four new themes (Verdant, Dusk, Paper, Signal — the last a high-contrast theme) and a
Custom theme editor: base theme, accent hue, surface tint, contrast, corner radius, glow — with a
live miniature.

**Distribution.** `Vigil.exe` = launcher + embedded DLL. Data moves to `%LOCALAPPDATA%\Vigil`
(config, history, sessions, logs), with one-time migration from the old `vigil/` folder.

---

## 10. Risk assessment

| Risk | L | I | Mitigation |
|---|---|---|---|
| New JNI members (attribute map, skins, chat lines, containers) absent on Lunar | M | M | Each resolved independently with logging; features degrade with a visible reason on Status |
| GL texture ids for faces reused after unload | M | L | Re-resolve every few seconds for present players only; letter avatar fallback |
| Fast Buy click swallowing misfires outside Quick Buy | L | M | Only when the open container's slot 0 is "Quick Buy"; queue cleared when the screen closes |
| Embedded-DLL launcher draws more AV attention than a zip | M | M | Plain resource, fixed path, metadata, no obfuscation; trust doc + signing recommendation |
| Data directory move loses settings | L | H | Copy-once migration, old folder left intact |
| Network failures in identity service | M | L | Timeouts, backoff, per-request status in UI; never blocks the game thread |
| Scope of UI restructure regresses existing screens | M | M | Preview screenshots of every page × several themes before/after; tests unchanged |
| Hypixel chat format changes break utilities | M | L | Pure-logic parsers with unit tests; patterns in one place |

## 11. Implementation roadmap (execution order)

| # | Work | Verification |
|---|---|---|
| 1 | Version source, motion tokens, renderer image support | Build |
| 2 | Shell: grouped rail, top bar, breadcrumbs, history, page routing | Preview screenshots |
| 3 | Players profile (merge Evidence), faces, confidence trend/history/milestones | Screenshots |
| 4 | Overview grouping, Status page, rail health card | Screenshots |
| 5 | Global search | Screenshot + scripted interaction |
| 6 | Toast redesign, activation card | HUD screenshots |
| 7 | Onboarding, keybinds + conflicts | Screenshots |
| 8 | Themes + Custom editor, logo geometry + brand assets | Screenshots, regenerated assets |
| 9 | Detection fixes + tests | `detection_tests` |
| 10 | Utilities (logic + tests, runtime glue, UI) | Tests, build, screenshots |
| 11 | Identity service + denickers (logic + tests, WinHTTP, glue, UI) | Tests, build, screenshots |
| 12 | `Vigil.exe`: embedded DLL, launcher window, ready event, data dir migration, CI | Build, launcher screenshot |
| 13 | Reports, README, release notes | Review |
| 14 | QA: full build, tests, every page × themes | Reports |
