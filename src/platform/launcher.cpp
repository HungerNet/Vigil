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
#include "../core/log.h"
#include "../core/paths.h"
#include "../ui/font.h"
#include "../ui/gfx.h"
#include "../ui/icons.h"
#include "../ui/motion.h"
#include "../ui/resources.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"
#include "core/version.h"
#include "runtime_guard.h"

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

// The runtime embedded in this executable (resource 301).
bool embeddedRuntime(const void*& bytes, DWORD& size) {
    HMODULE self = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(self, MAKEINTRESOURCEW(VIGIL_RES_EMBEDDED_DLL), MAKEINTRESOURCEW(10));
    HGLOBAL data = res ? LoadResource(self, res) : nullptr;
    size = res ? SizeofResource(self, res) : 0;
    bytes = data ? LockResource(data) : nullptr;
    return bytes && size > 0;
}

std::string fileSha256(const fs::path& p, std::uintmax_t expectedSize) {
    std::error_code ec;
    if (!fs::exists(p, ec) || fs::file_size(p, ec) != expectedSize) return {};
    std::ifstream in(p, std::ios::binary);
    std::vector<char> bytes(static_cast<std::size_t>(expectedSize));
    if (!in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return {};
    return platform::sha256Hex(bytes.data(), bytes.size());
}

// The version resource of a runtime file must name this executable's version and build.
bool runtimeMatchesExe(const fs::path& p, platform::RuntimeInfo& info) {
    return platform::readModuleVersion(p.wstring(), info) && info.version == VIGIL_VERSION_STRING && info.build == VIGIL_BUILD_ID;
}

// Writes the embedded runtime to runtime\Vigil-<version>.dll. Verified three ways (PLAN.md §10):
// the embedded runtime names this exe's version and build, the file on disk has the embedded
// bytes (SHA-256), and an existing file is reused only when both hold.
bool extractRuntime(std::wstring& dllPath, std::string& error) {
    const void* bytes = nullptr;
    DWORD size = 0;
    if (!embeddedRuntime(bytes, size)) { error = "This copy of Vigil.exe is incomplete. Download it again."; return false; }
    const std::string embeddedSha = platform::sha256Hex(bytes, size);
    const fs::path dir = fs::path(paths::runtimeDirectory());
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path target = dir / ("Vigil-" + std::string(VIGIL_VERSION_STRING) + ".dll");
    dllPath = target.wstring();
    platform::RuntimeInfo info;
    // Reuse an identical file; rewrite only when missing or different.
    if (!embeddedSha.empty() && fileSha256(target, size) == embeddedSha && runtimeMatchesExe(target, info)) return true;
    const fs::path tmp = dir / ("Vigil-" + std::string(VIGIL_VERSION_STRING) + ".dll.partial");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(static_cast<const char*>(bytes), size)) {
            error = "Vigil could not write to " + dir.string() + ".";
            return false;
        }
    }
    if (!runtimeMatchesExe(tmp, info)) {
        fs::remove(tmp, ec);
        log::writef("launcher", "embedded runtime mismatch: exe %s/%s, runtime %s/%s", VIGIL_VERSION_STRING, VIGIL_BUILD_ID,
                    info.version.c_str(), info.build.c_str());
        error = "This copy of Vigil.exe is damaged: the runtime inside it belongs to another build. Download it again.";
        return false;
    }
    if (!MoveFileExW(tmp.wstring().c_str(), target.wstring().c_str(), MOVEFILE_REPLACE_EXISTING)) {
        // The existing file is loaded by a running game. It is only usable if it is this build.
        fs::remove(tmp, ec);
        platform::RuntimeInfo existing;
        if (fs::exists(target, ec) && runtimeMatchesExe(target, existing)) return true;
        error = "Vigil could not update its runtime file because a game is using an older copy. Close Lunar Client and "
                "start Vigil again.";
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

std::string menuKeyName() {
    std::ifstream in(fs::path(paths::dataDirectory()) / "config.json", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    json::Value root;
    int vk = VK_RSHIFT;
    if (!text.empty() && json::parse(text, root)) vk = static_cast<int>(root.get("settings").get("ui.menuKey").asNumber(VK_RSHIFT));
    return keyName(vk);
}

void work() {
    log::init((fs::path(paths::dataDirectory()) / "logs").string());
    log::writef("launcher", "Vigil.exe %s (build %s) starting", VIGIL_VERSION_STRING, VIGIL_BUILD_ID);
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

    // 3. Start inside the game — unless a Vigil runtime is already there (PLAN.md §10).
    platform::RuntimeInfo running;
    if (platform::findRunningRuntime(pid, running)) {
        const auto match = platform::compareRuntime(running, VIGIL_VERSION_STRING, VIGIL_BUILD_ID);
        log::writef("launcher", "a Vigil runtime is already loaded: %s (build %s)", running.version.c_str(),
                    running.build.empty() ? "unknown" : running.build.c_str());
        if (match == platform::RuntimeMatch::Same || match == platform::RuntimeMatch::Unknown) {
            setStep(2, StepState::Done, "Already running");
            setStep(3, StepState::Done);
            {
                std::lock_guard<std::mutex> lk(g.mu);
                g.alreadyRunning = true;
            }
            finish(true, "Vigil is already running", "Press " + menuKeyName() + " in game, or type /vigil in chat.");
            return;
        }
        // Never load a second runtime next to another version: restarting the game is the update.
        setStep(2, StepState::Failed, "Vigil " + running.version + " is running");
        const std::string what = match == platform::RuntimeMatch::OtherVersion
                                     ? "Vigil " + running.version + " is running in this game."
                                     : "A different build of Vigil " + running.version + " is running in this game.";
        finish(false, "Restart Lunar Client to update", what + " Close Lunar Client, start it again, then run Vigil " +
                                                             VIGIL_VERSION_STRING + ".");
        return;
    }
    setStep(2, StepState::Active);
    wchar_t readyName[64], failName[64];
    swprintf_s(readyName, L"Local\\Vigil.Ready.%lu", pid);
    swprintf_s(failName, L"Local\\Vigil.Failed.%lu", pid);
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, readyName);
    HANDLE failed = CreateEventW(nullptr, TRUE, FALSE, failName);
    HANDLE reason = platform::createFailureChannel(pid);
    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                  PROCESS_VM_READ | SYNCHRONIZE,
                              FALSE, pid);
    auto cleanup = [&]() {
        if (proc) CloseHandle(proc);
        if (ready) CloseHandle(ready);
        if (failed) CloseHandle(failed);
        if (reason) CloseHandle(reason);
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
        // The runtime says why when it can (version conflict, JVM not found, …).
        const std::string why = platform::readFailureReason(reason);
        log::writef("launcher", "runtime did not come online (wait=%lu): %s", r, why.empty() ? "no reason given" : why.c_str());
        finish(false, r == WAIT_OBJECT_0 + 2 ? "The game closed" : "Vigil did not come online",
               r == WAIT_OBJECT_0 + 2 ? "Lunar Client exited before Vigil finished starting."
               : !why.empty()         ? why
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

// ── Release validation (no window, no game, nothing written outside %TEMP%) ─────────────────
//
//   Vigil.exe --self-check <report.json>   verifies that this executable and the runtime embedded
//                                           in it are the same version and build; exit code 0/1
//   Vigil.exe --version <out.txt>          writes "<version> <build>"
//
// The workflow runs both before publishing (docs/RELEASE_PROCESS.md).
int selfCheck(const std::wstring& reportPath) {
    json::Value report = json::Value::object();
    json::Value checks = json::Value::array();
    bool ok = true;
    auto check = [&](const char* name, bool pass, const std::string& detail) {
        json::Value c = json::Value::object();
        c["check"] = json::Value(name);
        c["pass"] = json::Value(pass);
        c["detail"] = json::Value(detail);
        checks.push(c);
        ok = ok && pass;
    };
    report["exeVersion"] = json::Value(VIGIL_VERSION_STRING);
    report["exeBuild"] = json::Value(VIGIL_BUILD_ID);

    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    platform::RuntimeInfo self;
    const bool selfRead = platform::readModuleVersion(exe, self);
    check("exe version resource", selfRead && self.version == VIGIL_VERSION_STRING && self.build == VIGIL_BUILD_ID,
          selfRead ? self.version + " / " + self.build : "unreadable");

    const void* bytes = nullptr;
    DWORD size = 0;
    const bool embedded = embeddedRuntime(bytes, size);
    check("runtime embedded", embedded, embedded ? std::to_string(size) + " bytes" : "resource 301 missing");
    if (embedded) {
        const std::string sha = platform::sha256Hex(bytes, size);
        report["runtimeSha256"] = json::Value(sha);
        report["runtimeSize"] = json::Value(static_cast<double>(size));
        // A 64-bit DLL: MZ header, PE signature, AMD64 machine, DLL characteristic.
        const auto* b = static_cast<const unsigned char*>(bytes);
        bool pe = size > 0x200 && b[0] == 'M' && b[1] == 'Z';
        if (pe) {
            const std::uint32_t off = *reinterpret_cast<const std::uint32_t*>(b + 0x3C);
            pe = off + 24 < size && std::memcmp(b + off, "PE\0\0", 4) == 0 &&
                 *reinterpret_cast<const std::uint16_t*>(b + off + 4) == IMAGE_FILE_MACHINE_AMD64 &&
                 (*reinterpret_cast<const std::uint16_t*>(b + off + 22) & IMAGE_FILE_DLL) != 0;
        }
        check("runtime is a 64-bit DLL", pe, pe ? "x64 DLL" : "not an x64 DLL");
        // Read the embedded runtime's version resource from a temporary copy.
        wchar_t tmpDir[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tmpDir);
        const fs::path tmp = fs::path(tmpDir) / ("Vigil-selfcheck-" + std::to_string(GetCurrentProcessId()) + ".dll");
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out.write(static_cast<const char*>(bytes), size);
        }
        platform::RuntimeInfo rt;
        const bool rtRead = platform::readModuleVersion(tmp.wstring(), rt);
        std::error_code ec;
        fs::remove(tmp, ec);
        report["runtimeVersion"] = json::Value(rt.version);
        report["runtimeBuild"] = json::Value(rt.build);
        check("runtime version matches exe", rtRead && rt.version == VIGIL_VERSION_STRING, rtRead ? rt.version : "unreadable");
        check("runtime build matches exe", rtRead && rt.build == VIGIL_BUILD_ID, rtRead ? rt.build : "unreadable");
        check("runtime hash computed", sha.size() == 64, sha);
    }
    report["checks"] = checks;
    report["ok"] = json::Value(ok);
    std::ofstream out(fs::path(reportPath), std::ios::binary | std::ios::trunc);
    out << json::write(report, 2);
    return ok ? 0 : 1;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv && argc >= 3 && std::wcscmp(argv[1], L"--self-check") == 0) {
            const int code = selfCheck(argv[2]);
            LocalFree(argv);
            return code;
        }
        if (argv && argc >= 3 && std::wcscmp(argv[1], L"--version") == 0) {
            std::ofstream out(fs::path(argv[2]), std::ios::binary | std::ios::trunc);
            out << VIGIL_VERSION_STRING << " " << VIGIL_BUILD_ID << "\n";
            LocalFree(argv);
            return 0;
        }
        if (argv) LocalFree(argv);
    }
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
    std::wstring title = L"Vigil ";
    for (const char* p = VIGIL_VERSION_STRING; *p; ++p)
        title.push_back(static_cast<wchar_t>(*p));
    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"VigilLauncher", title.c_str(), WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
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
