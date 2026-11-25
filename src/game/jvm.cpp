#include "jvm.h"

JavaVM* g_jvm = nullptr;
jclass g_mcClassGlobal = nullptr;
jmethodID g_midGetMinecraftStatic = nullptr;
jobject g_injectClassLoaderGlobal = nullptr;

namespace vigil::game {

JNIEnv* currentEnv() {
    if (!g_jvm) return nullptr;
    JNIEnv* env = nullptr;
    if (g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return nullptr;
    return env;
}

std::string jstringToUtf8(JNIEnv* env, jstring s) {
    if (!env || !s) return {};
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string out = c ? c : "";
    if (c) env->ReleaseStringUTFChars(s, c);
    return out;
}

jobject minecraft(JNIEnv* env) {
    if (!env || !g_mcClassGlobal || !g_midGetMinecraftStatic) return nullptr;
    jobject mc = env->CallStaticObjectMethod(g_mcClassGlobal, g_midGetMinecraftStatic);
    if (jniFailed(env)) return nullptr;
    return mc;
}

}  // namespace vigil::game
