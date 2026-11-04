#include "class_finder_log.h"

#ifdef VIGIL_LUNAR

#include "class_finder_obf_map.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>

namespace {

constexpr UINT WM_LOG_APPEND = WM_APP + 42;
constexpr int IDC_LOG_EDIT = 1001;
constexpr int IDC_BTN_CLEAR = 1002;
constexpr int kMaxLogChars = 256 * 1024;
constexpr DWORD kUiDeferMs = 2500;

HWND g_logHwnd = nullptr;
HWND g_editHwnd = nullptr;
HANDLE g_logThread = nullptr;
HANDLE g_uiDeferThread = nullptr;
HMODULE g_logModule = nullptr;
std::atomic<bool> g_logReady{false};
std::atomic<bool> g_backendStarted{false};
std::atomic<bool> g_uiDisabled{false};
std::mutex g_logMu;
std::mutex g_pendingMu;
std::string g_pendingLines;
std::string g_logFilePath;

const char* LookupObfForMcp(const char* mcpSlash) {
    if (!mcpSlash) return nullptr;
    for (int i = 0; i < kClassObfMapCount; ++i) {
        if (strcmp(kClassObfMap[i].mcpSlash, mcpSlash) == 0) {
            return kClassObfMap[i].obfSlash;
        }
    }
    return nullptr;
}

const char* InferExpectedKind(const char* mcpSlash) {
    if (!mcpSlash || !mcpSlash[0]) return "?";
    if (strncmp(mcpSlash, "java/", 5) == 0 || strncmp(mcpSlash, "javax/", 6) == 0) {
        return "JDK";
    }
    if (strncmp(mcpSlash, "com/moonsworth/lunar/", 21) == 0) {
        return "LUNAR";
    }
    if (strncmp(mcpSlash, "net/minecraft/", 14) == 0 || strncmp(mcpSlash, "com/mojang/", 11) == 0) {
        return "MCP";
    }
    return "OTHER";
}

bool UiEnabledByEnv() {
    char buf[8] = {};
    const DWORD n = GetEnvironmentVariableA("VIGIL_CLASS_LOG_UI", buf, sizeof(buf));
    if (n == 0) return false;
    return buf[0] == '1' && buf[1] == '\0';
}

void InitLogFilePath() {
    if (!g_logFilePath.empty()) return;
    char dllPath[MAX_PATH] = {};
    if (g_logModule && GetModuleFileNameA(g_logModule, dllPath, MAX_PATH) > 0) {
        g_logFilePath = dllPath;
        const auto pos = g_logFilePath.find_last_of("\\/");
        if (pos != std::string::npos) {
            g_logFilePath.replace(pos + 1, std::string::npos, "VigilClassFinder.log");
            return;
        }
    }
    g_logFilePath = "VigilClassFinder.log";
}

void AppendFileLogUnlocked(const char* text) {
    if (!text || !text[0]) return;
    InitLogFilePath();
    FILE* f = nullptr;
    if (fopen_s(&f, g_logFilePath.c_str(), "ab") != 0 || !f) return;
    fwrite(text, 1, strlen(text), f);
    fputc('\n', f);
    fclose(f);
}

void AppendTextToEdit(const char* text) {
    if (!g_editHwnd || !text || !text[0]) return;
    const int len = GetWindowTextLengthA(g_editHwnd);
    if (len > kMaxLogChars) {
        SendMessageA(g_editHwnd, EM_SETSEL, 0, len / 2);
        SendMessageA(g_editHwnd, EM_REPLACESEL, FALSE, (LPARAM)"");
    }
    SendMessageA(g_editHwnd, EM_SETSEL, len, len);
    SendMessageA(g_editHwnd, EM_REPLACESEL, FALSE, (LPARAM)text);
}

void FlushPendingLinesToUi() {
    std::string batch;
    {
        std::lock_guard<std::mutex> lk(g_pendingMu);
        batch.swap(g_pendingLines);
    }
    if (!batch.empty()) {
        AppendTextToEdit(batch.c_str());
    }
}

LRESULT CALLBACK LogWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_editHwnd = CreateWindowExA(
                WS_EX_CLIENTEDGE, "EDIT", "",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                8, 8, 760, 430,
                hwnd, (HMENU)(INT_PTR)IDC_LOG_EDIT,
                (HINSTANCE)GetWindowLongPtrA(hwnd, GWLP_HINSTANCE), nullptr);
            CreateWindowExA(
                0, "BUTTON", "Clear",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                8, 446, 80, 24,
                hwnd, (HMENU)(INT_PTR)IDC_BTN_CLEAR,
                (HINSTANCE)GetWindowLongPtrA(hwnd, GWLP_HINSTANCE), nullptr);
            SendMessageA(g_editHwnd, WM_SETFONT,
                (WPARAM)GetStockObject(ANSI_FIXED_FONT), TRUE);
            AppendTextToEdit(
                "Vigil ClassFinder log\r\n"
                "Columns: status | via | MCP | vanilla obf | expected kind\r\n"
                "Log file: VigilClassFinder.log (beside DLL)\r\n"
                "Set VIGIL_CLASS_LOG_UI=1 to show this window\r\n\r\n");
            FlushPendingLinesToUi();
            return 0;
        }
        case WM_SIZE: {
            const int w = LOWORD(lParam);
            const int h = HIWORD(lParam);
            if (g_editHwnd) MoveWindow(g_editHwnd, 8, 8, w - 16, h - 48, TRUE);
            HWND clearBtn = GetDlgItem(hwnd, IDC_BTN_CLEAR);
            if (clearBtn) MoveWindow(clearBtn, 8, h - 32, 80, 24, TRUE);
            return 0;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) == IDC_BTN_CLEAR && g_editHwnd) {
                SetWindowTextA(g_editHwnd, "");
                AppendTextToEdit("Vigil ClassFinder log (cleared)\r\n\r\n");
            }
            return 0;
        }
        case WM_LOG_APPEND: {
            char* owned = reinterpret_cast<char*>(lParam);
            if (owned) {
                AppendTextToEdit(owned);
                delete[] owned;
            }
            return 0;
        }
        case WM_DESTROY:
            g_logHwnd = nullptr;
            g_editHwnd = nullptr;
            g_logReady.store(false);
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

DWORD WINAPI LogUiThreadProc(LPVOID) {
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = LogWndProc;
    wc.hInstance = g_logModule ? g_logModule : GetModuleHandleA(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "VigilClassFinderLog";
    RegisterClassExA(&wc);

    g_logHwnd = CreateWindowExA(
        0,
        "VigilClassFinderLog",
        "Vigil ClassFinder",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        120, 120, 800, 520,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!g_logHwnd) {
        g_uiDisabled.store(true);
        return 1;
    }

    g_logReady.store(true);
    FlushPendingLinesToUi();

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}

DWORD WINAPI DeferredUiThreadProc(LPVOID) {
    Sleep(kUiDeferMs);
    if (g_uiDisabled.load(std::memory_order_relaxed) || !UiEnabledByEnv()) {
        return 0;
    }
    if (g_logThread) return 0;
    g_logThread = CreateThread(nullptr, 0, LogUiThreadProc, nullptr, 0, nullptr);
    return 0;
}

void EnsureLogBackend() {
#if !VIGIL_DEBUG_LOG
    return;
#endif
    if (g_backendStarted.exchange(true)) return;
    InitLogFilePath();
    AppendFileLogUnlocked("=== Vigil ClassFinder log started ===");
    if (UiEnabledByEnv()) {
        g_uiDeferThread = CreateThread(nullptr, 0, DeferredUiThreadProc, nullptr, 0, nullptr);
    } else {
        g_uiDisabled.store(true);
    }
}

void PostLogLineInternal(const std::string& line) {
#if !VIGIL_DEBUG_LOG
    (void)line;
    return;
#endif
    EnsureLogBackend();

    std::string withNl = line;
    if (withNl.empty() || withNl.back() != '\n') {
        withNl += "\r\n";
    }

    {
        std::lock_guard<std::mutex> lk(g_logMu);
        AppendFileLogUnlocked(line.c_str());
    }

    if (g_uiDisabled.load(std::memory_order_relaxed)) return;

    if (!g_logReady.load(std::memory_order_relaxed) || !g_logHwnd) {
        std::lock_guard<std::mutex> lk(g_pendingMu);
        g_pendingLines += withNl;
        return;
    }

    char* owned = new char[withNl.size() + 1];
    memcpy(owned, withNl.c_str(), withNl.size() + 1);
    if (!PostMessageA(g_logHwnd, WM_LOG_APPEND, 0, (LPARAM)owned)) {
        delete[] owned;
        std::lock_guard<std::mutex> lk(g_pendingMu);
        g_pendingLines += withNl;
    }
}

} // namespace

void ClassFinderLogInit(void* moduleHandle) {
    g_logModule = static_cast<HMODULE>(moduleHandle);
}

void ClassFinderLogShutdown() {
    if (g_logHwnd) {
        PostMessageA(g_logHwnd, WM_CLOSE, 0, 0);
    }
    if (g_logThread) {
        WaitForSingleObject(g_logThread, 2000);
        CloseHandle(g_logThread);
        g_logThread = nullptr;
    }
    if (g_uiDeferThread) {
        WaitForSingleObject(g_uiDeferThread, 1000);
        CloseHandle(g_uiDeferThread);
        g_uiDeferThread = nullptr;
    }
}

void ClassFinderLogLine(const char* line) {
    if (!line) return;
    PostLogLineInternal(line);
}

void ClassFinderLogClassResolve(const char* requestedMcpSlash, const char* via, bool ok) {
    const char* obf = LookupObfForMcp(requestedMcpSlash);
    const char* expected = InferExpectedKind(requestedMcpSlash);

    char buf[768];
    std::snprintf(buf, sizeof(buf),
        "[%s] via=%s | MCP=%s | obf=%s | expected=%s",
        ok ? "OK" : "FAIL",
        via ? via : "?",
        requestedMcpSlash ? requestedMcpSlash : "?",
        obf ? obf : "-",
        ok ? expected : "NOT FOUND");
    PostLogLineInternal(buf);
}

void ClassFinderLogMemberMiss(const char* kind, const char* ownerHint, const char* name, const char* signature) {
    if (!name || !name[0]) return;
    static std::mutex s_seenMu;
    static std::unordered_set<std::string> s_seen;
    std::string key = std::string(kind ? kind : "?") + "|" + (ownerHint ? ownerHint : "?") + "|" + name + "|" + (signature ? signature : "?");
    {
        std::lock_guard<std::mutex> lk(s_seenMu);
        if (!s_seen.insert(key).second) return;
    }
    char buf[768];
    std::snprintf(buf, sizeof(buf),
        "[JNI MISS] kind=%s | owner=%s | name=%s | sig=%s",
        kind ? kind : "?",
        ownerHint ? ownerHint : "?",
        name,
        signature ? signature : "?");
    PostLogLineInternal(buf);
}

#endif
