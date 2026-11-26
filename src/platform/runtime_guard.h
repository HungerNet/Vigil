#pragma once
// Vigil — runtime version protection (PLAN.md §10, docs/RELEASE_PROCESS.md).
//
// Three artifacts must agree: Vigil.exe, the Vigil.dll embedded in it, and the copy extracted to
// %LOCALAPPDATA%\Vigil\runtime. A fourth may already exist: a Vigil runtime loaded in the game by
// an earlier launch, possibly of another version. Every check compares the product version and
// the build ID that the build stamps into both binaries' version resources.
//
//   exe ↔ embedded      Vigil.exe --self-check, and before every extraction
//   embedded ↔ extracted SHA-256 of the file on disk equals the embedded bytes
//   extracted ↔ running  the launcher reads the runtime already in the game (shared info block,
//                        or the loaded module's version resource for runtimes before 1.0-dev.9)
//   running ↔ running    a per-process mutex: a second Vigil runtime never starts in one game

#include <windows.h>

#include <cstddef>
#include <string>

namespace vigil::platform {

struct RuntimeInfo {
    std::string version;   // "1.0-dev.9"
    std::string build;     // "<commit>.<time>", empty for runtimes older than 1.0-dev.9
    std::string path;      // module path when known
    bool known() const { return !version.empty(); }
};

// Reads ProductVersion and VigilBuild from a PE file's version resource.
bool readModuleVersion(const std::wstring& path, RuntimeInfo& out);
// Lower-case hex SHA-256 (BCrypt). Empty on failure.
std::string sha256Hex(const void* data, std::size_t size);

// ── Inside the game (Vigil.dll) ──────────────────────────────────────────────────────────────
// Claims "the Vigil runtime of this process" and publishes this runtime's version and build so a
// later launcher can read them. Returns false when another Vigil runtime already holds the claim;
// `holder` then receives what that runtime published (if anything).
bool claimRuntime(const char* version, const char* build, RuntimeInfo& holder);
// Writes a short reason into the launcher's failure channel (if the launcher created one).
void publishFailureReason(DWORD pid, const std::string& reason);

// ── In the launcher (Vigil.exe) ──────────────────────────────────────────────────────────────
// The Vigil runtime loaded in `pid`, if any: the shared info block first, then the module list
// (runtimes before 1.0-dev.9 only show up there).
bool findRunningRuntime(DWORD pid, RuntimeInfo& out);
// Creates the failure channel for `pid` (kept open by the returned handle) and reads it back.
HANDLE createFailureChannel(DWORD pid);
std::string readFailureReason(HANDLE channel);

enum class RuntimeMatch { Same, OtherVersion, OtherBuild, Unknown };
// How a running runtime relates to this launcher's version and build.
RuntimeMatch compareRuntime(const RuntimeInfo& running, const char* version, const char* build);

}  // namespace vigil::platform
