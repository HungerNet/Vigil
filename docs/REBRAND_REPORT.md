# Rebrand report — Frenchify → Vigil

## Method

1. Text search of every tracked file for `frenchify` (any case), the color-split chat form
   (`Fre` + `nch` + `ify` separated by `§` codes or JSON segments), `Blackchify`, `ChatReader`
   and the legacy binary names.
2. Byte-level search of every embedded Java class (the `*.h` headers that carried compiled
   bytecode), decoding the hex arrays and scanning the constant pools.
3. Review of user-visible strings, logs, comments and CI for French-language remnants of the
   Frenchify era.
4. After implementation, the same scans were repeated on the source tree **and on the built
   binaries** (`Vigil.dll`, `VigilLauncher.exe`).

## References discovered

| # | Location | Reference | Kind |
|---|----------|-----------|------|
| 1 | `src/anticheat.cpp:529` | `§7[§1Fre§fnch§cify§7]` local chat prefix (French flag colors) | User-visible string |
| 2 | `src/anticheat.cpp:797-799` | JSON flag message split as `"Fre"` (dark_blue) / `"nch"` (white) / `"ify"` (red) | User-visible string (hidden — not found by plain-text search) |
| 3 | `src/dllmain.cpp:608, 618` | `/stats` error and usage messages with Frenchify prefix | User-visible string |
| 4 | `src/dllmain.cpp:3721, 3725` | "Invalid Hypixel/Seraph API key" messages | User-visible string |
| 5 | `src/dllmain.cpp:3782, 3813` | "Fetching stats…" / "Unable to fetch stats…" | User-visible string |
| 6 | `src/dllmain.cpp:3925, 3933` | "Party of N joined/quit" | User-visible string |
| 7 | `src/jni_helpers.cpp:1082` | Inject toast title | User-visible string |
| 8 | `src/party_queue_detector.cpp:381, 387` | Party queue announcements | User-visible string |
| 9 | `src/tab_overlay.cpp:2333, 4330, 4337` | Overlay chat prefix and announcements | User-visible string |
| 10 | `java_src/VigilGui4*.java` (×11 each) | "Blackchify" settings tab, `blackchifyScroll`, `drawBlackchifyPanels`, `handleBlackchifyPanelClicks` | UI text + identifiers |
| 11 | `java_src/lunar_headers/VigilGuiLunar.h` | "Blackchify" inside compiled bytecode | Binary resource |
| 12 | `scripts/rename_embedded_java.py:36-38` | Build-time bytecode patcher rewriting `Frenchify`→`Vigil` | Build tooling (rebrand by patching) |
| 13 | `CMakeLists.txt` (×10) | Target `ChatReaderLunar` | Build output name |
| 14 | `.github/workflows/release.yml:81` | Packaged `ChatReaderLunar.dll`, `InjectorLunar.exe` | Build output name |
| 15 | `src/injector.cpp:104-106` | `ChatReaderLunar.dll` / `ChatReader.dll` | Developer reference |
| 16 | `CMakeLists.txt` | `known-nicks.json` copy step "beside ChatReaderLunar.dll" | Developer reference |
| 17 | `config/LUNAR_MAPPINGS.md`, `CLASS_DEOBF.md`, `CLASS_OBFUSCATION.md` | French-language developer docs describing a `release_lunar/` workflow | Documentation (legacy era) |
| 18 | `src/*.cpp` comments, `dllmain.cpp` boot logs (`ECHEC inject`, `pas encore charge`, …), injector errors (`Jeu introuvable`, …) | French-language text | Comments, logs, user errors |

No Frenchify string existed in the other embedded classes (`VigilClientPump`, `VigilRenderGlobal`,
`VigilHitboxInstaller`, tab overlay, session HUD, packet log, chat hook).

## Changes performed

| Reference(s) | Change |
|---|---|
| 1-9 | Modules deleted (overlay/automation) or rewritten. The only chat prefix is now `§8[§9Vigil§8]` (`src/game/alerts.cpp`). Alerts are JSON components with hover evidence. |
| 10-11 | Java GUI deleted entirely; replaced by the native UI (`src/ui/`). No Java UI code remains. |
| 12 | Script deleted. The three retained Java shims were recovered to source (`java/src/`) and are compiled reproducibly by `tools/build_java.py`; no bytecode patching exists. |
| 13-16 | Targets renamed: `Vigil.dll`, `VigilLauncher.exe`; `known-nicks.json` removed with the denicker. CI rewritten. |
| 17 | French docs deleted; the structured obfuscation map kept as `docs/reference/mcp-obfuscation-map.json`. |
| 18 | All retained code, logs and user-facing errors are English. Encoding-damaged comments (mojibake) fixed. |
| — | Version info resources (`assets/vigil.rc`, `assets/launcher.rc`) brand the binaries as Vigil. |
| — | New identity: logo, app icon, palette, typography (see `BRAND.md`). |

## Verification

- `git grep -i -E "frenchify|blackchify|chatreader"` → only this report.
- Byte scan of `Vigil.dll` and `VigilLauncher.exe` for `Frenchify`, `Blackchify`, `ChatReader`,
  `seraph`, `api_hypixel` → no matches (only substrings such as `FreeLibrary`, `frequent`).
- Chat output: `[Vigil] Name · Check 82%` with hover evidence; no tri-color prefix remains.
