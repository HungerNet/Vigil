# Vigil — Quality, Relevance & UX Plan (1.0-dev.8)

> Written before any code changed in this pass. Earlier plans are kept in
> [`docs/history/`](./) (2.0 rebuild, polish & maturity pass). Outcomes are recorded in
> [`docs/QUALITY_REPORT.md`](../QUALITY_REPORT.md).

The overhaul made Vigil a product, and the polish pass made it feel finished. This pass asks a
different question: **is Vigil looking at the right players, for the right reasons, and saying so
clearly?** It found root causes in four places:

* **Relevance.** Vigil remembers everyone it has ever seen this session and ranks them together.
  Lobby NPCs, players from three games ago and the people in the current match share one list.
* **Detection quality.** Velocity, BHop, Speed and NoSlow judge server-relayed positions as if
  they arrived on time, every tick, from the moment of the action. They don't. Each check has at
  least one false positive that comes from latency, bunching or a single-point environment probe,
  not from a threshold.
* **Interface scale.** Dragging the scale slider moves the slider under the cursor. That changes
  the value, which moves the slider again: a feedback loop.
* **Visual language.** Confidence is a percentage in five slightly different styles. Faces are
  square, rings are separate, colors are the only cue, and highlights in the world use colors no
  theme defines.

Method: every source file in `src/`, the preview harness, tests and docs was read again. All
targets were built (MSVC 19.44, 0 warnings), both test suites pass (100 + 44 expectations), and
the main screens were rendered in the preview harness and reviewed.

---

## 1. Current state audit

### 1.1 What works well and stays unchanged

| Area | Why it stays |
|---|---|
| Layering (`core` → `detection` → `game`/`ui` → `platform`) | Pure detection and UI code is testable outside the game |
| Evidence → confidence → gate model | Sound; a single outlier still can never produce a detection |
| Typed settings registry | One declaration drives defaults, persistence, UI and search |
| Profile structure (Summary · Evidence · Behavior · Identity) and the inspector | The strongest investigation surface |
| Motion language (`motion.h`) | Coherent; this pass adds to it rather than changing it |
| Privacy posture | No telemetry, no automatic reports, network only for identity tools |
| Runtime bootstrap (JVM attach, client pump, SwapBuffers hook, menu host screen) | Proven in game; changed only where compatibility requires it |
| Utilities, denickers, search, onboarding, Status page | Complete and correct apart from the specific issues below |

### 1.2 What feels unfinished

* Players from earlier games and lobbies stay in every list for the whole session.
* Confidence freezes when a player leaves view, then collapses to 0% when they come back (the
  decay clock never stopped). Neither behavior was designed.
* Watchlist pins live only in memory and disappear when the game restarts.
* NPCs (shopkeepers, lobby characters, Watchdog bots, replay duplicates) are treated as players.
* There is no notion of "this match": no start, no end, no summary.
* The user has no way to record their own conclusion about a player.

### 1.3 What should be redesigned

* Player identity visuals: circular faces with a confidence **halo** as the primary signal.
* One suspicion language (tiers, colors, shapes, words) shared by every surface.
* The roster: scoped to the current game, with earlier players one click away.
* Theme changes: a crossfade instead of a cut.
* The evidence inspector's default view: concise first, detail on demand.

### 1.4 What should be fixed

See §8 (bug audit) and §5 (detection audit).

---

## 2. UI audit

| # | Finding | Change |
|---|---|---|
| U1 | Square avatars, with the confidence ring drawn next to the face on toasts and the profile header: two objects for one idea | Circular face inside a halo everywhere (§9.4) |
| U2 | Percentages are the main cue. `0%` is painted teal on every quiet row, which is visual noise | Halo first; percentage only where a number helps (profile, inspector, drivers); quiet players get no colored text |
| U3 | Profile header truncates its own subtitle ("Observed 4m 07s · 600 evid…") because the right cluster reserves fixed width | Two-line identity block; actions grouped; ring replaced by the halo around the face |
| U4 | Pin icon reads as a key or lollipop; Reset's arrowhead floats off its arc; Shop icon contains a zero-length stroke | Redrawn on the 20-unit grid (§9.10) |
| U5 | Evidence summaries truncated in "Strongest evidence" and toasts ("Hit Nimbus while aimi…") | Shorter summaries written for the space they appear in (§9.11) |
| U6 | Number glyphs are proportional, so counters and live percentages jitter as they change | Tabular figures for numeric styles (§9.9) |
| U7 | Letter tracking is rounded per glyph, so spacing is uneven and measured widths don't match drawn widths (centered text off by 1–2 px) | Whole-pixel tracking, one rounding rule for measure and draw |
| U8 | Page header aligns the purpose line by line-box bottoms, not baselines | Baseline alignment helper |
| U9 | Suspect Highlight colors are hard-coded amber→red in the game layer and ignore the theme | Highlights come from the active theme's severity scale |
| U10 | Mask icon for nicks is 14 px at 50 % contrast, easy to miss | Part of the shared indicator set (§9.5) |

## 3. UX audit

| # | Friction | Change |
|---|---|---|
| X1 | "Who is in my game right now?" requires scanning a session-long list | Roster defaults to **This game**; Session and Marked are one click away |
| X2 | No match-start awareness: nicked, pinned or previously-detected players are discovered by accident | A concise match briefing on screen (and optionally in chat), only when there is something worth saying |
| X3 | Conclusions can't be recorded; pins are lost on restart | Persistent marks: Watch and **Confirmed by you** |
| X4 | Changing interface scale is erratic | Scale commits on release, quantized, with a short settle (§8 B1) |
| X5 | Theme switching is an abrupt cut | Interpolated crossfade, retargetable while scrubbing the theme editor |
| X6 | The inspector opens with a long paragraph and a formula | Default: what happened, why it matters, how confidence moved. Formula and raw numbers under "Details" |
| X7 | The roster mixes NPC entries ("#1234", shopkeepers) with players | NPCs are filtered and counted, never listed |
| X8 | Duplicate names (lobby NPCs wearing your name, replay copies) produce confusing twin rows | One row per person; collisions resolved (§9.3) |

---

## 4. Visualization audit

Surfaces that show suspicion today and how each shows it:

| Surface | Today | Problem |
|---|---|---|
| Roster row | Square face, colored `%`, trend | Color is the only cue; 0% colored |
| Profile header | Square face + separate ring with number | Two objects; ring competes with the name |
| Overview feed / watchlist | Square face + small ring stroke | Ring drawn as a rounded-rect stroke, not a ring |
| Toasts | Ring with number + 20 px square face | Two avatars |
| Timeline / evidence dots | Severity-colored dots | Fine; keep, but tier legend missing |
| World highlight | Hard-coded amber→red | Not the theme's palette; no shape cue |
| Chat alert | Color names `yellow/gold/red` | Color-only |

**Direction.** One component, the **player halo**: a circular face inside a ring whose arc length
is Vigil's confidence and whose weight and glow grow with the tier. Length and weight carry the
meaning without color; color reinforces it. The user's own verdict is a separate mark (a shield
badge in a dedicated "verdict" color that is not part of the severity scale), so Vigil's confidence
and the user's conclusion can never be confused.

---

## 5. Detection audit

Each finding is a **root cause**, with the scenario that turns it into a false positive. None of
the fixes loosen a threshold.

### 5.1 Velocity

| # | Root cause | False positive | Fix |
|---|---|---|---|
| V1 | The response window is a fixed 5 ticks from the hurt animation. The hurt animation arrives immediately, but the victim's knockback movement arrives only after the victim's round trip plus the server's 2-tick relay cadence | A 150 ms victim shows ~0 rise inside 5 ticks: "Rose 0.00 blocks" | Window anchored to the victim's latency (tab-list ping, or a conservative 4 ticks when unknown); evaluation requires position updates **after** the earliest possible response |
| V2 | Push is measured at exactly 3 ticks after the hit | Same latency problem, on the horizontal axis | Maximum displacement along the knockback direction within the latency-aware window |
| V3 | "Wall" is probed in four cardinal directions at one height | A wall diagonally behind the victim, or a block at head height, stops the push undetected | Eight directions at feet and head height; the check tests the octant the push points into |
| V4 | Attribution accepts the best-aimed nearby swinger even when the "hit" was an arrow, fire or poison tick | Burning or shot victims next to a swinging player look like no-knockback | Velocity uses only attributions within melee geometry (reach ≤ 3.6 to the hitbox, aim within the hitbox ± 15°) and skips burning victims |
| V5 | A lag gap inside the window (victim stops sending) reads as "did not move" | Lag spike right after a hit | Skip when the window has a gap of more than 4 ticks without updates |
| V6 | Severity ignores the victim's run-in speed (knockback halves existing velocity) | Weak but legitimate pushes when the victim sprinted into the hit | Expected push from vanilla physics given the pre-hit velocity; severity is measured ÷ expected |

### 5.2 Speed

| # | Root cause | False positive | Fix |
|---|---|---|---|
| S1 | Average speed uses the window's start **tick**, not the time the start position was actually received | After a 0.5–1 s lag catch-up, 30 ticks of movement fall into a 20-tick window: 1.5× apparent speed | Update-aware speed: distance between two *received* positions ÷ ticks between their arrivals |
| S2 | The movement multiplier is read from the last frame only | Speed II expires; the previous second at 1.4× is judged against 1.0× | Largest multiplier over the window (+10 ticks of momentum) |
| S3 | Server stalls deliver everyone's positions in bursts | Server lag spike: every moving player looks fast at once | World-level stall detection: when nearly every moving player stops updating and then jumps, all movement checks pause briefly |
| S4 | Invisible spectators (dead players waiting to respawn) fly freely; the flight exemption needs 40 airborne ticks | First 2 s of a spectator's flight | Invisible players are treated as flying after 10 non-falling airborne ticks (visible players keep 40) |
| S5 | Ceiling probe at one point above the feet center | Head-hitters at the edge of an overhang | Hitbox-corner ceiling probes (shared with BHop, §5.3) |

### 5.3 BHop

| # | Root cause | False positive | Fix |
|---|---|---|---|
| B1 | Ceiling probe at the feet center only, although the hitbox is 0.6 wide | Jumping at the edge of an overhang ends early: "low hop" | Probe every block the hitbox top touches (cached per block position, so it costs nothing while standing) |
| B2 | Start/end velocities come from 2-tick windows over positions that arrive every ~2 ticks with jitter | Bunched updates double a velocity: "Accelerated to 0.6 b/tick in the air"; an empty window zeroes it | 4-tick, update-aware velocity estimates; the air-control check requires regular data throughout the jump |
| B3 | Mid-air collisions with walls zero one velocity axis, which reads as a turn | Jumping along a wall | Collision signature (one axis stops dead while the other keeps speed) skips the strafe indicator |
| B4 | Hop-chain speed shares Speed's S1/S2 | As S1/S2 | Same fixes |

### 5.4 NoSlow

| # | Root cause | False positive | Fix |
|---|---|---|---|
| N1 | "Speed while using" uses a 4-tick raw displacement | A catch-up after lag while eating or blocking | Update-aware speed, plus a regular-data requirement |
| N2 | The item-use start is the client-side metadata flag, which can lag the server's state by the relay cadence | Speed measured too soon after use began | Momentum model: from the speed at use start, vanilla ground friction (×0.546/tick) gives the fastest legitimate speed at each later tick, so the limit tracks decay |
| N3 | Ice and slime are exempt only in the speed indicator | Slippery momentum during the sprint overlap | Momentum surfaces exempt both indicators |
| N4 | Multiplier from the last frame only | As S2 | As S2 |

### 5.5 Confidence formation

* Evidence carries its **data quality** (regularity of updates, latency known or assumed),
  folded into severity and shown in the inspector, so borderline data adds less confidence.
* A confidence that freezes while a player is away and then collapses on return is replaced by a
  defined rule (§6.3).

---

## 6. Relevance audit

### 6.1 Collection

`world.playerEntities` contains every `EntityPlayer`: real players, Hypixel NPCs (shopkeepers,
lobby characters, quest givers), Watchdog bots, the "your profile" NPC that wears *your* name and
skin, and replay entities. All of them become tracks, all are analyzed, all get roster rows.

### 6.2 Tracking

* Tracks are keyed by entity id, records by UUID. Two entities with the same UUID (respawn
  overlap, replay copies) feed **one** record from two tracks, which double-counts evidence.
* Records are created only while checks are active, so the people you queue with are invisible
  until the match begins.

### 6.3 Analysis and retention

* Session records are never pruned.
* A record's confidence stops decaying when the player leaves view (sampling stops), and the next
  `decayTo` after they return applies the whole absence at once, usually dropping straight to 0%.

**Decision.** Confidence measures what Vigil has observed. While a player is out of view nothing
is observed, so their confidence **holds**, shown as "when last seen". On return the clock resumes
without charging the absence. Across games (a different world), carried confidence is halved and
marked as carried over, because an earlier game's evidence is relevant but should not let a new
game's first few observations trigger a detection.

### 6.4 Display

Roster, Overview and search rank the whole session together. Default views should show the
current game; earlier players should stay reachable.

### 6.5 Lobby & match awareness

The context detector knows Lobby, Pre-game, In game and Replay from the scoreboard. It cannot see
**post-game** (the scoreboard is unchanged after a win), and nothing marks the start of a match.

| Phase | Entered when | Records | Checks run |
|---|---|---|---|
| Lobby | Lobby scoreboard | not recorded (noise) | off by default (setting) |
| Queue | Pre-game scoreboard ("Waiting…", "Starting in") | recorded: these are your opponents | off by default (setting) |
| Match | In-game scoreboard; the start is a Queue→Match transition or a new world already in game | recorded | on |
| Post-game | End-of-game chat lines (winner, "Reward Summary", "1st Killer") until the world changes | recorded | **off**: celebrations, fireworks and spectator flight look like cheats |
| Replay | Replay or Atlas scoreboard | recorded | on (setting) |

### 6.6 Bot / NPC audit

| Entity | How it looks to Vigil today | Signal that identifies it |
|---|---|---|
| Hypixel NPCs (shops, lobby characters) | A player that never moves | UUID version 2, absent from the tab list, or a name that is not a valid Minecraft name |
| "Your profile" NPCs | A second you | Same name as the local player, different UUID |
| Watchdog bots | Fast, erratic, often invisible "players" | Absent from the tab list while staying near players |
| Replay duplicates | Two tracks with one UUID | Duplicate UUID in the same frame |
| Name collisions | Twin rows | Same name, different UUID, one of them unlisted |

Tab-list presence is the strongest general signal. Hypixel lists real players, including nicked
ones, and removes NPC entries shortly after spawning them. It needs a grace period (entries arrive
slightly before or after the entity) and is ignored in replays, where tab contents differ.

---

## 7. Compatibility audit

| # | Finding | Risk | Change |
|---|---|---|---|
| C1 | The window-procedure guard re-subclasses the game window every second whenever another tool subclassed after Vigil. That tool's procedure chains to Vigil's, and Vigil's would chain back to it | **Infinite recursion** (stack overflow) with overlays that subclass the window (recorders, some injectables) | Re-hook only when Vigil is actually out of the chain, proven with a registered probe message |
| C2 | The SwapBuffers IAT hook patches every module, including other overlays' own imports | Vigil draws inside another overlay's present call | Skip modules that belong to known overlays and recorders; the game's presenter is unaffected |
| C3 | The RenderGlobal swap (Suspect Highlight) is installed at startup whenever the highlight is on, even if nobody is ever highlighted | Replaces a core renderer object for every session | Install lazily, the first time there is something to draw |
| C4 | Tab list, nametags, scoreboard teams and incoming chat are **not** modified | — | Keep: indicators are drawn by Vigil itself (§9.5) |
| C5 | Block probes allocate a `BlockPos` per probe per player per tick | Allocation pressure shared with the game | Probe cache per player (positions rarely change); corner probes add none while standing |
| C6 | Font atlases are created per pixel size and never freed | GPU and RAM growth while scaling or resizing | Bounded, evicted atlases (§8 B1) |

---

## 8. Bug audit

| # | Bug | Root cause | Fix |
|---|---|---|---|
| **B1** | **Interface scaling jumps and oscillates** | (1) The scale is re-applied every frame while its slider is dragged, so the slider moves under the stationary cursor and changes the value again; (2) mouse coordinates are converted to layout units with the scale at the moment of the event, so they are stale after a change; (3) the scale is continuous, so every value creates new 1024² font atlases that are never freed, and integer glyph metrics divided by a continuous scale make text widths jitter; (4) springs keep animating from old layout positions after the window moves | Pending value while dragging, committed on release (keyboard and wheel commit immediately); raw pixel mouse coordinates converted each frame; effective scale quantized to 1/20 steps; atlas eviction; springs snap when the layout scale or window size changes; a short settle animation so the change reads as intentional |
| **B2** | **Auto Who can fire after being turned off** | A `/who` scheduled (or retrying) when Auto Who is switched off stays pending. Turning it back on later sends it immediately, minutes after the game start | Disabling clears all Auto Who state (scheduled, retries, trigger cooldown); see §8.1 |
| B3 | Confidence freezes while away, then collapses on return | Decay clock not paused | §6.3 |
| B4 | Watchlist lost on restart | UI-only `std::set` | Persistent marks store |
| B5 | Duplicate-UUID tracks double-count evidence | Records keyed by UUID, tracks by entity id | One primary track per UUID |
| B6 | Window-procedure recursion risk | §7 C1 | §7 C1 |
| B7 | Text measured with fractional tracking but drawn with per-glyph rounding | §2 U7 | §2 U7 |

### 8.1 Auto Who audit

Every path that can send a command was traced (`sendChatMessage` has two callers: Auto Who's
`/who` and the explicit Report button's `/wdr`).

| Requirement when disabled | Before | After |
|---|---|---|
| No automatic actions | ✅ `update()` checks the toggle before sending | ✅ |
| No queued actions | ❌ a pending `/who` survives disable | ✅ cleared on disable |
| No delayed actions | ❌ the stale schedule fires on re-enable | ✅ |
| No retries | ⚠️ retries stop while disabled but resume on re-enable | ✅ cleared |
| No background triggers | ✅ `onChat` ignores lines while disabled | ✅ plus no cooldown carried over |
| No hidden functionality | ✅ nothing else sends `/who` | ✅ |

Two non-bug explanations for behavior seen "while disabled", documented for the user:
1. **Legacy import.** On the first run after upgrading, Vigil imports `autoWho=true` from the
   2.0 toggles file. Auto Who is then enabled, and the Utilities page shows it.
2. **Its shortcut.** A bound toggle key turns Auto Who on from the game. A notice appears, but it
   is easy to miss mid-fight.

Both are made more visible. Shortcut toggles now also log to the Utilities activity, and Status
lists every utility that is on.

---

## 9. Designs

### 9.1 Phase model and relevance (`src/detection/match.*`, engine)

A pure `MatchTracker` turns scoreboard context, world changes and chat lines into a **phase** and
a **scene** (a counter that advances on every world change and every match start). Records gain
`scene`, `phase` and `participant` (seen during Match). The engine records players in Queue,
Match, Post-game and Replay, and runs checks only where the phase allows. A player who re-enters
view resumes their confidence clock; entering a new scene halves carried confidence and adds a
timeline entry. Records from earlier scenes with no evidence and no marks are pruned beyond 400.

### 9.2 Entity classification (`src/detection/roster.*`)

`classifyEntity()` is a pure function over UUID version, name validity, tab-list presence (with
a 40-tick grace), invisibility, the local player's name, phase and duplicate status. It returns
**Player**, **NPC** or **Duplicate** plus a reason. Non-players are tracked only for
classification: no checks, no record, no combat attribution, no roster row, no highlight. The
roster footer shows "3 NPCs ignored" with the reasons in a tooltip, and Status counts them.

### 9.3 Duplicates

* Same UUID on two entities: the most recently updated one is primary; the other is a Duplicate.
* Same name, different UUIDs: the tab-listed one is the player; the other is an NPC
  ("name collision"). If both are listed (rare), both stay players and the rows show a short UUID
  suffix so they can be told apart.
* An entity with your name that is not you is an NPC ("uses your name").

### 9.4 Halo (`src/ui/suspicion.*`)

```
            ╭─────╮   ring track (faint, full circle)
          ╱  ●●●   ╲  confidence arc from 12 o'clock, clockwise; length = confidence
         │  (face)  │ weight: 1.5 px (low) → 3.5 px (critical), proportional to size
          ╲        ╱  glow: outer bloom from High; one pulse on a new detection
            ╰─────╯
               ⛨      verdict badge (user-confirmed), bottom-right, verdict color
```

One function, `playerHalo(ui, rect, …)`, draws faces in roster rows (34 px), feed and watchlist
(36 px), toasts (44 px), the match briefing (28 px) and the profile header (60 px). The circular
face is a textured disc (new `Renderer::imageDisc`) with an anti-aliased rim. When no skin is
loaded, initials sit on a neutral disc instead of a severity-tinted square.

### 9.5 Suspicion language

| Tier | Confidence | Word | Halo | Glyph (chat, world) |
|---|---|---|---|---|
| Clear | < 15 % | "No concern" | track only | none |
| Low | 15–40 % | "Low" | thin arc | none (avoids clutter) |
| Elevated | 40–65 % | "Elevated" | medium arc | `◇` |
| High | 65–85 % | "High" | heavy arc + glow | `◆` |
| Critical | ≥ 85 % | "Critical" | heaviest arc + glow | `⚠` |

User marks: **Watching** `◎` (accent color) and **Confirmed by you** `✖` with a shield badge
(verdict color, defined per theme). Nicked players `≈`. Each glyph has a distinct shape, so
indicators stay readable without color, under rank colors and under chat or tab modifications.
Color, when present, comes from the active theme.

Surfaces: roster, profile header, feed, watchlist, toasts, match briefing, chat lines Vigil
prints (alerts, briefing), Suspect Highlight colors, and an optional over-head marker drawn by
Vigil (ring with the tier glyph's shape) above players whose tier is High or higher, or who are
confirmed. The tab list and nametags are not modified (§7 C4).

### 9.6 Confirmed by you

* **Never automatic.** Only the Confirm action in a profile creates it.
* **Flow:** Confirm → a popover anchored to the button: "Confirm *Kestrel* as a cheater? This is
  your conclusion, kept separate from Vigil's confidence and stored only on this computer."
  Optional reason chips (the checks with evidence, plus Other) → **Confirm** / Cancel.
* **Presentation:** shield badge on the halo, a "Confirmed by you" chip, and in the verdict color
  (not part of the severity scale) wherever the player appears. Vigil's confidence is still shown
  beside it, unchanged.
* **Management:** clicking the chip shows when and why, with **Remove confirmation**. The roster's
  *Marked* scope lists every watched and confirmed player, including those not seen this
  session, with a remove action.
* **Watchlist:** confirmed players are always watched and are announced by the match briefing.
* **Storage:** `%LOCALAPPDATA%\Vigil\history\marks.json`, keyed by UUID (last known name kept).

### 9.7 Match briefing

Three seconds after a match starts (so the roster has loaded), Vigil checks who is in the game.
If any player is confirmed by you, watched, previously detected (history), or nicked, a compact
card appears at the top center for 7 s: a title ("Match briefing · 16 players"), up to four rows
(halo + name + marker + reason), and "+N more" when needed. Nothing is shown when there is nothing
notable. Setting `alerts.briefing`: Off / On screen (default) / On screen + chat.

### 9.8 Theme transition

`setTheme` becomes a target. Each frame the active theme is interpolated from the theme shown at
the moment of the change to the target (all colors, radii, glow and shadow strength). The curve is
emphasized decelerate over 420 ms; `light` flips at the midpoint. Changing again mid-transition
retargets from what is on screen. Rapid edits (the theme editor's sliders) use a 120 ms follow, so
dragging feels live rather than laggy. Motion "Off" is instant.

### 9.9 Typography

* Whole-pixel tracking with the same rule for measuring and drawing.
* Tabular figures for numeric styles (digits drawn on a fixed advance) so live numbers don't jitter.
* `Fonts::ascent()` for baseline alignment (page header, chips, mixed-size rows).
* Line heights snapped to whole device pixels at the quantized scale.
* Hierarchy check: one size step between adjacent levels. Overline only for section labels.

### 9.10 Iconography

Redrawn on the 20-unit grid at 1.6 stroke: **Reset** (counter-clockwise arc, arrowhead tangent to
the arc's end), **Pin** (push-pin: head, collar, needle), **Shop** (stray stroke removed). New:
**Shield** / **ShieldCheck** (confirm), **Bot** (NPC), **History** (earlier players), **Halo**
(suspicion language). Profile actions become one group with a uniform 34 px hit area and order
Copy · Watch · Confirm · Report.

### 9.11 Explanations

Pattern for every evidence detail: *what happened* (one sentence with the measurement) + *the
vanilla expectation* (one clause). Summaries ≤ 48 characters so they fit toasts and rows. Check
descriptions get a one-line summary plus "How it works" detail. The inspector shows the summary,
"Why it matters", and "+12 → 74 %". The formula and measured values sit under **Details**.

---

## 10. Risk assessment

| Risk | L | I | Mitigation |
|---|---|---|---|
| Tab-list signal unavailable or delayed on Lunar | M | M | Classification falls back to UUID and name rules; Status reports the signal; 40-tick grace before tab absence counts |
| A real player misclassified as an NPC (missed detections) | L | H | Conservative rules: tab absence needs ≥ 40 ticks *and* (UUID v2, invalid name, or the entity is invisible or never moves); never in replays; tests for nicked (v1) and normal (v4) players |
| Latency-aware Velocity misses cheaters | L | M | Cheats cancel knockback entirely, which no window length hides; the anti-KB test must still detect |
| Post-game end triggers misfire on player chat | L | L | Same "name:" prefix rule as Auto Who; the phase also resets on world change |
| Theme interpolation cost | L | L | ~60 float lerps per frame only while transitioning |
| Circular face rendering on host textures | M | L | Uses texture coordinates only, never texture parameters (as today); initials fallback |
| Over-head marker JNI fields absent | M | L | Resolved once; the feature disables itself with a Status line |
| Lazy RenderGlobal install delays the first highlight by a frame | H | L | Acceptable; the hook installs the moment it is first needed |
| WndProc probe misjudges chain membership | L | M | Probe is a posted registered message; with no answer after 2 s, re-hook as before |

## 11. Implementation roadmap (execution order)

| # | Work | Verification |
|---|---|---|
| 1 | Version 1.0-dev.8; this plan | — |
| 2 | Scaling fix (pending commit, raw mouse, quantize, atlas eviction, spring snap, settle) + typography (tracking, tabular, ascent) | Preview: scaled screenshots; scripted drag |
| 3 | Auto Who disable semantics | Feature tests |
| 4 | Track: update-aware speed/velocity, max multiplier, data regularity; sampler: ping, tab presence, burning, 8-way walls, hitbox-corner ceilings with cache | Detection tests (new falsifiable cases) |
| 5 | Velocity, Speed, BHop, NoSlow root-cause fixes; world stall exemption; concise explanations | Detection tests incl. latency, lag catch-up, expiring potion, edge ceiling, wall-jump, server stall |
| 6 | MatchTracker (phases, scenes), engine relevance (recording vs analysis, resume/carry-over, pruning) | Detection tests: phase transitions, carry-over, post-game quiet |
| 7 | Entity classification + duplicates in the engine | Detection tests: NPC v2, unlisted, your-name NPC, duplicate UUID, nicked player stays a player |
| 8 | Marks store (watch, confirmed) with persistence | Feature tests |
| 9 | Suspicion language + halo component + `imageDisc`; theme verdict color; icons | Preview screenshots in several themes |
| 10 | Roster scopes and sections, profile header + confirm flow, overview, toasts, match briefing card | Preview screenshots |
| 11 | Theme transition | Preview mid-transition frame |
| 12 | Inspector "Details", check descriptions | Preview |
| 13 | Game layer: phase/chat wiring, briefing chat line, alert glyphs, theme-colored highlights, over-head markers, lazy RenderGlobal, WndProc probe, overlay-aware IAT patching | Build; in-game checklist |
| 14 | Docs: QUALITY_REPORT.md (audits' outcomes, detection-quality report, final summary), README, release notes | Review |
| 15 | QA: full build (0 warnings), both suites, every page × themes × scales | Report |
