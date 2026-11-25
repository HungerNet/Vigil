# Technical debt report

## Resolved in 2.0

| Debt | Before | After |
|---|---|---|
| God function | `PumpMinecraftClientLogic`, 760 lines | `game::Runtime::pump`, an explicit pipeline of small steps |
| Monolithic bootstrap | `dllmain.cpp`, 4.1k lines | `platform/dllmain.cpp` (~150 lines) + `platform/hooks.cpp` |
| Single-function anticheat | `RunAnticheatChecks`, 1.0k lines | Framework + one file per check |
| Settings via Java static fields polled over JNI; JSON rewritten every 500 ms | ~30 `GetStaticFieldID` reads per pump | Typed native registry, debounced atomic saves |
| Lost Java sources | 5 classes only as bytecode | 3 retained classes recovered to `java/src`, verified instruction-for-instruction against the originals, reproducible build |
| Duplicate GUI sources + text-patch codegen | `VigilGui4.java` ↔ `VigilGui4Lunar.java` via `make_lunar_gui.py` | Removed (native UI) |
| Build-time bytecode string patching | `rename_embedded_java.py` | Removed |
| Network stack | libcurl fetched and built on every CI run | Removed; Vigil makes no network requests |
| Stringly-typed state | `g_redState == "Ingame"` | `detection::GameContext` enum |
| Silent `catch (...)` around checks | Errors swallowed | Checks are plain, testable code; failures surface in tests |
| Wall-clock sampling | 50 ms timer | One frame per client tick, skipped ticks counted |
| Untestable logic | No tests | 16 scenarios / 94 expectations; preview harness with screenshots |
| French comments, logs, errors | Throughout | English |
| Unbounded logs in the DLL folder | Several `vigil_*.log` | `vigil/logs/<channel>.log`, 4 MB rotation |
| Unconfirmed obfuscated-name fallbacks | Guessed names could bind to the wrong field | Only names confirmed by the mapping reference are used as fallbacks |
| Dead code carried from cosmetics | Dragon/fireball/team hitbox paths | Pruned |

## Remaining debt (known, accepted)

| Item | Why it remains | Suggested next step |
|---|---|---|
| `game/minecraft_util.cpp` still contains Bed Wars team-name helpers and some unused string utilities from the legacy module | Team-color resolution is tangled with them and proven in game | Extract a minimal `TeamResolver` and delete the rest once tested in game |
| `game/world_render.cpp` keeps the legacy JNI cache structure (some fields unused, e.g. health methods) | Proven RenderGlobal hook path; changes cannot be validated without a live client | Shrink the cache after in-game validation |
| No automated in-game integration test | Requires a Lunar client | A recorded-frames replay format (see roadmap) would let real sessions become regression tests |
| UI immediate-mode layout uses hand-tuned constants per page | Speed of delivery | Introduce row/column layout helpers if pages grow |
| Fonts are embedded as four static faces (~1.6 MB) | Static faces render reliably through GDI | Subset the fonts to Latin to cut size by ~70% |
| Scroll state map in `widgets.cpp` grows with unique ids | Bounded in practice (a few dozen) | Garbage-collect entries not used for N frames |
