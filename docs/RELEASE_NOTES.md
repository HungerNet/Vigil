## Vigil 1.0-dev.7 — polish & maturity

**One file.** Download `Vigil.exe` and run it. It starts Vigil inside Lunar Client, confirms
when Vigil is active, and closes. Settings move to `%LOCALAPPDATA%\Vigil` automatically.

**New**
- In-game activation card and a launcher that waits for Vigil to really be running.
- Status page: injection, hooks, detection engine, signals, services and a live log.
- Global search (Ctrl K or `/`): players, checks, settings, evidence, themes and actions.
- Player profiles: Summary (confidence over time, drivers, milestones), Evidence, Behavior,
  Identity — with player faces and confidence trends everywhere.
- Redesigned detection notifications; press the menu key while one is visible to investigate.
- First-run introduction (replayable from Settings → About).
- Keybinds with conflict detection; utility shortcuts work in game.
- Themes: Verdant, Dusk, Paper, Signal, and a Custom theme editor. Refined logo.

**Restored**
- Utilities: Auto Who, Fast Buy, Party Detector, Party Queue Detector.
- Denickers: skin denicker and number denicker (Bordic key), in the Identity tab.

**Improved detection**
- Speed, BHop and NoSlow follow each player's movement-speed attribute, so Speed potions drunk
  in view no longer cause false positives.
- Head-hitter jumping and stair climbing no longer look like Speed or BHop.

**Changed**
- Evidence and Players are now one page. Chat commands: `/vigil status`, `/vigil search <text>`.
