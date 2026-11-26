#include "runtime_guard.h"

#include <bcrypt.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace vigil::platform {
namespace {

constexpr std::size_t kInfoSize = 256;
constexpr std::size_t kReasonSize = 512;

std::wstring objectName(const wchar_t* kind, DWORD pid) {
    wchar_t name[96];
    swprintf_s(name, L"Local\\Vigil.%s.%lu", kind, pid);
    return name;
}

std::string narrow(const wchar_t* w) {
    if (!w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// "version\nbuild\n" in a small shared block.
bool parseInfo(const char* block, RuntimeInfo& out) {
    std::string text(block, strnlen(block, kInfoSize));
    const auto a = text.find('\n');
    if (a == std::string::npos) return false;
    out.version = text.substr(0, a);
    const auto b = text.find('\n', a + 1);
    out.build = text.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
    return !out.version.empty();
}

HANDLE g_runtimeMutex = nullptr;
HANDLE g_runtimeInfo = nullptr;

}  // namespace

bool readModuleVersion(const std::wstring& path, RuntimeInfo& out) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (size == 0) return false;
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return false;
    auto query = [&](const wchar_t* key) -> std::string {
        wchar_t sub[96];
        swprintf_s(sub, L"\\StringFileInfo\\040904b0\\%s", key);
        wchar_t* value = nullptr;
        UINT len = 0;
        if (!VerQueryValueW(data.data(), sub, reinterpret_cast<void**>(&value), &len) || !value || len == 0) return {};
        return narrow(value);
    };
    out.version = query(L"ProductVersion");
    out.build = query(L"VigilBuild");
    out.path = narrow(path.c_str());
    return !out.version.empty();
}

std::string sha256Hex(const void* data, std::size_t size) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
    unsigned char digest[32] = {};
    const bool ok = BCryptHash(alg, nullptr, 0, static_cast<PUCHAR>(const_cast<void*>(data)), static_cast<ULONG>(size), digest,
                               sizeof(digest)) == 0;
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) return {};
    char hex[65] = {};
    for (int i = 0; i < 32; ++i) std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return hex;
}

bool claimRuntime(const char* version, const char* build, RuntimeInfo& holder) {
    const DWORD pid = GetCurrentProcessId();
    HANDLE m = CreateMutexW(nullptr, FALSE, objectName(L"Runtime", pid).c_str());
    if (!m) return true;   // cannot tell; never block a start on an OS failure
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(m);
        if (HANDLE info = OpenFileMappingW(FILE_MAP_READ, FALSE, objectName(L"RuntimeInfo", pid).c_str())) {
            if (const char* view = static_cast<const char*>(MapViewOfFile(info, FILE_MAP_READ, 0, 0, kInfoSize))) {
                parseInfo(view, holder);
                UnmapViewOfFile(view);
            }
            CloseHandle(info);
        }
        return false;
    }
    g_runtimeMutex = m;   // held for the life of the process
    g_runtimeInfo = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kInfoSize,
                                       objectName(L"RuntimeInfo", pid).c_str());
    if (g_runtimeInfo) {
        if (char* view = static_cast<char*>(MapViewOfFile(g_runtimeInfo, FILE_MAP_WRITE, 0, 0, kInfoSize))) {
            std::snprintf(view, kInfoSize, "%s\n%s\n", version, build);
            UnmapViewOfFile(view);
        }
    }
    return true;
}

void publishFailureReason(DWORD pid, const std::string& reason) {
    HANDLE ch = OpenFileMappingW(FILE_MAP_WRITE, FALSE, objectName(L"FailReason", pid).c_str());
    if (!ch) return;
    if (char* view = static_cast<char*>(MapViewOfFile(ch, FILE_MAP_WRITE, 0, 0, kReasonSize))) {
        std::snprintf(view, kReasonSize, "%s", reason.c_str());
        UnmapViewOfFile(view);
    }
    CloseHandle(ch);
}

bool findRunningRuntime(DWORD pid, RuntimeInfo& out) {
    if (HANDLE info = OpenFileMappingW(FILE_MAP_READ, FALSE, objectName(L"RuntimeInfo", pid).c_str())) {
        bool ok = false;
        if (const char* view = static_cast<const char*>(MapViewOfFile(info, FILE_MAP_READ, 0, 0, kInfoSize))) {
            ok = parseInfo(view, out);
            UnmapViewOfFile(view);
        }
        CloseHandle(info);
        if (ok) return true;
    }
    // Runtimes before 1.0-dev.9 publish nothing; find their module and read its version resource.
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    bool found = false;
    for (BOOL ok = Module32FirstW(snap, &me); ok && !found; ok = Module32NextW(snap, &me)) {
        const std::wstring n = me.szModule;
        if (_wcsicmp(n.c_str(), L"Vigil.dll") == 0 || (n.size() > 10 && _wcsnicmp(n.c_str(), L"Vigil-", 6) == 0 &&
                                                       _wcsicmp(n.c_str() + n.size() - 4, L".dll") == 0)) {
            found = true;
            if (!readModuleVersion(me.szExePath, out)) {
                // Fall back to the version in the file name ("Vigil-1.0-dev.8.dll").
                std::string name = narrow(n.c_str());
                out.path = narrow(me.szExePath);
                if (name.size() > 10 && name.rfind("Vigil-", 0) == 0) out.version = name.substr(6, name.size() - 10);
                else out.version = "unknown";
            }
        }
    }
    CloseHandle(snap);
    return found;
}

HANDLE createFailureChannel(DWORD pid) {
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kReasonSize, objectName(L"FailReason", pid).c_str());
    if (h) {
        if (char* view = static_cast<char*>(MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, kReasonSize))) {
            view[0] = '\0';
            UnmapViewOfFile(view);
        }
    }
    return h;
}

std::string readFailureReason(HANDLE channel) {
    if (!channel) return {};
    std::string out;
    if (const char* view = static_cast<const char*>(MapViewOfFile(channel, FILE_MAP_READ, 0, 0, kReasonSize))) {
        out.assign(view, strnlen(view, kReasonSize));
        UnmapViewOfFile(view);
    }
    return out;
}

RuntimeMatch compareRuntime(const RuntimeInfo& running, const char* version, const char* build) {
    if (!running.known() || running.version == "unknown") return RuntimeMatch::Unknown;
    if (running.version != version) return RuntimeMatch::OtherVersion;
    if (!running.build.empty() && running.build != build) return RuntimeMatch::OtherBuild;
    return RuntimeMatch::Same;
}

}  // namespace vigil::platform
