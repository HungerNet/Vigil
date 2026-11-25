#pragma once
// Vigil game layer — resolves Minecraft classes through Lunar's class loader (MCP names) and
// caches global references.

#include <jni.h>

jobject LunarResolveClassLoader(JNIEnv* env);
jclass LunarFindClass(JNIEnv* env, const char* mcpSlashPath);  // returns a local reference
jfieldID LunarGetFieldIDFlexible(JNIEnv* env, jclass cls, const char* name, const char* const* signatures, int count);
jmethodID LunarGetMethodIDFlexible(JNIEnv* env, jclass cls, const char* name, const char* const* signatures, int count);
jmethodID LunarGetStaticMethodIDFlexible(JNIEnv* env, jclass cls, const char* name, const char* const* signatures, int count);
void LunarClassFinderShutdown(JNIEnv* env);

inline jobject ResolveClientClassLoader(JNIEnv* env) { return LunarResolveClassLoader(env); }
inline jclass McFindClass(JNIEnv* env, const char* slashPath) { return LunarFindClass(env, slashPath); }
inline jfieldID McGetFieldIDFlexible(JNIEnv* env, jclass cls, const char* name, const char* const* s, int n) {
    return LunarGetFieldIDFlexible(env, cls, name, s, n);
}
inline jmethodID McGetMethodIDFlexible(JNIEnv* env, jclass cls, const char* name, const char* const* s, int n) {
    return LunarGetMethodIDFlexible(env, cls, name, s, n);
}
inline jmethodID McGetStaticMethodIDFlexible(JNIEnv* env, jclass cls, const char* name, const char* const* s, int n) {
    return LunarGetStaticMethodIDFlexible(env, cls, name, s, n);
}
