# Feature removal report

Vigil 2.0 removes every feature that does not serve detection, analysis, evidence,
investigation, debugging, visualization or configuration. Roughly 38,500 lines were deleted.

| Removed | Former files | Why it does not fit | Replacement |
|---|---|---|---|
| Tab stats overlay (5 modes, 18 columns, column editor, resource header, bed timers) | `tab_overlay.cpp/.h` (4.9k lines), Java GUI "Overlay" tab, `VigilTabOverlayNative` | Judges players by statistics, not behavior; overlay-first identity | Players page profiles behavior; Evidence Explorer explains suspicion |
| Hypixel API, Seraph, Aurora, Winstreak integrations, Vega proxy | `player_api.cpp`, `number_denicker.cpp`, parts of `tab_overlay.cpp`, `utils.cpp` HTTP | Third-party dependencies, network traffic, rate limits; unrelated to behavior | None needed — Vigil makes no network requests |
| API key entry, show/hide, storage, invalid-key notices | Java GUI "Settings" tab, `vigil_toggles.json` | Nothing consumes keys anymore; plaintext secrets on disk | Keys are not migrated and are no longer read |
| Skin-hash denicker + `known-nicks.json` | `denick.cpp`, `config/known-nicks.json` | Identity resolution via Mojang/Hypixel, privacy-sensitive, not behavior analysis | — |
| Number denicker | `number_denicker.cpp` | Aurora API; identity, not behavior | — |
| Chat overlay (stat thresholds) | `tab_overlay.cpp` | Flags players for being good, not for cheating | Evidence-based alerts |
| `/stats <player>` | `dllmain.cpp` | Stat lookup | `/vigil <player>` opens the evidence for a player |
| Auto /who, Auto GL, "Want to bet", Nick QoL | `dllmain.cpp` | Chat automation unrelated to detection | — |
| Fast Buy (left click → middle click replay) | `dllmain.cpp` | Rewrites game input; inappropriate for a detection product | — |
| Quick-buy hotkeys with turbo, hold render distance ("Blackchify") | Java GUI, `hud_edit.cpp` | Gameplay macros | — |
| Session stats HUD, HUD edit mode, trade indicator, resource timers | `session_stats.cpp`, `hud_edit.cpp`, `trade_indicator.cpp`, session HUD classes | Bed Wars cosmetics | In-game detection toasts |
| Party detector (chat) and party queue detector (packet hook) | `dllmain.cpp`, `party_queue_detector.cpp`, `VigilPacketLog` | Lobby intelligence, unrelated to behavior | — |
| Bow distance tracker | `utils.cpp` | Chat cosmetics | Reach evidence for melee |
| Colored hitboxes (team, dragon, fireball) | `colored_hitboxes.cpp` | Cosmetic | **Redesigned** as Suspect Highlight (`world_render.cpp`) |
| Java configuration GUI | `java_src/VigilGui4*.java` | Minecraft font, flat rectangles, no explanations | Native UI (`src/ui/`) |
| Native chat hook, packet log class | `VigilGuiNewChat.h`, `VigilPacketLog.h` | Only served removed features | — |
| libcurl (FetchContent) | `CMakeLists.txt` | Only served network features | — |
| Class-finder debug window | `class_finder_log.cpp`, `class_finder_obf_map.h` (163 KB) | Developer window and map used only for logging | Misses logged once to `vigil/logs/runtime.log` |
| Build-time bytecode patching | `scripts/*.py`, `scripts/*.ps1` | Rebrand-by-patch and lost sources | `java/src` + `tools/build_java.py` |

## Migration

On first start Vigil reads the legacy `vigil_toggles.json` (if present) and imports only the
detection toggles: `anticheat`, `ac_flagsound`, `ignoreTeammates`, `ac_killaura`, `ac_bhop`,
`ac_autoblock`, `ac_noslow`, `ac_scaffold`, `ac_scaffoldb`, `ac_legitscaff`. Everything else,
including API keys, is ignored. The legacy file is left untouched; users may delete it.
