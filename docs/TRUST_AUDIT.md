# Trust & antivirus audit

Goal: make Vigil as legitimate and transparent as a tool of its kind can be, and remove every
*unnecessary* reason for an antivirus engine to flag it. Not a goal: hiding from, evading or
bypassing antivirus software. Nothing in Vigil is obfuscated, packed, encrypted or disguised.

## What Vigil does that security software watches

| Behavior | Why Vigil needs it | Can it be avoided? |
|---|---|---|
| Opens the Lunar Client process with create-thread / VM-write rights | To load `Vigil.dll` into the game | **No** — it is the product's delivery mechanism |
| `CreateRemoteThread(LoadLibraryW)` | The documented, least exotic way to load a DLL into another process | **No** (alternatives such as manual mapping, APC queueing or thread hijacking are *more* suspicious and were rejected) |
| Writes a DLL to disk from an executable's resources | Single-file distribution (`Vigil.exe`) | Partly — see "Distribution" below |
| Patches `SwapBuffers` import-table slots inside the game | Draws the interface inside the game's frame | **No** for an in-game overlay; it is an IAT patch, not inline code patching |
| Subclasses the game window (`SetWindowLongPtr`) | Routes keyboard and mouse to the menu | **No** for an in-game menu |
| Defines small Java classes in the game's JVM | Hosts the menu screen and schedules work on the game thread | **No** |
| HTTPS requests (identity tools only) | Denickers | Yes — off unless enabled; only for nicked players |

## Changes made in this pass

| Area | Before | After | Effect on trust |
|---|---|---|---|
| Distribution | Zip with `VigilLauncher.exe` + `Vigil.dll` | One `Vigil.exe` with the DLL as a **plain, uncompressed** resource | Users run one signed-ready file; the payload stays scannable byte-for-byte |
| Extraction location | n/a (DLL beside launcher) | `%LOCALAPPDATA%\Vigil\runtime\Vigil-<version>.dll` | Fixed, versioned, human-readable path. Never `%TEMP%`, never a random name, never deleted right after use (all classic dropper traits) |
| Extraction behavior | n/a | Written once per version and reused when the content hash matches; older versions cleaned up only when not in use | No repeated drop-and-load pattern |
| Injection call | `LoadLibraryA`, ANSI path | `LoadLibraryW`, full Unicode path | Correct for every user profile path |
| Process rights | Minimal set | Same minimal set (+ `SYNCHRONIZE` to notice the game exiting) | No `PROCESS_ALL_ACCESS` |
| Memory | `PAGE_READWRITE` path buffer, freed after load | Unchanged | No executable allocations in the game, no shellcode |
| Success reporting | Message box as soon as `LoadLibrary` returned | Named event signaled by the DLL after its first rendered frame; failures signaled the same way | No OS popups; honest status |
| Elevation | Unspecified | Application manifest: `asInvoker`, DPI-aware, Windows 10/11 | Never asks for administrator rights |
| Metadata | Version info on both binaries | Full VERSIONINFO (company, product, description, original filename) on `Vigil.exe` and `Vigil.dll`, generated from `version.yaml` | Reputation systems can identify the product consistently |
| Network | None | WinHTTP (a Windows component), HTTPS only, two documented endpoints, generic user agent, no cookies | No bundled network stack; traffic is explainable |
| Logs | `vigil/logs` beside the DLL | `%LOCALAPPDATA%\Vigil\logs`, readable text | Every action is inspectable |
| Release | Zip | `Vigil.exe` + published SHA-256 | Users can verify what they downloaded |

## What remains unavoidable

* **Heuristic detections of process injection.** Any program that loads code into another process
  can be flagged by behavior-based engines, regardless of intent. Vigil uses the most
  conventional technique and documents it.
* **Resource-embedded executables.** A PE inside another PE is a common dropper pattern. The
  alternative — shipping a zip with two files — contradicts the "download one file" requirement.
  Vigil keeps the payload plain so engines can scan it, and the runtime path is stable.
* **Unsigned binaries.** Without a code-signing certificate, SmartScreen shows "Unknown
  publisher" and reputation starts at zero for every release.

## Recommendations (in order of impact)

1. **Sign both binaries with an Authenticode certificate** (an EV certificate gives immediate
   SmartScreen reputation). Sign `Vigil.dll` *before* it is embedded, then sign `Vigil.exe`.
   The build already produces stable metadata for this.
2. **Submit each release to Microsoft** at the Defender false-positive portal
   (microsoft.com/wdsi/filesubmission) as "Software developer — incorrectly detected". Include
   the SHA-256 published by CI.
3. **Keep releases reproducible** (CI builds from tagged sources; the SHA-256 is published with
   the release) so a submitted hash always matches what users download.
4. **Never add**: packers, string encryption, anti-debugging, sandbox checks, self-deletion,
   randomized file names, `%TEMP%` drops, or process-hollowing techniques. Each of these trades a
   short-term detection miss for a long-term reputation loss.
5. **Explain it to users** (README → "Antivirus"): what Vigil does, where it writes, and how to
   verify the download.

## Verified in this pass

* `Vigil.exe` contains the runtime's own version resource exactly once (byte search), and the
  executable is the DLL plus fonts and launcher code — no compression.
* Running `Vigil.exe` wrote exactly one file, `%LOCALAPPDATA%\Vigil\runtime\Vigil-1.0-dev.7.dll`,
  byte-identical in size to the built `Vigil.dll`.
* Both binaries import only Windows system libraries (`dumpbin /dependents`):
  `Vigil.dll` — KERNEL32, USER32, GDI32, OPENGL32, SHELL32, OLE32, WINHTTP;
  `Vigil.exe` — KERNEL32, USER32, GDI32, OPENGL32, SHELL32, OLE32, DWMAPI.
