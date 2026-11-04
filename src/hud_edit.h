#pragma once

#include <jni.h>

void HudEditTick(JNIEnv* env, jobject mcObj, jclass mcClass, jclass guiClass);
void PollUtilsHotkeys(JNIEnv* env, jclass guiClass);
