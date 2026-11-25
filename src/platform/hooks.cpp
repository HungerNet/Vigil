// Vigil platform — SwapBuffers IAT hook (UI render point) and WndProc subclass (input routing).
// The IAT technique and window subclassing are carried over unchanged from the proven legacy
// runtime; only their consumers changed.

#include "hooks.h"

#include "../core/log.h"
#include "../game/jvm.h"
#include "../game/runtime.h"

#include <tlhelp32.h>

#include <cstring>
#include <vector>

namespace vigil::platform {
namespace {

using SwapBuffersFn = BOOL(WINAPI*)(HDC);

HWND g_hwnd = nullptr;
WNDPROC g_origWndProc = nullptr;
SwapBuffersFn g_origSwap = nullptr;
std::vector<void**> g_swapSlots;
bool g_inSwap = false;

LRESULT CALLBACK hookedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LRESULT result = 0;
    if (game::Runtime::instance().onWindowMessage(hwnd, msg, wParam, lParam, result)) return result;
    return g_origWndProc ? CallWindowProcA(g_origWndProc, hwnd, msg, wParam, lParam) : DefWindowProcA(hwnd, msg, wParam, lParam);
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
        int score = 0;
        if (std::strcmp(cls, "LWJGL") == 0 || std::strncmp(cls, "GLFW", 4) == 0) score += 100;
        RECT rc{};
        GetClientRect(hwnd, &rc);
        score += (rc.right - rc.left) * (rc.bottom - rc.top) > 0 ? 10 : 0;
        if (score > c->score) { c->score = score; c->best = hwnd; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.score >= 100 ? ctx.best : nullptr;
}

bool installWindowHook(HWND hwnd) {
    if (!hwnd) return false;
    g_hwnd = hwnd;
    auto prev = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&hookedWndProc)));
    if (prev && prev != &hookedWndProc) g_origWndProc = prev;
    return g_origWndProc != nullptr;
}

void ensureWindowHook() {
    // Some clients reinstall their window procedure (fullscreen toggles); re-subclass if so.
    if (!g_hwnd) return;
    auto cur = reinterpret_cast<WNDPROC>(GetWindowLongPtrA(g_hwnd, GWLP_WNDPROC));
    if (cur != &hookedWndProc) {
        g_origWndProc = cur;
        SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&hookedWndProc));
        log::write("runtime", "window procedure re-hooked");
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
    for (BOOL ok = Module32First(snap, &me); ok; ok = Module32Next(snap, &me)) patchModule(me.hModule);
    CloseHandle(snap);
    if (!g_origSwap) {
        if (HMODULE gdi = GetModuleHandleA("gdi32.dll")) g_origSwap = reinterpret_cast<SwapBuffersFn>(GetProcAddress(gdi, "SwapBuffers"));
    }
    log::writef("runtime", "present hook: %d import slots patched", static_cast<int>(g_swapSlots.size()));
    return !g_swapSlots.empty();
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
