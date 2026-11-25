# Polish & maturity pass — report (1.0-dev.7)

The plan, with all audits as they stood before any code changed, is in [`PLAN.md`](../PLAN.md).
This report records what was done, how it was verified, and what still needs a live client.
Trust and antivirus findings are in [`TRUST_AUDIT.md`](TRUST_AUDIT.md).

---

## 1. Final implementation summary

| Area | Result |
|---|---|
| Navigation | Grouped rail (Monitor · Investigate · Configure), top bar with Back, breadcrumb and search, health card → Status. Evidence and Players merged into one profile. |
| Search | Ctrl+K / "/" palette over players (including names behind nicks), pages, profile tabs, checks, every setting, utilities, identity tools, recent evidence, themes and actions. Results deep-link and flash their target. |
| Injection confirmation | Launcher window that completes only on the runtime's ready signal; in-game activation card (mark ignites, four checks tick in, key hint); chat confirmation line. |
| Runtime status | Status page: health hero, injection & hooks, engine, signals (with the checks each feeds), utilities, identity & network, data, live log. Rail health card on every page. |
| Onboarding | Five-step introduction with live illustrations; skippable; replayable from About and search. |
| Hotkeys | Keybinds section, capture with Esc/Backspace, conflict detection against Vigil bindings and Minecraft defaults with one-click resolution, per-binding and global reset, built-in shortcut reference, utility toggles from the game. |
| Notifications | Redesigned: face, confidence ring, trend since first alert, strongest reason, confidence sparkline, key hint; repeat alerts fold into one card; critical alerts stay longer; configurable corner; menu key investigates the player on screen. |
| Confidence | Trends (Δ60 s) in roster, watchlist, profile; session chart; per-check drivers; milestone story; inspector shows how each observation formed confidence numerically. |
| Profiles | Roster with faces and filters (All · Flagged · In view · Nicked); profile header with actions (Report, Watch, Copy); Summary · Evidence · Behavior · Identity tabs. |
| Denickers | Skin denicker and number denicker restored inside the Identity tab, search and chat. |
| Utilities | Auto Who, Fast Buy, Party Detector, Party Queue Detector restored with cards, status, shortcuts and an activity log. |
| Motion | `src/ui/motion.h` — one motion language; see §5. |
| Themes | Verdant, Dusk, Paper (light), Signal (high contrast) + Custom theme editor (base, hue, tint, contrast, radius, glow). |
| Logo | Reconstructed geometry; SVG/ICO/PNG regenerated with true arcs. |
| Distribution | Single `Vigil.exe`; data in `%LOCALAPPDATA%\Vigil` with one-time migration. |
| Detection | Movement-modifier fixes (attribute, stale potions, head-hitters, stairs) with regression tests. |

New code: `src/utilities/` (logic), `src/identity/` (denickers), `src/net/` (WinHTTP),
`src/game/chat.*`, `src/game/utilities_runtime.*`, `src/ui/{hud,search,onboarding,page_status,
page_utilities,motion}`, `src/core/paths.*`, `tests/feature_tests.cpp`. The shell, players,
overview, settings, widgets, themes, icons, launcher and runtime were substantially rewritten.

## 2. Verification

| Check | Result |
|---|---|
| Full build, MSVC 19.44 `/W4 /permissive-`, all targets (`Vigil.dll`, `Vigil.exe`, preview, two test suites) | ✅ 0 errors, 0 warnings |
| Detection tests | ✅ 100 expectations, 0 failed (6 new) |
| Feature tests (utilities + identity) | ✅ 44 expectations, 0 failed (new suite) |
| New detection tests are falsifiable | ✅ each fix has a sanity case that *does* produce evidence without the fix |
| UI: every page, profile tabs, search, onboarding, HUD, activation, five themes rendered in the preview and reviewed | ✅ defects found and fixed (header overlap, truncated filters, clipped toast edges, privacy card overflow, search ranking, milestone accuracy) |
| `Vigil.exe` embeds the runtime; first run writes exactly one versioned file of the right size | ✅ |
| Imports: system libraries only | ✅ (`dumpbin /dependents`) |
| Obfuscated JNI fallbacks | ✅ only names confirmed by `docs/reference/mcp-obfuscation-map.json` or proven legacy code |

### Not verifiable here (needs Lunar Client)

The JNI paths added in this pass compile but have not run in a live client: movement attribute,
potion particles, profile textures and skins (faces), chat reader, Fast Buy shop members, entity
spawns, the ready/failed events, and the launcher end to end. Each resolves independently, logs
its result to `runtime.log`, and degrades with a visible reason on the Status page.

**In-game checklist**

1. Run `Vigil.exe` with Lunar Client open: the window walks through its steps and closes; in game
   the activation card appears with your menu key, and a chat line confirms.
2. Status shows all hooks green and every signal available (`runtime.log`:
   `sampler: … attribute=1 … profiles=1 skins=1`, `chat reader: resolved=1`).
3. Faces appear in the roster, watchlist and notifications after players have rendered.
4. Drink Speed II in front of a friend with "Analyze yourself" off on their side: no Speed
   evidence (attribute path); sprint-jump under a 2-block ceiling: no Speed/BHop evidence.
5. Utilities: Auto Who sends `/who` at game start; Party Detector reports a party joining a Bed
   Wars lobby; Fast Buy buys on left click in Quick Buy and nowhere else; Party Queue Detector
   reports consecutive-id parties on a busy server; shortcuts toggle each with a notice.
6. Identity: a nicked player shows "Hypixel nick" in Identity; a nick wearing their own skin
   resolves to an account name; with a Bordic key, final/bed counters narrow candidates.
7. Ctrl+K finds a player, a check, a setting (flashes it), and evidence; `/vigil search reach`
   and `/vigil status` open the right places.
8. Settings move from the old `vigil/` folder beside the DLL to `%LOCALAPPDATA%\Vigil`.

## 3. Regression audit — outcome

| # | Regression (PLAN.md §2) | Outcome |
|---|---|---|
| R1 | Utilities removed | ✅ Restored (§7) |
| R2 | Denickers removed | ✅ Restored (§8) |
| R3 | No chat reading | ✅ `ChatReader` (newest-line identity tracking; ignores Vigil's own lines; primes without replaying history) |
| R4 | Feature hotkeys removed | ✅ Unified keybinds; utilities toggle in game with a notice |
| R5 | OS popup instead of confirmation | ✅ Launcher window + activation card + chat line; failures reported in the launcher |
| R6 | Evidence/Players duplication | ✅ One profile, four tabs; every old deep link lands in the right tab |
| R7 | Noisy live feed | ✅ Grouped by player and check with counts |
| R8 | Limited search | ✅ Global search |
| R9 | Toasts led nowhere | ✅ Menu key investigates the player on screen |
| R10 | Loose known-nicks file | ✅ 228 hashes embedded |
| R11 | Slow fine-tuning | ✅ Wheel/arrow keys step sliders; per-check reset |
| R12 | Perpetual badge pulse | ✅ Counts players, pulses once per detection |

Also restored from 2.0 behavior: legacy toggles `autoWho`, `fastBuy`, `partyDetector` are
imported from the original settings file on first run.

## 4. Navigation, IA and UI/UX — outcome

* **Where am I?** Breadcrumb (section › page › player) and a single-line page header (title +
  purpose) on every page; the rail groups pages by intent.
* **How do I get back?** Back button and Backspace with a 32-entry history.
* **Where does it live?** Search reaches every setting and flashes it in place; the Settings
  sections now include Keybinds and Identity; Utilities have their own page.
* **Duplicates removed:** one player list (roster), one player header (profile), one place for
  per-check confidence (Summary drivers; Behavior keeps the 15-minute sparklines).
* **Evidence icon** redrawn as a document (it was indistinguishable from Settings' sliders).

## 5. Animation audit — outcome

All durations, rates and springs now come from `src/ui/motion.h`. Changes against the 2.0 audit:

| Interaction | Before | After |
|---|---|---|
| Menu open / close | 420 ms / 220 ms | 260 ms emphasized / 150 ms in-out — fast in, faster out |
| Page change | 280 ms fade + 10 px down | 200 ms, 12 px **in the direction of travel** |
| Profile tab change | — | 160 ms, 6 px |
| Hover | rate 16–18 everywhere | one quiet token (≈110 ms), never moves things |
| Press | linear 30-rate inset | stiff critically damped spring |
| Toggle | 420/22 spring | 560/25 — a crisper switch with ~6 % overshoot |
| Selection indicators (rail, segmented, tabs, settings nav) | mixed | one indicator spring |
| Tooltip | 350 ms delay, same rate in/out | 400 ms delay, 120 ms in, 70 ms out |
| Dropdown | fixed 6 px drop | 140 ms, 4 px from its anchor, direction-aware |
| Modals (search, onboarding) | — | 200 ms rise + backdrop fade |
| Toasts | spring in, fade out | spring in from the edge, 180 ms accelerate out, reflow springs, single bump on update |
| Attention | badge pulsed forever | one pulse per new detection |
| Lists | per-row fade | 180 ms entrance with travel |
| Reduced motion | partially honored | every token honors it (no travel, shorter tweens) |

## 6. Branding audit — outcome

* Mark reconstructed (constant arm width, lid concentric with the light, optical fillets);
  regenerated everywhere from one construction. See [`BRAND.md`](BRAND.md).
* One version string: `version.yaml` → generated header → UI, About, Status, launcher, VERSIONINFO.
* One user-facing file name: `Vigil.exe`.

## 7. Detection audit — outcome

| Finding | Fix | Test |
|---|---|---|
| Speed limits used the potion list, which other clients only receive when tracking starts; a Speed potion drunk in view was invisible (**false positive**: ground sprinting with Speed II is ~0.39 b/t vs a 0.31 limit) | Server-synced movement-speed attribute (sprint boost divided out) is the primary multiplier; potion particles without a known effect use the conservative allowance; the larger source wins | `stale potion uses movement attribute` (sanity + 2 fixed variants) |
| Head-hitter sprint-jumping exceeds the sprint-jump envelope (**false positive**) | Sustained/ground Speed and BHop hop-chain skip 20 ticks after a ceiling; ceiling is detected mid-jump too | `head-hitter jumping is quiet` (sanity + fixed) |
| Step-up jumps (stairs/slabs) shorten air time | 12 % tolerance for 20 ticks after a jump that lands ≥ 0.4 higher | covered by existing legit-movement test |
| Explanations didn't say which modifier applied | Evidence text names the multiplier and its source | — |
| Maintainability | Movement modifiers in one documented function (`speedMultiplier`/`speedMultiplierSource`) | — |

Other checks were reviewed; no further changes were needed beyond the shared multiplier
(NoSlow and Scaffold already call it).

## 8. Utility restoration report

| Utility | Legacy implementation | Now |
|---|---|---|
| Auto Who | Chat trigger list + 400 ms delay + 3 s cooldown + retries | Same triggers and timing (`utilities::AutoWho`), plus: player chat can no longer trigger it, delay is configurable, each send is logged |
| Fast Buy | WndProc swallowed left clicks in "Quick Buy"; replayed as `windowClick(…, 2, 3)` | Same mechanism (`UtilitiesRuntime::onMouse/update`), queue capped at 64, cleared 350 ms after the shop closes, stuck clicks dropped after 8 failures |
| Party Detector | Batched "has joined (x/y)" / "has quit!" lines in Bed Wars lobbies | Same batching (600 ms settle, de-duplicated names) with names listed in the activity log |
| Party Queue Detector | Netty packet hook (bytecode without sources) collecting spawn entity ids; `/locraw` for mode | Spawn ids now come from the sampler's player list (no packet hook, no lost bytecode); mode comes from the lobby sidebar ("Mode: Doubles"), so Vigil sends no extra commands; same run/chunk analysis and requeue hint |

All four are off by default, appear on the Utilities page with live status, have optional
shortcuts, and announce results in chat when "Announce in chat" is on.

## 9. Denicker integration report

| Piece | Legacy | Now |
|---|---|---|
| Nick detection | UUID version 1 in the tab list | Same rule, applied to every observed player |
| Hypixel nick skins | `config/known-nicks.json` beside the DLL | 228 hashes embedded (`src/identity/nick_skins.inc`) |
| Skin owner | Textures `profileId` → Mojang session server → Hypixel API check (key required) | Textures `profileId` → Mojang session server; no Hypixel key needed. The UI says plainly that a borrowed skin would point elsewhere |
| Number denicker | Finals/beds counters → Bordic with the user's key, candidates intersected | Same; fixed a race where results were declared final before both lookups completed (caught by a new test) |
| Presentation | Tab overlay + chat lines | Identity tab (account, skin denicker, number denicker, privacy), header chip, roster filter and mask icon, search by real name, chat line |
| Privacy | API keys in plaintext, many endpoints | Only two endpoints; lookups only for nicked players; skin lookups can be manual-only; Bordic key stored only in `config.json`; no analytics, telemetry, tracking or reporting |

## 10. Distribution redesign report

* **User experience:** download `Vigil.exe`, run it. It prepares itself, waits for Lunar Client
  if needed, starts Vigil, confirms in its own window and in game, and closes.
* **Runtime file:** `%LOCALAPPDATA%\Vigil\runtime\Vigil-<version>.dll`, written once per version,
  reused when identical, older versions removed when not in use.
* **Data:** `%LOCALAPPDATA%\Vigil\{config.json, history, sessions, logs}`; migrated once from the
  2.0 location (next to the DLL or next to the launcher). The old folder is left untouched.
* **Handshake:** `Local\Vigil.Ready.<pid>` / `Local\Vigil.Failed.<pid>` named events — the
  launcher reports success only after the runtime's first in-game frame and shows the failure
  reason otherwise (with an "Open logs" button). Manual injection still works (message boxes are
  used only when no launcher is listening).
* **Release:** CI publishes `Vigil.exe` and its SHA-256.

## 11. Known limitations and next steps

| Item | Note |
|---|---|
| In-game validation of new JNI paths | See checklist above |
| Faces depend on Minecraft having loaded a skin | Initials are shown until then |
| Number denicker requires a Bordic key | By design; the Identity tab explains how to enable it |
| Code signing | Strongest remaining trust improvement (TRUST_AUDIT.md) |
| Recorded-frame replays | Still the best path to turning real sessions into regression tests |
