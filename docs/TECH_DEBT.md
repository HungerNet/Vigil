# Technical debt report

Last reviewed in the Rust P1/P2 pass. Long-term direction: [RUST_PLAN.md](../RUST_PLAN.md).

## Resolved in Rust P1 (engine configuration)

| Debt | Before | After |
|---|---|---|
| Checks read thresholds through `Setting*` into the global registry | 248 live pointers into `Settings::instance()`; the engine's behavior depended on process-wide state | Immutable `EngineConfig` snapshot handed over between frames; only `detection/config_binding.*` knows the registry (CI-enforced) |
| Traces could only be replayed with default settings | — | `VigilReplay --config`; configuration documents round-trip exactly |

## Found in Rust P1, left as they are (behavior-preserving pass)

| Item | Effect | Suggested fix |
|---|---|---|
| `Setting::reset()` snaps to the step, but registration stores the unsnapped default. Indicator weights are declared as `float` (`0.8f` = 0.80000001192…), so "Reset check" stores 0.8, and `isDefault()` (1e-9 tolerance) then reports those weights as modified | After "Reset check", the reset button can stay visible and weights show as changed; detection unaffected beyond a 1e-8 weight difference | Declare weights as exact doubles, or make `isDefault()` compare after snapping; a deliberate change with its own golden review |
| `Settings::load()` assigns a JSON boolean to any setting type without clamping, and `strtod` accepts `nan`/`inf` | A hand-edited `config.json` can put out-of-range or non-finite values in the registry (the engine then runs with them) | Type-check booleans and refuse non-finite numbers on load |

## Introduced deliberately in Rust P2 (accepted)

| Item | Why | When to revisit |
|---|---|---|
| Two implementations of every file format (C++ and `vigil-formats`) | The hybrid period (RUST_PLAN §6); both are held to one contract by shared fixtures and CI round trips | Delete the C++ readers one release after the Rust side owns each format |
| A hand-written JSON layer in Rust instead of `serde` | Byte-identical output needs the C++ number rule and byte strings; zero dependencies | When no C++ writer remains, consider `serde` for new formats |
| Rust refuses hexadecimal float literals that the C++ parser (`strtod`) accepts | No Vigil writer produces them; documented in `tests/fixtures/formats/divergences.txt` | Never, unless a format starts using them |
| `.gitattributes` (with `tests/traces/** -text`) is untracked in this checkout | It protects the golden trace's exact bytes; every reader and test also tolerates `\r` | Commit it, and add `tests/fixtures/** -text` so CRLF test inputs keep their bytes |

## Resolved in 1.0-dev.9

| Debt | Before | After |
|---|---|---|
| One library for pure and service code | `vigil_core` held detection, WinHTTP and identity threads | `vigil_core` / `vigil_detection` / `vigil_services` / `vigil_ui`, enforced by the linker |
| Copy-pasted test harness | Two suites with their own `EXPECT`, globals and `main` | `tests/test_framework.h`; simulator shared in `tests/sim.h` |
| No recorded-input regression path | Real sessions could not become tests | Trace format, `VigilReplay`, golden trace in CI, in-game recording |
| JSON lost precision (`%.6g`) | Values written and read back changed | Shortest exact representation |
| Settings dropped unknown keys | A downgrade or side-by-side version lost settings | Unknown keys kept and written back; `writtenBy` recorded |
| Marks overwritten regardless of format | An older build could destroy newer data | Format-guarded, read-only when newer or unreadable |
| Stale embedded runtime undetectable | NMake mtime trap could ship an old DLL silently | Build ID + self-check + hash comparison in CI |
| Release job with a write token on every run | — | Read-only by default; write only when publishing |
| Expectations implicit in prose | "the limit with 1.40x movement is …" | `EvidenceValue::Role` + derived `Envelope` |

## Resolved in 2.0

| Debt | Before | After |
|---|---|---|
| God function | `PumpMinecraftClientLogic`, 760 lines | `game::Runtime::pump`, an explicit pipeline of small steps |
| Monolithic bootstrap | `dllmain.cpp`, 4.1k lines | `platform/dllmain.cpp` + `platform/hooks.cpp` |
| Single-function anticheat | `RunAnticheatChecks`, 1.0k lines | Framework + one file per check |
| Settings via Java static fields polled over JNI | ~30 `GetStaticFieldID` reads per pump | Typed native registry, debounced atomic saves |
| Lost Java sources | 5 classes only as bytecode | 3 retained classes recovered to `java/src`, reproducible build |
| Network stack built on every CI run | libcurl | WinHTTP for the identity tools only |
| Stringly-typed state | `g_redState == "Ingame"` | `detection::GameContext` enum |
| Untestable logic | No tests | Suites + preview harness |

## Remaining debt (known, accepted)

| Item | Why it remains | Suggested next step |
|---|---|---|
| UI reads `PlayerRecord`s straight from the engine | Simple and fast in one process | When the engine moves behind an ABI: a revision-based record mirror (RUST_PLAN §6.1) |
| `game/minecraft_util.cpp` still holds Bed Wars team-name helpers and unused legacy string utilities | Team-color resolution is tangled with them and proven in game | Extract a `TeamResolver`; delete the rest after live validation |
| `game/world_render.cpp` keeps the legacy JNI cache structure | Proven RenderGlobal hook path | Shrink after live validation |
| Immediate-mode layout uses hand-tuned constants per page | Speed of delivery | Row/column helpers if pages keep growing |
| Focus order follows drawing order | Matches reading order on every page today | Explicit tab groups if a page ever draws out of reading order |
| Fonts embedded as four static faces (~1.6 MB) | GDI renders static faces reliably | Subset to Latin (~70 % smaller) |
| Scroll state map in `widgets.cpp` grows with unique ids | Bounded in practice | Collect entries unused for N frames |
| UI smoke renders are informational in CI | Hosted runners have software OpenGL 1.1 only | Promote to required after a stable streak |
| No code signing | No certificate | Sign releases when one is available; SHA256SUMS meanwhile |
