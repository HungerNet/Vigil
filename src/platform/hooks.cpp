// Vigil platform — SwapBuffers IAT hook (UI render point) and WndProc subclass (input routing).
// The IAT technique and window subclassing are carried over unchanged from the proven legacy
// runtime; only their consumers changed.

#include "hooks.h"

#include "../core/log.h"
#include "../game/jvm.h"
#include "../game/runtime.h"

#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace vigil::platform {
namespace {

using SwapBuffersFn = BOOL(WINAPI*)(HDC);

HWND g_hwnd = nullptr;
WNDPROC g_origWndProc = nullptr;
SwapBuffersFn g_origSwap = nullptr;
std::vector<void**> g_swapSlots;
bool g_inSwap = false;

// Chain-membership probe. When another tool subclasses the window after Vigil, its procedure
// forwards to ours, so Vigil is still in the chain and must NOT subclass again: doing so would make
// our "original" procedure theirs, which forwards back to ours — infinite recursion. A registered
// message posted to the window proves whether our procedure still receives messages.
UINT g_probeMsg = 0;
std::atomic<ULONGLONG> g_probeSeen{0};
ULONGLONG g_probeSent = 0;

LRESULT CALLBACK hookedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_probeMsg && msg == g_probeMsg) {
        g_probeSeen = GetTickCount64();
        return 0;   // ours; never forwarded
    }
    LRESULT result = 0;
    if (game::Runtime::instance().onWindowMessage(hwnd, msg, wParam, lParam, result)) return result;
    return g_origWndProc ? CallWindowProcA(g_origWndProc, hwnd, msg, wParam, lParam) : DefWindowProcA(hwnd, msg, wParam, lParam);
}

// Modules of other overlays and recorders. Their own SwapBuffers imports are left alone: Vigil
// only needs the game's presenter, and drawing inside another overlay's present call would
// interfere with it.
bool isForeignOverlay(const char* moduleName) {
    std::string n = moduleName;
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static const char* kOverlays[] = {"graphics-hook", "obs", "discord", "overlay", "rtss", "nvspcap", "gameoverlay",
                                      "steam", "medal", "fraps", "bdcam", "action_x64", "reshade"};
    for (const char* o : kOverlays) if (n.find(o) != std::string::npos) return true;
    return false;
}

BOOL WINAPI hookedSwapBuffers(HDC hdc) {
    if (!g_inSwap && wglGetCurrentContext()) {
        g_inSwap = true;
        game::Runtime& rt = game::Runtime::instance();
        // The swap happens on Minecraft's thread: pump there too, so sampling never waits for a
        // scheduled task when the frame rate is high.
        if (JNIEnv* env = game::currentEnv()) {
            if (jobject mc = game::minecraft(env)) {
                rt.pump(env, mc);
                env->DeleteLocalRef(mc);
            }
        }
        rt.onSwapBuffers(hdc);
        g_inSwap = false;
    }
    return g_origSwap ? g_origSwap(hdc) : FALSE;
}

bool patchModule(HMODULE module) {
    auto* base = reinterpret_cast<BYTE*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || !dir.Size) return false;
    bool patched = false;
    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
        const char* dll = reinterpret_cast<const char*>(base + imp->Name);
        if (_stricmp(dll, "GDI32.dll") != 0) continue;
        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        auto* orig = imp->OriginalFirstThunk ? reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk) : thunk;
        for (; orig->u1.AddressOfData; ++orig, ++thunk) {
            if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)) continue;
            auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + orig->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(ibn->Name), "SwapBuffers") != 0) continue;
            const auto hook = reinterpret_cast<ULONG_PTR>(&hookedSwapBuffers);
            if (thunk->u1.Function == hook) continue;
            DWORD old = 0;
            if (!VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) continue;
            if (!g_origSwap) g_origSwap = reinterpret_cast<SwapBuffersFn>(thunk->u1.Function);
            thunk->u1.Function = hook;
            DWORD dummy = 0;
            VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &dummy);
            g_swapSlots.push_back(reinterpret_cast<void**>(&thunk->u1.Function));
            patched = true;
        }
    }
    return patched;
}

}  // namespace

HWND findGameWindow() {
    struct Ctx { DWORD pid; HWND best; int score; } ctx{GetCurrentProcessId(), nullptr, -1};
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != c->pid || !IsWindowVisible(hwnd)) return TRUE;
        char cls[64] = {};
        GetClassNameA(hwnd, cls, sizeof(cls));
        if (std::strcmp(cls, "LWJGL") != 0 && std::strncmp(cls, "GLFW", 4) != 0) return TRUE;
        int score = 0;
        score += 100;
        char title[256] = {};
        GetWindowTextA(hwnd, title, sizeof(title));
        std::string lowerTitle = title;
        std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (lowerTitle.find("lunar") != std::string::npos) score += 1000;
        if (lowerTitle.find("minecraft") != std::string::npos) score += 500;
        if (lowerTitle.find("1.8.9") != std::string::npos) score += 200;
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const long long area = static_cast<long long>(rc.right - rc.left) * (rc.bottom - rc.top);
        if (area > 0) score += 10 + static_cast<int>(std::min<long long>(area / 100000, 50));
        if (score > c->score) { c->score = score; c->best = hwnd; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.score >= 100 ? ctx.best : nullptr;
}

bool installWindowHook(HWND hwnd) {
    if (!hwnd) return false;
    g_hwnd = hwnd;
    char cls[64] = {};
    char title[256] = {};
    GetClassNameA(hwnd, cls, sizeof(cls));
    GetWindowTextA(hwnd, title, sizeof(title));
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    log::writef("runtime", "window hook target: hwnd=%p pid=%lu class=%s title=%s",
                static_cast<void*>(hwnd), pid, cls, title);
    SetLastError(0);
    auto prev = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&hookedWndProc)));
    const DWORD error = GetLastError();
    if (!prev && error != 0) {
        log::writef("runtime", "window procedure hook failed: error %lu", error);
        return false;
    }
    if (prev && prev != &hookedWndProc) g_origWndProc = prev;
    return prev == &hookedWndProc || g_origWndProc != nullptr;
}

void ensureWindowHook() {
    static ULONGLONG lastWindowSearch = 0;
    const ULONGLONG now = GetTickCount64();
    if (!g_hwnd || now - lastWindowSearch >= 1000) {
        lastWindowSearch = now;
        if (HWND current = findGameWindow(); current && current != g_hwnd) {
            g_hwnd = current;
            g_origWndProc = nullptr;
            char cls[64] = {};
            char title[256] = {};
            GetClassNameA(current, cls, sizeof(cls));
            GetWindowTextA(current, title, sizeof(title));
            log::writef("runtime", "game window selected: hwnd=%p class=%s title=%s",
                        static_cast<void*>(current), cls, title);
        }
    }
    if (!g_hwnd) return;
    auto cur = reinterpret_cast<WNDPROC>(GetWindowLongPtrA(g_hwnd, GWLP_WNDPROC));
    if (cur != &hookedWndProc && g_origWndProc) {
        // Someone subclassed after us. If our procedure still receives messages we are in the chain
        // (they forward to us) and must leave it alone; re-hook only once a probe goes unanswered.
        if (!g_probeMsg) g_probeMsg = RegisterWindowMessageA("Vigil.WndProc.Probe");
        if (g_probeSent == 0 || now - g_probeSent > 2000) {
            if (g_probeSent != 0 && g_probeSeen.load() < g_probeSent) {
                log::write("runtime", "window procedure: probe unanswered, Vigil left the chain; re-hooking");
                g_probeSent = 0;
            } else {
                g_probeSent = now;
                PostMessageA(g_hwnd, g_probeMsg, 0, 0);
                return;
            }
        } else {
            return;
        }
    }
    if (cur != &hookedWndProc) {
        SetLastError(0);
        auto prev = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&hookedWndProc)));
        const DWORD error = GetLastError();
        if (!prev && error != 0) {
            static ULONGLONG lastFailureLog = 0;
            if (now - lastFailureLog >= 2000) {
                log::writef("runtime", "window procedure re-hook failed: error %lu", error);
                lastFailureLog = now;
            }
            return;
        }
        if (prev && prev != &hookedWndProc) g_origWndProc = prev;
        else if (!g_origWndProc) g_origWndProc = cur;
        log::write("runtime", "window procedure hooked");
    }
}

void removeWindowHook() {
    if (g_hwnd && g_origWndProc) SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origWndProc));
    g_hwnd = nullptr;
}

bool installPresentHook() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32 me{};
    me.dwSize = sizeof(me);
    for (BOOL ok = Module32First(snap, &me); ok; ok = Module32Next(snap, &me)) {
        if (isForeignOverlay(me.szModule)) continue;
        patchModule(me.hModule);
    }
    CloseHandle(snap);
    if (!g_origSwap) {
        if (HMODULE gdi = GetModuleHandleA("gdi32.dll")) g_origSwap = reinterpret_cast<SwapBuffersFn>(GetProcAddress(gdi, "SwapBuffers"));
    }
    const bool installed = !g_swapSlots.empty();
    static bool reportedUnavailable = false;
    if (installed) {
        log::writef("runtime", "present hook: %d import slots patched", static_cast<int>(g_swapSlots.size()));
        reportedUnavailable = false;
    } else if (!reportedUnavailable) {
        log::write("runtime", "present hook: no SwapBuffers import slots found; will retry");
        reportedUnavailable = true;
    }
    return installed;
}

void ensurePresentHook() {
    static ULONGLONG lastTry = 0;
    if (!g_swapSlots.empty() || GetTickCount64() - lastTry < 2000) return;
    lastTry = GetTickCount64();
    installPresentHook();
}

int presentHookSlots() { return static_cast<int>(g_swapSlots.size()); }

bool windowHookActive() {
    return g_hwnd && reinterpret_cast<WNDPROC>(GetWindowLongPtrA(g_hwnd, GWLP_WNDPROC)) == &hookedWndProc;
}

void removePresentHook() {
    for (void** slot : g_swapSlots) {
        DWORD old = 0;
        if (VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
            *slot = reinterpret_cast<void*>(g_origSwap);
            DWORD dummy = 0;
            VirtualProtect(slot, sizeof(void*), old, &dummy);
        }
    }
    g_swapSlots.clear();
}

}  // namespace vigil::platform
