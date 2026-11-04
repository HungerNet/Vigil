#include "hud_edit.h"
#include "utils.h"
#include "session_stats.h"
#include <cmath>
#include <windows.h>

namespace {

void WriteGuiStaticBool(JNIEnv* env, jclass guiClass, const char* fieldName, bool value) {
    if (!env || !guiClass || !fieldName) return;
    jfieldID fid = env->GetStaticFieldID(guiClass, fieldName, "Z");
    if (!fid || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    env->SetStaticBooleanField(guiClass, fid, value ? JNI_TRUE : JNI_FALSE);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

void WriteGuiStaticInt(JNIEnv* env, jclass guiClass, const char* fieldName, int value) {
    if (!env || !guiClass || !fieldName) return;
    jfieldID fid = env->GetStaticFieldID(guiClass, fieldName, "I");
    if (!fid || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    env->SetStaticIntField(guiClass, fid, (jint)value);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

int ClampPos(int v) {
    if (v < 0) return 0;
    if (v > 1000) return 1000;
    return v;
}

bool CallStaticVoid(JNIEnv* env, jclass cls, const char* name, const char* sig) {
    if (!env || !cls || !name || !sig) return false;
    jmethodID mid = env->GetStaticMethodID(cls, name, sig);
    if (!mid || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    env->CallStaticVoidMethod(cls, mid);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return true;
}

bool SetMouseGrabbed(JNIEnv* env, bool grabbed) {
    jclass mouseCls = env->FindClass("org/lwjgl/input/Mouse");
    if (!mouseCls || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    jmethodID mid = env->GetStaticMethodID(mouseCls, "setGrabbed", "(Z)V");
    if (!mid || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(mouseCls);
        return false;
    }
    env->CallStaticVoidMethod(mouseCls, mid, grabbed ? JNI_TRUE : JNI_FALSE);
    env->DeleteLocalRef(mouseCls);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return true;
}

bool IsKeyDown(JNIEnv* env, int keyCode) {
    if (keyCode <= 0) return false;
    jclass kb = env->FindClass("org/lwjgl/input/Keyboard");
    if (!kb || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    jmethodID mid = env->GetStaticMethodID(kb, "isKeyDown", "(I)Z");
    if (!mid || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(kb);
        return false;
    }
    jboolean down = env->CallStaticBooleanMethod(kb, mid, (jint)keyCode);
    env->DeleteLocalRef(kb);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return down == JNI_TRUE;
}

bool IsMouseButtonDown(JNIEnv* env, int button) {
    jclass mouseCls = env->FindClass("org/lwjgl/input/Mouse");
    if (!mouseCls || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    jmethodID mid = env->GetStaticMethodID(mouseCls, "isButtonDown", "(I)Z");
    if (!mid || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(mouseCls);
        return false;
    }
    jboolean down = env->CallStaticBooleanMethod(mouseCls, mid, (jint)button);
    env->DeleteLocalRef(mouseCls);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return down == JNI_TRUE;
}

bool GetMouseScaled(JNIEnv* env, jobject mcObj, int scaledW, int scaledH, int& outX, int& outY) {
    outX = 0;
    outY = 0;
    if (!env || !mcObj || scaledW <= 0 || scaledH <= 0) return false;
    jclass mouseCls = env->FindClass("org/lwjgl/input/Mouse");
    if (!mouseCls || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    jmethodID midX = env->GetStaticMethodID(mouseCls, "getX", "()I");
    jmethodID midY = env->GetStaticMethodID(mouseCls, "getY", "()I");
    if (!midX || !midY || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(mouseCls);
        return false;
    }
    const int mx = env->CallStaticIntMethod(mouseCls, midX);
    const int my = env->CallStaticIntMethod(mouseCls, midY);
    env->DeleteLocalRef(mouseCls);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    int displayW = 0;
    int displayH = 0;
    jclass mcClass = env->GetObjectClass(mcObj);
    jfieldID fidW = env->GetFieldID(mcClass, "displayWidth", "I");
    jfieldID fidH = env->GetFieldID(mcClass, "displayHeight", "I");
#ifdef FRENCHIFY_LUNAR
    if ((!fidW || env->ExceptionCheck()) && mcClass) {
        env->ExceptionClear();
        fidW = env->GetFieldID(mcClass, "displayWidth", "I");
        fidH = env->GetFieldID(mcClass, "displayHeight", "I");
    }
#else
    if ((!fidW || env->ExceptionCheck()) && mcClass) {
        env->ExceptionClear();
        fidW = env->GetFieldID(mcClass, "d", "I");
        if (!fidW || env->ExceptionCheck()) {
            env->ExceptionClear();
            fidW = env->GetFieldID(mcClass, "displayWidth", "I");
        }
        fidH = env->GetFieldID(mcClass, "e", "I");
        if (!fidH || env->ExceptionCheck()) {
            env->ExceptionClear();
            fidH = env->GetFieldID(mcClass, "displayHeight", "I");
        }
    }
#endif
    if (fidW && fidH && !env->ExceptionCheck()) {
        displayW = env->GetIntField(mcObj, fidW);
        displayH = env->GetIntField(mcObj, fidH);
    } else {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(mcClass);
    if (displayW <= 0 || displayH <= 0) {
        displayW = scaledW;
        displayH = scaledH;
    }

    outX = mx * scaledW / displayW;
    outY = scaledH - my * scaledH / displayH - 1;
    return true;
}

enum class DragTarget : int {
    None = 0,
    Session = 1,
    Trade = 2,
};

DragTarget g_drag = DragTarget::None;
float g_dragOffX = 0.0f;
float g_dragOffY = 0.0f;
bool g_wasLmb = false;
bool g_ungrabbed = false;

} // namespace

void PollUtilsHotkeys(JNIEnv* env, jclass guiClass) {
    if (!env || !guiClass) return;
    CallStaticVoid(env, guiClass, "pollUtilsHotkeys", "()V");
    CallStaticVoid(env, guiClass, "pollNickQolHotkey", "()V");
}

void HudEditTick(JNIEnv* env, jobject mcObj, jclass mcClass, jclass guiClass) {
    if (!env || !mcObj || !guiClass) return;
    const bool edit = ReadGuiStaticBool(env, guiClass, "hudEditMode", false);
    if (!edit) {
        if (g_ungrabbed) {
            SetMouseGrabbed(env, true);
            g_ungrabbed = false;
        }
        g_drag = DragTarget::None;
        g_wasLmb = false;
        return;
    }

    if (!g_ungrabbed) {
        SetMouseGrabbed(env, false);
        g_ungrabbed = true;
    }

    // ESC = KEY_ESCAPE = 1
    if (IsKeyDown(env, 1)) {
        WriteGuiStaticBool(env, guiClass, "hudEditMode", false);
        SetMouseGrabbed(env, true);
        g_ungrabbed = false;
        g_drag = DragTarget::None;
        g_wasLmb = false;
        return;
    }

    int scaledW = 0;
    int scaledH = 0;
    if (!SessionStatsGetGuiScaledSize(env, mcObj, scaledW, scaledH) || scaledW <= 0 || scaledH <= 0) {
        return;
    }

    int mx = 0;
    int my = 0;
    if (!GetMouseScaled(env, mcObj, scaledW, scaledH, mx, my)) return;

    const bool lmb = IsMouseButtonDown(env, 0);
    const bool lmbEdge = lmb && !g_wasLmb;
    g_wasLmb = lmb;

    float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    const bool hasSession = SessionStatsGetHudRect(s0, s1, s2, s3);

    const int tradePosX = ClampPos(ReadGuiStaticInt(env, guiClass, "tradeIndicatorPosX", 520));
    const int tradePosY = ClampPos(ReadGuiStaticInt(env, guiClass, "tradeIndicatorPosY", 470));
    const float tradeX = (tradePosX / 1000.0f) * (float)scaledW;
    const float tradeY = (tradePosY / 1000.0f) * (float)scaledH;
    const float tradeW = 54.0f;
    const float tradeH = 18.0f;

    if (lmbEdge) {
        g_drag = DragTarget::None;
        if (hasSession && mx >= s0 && mx <= s2 && my >= s1 && my <= s3) {
            g_drag = DragTarget::Session;
            g_dragOffX = s2 - (float)mx;
            g_dragOffY = s3 - (float)my;
        } else if (mx >= tradeX && mx <= tradeX + tradeW && my >= tradeY && my <= tradeY + tradeH) {
            g_drag = DragTarget::Trade;
            g_dragOffX = tradeX - (float)mx;
            g_dragOffY = tradeY - (float)my;
        }
    }

    if (!lmb) {
        g_drag = DragTarget::None;
        return;
    }

    if (g_drag == DragTarget::Session) {
        float endX = (float)mx + g_dragOffX;
        float endY = (float)my + g_dragOffY;
        if (endX < 8.0f) endX = 8.0f;
        if (endY < 8.0f) endY = 8.0f;
        if (endX > (float)scaledW) endX = (float)scaledW;
        if (endY > (float)scaledH) endY = (float)scaledH;
        const int posX = ClampPos((int)std::lround((endX / (float)scaledW) * 1000.0f));
        const int posY = ClampPos((int)std::lround((endY / (float)scaledH) * 1000.0f));
        WriteGuiStaticInt(env, guiClass, "sessionStatsPosX", posX);
        WriteGuiStaticInt(env, guiClass, "sessionStatsPosY", posY);
        SessionStatsApplyPosNormalized(posX, posY);
    } else if (g_drag == DragTarget::Trade) {
        float x = (float)mx + g_dragOffX;
        float y = (float)my + g_dragOffY;
        if (x < 0.0f) x = 0.0f;
        if (y < 0.0f) y = 0.0f;
        if (x > (float)scaledW - 8.0f) x = (float)scaledW - 8.0f;
        if (y > (float)scaledH - 8.0f) y = (float)scaledH - 8.0f;
        const int posX = ClampPos((int)std::lround((x / (float)scaledW) * 1000.0f));
        const int posY = ClampPos((int)std::lround((y / (float)scaledH) * 1000.0f));
        WriteGuiStaticInt(env, guiClass, "tradeIndicatorPosX", posX);
        WriteGuiStaticInt(env, guiClass, "tradeIndicatorPosY", posY);
    }

    (void)mcClass;
}
