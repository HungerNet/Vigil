#pragma once

#include <jni.h>

void TradeIndicatorTick(JNIEnv* env, jobject mcObj, jclass mcClass, jclass customGuiClass);
void TradeIndicatorRender(JNIEnv* env);
