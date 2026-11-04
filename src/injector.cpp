#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <cstring>
#include <vector>
#include <cctype>

static std::string ExeDir() {
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string s = exe;
    const auto pos = s.find_last_of("\\/");
    if (pos != std::string::npos) return s.substr(0, pos + 1);
    return "";
}

static std::string ToLower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static bool IsGameWindowClass(const char* cls) {
    return cls && (strcmp(cls, "LWJGL") == 0 || strcmp(cls, "GLFW30") == 0 || strcmp(cls, "GLFW32") == 0);
}

static int ScoreGameWindow(HWND hwnd) {
    char title[256] = {};
    GetWindowTextA(hwnd, title, sizeof(title));
    const std::string t = ToLower(title);
    int score = 1;
    if (t.find("lunar") != std::string::npos) score += 100;
    if (t.find("minecraft") != std::string::npos) score += 40;
    if (t.find("1.8.9") != std::string::npos) score += 20;
    if (t.find("badlion") != std::string::npos) score -= 50;
    if (t.empty()) score -= 5;
    return score;
}

static HWND FindGameWindow() {
    struct Cand { HWND hwnd; int score; };
    std::vector<Cand> cands;

    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        if (!IsWindowVisible(hwnd)) return TRUE;
        char cls[64] = {};
        GetClassNameA(hwnd, cls, sizeof(cls));
        if (!IsGameWindowClass(cls)) return TRUE;
        auto* out = reinterpret_cast<std::vector<Cand>*>(lp);
        out->push_back({hwnd, ScoreGameWindow(hwnd)});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&cands));

    if (cands.empty()) {
        static const char* kClasses[] = {"LWJGL", "GLFW30", "GLFW32"};
        for (const char* cls : kClasses) {
            HWND hwnd = FindWindowA(cls, nullptr);
            if (hwnd) return hwnd;
        }
        return nullptr;
    }

    HWND best = cands[0].hwnd;
    int bestScore = cands[0].score;
    for (size_t i = 1; i < cands.size(); ++i) {
        if (cands[i].score > bestScore) {
            bestScore = cands[i].score;
            best = cands[i].hwnd;
        }
    }
    return best;
}

static bool ModuleAlreadyLoaded(DWORD pid, const char* moduleName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    wchar_t wideName[MAX_PATH] = {};
    MultiByteToWideChar(CP_ACP, 0, moduleName, -1, wideName, MAX_PATH);
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    bool found = false;
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, wideName) == 0) {
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

static void ShowInjectorError(const char* message) {
    MessageBoxA(nullptr, message, "Vigil Inject", MB_ICONERROR | MB_TOPMOST);
}

static void ShowInjectorInfo(const char* message) {
    MessageBoxA(nullptr, message, "Vigil Inject", MB_ICONINFORMATION | MB_TOPMOST);
}

int main()
{
#ifdef VIGIL_LUNAR_INJECTOR
    const char* dllName = "ChatReaderLunar.dll";
#else
    const char* dllName = "ChatReader.dll";
#endif

    HWND hwnd = FindGameWindow();
    if (!hwnd) {
        ShowInjectorError("Jeu introuvable.\nLance Minecraft / Lunar Client d'abord.");
        return 1;
    }

    DWORD procId = 0;
    GetWindowThreadProcessId(hwnd, &procId);
    if (!procId) {
        ShowInjectorError("Impossible de recuperer le PID du jeu.");
        return 1;
    }

    const std::string dllPath = ExeDir() + dllName;

    if (GetFileAttributesA(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        ShowInjectorError("DLL introuvable.\nPlacez l'injecteur et la DLL dans le meme repertoire.");
        return 1;
    }

    if (ModuleAlreadyLoaded(procId, dllName)) {
        ShowInjectorInfo("Deja injecte dans ce processus.\nRelance Lunar si tu veux reinjecter.");
        return 0;
    }

    HANDLE hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                               PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                               FALSE, procId);
    if (!hProc || hProc == INVALID_HANDLE_VALUE) {
        ShowInjectorError("Impossible d'ouvrir le processus.\nEssayez en administrateur.");
        return 1;
    }

    const size_t pathLen = dllPath.size() + 1;
    void* loc = VirtualAllocEx(hProc, nullptr, pathLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!loc) {
        ShowInjectorError("Echec d'allocation memoire dans le jeu.");
        CloseHandle(hProc);
        return 1;
    }

    if (!WriteProcessMemory(hProc, loc, dllPath.c_str(), pathLen, nullptr)) {
        ShowInjectorError("Echec d'ecriture memoire dans le jeu.");
        VirtualFreeEx(hProc, loc, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return 1;
    }

    // Resolve LoadLibraryA inside the target (same address as local for system DLLs).
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    auto pLoadLibraryA = (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "LoadLibraryA");
    if (!pLoadLibraryA) {
        ShowInjectorError("LoadLibraryA introuvable.");
        VirtualFreeEx(hProc, loc, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return 1;
    }

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, pLoadLibraryA, loc, 0, nullptr);
    if (!hThread) {
        ShowInjectorError("Echec de creation du thread distant.");
        VirtualFreeEx(hProc, loc, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return 1;
    }

    const DWORD wait = WaitForSingleObject(hThread, 20000);
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);
    VirtualFreeEx(hProc, loc, 0, MEM_RELEASE);
    CloseHandle(hProc);

    if (wait != WAIT_OBJECT_0) {
        ShowInjectorError("Injection timeout (20s).");
        return 1;
    }
    if (exitCode == 0) {
        ShowInjectorError("LoadLibrary a echoue dans le jeu.\nVerifie que la DLL est compatible.");
        return 1;
    }

    // Give MainThread a moment to attach JVM before the user assumes a ghost inject.
    Sleep(500);
    return 0;
}
