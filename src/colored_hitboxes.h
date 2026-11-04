#pragma once
#include <jni.h>

// Team-colored hitboxes drawn inside RenderGlobal.renderEntities (in-world, vanilla draw path).
// Toggles: VigilGui.coloredHitboxes (players, ingame only), VigilGui.dragonHitboxes (anywhere)
bool TryResolveHitboxJni(JNIEnv* env, jobject mcObj);
bool HitboxJniReady();
bool TryInstallRenderGlobalHook(JNIEnv* env, jobject mcObj, jobject classLoader);
bool IsRenderGlobalHookInstalled();

void RenderColoredHitboxes(JNIEnv* env, jobject mcObj, jclass mcClass, jclass customGuiClass);
