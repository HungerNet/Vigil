## Vigil 1.0-dev.9 — records, keyboard, validation

Download **`Vigil-1.0-dev.9.exe`** below; `SHA256SUMS` lets you verify it. It is still the only
file you need.

**Records** — a new page under Investigate
- **Confirm, watch or exempt anyone**: a player from this game, from earlier in the session, or
  any username. A name you add follows that player's real account once Vigil sees them.
- **Exempt** friends and party members: they stay listed, but Vigil never judges them (no evidence,
  alerts, briefing or highlight).
- Search and review your confirmations: reasons you can edit, a private note, and what Vigil's
  confidence was when you confirmed.
- **Saved profiles**: save an investigation from a profile's More menu, export it to share, and
  import profiles from a file you copied in Explorer or from the imports folder.

**Clearer evidence**
- Each observation now says what was **expected** (vanilla, for this player's speed effects,
  ping or terrain), what was **observed**, and how far beyond the limit it was.
- Low-confidence findings read as "Unusual movement" or "Unusual combat" instead of naming a
  check before Vigil is confident.
- Detection notifications, the match briefing and "new detection" badges no longer follow you into
  the next game.

**Use Vigil without a mouse**
- Tab and Shift+Tab move through every control with a visible focus ring; Enter or Space activate;
  arrows adjust sliders and choices and move through lists.
- Type exact values: click a slider's number (or press Enter on the slider).
- New **Accessibility** settings: higher text contrast, focus ring, motion, and optional quiet
  interface sounds (off by default).

**Tidier**
- Denickers moved to **Utilities → Identity tools**, with their status and setup together.
- Checks list: a number shows each check's observations this session; a warning appears when a
  game signal it needs is missing.
- Status → **Diagnostics**: copy or save a report (no player names) and record a trace for bug
  reports.

**Safer updates**
- Vigil.exe verifies the runtime inside it and the copy it extracts, and tells you to restart Lunar
  Client if a different Vigil version is already running in the game instead of loading a second one.
- Settings written by another Vigil version are kept, and newer data files are never overwritten.
- Releases are published only after the full validation suite passes, and a version is never
  replaced once published.
