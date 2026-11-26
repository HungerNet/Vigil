## Vigil 1.0-dev.8 — quality, relevance & UX

**Focused on the game you're in**
- Vigil now knows lobbies, queues, matches and post-game. Players are judged during matches (and
  replays); queue mates are listed but not judged; nothing is judged after the game ends.
- Players → **This game** by default, with Session, Marked and Nicked one click away.
- NPCs, Watchdog bots, the "your profile" NPC and duplicate entities are filtered out of every
  list, notification, highlight and check.
- **Match briefing:** a few seconds into each match, the players worth knowing about (confirmed,
  watched, detected before, nicked). Nothing appears when nobody stands out.
- Confidence now holds while a player is out of view, and carries into the next game at half.

**Seeing suspicion**
- Every player wears a **halo**: a circular face with a ring that fills and grows heavier with
  Vigil's confidence. Tier words (Low · Elevated · High · Critical) replace bare percentages.
- **Confirmed by you:** mark a player after investigating. It is your conclusion, shown in its own
  color, never set by Vigil and never mixed into its confidence. Watch players to pin them.
- Chat alerts carry shapes (`◆`, `⚠`, `✖`, `◎`) that read without colors. Suspect Highlight uses
  your theme's colors and can show a small marker above the player.

**Fewer false positives** (no thresholds were loosened)
- Velocity waits for the victim's latency before judging knockback.
- Speed, BHop and NoSlow ignore lag catch-ups and server stalls, remember a Speed effect that just
  ended, and see ceilings over the whole hitbox and walls in every direction.

**Polish**
- Interface scale no longer jumps while dragging: it applies when you release.
- Themes crossfade instead of switching abruptly.
- Clearer explanations; steadier numbers and spacing; refined icons.
- Auto Who: turning it off cancels anything it had scheduled.
- Better coexistence with other overlays and mods.
