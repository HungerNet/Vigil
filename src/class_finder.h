#pragma once

#include <jni.h>

// Lunar Client — résolution MCP via ClassLoader + cache des jclass.

jobject LunarResolveClassLoader(JNIEnv* env);
jclass  LunarFindClass(JNIEnv* env, const char* mcpSlashPath);
jfieldID LunarGetFieldIDFlexible(JNIEnv* env, jclass cls, const char* name,
                                 const char* const* signatures, int signatureCount);
jmethodID LunarGetMethodIDFlexible(JNIEnv* env, jclass cls, const char* name,
                                   const char* const* signatures, int signatureCount);
jmethodID LunarGetStaticMethodIDFlexible(JNIEnv* env, jclass cls, const char* name,
                                         const char* const* signatures, int signatureCount);
void    LunarClassFinderShutdown(JNIEnv* env);

// Noms legacy (sortie de convert_lunar_mappings.py)
inline jobject ResolveClientClassLoader(JNIEnv* env) { return LunarResolveClassLoader(env); }
inline jclass  McFindClass(JNIEnv* env, const char* slashPath) { return LunarFindClass(env, slashPath); }
inline jfieldID McGetFieldIDFlexible(JNIEnv* env, jclass cls, const char* name,
                                     const char* const* signatures, int signatureCount) {
    return LunarGetFieldIDFlexible(env, cls, name, signatures, signatureCount);
}
inline jmethodID McGetMethodIDFlexible(JNIEnv* env, jclass cls, const char* name,
                                       const char* const* signatures, int signatureCount) {
    return LunarGetMethodIDFlexible(env, cls, name, signatures, signatureCount);
}
inline jmethodID McGetStaticMethodIDFlexible(JNIEnv* env, jclass cls, const char* name,
                                             const char* const* signatures, int signatureCount) {
    return LunarGetStaticMethodIDFlexible(env, cls, name, signatures, signatureCount);
}
inline void McLoaderShutdown(JNIEnv* env) { LunarClassFinderShutdown(env); }
