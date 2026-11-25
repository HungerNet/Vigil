#pragma once
// Vigil game layer — Suspect Highlight (in-world outlines drawn inside RenderGlobal.renderEntities).

#include <jni.h>

#include <functional>

// Returns true and fills color (0xRRGGBB) and fill alpha (0..1) when `entityId` should be outlined.
using HighlightQuery = std::function<bool(int entityId, int& rgb, float& fillAlpha)>;

bool TryResolveHitboxJni(JNIEnv* env, jobject mcObj);
bool HitboxJniReady();
bool TryInstallRenderGlobalHook(JNIEnv* env, jobject mcObj, jobject classLoader);
bool IsRenderGlobalHookInstalled();
void RenderSuspectHighlights(JNIEnv* env, jobject mcObj, const HighlightQuery& query);
