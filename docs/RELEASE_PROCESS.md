# Release process

For maintainers. How a Vigil version goes from a commit to a published, verifiable download, and
what protects each step.

## 1. Publishing a version

1. Set `version` in `version.yaml` (`1.0-dev.N`), add the `Dev N:` changelog entry under
   `description`, and keep `run: true`.
2. Rewrite `docs/RELEASE_NOTES.md` for the version (its first lines must name it; it becomes the
   release body).
3. Merge to `master`. `release.yml` runs Validate, the release gate and Publish.
4. After publishing, run the live checklist ([LIVE_VALIDATION.md](LIVE_VALIDATION.md) §4).

`run: false` builds and validates without publishing. A version that is already published is
**never** replaced: the gate stops and the run summary says to bump the version.

## 2. Pipeline

```
push to master
   └─ Release
        ├─ Validate (validate.yml via workflow_call)
        │    lint ── metadata ── build (/WX, 5 suites, golden replay, self-check, hash) ── complete
        │                         └─ artifact "vigil-validated": Vigil.exe + selfcheck.json
        ├─ gate: run == true and tag does not exist
        └─ publish (contents: write, only job with write access)
             download artifact → self-check again → same runtime hash and version
             → Vigil-<version>.exe + SHA256SUMS → GitHub release → verify assets and size
```

Pull requests and other branches run Validate only (read-only token). Releases are serialized
(`concurrency: release`).

## 3. What each check prevents

| Risk | Check |
|---|---|
| A broken workflow discovered at release time | `actionlint` on every change |
| A version string that doesn't parse, a missing changelog entry, release notes for the wrong version | `validate_repo.py metadata` |
| Broken documentation links | `validate_repo.py links` |
| A compiler warning regression | `VIGIL_WARNINGS_AS_ERRORS=ON` (`/WX`) |
| A behavior change in detection | five suites + golden trace replay |
| `Vigil.exe` embedding a stale or foreign `Vigil.dll` | self-check (version + build ID of the embedded runtime) and SHA-256 of the embedded runtime == the DLL built in this run |
| A binary not built from the released commit | build ID must start with the commit SHA |
| Publishing something other than what was validated | publish downloads the validated artifact, re-runs the self-check, compares hash and version |
| Overwriting a published version with different bytes | gate refuses an existing tag |
| A release missing its files | `fail_on_unmatched_files` + post-publish asset check |
| An over-privileged token on pull requests | `contents: read` everywhere except the publish job |

## 4. Release files and naming

| File | Purpose |
|---|---|
| `Vigil-<version>.exe` | The only file users run |
| `SHA256SUMS` | `sha256  Vigil-<version>.exe`, verifiable with `Get-FileHash` or `sha256sum -c` |

**Why `Vigil-1.0-dev.9.exe`** (PLAN.md §10.2): GitHub replaces spaces in asset names with dots
(`Vigil 1.0-dev.9.exe` would download as `Vigil.1.0-dev.9.exe`), spaces need quoting in shells and
`%20` in links, and underscores read poorly next to version dots. A hyphen is safe everywhere,
sorts by version and reads naturally. Vigil never depends on its own file name (renaming the file is
harmless; the runtime is extracted under its version, not the exe's name).

## 5. Runtime version protection (what users see)

| Situation | Behavior |
|---|---|
| The runtime inside `Vigil.exe` is from another build (corrupt or tampered download) | Refuses: "This copy of Vigil.exe is damaged … Download it again." |
| `runtime\Vigil-<ver>.dll` exists with different bytes | Rewritten from the embedded copy (verified by SHA-256 and version resource) |
| …and it is locked by a running game | Reused only if it is this exact build; otherwise "Close Lunar Client and start Vigil again" |
| The same version and build already runs in the game | "Vigil is already running" (nothing injected) |
| Another version or build runs in the game | "Restart Lunar Client to update" (nothing injected) |
| A second runtime is loaded into the same game anyway (manual injection) | It refuses to start (per-process claim) and tells the launcher why |
| `config.json` written by another version | Unknown keys kept and written back |
| `marks.json` / saved profiles written by a newer format | Read-only / refused with a message; never overwritten |

## 6. Version numbers

`version.yaml` is the single source. CMake derives the numeric file version (`1.0.9`) for the
Windows version resource, the full string (`1.0-dev.9`) for the UI, logs and release, and the build
ID (`<commit>.<UTC configure time>`) stamped into both binaries' version resources (`VigilBuild`).
