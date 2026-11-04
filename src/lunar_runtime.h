#pragma once

#include <jni.h>

#ifdef FRENCHIFY_LUNAR

bool Lunar_InitClientPump(JNIEnv* env);
void Lunar_ScheduleClientPump(JavaVM* jvm);
void Lunar_PumpBegin(JNIEnv* env, jobject mcObj, jclass mcClass);
void Lunar_PumpEnd(JNIEnv* env, jobject mcObj, jclass mcClass, unsigned long long nowPump);
void Lunar_HandleNativeTabRender(JNIEnv* env, jobject mcObj, jclass mcClass);
bool Lunar_WndProcSkipTabKeys();
void Lunar_Shutdown(JNIEnv* env);

#else

inline bool Lunar_InitClientPump(JNIEnv*) { return false; }
inline void Lunar_ScheduleClientPump(JavaVM*) {}
inline void Lunar_PumpBegin(JNIEnv*, jobject, jclass) {}
inline void Lunar_PumpEnd(JNIEnv*, jobject, jclass, unsigned long long) {}
inline void Lunar_HandleNativeTabRender(JNIEnv*, jobject, jclass) {}
inline bool Lunar_WndProcSkipTabKeys() { return false; }
inline void Lunar_Shutdown(JNIEnv*) {}

#endif
