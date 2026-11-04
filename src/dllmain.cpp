#include "globals.h"
#include "class_finder.h"
#include "lunar_runtime.h"
#include "utils.h"
#include "jni_helpers.h"
#include "anticheat.h"
#include "colored_hitboxes.h"
#include "tab_overlay.h"
#include "player_api.h"
#include "denick.h"
#include "../java_src/lunar_headers/FrenchifyTabOverlayNativeLunar.h"
#include "../java_src/FrenchifyGuiNewChat.h"
#include "number_denicker.h"
#include "session_stats.h"
#include "party_queue_detector.h"
#include "trade_indicator.h"
#include "hud_edit.h"
#ifdef FRENCHIFY_LUNAR
#include "class_finder_log.h"
#endif
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <unordered_set>
#include <vector>
#include <atomic>
#include <tlhelp32.h>
#include <GL/gl.h>
#include <atomic>

static void LogInitOnce(const char* line);

// ===== Cross-module globals (declared extern in globals.h) =====
std::string   g_dllDir;
JavaVM*       g_jvmGlobal = nullptr;
bool          g_allowNickV2Display = false;
std::string   g_atlasReplayViewMode;
std::mutex    g_flaggedPlayersMutex;
std::map<std::string, std::string> g_flaggedPlayersLastCheckByUuid;
std::map<std::string, PlayerAcFlagCounts> g_flaggedPlayerCountsByUuid;
std::map<std::string, std::string> g_flaggedPlayersLastCheckByNameLower;
std::map<std::string, PlayerAcFlagCounts> g_flaggedPlayerCountsByNameLower;

static void ClearAtlasReplayFlagMaps() {
    std::lock_guard<std::mutex> lock(g_flaggedPlayersMutex);
    g_flaggedPlayersLastCheckByNameLower.clear();
    g_flaggedPlayerCountsByNameLower.clear();
}
std::string   g_localPlayerNameLower;
std::string   g_localPlayerIdLower;
std::string   g_localTeamLetter;
bool          g_localTeamColorResolved = false;
std::unordered_set<std::string> g_whoTeammateNamesLower;
bool          g_whoTeamResolved = false;

// ===== Main-loop globals =====
WNDPROC     oWndProc          = nullptr;
std::string currentChatInput  = "";
bool        shouldExecuteConfig = false;
std::string pendingStatsPlayer = "";
std::string pendingStatsMode = "";

// Async /stats query ? worker thread state (never block the main loop for HTTP)
static std::atomic<bool>    g_statsWorkerRunning{false};
static std::mutex            g_statsResultMutex;
struct G_AsyncStatsResult {
    bool                          ready  = false;
    std::string                   target;
    taboverlay::PlayerLookupStats st;
};
static G_AsyncStatsResult    g_asyncStatsResult;

// AutoWho State
ULONGLONG               lastTriggerTime    = 0;
ULONGLONG               scheduledWhoTime   = 0;
const ULONGLONG         triggerCooldown    = 3000;
int                     whoRetryCount      = 0;
std::vector<std::string> whoTriggers = {
    "Protect your bed", "Players swap teams", "All generators are maxed",
    "Select an ultimate", "Collect Lucky Blocks", "Become a Soul Collector",
    "Every few seconds", "Gather resources", "Opponents:", "Opponent:",
    "Bed Wars Duels",
    "Teaming with the Murderer is not allowed!",
    "Teaming with the Detective/Innocents is not allowed!"
};

// AutoGL State
ULONGLONG       lastAutoGLTime    = 0;
ULONGLONG       scheduledGLTime   = 0;
const ULONGLONG autoGLCooldown    = 5000;
std::string     pendingGLMessage  = "gl";

static int g_georgeWinCount = 0;
static ULONGLONG scheduledWantToBetTime = 0;
static ULONGLONG scheduledNickQolTime = 0;

static jobject  g_lastWorldObjGlobal = nullptr;
#ifndef FRENCHIFY_LUNAR
static ULONGLONG scheduledLocrawTime = 0;
#endif

// Party Tracker State
int       partyJoinCount     = 0;
int       partyQuitCount     = 0;
ULONGLONG lastPartyJoinTime  = 0;
ULONGLONG lastPartyQuitTime  = 0;
static std::unordered_set<std::string> g_partyJoinPlayersSeen;
static std::unordered_set<std::string> g_partyQuitPlayersSeen;

static std::string ExtractPartyEventPlayerName(const std::string& msg, const char* marker) {
    const size_t pos = msg.find(marker);
    if (pos == 0 || pos == std::string::npos) return "";
    return Trim(msg.substr(0, pos));
}

// Timing
ULONGLONG lastToggleWriteTime = 0;
ULONGLONG g_lastAcScanTime    = 0;

// Fast Buy State
bool g_inQuickBuyMenu       = false;
bool g_fastBuyEnabled       = false;
static volatile LONG g_pendingFastBuyClicks = 0;
static bool g_fastBuyLButtonHeld = false;
static ULONGLONG g_lastFastBuyClickQueuedAt = 0;
static int g_fastBuyReplayFailStreak = 0;
// For dedup logs in the main loop
static bool s_fbLastIsChest    = false;
static bool s_fbLastMenuFound  = false;
static std::string s_fbLastSlot0Name = "";
taboverlay::OverlayController g_tabOverlay;
bool g_tabHeld = false;
static HWND g_gameHwnd = nullptr;
bool g_useNativeTabPipeline = false;
static bool g_useNativeChatHook = false;
static jclass g_nativeTabClassGlobal = nullptr;
jclass g_mcClassGlobal = nullptr;
jclass g_customGuiClassGlobal = nullptr;
jobject g_injectClassLoaderGlobal = nullptr;
jmethodID g_midGetMinecraftStatic = nullptr;
static ULONGLONG g_lastCoreJniFailLog = 0;

using tSwapBuffers = BOOL(WINAPI*)(HDC);
static tSwapBuffers g_origSwapBuffers = nullptr;
static std::vector<void**> g_swapIatSlots;
static bool g_swapHookInstalled = false;
static bool g_inSwapHook = false;

using tGlReadPixels = void (APIENTRY*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid*);
static tGlReadPixels g_origGlReadPixels = nullptr;
static std::vector<void**> g_readPixelsIatSlots;
static bool g_readPixelsHookInstalled = false;
static bool g_inReadPixelsHook = false;

using tGlOrtho = void (APIENTRY*)(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble);
static tGlOrtho g_origGlOrtho = nullptr;
static std::vector<void**> g_glOrthoIatSlots;
static bool g_glOrthoHookInstalled = false;
static bool g_inGlOrthoHook = false;

using tWglGetProcAddress = PROC(WINAPI*)(LPCSTR);
static tWglGetProcAddress g_origWglGetProcAddress = nullptr;
static std::vector<void**> g_wglGpaIatSlots;
static bool g_wglGpaHookInstalled = false;

static ULONGLONG g_lastSwapDiagLog = 0;
static int g_swapMatchCount = 0;
static int g_swapMismatchCount = 0;
static ULONGLONG g_lastScoreboardPollTime = 0;
static unsigned long long g_scoreboardPollSeq = 0;
std::string g_tokensState = "Unknown";
std::string g_redState = "Unknown";
std::string g_modeState = "Unknown";
bool g_tabOverlayGuiEnabled = true;
bool g_tabOverlayStateAllowed = false;
// Set when a /who response is parsed (Online: ...). Used for API allowlist only, not tab UI.
static bool        g_inGameFromWho      = false;
static ULONGLONG   g_inGameFromWhoSetAt = 0;
static const ULONGLONG kInGameFromWhoTtlMs = 5ULL * 60ULL * 1000ULL; // 5 minutes
static std::string g_overlayColumnsConfigLast = "";
static std::string g_overlayGamemodeActiveLast = "";
static std::string g_scoreboardGamemode = "";

static bool IsPartyTrackerScoreboardActive() {
    return g_scoreboardGamemode == "bedwars";
}

static void ResetPartyTrackerState() {
    partyJoinCount = 0;
    partyQuitCount = 0;
    g_partyJoinPlayersSeen.clear();
    g_partyQuitPlayersSeen.clear();
}
static bool g_savedTogglesApplied = false;
static ULONGLONG g_lastJniExceptionClearLog = 0;
static bool g_chatInputLive = false;
static ULONGLONG g_lastChatInputUpdate = 0;
static std::mutex g_chatInputMutex;
static std::atomic<bool> g_pendingChatSubmit{false};
static ULONGLONG g_lastWndProcHookCheck = 0;
static bool g_nativeTabEnabledLastKnown = false;
static bool g_nativeTabEnabledLast = false;
std::atomic<bool> g_injectReady{false};
static std::atomic<bool> g_injectNotified{false};

static void LogInitOnce(const char* line) {
#ifdef FRENCHIFY_LUNAR
    static std::mutex s_mu;
    static std::unordered_set<std::string> s_seen;
    if (!line || !line[0]) return;
    std::lock_guard<std::mutex> lk(s_mu);
    if (!s_seen.insert(line).second) return;
    ClassFinderLogLine(line);
#else
    (void)line;
#endif
}

#ifdef FRENCHIFY_LUNAR
void FrenchifyBootLog(const char* msg) {
    if (msg) ClassFinderLogLine(msg);
}
#endif


static void BootLogInit(HMODULE mod) {
    char buf[MAX_PATH] = {};
    ClassFinderLogInit(mod);
    GetModuleFileNameA(mod, buf, MAX_PATH);
    std::string s = buf;
    const auto pos = s.find_last_of("\\/");
    if (pos != std::string::npos) g_dllDir = s.substr(0, pos);
    if (!g_dllDir.empty()) {
        AppendFrenchifyNamedLog("frenchify_inject.log",
            "=== Frenchify Lunar boot | log: " + g_dllDir + "\\frenchify_inject.log ===");
    }
#if FRENCHIFY_DEBUG_LOG
    if (!g_dllDir.empty()) {
        std::string msg = "Frenchify loaded. Debug log: " + g_dllDir + "\\frenchify_debug.log";
        FrenchifyFileLog("boot", msg.c_str());
    }
#endif
}

static HWND FindGameHwnd() {
    static const char* kClasses[] = {"LWJGL", "GLFW30", "GLFW32"};
    for (const char* cls : kClasses) {
        HWND hwnd = FindWindowA(cls, nullptr);
        if (hwnd) return hwnd;
    }
    HWND found = nullptr;
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        if (!IsWindowVisible(hwnd)) return TRUE;
        char cls[64] = {};
        GetClassNameA(hwnd, cls, sizeof(cls));
        if (strcmp(cls, "LWJGL") == 0 || strcmp(cls, "GLFW30") == 0 || strcmp(cls, "GLFW32") == 0) {
            *reinterpret_cast<HWND*>(lp) = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}

LRESULT CALLBACK hkWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static std::string NormalizeChatCommand(std::string cmd) {
    cmd = Trim(cmd);
    for (char& c : cmd) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return cmd;
}

static std::vector<std::string> SplitCommandTokens(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (unsigned char c : s) {
        if (c == ' ' || c == '\t') {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back((char)c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static std::string NormalizeStatsModeToken(std::string mode) {
    mode = LowerAscii(Trim(mode));
    if (mode == "bedwarsduel" || mode == "bwduels" || mode == "bwduel") return "bedwarsduels";
    if (mode == "mm" || mode == "murder" || mode == "murder_mystery") return "murdermystery";
    return mode;
}

static bool IsStatsGamemodeToken(const std::string& token) {
    return taboverlay::OverlayGamemodeFromString(NormalizeStatsModeToken(token)) !=
           taboverlay::OverlayGamemode::Unknown;
}

static bool ParseStatsCommandArgs(const std::string& rawCmd, std::string& outPlayer, std::string& outMode) {
    outPlayer.clear();
    outMode.clear();
    if (rawCmd.size() < 6 || rawCmd.rfind("/stats", 0) != 0) return false;

    size_t argsStart = 6;
    if (rawCmd.size() > argsStart && rawCmd[argsStart] == ' ') ++argsStart;
    if (argsStart >= rawCmd.size()) return false;

    const std::vector<std::string> tokens = SplitCommandTokens(Trim(rawCmd.substr(argsStart)));
    if (tokens.empty()) return false;

    if (tokens.size() >= 2 && IsStatsGamemodeToken(tokens.back())) {
        outMode = NormalizeStatsModeToken(tokens.back());
        outPlayer = tokens.front();
        for (size_t i = 1; i + 1 < tokens.size(); ++i) {
            outPlayer += " " + tokens[i];
        }
    } else {
        outPlayer = tokens.front();
        for (size_t i = 1; i < tokens.size(); ++i) {
            outPlayer += " " + tokens[i];
        }
    }
    outPlayer = Trim(outPlayer);
    return !outPlayer.empty();
}

static bool SyncChatInputFromScreen(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (!env || !mcObj || !mcClass) return false;

    g_chatInputLive = false;
    jfieldID fidCurrentScreen = env->GetFieldID(mcClass, "currentScreen", "Lnet/minecraft/client/gui/GuiScreen;");
    if (!fidCurrentScreen || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jobject currentScreen = env->GetObjectField(mcObj, fidCurrentScreen);
    if (!currentScreen || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    bool chatOpen = false;
    jclass guiChatClass = McFindClass(env, "net/minecraft/client/gui/GuiChat");
    if (guiChatClass && !env->ExceptionCheck() &&
        env->IsInstanceOf(currentScreen, guiChatClass)) {
        chatOpen = true;
        jfieldID fidInputField = env->GetFieldID(guiChatClass, "inputField", "Lnet/minecraft/client/gui/GuiTextField;");
        if (fidInputField && !env->ExceptionCheck()) {
            jobject inputField = env->GetObjectField(currentScreen, fidInputField);
            if (inputField && !env->ExceptionCheck()) {
                jclass guiTextFieldClass = env->GetObjectClass(inputField);
                jmethodID midGetText = guiTextFieldClass
                    ? env->GetMethodID(guiTextFieldClass, "getText", "()Ljava/lang/String;") : nullptr;
                if (midGetText && !env->ExceptionCheck()) {
                    jstring text = (jstring)env->CallObjectMethod(inputField, midGetText);
                    if (text && !env->ExceptionCheck()) {
                        const char* str = env->GetStringUTFChars(text, nullptr);
                        {
                            std::lock_guard<std::mutex> lk(g_chatInputMutex);
                            currentChatInput = str ? str : "";
                        }
                        if (str) env->ReleaseStringUTFChars(text, str);
                        env->DeleteLocalRef(text);
                        g_lastChatInputUpdate = GetTickCount64();
                    } else {
                        env->ExceptionClear();
                        std::lock_guard<std::mutex> lk(g_chatInputMutex);
                        currentChatInput.clear();
                        g_lastChatInputUpdate = GetTickCount64();
                    }
                } else {
                    env->ExceptionClear();
                }
                if (guiTextFieldClass) env->DeleteLocalRef(guiTextFieldClass);
                env->DeleteLocalRef(inputField);
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    if (guiChatClass) env->DeleteLocalRef(guiChatClass);
    env->DeleteLocalRef(currentScreen);
    g_chatInputLive = chatOpen;
    return chatOpen;
}

static void SendVanillaChatMessage(JNIEnv* env, jobject mcObj, jclass mcClass, const std::string& message) {
    if (!env || !mcObj || !mcClass || message.empty()) return;

    jfieldID fidPlayer = env->GetFieldID(mcClass, "thePlayer", "Lnet/minecraft/client/entity/EntityPlayerSP;");
    if (!fidPlayer || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    jobject player = env->GetObjectField(mcObj, fidPlayer);
    if (!player || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    jclass playerClass = env->GetObjectClass(player);
    jmethodID midSend = playerClass ? env->GetMethodID(playerClass, "sendChatMessage", "(Ljava/lang/String;)V") : nullptr;
    if (midSend && !env->ExceptionCheck()) {
        jstring jMsg = env->NewStringUTF(message.c_str());
        if (jMsg && !env->ExceptionCheck()) {
            env->CallVoidMethod(player, midSend, jMsg);
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(jMsg);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    jmethodID midDisplayGui = env->GetMethodID(mcClass, "displayGuiScreen", "(Lnet/minecraft/client/gui/GuiScreen;)V");
    if (midDisplayGui && !env->ExceptionCheck()) {
        env->CallVoidMethod(mcObj, midDisplayGui, (jobject)nullptr);
        if (env->ExceptionCheck()) env->ExceptionClear();
    } else {
        env->ExceptionClear();
    }

    if (playerClass) env->DeleteLocalRef(playerClass);
    env->DeleteLocalRef(player);
}

static bool OpenFrenchifyConfigGui(JNIEnv* env, jobject mcObj, jclass mcClass, jclass guiClass) {
    if (!env || !mcObj || !mcClass) return false;
    jclass useGuiClass = guiClass ? guiClass : g_customGuiClassGlobal;
    if (!useGuiClass) {
        return false;
    }

    jmethodID midDisplayGui = env->GetMethodID(mcClass, "displayGuiScreen", "(Lnet/minecraft/client/gui/GuiScreen;)V");
    if (!midDisplayGui || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    // Close chat / current screen before opening config.
    env->CallVoidMethod(mcObj, midDisplayGui, (jobject)nullptr);
    if (env->ExceptionCheck()) env->ExceptionClear();

    jmethodID guiConstructor = env->GetMethodID(useGuiClass, "<init>", "()V");
    if (!guiConstructor || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jobject myGuiInstance = env->NewObject(useGuiClass, guiConstructor);
    if (!myGuiInstance || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jmethodID midInitGui = env->GetMethodID(useGuiClass, "b", "()V");
    if (midInitGui && !env->ExceptionCheck()) {
        env->CallVoidMethod(myGuiInstance, midInitGui);
        if (env->ExceptionCheck()) env->ExceptionClear();
    } else {
        env->ExceptionClear();
    }

    env->CallVoidMethod(mcObj, midDisplayGui, myGuiInstance);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(myGuiInstance);
        return false;
    }

    env->DeleteLocalRef(myGuiInstance);

    jfieldID fidIngameGui = env->GetFieldID(mcClass, "ingameGUI", "Lnet/minecraft/client/gui/GuiIngame;");
    if (fidIngameGui && !env->ExceptionCheck()) {
        jobject ingameGui = env->GetObjectField(mcObj, fidIngameGui);
        if (ingameGui && !env->ExceptionCheck()) {
            jclass ingameGuiClass = env->GetObjectClass(ingameGui);
            jmethodID midGetChatGui = ingameGuiClass
                ? env->GetMethodID(ingameGuiClass, "getChatGUI", "()Lnet/minecraft/client/gui/GuiNewChat;") : nullptr;
            if (midGetChatGui && !env->ExceptionCheck()) {
                jobject chatGui = env->CallObjectMethod(ingameGui, midGetChatGui);
                if (chatGui && !env->ExceptionCheck()) {
                    jclass chatGuiClass2 = env->GetObjectClass(chatGui);
                    jmethodID midAddToSentMessages = chatGuiClass2
                        ? env->GetMethodID(chatGuiClass2, "addToSentMessages", "(Ljava/lang/String;)V") : nullptr;
                    if (midAddToSentMessages && !env->ExceptionCheck()) {
                        jstring configStr = env->NewStringUTF("/config");
                        if (configStr && !env->ExceptionCheck()) {
                            env->CallVoidMethod(chatGui, midAddToSentMessages, configStr);
                            if (env->ExceptionCheck()) env->ExceptionClear();
                            env->DeleteLocalRef(configStr);
                        } else {
                            env->ExceptionClear();
                        }
                    } else {
                        env->ExceptionClear();
                    }
                    if (chatGuiClass2) env->DeleteLocalRef(chatGuiClass2);
                    env->DeleteLocalRef(chatGui);
                } else {
                    env->ExceptionClear();
                }
            } else {
                env->ExceptionClear();
            }
            if (ingameGuiClass) env->DeleteLocalRef(ingameGuiClass);
            env->DeleteLocalRef(ingameGui);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    return true;
}

static bool IsFrenchifyChatCommand(const std::string& rawCmd) {
    const std::string cmd = NormalizeChatCommand(rawCmd);
    return cmd == "/config" || cmd == "/stats" || cmd.rfind("/stats ", 0) == 0;
}

static void AddMessageToChatHistory(JNIEnv* env, jobject mcObj, jclass mcClass, const std::string& message) {
    if (!env || !mcObj || !mcClass || message.empty()) return;

    jfieldID fidIngameGui = env->GetFieldID(mcClass, "ingameGUI", "Lnet/minecraft/client/gui/GuiIngame;");
    if (!fidIngameGui || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    jobject ingameGui = env->GetObjectField(mcObj, fidIngameGui);
    if (!ingameGui || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    jclass ingameGuiClass = env->GetObjectClass(ingameGui);
    jmethodID midGetChatGui = ingameGuiClass
        ? env->GetMethodID(ingameGuiClass, "getChatGUI", "()Lnet/minecraft/client/gui/GuiNewChat;") : nullptr;
    if (midGetChatGui && !env->ExceptionCheck()) {
        jobject chatGui = env->CallObjectMethod(ingameGui, midGetChatGui);
        if (chatGui && !env->ExceptionCheck()) {
            jclass chatGuiClass = env->GetObjectClass(chatGui);
            jmethodID midAddToSentMessages = chatGuiClass
                ? env->GetMethodID(chatGuiClass, "addToSentMessages", "(Ljava/lang/String;)V") : nullptr;
            if (midAddToSentMessages && !env->ExceptionCheck()) {
                jstring jMsg = env->NewStringUTF(message.c_str());
                if (jMsg && !env->ExceptionCheck()) {
                    env->CallVoidMethod(chatGui, midAddToSentMessages, jMsg);
                    if (env->ExceptionCheck()) env->ExceptionClear();
                    env->DeleteLocalRef(jMsg);
                } else {
                    env->ExceptionClear();
                }
            } else {
                env->ExceptionClear();
            }
            if (chatGuiClass) env->DeleteLocalRef(chatGuiClass);
            env->DeleteLocalRef(chatGui);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionCheck();
        env->ExceptionClear();
    }

    if (ingameGuiClass) env->DeleteLocalRef(ingameGuiClass);
    env->DeleteLocalRef(ingameGui);
}

static void ProcessPendingChatSubmit(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (!g_pendingChatSubmit.exchange(false)) return;

    SyncChatInputFromScreen(env, mcObj, mcClass);

    std::string rawCmd;
    {
        std::lock_guard<std::mutex> lk(g_chatInputMutex);
        rawCmd = currentChatInput;
    }
    if (!IsFrenchifyChatCommand(rawCmd)) return;

    const std::string cmd = NormalizeChatCommand(rawCmd);

    if (cmd == "/config") {
        if (OpenFrenchifyConfigGui(env, mcObj, mcClass, g_customGuiClassGlobal)) {
            return;
        }
        shouldExecuteConfig = true;
    } else if (cmd == "/stats" || cmd.rfind("/stats ", 0) == 0) {
        if (!FrenchifyIsHypixelServerActive()) {
            PrintLocalChat(env, mcObj, mcClass,
                "\xC2\xA7""7[\xC2\xA7""1Fre\xC2\xA7""fnch\xC2\xA7""cify\xC2\xA7""7] \xC2\xA7""cFrenchify features require a hypixel.net server.");
        } else {
            std::string player;
            std::string mode;
            if (ParseStatsCommandArgs(rawCmd, player, mode)) {
                pendingStatsPlayer = player;
                pendingStatsMode = mode;
                AddMessageToChatHistory(env, mcObj, mcClass, rawCmd);
            } else {
                PrintLocalChat(env, mcObj, mcClass,
                    "\xC2\xA7""7[\xC2\xA7""1Fre\xC2\xA7""fnch\xC2\xA7""cify\xC2\xA7""7] \xC2\xA7""cUsage: \xC2\xA7""f/stats <player> \xC2\xA7""7[bedwars|skywars|duel|bedwarsduels|murdermystery]");
            }
        }
    }

    jmethodID midDisplayGui = env->GetMethodID(mcClass, "displayGuiScreen", "(Lnet/minecraft/client/gui/GuiScreen;)V");
    if (midDisplayGui && !env->ExceptionCheck()) {
        env->CallVoidMethod(mcObj, midDisplayGui, (jobject)nullptr);
        if (env->ExceptionCheck()) env->ExceptionClear();
    } else {
        env->ExceptionClear();
    }
}

static void InjectDiagLog(const char* msg) {
    AppendFrenchifyNamedLog("frenchify_inject.log", msg ? msg : "");
}

static LONG WINAPI UnhandledCrashFilter(EXCEPTION_POINTERS* ep) {
    unsigned long code = 0;
    const void* addr = nullptr;
    if (ep && ep->ExceptionRecord) {
        code = (unsigned long)ep->ExceptionRecord->ExceptionCode;
        addr = ep->ExceptionRecord->ExceptionAddress;
    }

    char line[320] = {};
    std::snprintf(line, sizeof(line),
        "CRASH non gere | code=0x%08lX addr=%p | voir frenchify_inject.log a cote de la DLL",
        code, addr);
    InjectDiagLog(line);
    MessageBoxA(nullptr, line, "Frenchify Inject - CRASH", MB_ICONERROR | MB_TOPMOST);

    return EXCEPTION_EXECUTE_HANDLER;
}

static void EnsureWndProcHookAlive() {
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastWndProcHookCheck < 1000) return;
    g_lastWndProcHookCheck = now;

    HWND hwnd = FindGameHwnd();
    if (!hwnd) return;

    const WNDPROC currentProc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
    if (currentProc == hkWndProc) {
        g_gameHwnd = hwnd;
        return;
    }

    // Fresh window (or first install): hook once.
    if (hwnd != g_gameHwnd || !oWndProc) {
        WNDPROC prev = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
        if (prev && prev != hkWndProc) {
            oWndProc = prev;
        }
        g_gameHwnd = hwnd;
        return;
    }

    // Same hwnd, but the client took GWLP_WNDPROC.
    // Do NOT re-wrap: chaining back to hkWndProc causes stack overflow (0xC00000FD).
}

static std::vector<std::string> BuildWhoTriggersLower() {
    std::vector<std::string> out;
    out.reserve(whoTriggers.size());
    for (const auto& t : whoTriggers) out.push_back(LowerAscii(t));
    return out;
}

static const std::vector<std::string> kWhoTriggersLower = BuildWhoTriggersLower();

std::string CleanMinecraftText(const std::string& text);
static std::string SerializeChatComponentToJson(JNIEnv* env, jobject chatComponent);
static bool ExtractPrequeueSpeakerNameFromComponentJson(const std::string& componentJson, std::string& outName);
static bool ExtractViewProfileUuidFromComponentJson(const std::string& componentJson, std::string& outUuid);
static int ExtractSpeakerColorFromRaw(const std::string& rawMsg);
static int ExtractSpeakerColorFromComponentJson(const std::string& componentJson);

static ULONGLONG g_lastChatPollTime = 0;
static const ULONGLONG kChatPollIntervalMs = 25;
static bool g_autoWhoEnabledCached = false;

struct ChatLineSnapshot {
    int updateCounter = 0;
    std::string raw;
    std::string clean;
    jobject component = nullptr;
};

static int g_prevNewestUpdateCounter = -1;
static std::string g_prevNewestChatRaw;
static bool g_chatPollInitialized = false;
static bool g_deferChatPollSync = false;

static void ResetChatPollCursor() {
    g_prevNewestUpdateCounter = -1;
    g_prevNewestChatRaw.clear();
    g_chatPollInitialized = false;
}

static std::string ExtractJsonStringFieldAt(const std::string& json, size_t keyPos, const std::string& key) {
    size_t pos = keyPos + key.size();
    std::string chunk;
    bool esc = false;
    for (; pos < json.size(); ++pos) {
        const char c = json[pos];
        if (esc) {
            chunk.push_back(c);
            esc = false;
            continue;
        }
        if (c == '\\') {
            esc = true;
            continue;
        }
        if (c == '"') break;
        chunk.push_back(c);
    }
    return chunk;
}

static std::string ExtractTextFromComponentJson(const std::string& json) {
    if (json.empty()) return "";
    std::string out;
    const std::string textKey = "\"text\":\"";
    size_t pos = 0;
    while ((pos = json.find(textKey, pos)) != std::string::npos) {
        const std::string chunk = ExtractJsonStringFieldAt(json, pos, textKey);
        if (!chunk.empty()) out += chunk;
        pos += textKey.size();
    }
    return out;
}

static std::string ChatLineLogText(const ChatLineSnapshot& line) {
    if (!line.clean.empty()) return line.clean;
    if (!line.raw.empty()) return CleanMinecraftText(line.raw);
    return "";
}

static std::string ReadIChatComponentText(JNIEnv* env, jobject chatComponent) {
    if (!env || !chatComponent) return "";
    jclass componentClass = env->GetObjectClass(chatComponent);
    if (!componentClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }

    auto readMethod = [&](const char* mcpName, const char* obfName) -> std::string {
        jmethodID mid = JniResolveInstanceMethod(env, componentClass, "()Ljava/lang/String;", mcpName, obfName);
        if (!mid) return "";
        jstring jText = (jstring)env->CallObjectMethod(chatComponent, mid);
        if (!jText || env->ExceptionCheck()) {
            env->ExceptionClear();
            return "";
        }
        const char* str = env->GetStringUTFChars(jText, nullptr);
        std::string out = str ? str : "";
        if (str) env->ReleaseStringUTFChars(jText, str);
        env->DeleteLocalRef(jText);
        return out;
    };

    std::string formatted = readMethod("getFormattedText", "d");
    std::string unformatted = readMethod("getUnformattedText", "c");
    std::string out = formatted.size() >= unformatted.size() ? formatted : unformatted;
    env->DeleteLocalRef(componentClass);
    return out;
}

static jobject ResolveChatLinesList(JNIEnv* env, jobject chatGuiObj, jclass chatGuiClass) {
    if (!env || !chatGuiObj || !chatGuiClass) return nullptr;

    static const char* kFieldNames[] = {"chatLines", "drawnChatLines", "h", "i"};
    for (const char* name : kFieldNames) {
        jfieldID fid = env->GetFieldID(chatGuiClass, name, "Ljava/util/List;");
        if (!fid || env->ExceptionCheck()) {
            env->ExceptionClear();
            continue;
        }
        jobject list = env->GetObjectField(chatGuiObj, fid);
        if (list && !env->ExceptionCheck()) return list;
        env->ExceptionClear();
        if (list) env->DeleteLocalRef(list);
    }
    return nullptr;
}

static bool CollectVisibleChatLines(JNIEnv* env,
                                    jobject mcObj,
                                    jclass mcClass,
                                    std::vector<ChatLineSnapshot>& out,
                                    int maxScan = 100) {
    out.clear();
    if (!env || !mcObj || !mcClass) return false;

    jfieldID fidIngameGuiReader = env->GetFieldID(mcClass, "ingameGUI", "Lnet/minecraft/client/gui/GuiIngame;");
    if (!fidIngameGuiReader || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jobject ingameGui = env->GetObjectField(mcObj, fidIngameGuiReader);
    if (!ingameGui || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jclass ingameGuiClass = env->GetObjectClass(ingameGui);
    jmethodID midGetChatGui = ingameGuiClass ? env->GetMethodID(ingameGuiClass, "getChatGUI", "()Lnet/minecraft/client/gui/GuiNewChat;") : nullptr;
    if (!midGetChatGui || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(ingameGuiClass);
        env->DeleteLocalRef(ingameGui);
        return false;
    }

    jobject chatGui = env->CallObjectMethod(ingameGui, midGetChatGui);
    if (!chatGui || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(ingameGuiClass);
        env->DeleteLocalRef(ingameGui);
        return false;
    }

    jobject chatStorage = ResolveChatGuiDelegate(env, chatGui);

    jclass chatGuiClass2 = env->GetObjectClass(chatStorage);
    jobject chatLinesList = ResolveChatLinesList(env, chatStorage, chatGuiClass2);
    if (!chatLinesList && chatGuiClass2 && chatGui != chatStorage) {
        jclass outerClass = env->GetObjectClass(chatGui);
        if (outerClass && !env->ExceptionCheck()) {
            chatLinesList = ResolveChatLinesList(env, chatGui, outerClass);
            env->DeleteLocalRef(outerClass);
        } else {
            env->ExceptionClear();
        }
    }
    if (!chatLinesList || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(chatGuiClass2);
        env->DeleteLocalRef(chatStorage);
        env->DeleteLocalRef(ingameGuiClass);
        env->DeleteLocalRef(ingameGui);
        return false;
    }

    jclass listClass = env->GetObjectClass(chatLinesList);
    jmethodID midSize = listClass ? env->GetMethodID(listClass, "size", "()I") : nullptr;
    jmethodID midGet = listClass ? env->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;") : nullptr;
    if (!midSize || !midGet || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(listClass);
        env->DeleteLocalRef(chatLinesList);
        env->DeleteLocalRef(chatGuiClass2);
        env->DeleteLocalRef(chatStorage);
        env->DeleteLocalRef(ingameGuiClass);
        env->DeleteLocalRef(ingameGui);
        return false;
    }

    const int size = env->CallIntMethod(chatLinesList, midSize);
    if (size <= 0 || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(listClass);
        env->DeleteLocalRef(chatLinesList);
        env->DeleteLocalRef(chatGuiClass2);
        env->DeleteLocalRef(chatStorage);
        env->DeleteLocalRef(ingameGuiClass);
        env->DeleteLocalRef(ingameGui);
        return false;
    }

    const int scanCount = (std::min)(size, maxScan);
    out.reserve((size_t)scanCount);
    for (int i = 0; i < scanCount; ++i) {
        jobject lineObj = env->CallObjectMethod(chatLinesList, midGet, i);
        if (!lineObj || env->ExceptionCheck()) {
            env->ExceptionClear();
            continue;
        }

        jclass chatLineClass = env->GetObjectClass(lineObj);
        jmethodID midGetComponent = nullptr;
        if (chatLineClass) {
            static const char* kGetComponentNames[] = {"getChatComponent", "a"};
            static const char* kGetComponentSigs[] = {
                "()Lnet/minecraft/util/IChatComponent;",
                "()Lnet/minecraft/util/IChatComponent;",
            };
            midGetComponent = JniResolveInstanceMethodAny(env, chatLineClass,
                kGetComponentNames, kGetComponentSigs, 2);
        }
        if (!midGetComponent) {
            env->DeleteLocalRef(chatLineClass);
            env->DeleteLocalRef(lineObj);
            continue;
        }

        jobject chatComponent = env->CallObjectMethod(lineObj, midGetComponent);
        if (!chatComponent || env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(chatLineClass);
            env->DeleteLocalRef(lineObj);
            continue;
        }

        int updateCounter = 0;
        jmethodID midGetUpdateCounter = JniResolveInstanceMethod(env, chatLineClass, "()I",
                                                                 "getUpdatedCounter", "b");
        if (midGetUpdateCounter) {
            updateCounter = env->CallIntMethod(lineObj, midGetUpdateCounter);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                updateCounter = 0;
            }
        }

        ChatLineSnapshot line;
        line.updateCounter = updateCounter;
        const std::string componentJson = SerializeChatComponentToJson(env, chatComponent);
        line.raw = ReadIChatComponentText(env, chatComponent);
        const std::string jsonText = ExtractTextFromComponentJson(componentJson);
        if (jsonText.size() > line.raw.size()) line.raw = jsonText;
        else if (line.raw.empty()) line.raw = jsonText;
        line.clean = CleanMinecraftText(line.raw);
        line.component = chatComponent;
        if (line.raw.empty() && line.clean.empty()) {
            env->DeleteLocalRef(chatComponent);
            env->DeleteLocalRef(chatLineClass);
            env->DeleteLocalRef(lineObj);
            continue;
        }
        out.push_back(std::move(line));

        env->DeleteLocalRef(chatLineClass);
        env->DeleteLocalRef(lineObj);
    }

    env->DeleteLocalRef(listClass);
    env->DeleteLocalRef(chatLinesList);
    env->DeleteLocalRef(chatGuiClass2);
    env->DeleteLocalRef(chatStorage);
    env->DeleteLocalRef(ingameGuiClass);
    env->DeleteLocalRef(ingameGui);
    return !out.empty();
}

void SyncChatPollAfterRewrite(JNIEnv* env, jobject mcObj, jclass mcClass) {
    std::vector<ChatLineSnapshot> lines;
    if (!CollectVisibleChatLines(env, mcObj, mcClass, lines, 1)) {
        ResetChatPollCursor();
        return;
    }
    g_chatPollInitialized = true;
    g_prevNewestUpdateCounter = lines[0].updateCounter;
    g_prevNewestChatRaw = lines[0].raw;
    if (lines[0].component) env->DeleteLocalRef(lines[0].component);
}

static bool MessageMatchesWhoTrigger(const std::string& msg) {
    if (msg.empty()) return false;
    const std::string lower = LowerAscii(msg);
    for (const auto& trigger : kWhoTriggersLower) {
        if (lower.find(trigger) != std::string::npos) return true;
    }
    for (const auto& trigger : whoTriggers) {
        if (msg.find(trigger) != std::string::npos) return true;
    }
    return false;
}

static bool ReadAutoWhoEnabled(JNIEnv* env, jclass customGuiClass) {
    if (customGuiClass && env) {
        jfieldID fid = env->GetStaticFieldID(customGuiClass, "autoWho", "Z");
        if (fid && !env->ExceptionCheck()) {
            g_autoWhoEnabledCached = (bool)env->GetStaticBooleanField(customGuiClass, fid);
            if (env->ExceptionCheck()) env->ExceptionClear();
            return g_autoWhoEnabledCached;
        }
        env->ExceptionClear();
    }
    return g_autoWhoEnabledCached;
}

static void TryScheduleAutoWho(const std::string& rawMsg, const std::string& cleanMsg) {
    if (!MessageMatchesWhoTrigger(cleanMsg) && !MessageMatchesWhoTrigger(rawMsg)) return;
    const ULONGLONG now = GetTickCount64();
    if (now - lastTriggerTime < triggerCooldown) return;
    lastTriggerTime = now;
    scheduledWhoTime = now + 400;
    whoRetryCount = 0;
}

static bool SendChatCommand(JNIEnv* env, jobject mcObj, jclass mcClass, const std::string& cmd) {
    if (!env || !mcObj || !mcClass || cmd.empty()) return false;

    jfieldID fidThePlayer = env->GetFieldID(mcClass, "thePlayer", "Lnet/minecraft/client/entity/EntityPlayerSP;");
    if (!fidThePlayer || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jobject thePlayer = env->GetObjectField(mcObj, fidThePlayer);
    if (!thePlayer || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jclass playerClass = env->GetObjectClass(thePlayer);
    if (!playerClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(thePlayer);
        return false;
    }

    static const char* kSendMethodNames[] = {"sendChatMessage", "e", "f", "a"};
    bool sent = false;
    for (const char* methodName : kSendMethodNames) {
        jmethodID midSend = env->GetMethodID(playerClass, methodName, "(Ljava/lang/String;)V");
        if (!midSend || env->ExceptionCheck()) {
            env->ExceptionClear();
            continue;
        }
        jstring jCmd = env->NewStringUTF(cmd.c_str());
        if (!jCmd) continue;
        env->CallVoidMethod(thePlayer, midSend, jCmd);
        env->DeleteLocalRef(jCmd);
        if (!env->ExceptionCheck()) {
            sent = true;
            break;
        }
        env->ExceptionClear();
    }

    env->DeleteLocalRef(playerClass);
    env->DeleteLocalRef(thePlayer);
    return sent;
}

static void ExecutePendingAutoWho(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (scheduledWhoTime == 0 || GetTickCount64() < scheduledWhoTime) return;

    const bool sent = SendChatCommand(env, mcObj, mcClass, "/who");
    if (sent) {
        scheduledWhoTime = 0;
        whoRetryCount = 0;
        g_tabOverlay.resetWhoSession();
        return;
    }

    whoRetryCount++;
    if (whoRetryCount <= 8) {
        scheduledWhoTime = GetTickCount64() + 250;
    } else {
        scheduledWhoTime = 0;
        whoRetryCount = 0;
    }
}

static void ExecutePendingAutoGL(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (scheduledGLTime == 0 || GetTickCount64() < scheduledGLTime) return;
    scheduledGLTime = 0;
    if (SendChatCommand(env, mcObj, mcClass, pendingGLMessage)) {
        lastAutoGLTime = GetTickCount64();
    }
}

static void CloseNickBookIfOpen(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (!env || !mcObj || !mcClass) return;

    jfieldID fidCurrentScreen = env->GetFieldID(mcClass, "currentScreen", "Lnet/minecraft/client/gui/GuiScreen;");
    if (!fidCurrentScreen || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    jobject currentScreen = env->GetObjectField(mcObj, fidCurrentScreen);
    if (!currentScreen || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    jclass bookClass = McFindClass(env, "net/minecraft/client/gui/GuiScreenBook");
    if (!bookClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(currentScreen);
        return;
    }

    const bool isBook = env->IsInstanceOf(currentScreen, bookClass) == JNI_TRUE;
    env->DeleteLocalRef(bookClass);
    env->DeleteLocalRef(currentScreen);
    if (!isBook) return;

    jmethodID midDisplayGui = env->GetMethodID(mcClass, "displayGuiScreen", "(Lnet/minecraft/client/gui/GuiScreen;)V");
    if (!midDisplayGui || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    env->CallVoidMethod(mcObj, midDisplayGui, (jobject)nullptr);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

static void ExecutePendingWantToBet(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (scheduledWantToBetTime == 0 || GetTickCount64() < scheduledWantToBetTime) return;
    scheduledWantToBetTime = 0;
    SendChatCommand(env, mcObj, mcClass, "/wanttobet true");
}

static void ExecutePendingNickQol(JNIEnv* env, jobject mcObj, jclass mcClass, jclass customGuiClass) {
    if (scheduledNickQolTime == 0 || GetTickCount64() < scheduledNickQolTime) return;
    scheduledNickQolTime = 0;
    if (customGuiClass) {
        jfieldID fid = env->GetStaticFieldID(customGuiClass, "nickQolPendingSend", "Z");
        if (fid && !env->ExceptionCheck()) {
            env->SetStaticBooleanField(customGuiClass, fid, JNI_FALSE);
        } else {
            env->ExceptionClear();
        }
    }
    CloseNickBookIfOpen(env, mcObj, mcClass);
    SendChatCommand(env, mcObj, mcClass, "/nick help setrandom");
}

static void PollWorldJoinForLocraw(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (!env || !mcObj || !mcClass) return;

    jfieldID fidWorld = env->GetFieldID(mcClass, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");
    if (!fidWorld || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    jobject world = env->GetObjectField(mcObj, fidWorld);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    if (!world) {
        if (g_lastWorldObjGlobal) {
            env->DeleteGlobalRef(g_lastWorldObjGlobal);
            g_lastWorldObjGlobal = nullptr;
        }
        return;
    }

    bool worldChanged = false;
    if (!g_lastWorldObjGlobal) {
        worldChanged = true;
        g_lastWorldObjGlobal = env->NewGlobalRef(world);
    } else if (!env->IsSameObject(world, g_lastWorldObjGlobal)) {
        worldChanged = true;
        env->DeleteGlobalRef(g_lastWorldObjGlobal);
        g_lastWorldObjGlobal = env->NewGlobalRef(world);
    }

    env->DeleteLocalRef(world);

    if (worldChanged) {
        PartyQueueDetectorOnWorldLoad();
#ifndef FRENCHIFY_LUNAR
        scheduledLocrawTime = GetTickCount64() + 500;
#endif
    }
}

#ifndef FRENCHIFY_LUNAR
static void ExecutePendingAutoLocraw(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (scheduledLocrawTime == 0 || GetTickCount64() < scheduledLocrawTime) return;
    scheduledLocrawTime = 0;
    SendChatCommand(env, mcObj, mcClass, "/locraw");
}
#endif

static std::string ExtractJsonString(const std::string& json, const std::string& key);

static std::string DetectOverlayGamemodeFromChat(const std::string& msg) {
    const std::string lower = LowerAscii(msg);
    if (lower.find("bed wars duels") != std::string::npos ||
        lower.find("bedwars duels") != std::string::npos) {
        return "bedwarsduels";
    }
    if (msg.find("Protect your bed") != std::string::npos ||
        msg.find("Players swap teams") != std::string::npos ||
        msg.find("All generators are maxed") != std::string::npos ||
        msg.find("Select an ultimate") != std::string::npos ||
        msg.find("Collect Lucky Blocks") != std::string::npos ||
        msg.find("Become a Soul Collector") != std::string::npos ||
        msg.find("Every few seconds") != std::string::npos) {
        return "bedwars";
    }
    if (msg.find("Gather resources") != std::string::npos) return "skywars";
    if (msg.find("Opponents:") != std::string::npos || msg.find("Opponent:") != std::string::npos) return "duel";
    if (lower.find("murder mystery") != std::string::npos ||
        lower.find("you are the murderer") != std::string::npos ||
        lower.find("you are a detective") != std::string::npos ||
        lower.find("you are innocent") != std::string::npos) {
        return "murdermystery";
    }
    return "";
}

static std::string DetectOverlayGamemodeFromWhoHeader(const std::string& msg) {
    const std::string lower = LowerAscii(Trim(msg));
    if (lower.rfind("mode:", 0) != 0) return "";
    if ((lower.find("bed wars") != std::string::npos || lower.find("bedwars") != std::string::npos) &&
        lower.find("duel") != std::string::npos) {
        return "bedwarsduels";
    }
    if (lower.find("sky wars") != std::string::npos || lower.find("skywars") != std::string::npos) return "skywars";
    if (lower.find("murder mystery") != std::string::npos || lower.find("murdermystery") != std::string::npos) return "murdermystery";
    if (lower.find("bed wars") != std::string::npos || lower.find("bedwars") != std::string::npos) return "bedwars";
    if (lower.find("duel") != std::string::npos) return "duel";
    return "";
}

static std::string DetectOverlayGamemodeFromScoreboardTitle(const std::string& title) {
    if (title.empty()) return "";
    const std::string lower = LowerAscii(Trim(title));
    const bool hasDuel = lower.find("duel") != std::string::npos;
    const bool hasBedWars = lower.find("bed wars") != std::string::npos || lower.find("bedwars") != std::string::npos;
    const bool hasSkyWars = lower.find("sky wars") != std::string::npos || lower.find("skywars") != std::string::npos;
    const bool hasMurder = lower.find("murder") != std::string::npos;
    if (hasBedWars && hasDuel) return "bedwarsduels";
    if (hasMurder) return "murdermystery";
    if (hasBedWars) return "bedwars";
    if (hasSkyWars) return "skywars";
    if (hasDuel) return "duel";
    return "";
}

static std::string DetectOverlayGamemodeFromLocraw(const std::string& msg) {
    if (!IsHypixelLocrawChatLine(msg)) return "";
    const std::string gametype = ExtractJsonString(msg, "gametype");
    const std::string mode = ExtractJsonString(msg, "mode");
    const std::string gametypeLower = LowerAscii(gametype);
    const std::string modeLower = LowerAscii(mode);
    if (gametypeLower.find("bedwars") != std::string::npos ||
        modeLower.find("bedwars") != std::string::npos) {
        if (gametypeLower.find("duel") != std::string::npos || modeLower.find("duel") != std::string::npos) {
            return "bedwarsduels";
        }
        return "bedwars";
    }
    if (gametypeLower.find("skywars") != std::string::npos || modeLower.find("skywars") != std::string::npos) {
        return "skywars";
    }
    if (gametypeLower.find("murder") != std::string::npos || modeLower.find("murder") != std::string::npos) {
        return "murdermystery";
    }
    if (gametypeLower.find("duel") != std::string::npos || modeLower.find("duel") != std::string::npos) {
        return "duel";
    }
    return "";
}

static void ApplyOverlayGamemodeIfChanged(JNIEnv* env, jclass customGuiClass, const std::string& mode) {
    if (mode.empty() || mode == g_overlayGamemodeActiveLast) return;
    g_overlayGamemodeActiveLast = mode;
    g_overlayColumnsConfigLast.clear();
    taboverlay::SetOverlayActiveGamemode(taboverlay::OverlayGamemodeFromString(mode));
    if (!env || !customGuiClass) return;
    jmethodID mid = env->GetStaticMethodID(customGuiClass, "setOverlayGamemodeActive", "(Ljava/lang/String;)V");
    if (!mid || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    jstring jMode = env->NewStringUTF(mode.c_str());
    if (!jMode) return;
    env->CallStaticVoidMethod(customGuiClass, mid, jMode);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(jMode);
}

static void ApplyOverlayGamemodeFromLocraw(JNIEnv* env, jclass customGuiClass, const std::string& msg) {
    const std::string mode = DetectOverlayGamemodeFromLocraw(msg);
    if (mode.empty()) return;
    ApplyOverlayGamemodeIfChanged(env, customGuiClass, mode);
}

static void ApplyOverlayGamemodeFromChat(JNIEnv* env, jclass customGuiClass, const std::string& msg) {
    const std::string mode = DetectOverlayGamemodeFromChat(msg);
    if (mode.empty()) return;
    ApplyOverlayGamemodeIfChanged(env, customGuiClass, mode);
}

static void ApplyOverlayGamemodeFromScoreboard(JNIEnv* env, jclass customGuiClass, const std::string& title) {
    const std::string mode = DetectOverlayGamemodeFromScoreboardTitle(title);
    if (mode.empty()) return;
    // BW Duels scoreboard title is often just "Bed Wars" — keep chat/who mode.
    if (mode == "bedwars" && g_overlayGamemodeActiveLast == "bedwarsduels") return;
    ApplyOverlayGamemodeIfChanged(env, customGuiClass, mode);
}

static void ApplyOverlayGamemodeFromWhoHeader(JNIEnv* env, jclass customGuiClass, const std::string& msg) {
    const std::string mode = DetectOverlayGamemodeFromWhoHeader(msg);
    if (mode.empty()) return;
    ApplyOverlayGamemodeIfChanged(env, customGuiClass, mode);
}

static bool IsFrenchifyOwnedChat(const std::string& rawMsg, const std::string& cleanMsg) {
    if (cleanMsg.find("Frenchify") != std::string::npos) return true;
    if (rawMsg.find("Frenchify") != std::string::npos) return true;
    if (cleanMsg.find('[') != std::string::npos &&
        cleanMsg.find("Fre") != std::string::npos &&
        cleanMsg.find("nch") != std::string::npos &&
        cleanMsg.find("ify") != std::string::npos) {
        return true;
    }
    return false;
}

static void ProcessChatMessage(JNIEnv* env,
                               jobject mcObj,
                               jclass mcClass,
                               jclass customGuiClass,
                               jobject chatComponent,
                               const std::string& rawMsg,
                               const std::string& msg);

static bool ShouldSuppressIncomingChat(JNIEnv* env, jobject chatComponent) {
    if (!env || !chatComponent) return false;
    const std::string componentJson = SerializeChatComponentToJson(env, chatComponent);
    std::string rawMsg = ReadIChatComponentText(env, chatComponent);
    const std::string jsonText = ExtractTextFromComponentJson(componentJson);
    if (jsonText.size() > rawMsg.size()) rawMsg = jsonText;
    else if (rawMsg.empty()) rawMsg = jsonText;
    const std::string cleanMsg = CleanMinecraftText(rawMsg);
    return IsHypixelLocrawChatLine(cleanMsg);
}

static void HandleIncomingChatFromHook(JNIEnv* env, jobject chatComponent) {
    if (!env || !chatComponent || IsChatHookSuppressed()) return;

    jclass mcClass = g_mcClassGlobal ? g_mcClassGlobal : McFindClass(env, "net/minecraft/client/Minecraft");
    if (!mcClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    const bool mcClassLocal = (mcClass != g_mcClassGlobal);

    jmethodID midGetMc = g_midGetMinecraftStatic;
    if (!midGetMc) {
        midGetMc = env->GetStaticMethodID(mcClass, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
        if (!midGetMc || env->ExceptionCheck()) {
            env->ExceptionClear();
            if (mcClassLocal) env->DeleteLocalRef(mcClass);
            return;
        }
    }

    jobject mcObj = env->CallStaticObjectMethod(mcClass, midGetMc);
    if (!mcObj || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (mcClassLocal) env->DeleteLocalRef(mcClass);
        return;
    }

    const std::string componentJson = SerializeChatComponentToJson(env, chatComponent);
    std::string rawMsg = ReadIChatComponentText(env, chatComponent);
    const std::string jsonText = ExtractTextFromComponentJson(componentJson);
    if (jsonText.size() > rawMsg.size()) rawMsg = jsonText;
    else if (rawMsg.empty()) rawMsg = jsonText;
    const std::string cleanMsg = CleanMinecraftText(rawMsg);
    if (cleanMsg.empty() || IsFrenchifyOwnedChat(rawMsg, cleanMsg)) {
        env->DeleteLocalRef(mcObj);
        if (mcClassLocal) env->DeleteLocalRef(mcClass);
        return;
    }

    ProcessChatMessage(env, mcObj, mcClass, g_customGuiClassGlobal,
                       chatComponent, rawMsg, cleanMsg);

    env->DeleteLocalRef(mcObj);
    if (mcClassLocal) env->DeleteLocalRef(mcClass);
}

extern "C" JNIEXPORT void JNICALL Java_FrenchifyGuiNewChat_nativeOnIncomingChat(JNIEnv* env,
                                                                                jclass,
                                                                                jobject chatComponent) {
    HandleIncomingChatFromHook(env, chatComponent);
}

extern "C" JNIEXPORT jboolean JNICALL Java_FrenchifyGuiNewChat_nativeShouldSuppressIncomingChat(
    JNIEnv* env, jclass, jobject chatComponent) {
    return ShouldSuppressIncomingChat(env, chatComponent) ? JNI_TRUE : JNI_FALSE;
}

static void ProcessChatMessage(JNIEnv* env,
                               jobject mcObj,
                               jclass mcClass,
                               jclass customGuiClass,
                               jobject chatComponent,
                               const std::string& rawMsg,
                               const std::string& msg) {
    if (msg.empty()) return;
    if (IsFrenchifyOwnedChat(rawMsg, msg)) return;

    if (IsHypixelLocrawChatLine(msg)) {
        ApplyOverlayGamemodeFromLocraw(env, customGuiClass, msg);
        const std::string locrawMode = ExtractJsonString(msg, "mode");
        PartyQueueDetectorSetModeFromLocraw(locrawMode);
        taboverlay::SetBedwarsLocrawMode(locrawMode);
    }

    // BedWars: chat can announce "Bed gone" before/without the sidebar updating.
    // Force invalidate resource cache so Emeralds/Diamonds don't freeze until next event.
    {
        const std::string lower = LowerAscii(msg);
        if (lower.find("bed gone") != std::string::npos) {
            taboverlay::ForceClearResourcesCache();
        }
        if (lower.find(" disconnected.") != std::string::npos) {
            taboverlay::OnBedwarsDisconnectChatLine(msg);
        }
        if (lower.find(" reconnected.") != std::string::npos) {
            taboverlay::OnBedwarsReconnectChatLine(msg);
        }
        if (lower.find("sending you to") != std::string::npos) {
            g_tabOverlay.clearPrequeueGhosts();
        }
    }

    const bool skipWhoParty = IsIgnoredChatForWhoParty(msg);

    if (!skipWhoParty) {
        ApplyOverlayGamemodeFromChat(env, customGuiClass, msg);

        if (IsWhoModeHeaderLine(msg)) {
            ApplyOverlayGamemodeFromWhoHeader(env, customGuiClass, msg);
        }

        std::vector<std::string> whoOnlineNames = ParseOnlineNamesFromMessage(msg);
        if (!whoOnlineNames.empty()) {
            if (IsWhoFullOnlineLine(msg)) {
                g_tabOverlay.addPlayersFromWhoList(whoOnlineNames);
            } else {
                g_tabOverlay.mergePlayersFromWhoList(whoOnlineNames);
                taboverlay::ApplyWhoTeamLine(whoOnlineNames);
            }
            g_inGameFromWho = true;
            g_inGameFromWhoSetAt = GetTickCount64();
            taboverlay::NumberDenickerSetInGame(true);
            g_tabOverlay.clearPrequeueGhosts();
        }
    }

    if (g_modeState == "Prequeue" && chatComponent && !skipWhoParty) {
        const std::string componentJson = SerializeChatComponentToJson(env, chatComponent);
        std::string speakerName;
        if (ExtractPrequeueSpeakerNameFromComponentJson(componentJson, speakerName) && !speakerName.empty()) {
            int speakerColor = ExtractSpeakerColorFromComponentJson(componentJson);
            if (speakerColor < 0) speakerColor = ExtractSpeakerColorFromRaw(rawMsg);
            g_tabOverlay.addOnlinePlayerForApi(speakerName, speakerColor);
            std::string viewProfileUuid;
            if (ExtractViewProfileUuidFromComponentJson(componentJson, viewProfileUuid) && !viewProfileUuid.empty()) {
                g_tabOverlay.addSpeakerUuidForApi(speakerName, viewProfileUuid);
            }
        }
    }

    if (!skipWhoParty && ReadAutoWhoEnabled(env, customGuiClass)) {
        TryScheduleAutoWho(rawMsg, msg);
    }

    bool isPartyDetectorEnabled = false;
    bool isAutoGLEnabled = false;
    std::string glMessage = "gg";
    if (customGuiClass) {
        jfieldID fidParty = env->GetStaticFieldID(customGuiClass, "partyDetector", "Z");
        if (fidParty && !env->ExceptionCheck()) {
            isPartyDetectorEnabled = (bool)env->GetStaticBooleanField(customGuiClass, fidParty);
        } else {
            env->ExceptionClear();
        }

        jfieldID fidAutoGL = env->GetStaticFieldID(customGuiClass, "autoGL", "Z");
        if (fidAutoGL && !env->ExceptionCheck()) {
            isAutoGLEnabled = (bool)env->GetStaticBooleanField(customGuiClass, fidAutoGL);
        } else {
            env->ExceptionClear();
        }

        jfieldID fidGlMsg = env->GetStaticFieldID(customGuiClass, "gl_message", "Ljava/lang/String;");
        if (fidGlMsg && !env->ExceptionCheck()) {
            jstring jGl = (jstring)env->GetStaticObjectField(customGuiClass, fidGlMsg);
            if (jGl) {
                const char* cgl = env->GetStringUTFChars(jGl, nullptr);
                if (cgl) glMessage = cgl;
                if (cgl) env->ReleaseStringUTFChars(jGl, cgl);
                env->DeleteLocalRef(jGl);
            }
        } else {
            env->ExceptionClear();
        }
    }

    if (customGuiClass && ReadGuiBool(env, customGuiClass, "arrowDistance", false)) {
        if (TryHandleBowDistanceHpChat(env, mcObj, mcClass, chatComponent, rawMsg, msg)) {
            if (!g_useNativeChatHook) g_deferChatPollSync = true;
            return;
        }
    }

    if (msg.find("The game starts in 10 seconds!") != std::string::npos) {
        taboverlay::DenickOnGameStarted();
        if (isAutoGLEnabled) {
            const ULONGLONG now = GetTickCount64();
            if (now - lastAutoGLTime >= autoGLCooldown) {
                scheduledGLTime = now + 250;
                if (glMessage.empty()) glMessage = "gg";
                pendingGLMessage = glMessage;
            }
        }
    }

    if (customGuiClass && ReadGuiBool(env, customGuiClass, "numberDenicker", false)) {
        taboverlay::NumberDenickerOnChatLine(msg);
    }

    if (isPartyDetectorEnabled && !skipWhoParty && IsPartyTrackerScoreboardActive()) {
        if (msg.find(" has joined (") != std::string::npos && msg.find("/") != std::string::npos) {
            const std::string player = LowerAscii(ExtractPartyEventPlayerName(msg, " has joined ("));
            if (!player.empty() && g_partyJoinPlayersSeen.insert(player).second) {
                partyJoinCount++;
            }
            lastPartyJoinTime = GetTickCount64();
        } else if (msg.find(" has quit!") != std::string::npos) {
            const std::string player = LowerAscii(ExtractPartyEventPlayerName(msg, " has quit!"));
            if (!player.empty() && g_partyQuitPlayersSeen.insert(player).second) {
                partyQuitCount++;
            }
            lastPartyQuitTime = GetTickCount64();
        }
    }

    SessionStatsOnChat(env, mcObj, mcClass, msg);

    if (customGuiClass) {
        const bool georgeEnabled = ReadGuiBool(env, customGuiClass, "george", false);
        if (!georgeEnabled) {
            g_georgeWinCount = 0;
        } else {
            const std::string trimmed = Trim(msg);
            const std::string lower = LowerAscii(msg);
            bool isWin = false;
            if (lower.find("(win)") != std::string::npos && lower.find("token") != std::string::npos && msg.find('+') != std::string::npos) {
                isWin = true;
            } else if (trimmed == "Victory!" || trimmed.rfind("Victory!", 0) == 0) {
                isWin = true;
            } else if (lower.find("you won") != std::string::npos && trimmed.find(':') == std::string::npos) {
                isWin = true;
            }
            if (isWin) {
                g_georgeWinCount++;
                if (g_georgeWinCount % 2 == 0) {
                    ShowFrenchifyToast("Time To Bet", "");
                }
            }
            if (lower.find("+200") != std::string::npos && lower.find("slumber tickets") != std::string::npos) {
                scheduledWantToBetTime = GetTickCount64() + 150;
            }
        }
    }
}

static void PollMinecraftChat(JNIEnv* env, jobject mcObj, jclass mcClass, jclass customGuiClass) {
    if (g_useNativeChatHook) return;
    if (!env || !mcObj || !mcClass) return;

    const ULONGLONG now = GetTickCount64();
    if (now - g_lastChatPollTime < kChatPollIntervalMs) return;
    g_lastChatPollTime = now;

    std::vector<ChatLineSnapshot> lines;
    if (!CollectVisibleChatLines(env, mcObj, mcClass, lines)) {
        if (g_chatPollInitialized) {
            LogInitOnce("[WARN] Chat poll: CollectVisibleChatLines failed");
        }
        return;
    }

    auto releaseLines = [&]() {
        for (auto& line : lines) {
            if (line.component) env->DeleteLocalRef(line.component);
        }
    };

    if (!g_chatPollInitialized) {
        g_chatPollInitialized = true;
        if (!lines.empty()) {
            g_prevNewestUpdateCounter = lines[0].updateCounter;
            g_prevNewestChatRaw = lines[0].raw;
        }
        releaseLines();
        return;
    }

    if (lines.empty()) {
        releaseLines();
        return;
    }

    if (g_prevNewestUpdateCounter >= 0 &&
        lines[0].updateCounter == g_prevNewestUpdateCounter &&
        lines[0].raw != g_prevNewestChatRaw) {
        ProcessChatMessage(env, mcObj, mcClass, customGuiClass,
                           lines[0].component, lines[0].raw, lines[0].clean);
        g_prevNewestChatRaw = lines[0].raw;
        releaseLines();
        return;
    }

    std::vector<size_t> pending;
    pending.reserve(lines.size());
    int maxCounter = g_prevNewestUpdateCounter;
    for (size_t i = 0; i < lines.size(); ++i) {
        const ChatLineSnapshot& line = lines[i];
        if (g_prevNewestUpdateCounter >= 0 && line.updateCounter <= g_prevNewestUpdateCounter) {
            break;
        }
        pending.push_back(i);
        if (line.updateCounter > maxCounter) maxCounter = line.updateCounter;
    }

    if (pending.empty()) {
        releaseLines();
        return;
    }

    for (auto it = pending.rbegin(); it != pending.rend(); ++it) {
        const ChatLineSnapshot& line = lines[*it];
        ProcessChatMessage(env, mcObj, mcClass, customGuiClass, line.component, line.raw, line.clean);
    }

    if (g_deferChatPollSync) {
        g_deferChatPollSync = false;
        SyncChatPollAfterRewrite(env, mcObj, mcClass);
    } else {
        g_prevNewestUpdateCounter = maxCounter;
        g_prevNewestChatRaw = lines[0].raw;
    }
    releaseLines();
}

static std::vector<std::string> SplitCsvList(const std::string& csv);

static std::string ExtractJsonString(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    ++pos;

    std::string out;
    bool esc = false;
    for (; pos < json.size(); ++pos) {
        char c = json[pos];
        if (esc) {
            if (c == 'n') out.push_back('\n');
            else if (c == 'r') out.push_back('\r');
            else if (c == 't') out.push_back('\t');
            else out.push_back(c);
            esc = false;
            continue;
        }
        if (c == '\\') {
            esc = true;
            continue;
        }
        if (c == '"') break;
        out.push_back(c);
    }
    return out;
}

static bool ExtractJsonBool(const std::string& json, const std::string& key, bool fallback) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return fallback;
    size_t start = json.find_first_not_of(" \t\r\n", pos + 1);
    if (start == std::string::npos) return fallback;
    if (json.compare(start, 4, "true") == 0) return true;
    if (json.compare(start, 5, "false") == 0) return false;
    return fallback;
}

static int ExtractJsonInt(const std::string& json, const std::string& key, int fallback) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return fallback;
    size_t start = json.find_first_not_of(" \t\r\n", pos + 1);
    if (start == std::string::npos) return fallback;
    size_t end = start;
    if (json[end] == '-') ++end;
    while (end < json.size() && (json[end] >= '0' && json[end] <= '9')) ++end;
    if (end <= start) return fallback;
    return atoi(json.substr(start, end - start).c_str());
}

static int ParseHexColorRgb(const std::string& input, int fallbackRgb) {
    std::string s;
    s.reserve(input.size());
    for (char c : input) {
        if (c == '#') continue;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        s.push_back(c);
    }
    if (s.size() != 6) return (fallbackRgb & 0xFFFFFF);
    for (char c : s) {
        const bool hex = (c >= '0' && c <= '9') ||
                         (c >= 'a' && c <= 'f') ||
                         (c >= 'A' && c <= 'F');
        if (!hex) return (fallbackRgb & 0xFFFFFF);
    }
    return (int)strtoul(s.c_str(), nullptr, 16) & 0xFFFFFF;
}

static std::string LoadTogglesJsonFromFile() {
    if (g_dllDir.empty()) return "";
    std::ifstream fin(g_dllDir + "\\toggles.json", std::ios::in | std::ios::binary);
    if (!fin.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
}

static void ApplySavedTogglesToGui(JNIEnv* env, jclass customGuiClass, const std::string& json) {
    if (!env || !customGuiClass || json.empty()) return;

    auto setBool = [&](const char* fieldName, bool value) {
        jfieldID fid = env->GetStaticFieldID(customGuiClass, fieldName, "Z");
        if (fid && !env->ExceptionCheck()) {
            env->SetStaticBooleanField(customGuiClass, fid, value ? JNI_TRUE : JNI_FALSE);
            if (env->ExceptionCheck()) env->ExceptionClear();
        } else {
            env->ExceptionClear();
        }
    };

    auto setString = [&](const char* fieldName, const std::string& value) {
        jfieldID fid = env->GetStaticFieldID(customGuiClass, fieldName, "Ljava/lang/String;");
        if (!fid || env->ExceptionCheck()) {
            env->ExceptionClear();
            return;
        }
        jstring jv = env->NewStringUTF(value.c_str());
        if (!jv) return;
        env->SetStaticObjectField(customGuiClass, fid, jv);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(jv);
    };

    auto setInt = [&](const char* fieldName, int value) {
        jfieldID fid = env->GetStaticFieldID(customGuiClass, fieldName, "I");
        if (fid && !env->ExceptionCheck()) {
            env->SetStaticIntField(customGuiClass, fid, (jint)value);
            if (env->ExceptionCheck()) env->ExceptionClear();
        } else {
            env->ExceptionClear();
        }
    };

    g_autoWhoEnabledCached = ExtractJsonBool(json, "autoWho", false);
    setBool("autoWho", g_autoWhoEnabledCached);
    setBool("partyDetector", ExtractJsonBool(json, "partyDetector", false));
    setBool("partyQueueDetector", ExtractJsonBool(json, "partyQueueDetector", false));
    setBool("bedDisconnectTimer", ExtractJsonBool(json, "bedDisconnectTimer", false));

    setBool("numberDenicker", ExtractJsonBool(json, "numberDenicker", false));
    setBool("autoGL", ExtractJsonBool(json, "autoglEnabled", false));
    setBool("fastBuy", ExtractJsonBool(json, "fastBuy", false));
    setBool("coloredHitboxes", ExtractJsonBool(json, "coloredHitboxes", false));
    setBool("dragonHitboxes", ExtractJsonBool(json, "dragonHitboxes", false));
    setBool("fireballHitboxes", ExtractJsonBool(json, "fireballHitboxes", false));
    {
        std::string fbColor = ExtractJsonString(json, "fireballHitboxColor");
        if (fbColor.empty()) fbColor = "c";
        setString("fireballHitboxColor", fbColor);
    }
    {
        int hitboxThickness = ExtractJsonInt(json, "hitboxThickness", 1);
        if (hitboxThickness < 1) hitboxThickness = 1;
        if (hitboxThickness > 10) hitboxThickness = 10;
        setInt("hitboxThickness", hitboxThickness);
    }
    setBool("arrowDistance", ExtractJsonBool(json, "arrowDistance", false));
    setBool("tradeIndicator", ExtractJsonBool(json, "tradeIndicator", false));
    setBool("george", ExtractJsonBool(json, "george", false));
    setBool("nickQol", ExtractJsonBool(json, "nickQol", false));
    setBool("tabOverlay", ExtractJsonBool(json, "tabOverlay", true));
    setBool("overlayResourceHeader", ExtractJsonBool(json, "overlayResourceHeader", true));
    setBool("overlayResourceDiamonds", ExtractJsonBool(json, "overlayResourceDiamonds", true));
    setBool("overlayResourceEmeralds", ExtractJsonBool(json, "overlayResourceEmeralds", true));
    {
        std::string resourceLoc = ExtractJsonString(json, "overlayResourceLocation");
        if (resourceLoc.empty()) resourceLoc = "tab";
        setString("overlayResourceLocation", resourceLoc);
    }
    setBool("anticheat", ExtractJsonBool(json, "anticheat", false));
    setBool("ac_flagsound", ExtractJsonBool(json, "ac_flagsound", true));
    setBool("ac_autoblock", ExtractJsonBool(json, "ac_autoblock", false));
    {
        const bool legitScaff = ExtractJsonBool(json, "ac_legitscaff", false);
        const bool legitScaffLegacy = ExtractJsonBool(json, "ac_legitscaffb", false);
        setBool("ac_legitscaff", legitScaff || legitScaffLegacy);
    }
    setBool("ac_scaffold", ExtractJsonBool(json, "ac_scaffold", false));
    setBool("ac_scaffoldb", ExtractJsonBool(json, "ac_scaffoldb", false));
    setBool("ac_noslow", ExtractJsonBool(json, "ac_noslow", false));
    setBool("compactBlacklist", ExtractJsonBool(json, "compactBlacklist", false));
    setBool("chatOverlay", ExtractJsonBool(json, "chatOverlay", false));
    setBool("ignoreTeammates", ExtractJsonBool(json, "ignoreTeammates", false));
    setBool("sessionStats", ExtractJsonBool(json, "sessionStats", false));
    setBool("sessionStatsGame", ExtractJsonBool(json, "sessionStatsGame", true));
    setBool("sessionStatsFinalKills", ExtractJsonBool(json, "sessionStatsFinalKills", true));
    setBool("sessionStatsBeds", ExtractJsonBool(json, "sessionStatsBeds", true));
    setBool("sessionStatsKills", ExtractJsonBool(json, "sessionStatsKills", true));
    setBool("sessionStatsWins", ExtractJsonBool(json, "sessionStatsWins", true));
    setBool("sessionStatsWinstreak", ExtractJsonBool(json, "sessionStatsWinstreak", true));
    setBool("sessionStatsSessionGames", ExtractJsonBool(json, "sessionStatsSessionGames", true));
    setBool("sessionStatsGameTime", ExtractJsonBool(json, "sessionStatsGameTime", true));
    setBool("sessionStatsAvgTime", ExtractJsonBool(json, "sessionStatsAvgTime", true));
    setBool("sessionStatsSessionTime", ExtractJsonBool(json, "sessionStatsSessionTime", true));
    setBool("sessionStatsQueueEta", ExtractJsonBool(json, "sessionStatsQueueEta", true));
    setBool("sessionStatsSlumberTickets", ExtractJsonBool(json, "sessionStatsSlumberTickets", true));
    setBool("sessionStatsTextShadow", ExtractJsonBool(json, "sessionStatsTextShadow", false));
    {
        int bgOpacity = ExtractJsonInt(json, "sessionStatsBgOpacity", 100);
        if (bgOpacity < 0) bgOpacity = 0;
        if (bgOpacity > 255) bgOpacity = 255;
        setInt("sessionStatsBgOpacity", bgOpacity);
    }
    {
        int scale = ExtractJsonInt(json, "sessionStatsScale", 100);
        if (scale < 50) scale = 50;
        if (scale > 150) scale = 150;
        setInt("sessionStatsScale", scale);
    }
    {
        int posX = ExtractJsonInt(json, "sessionStatsPosX", 1000);
        int posY = ExtractJsonInt(json, "sessionStatsPosY", 1000);
        if (posX < 0) posX = 0;
        if (posX > 1000) posX = 1000;
        if (posY < 0) posY = 0;
        if (posY > 1000) posY = 1000;
        setInt("sessionStatsPosX", posX);
        setInt("sessionStatsPosY", posY);
    }
    {
        int posX = ExtractJsonInt(json, "tradeIndicatorPosX", 520);
        int posY = ExtractJsonInt(json, "tradeIndicatorPosY", 470);
        if (posX < 0) posX = 0;
        if (posX > 1000) posX = 1000;
        if (posY < 0) posY = 0;
        if (posY > 1000) posY = 1000;
        setInt("tradeIndicatorPosX", posX);
        setInt("tradeIndicatorPosY", posY);
    }
    {
        auto loadHk = [&](const char* key, const char* field) {
            setInt(field, ExtractJsonInt(json, key, 0));
        };
        loadHk("hkAutoWho", "hkAutoWho");
        loadHk("hkPartyDetector", "hkPartyDetector");
        loadHk("hkNumberDenicker", "hkNumberDenicker");
        loadHk("hkAutoGL", "hkAutoGL");
        loadHk("hkFastBuy", "hkFastBuy");
        loadHk("hkColoredHitboxes", "hkColoredHitboxes");
        loadHk("hkDragonHitboxes", "hkDragonHitboxes");
        loadHk("hkArrowDistance", "hkArrowDistance");
        loadHk("hkBedDisconnectTimer", "hkBedDisconnectTimer");
        loadHk("hkTradeIndicator", "hkTradeIndicator");
        loadHk("hkFireballHitboxes", "hkFireballHitboxes");
        loadHk("hkGeorge", "hkGeorge");
        loadHk("hkNickQol", "hkNickQol");
    }
    {
        int theme = ExtractJsonInt(json, "selectedTheme", 1);
        if (theme < 0) theme = 0;
        if (theme > 7) theme = 7;
        setInt("selectedTheme", theme);
    }

    setString("api_hypixel", ExtractJsonString(json, "api_hypixel"));
    setString("api_seraph", ExtractJsonString(json, "api_seraph"));
    setString("api_aurora", ExtractJsonString(json, "api_aurora"));
    setBool("vegaProxy", ExtractJsonBool(json, "vegaProxy", false));
    std::string glMessage = ExtractJsonString(json, "glMessage");
    if (glMessage.empty()) glMessage = ExtractJsonString(json, "gl_message");
    setString("gl_message", glMessage);
    {
        std::string minStars = ExtractJsonString(json, "chatOverlayMinStars");
        if (minStars.empty()) minStars = "100";
        setString("chatOverlayMinStars", minStars);
    }
    {
        std::string minFkdr = ExtractJsonString(json, "chatOverlayMinFkdr");
        if (minFkdr.empty()) minFkdr = "5";
        setString("chatOverlayMinFkdr", minFkdr);
    }
    {
        std::string minSwKdr = ExtractJsonString(json, "chatOverlayMinSwKdr");
        if (minSwKdr.empty()) minSwKdr = "1.5";
        setString("chatOverlayMinSwKdr", minSwKdr);
    }

    const std::string legacyCols = ExtractJsonString(json, "overlayColumnsConfig");
    const std::string colsBedwars = ExtractJsonString(json, "overlayColumnsBedwars");
    const std::string colsSkywars = ExtractJsonString(json, "overlayColumnsSkywars");
    const std::string colsDuel = ExtractJsonString(json, "overlayColumnsDuel");
    const std::string colsBwDuels = ExtractJsonString(json, "overlayColumnsBedwarsduels");
    const std::string colsMurder = ExtractJsonString(json, "overlayColumnsMurdermystery");
    const std::string gamemodeEdit = ExtractJsonString(json, "overlayGamemodeEdit");
    const std::string gamemodeActive = ExtractJsonString(json, "overlayGamemodeActive");

    jmethodID midLoadAll = env->GetStaticMethodID(customGuiClass,"loadAllOverlayColumnsFromToggles", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    if (midLoadAll && !env->ExceptionCheck()) {
        auto mk = [&](const std::string& s) -> jstring {
            return env->NewStringUTF(s.c_str());
        };
        jstring jBw = mk(colsBedwars);
        jstring jSw = mk(colsSkywars);
        jstring jDu = mk(colsDuel);
        jstring jBd = mk(colsBwDuels);
        jstring jMm = mk(colsMurder);
        jstring jLeg = mk(legacyCols);
        env->CallStaticVoidMethod(customGuiClass, midLoadAll, jBw, jSw, jDu, jBd, jMm, jLeg);
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (jBw) env->DeleteLocalRef(jBw);
        if (jSw) env->DeleteLocalRef(jSw);
        if (jDu) env->DeleteLocalRef(jDu);
        if (jBd) env->DeleteLocalRef(jBd);
        if (jMm) env->DeleteLocalRef(jMm);
        if (jLeg) env->DeleteLocalRef(jLeg);
    } else {
        env->ExceptionClear();
    }

    if (!gamemodeEdit.empty()) {
        jfieldID fidEdit = env->GetStaticFieldID(customGuiClass, "overlayGamemodeEdit", "Ljava/lang/String;");
        jmethodID midLoadEdit = env->GetStaticMethodID(customGuiClass, "loadOverlayColumnsForEditMode", "()V");
        if (fidEdit && midLoadEdit && !env->ExceptionCheck()) {
            jstring jEdit = env->NewStringUTF(gamemodeEdit.c_str());
            if (jEdit) {
                env->SetStaticObjectField(customGuiClass, fidEdit, jEdit);
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->CallStaticVoidMethod(customGuiClass, midLoadEdit);
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(jEdit);
            }
        } else {
            env->ExceptionClear();
        }
    }

    if (!gamemodeActive.empty() && gamemodeActive != "unknown") {
        taboverlay::SetOverlayActiveGamemode(taboverlay::OverlayGamemodeFromString(gamemodeActive));
        g_overlayGamemodeActiveLast = gamemodeActive;
        jmethodID midActive = env->GetStaticMethodID(customGuiClass, "setOverlayGamemodeActive", "(Ljava/lang/String;)V");
        if (midActive && !env->ExceptionCheck()) {
            jstring jActive = env->NewStringUTF(gamemodeActive.c_str());
            if (jActive) {
                env->CallStaticVoidMethod(customGuiClass, midActive, jActive);
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(jActive);
            }
        } else {
            env->ExceptionClear();
        }
    }

    jmethodID midGetCols = env->GetStaticMethodID(customGuiClass, "getOverlayColumnsConfig", "()Ljava/lang/String;");
    if (midGetCols && !env->ExceptionCheck()) {
        jstring jCfg = (jstring)env->CallStaticObjectMethod(customGuiClass, midGetCols);
        if (!env->ExceptionCheck() && jCfg) {
            const char* cfgChars = env->GetStringUTFChars(jCfg, nullptr);
            const std::string savedCols = cfgChars ? cfgChars : "";
            if (cfgChars) env->ReleaseStringUTFChars(jCfg, cfgChars);
            env->DeleteLocalRef(jCfg);
            if (!savedCols.empty()) {
                g_overlayColumnsConfigLast = savedCols;
                g_tabOverlay.applyColumnOrder(SplitCsvList(savedCols));
            }
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    std::string configThemeColor = ExtractJsonString(json, "configThemeColor");
    if (configThemeColor.empty()) configThemeColor = "1A2227";
    setString("configThemeColor", configThemeColor);

    std::string configTextColor = ExtractJsonString(json, "configTextColor");
    if (configTextColor.empty()) configTextColor = "D6DEE4";
    setString("configTextColor", configTextColor);

    int overlayOpacity = ExtractJsonInt(json, "overlayOpacity", 60);
    if (overlayOpacity < 0) overlayOpacity = 0;
    if (overlayOpacity > 95) overlayOpacity = 95;
    jfieldID fidOverlayOpacity = env->GetStaticFieldID(customGuiClass, "overlayOpacity", "I");
    if (fidOverlayOpacity && !env->ExceptionCheck()) {
        env->SetStaticIntField(customGuiClass, fidOverlayOpacity, (jint)overlayOpacity);
        if (env->ExceptionCheck()) env->ExceptionClear();
    } else {
        env->ExceptionClear();
    }

    std::string overlayThemeColor = ExtractJsonString(json, "overlayThemeColor");
    if (overlayThemeColor.empty()) overlayThemeColor = "0F1316";
    setString("overlayThemeColor", overlayThemeColor);

    g_tabOverlay.setOverlayOpacityPercent(overlayOpacity);
    // Keep overlay panel background forced to black for readability.
    g_tabOverlay.setOverlayThemeColorRgb(0x000000);
}

static std::vector<std::string> SplitCsvList(const std::string& csv) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < csv.size(); ++i) {
        char c = csv[i];
        if (c == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static std::string SerializeChatComponentToJson(JNIEnv* env, jobject chatComponent) {
    if (!env || !chatComponent) return "";

    jclass serClass = McFindClass(env, "net/minecraft/util/IChatComponent$Serializer");
    if (!serClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }

    jmethodID midToJson = env->GetStaticMethodID(serClass, "componentToJson", "(Lnet/minecraft/util/IChatComponent;)Ljava/lang/String;");
    if (!midToJson || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(serClass);
        return "";
    }

    jstring jJson = (jstring)env->CallStaticObjectMethod(serClass, midToJson, chatComponent);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(serClass);
        return "";
    }

    std::string out;
    if (jJson) {
        const char* c = env->GetStringUTFChars(jJson, nullptr);
        out = c ? c : "";
        if (c) env->ReleaseStringUTFChars(jJson, c);
        env->DeleteLocalRef(jJson);
    }

    env->DeleteLocalRef(serClass);
    return out;
}

static bool ExtractPrequeueSpeakerNameFromComponentJson(const std::string& componentJson, std::string& outName) {
    outName.clear();
    if (componentJson.empty()) return false;

    std::string lower = componentJson;
    for (char& c : lower) c = (char)std::tolower((unsigned char)c);

    const std::string needle = "socialoptions";
    size_t pos = lower.find(needle);
    if (pos == std::string::npos) return false;

    size_t p = pos + needle.size();
    while (p < componentJson.size() && componentJson[p] == ' ') ++p;
    if (p >= componentJson.size()) return false;

    std::string candidate;
    while (p < componentJson.size()) {
        const char c = componentJson[p];
        const bool isAlphaNum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (isAlphaNum || c == '_') {
            candidate.push_back(c);
            ++p;
            continue;
        }
        break;
    }

    if (candidate.size() < 3 || candidate.size() > 16) return false;
    outName = candidate;
    return true;

}

static bool IsHexLowerOrUpper(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool IsCanonicalUuid(const std::string& s) {
    if (s.size() != 36) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return false;
        } else if (!IsHexLowerOrUpper(s[i])) {
            return false;
        }
    }
    return true;
}

static bool ExtractViewProfileUuidFromComponentJson(const std::string& componentJson, std::string& outUuid) {
    outUuid.clear();
    if (componentJson.empty()) return false;

    const std::string needle = "viewprofile";
    for (size_t i = 0; i + needle.size() < componentJson.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if ((char)std::tolower((unsigned char)componentJson[i + j]) != needle[j]) {
                match = false;
                break;
            }
        }
        if (!match) continue;

        size_t p = i + needle.size();
        while (p < componentJson.size() && componentJson[p] == ' ') ++p;

        // Accept both '/viewprofile <uuid>' and '\/viewprofile <uuid>' JSON forms.
        while (p < componentJson.size() && (componentJson[p] == '\\' || componentJson[p] == '/' || componentJson[p] == ':' || componentJson[p] == '"')) ++p;
        while (p < componentJson.size() && componentJson[p] == ' ') ++p;

        for (; p + 36 <= componentJson.size(); ++p) {
            std::string candidate = componentJson.substr(p, 36);
            if (IsCanonicalUuid(candidate)) {
                outUuid = candidate;
                return true;
            }
        }
    }
    return false;
}

static std::string ToLowerAsciiCopy(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

static int McCodeToRgb(char code) {
    switch ((char)std::tolower((unsigned char)code)) {
        case '0': return 0x000000;
        case '1': return 0x0000AA;
        case '2': return 0x00AA00;
        case '3': return 0x00AAAA;
        case '4': return 0xAA0000;
        case '5': return 0xAA00AA;
        case '6': return 0xFFAA00;
        case '7': return 0xAAAAAA;
        case '8': return 0x555555;
        case '9': return 0x5555FF;
        case 'a': return 0x55FF55;
        case 'b': return 0x55FFFF;
        case 'c': return 0xFF5555;
        case 'd': return 0xFF55FF;
        case 'e': return 0xFFFF55;
        case 'f': return 0xFFFFFF;
        default: return 0xFFFFFF;
    }
}

static int ExtractSpeakerColorFromRaw(const std::string& rawMsg) {
    int color = 0xFFFFFF;
    for (size_t i = 0; i < rawMsg.size(); ++i) {
        if (rawMsg[i] == ':') break;
        if (rawMsg[i] == '\xA7' && i + 1 < rawMsg.size()) {
            color = McCodeToRgb(rawMsg[i + 1]);
            ++i;
            continue;
        }
        if ((unsigned char)rawMsg[i] == 0xC2 && i + 2 < rawMsg.size() && (unsigned char)rawMsg[i + 1] == 0xA7) {
            color = McCodeToRgb(rawMsg[i + 2]);
            i += 2;
            continue;
        }
    }
    return color;
}

static int ExtractSpeakerColorFromComponentJson(const std::string& componentJson) {
    if (componentJson.empty()) return -1;

    const std::string key = "\"text\":\"";
    size_t pos = componentJson.find(key);
    if (pos == std::string::npos) return -1;
    pos += key.size();

    std::string text;
    bool esc = false;
    for (; pos < componentJson.size(); ++pos) {
        const char c = componentJson[pos];
        if (esc) {
            text.push_back(c);
            esc = false;
            continue;
        }
        if (c == '\\') {
            esc = true;
            continue;
        }
        if (c == '"') break;
        text.push_back(c);
    }

    if (text.empty()) return -1;
    return ExtractSpeakerColorFromRaw(text);
}

static void TryRenderOverlayOnWindowThread(HDC frameDc);
std::string CleanMinecraftText(const std::string& text);

static void LogSidebarScoreboard(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (!env || !mcObj || !mcClass) return;

    const ULONGLONG now = GetTickCount64();
    if (now - g_lastScoreboardPollTime < 500) return;
    g_lastScoreboardPollTime = now;
    g_scoreboardPollSeq++;

    jfieldID fidThePlayer = env->GetFieldID(mcClass, "thePlayer", "Lnet/minecraft/client/entity/EntityPlayerSP;");
    if (!fidThePlayer || env->ExceptionCheck()) {
        env->ExceptionClear();
        LogInitOnce("[WARN] Scoreboard: thePlayer field missing");
        return;
    }

    jobject thePlayer = env->GetObjectField(mcObj, fidThePlayer);
    if (!thePlayer || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    jclass playerClass = env->GetObjectClass(thePlayer);
    jmethodID midGetWorldScoreboard = playerClass ? env->GetMethodID(playerClass, "getWorldScoreboard", "()Lnet/minecraft/scoreboard/Scoreboard;") : nullptr;
    if (!midGetWorldScoreboard || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (playerClass) env->DeleteLocalRef(playerClass);
        env->DeleteLocalRef(thePlayer);
        return;
    }

    jobject scoreboard = env->CallObjectMethod(thePlayer, midGetWorldScoreboard);
    if (!scoreboard || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(playerClass);
        env->DeleteLocalRef(thePlayer);
        return;
    }

    jclass scoreboardClass = env->GetObjectClass(scoreboard);
    jmethodID midGetObjectiveInSlot = scoreboardClass ? env->GetMethodID(scoreboardClass, "getObjectiveInDisplaySlot", "(I)Lnet/minecraft/scoreboard/ScoreObjective;") : nullptr;
    if (!midGetObjectiveInSlot || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(scoreboardClass);
        env->DeleteLocalRef(scoreboard);
        env->DeleteLocalRef(playerClass);
        env->DeleteLocalRef(thePlayer);
        return;
    }

    jobject objective = env->CallObjectMethod(scoreboard, midGetObjectiveInSlot, (jint)1); // sidebar
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (!objective) {
        g_allowNickV2Display = false;
        g_atlasReplayViewMode.clear();
        ClearAtlasReplayFlagMaps();
        if (g_scoreboardGamemode == "bedwars") ResetPartyTrackerState();
        g_scoreboardGamemode.clear();
        g_tokensState = "Unknown";
        g_redState = "Unknown";
        g_modeState = "Unknown";
        SessionStatsSetBedwarsContext(false);
        g_tabOverlayStateAllowed = false;
        env->DeleteLocalRef(scoreboardClass);
        env->DeleteLocalRef(scoreboard);
        env->DeleteLocalRef(playerClass);
        env->DeleteLocalRef(thePlayer);
        return;
    }

    std::string dump;
    jclass objectiveClass = env->GetObjectClass(objective);
    jmethodID midObjectiveDisplayName = objectiveClass ? env->GetMethodID(objectiveClass, "getDisplayName", "()Ljava/lang/String;") : nullptr;
    std::string scoreboardTitle = "";
    if (midObjectiveDisplayName) {
        jstring jTitle = (jstring)env->CallObjectMethod(objective, midObjectiveDisplayName);
        if (!env->ExceptionCheck() && jTitle) {
            const char* titleChars = env->GetStringUTFChars(jTitle, nullptr);
            std::string title = titleChars ? titleChars : "";
            if (titleChars) env->ReleaseStringUTFChars(jTitle, titleChars);
            env->DeleteLocalRef(jTitle);
            scoreboardTitle = CleanMinecraftText(title);
            const std::string titleLower = ToLowerAsciiCopy(scoreboardTitle);
            const bool wasAtlasReplay = !g_atlasReplayViewMode.empty();
            g_atlasReplayViewMode.clear();
            if (titleLower.find("atlas") != std::string::npos) g_atlasReplayViewMode = "atlas";
            else if (titleLower.find("replay") != std::string::npos) g_atlasReplayViewMode = "replay";
            const bool atlasReplayActive = !g_atlasReplayViewMode.empty();
            if (wasAtlasReplay && !atlasReplayActive) ClearAtlasReplayFlagMaps();
            if (!wasAtlasReplay && atlasReplayActive) {
                taboverlay::NumberDenickerSetInGame(true);
            } else if (wasAtlasReplay && !atlasReplayActive) {
                if (!g_inGameFromWho) taboverlay::NumberDenickerSetInGame(false);
            }
            g_allowNickV2Display = atlasReplayActive;
            {
                const std::string prevScoreboardMode = g_scoreboardGamemode;
                g_scoreboardGamemode = DetectOverlayGamemodeFromScoreboardTitle(scoreboardTitle);
                if (prevScoreboardMode == "bedwars" && g_scoreboardGamemode != "bedwars") {
                    ResetPartyTrackerState();
                }
            }
            ApplyOverlayGamemodeFromScoreboard(env, g_customGuiClassGlobal, scoreboardTitle);
            dump += "[Scoreboard] ";
            dump += scoreboardTitle;
            dump += "\n";
        } else {
            env->ExceptionClear();
        }
    }

    jmethodID midGetSortedScores = env->GetMethodID(scoreboardClass, "getSortedScores", "(Lnet/minecraft/scoreboard/ScoreObjective;)Ljava/util/Collection;");
    jmethodID midGetPlayersTeam = env->GetMethodID(scoreboardClass, "getPlayersTeam", "(Ljava/lang/String;)Lnet/minecraft/scoreboard/ScorePlayerTeam;");
    jclass scorePlayerTeamClass = McFindClass(env, "net/minecraft/scoreboard/ScorePlayerTeam");
    jmethodID midFormatPlayerName = scorePlayerTeamClass
        ? env->GetStaticMethodID(scorePlayerTeamClass, "formatPlayerName", "(Lnet/minecraft/scoreboard/Team;Ljava/lang/String;)Ljava/lang/String;")
        : nullptr;
    if (env->ExceptionCheck()) env->ExceptionClear();

    if (midGetSortedScores && !env->ExceptionCheck()) {
        jobject scoresCollection = env->CallObjectMethod(scoreboard, midGetSortedScores, objective);
        if (!env->ExceptionCheck() && scoresCollection) {
            jclass collectionClass = env->GetObjectClass(scoresCollection);
            jmethodID midIterator = collectionClass ? env->GetMethodID(collectionClass, "iterator", "()Ljava/util/Iterator;") : nullptr;
            if (midIterator) {
                jobject iterator = env->CallObjectMethod(scoresCollection, midIterator);
                if (!env->ExceptionCheck() && iterator) {
                    jclass iteratorClass = env->GetObjectClass(iterator);
                    jmethodID midHasNext = iteratorClass ? env->GetMethodID(iteratorClass, "hasNext", "()Z") : nullptr;
                    jmethodID midNext = iteratorClass ? env->GetMethodID(iteratorClass, "next", "()Ljava/lang/Object;") : nullptr;

                    int lineCount = 0;
                    while (midHasNext && midNext && env->CallBooleanMethod(iterator, midHasNext)) {
                        jobject scoreObj = env->CallObjectMethod(iterator, midNext);
                        if (env->ExceptionCheck()) {
                            env->ExceptionClear();
                            break;
                        }
                        if (!scoreObj) continue;

                        jclass scoreClass = env->GetObjectClass(scoreObj);
                        jmethodID midPlayerName = scoreClass ? env->GetMethodID(scoreClass, "getPlayerName", "()Ljava/lang/String;") : nullptr;
                        jmethodID midScorePoints = scoreClass ? env->GetMethodID(scoreClass, "getScorePoints", "()I") : nullptr;

                        std::string playerName;
                        int points = 0;

                        if (midPlayerName) {
                            jstring jName = (jstring)env->CallObjectMethod(scoreObj, midPlayerName);
                            if (!env->ExceptionCheck() && jName) {
                                const char* n = env->GetStringUTFChars(jName, nullptr);
                                playerName = n ? n : "";
                                if (n) env->ReleaseStringUTFChars(jName, n);
                                env->DeleteLocalRef(jName);
                            } else {
                                env->ExceptionClear();
                            }
                        }
                        if (midScorePoints) {
                            points = env->CallIntMethod(scoreObj, midScorePoints);
                            if (env->ExceptionCheck()) {
                                env->ExceptionClear();
                                points = 0;
                            }
                        }

                        if (!playerName.empty() && playerName[0] != '#') {
                            if (midGetPlayersTeam && midFormatPlayerName) {
                                jstring jRawName = env->NewStringUTF(playerName.c_str());
                                jobject teamObj = jRawName ? env->CallObjectMethod(scoreboard, midGetPlayersTeam, jRawName) : nullptr;
                                if (env->ExceptionCheck()) {
                                    env->ExceptionClear();
                                    teamObj = nullptr;
                                }

                                jstring jFormatted = jRawName
                                    ? (jstring)env->CallStaticObjectMethod(scorePlayerTeamClass, midFormatPlayerName, teamObj, jRawName)
                                    : nullptr;
                                if (env->ExceptionCheck()) {
                                    env->ExceptionClear();
                                    jFormatted = nullptr;
                                }

                                if (jFormatted) {
                                    const char* f = env->GetStringUTFChars(jFormatted, nullptr);
                                    if (f) {
                                        playerName = f;
                                        env->ReleaseStringUTFChars(jFormatted, f);
                                    }
                                    env->DeleteLocalRef(jFormatted);
                                }

                                if (teamObj) env->DeleteLocalRef(teamObj);
                                if (jRawName) env->DeleteLocalRef(jRawName);
                            }

                            dump += "  - ";
                            dump += CleanMinecraftText(playerName);
                            dump += " : ";
                            dump += std::to_string(points);
                            dump += "\n";
                            lineCount++;
                        }

                        env->DeleteLocalRef(scoreClass);
                        env->DeleteLocalRef(scoreObj);

                        if (lineCount >= 15) break;
                    }
                    env->DeleteLocalRef(iteratorClass);
                    env->DeleteLocalRef(iterator);
                } else {
                    env->ExceptionClear();
                }
            }
            env->DeleteLocalRef(collectionClass);
            env->DeleteLocalRef(scoresCollection);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    if (scorePlayerTeamClass) env->DeleteLocalRef(scorePlayerTeamClass);

    // Tab visibility: scoreboard only (Ingame red team / Prequeue). /who is for API allowlist, not tab UI.
    {
        const std::string evalText = dump.empty() ? scoreboardTitle : dump;
        const std::string evalLower = ToLowerAsciiCopy(evalText);

        const bool hasTokens = (evalLower.find("tokens") != std::string::npos);
        const bool hasLobbyToken = (evalLower.find("lobby:") != std::string::npos);
        const bool hasLobbyWord = (evalLower.find(" lobby") != std::string::npos) ||
                                (evalLower.rfind("lobby", 0) == 0);
        const bool hasMode = (evalLower.find("mode") != std::string::npos);
        const bool hasStartingIn = (evalLower.find("starting in") != std::string::npos);
        const bool hasPlayers = (evalLower.find("players:") != std::string::npos);
        const bool hasTeamRed = (evalLower.find("red:") != std::string::npos) ||
                                (evalLower.find(" red ") != std::string::npos);
        const bool hasTimeLeft = (evalLower.find("time left:") != std::string::npos);
        const bool hasPlayersLeft = (evalLower.find("players left:") != std::string::npos);
        const bool looksLikeLobby = (hasTokens || hasLobbyToken || hasLobbyWord);

        const std::string nextTokensState = looksLikeLobby ? "Lobby" : "Unknown";
        // Bedwars: red team | Duels: time left: | SkyWars: players left:
        const bool looksIngame = !looksLikeLobby && (hasTeamRed || hasTimeLeft || hasPlayersLeft);
        const std::string nextRedState = looksIngame ? "Ingame" : "Unknown";
        // Prequeue only before match start. SW/Duels keep "Mode:" on sidebar ingame — must not
        // flip to Prequeue while players left / time left / red team is present.
        const bool looksPrequeue = !looksLikeLobby && !looksIngame &&
            (hasMode || hasStartingIn || hasPlayers);
        const std::string nextModeState = looksPrequeue ? "Prequeue" : "Unknown";
        const bool nextStateAllowed =
            (nextRedState == "Ingame") || (nextModeState == "Prequeue") || !g_atlasReplayViewMode.empty();

        if (g_inGameFromWho && hasTokens) {
            g_inGameFromWho = false;
            taboverlay::NumberDenickerSetInGame(false);
            g_tabOverlay.clearApiAllowlist();
        }

        const bool enteringIngame = (nextRedState == "Ingame" && g_redState != "Ingame");
        if (enteringIngame) {
            g_tabOverlay.clearPrequeueGhosts();
        }

        if (nextModeState != g_modeState) {
            if (nextModeState == "Prequeue") {
                g_tabOverlay.clearPrequeueGhosts();
                taboverlay::ClearBlacklistChatAnnounced();
                taboverlay::ClearChatOverlayAnnounced();
                taboverlay::ClearLocalPlayerTeam();
                taboverlay::SetOverlayActiveGamemode(taboverlay::OverlayGamemode::Unknown);
                g_overlayGamemodeActiveLast.clear();
                ResetChatPollCursor();
            }
        }
        g_tokensState = nextTokensState;
        g_redState = nextRedState;
        g_modeState = nextModeState;

        // Keep Bedwars session context sticky for a bit after leaving Ingame so
        // end-of-game reward chat (+tokens Win) still counts on Lunar.
        static ULONGLONG s_lastBedwarsIngameAt = 0;
        const bool titleIsBedwars =
            (g_scoreboardGamemode == "bedwars" ||
             scoreboardTitle.find("BED WARS") != std::string::npos ||
             ToLowerAsciiCopy(scoreboardTitle).find("bed wars") != std::string::npos);
        if (titleIsBedwars && nextRedState == "Ingame") {
            s_lastBedwarsIngameAt = GetTickCount64();
        }
        const bool recentBedwarsIngame =
            s_lastBedwarsIngameAt != 0 &&
            (GetTickCount64() - s_lastBedwarsIngameAt) <= 20000;
        SessionStatsSetBedwarsContext(
            titleIsBedwars &&
            (nextRedState == "Ingame" || nextModeState == "Prequeue" ||
             nextTokensState == "Lobby" || recentBedwarsIngame));
        g_tabOverlayStateAllowed = nextStateAllowed;
        static std::string s_lastLoggedState;
        const std::string stateKey = g_tokensState + "|" + g_redState + "|" + g_modeState + "|" +
            (g_tabOverlayStateAllowed ? "1" : "0");
        if (stateKey != s_lastLoggedState) {
            s_lastLoggedState = stateKey;
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                "[State] tokens=%s red=%s mode=%s tabAllowed=%d",
                g_tokensState.c_str(), g_redState.c_str(), g_modeState.c_str(),
                g_tabOverlayStateAllowed ? 1 : 0);
            LogInitOnce(buf);
        }

    }

    env->DeleteLocalRef(objectiveClass);
    env->DeleteLocalRef(objective);
    env->DeleteLocalRef(scoreboardClass);
    env->DeleteLocalRef(scoreboard);
    env->DeleteLocalRef(playerClass);
    env->DeleteLocalRef(thePlayer);
}

extern "C" JNIEXPORT void JNICALL Java_FrenchifyTabOverlayNative_nativeRenderCustomTab(JNIEnv* env, jclass) {
    if (!env) return;
    static ULONGLONG s_lastNativeRenderLogAt = 0;
    static int s_nativeRenderCalls = 0;

    jclass mcClass = McFindClass(env, "net/minecraft/client/Minecraft");
    if (!mcClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    jmethodID midGetMc = env->GetStaticMethodID(mcClass, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
    if (!midGetMc || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(mcClass);
        return;
    }

    jobject mcObj = env->CallStaticObjectMethod(mcClass, midGetMc);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    if (mcObj) {
        s_nativeRenderCalls++;
        const ULONGLONG now = GetTickCount64();
        if (now - s_lastNativeRenderLogAt >= 2000) {
            s_lastNativeRenderLogAt = now;
            s_nativeRenderCalls = 0;
        }
        PumpMinecraftClientLogic(env, mcObj, mcClass);
        if (g_tabOverlayGuiEnabled && g_tabOverlayStateAllowed) {
            g_tabOverlay.renderCustomTab(env, mcObj);
        }
        env->DeleteLocalRef(mcObj);
    }
    env->DeleteLocalRef(mcClass);
}

static bool InstallNativeTabOverlay(JNIEnv* env, jobject classLoader) {
    if (!env) return false;
    if (g_useNativeTabPipeline) return true;

    auto fail = [&](const char* step) -> bool {
        (void)step;
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        return false;
    };

    jclass mcClass = McFindClass(env, "net/minecraft/client/Minecraft");
    if (!mcClass || env->ExceptionCheck()) {
        return fail("FindClass(ave)");
    }

    jmethodID midGetMc = env->GetStaticMethodID(mcClass, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
    if (!midGetMc || env->ExceptionCheck()) {
        env->DeleteLocalRef(mcClass);
        return fail("GetStaticMethodID(ave.A)");
    }

    jobject mcObj = env->CallStaticObjectMethod(mcClass, midGetMc);
    if (env->ExceptionCheck()) return fail("CallStaticObjectMethod(ave.A)");
    if (!mcObj) {
        env->DeleteLocalRef(mcClass);
        return fail("Minecraft instance null");
    }

    if (!classLoader) {
        jclass objClass = env->FindClass("java/lang/Object");
        jclass classClass = env->FindClass("java/lang/Class");
        if (objClass && classClass && !env->ExceptionCheck()) {
            jmethodID midGetClass = env->GetMethodID(objClass, "getClass", "()Ljava/lang/Class;");
            jmethodID midGetClassLoader = env->GetMethodID(classClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
            if (midGetClass && midGetClassLoader) {
                jobject mcClsObj = env->CallObjectMethod(mcObj, midGetClass);
                if (!env->ExceptionCheck() && mcClsObj) {
                    classLoader = env->CallObjectMethod(mcClsObj, midGetClassLoader);
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        classLoader = nullptr;
                    }
                    env->DeleteLocalRef(mcClsObj);
                } else {
                    env->ExceptionClear();
                }
            }
        } else {
            env->ExceptionClear();
        }
        if (objClass) env->DeleteLocalRef(objClass);
        if (classClass) env->DeleteLocalRef(classClass);
    }

    jfieldID fidIngameGui = env->GetFieldID(mcClass, "ingameGUI", "Lnet/minecraft/client/gui/GuiIngame;");
    if (!fidIngameGui || env->ExceptionCheck()) {
        env->DeleteLocalRef(mcObj);
        env->DeleteLocalRef(mcClass);
        return fail("GetFieldID(ave.q)");
    }

    jobject ingameGui = env->GetObjectField(mcObj, fidIngameGui);
    if (!ingameGui || env->ExceptionCheck()) {
        env->DeleteLocalRef(mcObj);
        env->DeleteLocalRef(mcClass);
        return fail("GetObjectField(ingameGui)");
    }

    jclass tabClass = env->DefineClass("FrenchifyTabOverlayNative", classLoader,
        (const jbyte*)FrenchifyTabOverlayNativeLunar_class, FrenchifyTabOverlayNativeLunar_class_len);
    if (env->ExceptionCheck() || !tabClass) {
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        env->ExceptionClear();
        if (classLoader) {
            jclass loaderClass = env->GetObjectClass(classLoader);
            if (loaderClass) {
                jmethodID midLoadClass = env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
                if (midLoadClass) {
                    jstring className = env->NewStringUTF("FrenchifyTabOverlayNative");
                    tabClass = (jclass)env->CallObjectMethod(classLoader, midLoadClass, className);
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        tabClass = nullptr;
                    }
                    env->DeleteLocalRef(className);
                }
                env->DeleteLocalRef(loaderClass);
            }
        }
    }

    if (!tabClass) {
        env->DeleteLocalRef(ingameGui);
        env->DeleteLocalRef(mcObj);
        env->DeleteLocalRef(mcClass);
        return fail("DefineClass/LoadClass(FrenchifyTabOverlayNative)");
    }

    JNINativeMethod methods[] = {
        {(char*)"nativeRenderCustomTab", (char*)"()V", (void*)&Java_FrenchifyTabOverlayNative_nativeRenderCustomTab}
    };
    if (env->RegisterNatives(tabClass, methods, 1) != JNI_OK || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        env->DeleteLocalRef(tabClass);
        env->DeleteLocalRef(ingameGui);
        env->DeleteLocalRef(mcObj);
        env->DeleteLocalRef(mcClass);
        return false;
    }

    jmethodID ctor = env->GetMethodID(tabClass, "<init>", "(Lnet/minecraft/client/Minecraft;Lnet/minecraft/client/gui/GuiIngame;)V");
    if (!ctor || env->ExceptionCheck()) {
        env->DeleteLocalRef(tabClass);
        env->DeleteLocalRef(ingameGui);
        env->DeleteLocalRef(mcObj);
        env->DeleteLocalRef(mcClass);
        return fail("GetMethodID(<init>)");
    }

    jobject tabObj = env->NewObject(tabClass, ctor, mcObj, ingameGui);
    if (!tabObj || env->ExceptionCheck()) {
        env->DeleteLocalRef(tabClass);
        env->DeleteLocalRef(ingameGui);
        env->DeleteLocalRef(mcObj);
        env->DeleteLocalRef(mcClass);
        return fail("NewObject(FrenchifyTabOverlayNative)");
    }

    jclass ingameGuiClass = env->GetObjectClass(ingameGui);
    jfieldID fidTabOverlay = ingameGuiClass ? env->GetFieldID(ingameGuiClass, "overlayPlayerList", "Lnet/minecraft/client/gui/GuiPlayerTabOverlay;") : nullptr;
    if (!fidTabOverlay || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        if (ingameGuiClass) env->DeleteLocalRef(ingameGuiClass);
        env->DeleteLocalRef(tabObj);
        env->DeleteLocalRef(tabClass);
        env->DeleteLocalRef(ingameGui);
        env->DeleteLocalRef(mcObj);
        env->DeleteLocalRef(mcClass);
        return false;
    }

    env->SetObjectField(ingameGui, fidTabOverlay, tabObj);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        env->DeleteLocalRef(ingameGuiClass);
        env->DeleteLocalRef(tabObj);
        env->DeleteLocalRef(tabClass);
        env->DeleteLocalRef(ingameGui);
        env->DeleteLocalRef(mcObj);
        env->DeleteLocalRef(mcClass);
        return fail("SetObjectField(avo.v)");
    }

    if (g_nativeTabClassGlobal) {
        env->DeleteGlobalRef(g_nativeTabClassGlobal);
        g_nativeTabClassGlobal = nullptr;
    }
    g_nativeTabClassGlobal = (jclass)env->NewGlobalRef(tabClass);
    if (!g_nativeTabClassGlobal || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        if (ingameGuiClass) env->DeleteLocalRef(ingameGuiClass);
        env->DeleteLocalRef(tabObj);
        env->DeleteLocalRef(tabClass);
        env->DeleteLocalRef(ingameGui);
        env->DeleteLocalRef(mcObj);
        env->DeleteLocalRef(mcClass);
        return fail("NewGlobalRef(tabClass)");
    }
    g_useNativeTabPipeline = true;

    if (ingameGuiClass) env->DeleteLocalRef(ingameGuiClass);
    env->DeleteLocalRef(tabObj);
    env->DeleteLocalRef(tabClass);
    env->DeleteLocalRef(ingameGui);
    env->DeleteLocalRef(mcObj);
    env->DeleteLocalRef(mcClass);
    return true;
}

static jfieldID ResolveIngameChatGuiField(JNIEnv* env, jobject ingameGui, jclass ingameGuiClass) {
    if (!env || !ingameGui || !ingameGuiClass) return nullptr;

    jmethodID midGetChatGui = env->GetMethodID(ingameGuiClass, "getChatGUI", "()Lnet/minecraft/client/gui/GuiNewChat;");
    if (!midGetChatGui || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }

    jobject currentChatGui = env->CallObjectMethod(ingameGui, midGetChatGui);
    if (!currentChatGui || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }

    static const char* kCandidates[] = {
        "l", "r", "k", "j", "m", "n", "h", "i", "g", "f", "e", "d", "c", "b", "a"
    };

    jfieldID resolved = nullptr;
    for (const char* name : kCandidates) {
        jfieldID fid = env->GetFieldID(ingameGuiClass, name, "Lnet/minecraft/client/gui/GuiNewChat;");
        if (!fid || env->ExceptionCheck()) {
            env->ExceptionClear();
            continue;
        }
        jobject fieldVal = env->GetObjectField(ingameGui, fid);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            continue;
        }
        if (fieldVal && env->IsSameObject(fieldVal, currentChatGui)) {
            resolved = fid;
            env->DeleteLocalRef(fieldVal);
            break;
        }
        if (fieldVal) env->DeleteLocalRef(fieldVal);
    }

    env->DeleteLocalRef(currentChatGui);
    return resolved;
}

static bool InstallNativeChatHook(JNIEnv* env, jobject classLoader) {
    if (!env) return false;
    if (g_useNativeChatHook) return true;

    auto fail = [&](const char* step) -> bool {
        (void)step;
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        return false;
    };

    jclass mcClass = g_mcClassGlobal ? g_mcClassGlobal : McFindClass(env, "net/minecraft/client/Minecraft");
    if (!mcClass || env->ExceptionCheck()) {
        return fail("FindClass(ave)");
    }
    const bool mcClassLocal = (mcClass != g_mcClassGlobal);

    jmethodID midGetMc = g_midGetMinecraftStatic;
    if (!midGetMc) {
        midGetMc = env->GetStaticMethodID(mcClass, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
        if (!midGetMc || env->ExceptionCheck()) {
            if (mcClassLocal) env->DeleteLocalRef(mcClass);
            return fail("GetStaticMethodID(ave.A)");
        }
    }

    jobject mcObj = env->CallStaticObjectMethod(mcClass, midGetMc);
    if (env->ExceptionCheck()) {
        if (mcClassLocal) env->DeleteLocalRef(mcClass);
        return fail("CallStaticObjectMethod(ave.A)");
    }
    if (!mcObj) {
        if (mcClassLocal) env->DeleteLocalRef(mcClass);
        return fail("Minecraft instance null");
    }

    if (!classLoader) {
        classLoader = g_injectClassLoaderGlobal;
    }

    jfieldID fidIngameGui = env->GetFieldID(mcClass, "ingameGUI", "Lnet/minecraft/client/gui/GuiIngame;");
    if (!fidIngameGui || env->ExceptionCheck()) {
        env->DeleteLocalRef(mcObj);
        if (mcClassLocal) env->DeleteLocalRef(mcClass);
        return fail("GetFieldID(ave.q)");
    }

    jobject ingameGui = env->GetObjectField(mcObj, fidIngameGui);
    if (!ingameGui || env->ExceptionCheck()) {
        env->DeleteLocalRef(mcObj);
        if (mcClassLocal) env->DeleteLocalRef(mcClass);
        return fail("GetObjectField(ingameGui)");
    }

    jclass chatHookClass = env->DefineClass("FrenchifyGuiNewChat", classLoader,
        (const jbyte*)FrenchifyGuiNewChat_class, FrenchifyGuiNewChat_class_len);
    if (env->ExceptionCheck() || !chatHookClass) {
        env->ExceptionClear();
        if (classLoader) {
            jclass loaderClass = env->GetObjectClass(classLoader);
            if (loaderClass) {
                jmethodID midLoadClass = env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
                if (midLoadClass) {
                    jstring className = env->NewStringUTF("FrenchifyGuiNewChat");
                    chatHookClass = (jclass)env->CallObjectMethod(classLoader, midLoadClass, className);
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        chatHookClass = nullptr;
                    }
                    env->DeleteLocalRef(className);
                }
                env->DeleteLocalRef(loaderClass);
            }
        }
    }

    if (!chatHookClass) {
        env->DeleteLocalRef(ingameGui);
        env->DeleteLocalRef(mcObj);
        if (mcClassLocal) env->DeleteLocalRef(mcClass);
        return fail("DefineClass/LoadClass(FrenchifyGuiNewChat)");
    }

    JNINativeMethod chatMethods[] = {
        {(char*)"nativeOnIncomingChat", (char*)"(Ljava/lang/Object;)V",
         (void*)&Java_FrenchifyGuiNewChat_nativeOnIncomingChat},
        {(char*)"nativeShouldSuppressIncomingChat", (char*)"(Ljava/lang/Object;)Z",
         (void*)&Java_FrenchifyGuiNewChat_nativeShouldSuppressIncomingChat}
    };
    if (env->RegisterNatives(chatHookClass, chatMethods, 2) != JNI_OK || env->ExceptionCheck()) {
        env->DeleteLocalRef(chatHookClass);
        env->DeleteLocalRef(ingameGui);
        env->DeleteLocalRef(mcObj);
        if (mcClassLocal) env->DeleteLocalRef(mcClass);
        return fail("RegisterNatives(FrenchifyGuiNewChat)");
    }

    jclass ingameGuiClass = env->GetObjectClass(ingameGui);
    jmethodID midGetChatGui = ingameGuiClass ? env->GetMethodID(ingameGuiClass, "getChatGUI", "()Lnet/minecraft/client/gui/GuiNewChat;") : nullptr;
    if (!midGetChatGui || env->ExceptionCheck()) {
        if (ingameGuiClass) env->DeleteLocalRef(ingameGuiClass);
        env->DeleteLocalRef(chatHookClass);
        env->DeleteLocalRef(ingameGui);
        env->DeleteLocalRef(mcObj);
        if (mcClassLocal) env->DeleteLocalRef(mcClass);
        return fail("GetMethodID(avo.d)");
    }

    jobject originalChatGui = env->CallObjectMethod(ingameGui, midGetChatGui);
    if (!originalChatGui || env->ExceptionCheck()) {
        if (ingameGuiClass) env->DeleteLocalRef(ingameGuiClass);
        env->DeleteLocalRef(chatHookClass);
        env->DeleteLocalRef(ingameGui);
        env->DeleteLocalRef(mcObj);
        if (mcClassLocal) env->DeleteLocalRef(mcClass);
        return fail("GetObjectField(originalChatGui)");
    }

    originalChatGui = ResolveChatGuiDelegate(env, originalChatGui);

    jmethodID chatCtor = env->GetMethodID(chatHookClass, "<init>", "(Lnet/minecraft/client/Minecraft;Lnet/minecraft/client/gui/GuiNewChat;)V");
    if (!chatCtor || env->ExceptionCheck()) {
        env->DeleteLocalRef(originalChatGui);
        if (ingameGuiClass) env->DeleteLocalRef(ingameGuiClass);
        env->DeleteLocalRef(chatHookClass);
        env->DeleteLocalRef(ingameGui);
        env->DeleteLocalRef(mcObj);
        if (mcClassLocal) env->DeleteLocalRef(mcClass);
        return fail("GetMethodID(FrenchifyGuiNewChat.<init>)");
    }

    jobject chatHookObj = env->NewObject(chatHookClass, chatCtor, mcObj, originalChatGui);
    env->DeleteLocalRef(originalChatGui);
    if (!chatHookObj || env->ExceptionCheck()) {
        if (ingameGuiClass) env->DeleteLocalRef(ingameGuiClass);
        env->DeleteLocalRef(chatHookClass);
        env->DeleteLocalRef(ingameGui);
        env->DeleteLocalRef(mcObj);
        if (mcClassLocal) env->DeleteLocalRef(mcClass);
        return fail("NewObject(FrenchifyGuiNewChat)");
    }

    jfieldID fidChatGui = ResolveIngameChatGuiField(env, ingameGui, ingameGuiClass);
    if (!fidChatGui) {
        if (ingameGuiClass) env->DeleteLocalRef(ingameGuiClass);
        env->DeleteLocalRef(chatHookObj);
        env->DeleteLocalRef(chatHookClass);
        env->DeleteLocalRef(ingameGui);
        env->DeleteLocalRef(mcObj);
        if (mcClassLocal) env->DeleteLocalRef(mcClass);
        return fail("ResolveIngameChatGuiField");
    }

    env->SetObjectField(ingameGui, fidChatGui, chatHookObj);
    if (env->ExceptionCheck()) {
        if (ingameGuiClass) env->DeleteLocalRef(ingameGuiClass);
        env->DeleteLocalRef(chatHookObj);
        env->DeleteLocalRef(chatHookClass);
        env->DeleteLocalRef(ingameGui);
        env->DeleteLocalRef(mcObj);
        if (mcClassLocal) env->DeleteLocalRef(mcClass);
        return fail("SetObjectField(chatGui)");
    }

    g_useNativeChatHook = true;
    ResetChatPollCursor();

    if (ingameGuiClass) env->DeleteLocalRef(ingameGuiClass);
    env->DeleteLocalRef(chatHookObj);
    env->DeleteLocalRef(chatHookClass);
    env->DeleteLocalRef(ingameGui);
    env->DeleteLocalRef(mcObj);
    if (mcClassLocal) env->DeleteLocalRef(mcClass);
    return true;
}

static void TryRenderInjectToastOnPresent();
static void TryRenderSessionStatsOnPresent();
static void TryRenderOverlayOnWindowThread(HDC frameDc);

static BOOL WINAPI hkSwapBuffers(HDC hdc) {
    if (g_injectReady.load(std::memory_order_relaxed) && !g_inSwapHook) {
        g_inSwapHook = true;
        TryRenderInjectToastOnPresent();
        TryRenderSessionStatsOnPresent();
        TryRenderOverlayOnWindowThread(hdc);
        g_inSwapHook = false;
    }
    if (g_origSwapBuffers) return g_origSwapBuffers(hdc);
    return FALSE;
}

static void APIENTRY hkGlOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                               GLdouble zNear, GLdouble zFar) {
    if (g_origGlOrtho) {
        g_origGlOrtho(left, right, bottom, top, zNear, zFar);
    }
}

static PROC WINAPI hkWglGetProcAddress(LPCSTR name) {
    PROC proc = g_origWglGetProcAddress ? g_origWglGetProcAddress(name) : nullptr;
    if (name) {
        if (strcmp(name, "glOrtho") == 0) {
            return reinterpret_cast<PROC>(reinterpret_cast<void*>(&hkGlOrtho));
        }
    }
    return proc;
}

static void APIENTRY hkGlReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid* pixels) {
    if (g_injectReady.load(std::memory_order_relaxed) && !g_inReadPixelsHook) {
        g_inReadPixelsHook = true;
        // Render right before framebuffer capture to make F2 screenshots include the overlay.
        TryRenderInjectToastOnPresent();
        TryRenderSessionStatsOnPresent();
        TryRenderOverlayOnWindowThread(nullptr);
        g_inReadPixelsHook = false;
    }
    if (g_origGlReadPixels) {
        g_origGlReadPixels(x, y, width, height, format, type, pixels);
    }
}

static bool PatchModuleIATSwapBuffers(HMODULE moduleHandle) {
    if (!moduleHandle) return false;

    auto* base = reinterpret_cast<BYTE*>(moduleHandle);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE) return false;

    const IMAGE_DATA_DIRECTORY& importDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!importDir.VirtualAddress || !importDir.Size) return false;

    auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importDir.VirtualAddress);
    bool patchedAny = false;

    for (; imp->Name; ++imp) {
        const char* importedDll = reinterpret_cast<const char*>(base + imp->Name);
        if (!importedDll) continue;

        const bool isGdi = (_stricmp(importedDll, "GDI32.dll") == 0);
        const bool isGl  = (_stricmp(importedDll, "OPENGL32.dll") == 0);
        if (!isGdi && !isGl) continue;

        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        auto* thunkOrig = imp->OriginalFirstThunk
            ? reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk)
            : thunk;

        for (; thunkOrig && thunkOrig->u1.AddressOfData; ++thunkOrig, ++thunk) {
            if (IMAGE_SNAP_BY_ORDINAL(thunkOrig->u1.Ordinal)) continue;

            auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + thunkOrig->u1.AddressOfData);
            if (!ibn) continue;
            const char* fnName = reinterpret_cast<const char*>(ibn->Name);
            if (!fnName) continue;

            if (isGdi && strcmp(fnName, "SwapBuffers") == 0) {
                const auto already = reinterpret_cast<uintptr_t>(&hkSwapBuffers);
                if (static_cast<uintptr_t>(thunk->u1.Function) == already) continue;
                DWORD oldProtect = 0;
                if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                    auto* candidate = reinterpret_cast<tSwapBuffers>(static_cast<uintptr_t>(thunk->u1.Function));
                    if (!g_origSwapBuffers && candidate &&
                        reinterpret_cast<uintptr_t>(candidate) != already) {
                        g_origSwapBuffers = candidate;
                    }
                    thunk->u1.Function = static_cast<ULONG_PTR>(already);
                    DWORD dummy = 0;
                    VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProtect, &dummy);
                    g_swapIatSlots.push_back(reinterpret_cast<void**>(&thunk->u1.Function));
                    patchedAny = true;
                }
            }

            if (isGl && strcmp(fnName, "glReadPixels") == 0) {
                DWORD oldProtect = 0;
                if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                    if (!g_origGlReadPixels) g_origGlReadPixels = reinterpret_cast<tGlReadPixels>(static_cast<uintptr_t>(thunk->u1.Function));
                    thunk->u1.Function = static_cast<ULONG_PTR>(reinterpret_cast<uintptr_t>(&hkGlReadPixels));
                    DWORD dummy = 0;
                    VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProtect, &dummy);
                    g_readPixelsIatSlots.push_back(reinterpret_cast<void**>(&thunk->u1.Function));
                    patchedAny = true;
                }
            }

            if (isGl && strcmp(fnName, "glOrtho") == 0) {
                DWORD oldProtect = 0;
                if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                    if (!g_origGlOrtho) g_origGlOrtho = reinterpret_cast<tGlOrtho>(static_cast<uintptr_t>(thunk->u1.Function));
                    thunk->u1.Function = static_cast<ULONG_PTR>(reinterpret_cast<uintptr_t>(&hkGlOrtho));
                    DWORD dummy = 0;
                    VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProtect, &dummy);
                    g_glOrthoIatSlots.push_back(reinterpret_cast<void**>(&thunk->u1.Function));
                    patchedAny = true;
                }
            }

            if (isGl && strcmp(fnName, "wglGetProcAddress") == 0) {
                DWORD oldProtect = 0;
                if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                    if (!g_origWglGetProcAddress) {
                        g_origWglGetProcAddress = reinterpret_cast<tWglGetProcAddress>(
                            static_cast<uintptr_t>(thunk->u1.Function));
                    }
                    thunk->u1.Function = static_cast<ULONG_PTR>(reinterpret_cast<uintptr_t>(&hkWglGetProcAddress));
                    DWORD dummy = 0;
                    VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProtect, &dummy);
                    g_wglGpaIatSlots.push_back(reinterpret_cast<void**>(&thunk->u1.Function));
                    patchedAny = true;
                }
            }
        }
    }

    return patchedAny;
}

static bool InstallSwapBuffersHook() {
    if (g_swapHookInstalled) return true;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return false;

    MODULEENTRY32 me = {};
    me.dwSize = sizeof(me);
    bool ok = Module32First(snap, &me) != FALSE;
    while (ok) {
        PatchModuleIATSwapBuffers(me.hModule);
        ok = Module32Next(snap, &me) != FALSE;
    }
    CloseHandle(snap);

    if (!g_origSwapBuffers) {
        HMODULE hGdi = GetModuleHandleA("gdi32.dll");
        if (hGdi) g_origSwapBuffers = reinterpret_cast<tSwapBuffers>(GetProcAddress(hGdi, "SwapBuffers"));
    }
    if (!g_origGlReadPixels) {
        HMODULE hGl = GetModuleHandleA("opengl32.dll");
        if (hGl) g_origGlReadPixels = reinterpret_cast<tGlReadPixels>(GetProcAddress(hGl, "glReadPixels"));
    }
    if (!g_origGlOrtho) {
        HMODULE hGl = GetModuleHandleA("opengl32.dll");
        if (hGl) g_origGlOrtho = reinterpret_cast<tGlOrtho>(GetProcAddress(hGl, "glOrtho"));
    }
    if (!g_origWglGetProcAddress) {
        HMODULE hGl = GetModuleHandleA("opengl32.dll");
        if (hGl) {
            g_origWglGetProcAddress = reinterpret_cast<tWglGetProcAddress>(
                GetProcAddress(hGl, "wglGetProcAddress"));
        }
    }

    g_swapHookInstalled = !g_swapIatSlots.empty() && g_origSwapBuffers != nullptr;
    g_readPixelsHookInstalled = !g_readPixelsIatSlots.empty() && g_origGlReadPixels != nullptr;
    g_glOrthoHookInstalled = !g_glOrthoIatSlots.empty() && g_origGlOrtho != nullptr;
    g_wglGpaHookInstalled = !g_wglGpaIatSlots.empty() && g_origWglGetProcAddress != nullptr;
    return g_swapHookInstalled;
}

static void RemoveSwapBuffersHook() {
    if (!g_swapHookInstalled || !g_origSwapBuffers) return;

    for (void** slot : g_swapIatSlots) {
        if (!slot) continue;
        DWORD oldProtect = 0;
        if (VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            *slot = reinterpret_cast<void*>(g_origSwapBuffers);
            DWORD dummy = 0;
            VirtualProtect(slot, sizeof(void*), oldProtect, &dummy);
        }
    }

    g_swapIatSlots.clear();
    g_swapHookInstalled = false;

    if (g_glOrthoHookInstalled && g_origGlOrtho) {
        for (void** slot : g_glOrthoIatSlots) {
            if (!slot) continue;
            DWORD oldProtect = 0;
            if (VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                *slot = reinterpret_cast<void*>(g_origGlOrtho);
                DWORD dummy = 0;
                VirtualProtect(slot, sizeof(void*), oldProtect, &dummy);
            }
        }
        g_glOrthoIatSlots.clear();
        g_glOrthoHookInstalled = false;
    }

    if (g_wglGpaHookInstalled && g_origWglGetProcAddress) {
        for (void** slot : g_wglGpaIatSlots) {
            if (!slot) continue;
            DWORD oldProtect = 0;
            if (VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                *slot = reinterpret_cast<void*>(g_origWglGetProcAddress);
                DWORD dummy = 0;
                VirtualProtect(slot, sizeof(void*), oldProtect, &dummy);
            }
        }
        g_wglGpaIatSlots.clear();
        g_wglGpaHookInstalled = false;
    }

    if (g_readPixelsHookInstalled && g_origGlReadPixels) {
        for (void** slot : g_readPixelsIatSlots) {
            if (!slot) continue;
            DWORD oldProtect = 0;
            if (VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                *slot = reinterpret_cast<void*>(g_origGlReadPixels);
                DWORD dummy = 0;
                VirtualProtect(slot, sizeof(void*), oldProtect, &dummy);
            }
        }
        g_readPixelsIatSlots.clear();
        g_readPixelsHookInstalled = false;
    }
}

static void TryRenderInjectToastOnPresent() {
    if (!g_injectReady.load(std::memory_order_relaxed)) return;
    if (g_injectNotified.load(std::memory_order_relaxed) && !FrenchifyToastIsShowing()) return;
    if (!g_jvmGlobal) return;
    if (wglGetCurrentContext() == nullptr) return;

    JNIEnv* env = nullptr;
    bool didAttach = false;

    jint getEnvRc = g_jvmGlobal->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (getEnvRc == JNI_EDETACHED) {
        if (g_jvmGlobal->AttachCurrentThread((void**)&env, nullptr) != JNI_OK) {
            return;
        }
        didAttach = true;
    } else if (getEnvRc != JNI_OK || !env) {
        return;
    }

    const bool done = RenderInjectToast(env);
    if (done && !g_injectNotified.load(std::memory_order_relaxed)) {
        g_injectNotified.store(true, std::memory_order_relaxed);
    }

    if (didAttach) g_jvmGlobal->DetachCurrentThread();
}

static void TryRenderSessionStatsOnPresent() {
    if (!g_jvmGlobal) return;
    if (wglGetCurrentContext() == nullptr) return;

    JNIEnv* env = nullptr;
    bool didAttach = false;

    jint getEnvRc = g_jvmGlobal->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (getEnvRc == JNI_EDETACHED) {
        if (g_jvmGlobal->AttachCurrentThread((void**)&env, nullptr) != JNI_OK) {
            return;
        }
        didAttach = true;
    } else if (getEnvRc != JNI_OK || !env) {
        return;
    }

    jclass mcClass = McFindClass(env, "net/minecraft/client/Minecraft");
    if (!mcClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (didAttach) g_jvmGlobal->DetachCurrentThread();
        return;
    }

    jmethodID midGetMc = env->GetStaticMethodID(mcClass, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
    if (!midGetMc || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(mcClass);
        if (didAttach) g_jvmGlobal->DetachCurrentThread();
        return;
    }

    jobject mcObj = env->CallStaticObjectMethod(mcClass, midGetMc);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (mcObj) {
        PumpMinecraftClientLogic(env, mcObj, mcClass);
        if (FrenchifyIsHypixelServerActive()) {
            if (g_customGuiClassGlobal && !IsSessionHudHookInstalled()) {
                SessionStatsTick(env, mcObj, mcClass, g_customGuiClassGlobal);
                SessionStatsRender(env);
            }
            TradeIndicatorRender(env);
        }
        env->DeleteLocalRef(mcObj);
    }

    env->DeleteLocalRef(mcClass);
    if (didAttach) g_jvmGlobal->DetachCurrentThread();
}

static void TryRenderOverlayOnWindowThread(HDC frameDc) {
    ULONGLONG now = GetTickCount64();

    // Native pipeline renders from GuiPlayerTabOverlay directly.
    if (g_useNativeTabPipeline) return;
    if (!g_jvmGlobal) return;
    // Render only while TAB is held from the Minecraft window.
    if (!g_tabHeld) return;
    if (g_gameHwnd && GetForegroundWindow() != g_gameHwnd) return;

    if (g_gameHwnd && frameDc) {
        HWND dcWnd = WindowFromDC(frameDc);
        if (dcWnd == g_gameHwnd) {
            g_swapMatchCount++;
        } else {
            g_swapMismatchCount++;
            if (now - g_lastSwapDiagLog >= 1500) {
                g_lastSwapDiagLog = now;
            }
            return;
        }
    }

    if (wglGetCurrentContext() == nullptr) return;

    JNIEnv* env = nullptr;
    bool didAttach = false;

    jint getEnvRc = g_jvmGlobal->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (getEnvRc == JNI_EDETACHED) {
        if (g_jvmGlobal->AttachCurrentThread((void**)&env, nullptr) != JNI_OK) {
            return;
        }
        didAttach = true;
    } else if (getEnvRc != JNI_OK || !env) {
        return;
    }

    jclass mcClass = McFindClass(env, "net/minecraft/client/Minecraft");
    if (!mcClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (didAttach) g_jvmGlobal->DetachCurrentThread();
        return;
    }

    jmethodID midGetMc = env->GetStaticMethodID(mcClass, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
    if (!midGetMc || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(mcClass);
        if (didAttach) g_jvmGlobal->DetachCurrentThread();
        return;
    }

    jobject mcObj = env->CallStaticObjectMethod(mcClass, midGetMc);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    // Only render in HUD gameplay context (no inventory/chat GUI open).
    static jfieldID fidCurrentScreen = nullptr;
    if (!fidCurrentScreen) {
        fidCurrentScreen = env->GetFieldID(mcClass, "currentScreen", "Lnet/minecraft/client/gui/GuiScreen;");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            fidCurrentScreen = nullptr;
        }
    }

    if (mcObj) {
        PumpMinecraftClientLogic(env, mcObj, mcClass);
    }

    if (mcObj && fidCurrentScreen) {
        jobject currentScreen = env->GetObjectField(mcObj, fidCurrentScreen);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            currentScreen = nullptr;
        }
        if (currentScreen) {
            env->DeleteLocalRef(currentScreen);
            env->DeleteLocalRef(mcObj);
            env->DeleteLocalRef(mcClass);
            if (didAttach) g_jvmGlobal->DetachCurrentThread();
            return;
        }
    }

    if (mcObj) {
        if (g_tabOverlayGuiEnabled && g_tabOverlayStateAllowed) {
            g_tabOverlay.renderCustomTab(env, mcObj);
        }
        env->DeleteLocalRef(mcObj);
    }

    if (now - g_lastSwapDiagLog >= 1500) {
        g_lastSwapDiagLog = now;
    }

    env->DeleteLocalRef(mcClass);
    if (didAttach) g_jvmGlobal->DetachCurrentThread();
}

LRESULT CALLBACK hkWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (!oWndProc) {
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }

    if (uMsg == WM_KILLFOCUS || uMsg == WM_ACTIVATEAPP) {
        if (uMsg == WM_ACTIVATEAPP && wParam == TRUE) {
            // app re-activated, keep current state untouched
        } else {
            g_tabHeld = false;
        }
    }

    if (uMsg == WM_KEYDOWN && wParam == VK_TAB) {
        if (!g_useNativeTabPipeline) {
            if (!g_tabHeld) {
                g_tabHeld = true;
            }
            return 0; // hide original tab overlay (legacy swap path)
        }
    }
    if (uMsg == WM_KEYUP && wParam == VK_TAB) {
        if (!g_useNativeTabPipeline) {
            if (g_tabHeld) {
                g_tabHeld = false;
            }
            return 0; // hide original tab overlay (legacy swap path)
        }
    }

    if (uMsg == WM_LBUTTONDOWN && g_fastBuyEnabled && g_inQuickBuyMenu) {
        g_fastBuyLButtonHeld = true;
        g_lastFastBuyClickQueuedAt = GetTickCount64();
        const LONG queued = InterlockedIncrement(&g_pendingFastBuyClicks);
        if (queued > 64) {
            // Keep a short queue to avoid unbounded backlog when spam clicking.
            InterlockedExchange(&g_pendingFastBuyClicks, 64);
        }
        return 0; // remplace le clic gauche par un middle click JNI
    }
    if (uMsg == WM_LBUTTONUP && g_fastBuyEnabled && g_inQuickBuyMenu) {
        g_fastBuyLButtonHeld = false;
        return 0; // bloque aussi la release du clic gauche original
    }

    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN && g_chatInputLive)
    {
        // Intercept only Frenchify commands; vanilla Enter keeps chat history (arrow keys).
        std::string rawCmd;
        {
            std::lock_guard<std::mutex> lk(g_chatInputMutex);
            rawCmd = currentChatInput;
        }
        if (IsFrenchifyChatCommand(rawCmd)) {
            g_pendingChatSubmit.store(true);
            return 0;
        }
    }

    LRESULT result = CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
    // Rendering is driven by the SwapBuffers hook to avoid timing flicker.
    return result;
}

// Retire les codes couleurs Minecraft (?X = 0xC2 0xA7 + lettre en UTF-8 modifie)
std::string CleanMinecraftText(const std::string& text) {
    std::string result;
    for (size_t i = 0; i < text.length(); ++i) {
        if (i + 2 < text.length() && (unsigned char)text[i] == 0xC2 && (unsigned char)text[i+1] == 0xA7) {
            i += 2;
            continue;
        }
        if (i + 1 < text.length() && (unsigned char)text[i] == 0xA7) {
            i += 1;
            continue;
        }
        result += text[i];
    }
    return result;
}


// Minecraft JNI logic runs on the client/render thread, not on MainThread (deadlock risk).
static std::atomic<bool> g_inClientPump{false};
std::atomic<ULONGLONG> g_clientPumpHeartbeat{0};
std::atomic<ULONGLONG> g_mainLoopHeartbeat{0};

void PumpMinecraftClientLogic(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (!env || !mcObj || !mcClass) return;

    // Keep server whitelist fresh even when the rest of the pump is throttled.
    FrenchifyUpdateHypixelServerWhitelist(env, mcObj, mcClass);

    static ULONGLONG s_lastPumpAt = 0;
    const ULONGLONG nowPump = GetTickCount64();
    if (nowPump - s_lastPumpAt < 20) return;

    bool expected = false;
    if (!g_inClientPump.compare_exchange_strong(expected, true)) return;
    struct ClientPumpGuard {
        std::atomic<bool>& flag;
        ~ClientPumpGuard() { flag.store(false); }
    } guard{g_inClientPump};

    s_lastPumpAt = nowPump;
    g_clientPumpHeartbeat.store(nowPump, std::memory_order_relaxed);

    jclass customGuiClass = g_customGuiClassGlobal;

    // Always allow /config + settings save, even off Hypixel.
    ProcessPendingChatSubmit(env, mcObj, mcClass);

    static bool s_wasOnHypixel = false;
    if (!FrenchifyIsHypixelServerActive()) {
        if (s_wasOnHypixel) {
            s_wasOnHypixel = false;
            g_tabOverlayStateAllowed = false;
            taboverlay::NumberDenickerSetEnabled(false);
            taboverlay::DenickSetEnabled(false);
            if (g_nativeTabClassGlobal) {
                jfieldID fidEnabled = env->GetStaticFieldID(g_nativeTabClassGlobal, "enabled", "Z");
                if (fidEnabled && !env->ExceptionCheck()) {
                    env->SetStaticBooleanField(g_nativeTabClassGlobal, fidEnabled, JNI_FALSE);
                    if (env->ExceptionCheck()) env->ExceptionClear();
                } else {
                    env->ExceptionClear();
                }
            }
        }
        // Force-hide session HUD even if GuiIngame hook is not ticking (menus / other servers).
        if (customGuiClass) {
            SessionStatsTick(env, mcObj, mcClass, customGuiClass);
        }
        if (customGuiClass && g_savedTogglesApplied) {
            static ULONGLONG s_lastOffWrite = 0;
            const ULONGLONG nowOff = GetTickCount64();
            if (nowOff - s_lastOffWrite >= 500) {
                s_lastOffWrite = nowOff;
                WriteTogglesToFile(env, customGuiClass);
            }
        }
        return;
    }
    s_wasOnHypixel = true;

    LogSidebarScoreboard(env, mcObj, mcClass);
    PollMinecraftChat(env, mcObj, mcClass, customGuiClass);
    ExecutePendingAutoWho(env, mcObj, mcClass);
    ExecutePendingAutoGL(env, mcObj, mcClass);
    ExecutePendingWantToBet(env, mcObj, mcClass);
    PollWorldJoinForLocraw(env, mcObj, mcClass);
#ifndef FRENCHIFY_LUNAR
    ExecutePendingAutoLocraw(env, mcObj, mcClass);
#endif

    if (customGuiClass) {
        jfieldID fidTabOverlay = env->GetStaticFieldID(customGuiClass, "tabOverlay", "Z");
        if (fidTabOverlay && !env->ExceptionCheck()) {
            g_tabOverlayGuiEnabled = (bool)env->GetStaticBooleanField(customGuiClass, fidTabOverlay);
        } else {
            env->ExceptionClear();
        }

        bool overlayResourceHeader = true;
        jfieldID fidOverlayResourceHeader = env->GetStaticFieldID(customGuiClass, "overlayResourceHeader", "Z");
        if (fidOverlayResourceHeader && !env->ExceptionCheck()) {
            overlayResourceHeader = (bool)env->GetStaticBooleanField(customGuiClass, fidOverlayResourceHeader);
        } else {
            env->ExceptionClear();
        }
        g_tabOverlay.setResourceHeaderEnabled(overlayResourceHeader);

        bool overlayResourceDiamonds = true;
        jfieldID fidOverlayResourceDiamonds = env->GetStaticFieldID(customGuiClass, "overlayResourceDiamonds", "Z");
        if (fidOverlayResourceDiamonds && !env->ExceptionCheck()) {
            overlayResourceDiamonds = (bool)env->GetStaticBooleanField(customGuiClass, fidOverlayResourceDiamonds);
        } else {
            env->ExceptionClear();
        }
        g_tabOverlay.setResourceDiamondsEnabled(overlayResourceDiamonds);

        bool overlayResourceEmeralds = true;
        jfieldID fidOverlayResourceEmeralds = env->GetStaticFieldID(customGuiClass, "overlayResourceEmeralds", "Z");
        if (fidOverlayResourceEmeralds && !env->ExceptionCheck()) {
            overlayResourceEmeralds = (bool)env->GetStaticBooleanField(customGuiClass, fidOverlayResourceEmeralds);
        } else {
            env->ExceptionClear();
        }
        g_tabOverlay.setResourceEmeraldsEnabled(overlayResourceEmeralds);

        std::string overlayResourceLocation = "tab";
        jfieldID fidOverlayResourceLocation = env->GetStaticFieldID(customGuiClass, "overlayResourceLocation", "Ljava/lang/String;");
        if (fidOverlayResourceLocation && !env->ExceptionCheck()) {
            jstring jLoc = (jstring)env->GetStaticObjectField(customGuiClass, fidOverlayResourceLocation);
            if (!env->ExceptionCheck() && jLoc) {
                const char* c = env->GetStringUTFChars(jLoc, nullptr);
                overlayResourceLocation = c ? c : "tab";
                if (c) env->ReleaseStringUTFChars(jLoc, c);
                env->DeleteLocalRef(jLoc);
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }
        g_tabOverlay.setResourceDisplayLocation(taboverlay::ParseResourceDisplayLocation(overlayResourceLocation));

        std::string hypixelApi;
        std::string seraphApi;
        jfieldID fidHypixelApi = env->GetStaticFieldID(customGuiClass, "api_hypixel", "Ljava/lang/String;");
        if (fidHypixelApi && !env->ExceptionCheck()) {
            jstring jHyp = (jstring)env->GetStaticObjectField(customGuiClass, fidHypixelApi);
            if (!env->ExceptionCheck() && jHyp) {
                const char* c = env->GetStringUTFChars(jHyp, nullptr);
                hypixelApi = c ? c : "";
                if (c) env->ReleaseStringUTFChars(jHyp, c);
                env->DeleteLocalRef(jHyp);
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }

        jfieldID fidSeraphApi = env->GetStaticFieldID(customGuiClass, "api_seraph", "Ljava/lang/String;");
        if (fidSeraphApi && !env->ExceptionCheck()) {
            jstring jSer = (jstring)env->GetStaticObjectField(customGuiClass, fidSeraphApi);
            if (!env->ExceptionCheck() && jSer) {
                const char* c = env->GetStringUTFChars(jSer, nullptr);
                seraphApi = c ? c : "";
                if (c) env->ReleaseStringUTFChars(jSer, c);
                env->DeleteLocalRef(jSer);
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }

        bool vegaProxy = false;
        jfieldID fidVegaProxy = env->GetStaticFieldID(customGuiClass, "vegaProxy", "Z");
        if (fidVegaProxy && !env->ExceptionCheck()) {
            vegaProxy = env->GetStaticBooleanField(customGuiClass, fidVegaProxy) == JNI_TRUE;
        } else {
            env->ExceptionClear();
        }

        static std::string s_cachedHypixelApi;
        static std::string s_cachedSeraphApi;
        static bool s_cachedVegaProxy = false;
        if (hypixelApi != s_cachedHypixelApi || seraphApi != s_cachedSeraphApi ||
            vegaProxy != s_cachedVegaProxy) {
            s_cachedHypixelApi = hypixelApi;
            s_cachedSeraphApi = seraphApi;
            s_cachedVegaProxy = vegaProxy;
            g_tabOverlay.setApiKeys(hypixelApi, seraphApi, "", vegaProxy);
        }

        if (taboverlay::PlayerApi::ConsumeInvalidHypixelKeyNotice()) {
            PrintLocalChat(env, mcObj, mcClass,
                "\xC2\xA7""7[\xC2\xA7""1Fre\xC2\xA7""fnch\xC2\xA7""cify\xC2\xA7""7] \xC2\xA7""cInvalid Hypixel API key\xC2\xA7""7. Check your key in settings.");
        }
        if (taboverlay::PlayerApi::ConsumeInvalidSeraphKeyNotice()) {
            PrintLocalChat(env, mcObj, mcClass,
                "\xC2\xA7""7[\xC2\xA7""1Fre\xC2\xA7""fnch\xC2\xA7""cify\xC2\xA7""7] \xC2\xA7""cInvalid Seraph API key\xC2\xA7""7. Check your key in settings.");
        }

        // Pump Number Denicker chat always (even if Chat Overlay / Tab Overlay are off).
        {
            std::string ndChat;
            bool ndChatJson = false;
            int pumped = 0;
            while (pumped < 8 && taboverlay::NumberDenickerTryConsumeChat(ndChat, ndChatJson)) {
                if (ndChatJson) PrintJsonChat(env, mcObj, mcClass, ndChat);
                else PrintLocalChat(env, mcObj, mcClass, ndChat);
                ndChat.clear();
                ++pumped;
            }
        }

        taboverlay::DenickSetEnabled(customGuiClass && !hypixelApi.empty());

        std::string auroraApi;
        jfieldID fidAuroraApi = env->GetStaticFieldID(customGuiClass, "api_aurora", "Ljava/lang/String;");
        if (fidAuroraApi && !env->ExceptionCheck()) {
            jstring jAu = (jstring)env->GetStaticObjectField(customGuiClass, fidAuroraApi);
            if (!env->ExceptionCheck() && jAu) {
                const char* c = env->GetStringUTFChars(jAu, nullptr);
                auroraApi = c ? c : "";
                if (c) env->ReleaseStringUTFChars(jAu, c);
                env->DeleteLocalRef(jAu);
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }

        static std::string s_cachedAuroraApi;
        static bool s_cachedNumberDenicker = false;
        const bool numberDenickerOn = customGuiClass &&
            ReadGuiBool(env, customGuiClass, "numberDenicker", false);
        if (auroraApi != s_cachedAuroraApi || numberDenickerOn != s_cachedNumberDenicker ||
            hypixelApi != s_cachedHypixelApi) {
            s_cachedAuroraApi = auroraApi;
            s_cachedNumberDenicker = numberDenickerOn;
            taboverlay::NumberDenickerSetKeys(auroraApi, hypixelApi);
            taboverlay::NumberDenickerSetEnabled(numberDenickerOn && !auroraApi.empty());
        }

        // Dispatch async /stats query so HTTP calls never block the main loop.
        // If a previous query is still running, leave pendingStatsPlayer set and
        // retry on the next loop iteration.
        if (!pendingStatsPlayer.empty() && !g_statsWorkerRunning.load()) {
            const std::string target          = pendingStatsPlayer;
            pendingStatsPlayer.clear();
            const std::string capturedHypixel = hypixelApi;
            const std::string capturedSeraph  = seraphApi;
            std::string capturedMode = g_overlayGamemodeActiveLast;
            if (capturedMode.empty()) capturedMode = "bedwars";
            PrintLocalChat(env, mcObj, mcClass,
                "\xC2\xA7""7[\xC2\xA7""1Fre\xC2\xA7""fnch\xC2\xA7""cify\xC2\xA7""7] \xC2\xA7""7Fetching stats for \xC2\xA7""f" + target + "\xC2\xA7""7...");
            g_statsWorkerRunning.store(true);
            std::thread([target, capturedHypixel, capturedSeraph, capturedMode]() {
                taboverlay::PlayerLookupStats st;
                const bool ok = taboverlay::QueryPlayerStatsNow(
                    target, capturedHypixel, capturedSeraph, capturedMode, st) && st.resolved;
                if (!ok) { st = taboverlay::PlayerLookupStats{}; st.playerName = target; }
                {
                    std::lock_guard<std::mutex> lk(g_statsResultMutex);
                    g_asyncStatsResult.st     = st;
                    g_asyncStatsResult.target = target;
                    g_asyncStatsResult.ready  = true;
                }
                g_statsWorkerRunning.store(false);
            }).detach();
        }
        // Print result once the worker thread has finished.
        {
            std::lock_guard<std::mutex> lk(g_statsResultMutex);
            if (g_asyncStatsResult.ready) {
                g_asyncStatsResult.ready = false;
                auto& st = g_asyncStatsResult.st;
                if (st.resolved) {
                    const taboverlay::PlayerLookupChatOutput chatOut =
                        taboverlay::FormatPlayerLookupChatMessage(st);
                    if (!chatOut.message.empty()) {
                        if (chatOut.isJson) PrintJsonChat(env, mcObj, mcClass, chatOut.message);
                        else PrintLocalChat(env, mcObj, mcClass, chatOut.message);
                    }
                } else {
                    PrintLocalChat(env, mcObj, mcClass,
                        "\xC2\xA7""7[\xC2\xA7""1Fre\xC2\xA7""fnch\xC2\xA7""cify\xC2\xA7""7] \xC2\xA7""cUnable to fetch stats for: \xC2\xA7""f" + st.playerName);
                }
            }
        }

        int overlayOpacity = 60;
        jfieldID fidOverlayOpacity = env->GetStaticFieldID(customGuiClass, "overlayOpacity", "I");
        if (fidOverlayOpacity && !env->ExceptionCheck()) {
            overlayOpacity = (int)env->GetStaticIntField(customGuiClass, fidOverlayOpacity);
        } else {
            env->ExceptionClear();
        }
        g_tabOverlay.setOverlayOpacityPercent(overlayOpacity);

        g_tabOverlay.setOverlayThemeColorRgb(0x000000);

        jfieldID fidActiveMode = env->GetStaticFieldID(customGuiClass, "overlayGamemodeActive", "Ljava/lang/String;");
        if (fidActiveMode && !env->ExceptionCheck()) {
            jstring jActive = (jstring)env->GetStaticObjectField(customGuiClass, fidActiveMode);
            if (!env->ExceptionCheck() && jActive) {
                const char* activeChars = env->GetStringUTFChars(jActive, nullptr);
                const std::string activeMode = activeChars ? activeChars : "";
                if (activeChars) env->ReleaseStringUTFChars(jActive, activeChars);
                env->DeleteLocalRef(jActive);
                if (!activeMode.empty() && activeMode != g_overlayGamemodeActiveLast) {
                    g_overlayGamemodeActiveLast = activeMode;
                    taboverlay::SetOverlayActiveGamemode(taboverlay::OverlayGamemodeFromString(activeMode));
                    g_overlayColumnsConfigLast.clear();
                }
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }

        jmethodID midGetCols = env->GetStaticMethodID(customGuiClass, "getOverlayColumnsConfig", "()Ljava/lang/String;");
        if (midGetCols && !env->ExceptionCheck()) {
            jstring jCfg = (jstring)env->CallStaticObjectMethod(customGuiClass, midGetCols);
            if (!env->ExceptionCheck() && jCfg) {
                const char* cfgChars = env->GetStringUTFChars(jCfg, nullptr);
                std::string cfg = cfgChars ? cfgChars : "";
                if (cfgChars) env->ReleaseStringUTFChars(jCfg, cfgChars);
                env->DeleteLocalRef(jCfg);

                if (cfg != g_overlayColumnsConfigLast) {
                    g_overlayColumnsConfigLast = cfg;
                    g_tabOverlay.applyColumnOrder(SplitCsvList(cfg));
                }
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }
    }

    if (g_nativeTabClassGlobal) {
        jfieldID fidEnabled = env->GetStaticFieldID(g_nativeTabClassGlobal, "enabled", "Z");
        if (fidEnabled && !env->ExceptionCheck()) {
            const bool enabledNow = g_tabOverlayGuiEnabled && g_tabOverlayStateAllowed;
            env->SetStaticBooleanField(g_nativeTabClassGlobal, fidEnabled, enabledNow ? JNI_TRUE : JNI_FALSE);
            if (env->ExceptionCheck()) env->ExceptionClear();

            if (!g_nativeTabEnabledLastKnown || g_nativeTabEnabledLast != enabledNow) {
                g_nativeTabEnabledLastKnown = true;
                g_nativeTabEnabledLast = enabledNow;
            }
        } else {
            env->ExceptionClear();
        }
    }

    // ====== ECRITURE DES TOGGLES TOUTES LES 500ms ======
    {
        ULONGLONG nowT = GetTickCount64();
        if (g_savedTogglesApplied && nowT - lastToggleWriteTime >= 500) {
            lastToggleWriteTime = nowT;
            WriteTogglesToFile(env, customGuiClass);
        }
    }
    // ===================================================

    // ====== HOLD RENDER DISTANCE HOTKEY ======
    if (customGuiClass) {
        TradeIndicatorTick(env, mcObj, mcClass, customGuiClass);
        {
            jfieldID fidSuppress = env->GetStaticFieldID(customGuiClass, "suppressHotkeys", "Z");
            if (fidSuppress && !env->ExceptionCheck()) {
                env->SetStaticBooleanField(customGuiClass, fidSuppress, g_chatInputLive ? JNI_TRUE : JNI_FALSE);
            } else {
                env->ExceptionClear();
            }
        }
        PollUtilsHotkeys(env, customGuiClass);
        if (!g_chatInputLive && ReadGuiBool(env, customGuiClass, "nickQolPendingSend", false)) {
            scheduledNickQolTime = GetTickCount64();
        }
        ExecutePendingNickQol(env, mcObj, mcClass, customGuiClass);
        HudEditTick(env, mcObj, mcClass, customGuiClass);
    }
    // =========================================

    // ====== PARTY TRACKER DIFFERE ======
    ULONGLONG now2 = GetTickCount64();

    // Chat-based party detector (Utils) — join/quit spam aggregation on lobby scoreboard.
    if (!IsPartyTrackerScoreboardActive()) {
        ResetPartyTrackerState();
    } else {
        if (partyJoinCount > 0 && (now2 - lastPartyJoinTime) >= PartyQueueDetectorAnnounceDelayMs()) {
            if (partyJoinCount > 1) {
                std::string localMsg = "\xC2\xA7""7[\xC2\xA7""1Fre\xC2\xA7""fnch\xC2\xA7""cify\xC2\xA7""7] \xC2\xA7""7Party of \xC2\xA7""c" + std::to_string(partyJoinCount) + " \xC2\xA7""7joined.";
                PrintLocalChat(env, mcObj, mcClass, localMsg);
            }
            ResetPartyTrackerState();
        }
    }
    if (IsPartyTrackerScoreboardActive() && partyQuitCount > 0 && (now2 - lastPartyQuitTime) >= PartyQueueDetectorAnnounceDelayMs()) {
        if (partyQuitCount > 1) {
            std::string localMsg = "\xC2\xA7""7[\xC2\xA7""1Fre\xC2\xA7""fnch\xC2\xA7""cify\xC2\xA7""7] \xC2\xA7""7Party of \xC2\xA7""c" + std::to_string(partyQuitCount) + " \xC2\xA7""7quit.";
            PrintLocalChat(env, mcObj, mcClass, localMsg);
        }
        partyQuitCount = 0;
        g_partyQuitPlayersSeen.clear();
    }

    // ID-based queue party detector (Experimental) — spawn entity IDs in Prequeue.
    {
        const bool queuePartyOn = customGuiClass
            ? ReadGuiStaticBool(env, customGuiClass, "partyQueueDetector", false)
            : false;
        const bool queuePartyActive = queuePartyOn && g_modeState == "Prequeue";
        PartyQueueDetectorSetActive(queuePartyActive);
        if (queuePartyActive) {
            static ULONGLONG s_lastQueuePacketHookTryAt = 0;
            if (!IsPartyQueuePacketHookInstalled()
                && now2 - s_lastQueuePacketHookTryAt >= 2000) {
                s_lastQueuePacketHookTryAt = now2;
                TryInstallPartyQueuePacketHook(env, mcObj, mcClass, g_injectClassLoaderGlobal);
            }
            PartyQueueDetectorTick(env, mcObj, mcClass);
        }
    }
    // ===================================

    // ====== ANTICHEAT CHECKS (toutes les 50ms) ======
    {
        ULONGLONG nowAC = GetTickCount64();
        if ((g_redState == "Ingame" || g_allowNickV2Display) && nowAC - g_lastAcScanTime >= 50) {
            g_lastAcScanTime = nowAC;
            try {
                RunAnticheatChecks(env, mcObj, mcClass, customGuiClass);
            } catch (const std::exception&) {
            } catch (...) {
            }
        }
    }
    // ================================================

    // ====== HITBOX (JNI on main thread; draw inside RenderGlobal.renderEntities) ======
    {
        static ULONGLONG s_lastHitboxHookTryAt = 0;
        const ULONGLONG nowHb = GetTickCount64();
        const bool hbOn = customGuiClass
            ? (ReadGuiStaticBool(env, customGuiClass, "coloredHitboxes", false) ||
               ReadGuiStaticBool(env, customGuiClass, "dragonHitboxes", false) ||
               ReadGuiStaticBool(env, customGuiClass, "fireballHitboxes", false))
            : false;
        if (hbOn) {
            TryResolveHitboxJni(env, mcObj);
            if (!IsRenderGlobalHookInstalled()
                && nowHb - s_lastHitboxHookTryAt >= 2000) {
                s_lastHitboxHookTryAt = nowHb;
                TryInstallRenderGlobalHook(env, mcObj, g_injectClassLoaderGlobal);
            }
        }
    }
    // ================================================================

    // ====== BOW DISTANCE (arrow release tracker) ======
    {
        const bool arrowDistance = customGuiClass
            ? ReadGuiBool(env, customGuiClass, "arrowDistance", false)
            : false;
        RunBowDistanceShotTracker(env, mcObj, mcClass, arrowDistance);
    }
    // =============================================

    // ====== SESSION STATS HUD ======
    {
        const bool sessionOn = customGuiClass
            ? (ReadGuiStaticBool(env, customGuiClass, "sessionStats", false) ||
               ReadGuiStaticBool(env, customGuiClass, "hudEditMode", false))
            : false;
        if (sessionOn) {
            if (!IsSessionHudHookInstalled()) {
                SessionStatsTick(env, mcObj, mcClass, customGuiClass);
            }
            static ULONGLONG s_lastSessionHudHookTryAt = 0;
            static ULONGLONG s_lastSessionPumpLogAt = 0;
            if (nowPump - s_lastSessionPumpLogAt >= 10000) {
                s_lastSessionPumpLogAt = nowPump;
                char buf[128];
                std::snprintf(buf, sizeof(buf), "pump session ON hook=%d scheduled=%d",
                              IsSessionHudHookInstalled() ? 1 : 0, 1);
                FrenchifyFileLog("session", buf);
            }
            if (!IsSessionHudHookInstalled()
                && nowPump - s_lastSessionHudHookTryAt >= 2000) {
                s_lastSessionHudHookTryAt = nowPump;
                FrenchifyFileLog("session", "pump: trying GuiIngame hook install");
                TryInstallSessionHudHook(env, mcObj, g_injectClassLoaderGlobal);
            }
        }
    }
    // ===============================

    // ====== OVERLAY SESSION (API + chat, sans TAB) ======
    {
        static ULONGLONG s_lastRgHookTry = 0;
        if (!IsRenderGlobalHookInstalled() && nowPump - s_lastRgHookTry >= 2000) {
            s_lastRgHookTry = nowPump;
            TryInstallRenderGlobalHook(env, mcObj, g_injectClassLoaderGlobal);
        }
        if (g_tabOverlayStateAllowed) {
            static ULONGLONG s_lastOverlaySessionTick = 0;
            if (nowPump - s_lastOverlaySessionTick >= 300) {
                s_lastOverlaySessionTick = nowPump;
                g_tabOverlay.tickSession(env, mcObj);
            }
        }
    }
    // ===================================================

    // ====== OUVERTURE DU GUI /config (independant du chat ouvert) ======
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        const ULONGLONG nowJ = GetTickCount64();
        if (nowJ - g_lastJniExceptionClearLog >= 2000) {
            g_lastJniExceptionClearLog = nowJ;
        }
    }
    if (shouldExecuteConfig) {
        if (OpenFrenchifyConfigGui(env, mcObj, mcClass, customGuiClass)) {
            shouldExecuteConfig = false;
        }
    }
    // ====================================================

    // ====== LECTURE DU CHAMP DE SAISIE CHAT ======
    jfieldID fidCurrentScreen = env->GetFieldID(mcClass, "currentScreen", "Lnet/minecraft/client/gui/GuiScreen;");
    if (fidCurrentScreen) {
        jobject currentScreen = env->GetObjectField(mcObj, fidCurrentScreen);
        if (currentScreen) {
            g_chatInputLive = false;
            jclass guiChatClass = McFindClass(env, "net/minecraft/client/gui/GuiChat");
            if (guiChatClass && env->IsInstanceOf(currentScreen, guiChatClass)) {
                g_chatInputLive = true;
                jfieldID fidInputField = env->GetFieldID(guiChatClass, "inputField", "Lnet/minecraft/client/gui/GuiTextField;");
                if (fidInputField) {
                    jobject inputField = env->GetObjectField(currentScreen, fidInputField);
                    if (inputField) {
                        jclass guiTextFieldClass = env->GetObjectClass(inputField);
                        jmethodID midGetText = env->GetMethodID(guiTextFieldClass, "getText", "()Ljava/lang/String;");

                        if (midGetText) {
                            jstring text = (jstring)env->CallObjectMethod(inputField, midGetText);
                            if (text) {
                                const char* str = env->GetStringUTFChars(text, nullptr);
                                {
                                    std::lock_guard<std::mutex> lk(g_chatInputMutex);
                                    currentChatInput = str ? str : "";
                                }
                                if (str) env->ReleaseStringUTFChars(text, str);
                                env->DeleteLocalRef(text);
                                g_lastChatInputUpdate = GetTickCount64();
                            } else {
                                std::lock_guard<std::mutex> lk(g_chatInputMutex);
                                currentChatInput = "";
                                g_lastChatInputUpdate = GetTickCount64();
                            }
                        }
                        env->DeleteLocalRef(guiTextFieldClass);
                        env->DeleteLocalRef(inputField);
                    }
                }
            }
            if (guiChatClass) env->DeleteLocalRef(guiChatClass);

            // ====== FAST BUY: DETECTION DU MENU QUICK BUY ======
            g_inQuickBuyMenu = false;
            if (customGuiClass) {
                jfieldID fidFastBuy = env->GetStaticFieldID(customGuiClass, "fastBuy", "Z");
                if (fidFastBuy) g_fastBuyEnabled = (bool)env->GetStaticBooleanField(customGuiClass, fidFastBuy);
                else { env->ExceptionClear(); g_fastBuyEnabled = false; }
            } else { g_fastBuyEnabled = false; }

            if (g_fastBuyEnabled) {
                jclass guiChestClass = McFindClass(env, "net/minecraft/client/gui/inventory/GuiChest");
                if (!guiChestClass) {
                    env->ExceptionClear();
                }
                if (guiChestClass) {
                    bool isChest = env->IsInstanceOf(currentScreen, guiChestClass);
                    if (isChest != s_fbLastIsChest) {
                        s_fbLastIsChest = isChest;
                    }
                    if (isChest) {
                        jfieldID fidLowerInv = env->GetFieldID(guiChestClass, "lowerChestInventory", "Lnet/minecraft/inventory/IInventory;");
                        if (!fidLowerInv) {
                            env->ExceptionClear();
                        }
                        if (fidLowerInv) {
                            jobject lowerInv = env->GetObjectField(currentScreen, fidLowerInv);
                            if (lowerInv) {
                                jclass invClass = env->GetObjectClass(lowerInv);
                                jmethodID midGetStack = JniResolveInstanceMethod(env, invClass,
                                    "(I)Lnet/minecraft/item/ItemStack;", "getStackInSlot", "a");
                                if (!midGetStack) {
                                    midGetStack = JniResolveInstanceMethod(env, invClass,
                                        "(I)Lnet/minecraft/item/ItemStack;", "getStackInSlot", "a");
                                }
                                if (!midGetStack) {
                                    env->ExceptionClear();
                                }
                                if (midGetStack) {
                                    jobject stack = env->CallObjectMethod(lowerInv, midGetStack, (jint)0);
                                    if (env->ExceptionCheck()) env->ExceptionClear();
                                    if (!stack && s_fbLastSlot0Name != "<null>") {
                                        s_fbLastSlot0Name = "<null>";
                                    }
                                    if (stack) {
                                        jclass stackClass = env->GetObjectClass(stack);
                                        jmethodID midGetName = env->GetMethodID(stackClass, "getDisplayName", "()Ljava/lang/String;");
                                        if (!midGetName) {
                                            env->ExceptionClear();
                                        }
                                        if (midGetName) {
                                            jstring jName = (jstring)env->CallObjectMethod(stack, midGetName);
                                            if (env->ExceptionCheck()) env->ExceptionClear();
                                            if (jName) {
                                                const char* nc = env->GetStringUTFChars(jName, nullptr);
                                                std::string rawName = nc ? nc : "";
                                                std::string cleanName = StripMinecraftFormatting(rawName);
                                                if (nc) env->ReleaseStringUTFChars(jName, nc);
                                                env->DeleteLocalRef(jName);
                                                if (cleanName != s_fbLastSlot0Name) {
                                                    s_fbLastSlot0Name = cleanName;
                                                }
                                                bool found = (cleanName == "Quick Buy");
                                                if (found != s_fbLastMenuFound) {
                                                    s_fbLastMenuFound = found;
                                                }
                                                if (found) g_inQuickBuyMenu = true;
                                            }
                                        }
                                        env->DeleteLocalRef(stackClass);
                                        env->DeleteLocalRef(stack);
                                    }
                                }
                                env->DeleteLocalRef(invClass);
                                env->DeleteLocalRef(lowerInv);
                            }
                        }
                    }
                    env->DeleteLocalRef(guiChestClass);
                }
            } else {
                s_fbLastIsChest = false;
                s_fbLastMenuFound = false;
                s_fbLastSlot0Name = "";
            }
            // ====================================================

            // ====== FAST BUY: EXECUTION windowClick VIA JNI ======
            const LONG pendingFastBuyClicks = InterlockedCompareExchange(&g_pendingFastBuyClicks, 0, 0);
            if (pendingFastBuyClicks > 0 && g_inQuickBuyMenu) {
                bool consumedClick = false;

                jclass guiContainerClass = McFindClass(env, "net/minecraft/client/gui/inventory/GuiContainer");
                jfieldID fidPlayerController = env->GetFieldID(mcClass, "playerController", "Lnet/minecraft/client/multiplayer/PlayerControllerMP;");
                if (!guiContainerClass) { env->ExceptionClear(); }
                if (!fidPlayerController) { env->ExceptionClear(); }

                if (guiContainerClass && fidPlayerController) {
                    jfieldID fidTheSlot  = env->GetFieldID(guiContainerClass, "theSlot", "Lnet/minecraft/inventory/Slot;");
                    jfieldID fidInvSlots = env->GetFieldID(guiContainerClass, "inventorySlots", "Lnet/minecraft/inventory/Container;");
                    if (!fidTheSlot)  { env->ExceptionClear(); }
                    if (!fidInvSlots) { env->ExceptionClear(); }

                    if (fidTheSlot && fidInvSlots) {
                        jobject theSlot  = env->GetObjectField(currentScreen, fidTheSlot);
                        jobject invSlots = env->GetObjectField(currentScreen, fidInvSlots);

                        if (theSlot && invSlots) {
                            jclass slotClass      = env->GetObjectClass(theSlot);
                            jclass containerClass = env->GetObjectClass(invSlots);

                            jfieldID fidSlotNumber = env->GetFieldID(slotClass,      "slotNumber", "I");
                            jfieldID fidWindowId   = env->GetFieldID(containerClass, "windowId", "I");
                            if (!fidSlotNumber) { env->ExceptionClear(); }
                            if (!fidWindowId)   { env->ExceptionClear(); }

                            if (fidSlotNumber && fidWindowId) {
                                jint slotNumber = env->GetIntField(theSlot,  fidSlotNumber);
                                jint windowId   = env->GetIntField(invSlots, fidWindowId);

                                jobject playerController = env->GetObjectField(mcObj, fidPlayerController);
                                jfieldID fidThePlayer    = env->GetFieldID(mcClass, "thePlayer", "Lnet/minecraft/client/entity/EntityPlayerSP;");
                                jobject  thePlayer       = fidThePlayer ? env->GetObjectField(mcObj, fidThePlayer) : nullptr;

                                if (playerController && thePlayer) {
                                    jclass pcClass = env->GetObjectClass(playerController);
                                    // windowClick(windowId, slotId, mouseButton, mode, player)
                                    // Replace the user left click with a middle click action.
                                    jmethodID midWindowClick = env->GetMethodID(pcClass, "windowClick", "(IIIILnet/minecraft/entity/player/EntityPlayer;)Lnet/minecraft/item/ItemStack;");
                                    if (!midWindowClick) { env->ExceptionClear(); }
                                    if (midWindowClick) {
                                        jobject result = env->CallObjectMethod(playerController, midWindowClick,
                                            windowId, slotNumber, (jint)2, (jint)3, thePlayer);
                                        if (env->ExceptionCheck()) {
                                            env->ExceptionClear();
                                        } else {
                                            consumedClick = true;
                                        }
                                        if (result) env->DeleteLocalRef(result);
                                    }
                                    env->DeleteLocalRef(pcClass);
                                }
                                if (playerController) env->DeleteLocalRef(playerController);
                                if (thePlayer)        env->DeleteLocalRef(thePlayer);
                            }
                            env->DeleteLocalRef(slotClass);
                            env->DeleteLocalRef(containerClass);
                        }
                        if (theSlot)  env->DeleteLocalRef(theSlot);
                        if (invSlots) env->DeleteLocalRef(invSlots);
                    }
                }
                if (guiContainerClass) env->DeleteLocalRef(guiContainerClass);
                if (consumedClick) {
                    InterlockedDecrement(&g_pendingFastBuyClicks);
                    g_fastBuyReplayFailStreak = 0;
                } else {
                    // Prevent permanent stuck queue if replay cannot execute on this frame.
                    ++g_fastBuyReplayFailStreak;
                    if (g_fastBuyReplayFailStreak >= 8) {
                        InterlockedDecrement(&g_pendingFastBuyClicks);
                        g_fastBuyReplayFailStreak = 0;
                    }
                }
            } else if (pendingFastBuyClicks > 0) {
                // Keep queued clicks briefly in case Quick Buy detection flickers for a frame.
                const ULONGLONG nowFb = GetTickCount64();
                if (!g_fastBuyLButtonHeld && (nowFb - g_lastFastBuyClickQueuedAt) > 350) {
                    InterlockedExchange(&g_pendingFastBuyClicks, 0);
                    g_fastBuyReplayFailStreak = 0;
                }
            } else {
                g_fastBuyReplayFailStreak = 0;
            }
            // =====================================================

            env->DeleteLocalRef(currentScreen);
        } else {
            g_chatInputLive = false;
            {
                std::lock_guard<std::mutex> lk(g_chatInputMutex);
                currentChatInput.clear();
            }
            // Pas d'ecran ouvert: reset etat Fast Buy
            g_inQuickBuyMenu      = false;
            InterlockedExchange(&g_pendingFastBuyClicks, 0);
            g_fastBuyLButtonHeld  = false;
            s_fbLastIsChest       = false;
            s_fbLastMenuFound     = false;
            s_fbLastSlot0Name     = "";
        }
    } else {
        g_chatInputLive = false;
        {
            std::lock_guard<std::mutex> lk(g_chatInputMutex);
            currentChatInput.clear();
        }
        g_fastBuyLButtonHeld = false;
    }

    if (g_tabOverlay.isInitialized()) {
        g_tabOverlay.renderScoreboardResources(env, mcObj);
        g_tabOverlay.renderScoreboardBedDisconnectTimers(env, mcObj);
    }
}

static void InstallGameHooks() {
    HWND hwnd = FindGameHwnd();
    g_gameHwnd = hwnd;
    if (hwnd) {
        WNDPROC prev = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
        if (prev && prev != hkWndProc) {
            oWndProc = prev;
        }
        InstallSwapBuffersHook();
    }
}

DWORD WINAPI MainThread(LPVOID lpReserved)
{
    SetUnhandledExceptionFilter(UnhandledCrashFilter);
    BootLogInit((HMODULE)lpReserved);

    JavaVM* jvm  = nullptr;
    JNIEnv* env  = nullptr;

    // Lunar can finish LoadLibrary before jvm.dll / JavaVM is ready (~1/10 ghost injects).
    // Retry attach for up to ~45s instead of giving up on the first attempt.
    typedef jint(JNICALL* tJNI_GetCreatedJavaVMs)(JavaVM**, jsize, jsize*);
    for (int attempt = 0; attempt < 225 && !env; ++attempt) {
        HMODULE hJvm = GetModuleHandleA("jvm.dll");
        if (!hJvm) {
            if (attempt == 0 || attempt % 25 == 0) {
                InjectDiagLog("WAIT inject: jvm.dll pas encore charge — retry...");
            }
            Sleep(200);
            continue;
        }
        tJNI_GetCreatedJavaVMs pGetCreatedJavaVMs =
            (tJNI_GetCreatedJavaVMs)GetProcAddress(hJvm, "JNI_GetCreatedJavaVMs");
        if (!pGetCreatedJavaVMs) {
            InjectDiagLog("ECHEC inject: JNI_GetCreatedJavaVMs introuvable dans jvm.dll");
            break;
        }
        jsize jvmCount = 0;
        jvm = nullptr;
        pGetCreatedJavaVMs(&jvm, 1, &jvmCount);
        if (jvmCount <= 0 || !jvm) {
            if (attempt == 0 || attempt % 25 == 0) {
                InjectDiagLog("WAIT inject: JavaVM pas encore creee — retry...");
            }
            Sleep(200);
            continue;
        }
        const jint attach = jvm->AttachCurrentThread((void**)&env, nullptr);
        if (attach != JNI_OK || !env) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                "WAIT inject: AttachCurrentThread code=%d — retry...", (int)attach);
            if (attempt == 0 || attempt % 25 == 0) InjectDiagLog(buf);
            env = nullptr;
            jvm = nullptr;
            Sleep(200);
            continue;
        }
        g_jvmGlobal = jvm;
        char okBuf[96];
        std::snprintf(okBuf, sizeof(okBuf), "OK: JVM attachee (attempt=%d)", attempt + 1);
        InjectDiagLog(okBuf);
        break;
    }
    if (!env) {
        InjectDiagLog("ECHEC inject: impossible d'attacher la JVM apres retries");
    }

    jclass customGuiClass = nullptr;

    if (env) {
        jclass aveClass = McFindClass(env, "net/minecraft/client/Minecraft");
        jobject loader  = nullptr;
        jclass clsClass = nullptr;
        if (!aveClass || env->ExceptionCheck()) {
            env->ExceptionClear();
            InjectDiagLog("ECHEC inject: McFindClass(Minecraft) a echoue");
            aveClass = nullptr;
        }
        if (aveClass) {
            if (!g_mcClassGlobal) {
                g_mcClassGlobal = (jclass)env->NewGlobalRef(aveClass);
                if (!g_mcClassGlobal || env->ExceptionCheck()) {
                    env->ExceptionClear();
                    g_mcClassGlobal = nullptr;
                    InjectDiagLog("ECHEC inject: NewGlobalRef(Minecraft) a echoue");
                } else {
                    g_midGetMinecraftStatic = env->GetStaticMethodID(g_mcClassGlobal, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
                    if (!g_midGetMinecraftStatic || env->ExceptionCheck()) {
                        env->ExceptionClear();
                        g_midGetMinecraftStatic = nullptr;
                        InjectDiagLog("ECHEC inject: GetStaticMethodID getMinecraft a echoue");
                    }
                }
            }
            clsClass = env->GetObjectClass(aveClass);
            if (clsClass) {
                jmethodID midGetLoader = env->GetMethodID(clsClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
                if (midGetLoader) {
                    loader = env->CallObjectMethod(aveClass, midGetLoader);
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        loader = nullptr;
                        InjectDiagLog("ECHEC inject: getClassLoader a leve une exception");
                    }
                } else {
                    env->ExceptionClear();
                    InjectDiagLog("ECHEC inject: getClassLoader method introuvable");
                }
            }
        }

        customGuiClass = env->DefineClass("FrenchifyGui4", loader, (const jbyte*)FrenchifyGuiLunar_class, FrenchifyGuiLunar_class_len);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            customGuiClass = nullptr;
            InjectDiagLog("WARN: DefineClass(FrenchifyGui4) a echoue, tentative loadClass...");

            if (loader) {
                jclass loaderClass = env->GetObjectClass(loader);
                if (loaderClass) {
                    jmethodID midLoadClass = env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
                    if (midLoadClass) {
                        jstring className = env->NewStringUTF("FrenchifyGui4");
                        customGuiClass = (jclass)env->CallObjectMethod(loader, midLoadClass, className);
                        if (env->ExceptionCheck() || !customGuiClass) {
                            env->ExceptionDescribe();
                            env->ExceptionClear();
                            customGuiClass = nullptr;
                            InjectDiagLog("ECHEC inject: loadClass(FrenchifyGui4) a echoue (GUI non chargee)");
                        }
                        env->DeleteLocalRef(className);
                    } else {
                        env->ExceptionClear();
                        InjectDiagLog("ECHEC inject: ClassLoader.loadClass introuvable");
                    }
                    env->DeleteLocalRef(loaderClass);
                }
            } else {
                InjectDiagLog("ECHEC inject: pas de ClassLoader pour charger FrenchifyGui4");
            }
        }

        if (customGuiClass) {
            jclass globalClass = (jclass)env->NewGlobalRef(customGuiClass);
            env->DeleteLocalRef(customGuiClass);
            customGuiClass = globalClass;
            g_customGuiClassGlobal = customGuiClass;
            InjectDiagLog("OK: FrenchifyGui4 chargee");
        } else {
            InjectDiagLog("ECHEC inject: FrenchifyGui4 absente — /config et toggles GUI indisponibles");
        }

        if (aveClass) env->DeleteLocalRef(aveClass);

        if (g_tabOverlay.initialize(env)) {
            LogInitOnce("[OK] TabOverlay JniCache init");
            InjectDiagLog("OK: TabOverlay init");
        } else {
            LogInitOnce("[FAIL] TabOverlay JniCache init");
            InjectDiagLog("ECHEC partiel: TabOverlay JniCache init a echoue");
        }

        if (customGuiClass) {
            std::string togglesJson = LoadTogglesJsonFromFile();
            if (!togglesJson.empty()) {
                ApplySavedTogglesToGui(env, customGuiClass, togglesJson);
            }
            g_savedTogglesApplied = true;
        }

        if (loader && !g_injectClassLoaderGlobal) {
            g_injectClassLoaderGlobal = env->NewGlobalRef(loader);
        }

        if (InstallNativeTabOverlay(env, loader)) {
            LogInitOnce("[OK] NativeTabOverlay installed");
            InjectDiagLog("OK: NativeTabOverlay install");
        } else {
            LogInitOnce("[FAIL] NativeTabOverlay install");
            InjectDiagLog("ECHEC partiel: NativeTabOverlay install a echoue");
        }
        // Lunar: hook chat vanilla incompatible — PollMinecraftChat via ClientPump.
        Lunar_InitClientPump(env);

        if (g_mcClassGlobal && g_midGetMinecraftStatic) {
            jobject mcObjInit = env->CallStaticObjectMethod(g_mcClassGlobal, g_midGetMinecraftStatic);
            if (mcObjInit && !env->ExceptionCheck()) {
                env->DeleteLocalRef(mcObjInit);
            } else {
                env->ExceptionClear();
                InjectDiagLog("WARN: getMinecraft() a echoue au boot (peut reussir plus tard)");
            }
        }

        if (clsClass) env->DeleteLocalRef(clsClass);
        if (loader) env->DeleteLocalRef(loader);
    }

    InstallGameHooks();
    g_injectReady.store(true, std::memory_order_release);
    if (env && g_customGuiClassGlobal) {
        InjectDiagLog("Inject pret (GUI OK)");
    } else if (env) {
        InjectDiagLog("Inject pret mais GUI manquante — features limitees");
    } else {
        InjectDiagLog("Inject thread actif sans JNI — features Minecraft indisponibles");
    }

    while (true)
    {
        try {
        EnsureWndProcHookAlive();
        Lunar_ScheduleClientPump(g_jvmGlobal);

        const ULONGLONG nowAlive = GetTickCount64();
        g_mainLoopHeartbeat.store(nowAlive, std::memory_order_relaxed);
        Sleep(20);
        } catch (const std::exception&) {
            if (env && env->ExceptionCheck()) env->ExceptionClear();
            Sleep(50);
        } catch (...) {
            if (env && env->ExceptionCheck()) env->ExceptionClear();
            Sleep(50);
        }
    }

    if (g_gameHwnd && oWndProc) {
        SetWindowLongPtr(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
    }
    g_gameHwnd = nullptr;
    RemoveSwapBuffersHook();
    if (jvm && env) {
        Lunar_Shutdown(env);
        g_tabOverlay.shutdown(env);
        if (customGuiClass) env->DeleteGlobalRef(customGuiClass);
        g_customGuiClassGlobal = nullptr;
        if (g_nativeTabClassGlobal) {
            env->DeleteGlobalRef(g_nativeTabClassGlobal);
            g_nativeTabClassGlobal = nullptr;
        }
        if (g_mcClassGlobal) {
            env->DeleteGlobalRef(g_mcClassGlobal);
            g_mcClassGlobal = nullptr;
        }
        g_midGetMinecraftStatic = nullptr;
        if (g_injectClassLoaderGlobal) {
            env->DeleteGlobalRef(g_injectClassLoaderGlobal);
            g_injectClassLoaderGlobal = nullptr;
        }
        g_useNativeTabPipeline = false;
        jvm->DetachCurrentThread();
    }
    g_jvmGlobal = nullptr;

    g_injectReady.store(false, std::memory_order_release);

    FreeLibraryAndExitThread((HMODULE)lpReserved, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
        break;
    }
    return TRUE;
}