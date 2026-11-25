## Vigil 2.0.0

Vigil is now a cheat-detection and player-analysis platform.

**New**
- Detection framework with evidence, confidence, decay, evidence gates, timelines and history.
- New checks: Reach, AutoClicker, Velocity, Speed.
- Rebuilt checks: KillAura (rewritten), BHop (rewritten), NoSlow, AutoBlock, Scaffold family.
- Evidence Explorer, Players analysis, Overview dashboard, in-game notifications, Suspect
  Highlight, and chat alerts with hover evidence.
- Native interface with five themes, presets, tooltips and full configuration.
- `/vigil`, `/vigil <player>` and `/vigil reset`.

**Removed**
- Stats overlay, API keys and all third-party API integrations, denickers, Bed Wars macros and
  HUDs, party detectors. Vigil makes no network requests.

**Changed**
- Files: `Vigil.dll` and `VigilLauncher.exe`. Settings live in `vigil/config.json`; detection
  toggles are imported once from the old settings file.
