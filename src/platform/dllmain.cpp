// Vigil — DLL bootstrap.
//
// 1. Attach to the game's JVM (retrying while Lunar finishes starting).
// 2. Resolve Minecraft and its class loader; define the VigilClientPump shim.
// 3. Initialize the runtime (settings, engine, UI) and install the window/present hooks.
// 4. Every 50 ms post a VigilClientPump to Minecraft's scheduled tasks so Vigil runs on the
//    client thread even when no frames are presented (menus, minimized).

#include "../core/log.h"
#include "../core/paths.h"
#include "../game/bytecode/VigilClientPump.h"
#include "../game/class_finder.h"
#include "../game/jvm.h"
#include "../game/runtime.h"
#include "hooks.h"

#include <windows.h>
#include <jni.h>

#include <atomic>
#include <cstdio>
#include <string>

namespace {

std::atomic<bool> g_pumpInflight{false};
jclass g_pumpClass = nullptr;
std::string g_dllDir;

// Reports a startup failure. When Vigil.exe is waiting, it shows the reason in its own window;
// otherwise (manual injection) a message box is the only way to tell the user.
void reportFailure(const char* logLine, const char* userMessage) {
    vigil::log::write("vigil", logLine);
    wchar_t name[64];
    swprintf_s(name, L"Local\\Vigil.Failed.%lu", GetCurrentProcessId());
    if (HANDLE e = OpenEventW(EVENT_MODIFY_STATE, FALSE, name)) {
        SetEvent(e);
        CloseHandle(e);
        return;
    }
    MessageBoxA(nullptr, userMessage, "Vigil", MB_ICONERROR | MB_TOPMOST);
}

extern "C" void JNICALL nativeClientPump(JNIEnv* env, jclass) {
    struct Guard { ~Guard() { g_pumpInflight.store(false); } } guard;
    if (jobject mc = vigil::game::minecraft(env)) {
        vigil::game::Runtime::instance().notePumpSource(true);
        vigil::game::Runtime::instance().pump(env, mc);
        env->DeleteLocalRef(mc);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
}

bool definePumpClass(JNIEnv* env, jobject loader) {
    jclass cls = env->DefineClass("VigilClientPump", loader, reinterpret_cast<const jbyte*>(kVigilClientPumpBytes),
                                  static_cast<jsize>(kVigilClientPumpSize));
    if (env->ExceptionCheck() || !cls) {
        env->ExceptionClear();
        // Already defined by a previous injection into this JVM.
        jclass loaderClass = env->GetObjectClass(loader);
        jmethodID load = env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        jstring name = env->NewStringUTF("VigilClientPump");
        cls = load ? static_cast<jclass>(env->CallObjectMethod(loader, load, name)) : nullptr;
        if (env->ExceptionCheck()) { env->ExceptionClear(); cls = nullptr; }
        env->DeleteLocalRef(name);
        env->DeleteLocalRef(loaderClass);
    }
    if (!cls) return false;
    JNINativeMethod natives[] = {{const_cast<char*>("nativeClientPump"), const_cast<char*>("()V"),
                                  reinterpret_cast<void*>(&nativeClientPump)}};
    if (env->RegisterNatives(cls, natives, 1) != JNI_OK) {
        env->ExceptionClear();
        env->DeleteLocalRef(cls);
        return false;
    }
    g_pumpClass = static_cast<jclass>(env->NewGlobalRef(cls));
    env->DeleteLocalRef(cls);
    return g_pumpClass != nullptr;
}

void schedulePump(JNIEnv* env) {
    if (!g_pumpClass || g_pumpInflight.load()) return;
    static jmethodID schedule = nullptr, ctor = nullptr;
    if (!schedule) {
        schedule = env->GetMethodID(g_mcClassGlobal, "addScheduledTask",
                                    "(Ljava/lang/Runnable;)Lcom/google/common/util/concurrent/ListenableFuture;");
        ctor = env->GetMethodID(g_pumpClass, "<init>", "()V");
        if (env->ExceptionCheck() || !schedule || !ctor) { env->ExceptionClear(); schedule = nullptr; return; }
    }
    jobject mc = vigil::game::minecraft(env);
    if (!mc) return;
    jobject task = env->NewObject(g_pumpClass, ctor);
    if (task && !env->ExceptionCheck()) {
        g_pumpInflight.store(true);
        jobject future = env->CallObjectMethod(mc, schedule, task);
        if (env->ExceptionCheck()) { env->ExceptionClear(); g_pumpInflight.store(false); }
        if (future) env->DeleteLocalRef(future);
    } else {
        env->ExceptionClear();
    }
    if (task) env->DeleteLocalRef(task);
    env->DeleteLocalRef(mc);
}

JNIEnv* attachJvm() {
    using GetVMs = jint(JNICALL*)(JavaVM**, jsize, jsize*);
    // Lunar can finish LoadLibrary before jvm.dll is ready; retry for ~45 s.
    for (int attempt = 0; attempt < 225; ++attempt) {
        HMODULE jvmDll = GetModuleHandleA("jvm.dll");
        auto getVMs = jvmDll ? reinterpret_cast<GetVMs>(GetProcAddress(jvmDll, "JNI_GetCreatedJavaVMs")) : nullptr;
        JavaVM* vm = nullptr;
        jsize count = 0;
        if (getVMs && getVMs(&vm, 1, &count) == JNI_OK && count > 0 && vm) {
            JNIEnv* env = nullptr;
            if (vm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) == JNI_OK && env) {
                g_jvm = vm;
                vigil::log::writef("vigil", "attached to JVM (attempt %d)", attempt + 1);
                return env;
            }
        }
        Sleep(200);
    }
    return nullptr;
}

DWORD WINAPI mainThread(LPVOID module) {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(static_cast<HMODULE>(module), path, MAX_PATH);
    g_dllDir = path;
    g_dllDir = g_dllDir.substr(0, g_dllDir.find_last_of("\\/"));
    vigil::log::init(vigil::paths::dataDirectory(g_dllDir) + "\\logs");

    JNIEnv* env = attachJvm();
    if (!env) {
        reportFailure("startup failed: could not attach to the JVM", "Vigil could not attach to the game's Java runtime.");
        FreeLibraryAndExitThread(static_cast<HMODULE>(module), 1);
    }

    jclass mcClass = nullptr;
    for (int attempt = 0; attempt < 225 && !mcClass; ++attempt) {
        mcClass = McFindClass(env, "net/minecraft/client/Minecraft");
        if (!mcClass) {
            if (attempt == 0 || (attempt + 1) % 25 == 0) {
                vigil::log::writef("vigil", "waiting for Minecraft class (attempt %d/225)", attempt + 1);
            }
            Sleep(200);
        }
    }
    if (!mcClass) {
        g_jvm->DetachCurrentThread();
        reportFailure("startup failed: Minecraft class was not found after retries",
                      "Vigil could not find Minecraft after waiting for startup. Logs are in %LOCALAPPDATA%\\Vigil\\logs.");
        FreeLibraryAndExitThread(static_cast<HMODULE>(module), 1);
    }
    g_mcClassGlobal = static_cast<jclass>(env->NewGlobalRef(mcClass));
    if (!g_mcClassGlobal || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(mcClass);
        g_jvm->DetachCurrentThread();
        reportFailure("startup failed: could not retain the Minecraft class", "Vigil could not start inside Minecraft.");
        FreeLibraryAndExitThread(static_cast<HMODULE>(module), 1);
    }
    g_midGetMinecraftStatic = env->GetStaticMethodID(g_mcClassGlobal, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
    if (env->ExceptionCheck() || !g_midGetMinecraftStatic) {
        env->ExceptionClear();
        env->DeleteLocalRef(mcClass);
        env->DeleteGlobalRef(g_mcClassGlobal);
        g_mcClassGlobal = nullptr;
        g_jvm->DetachCurrentThread();
        reportFailure("startup failed: Minecraft#getMinecraft was not found", "Vigil does not support this Minecraft version.");
        FreeLibraryAndExitThread(static_cast<HMODULE>(module), 1);
    }
    {
        jclass classClass = env->GetObjectClass(mcClass);
        jmethodID getLoader = env->GetMethodID(classClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
        jobject loader = getLoader ? env->CallObjectMethod(mcClass, getLoader) : nullptr;
        if (env->ExceptionCheck()) { env->ExceptionClear(); loader = nullptr; }
        if (loader) {
            g_injectClassLoaderGlobal = env->NewGlobalRef(loader);
            if (!definePumpClass(env, loader)) vigil::log::write("runtime", "client pump unavailable; relying on frame pump");
            env->DeleteLocalRef(loader);
        }
        env->DeleteLocalRef(classClass);
    }
    env->DeleteLocalRef(mcClass);

    HWND hwnd = nullptr;
    for (int i = 0; i < 450 && !(hwnd = vigil::platform::findGameWindow()); ++i) {
        if (i == 0 || (i + 1) % 50 == 0) {
            vigil::log::writef("vigil", "waiting for game window (attempt %d/450)", i + 1);
        }
        Sleep(100);
    }
    if (!hwnd) {
        g_jvm->DetachCurrentThread();
        reportFailure("startup failed: game window was not found after retries",
                      "Vigil could not find the game window. Logs are in %LOCALAPPDATA%\\Vigil\\logs.");
        FreeLibraryAndExitThread(static_cast<HMODULE>(module), 1);
    }
    vigil::game::Runtime::instance().initialize(g_dllDir, hwnd);
    if (!vigil::platform::installWindowHook(hwnd)) {
        vigil::log::write("runtime", "window procedure hook not installed yet; will retry");
    }
    vigil::platform::installPresentHook();
    vigil::log::write("vigil", "runtime initialized");

    for (;;) {
        vigil::platform::ensureWindowHook();
        vigil::platform::ensurePresentHook();
        schedulePump(env);
        Sleep(50);
    }
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        if (HANDLE t = CreateThread(nullptr, 0, mainThread, module, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
