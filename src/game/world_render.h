#pragma once
// Vigil game layer — Suspect Highlight (in-world outlines drawn inside RenderGlobal.renderEntities)
// and over-head markers. Both are drawn by Vigil in world space; nothing in the game (nametags,
// teams, the tab list) is modified.

#include <jni.h>

#include <functional>

// How one player is highlighted. Colors come from the active theme's suspicion language.
struct HighlightStyle {
    int rgb = 0;              // outline color (0xRRGGBB)
    int ringRgb = 0;          // marker ring: Vigil's confidence color (as on the halo)
    float fillAlpha = 0.f;    // tinted volume (0 = none)
    bool marker = false;      // draw the over-head marker
    int tier = 0;             // 0 none, 1 elevated, 2 high, 3 critical (marker glyph shape)
    float confidence = 0.f;   // 0..100 (marker ring)
    bool confirmed = false;   // the user's verdict (marker shows the verdict glyph)
    int verdictRgb = 0;
};

// Returns true and fills `style` when `entityId` should be highlighted.
using HighlightQuery = std::function<bool(int entityId, HighlightStyle& style)>;

bool TryResolveHitboxJni(JNIEnv* env, jobject mcObj);
bool HitboxJniReady();
bool TryInstallRenderGlobalHook(JNIEnv* env, jobject mcObj, jobject classLoader);
bool IsRenderGlobalHookInstalled();
void RenderSuspectHighlights(JNIEnv* env, jobject mcObj, const HighlightQuery& query);
