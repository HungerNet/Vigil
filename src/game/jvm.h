#pragma once
// Vigil game layer — process-wide JVM handles shared by the JNI modules.

#include <jni.h>

#include <string>

extern JavaVM* g_jvm;
extern jclass g_mcClassGlobal;            // net.minecraft.client.Minecraft
extern jmethodID g_midGetMinecraftStatic; // Minecraft.getMinecraft()
extern jobject g_injectClassLoaderGlobal; // Minecraft's class loader

namespace vigil::game {

// JNIEnv for the calling thread, or nullptr if it is not attached.
JNIEnv* currentEnv();
std::string jstringToUtf8(JNIEnv* env, jstring s);
// Returns Minecraft.getMinecraft() as a local reference (may be null).
jobject minecraft(JNIEnv* env);

// Clears and reports whether a Java exception was pending.
inline bool jniFailed(JNIEnv* env) {
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return true;
    }
    return false;
}

}  // namespace vigil::game
