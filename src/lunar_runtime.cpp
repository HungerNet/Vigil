#include "lunar_runtime.h"

#ifdef FRENCHIFY_LUNAR

#include "globals.h"
#include "class_finder.h"
#include "tab_overlay.h"
#include "colored_hitboxes.h"
#include "../java_src/lunar_headers/FrenchifyClientPumpLunar.h"

#include <windows.h>
#include <atomic>
#include <cstdio>

extern void PumpMinecraftClientLogic(JNIEnv* env, jobject mcObj, jclass mcClass);
extern void FrenchifyBootLog(const char* msg);

static ULONGLONG g_lastNativeTabRenderAt = 0;
static jclass g_clientPumpClassGlobal = nullptr;
static std::atomic<bool> g_clientPumpInflight{false};

static bool ShouldRenderCustomTab() {
    if (g_tokensState == "Lobby") return false;
    if (!g_tabOverlayStateAllowed) return false;
    if (g_redState != "Ingame" && g_modeState != "Prequeue") return false;
    return true;
}

static bool IsHudGameplayContext(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (!env || !mcObj || !mcClass) return false;
    static jfieldID s_fidCurrentScreen = nullptr;
    if (!s_fidCurrentScreen) {
        s_fidCurrentScreen = env->GetFieldID(mcClass, "currentScreen", "Lnet/minecraft/client/gui/GuiScreen;");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            s_fidCurrentScreen = nullptr;
            return true;
        }
    }
    jobject currentScreen = env->GetObjectField(mcObj, s_fidCurrentScreen);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        currentScreen = nullptr;
    }
    const bool hud = !currentScreen;
    if (currentScreen) env->DeleteLocalRef(currentScreen);
    return hud;
}

static void SuppressVanillaPlayerListTab(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (!env || !mcObj || !mcClass) return;

    static jfieldID s_fidGameSettings = nullptr;
    static jfieldID s_fidKeyBindPlayerList = nullptr;
    static jfieldID s_fidKeyPressed = nullptr;
    static const char* kGsSigs[] = {"Lnet/minecraft/client/settings/GameSettings;", "Lnet/minecraft/client/settings/GameSettings;"};
    static const char* kKbSigs[] = {"Lnet/minecraft/client/settings/KeyBinding;", "Lnet/minecraft/client/settings/KeyBinding;"};

    if (!s_fidGameSettings) {
        s_fidGameSettings = McGetFieldIDFlexible(env, mcClass, "gameSettings", kGsSigs, 2);
    }
    if (!s_fidGameSettings) return;

    jobject gameSettings = env->GetObjectField(mcObj, s_fidGameSettings);
    if (!gameSettings || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    jclass gsClass = env->GetObjectClass(gameSettings);
    if (!gsClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(gameSettings);
        return;
    }

    if (!s_fidKeyBindPlayerList) {
        s_fidKeyBindPlayerList = McGetFieldIDFlexible(env, gsClass, "keyBindPlayerList", kKbSigs, 2);
    }
    if (!s_fidKeyBindPlayerList) {
        env->DeleteLocalRef(gsClass);
        env->DeleteLocalRef(gameSettings);
        return;
    }

    jobject keyBind = env->GetObjectField(gameSettings, s_fidKeyBindPlayerList);
    if (!keyBind || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(gsClass);
        env->DeleteLocalRef(gameSettings);
        return;
    }

    jclass kbClass = env->GetObjectClass(keyBind);
    if (!kbClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(keyBind);
        env->DeleteLocalRef(gsClass);
        env->DeleteLocalRef(gameSettings);
        return;
    }

    if (!s_fidKeyPressed) {
        s_fidKeyPressed = env->GetFieldID(kbClass, "pressed", "Z");
        if (!s_fidKeyPressed || env->ExceptionCheck()) {
            env->ExceptionClear();
            s_fidKeyPressed = nullptr;
        }
    }
    if (s_fidKeyPressed) {
        env->SetBooleanField(keyBind, s_fidKeyPressed, JNI_FALSE);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    static jfieldID s_fidKeyCode = nullptr;
    static jmethodID s_midSetKeyBindState = nullptr;
    if (!s_fidKeyCode) {
        s_fidKeyCode = env->GetFieldID(kbClass, "keyCode", "I");
        if (!s_fidKeyCode || env->ExceptionCheck()) {
            env->ExceptionClear();
            s_fidKeyCode = nullptr;
        }
    }
    if (!s_midSetKeyBindState) {
        static const char* kSetKeyBindSigs[] = {"(IZ)V"};
        s_midSetKeyBindState = McGetMethodIDFlexible(env, gsClass, "setKeyBindState", kSetKeyBindSigs, 1);
        if (!s_midSetKeyBindState || env->ExceptionCheck()) {
            env->ExceptionClear();
            s_midSetKeyBindState = nullptr;
        }
    }
    if (s_fidKeyCode && s_midSetKeyBindState) {
        const jint keyCode = env->GetIntField(keyBind, s_fidKeyCode);
        env->CallVoidMethod(gameSettings, s_midSetKeyBindState, keyCode, JNI_FALSE);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    env->DeleteLocalRef(kbClass);
    env->DeleteLocalRef(keyBind);
    env->DeleteLocalRef(gsClass);
    env->DeleteLocalRef(gameSettings);
}

static bool PollMinecraftTabKeyHeld(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (!env || !mcObj || !mcClass) return false;

    static jfieldID s_fidGameSettings = nullptr;
    static jfieldID s_fidKeyBindPlayerList = nullptr;
    static jclass s_keyBindingClassGlobal = nullptr;
    static jmethodID s_midIsKeyDown = nullptr;

    if (!s_fidGameSettings) {
        s_fidGameSettings = env->GetFieldID(mcClass, "gameSettings", "Lnet/minecraft/client/settings/GameSettings;");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }
    }

    jobject gameSettings = env->GetObjectField(mcObj, s_fidGameSettings);
    if (!gameSettings || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jclass gsClass = env->GetObjectClass(gameSettings);
    if (!gsClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(gameSettings);
        return false;
    }

    if (!s_fidKeyBindPlayerList) {
        s_fidKeyBindPlayerList = env->GetFieldID(gsClass, "keyBindPlayerList", "Lnet/minecraft/client/settings/KeyBinding;");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(gsClass);
            env->DeleteLocalRef(gameSettings);
            return false;
        }
    }

    jobject keyBind = env->GetObjectField(gameSettings, s_fidKeyBindPlayerList);
    if (!keyBind || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(gsClass);
        env->DeleteLocalRef(gameSettings);
        return false;
    }

    if (!s_keyBindingClassGlobal) {
        jclass kbClass = McFindClass(env, "net/minecraft/client/settings/KeyBinding");
        if (kbClass && !env->ExceptionCheck()) {
            s_keyBindingClassGlobal = (jclass)env->NewGlobalRef(kbClass);
            env->DeleteLocalRef(kbClass);
        } else {
            env->ExceptionClear();
        }
    }

    if (!s_midIsKeyDown && s_keyBindingClassGlobal) {
        s_midIsKeyDown = env->GetMethodID(s_keyBindingClassGlobal, "isKeyDown", "()Z");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            s_midIsKeyDown = nullptr;
        }
    }

    bool held = false;
    if (s_midIsKeyDown) {
        held = env->CallBooleanMethod(keyBind, s_midIsKeyDown) == JNI_TRUE;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            held = false;
        }
    }

    env->DeleteLocalRef(keyBind);
    env->DeleteLocalRef(gsClass);
    env->DeleteLocalRef(gameSettings);
    return held;
}

static bool PollTabKeyHeldForRender(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if ((GetAsyncKeyState(VK_TAB) & 0x8000) != 0) return true;
    return PollMinecraftTabKeyHeld(env, mcObj, mcClass);
}

extern "C" JNIEXPORT void JNICALL Java_FrenchifyClientPump_nativeClientPump(JNIEnv* env, jclass);

static jclass LoadInjectedPumpClass(JNIEnv* env, jobject classLoader) {
    if (!env) return nullptr;
    jclass cls = classLoader
        ? env->DefineClass("FrenchifyClientPump", classLoader,
                           (const jbyte*)FrenchifyClientPumpLunar_class,
                           (jsize)FrenchifyClientPumpLunar_class_len)
        : nullptr;
    if (env->ExceptionCheck() || !cls) {
        env->ExceptionClear();
        if (classLoader) {
            jclass loaderClass = env->GetObjectClass(classLoader);
            jmethodID midLoad = loaderClass
                ? env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;")
                : nullptr;
            if (loaderClass) env->DeleteLocalRef(loaderClass);
            if (midLoad) {
                jstring jname = env->NewStringUTF("FrenchifyClientPump");
                cls = (jclass)env->CallObjectMethod(classLoader, midLoad, jname);
                env->DeleteLocalRef(jname);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    cls = nullptr;
                }
            }
        }
    }
    return cls;
}

bool Lunar_InitClientPump(JNIEnv* env) {
    if (!env) return false;
    if (g_clientPumpClassGlobal) return true;

    jobject classLoader = g_injectClassLoaderGlobal;
    if (!classLoader) classLoader = ResolveClientClassLoader(env);
    if (!classLoader) {
        FrenchifyBootLog("ClientPump: no class loader");
        return false;
    }

    jclass pumpClass = LoadInjectedPumpClass(env, classLoader);
    if (!pumpClass) {
        FrenchifyBootLog("ClientPump: DefineClass/loadClass failed");
        return false;
    }

    JNINativeMethod methods[] = {
        {(char*)"nativeClientPump", (char*)"()V", (void*)&Java_FrenchifyClientPump_nativeClientPump},
    };
    if (env->RegisterNatives(pumpClass, methods, 1) != JNI_OK || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(pumpClass);
        FrenchifyBootLog("ClientPump: RegisterNatives failed");
        return false;
    }

    g_clientPumpClassGlobal = (jclass)env->NewGlobalRef(pumpClass);
    env->DeleteLocalRef(pumpClass);
    if (!g_clientPumpClassGlobal) {
        FrenchifyBootLog("ClientPump: NewGlobalRef failed");
        return false;
    }
    FrenchifyBootLog("ClientPump: ready");
    return true;
}

extern "C" JNIEXPORT void JNICALL Java_FrenchifyClientPump_nativeClientPump(JNIEnv* env, jclass) {
    struct PumpInflightGuard {
        std::atomic<bool>& flag;
        ~PumpInflightGuard() { flag.store(false); }
    } guard{g_clientPumpInflight};

    if (!env || !g_mcClassGlobal || !g_midGetMinecraftStatic) return;

    jobject mcObj = env->CallStaticObjectMethod(g_mcClassGlobal, g_midGetMinecraftStatic);
    if (!mcObj || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    PumpMinecraftClientLogic(env, mcObj, g_mcClassGlobal);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(mcObj);
}

void Lunar_ScheduleClientPump(JavaVM* jvm) {
    if (!jvm || !g_injectReady.load(std::memory_order_relaxed)) return;
    if (!g_clientPumpClassGlobal || !g_mcClassGlobal || !g_midGetMinecraftStatic) return;
    if (g_clientPumpInflight.load(std::memory_order_relaxed)) return;

    static ULONGLONG s_lastScheduleAt = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - s_lastScheduleAt < 50) return;
    s_lastScheduleAt = now;

    JNIEnv* env = nullptr;
    bool attached = false;
    const jint envRc = jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (envRc == JNI_EDETACHED) {
        if (jvm->AttachCurrentThread((void**)&env, nullptr) != JNI_OK || !env) return;
        attached = true;
    } else if (envRc != JNI_OK || !env) {
        return;
    }

    static jmethodID s_midSchedule = nullptr;
    static jmethodID s_pumpCtor = nullptr;
    if (!s_midSchedule) {
        s_midSchedule = env->GetMethodID(g_mcClassGlobal, "addScheduledTask",
            "(Ljava/lang/Runnable;)Lcom/google/common/util/concurrent/ListenableFuture;");
        if (!s_midSchedule || env->ExceptionCheck()) {
            env->ExceptionClear();
            if (attached) jvm->DetachCurrentThread();
            return;
        }
    }
    if (!s_pumpCtor) {
        s_pumpCtor = env->GetMethodID(g_clientPumpClassGlobal, "<init>", "()V");
        if (!s_pumpCtor || env->ExceptionCheck()) {
            env->ExceptionClear();
            if (attached) jvm->DetachCurrentThread();
            return;
        }
    }

    jobject mcObj = env->CallStaticObjectMethod(g_mcClassGlobal, g_midGetMinecraftStatic);
    if (!mcObj || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (attached) jvm->DetachCurrentThread();
        return;
    }

    jobject pumpRunnable = env->NewObject(g_clientPumpClassGlobal, s_pumpCtor);
    if (pumpRunnable && !env->ExceptionCheck()) {
        g_clientPumpInflight.store(true, std::memory_order_relaxed);
        env->CallObjectMethod(mcObj, s_midSchedule, pumpRunnable);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            g_clientPumpInflight.store(false, std::memory_order_relaxed);
        }
        env->DeleteLocalRef(pumpRunnable);
    } else {
        env->ExceptionClear();
    }

    env->DeleteLocalRef(mcObj);
    if (attached) jvm->DetachCurrentThread();
}

void Lunar_PumpBegin(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (g_tabOverlayGuiEnabled && g_tabOverlay.isInitialized() && ShouldRenderCustomTab()
        && PollTabKeyHeldForRender(env, mcObj, mcClass)) {
        SuppressVanillaPlayerListTab(env, mcObj, mcClass);
    }

    static ULONGLONG s_lastPumpDiagAt = 0;
    const ULONGLONG nowPump = GetTickCount64();
    if (nowPump - s_lastPumpDiagAt >= 10000) {
        s_lastPumpDiagAt = nowPump;
        char diag[256];
        std::snprintf(diag, sizeof(diag),
            "[Pump] alive red=%s mode=%s tab=%d gui=%d native=%d rg=%d allowed=%d",
            g_redState.c_str(), g_modeState.c_str(),
            g_tabHeld ? 1 : 0, g_tabOverlayGuiEnabled ? 1 : 0,
            g_useNativeTabPipeline ? 1 : 0, IsRenderGlobalHookInstalled() ? 1 : 0,
            g_tabOverlayStateAllowed ? 1 : 0);
        FrenchifyBootLog(diag);
    }
}

void Lunar_PumpEnd(JNIEnv* env, jobject mcObj, jclass mcClass, unsigned long long nowPump) {
    if (!g_tabOverlayGuiEnabled) return;

    const bool tabKeyHeld = PollTabKeyHeldForRender(env, mcObj, mcClass);
    if (tabKeyHeld != g_tabHeld) {
        g_tabHeld = tabKeyHeld;
        const bool osHeld = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
        FrenchifyBootLog(tabKeyHeld
            ? (osHeld ? "[TabOverlay] TAB DOWN (OS)" : "[TabOverlay] TAB DOWN (JNI)")
            : "[TabOverlay] TAB UP");
    }

    if (g_tabHeld && ShouldRenderCustomTab() && IsHudGameplayContext(env, mcObj, mcClass)) {
        const ULONGLONG sinceNative = nowPump - g_lastNativeTabRenderAt;
        const bool nativeStale = !g_useNativeTabPipeline || sinceNative > 300;
        if (nativeStale) {
            SuppressVanillaPlayerListTab(env, mcObj, mcClass);
            g_tabOverlay.renderCustomTab(env, mcObj);
        }
    }
}

void Lunar_HandleNativeTabRender(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (!env || !mcObj || !mcClass) return;

    const bool tabAllowed = g_tabOverlayGuiEnabled && g_tabOverlay.isInitialized()
        && ShouldRenderCustomTab() && IsHudGameplayContext(env, mcObj, mcClass);
    if (!tabAllowed) {
        static ULONGLONG s_lastSkipLog = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - s_lastSkipLog >= 5000) {
            s_lastSkipLog = now;
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                "[TabOverlay] native skip gui=%d init=%d allow=%d red=%s mode=%s state=%d",
                g_tabOverlayGuiEnabled ? 1 : 0, g_tabOverlay.isInitialized() ? 1 : 0,
                ShouldRenderCustomTab() ? 1 : 0, g_redState.c_str(), g_modeState.c_str(),
                g_tabOverlayStateAllowed ? 1 : 0);
            FrenchifyBootLog(buf);
        }
        return;
    }

    const ULONGLONG now = GetTickCount64();
    g_lastNativeTabRenderAt = now;
    static ULONGLONG s_lastNativeRenderLogAt = 0;
    if (now - s_lastNativeRenderLogAt >= 2000) {
        s_lastNativeRenderLogAt = now;
        FrenchifyBootLog("[TabOverlay] native render active");
    }
    SuppressVanillaPlayerListTab(env, mcObj, mcClass);
    g_tabOverlay.renderCustomTab(env, mcObj);
}

bool Lunar_WndProcSkipTabKeys() {
    return g_useNativeTabPipeline;
}

void Lunar_Shutdown(JNIEnv* env) {
    if (env && g_clientPumpClassGlobal) {
        env->DeleteGlobalRef(g_clientPumpClassGlobal);
        g_clientPumpClassGlobal = nullptr;
    }
}

#endif
