# Vigil — Finalization, Validation & Rust Preparation Plan (1.0-dev.9)

> Written before any code changed in this pass. Earlier plans are kept in
> [`docs/history/`](docs/history/) (2.0 rebuild, polish pass, quality pass). Outcomes are recorded
> in [`docs/FINAL_REPORT.md`](docs/FINAL_REPORT.md); the Rust study is [`RUST_PLAN.md`](RUST_PLAN.md).

The overhaul made Vigil a product, the polish pass made it feel finished, and the quality pass made
it look at the right players for the right reasons. This pass asks two final questions:

1. **Can we prove it works, every release, without guessing?** Today a release is one workflow
   that builds, runs two test executables and publishes. Nothing checks that `Vigil.exe` embeds
   the DLL it was built with, that the version is consistent, that the workflow itself is valid,
   or that a version is never published twice. Six JNI paths have never run in a live client, and
   the only record of what to check is a paragraph in a report.
2. **Is every workflow the user needs actually there?** Confirmations work only for players seen
   this session, there is no way to exempt a friend, investigations disappear with the session,
   nothing can be navigated without a mouse, and sliders can't be typed into.

Stage 2 prepares — but does not begin — an eventual migration to Rust.

**Refinement rule.** Every item below was first checked against the existing systems. New
surfaces are introduced only where an existing one can't carry the job (§3.4 explains the one new
page); everything else extends what is there.

**Method.** Every source file in `src/`, the tests, the preview harness, the workflow, CMake and
the docs were read. All targets were built (MSVC 19.44, `/W4`, 0 warnings), both suites pass
(152 + 52 expectations), and every page was rendered in the preview harness and reviewed.

---

## 1. Current state audit

### 1.1 Architecture

```
platform/  dllmain (JVM attach, pump) · hooks (SwapBuffers IAT, WndProc) · launcher (Vigil.exe)
game/      JNI adapter: sampler, context, chat, utilities runtime, alerts, world render   ← JNI
ui/        OpenGL immediate-mode UI: app shell, pages, HUD, widgets, theme, fonts, icons ← GL/GDI
identity/  nick detection, denickers (worker thread, WinHTTP via net/)
utilities/ Auto Who, Fast Buy, party detectors (pure logic)
detection/ engine, tracks, combat, confidence, checks, match, roster, marks (pure C++)
core/      json, log, paths, settings registry
```

| Strength | Evidence |
|---|---|
| Pure detection core | `detection/` has no JNI, GL or Win32; tests drive it with synthetic frames |
| One data path in | `WorldFrame` per client tick is the only input to the engine |
| Typed settings registry | One declaration drives defaults, persistence, UI rows and search |
| Evidence → confidence → gate | A single outlier can never produce a detection |
| Root-cause detection fixes | The quality pass removed false positives without loosening thresholds |

| Weakness | Consequence |
|---|---|
| `vigil_core` mixes pure code (detection, json) with services (WinHTTP, identity threads) | Library boundaries don't show which code is portable or testable; a Rust FFI boundary has no natural seam |
| Detection reads the global `Settings` singleton | Engines can't be configured independently (parity testing, replays with different settings) |
| No recorded-input format | Real sessions can't become regression tests; a future port can't be checked against the current engine |
| Two test executables with a copy-pasted `EXPECT` macro | Every new suite repeats the harness |

### 1.2 What feels unfinished

* Confirmations exist only for players seen this session; there is no way to confirm a name, search
  confirmations, edit a reason or add a note.
* No way to stop Vigil analyzing a friend or party member (only "Ignore teammates").
* An investigation is lost when the session resets or the game closes.
* Keyboard: only Ctrl+1…6, Ctrl+K, `/`, Backspace and Esc. Nothing can be focused or activated.
* Sliders accept drag, wheel and arrows (while hovered) — never a typed value.
* Low-confidence evidence is named by check ("Speed") as confidently as a detection.

### 1.3 What should be fixed

See the audits below; each finding has an ID used in the roadmap (§12) and the final report.

---

## 2. UI audit

| # | Finding | Change |
|---|---|---|
| U1 | **Checks sidebar dots.** A 3 px dot on the icon tile of every check with evidence this session. Its color comes from `severity(30 + 4 × count)`, so after eight observations every dot is alarm red. It has no tooltip and no legend, and it looks exactly like the roster's "new detection" dot, which means something else | Replaced by a count badge with a number (observations this session), neutral color, red only when the check produced detections; tooltip; and a separate warning glyph when the check is missing a required game signal (§2 U1 detail in report) |
| U2 | Settings → Identity is four rows whose explanations are cut at one line ("Looking up an…"); the denickers' status lives on Status, their results in profiles | Denickers move to Utilities as proper cards (§3 X6) |
| U3 | Profile header actions are icon-only (Copy, Watch, Confirm) and there is no room for Save, Export or Exempt | Actions regrouped: Copy · Watch · Confirm · ⋯ (More: Save profile, Export, Exempt from checks) · Report |
| U4 | Slider value boxes look like fields but are inert | Clicking (or Enter) turns the value box into a numeric field (§9) |
| U5 | No visible keyboard focus anywhere | Focus ring drawn by the widget layer (§8) |
| U6 | Icon set lacks Save, Import, Export-out, Exempt, Add person, Sound; Export exists but reads as "share" | New icons on the 20-unit grid; icon audit in the report |
| U7 | Rail has one item under INVESTIGATE although investigation now has two kinds of objects (live players, the user's records) | Records page (§3.4) |
| U8 | Status shows health but offers no way to capture it (a bug report is "send a screenshot") | Diagnostics card: copy/save a report, record a diagnostic trace (§6) |

## 3. UX audit

| # | Friction | Change |
|---|---|---|
| X1 | "Confirm" only exists inside a live profile | Confirm, watch and exempt anyone: current players, players from this session, or any username (§3.4) |
| X2 | Confirmations can't be searched, reviewed or annotated | Records → Confirmed: search, reasons editable, note, Vigil's confidence at the moment of confirming, last seen |
| X3 | No per-player exemptions | Records → Exempt: friends and party members are listed but never judged |
| X4 | Investigations vanish with the session | Save profile (snapshot) · Export · Import; Records → Saved (§5) |
| X5 | Mouse-only | Keyboard navigation (§8) |
| X6 | Denickers are hard to find: Settings → Identity, Status, and the profile's Identity tab each hold a third | Utilities → **Identity tools** (two cards with status, setup and privacy); Settings → Identity removed; search and "Set up" buttons point there |
| X7 | Low-confidence findings use the same words as detections | Category language below Elevated: "Unusual movement" instead of "Speed" on summary surfaces (§7.4) |
| X8 | Detection cards, the briefing and "new detection" badges survive into the next game | Match cleanup (§7.5) |
| X9 | Sliders can't be set precisely | Typed values, Shift for ×10 steps, Home/End (§9) |

### 3.4 Records — the one new page

Confirmations, watches, exemptions and saved investigations are the user's **durable records**.
They share one model (a person, possibly not present, with the user's decision attached), one
need (search, add by name, review, remove) and one lifetime (across sessions). The Players
roster is 286 px wide and scoped to the live session; putting four management lists and an
add-by-name flow into it would bury both jobs. **Records** sits under INVESTIGATE next to Players:

```
INVESTIGATE  Players   who is here, what they are doing          (live)
             Records   confirmed · watching · exempt · saved      (yours, kept across sessions)
```

Players keeps its Marked scope (who in this session is marked), each row linking to Records.
Settings → Data → "Your marks" becomes a pointer to Records. Nothing is duplicated.

## 4. Testing audit

| # | Gap | Change |
|---|---|---|
| T1 | Two suites, duplicated harness macros | `tests/test_framework.h` (EXPECT, suites, filters, summary) shared by every suite |
| T2 | Marks (watch, confirm) are untested | Records suite: persistence, format compatibility, exempt semantics, name binding |
| T3 | No test reads or writes a saved profile | Snapshot round-trip, check-id remapping, malformed and future-format files |
| T4 | Match lifecycle tested only through detection outcomes | Explicit lifecycle tests: queue → match → post-game → new world; carried confidence; cleanup |
| T5 | Confidence model tested indirectly | Invariants: monotone add, bounded 0..100, decay half-life, gate needs N observations, carried confidence can't complete a detection |
| T6 | Player-state handling (exempt, teammate, local, NPC, duplicate) spread across scenarios | One table-driven test per state: listed? analyzed? alerts? highlighted? |
| T7 | No determinism check | The same frames through two engines must produce identical evidence, confidence and detections |
| T8 | No recorded-input regression path | Trace format + golden trace (§6.3) |
| T9 | UI logic untested (numeric parsing, focus order, concern labels) | Pure helpers moved out of draw code and tested |
| T10 | Falsifiability isn't automated | Kept as a documented procedure (VALIDATION.md); critical tests list the revert that must fail them |

## 5. Validation audit

| # | Finding | Risk | Change |
|---|---|---|---|
| V1 | `release.yml` builds, tests and publishes in one job on every push to master | A failing doc or metadata problem can't block; nothing runs before tests but the build | Split: reusable **validate** workflow (required) → **release** workflow that only packages already-validated artifacts |
| V2 | Workflows are never linted | A typo in a workflow is found when a release fails | `actionlint` job |
| V3 | Re-pushing with the same `version.yaml` republishes the tag with new binaries | Two different `Vigil.exe` files under one version | Release gate: an existing tag is never overwritten; the job explains how to publish |
| V4 | Nothing checks that `Vigil.exe` embeds the DLL built in the same build | A stale embedded DLL (NMake mtime trap, partial rebuild) ships silently | Build ID stamped into both; `Vigil.exe --self-check` verifies the embedded runtime's version, build ID and SHA-256; CI runs it |
| V5 | Version consistency is assumed | `version.yaml`, the resource version, release notes and the changelog can disagree | Metadata check: format `1.0-dev.N`, a changelog entry, release notes heading |
| V6 | Warnings are reported but don't fail CI | Warning regressions | `VIGIL_WARNINGS_AS_ERRORS` in CI |
| V7 | `permissions: contents: write` for the whole workflow, including PR builds | Over-broad token | Read-only by default; write only in the publish job |
| V8 | Java shim reproducibility is informational | Correct: javac output differs across JDK patches | Kept informational, documented |
| V9 | No UI smoke test | Rendering crashes found by users | Preview smoke job renders every page (informational until the runner's OpenGL proves stable; see VALIDATION.md) |
| V10 | Docs links aren't checked | Broken README links | Link check in the metadata job |

## 6. Live validation audit

### 6.1 What runs only in Lunar Client

| System | Validated in game? | Source |
|---|---|---|
| JVM attach, client pump, SwapBuffers hook, menu host screen | ✅ (user, "Fix GUI bug") | 2.0 |
| Sampler core signals (positions, rotation, swing, hurt, items, potions, blocks) | ✅ | 2.0 |
| Movement attribute, skins, chat reader, Fast Buy | ⚠️ unconfirmed | dev.7 |
| Tab list presence, ping, `isBurning`, over-head markers (view angles) | ⚠️ unconfirmed | dev.8 |
| Match phases from chat (post-game) | ⚠️ unconfirmed | dev.8 |
| WndProc probe re-hook, overlay-aware IAT patching | ⚠️ unconfirmed | dev.8 |
| Runtime version handshake (new) | ⚠️ new | dev.9 |

### 6.2 Gaps

* A missing JNI binding shows as one Status row; which name was tried (MCP or obfuscated) is only
  in the log.
* There is no way to hand a developer what Vigil saw: no report, no recording.
* The checklist lives in a report section and is not versioned per release.

### 6.3 Plan

* **Diagnostics report** (Status → Diagnostics → Copy / Save): versions (exe, runtime, build ID),
  hooks, every signal with the binding that resolved it, phase, counters, settings that differ
  from defaults, recent log lines. No player names unless the user includes the roster.
* **Diagnostic trace recording** (Status → Diagnostics → Record): the exact `WorldFrame`s the
  engine received, as JSON lines, capped in size and time, off by default. A trace replays through
  the engine offline (`VigilReplay`) and can become a golden regression test.
* **Binding report** from the sampler and runtime: each signal records the name it bound to.
* **`docs/LIVE_VALIDATION.md`**: what is validated, what is not, and for each item the exact steps,
  the expected Status/log evidence, and what to send back. A per-release checklist section.

## 7. Detection audit

### 7.1 Philosophy check

Every check already compares an observation against a **vanilla limit derived from the player's
context** (movement multiplier, latency, terrain, potion, item in use). What is missing is that
the *expectation* is never a first-class value: it is folded into a limit and described in prose.
The inspector can say "limit 0.40" but not "expected 0.36 (sprint-jumping, Speed I), legitimate up
to 0.40, observed 0.52".

### 7.2 Behavioral envelopes

`Evidence` gains an **envelope**: `quantity`, `expected` (what vanilla play produces in this
context), `limit` (the edge of the legitimate envelope: expected + tolerance), `observed`, `unit`
and the `context` that shaped it ("Speed I", "stair tolerance", "ping 180 ms"). Checks with a
physical model (Speed, BHop, NoSlow, Velocity, Reach) fill it explicitly; the rest derive it from
their limited value. The inspector leads with **Expected · Observed · Why it's suspicious**.
Severity formulas and thresholds are **unchanged** (no loosening, no tightening; tests prove it).

### 7.3 Prediction as evidence

Already true and kept: checks never decide. Each deviation becomes evidence; the confidence model
(diminishing returns, decay, gate) decides. Documented in the detection philosophy report.

### 7.4 Uncertain detections

Below **Elevated** (< 40 %) Vigil has a hint, not a finding. Summary surfaces (roster rows, match
briefing, search, watchlist) say **"Unusual movement / combat / building"** (the check's category)
instead of a check name; investigation surfaces (profile drivers, evidence) keep exact names with
"low confidence" context. Elevated and above name the check.

### 7.5 Match lifecycle cleanup

| Item | Today | Change |
|---|---|---|
| Detection cards on screen | Survive a world change | Cleared on world change |
| Match briefing | A scheduled briefing can fire after the world changed | Bound to its match and scene |
| "New detection" badges (rail, roster) | Persist across games | Cleared for players not in the new game when a match starts |
| Engine stall window, no-update run | Survive a world change | Reset |
| Combat history | Already cleared | — |

## 8. Accessibility audit

| Area | Today | Change |
|---|---|---|
| Keyboard focus | None | Tab / Shift+Tab through every control on the page, in reading order; Enter/Space activate; Esc clears |
| Visibility of focus | None | 2 px accent focus ring, shown only after keyboard use (mouse clicks hide it) or always (setting) |
| Controls | Sliders respond to arrows only while hovered | Focused sliders: arrows, Shift = ×10, Home/End, Enter to type; segmented/tabs: arrows; dropdown: Enter, arrows, Esc; toggles: Space |
| Lists | Mouse only | Players and Records lists: ↑/↓ move the selection |
| Scrolling | Wheel only | Focus scrolls the focused control into view; Page Up/Down scroll the page |
| Tooltips | Hover only | Shown for the focused control too, so icon-only buttons are discoverable by keyboard |
| Readability | Faint text at ~3.2:1 on some dark themes | **Higher contrast** option lifts secondary and faint text and borders in every theme |
| Motion | Full / Reduced / Off | Kept; moved to the new Accessibility section |
| Discoverability | Shortcuts listed under Keybinds | Keyboard section in Keybinds; search finds "keyboard", "focus", "contrast" |

## 9. Slider interaction model

* Drag, wheel and arrow keys as today; **Shift** multiplies the step by 10.
* Click the value (or press Enter while the slider is focused): the value becomes a numeric field
  showing the raw number. Enter commits (clamped to the range, snapped to the step, unit
  optional), Esc cancels, clicking elsewhere commits. Invalid input shakes the field and keeps the
  old value.
* Deferred settings (interface scale) apply on commit, as they do on release.

## 10. Runtime version protection & release naming

### 10.1 Consistency checks

| Pair | Check | On mismatch |
|---|---|---|
| Vigil.exe ↔ embedded DLL | Version and build ID in the DLL's version resource equal the exe's | `--self-check` fails (CI); the launcher refuses with "This copy of Vigil.exe is damaged" |
| Embedded ↔ extracted DLL | SHA-256 of `runtime\Vigil-<ver>.dll` equals the embedded bytes | Re-extract; if the file is locked by a running game, report it |
| Extracted ↔ running (in game) | The launcher reads the version of the Vigil module already loaded in the game | Same version: "already running". Different: "Vigil 1.0-dev.8 is running in this game. Restart Lunar Client to use 1.0-dev.9." — never injects a second runtime |
| Runtime ↔ runtime (in process) | A per-process named mutex records the running version | A second Vigil DLL of any version refuses to start and tells the launcher why |
| Data files ↔ runtime | Settings keep unknown keys; marks and snapshots carry a format number | A newer format is opened read-only, never overwritten (Status says so) |

### 10.2 Release naming

| Candidate | Problem |
|---|---|
| `Vigil 1.0-dev.9.exe` | GitHub replaces spaces in asset names with dots (`Vigil.1.0-dev.9.exe`); spaces need quoting in shells and `%20` in URLs |
| `Vigil_1.0-dev.9.exe` | Safe, but underscores read poorly next to version dots |
| **`Vigil-1.0-dev.9.exe`** | URL- and shell-safe, survives GitHub unchanged, sorts by version, readable |

**Decision:** `Vigil-<version>.exe` plus `SHA256SUMS`. The executable never depends on its own
file name (verified: only the legacy-migration path reads it, as a directory).

## 11. Documentation audit

| Doc | State | Change |
|---|---|---|
| README | Current for dev.8 | Records, keyboard, downloads named by version, validation links |
| Developer/architecture doc | **Missing** (layout table in README only) | `docs/ARCHITECTURE.md`: layers, data flow, threading, persistence, boundaries |
| Validation doc | Missing | `docs/VALIDATION.md`: test suites, golden traces, falsifiability, CI architecture |
| Live validation | A section of QUALITY_REPORT | `docs/LIVE_VALIDATION.md` + per-release checklist |
| Release process | Implicit in the workflow | `docs/RELEASE_PROCESS.md` |
| Reports | One per pass | `docs/FINAL_REPORT.md` holds every report this pass requires |
| TECH_DEBT | Last updated in 2.0 | Refreshed |
| Rust | — | `RUST_PLAN.md` |

## 12. Rust migration audit (summary; full study in RUST_PLAN.md)

| Layer | Lines | Rust readiness | Notes |
|---|---|---|---|
| detection/ | ~4.3k | High | Pure; needs settings injected instead of the global registry |
| utilities/, identity/ pure helpers | ~0.8k | High | Pure functions + small state machines |
| core/ (json, settings, log, paths) | ~0.7k | High | Replaced by serde/std equivalents |
| ui/ | ~8.9k | Medium-low | Immediate-mode GL 1.x + GDI font rasterization; large surface, no correctness risk, high churn |
| game/ (JNI adapter) | ~5.6k | Low (stay C++ longest) | Obfuscation fallbacks, global refs, unverifiable outside Lunar |
| platform/ (hooks, injection, launcher) | ~1k | Low for hooks; medium for launcher | IAT/WndProc hooks are the highest-risk code; launcher is plain Win32 |

**Preparation in this pass** (no Rust): split `vigil_core` into `vigil_core` (json, log, paths,
settings), `vigil_detection` (pure) and `vigil_services` (net, identity, utilities); the trace
format + replay tool as the parity harness; determinism tests; documented engine I/O contract.

---

## 13. Risk assessment

| Risk | L | I | Mitigation |
|---|---|---|---|
| Keyboard focus steals keys the game or a widget expects | M | M | Focus keys handled only while the menu is open and no text field captures input; Esc order unchanged; tests for focus order |
| Exempting the wrong player hides a cheater | L | H | Exempt is an explicit user action with a visible badge everywhere; Status counts exemptions; never automatic |
| Name-based marks bind to the wrong account | L | M | Bind only to real (v4) accounts; nicks can't take existing names on Hypixel; documented |
| Snapshot import of a hostile file | L | M | Parser bounds (size cap, item caps, string caps); unknown fields ignored; no paths or code from the file are used |
| Version handshake blocks a legitimate start | L | H | Only refuses when a *different* Vigil runtime is already loaded in the same process; message says exactly what to do |
| CI preview smoke flaky on software OpenGL | H | L | Informational job; required checks are build, tests, self-check, metadata, lint |
| Release gate blocks a needed hotfix | L | M | Bump the version (documented); no force path that overwrites a published binary |
| Trace recording leaks player names | M | L | Off by default; stored locally only; the report says traces contain names |
| UI sounds become noise | M | L | Off by default; four events only; one volume control |
| Envelope refactor changes detection behavior | L | H | Thresholds and severity untouched; every existing detection test must still pass unchanged |

## 14. Ordered implementation roadmap

| # | Work | Verification |
|---|---|---|
| 1 | Version 1.0-dev.9; this plan; archive the dev.8 plan | — |
| 2 | CMake: library split (core / detection / services), build ID, warnings-as-errors option, shared test framework | Build, suites |
| 3 | Runtime version protection: DLL version resource + build ID, mutex handshake, launcher checks, `--self-check`, settings unknown-key preservation, format guards | Self-check run; tests for format guards |
| 4 | Release naming + GitHub Actions: validate (lint, metadata, build/test/self-check, preview smoke, shims) → release (gate, package, publish) | actionlint locally where possible; dry-run reasoning documented |
| 5 | Marks v2: exempt, notes, confidence at confirm, name-keyed marks and binding; engine exempt handling | Records tests |
| 6 | Snapshots: save / export / import, check-id remapping, bounds | Snapshot tests |
| 7 | Envelopes in evidence; explicit envelopes in Speed, BHop, NoSlow, Velocity, Reach; inspector | All detection tests unchanged + envelope tests |
| 8 | Trace format, recorder, `VigilReplay`, golden trace, determinism | Trace tests |
| 9 | Match cleanup (engine, HUD, app state) | Lifecycle tests, preview |
| 10 | Widgets: keyboard focus, focus ring, keyboard tooltips, slider numeric entry, contrast option | UI logic tests; preview with `--key` sequences |
| 11 | Records page (Confirmed · Watching · Exempt · Saved), add-player picker, snapshot viewer | Preview screenshots |
| 12 | Profile header regroup (More menu), uncertain language, check sidebar badges, Identity tools in Utilities, Accessibility section, UI sounds, icons | Preview screenshots in several themes |
| 13 | Diagnostics: binding report, report copy/save, trace recording control | Preview; in-game checklist |
| 14 | Docs: README, ARCHITECTURE, VALIDATION, LIVE_VALIDATION, RELEASE_PROCESS, FINAL_REPORT, TECH_DEBT, RELEASE_NOTES | Link check |
| 15 | RUST_PLAN.md | Review |
| 16 | QA: full build (0 warnings as errors), all suites, falsifiability spot checks, every page × themes | Final report |
