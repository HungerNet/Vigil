// Vigil.exe — the only file users run.
//
//   1. Prepares Vigil: writes the embedded Vigil.dll to %LOCALAPPDATA%\Vigil\runtime\ (once per
//      version, verified by content hash; never to Temp, never under a random name).
//   2. Finds Lunar Client (Minecraft 1.8.9), waiting politely if it is not running yet.
//   3. Loads Vigil.dll into the game with the standard LoadLibrary technique.
//   4. Waits until Vigil reports that it is running (first frame rendered in game), then closes.
//
// The window uses the same renderer, typography and theme as the in-game interface.

#include "../core/json.h"
#include "../core/paths.h"
#include "../ui/font.h"
#include "../ui/gfx.h"
#include "../ui/icons.h"
#include "../ui/motion.h"
#include "../ui/resources.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"
#include "core/version.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <tuple>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace vigil;
using namespace vigil::ui;

namespace {

// ── Shared state between the worker and the window ───────────────────────────────────────────

enum class StepState { Pending, Active, Done, Failed };
struct Step {
    const char* title;
    std::string detail;
    StepState state = StepState::Pending;
    double changedAt = 0.0;
};

struct Shared {
    std::mutex mu;
    Step steps[4] = {{"Preparing Vigil", ""}, {"Finding Lunar Client", ""}, {"Starting inside the game", ""}, {"Coming online", ""}};
    bool finished = false;
    bool success = false;
    bool alreadyRunning = false;
    std::string headline = "Starting Vigil";
    std::string message;
    double finishedAt = 0.0;
    bool retry = false;
} g;

std::atomic<bool> g_quit{false};
double g_clock0 = 0.0;

double nowSec() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) / static_cast<double>(f.QuadPart) - g_clock0;
}

void setStep(int i, StepState s, const std::string& detail = {}) {
    std::lock_guard<std::mutex> lk(g.mu);
    g.steps[i].state = s;
    g.steps[i].detail = detail;
    g.steps[i].changedAt = nowSec();
}

void finish(bool success, const std::string& headline, const std::string& message) {
    std::lock_guard<std::mutex> lk(g.mu);
    g.finished = true;
    g.success = success;
    g.headline = headline;
    g.message = message;
    g.finishedAt = nowSec();
}

// ── Work ─────────────────────────────────────────────────────────────────────────────────────

std::uint64_t fnv1a(const void* data, std::size_t n) {
    std::uint64_t h = 1469598103934665603ull;
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

bool extractRuntime(std::wstring& dllPath, std::string& error) {
    HMODULE self = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(self, MAKEINTRESOURCEW(VIGIL_RES_EMBEDDED_DLL), MAKEINTRESOURCEW(10));
    HGLOBAL data = res ? LoadResource(self, res) : nullptr;
    const DWORD size = res ? SizeofResource(self, res) : 0;
    const void* bytes = data ? LockResource(data) : nullptr;
    if (!bytes || size == 0) { error = "This copy of Vigil.exe is incomplete. Download it again."; return false; }
    const fs::path dir = fs::path(paths::runtimeDirectory());
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path target = dir / ("Vigil-" + std::string(VIGIL_VERSION_STRING) + ".dll");
    dllPath = target.wstring();
    // Reuse an identical file; rewrite only when missing or different.
    if (fs::exists(target, ec) && fs::file_size(target, ec) == size) {
        std::ifstream in(target, std::ios::binary);
        std::vector<char> existing(size);
        if (in.read(existing.data(), size) && fnv1a(existing.data(), size) == fnv1a(bytes, size)) return true;
    }
    const fs::path tmp = dir / ("Vigil-" + std::string(VIGIL_VERSION_STRING) + ".dll.partial");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(static_cast<const char*>(bytes), size)) {
            error = "Vigil could not write to " + dir.string() + ".";
            return false;
        }
    }
    if (!MoveFileExW(tmp.wstring().c_str(), target.wstring().c_str(), MOVEFILE_REPLACE_EXISTING)) {
        // The previous copy is loaded in a running game: that game already has Vigil.
        fs::remove(tmp, ec);
        if (fs::exists(target, ec)) return true;
        error = "Vigil could not update its runtime file.";
        return false;
    }
    // Remove runtimes of older versions that are no longer in use (ignore files in use).
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const auto name = entry.path().filename().string();
        if (entry.path() != target && name.rfind("Vigil-", 0) == 0 && entry.path().extension() == ".dll") fs::remove(entry.path(), ec);
    }
    return true;
}

HWND findGameWindow() {
    struct Ctx { HWND best = nullptr; int score = -1000; } ctx;
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        if (!IsWindowVisible(hwnd)) return TRUE;
        char cls[64] = {};
        GetClassNameA(hwnd, cls, sizeof(cls));
        if (std::strcmp(cls, "LWJGL") != 0 && std::strncmp(cls, "GLFW", 4) != 0) return TRUE;
        char title[256] = {};
        GetWindowTextA(hwnd, title, sizeof(title));
        std::string t = title;
        for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        int score = 1;
        if (t.find("lunar") != std::string::npos) score += 100;
        if (t.find("minecraft") != std::string::npos) score += 40;
        if (t.find("1.8.9") != std::string::npos) score += 20;
        auto* c = reinterpret_cast<Ctx*>(lp);
        if (score > c->score) { c->score = score; c->best = hwnd; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.best;
}

bool vigilLoaded(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    bool found = false;
    for (BOOL ok = Module32FirstW(snap, &me); ok && !found; ok = Module32NextW(snap, &me)) {
        const std::wstring n = me.szModule;
        found = _wcsicmp(n.c_str(), L"Vigil.dll") == 0 || (n.size() > 10 && _wcsnicmp(n.c_str(), L"Vigil-", 6) == 0);
    }
    CloseHandle(snap);
    return found;
}

std::string menuKeyName() {
    std::ifstream in(fs::path(paths::dataDirectory()) / "config.json", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    json::Value root;
    int vk = VK_RSHIFT;
    if (!text.empty() && json::parse(text, root)) vk = static_cast<int>(root.get("settings").get("ui.menuKey").asNumber(VK_RSHIFT));
    return keyName(vk);
}

void work() {
    // 1. Prepare.
    setStep(0, StepState::Active);
    std::wstring dll;
    std::string error;
    {
        // Carry over settings from a 2.0 folder layout next to the executable.
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        paths::migrateLegacyData((fs::path(exe).parent_path() / "vigil").string(), paths::dataDirectory());
    }
    if (!extractRuntime(dll, error)) {
        setStep(0, StepState::Failed);
        finish(false, "Vigil could not start", error);
        return;
    }
    setStep(0, StepState::Done, "Version " + std::string(VIGIL_VERSION_STRING));

    // 2. Find the game (wait for it).
    setStep(1, StepState::Active, "Start Minecraft 1.8.9 in Lunar Client");
    HWND hwnd = nullptr;
    while (!g_quit && !(hwnd = findGameWindow())) Sleep(500);
    if (g_quit) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    setStep(1, StepState::Done, "Found the game");

    // 3. Start inside the game.
    if (vigilLoaded(pid)) {
        setStep(2, StepState::Done, "Already running");
        setStep(3, StepState::Done);
        {
            std::lock_guard<std::mutex> lk(g.mu);
            g.alreadyRunning = true;
        }
        finish(true, "Vigil is already running", "Press " + menuKeyName() + " in game, or type /vigil in chat.");
        return;
    }
    setStep(2, StepState::Active);
    wchar_t readyName[64], failName[64];
    swprintf_s(readyName, L"Local\\Vigil.Ready.%lu", pid);
    swprintf_s(failName, L"Local\\Vigil.Failed.%lu", pid);
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, readyName);
    HANDLE failed = CreateEventW(nullptr, TRUE, FALSE, failName);
    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                  PROCESS_VM_READ | SYNCHRONIZE,
                              FALSE, pid);
    auto cleanup = [&]() {
        if (proc) CloseHandle(proc);
        if (ready) CloseHandle(ready);
        if (failed) CloseHandle(failed);
    };
    if (!proc) {
        setStep(2, StepState::Failed);
        finish(false, "Vigil could not reach the game", "Windows denied access to the game process. If Lunar Client runs as "
                                                        "administrator, run Vigil.exe as administrator too.");
        cleanup();
        return;
    }
    const SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote || !WriteProcessMemory(proc, remote, dll.c_str(), bytes, nullptr)) {
        if (remote) VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        setStep(2, StepState::Failed);
        finish(false, "Vigil could not start", "The game process could not be prepared.");
        cleanup();
        return;
    }
    auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE thread = loadLibrary ? CreateRemoteThread(proc, nullptr, 0, loadLibrary, remote, 0, nullptr) : nullptr;
    if (!thread) {
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        setStep(2, StepState::Failed);
        finish(false, "Vigil could not start", "The game did not accept Vigil. Your antivirus may have blocked it; see "
                                               "the help page for how to allow Vigil.");
        cleanup();
        return;
    }
    const DWORD wait = WaitForSingleObject(thread, 20000);
    DWORD exitCode = 0;
    GetExitCodeThread(thread, &exitCode);
    CloseHandle(thread);
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    if (wait != WAIT_OBJECT_0 || exitCode == 0) {
        setStep(2, StepState::Failed);
        finish(false, "Vigil could not start", wait != WAIT_OBJECT_0 ? "The game took too long to load Vigil."
                                                                      : "The game could not load Vigil. Make sure you use the 64-bit "
                                                                        "Lunar Client and that your antivirus allows Vigil.");
        cleanup();
        return;
    }
    setStep(2, StepState::Done, "Loaded");

    // 4. Wait for Vigil to report that it is running.
    setStep(3, StepState::Active, "Waiting for the game to finish loading");
    HANDLE waits[3] = {ready, failed, proc};
    const DWORD r = WaitForMultipleObjects(3, waits, FALSE, 120000);
    if (r == WAIT_OBJECT_0) {
        setStep(3, StepState::Done);
        finish(true, "Vigil is active", "Press " + menuKeyName() + " in game, or type /vigil in chat.");
    } else {
        setStep(3, StepState::Failed);
        finish(false, r == WAIT_OBJECT_0 + 2 ? "The game closed" : "Vigil did not come online",
               r == WAIT_OBJECT_0 + 2 ? "Lunar Client exited before Vigil finished starting."
                                      : "Vigil started but could not finish setting up. The log explains why.");
    }
    cleanup();
}

// ── Window ───────────────────────────────────────────────────────────────────────────────────

constexpr int kW = 480, kH = 340;
Renderer* g_r = nullptr;
Fonts* g_fonts = nullptr;
float g_scale = 1.f;
float g_mx = -1, g_my = -1;
bool g_down = false, g_clicked = false;

struct ButtonHit { Rect rc; int id; };
std::vector<ButtonHit> g_buttons;

bool buttonAt(float x, float y, int& id) {
    for (const auto& b : g_buttons) if (b.rc.contains(x, y)) { id = b.id; return true; }
    return false;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST: {
            POINT p{static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
            ScreenToClient(hwnd, &p);
            int id = 0;
            if (buttonAt(p.x / g_scale, p.y / g_scale, id)) return HTCLIENT;
            return HTCAPTION;   // drag anywhere else
        }
        case WM_MOUSEMOVE: g_mx = static_cast<short>(LOWORD(lp)) / g_scale; g_my = static_cast<short>(HIWORD(lp)) / g_scale; return 0;
        case WM_LBUTTONDOWN: g_down = true; return 0;
        case WM_LBUTTONUP: if (g_down) g_clicked = true; g_down = false; return 0;
        case WM_SETCURSOR: {
            int id = 0;
            SetCursor(LoadCursorA(nullptr, buttonAt(g_mx, g_my, id) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        case WM_KEYDOWN: if (wp == VK_ESCAPE) PostQuitMessage(0); return 0;
        case WM_CLOSE: PostQuitMessage(0); return 0;
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void drawStep(Renderer& r, Fonts& f, const Theme& t, float x, float y, const Step& s, double now) {
    const float cx = x + 10.f, cy = y + 10.f;
    switch (s.state) {
        case StepState::Pending:
            r.ring(cx, cy, 8.f, 1.5f, t.textFaint.alpha(0.6f));
            break;
        case StepState::Active: {
            r.ring(cx, cy, 8.f, 2.f, t.accent.withAlpha(0.18f));
            const float a = static_cast<float>(std::fmod(now * 5.0, 6.2831853));
            r.arc(cx, cy, 8.f, 2.f, a, a + 1.9f, t.accent, true);
            break;
        }
        case StepState::Done: {
            const float k = ease::outBack(motion::progress(now, s.changedAt, 0.3f), 2.f);
            r.circle(cx, cy, 9.f * std::min(1.f, k), t.sevClear.withAlpha(0.18f));
            drawIcon(r, Icon::Check, cx - 6.f, cy - 6.f, 12.f, t.sevClear.alpha(std::min(1.f, k)), 2.f);
            break;
        }
        case StepState::Failed:
            r.circle(cx, cy, 9.f, t.sevCritical.withAlpha(0.18f));
            drawIcon(r, Icon::Close, cx - 5.f, cy - 5.f, 10.f, t.sevCritical, 2.f);
            break;
    }
    const Color tc = s.state == StepState::Pending ? t.textFaint : t.text;
    f.draw(r, type::BodyStrong, x + 30.f, y + 1.f, s.title, tc);
    if (!s.detail.empty() && s.state != StepState::Pending) {
        f.draw(r, type::Small, x + 30.f + f.width(type::BodyStrong, s.title) + 10.f, y + 2.f, s.detail, t.textMuted, Align::Left, 220.f);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_clock0 = 0.0;
    g_clock0 = nowSec();

    WNDCLASSW wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(VIGIL_RES_APP_ICON));
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    wc.lpszClassName = L"VigilLauncher";
    RegisterClassW(&wc);
    const UINT dpi = GetDpiForSystem();
    g_scale = dpi / 96.f;
    const int pw = static_cast<int>(kW * g_scale), ph = static_cast<int>(kH * g_scale);
    RECT area{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"VigilLauncher", L"Vigil", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
                                area.left + (area.right - area.left - pw) / 2, area.top + (area.bottom - area.top - ph) / 2, pw, ph,
                                nullptr, nullptr, inst, nullptr);
    const DWORD round = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &round, sizeof(round));
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));

    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    HGLRC rc = wglCreateContext(dc);
    wglMakeCurrent(dc, rc);
    glx::load();
    setTheme(0, 0, CustomTheme{});
    Renderer renderer;
    Fonts fonts;
    g_r = &renderer;
    g_fonts = &fonts;

    std::thread worker(work);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);

    const double openedAt = nowSec();
    MSG msg;
    bool running = true;
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;
        const double now = nowSec();
        Shared snap;
        {
            std::lock_guard<std::mutex> lk(g.mu);
            for (int i = 0; i < 4; ++i) snap.steps[i] = g.steps[i];
            snap.finished = g.finished;
            snap.success = g.success;
            snap.headline = g.headline;
            snap.message = g.message;
            snap.finishedAt = g.finishedAt;
        }
        // Success closes on its own after a short moment.
        if (snap.finished && snap.success && now - snap.finishedAt > 3.2) break;

        const Theme& t = theme();
        renderer.begin(pw, ph, g_scale, true);
        fonts.setScale(g_scale);
        const float W = static_cast<float>(kW), H = static_cast<float>(kH);
        const float in = ease::emphasized(motion::progress(now, openedAt, 0.35f));
        renderer.roundRectGradient({0, 0, W, H}, 0.f, t.windowTop, t.windowBottom);
        renderer.shadow({30.f, -110.f, 240.f, 150.f}, 75.f, 150.f, t.accent.withAlpha(0.07f), 0.f);
        renderer.pushAlpha(in);
        g_buttons.clear();

        // Header.
        const float lx = 30.f, ly = 30.f;
        drawLogo(renderer, lx, ly, 34.f, t.text, snap.finished && !snap.success ? t.sevCritical : t.accent);
        fonts.draw(renderer, type::Wordmark, lx + 48.f, ly + 3.f, "VIGIL", t.text);
        fonts.draw(renderer, type::Caption, lx + 48.f, ly + 21.f, std::string("Version ") + VIGIL_VERSION_STRING, t.textFaint);
        // Close button.
        {
            const Rect cr{W - 46.f, 18.f, 28.f, 28.f};
            const bool h = cr.contains(g_mx, g_my);
            if (h) renderer.roundRect(cr, 7.f, t.surfaceHover);
            drawIcon(renderer, Icon::Close, cr.x + 7.f, cr.y + 7.f, 14.f, h ? t.text : t.textMuted);
            g_buttons.push_back({cr, 1});
        }

        // Headline.
        const Color hc = !snap.finished ? t.text : snap.success ? t.text : t.sevCritical;
        fonts.draw(renderer, type::Heading, 30.f, 92.f, snap.headline, hc);
        // Steps.
        float sy = 140.f;
        for (int i = 0; i < 4; ++i) {
            drawStep(renderer, fonts, t, 30.f, sy, snap.steps[i], now);
            sy += 30.f;
        }
        // Footer: message + buttons.
        if (snap.finished) {
            renderer.rect({30.f, H - 76.f, W - 60.f, 1.f}, t.divider);
            fonts.drawWrapped(renderer, type::Small, 30.f, H - 62.f, snap.success ? W - 60.f : W - 230.f, snap.message, t.textMuted, 3.f, 3);
            if (snap.success) {
                const float f = static_cast<float>(std::clamp(1.0 - (now - snap.finishedAt) / 3.2, 0.0, 1.0));
                renderer.roundRect({30.f, H - 10.f, (W - 60.f) * f, 2.f}, 1.f, t.accent.alpha(0.5f));
            } else {
                const Rect logs{W - 190.f, H - 58.f, 88.f, 32.f};
                const Rect close{W - 94.f, H - 58.f, 64.f, 32.f};
                for (const auto& [rcb, label, id, primary] :
                     {std::tuple<Rect, const char*, int, bool>{logs, "Open logs", 2, false}, {close, "Close", 1, true}}) {
                    const bool h = rcb.contains(g_mx, g_my);
                    renderer.roundRect(rcb, t.radiusControl, primary ? t.accent.lighter(h ? 0.08f : 0.f) : t.surfaceRaised.mix(t.surfaceHover, h ? 1.f : 0.f));
                    if (!primary) renderer.roundRectStroke(rcb, t.radiusControl, 1.f, t.borderStrong);
                    fonts.draw(renderer, type::BodyStrong, rcb.cx(), rcb.cy() - fonts.lineHeight(type::BodyStrong) * 0.5f, label,
                               primary ? t.textOnAccent : t.text, Align::Center);
                    g_buttons.push_back({rcb, id});
                }
            }
        } else {
            fonts.draw(renderer, type::Small, 30.f, H - 40.f, "Keep this window open until Vigil is active.", t.textFaint);
        }
        renderer.popAlpha();
        renderer.end();
        SwapBuffers(dc);

        if (g_clicked) {
            g_clicked = false;
            int id = 0;
            if (buttonAt(g_mx, g_my, id)) {
                if (id == 1) running = false;
                if (id == 2) ShellExecuteW(nullptr, L"open", (fs::path(paths::dataDirectory()) / "logs").wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
        Sleep(snap.finished ? 16 : 12);
    }
    g_quit = true;
    fonts.releaseGraphics();
    renderer.releaseResources();
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(rc);
    ReleaseDC(hwnd, dc);
    DestroyWindow(hwnd);
    bool done = false;
    {
        std::lock_guard<std::mutex> lk(g.mu);
        done = g.finished;
    }
    if (done) {
        worker.join();
        return 0;
    }
    // Closed while the worker is still waiting on the game: nothing left to clean up in this
    // process, so leave without waiting for those (up to two-minute) waits.
    worker.detach();
    ExitProcess(0);
}
