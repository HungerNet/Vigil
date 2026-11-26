# Live validation

For whoever tests a Vigil build in Lunar Client before (or right after) a release. Everything
that can be proven outside the game is proven by CI ([VALIDATION.md](VALIDATION.md)); this page
covers the rest: what is already confirmed in game, what is not, and exactly how to check each
item without guessing.

## 1. Tools for live testing (new in 1.0-dev.9)

| Tool | Where | Use it to |
|---|---|---|
| **Diagnostics report** | Status → Diagnostics → Copy report / Save report | Capture versions, hooks, every signal and the binding behind it, the engine, services and changed settings. No player names, keys or notes. Paste it into the bug report. |
| **Game bindings** | Status → Diagnostics (row and tooltip), and the report | See which Minecraft members are missing and which resolved only through an obfuscated fallback name |
| **Trace recording** | Status → Diagnostics → Record a trace | Record exactly what Vigil saw; a developer replays it with `VigilReplay` and can turn it into a regression test. Stops itself after 15 min or 64 MB. Contains player names. |
| **Logs** | `%LOCALAPPDATA%\Vigil\logs` | `launcher.log` (new: versions, runtime handshake, failure reasons), `vigil.log`, `runtime.log` (sampler signals, hooks, traces), `detections.log` |
| **Self-check** | `Vigil-<ver>.exe --self-check report.json` | Prove the downloaded file is intact (versions, build IDs, embedded runtime hash) without starting anything |

## 2. Status of every live-only system

✅ confirmed in game · ⚠️ implemented and tested outside the game, not yet confirmed in game ·
🆕 new in this version

| # | System | Since | Status | Degrades to |
|---|---|---|---|---|
| L1 | JVM attach, client pump, SwapBuffers hook, menu host screen | 2.0 | ✅ | — |
| L2 | Core sampler signals (positions, rotation, swing, hurt, items, potions, blocks) | 2.0 | ✅ | — |
| L3 | Movement-speed attribute (`generic.movementSpeed`) | dev.7 | ⚠️ | Potion effects / conservative 1.45× |
| L4 | Player skins (faces) | dev.7 | ⚠️ | Initials on a disc |
| L5 | Chat reader | dev.7 | ⚠️ | Auto Who, party detectors, post-game detection unavailable (Status says so) |
| L6 | Fast Buy | dev.7 | ⚠️ | Off |
| L7 | Tab list presence (`getPlayerInfo`) | dev.8 | ⚠️ | NPC rules fall back to UUID/name |
| L8 | Ping (`getResponseTime`) | dev.8 | ⚠️ | Velocity assumes 150 ms |
| L9 | `Entity#isBurning` | dev.8 | ⚠️ | Burning victims not skipped by Velocity |
| L10 | Over-head markers (`RenderManager` view angles) | dev.8 | ⚠️ | Markers skipped |
| L11 | Post-game from chat announcements | dev.8 | ⚠️ | Next world change ends the match |
| L12 | WndProc probe re-hook, overlay-aware IAT patching | dev.8 | ⚠️ | — |
| L13 | Runtime version handshake (launcher ↔ running runtime, per-process claim) | dev.9 | 🆕 | — |
| L14 | Keyboard navigation through the game's window procedure (Tab, Enter, arrows) | dev.9 | 🆕 | Mouse |
| L15 | Interface sounds (`PlaySound` inside the game process) | dev.9 | 🆕 | Silent (off by default) |
| L16 | Clipboard import of a file copied in Explorer (`CF_HDROP`) | dev.9 | 🆕 | Imports folder |
| L17 | Trace recording during real play | dev.9 | 🆕 | — |
| L18 | Records, exemptions, saved profiles persist across game restarts | dev.9 | 🆕 | — |

## 3. How to validate each item

Before starting: enable Settings → Data → Session journal (default on) and keep Status open in a
second pass. For every ⚠️/🆕 item, record ✅ or ❌ in the checklist (§4) with the evidence named here.

| # | Steps | Expected evidence | If it fails, send |
|---|---|---|---|
| L3 | Bed Wars, drink a Speed potion, sprint-jump for 10 s | Status: *Movement attribute* ok; no Speed evidence on yourself with Settings → General → Analyze yourself on | Report, trace of the 10 s |
| L4 | Open Players in a full lobby | Real faces in halos, not initials | Report (bindings) |
| L5 | Join any game; type in chat | Status → Utilities: *Chat reader* "N lines read" increasing | Report |
| L6 | Turn on Fast Buy, open Quick Buy, left-click an item | Bought with one click; Utilities activity line | Report, `runtime.log` |
| L7 | Bed Wars lobby with shopkeepers | Players roster footer "N NPCs ignored"; shopkeepers absent from Players | Report |
| L8 | Any match | Report: *Player latency* ok; a 200 ms+ player taking knockback produces no Velocity evidence | Report, trace |
| L9 | Stand in fire/lava edge while someone swings nearby | No Velocity evidence on you (Analyze yourself on) | Trace |
| L10 | Confirm a player (or reach High), look at them in first person | Small ring marker above their head | Report |
| L11 | Finish a game (win or lose) | Status: phase *Post-game* until the world changes; no evidence after the announcement | `runtime.log`, trace around the end |
| L12 | Run with OBS/Discord overlay; open and close Vigil 10 times | No freeze or crash; Status: input hook ok | Report, logs |
| L13 | (a) Run Vigil.exe twice with the game open. (b) With an older Vigil running in the game, run this one | (a) "Vigil is already running". (b) "Restart Lunar Client to update — Vigil 1.0-dev.8 is running in this game" and nothing injected | `launcher.log` |
| L14 | Open Vigil; press Tab repeatedly, Enter on a page, arrows on a slider and in Players | Focus ring moves in reading order; Enter activates; arrows adjust/select; Esc closes popups then Vigil; the game never receives these keys | Video or description |
| L15 | Settings → Accessibility → Interface sounds On; save a profile; confirm a player | A short quiet tick / two-note sound; nothing on clicks or navigation | — |
| L16 | Export a profile; in Explorer copy the file (Ctrl+C); Records → Saved → Import | "Imported 1 profile"; it opens | The file |
| L17 | Record a 2-minute trace in a match; stop | File in `traces/`; `VigilReplay` on it reproduces the session's detections | The trace (contains names) |
| L18 | Exempt a friend, confirm someone by name, save a profile; restart the game | All three are still in Records; the friend is listed but never judged | `history/marks.json`, report |

## 4. Per-release checklist

Copy this block into the release's tracking issue and fill it in.

```
Vigil <version> — live validation            tester: ____   date: ____   Lunar build: ____
[ ] Downloaded Vigil-<version>.exe; SHA-256 matches SHA256SUMS
[ ] Vigil-<version>.exe --self-check → "ok": true
[ ] Fresh start: launcher reaches "Vigil is active"; activation card in game
[ ] Status: no issues (or every issue explained below); Diagnostics report saved
[ ] Game bindings: 0 missing   (if not: list them)
L3  movement attribute ......... [ ]     L11 post-game from chat ........ [ ]
L4  skins ...................... [ ]     L12 overlays / re-hook ......... [ ]
L5  chat reader ................ [ ]     L13 version handshake .......... [ ]
L6  Fast Buy ................... [ ]     L14 keyboard navigation ........ [ ]
L7  tab list / NPCs ............ [ ]     L15 interface sounds ........... [ ]
L8  ping / Velocity window ..... [ ]     L16 clipboard import ........... [ ]
L9  isBurning .................. [ ]     L17 trace recording ............ [ ]
L10 over-head markers .......... [ ]     L18 records persist ............ [ ]
[ ] One full Bed Wars game: no false detection on legitimate players you know
[ ] Two minutes of sprint-jumping with Analyze yourself on: no evidence on yourself
Notes / failures (attach report, logs, traces):
```

## 5. After validation

* Mark items confirmed in §2 (⚠️ → ✅) in the same pull request that records the checklist.
* A failure gets an issue with the report and, where possible, a trace; once fixed, the trace (or a
  synthetic equivalent) becomes a regression test ([VALIDATION.md](VALIDATION.md) §4).
