// VigilLauncher — loads Vigil.dll into a running Lunar Client (Minecraft 1.8.9) process.

#include <windows.h>
#include <tlhelp32.h>

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr const char* kDllName = "Vigil.dll";
constexpr const char* kTitle = "Vigil";

std::string exeDirectory() {
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string s = exe;
    const auto pos = s.find_last_of("\\/");
    return pos == std::string::npos ? std::string() : s.substr(0, pos + 1);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isGameWindowClass(const char* cls) {
    return cls && (std::strcmp(cls, "LWJGL") == 0 || std::strcmp(cls, "GLFW30") == 0 || std::strcmp(cls, "GLFW32") == 0);
}

int scoreWindow(HWND hwnd) {
    char title[256] = {};
    GetWindowTextA(hwnd, title, sizeof(title));
    const std::string t = lower(title);
    int score = 1;
    if (t.find("lunar") != std::string::npos) score += 100;
    if (t.find("minecraft") != std::string::npos) score += 40;
    if (t.find("1.8.9") != std::string::npos) score += 20;
    if (t.empty()) score -= 5;
    return score;
}

HWND findGameWindow() {
    struct Candidate { HWND hwnd; int score; };
    std::vector<Candidate> list;
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        if (!IsWindowVisible(hwnd)) return TRUE;
        char cls[64] = {};
        GetClassNameA(hwnd, cls, sizeof(cls));
        if (isGameWindowClass(cls)) reinterpret_cast<std::vector<Candidate>*>(lp)->push_back({hwnd, scoreWindow(hwnd)});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&list));
    HWND best = nullptr;
    int bestScore = -1000;
    for (const auto& c : list) if (c.score > bestScore) { bestScore = c.score; best = c.hwnd; }
    return best;
}

bool moduleLoaded(DWORD pid, const char* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32 me{};
    me.dwSize = sizeof(me);
    bool found = false;
    for (BOOL ok = Module32First(snap, &me); ok && !found; ok = Module32Next(snap, &me)) {
        found = _stricmp(me.szModule, name) == 0;
    }
    CloseHandle(snap);
    return found;
}

int fail(const char* message) {
    MessageBoxA(nullptr, message, kTitle, MB_ICONERROR | MB_TOPMOST);
    return 1;
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    HWND hwnd = findGameWindow();
    if (!hwnd) return fail("Lunar Client was not found.\nStart Minecraft 1.8.9 in Lunar Client first, then run VigilLauncher again.");

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return fail("Could not identify the game process.");

    const std::string dllPath = exeDirectory() + kDllName;
    if (GetFileAttributesA(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return fail("Vigil.dll was not found.\nKeep VigilLauncher.exe and Vigil.dll in the same folder.");
    }
    if (moduleLoaded(pid, kDllName)) {
        MessageBoxA(nullptr, "Vigil is already running in this game.\nPress Right Shift in game or type /vigil.", kTitle,
                    MB_ICONINFORMATION | MB_TOPMOST);
        return 0;
    }

    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                  PROCESS_VM_READ,
                              FALSE, pid);
    if (!proc) return fail("Could not open the game process.\nTry running VigilLauncher as administrator.");

    const SIZE_T len = dllPath.size() + 1;
    void* remote = VirtualAllocEx(proc, nullptr, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote || !WriteProcessMemory(proc, remote, dllPath.c_str(), len, nullptr)) {
        if (remote) VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);
        return fail("Could not prepare the game process.");
    }
    auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"));
    HANDLE thread = loadLibrary ? CreateRemoteThread(proc, nullptr, 0, loadLibrary, remote, 0, nullptr) : nullptr;
    if (!thread) {
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);
        return fail("Could not start Vigil inside the game.");
    }
    const DWORD wait = WaitForSingleObject(thread, 20000);
    DWORD exitCode = 0;
    GetExitCodeThread(thread, &exitCode);
    CloseHandle(thread);
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    CloseHandle(proc);
    if (wait != WAIT_OBJECT_0) return fail("Starting Vigil timed out after 20 seconds.");
    if (exitCode == 0) return fail("The game refused to load Vigil.dll.\nMake sure you downloaded the 64-bit release.");
    return 0;
}
