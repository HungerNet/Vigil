#include "trade_indicator.h"

#include "globals.h"
#include "jni_helpers.h"
#include "utils.h"
#ifdef VIGIL_LUNAR
#include "class_finder.h"
#endif

#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

static const char* g_tradeCrashStage = "demarrage";
constexpr const char* kInjectLog = "vigil_inject.log";

void TradeLog(const char*) {}
void TradeLogf(const char*, ...) {}
void TradeLogOnce(const char*, const char*) {}
void TradeLogOncef(const char*, const char*, ...) {}

void TradeLogCrash(const char* stage, unsigned long code, const char* detail) {
    if (!stage) stage = "?";
    if (!detail) detail = "";
    char buf[384];
    if (detail[0]) {
        std::snprintf(buf, sizeof(buf),
            "CRASH TradeIndicator HUD | etape=%s | code=0x%08lX | %s", stage, code, detail);
    } else {
        std::snprintf(buf, sizeof(buf),
            "CRASH TradeIndicator HUD | etape=%s | code=0x%08lX", stage, code);
    }
    AppendVigilNamedLog(kInjectLog, buf);
}

void TradeLogJniError(JNIEnv* env, const char* stage) {
    if (!stage) stage = "?";
    char buf[256];
    if (env && env->ExceptionCheck()) {
        std::snprintf(buf, sizeof(buf),
            "ERREUR TradeIndicator JNI | etape=%s | exception Java pending", stage);
        env->ExceptionClear();
    } else {
        std::snprintf(buf, sizeof(buf), "ERREUR TradeIndicator JNI | etape=%s", stage);
    }
    AppendVigilNamedLog(kInjectLog, buf);
}

void TradeResetLogFile() {}

struct TargetHudJni {
    bool ready = false;
    jclass clsGui = nullptr;
    jclass clsScaledRes = nullptr;
    jclass clsLivingBase = nullptr;
    jclass clsAbstractClientPlayer = nullptr;
    jfieldID fidObjectMouseOver = nullptr;
    jfieldID fidEntityHit = nullptr;
    jfieldID fidThePlayer = nullptr;
    jfieldID fidDisplayWidth = nullptr;
    jfieldID fidDisplayHeight = nullptr;
    jfieldID fidFontRenderer = nullptr;
    jfieldID fidTextureManager = nullptr;
    jmethodID midGetTextureManager = nullptr;
    jfieldID fidCurrentScreen = nullptr;
    jmethodID midScaledResCtor = nullptr;
    jmethodID midScaledWidth = nullptr;
    jmethodID midScaledHeight = nullptr;
    jmethodID midGuiDrawRect = nullptr;
    jmethodID midGuiDrawScaledCustomRect = nullptr;
    jmethodID midBindTexture = nullptr;
    jmethodID midDrawStringShadow = nullptr;
    jmethodID midGetHealth = nullptr;
    jmethodID midGetLocationSkin = nullptr;
    jmethodID midGetLocationSkinAlt = nullptr;
    jmethodID midGetPlayerInfo = nullptr;
    jmethodID midNpiGetLocationSkin = nullptr;
    jmethodID midRlGetPath = nullptr;
    jmethodID midGetGameProfile = nullptr;
    jmethodID midProfileGetName = nullptr;
};

TargetHudJni g_jni;

constexpr float kOutcomeEpsilon = 0.25f;
constexpr int kHeadSize = 28;
constexpr float kHudTextScale = 1.45f;
constexpr int kHudOffsetX = 10;
constexpr int kHudOffsetY = -18;
constexpr GLbitfield kTradeGlAttribMask =
    GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TRANSFORM_BIT | GL_VIEWPORT_BIT | GL_TEXTURE_BIT;

void SetupTradeOrtho(int displayW, int displayH) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)displayW, (double)displayH, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
}

int PackArgb(int a, int r, int g, int b) {
    return ((a & 0xFF) << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

int OutcomeColor(float selfHp, float enemyHp) {
    const float diff = selfHp - enemyHp;
    if (diff > kOutcomeEpsilon) return 0x55FF55;
    if (diff < -kOutcomeEpsilon) return 0xFF5555;
    return 0xFFFF55;
}

void FormatOutcomeLabel(float selfHp, float enemyHp, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    const float diff = selfHp - enemyHp;
    if (diff > kOutcomeEpsilon) {
        std::snprintf(out, outSize, "+%.1f", diff);
    } else if (diff < -kOutcomeEpsilon) {
        std::snprintf(out, outSize, "%.1f", diff);
    } else {
        std::snprintf(out, outSize, "0.0");
    }
}

jmethodID FindFontDrawMethod(JNIEnv* env, jclass fontClass, bool shadow) {
    static const char* kFloatSigs[] = {"(Ljava/lang/String;FFI)I", "(Ljava/lang/String;IFFI)I"};
    static const char* kNamesShadow[] = {"a", "b", "c"};
    static const char* kNamesPlain[] = {"b", "a", "c"};
    const char** names = shadow ? kNamesShadow : kNamesPlain;
    for (int i = 0; i < 3; ++i) {
        for (const char* sig : kFloatSigs) {
            jmethodID mid = env->GetMethodID(fontClass, names[i], sig);
            if (mid && !env->ExceptionCheck()) return mid;
            env->ExceptionClear();
        }
    }
#ifdef VIGIL_LUNAR
    return env->GetMethodID(fontClass, shadow ? "drawStringWithShadow" : "drawString",
        "(Ljava/lang/String;FFI)I");
#else
    jmethodID mid = env->GetMethodID(fontClass,
        shadow ? "drawStringWithShadow" : "drawString", "(Ljava/lang/String;FFI)I");
    if (mid && !env->ExceptionCheck()) return mid;
    env->ExceptionClear();
    return nullptr;
#endif
}

bool ResolveFontDrawFromRenderer(JNIEnv* env, jobject fontRenderer) {
    if (!env || !fontRenderer || g_jni.midDrawStringShadow) return g_jni.midDrawStringShadow != nullptr;
    jclass fontClass = env->GetObjectClass(fontRenderer);
    if (!fontClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    g_jni.midDrawStringShadow = FindFontDrawMethod(env, fontClass, true);
    if (!g_jni.midDrawStringShadow) {
        g_jni.midDrawStringShadow = FindFontDrawMethod(env, fontClass, false);
    }
    env->DeleteLocalRef(fontClass);
    return g_jni.midDrawStringShadow != nullptr;
}

bool ResolveBindTexture(JNIEnv* env, jobject textureManager) {
    if (!env || !textureManager) return false;
    if (g_jni.midBindTexture) return true;
    jclass texClass = env->GetObjectClass(textureManager);
    if (!texClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
#ifdef VIGIL_LUNAR
    g_jni.midBindTexture = env->GetMethodID(texClass, "bindTexture",
        "(Lnet/minecraft/util/ResourceLocation;)V");
#else
    g_jni.midBindTexture = env->GetMethodID(texClass, "a", "(Ljy;)V");
    if (!g_jni.midBindTexture || env->ExceptionCheck()) {
        env->ExceptionClear();
        g_jni.midBindTexture = env->GetMethodID(texClass, "bindTexture",
            "(Lnet/minecraft/util/ResourceLocation;)V");
    }
#endif
    env->DeleteLocalRef(texClass);
    if (g_jni.midBindTexture && !env->ExceptionCheck()) return true;
    env->ExceptionClear();
    g_jni.midBindTexture = nullptr;
    return false;
}

jobject GetTextureManager(JNIEnv* env, jobject mcObj) {
    if (!env || !mcObj) return nullptr;
    if (g_jni.fidTextureManager) {
        jobject tm = env->GetObjectField(mcObj, g_jni.fidTextureManager);
        if (tm && !env->ExceptionCheck()) return tm;
        env->ExceptionClear();
        if (tm) env->DeleteLocalRef(tm);
    }
    if (g_jni.midGetTextureManager) {
        jobject tm = env->CallObjectMethod(mcObj, g_jni.midGetTextureManager);
        if (tm && !env->ExceptionCheck()) return tm;
        env->ExceptionClear();
        if (tm) env->DeleteLocalRef(tm);
    }
    return nullptr;
}

bool ResolveTargetHudJni(JNIEnv* env, jclass mcClass) {
    if (g_jni.ready) return true;
    if (!env || !mcClass) return false;

#ifdef VIGIL_LUNAR
    g_jni.fidObjectMouseOver = env->GetFieldID(mcClass, "objectMouseOver",
        "Lnet/minecraft/util/MovingObjectPosition;");
    g_jni.fidThePlayer = env->GetFieldID(mcClass, "thePlayer",
        "Lnet/minecraft/client/entity/EntityPlayerSP;");
    g_jni.fidDisplayWidth = env->GetFieldID(mcClass, "displayWidth", "I");
    g_jni.fidDisplayHeight = env->GetFieldID(mcClass, "displayHeight", "I");
    g_jni.fidFontRenderer = env->GetFieldID(mcClass, "fontRendererObj",
        "Lnet/minecraft/client/gui/FontRenderer;");
    g_jni.fidTextureManager = env->GetFieldID(mcClass, "renderEngine",
        "Lnet/minecraft/client/renderer/texture/TextureManager;");
    if (!g_jni.fidTextureManager || env->ExceptionCheck()) {
        env->ExceptionClear();
        g_jni.fidTextureManager = nullptr;
    }
    g_jni.midGetTextureManager = env->GetMethodID(mcClass, "getTextureManager",
        "()Lnet/minecraft/client/renderer/texture/TextureManager;");
    if (!g_jni.midGetTextureManager || env->ExceptionCheck()) {
        env->ExceptionClear();
        g_jni.midGetTextureManager = nullptr;
    }
    g_jni.fidCurrentScreen = env->GetFieldID(mcClass, "currentScreen",
        "Lnet/minecraft/client/gui/GuiScreen;");

    jclass mopClass = McFindClass(env, "net/minecraft/util/MovingObjectPosition");
    jclass guiClass = McFindClass(env, "net/minecraft/client/gui/Gui");
    jclass texClass = McFindClass(env, "net/minecraft/client/renderer/texture/TextureManager");
    jclass fontClass = McFindClass(env, "net/minecraft/client/gui/FontRenderer");
    jclass scaledClass = McFindClass(env, "net/minecraft/client/gui/ScaledResolution");
    jclass profileClass = McFindClass(env, "com/mojang/authlib/GameProfile");
#else
    g_jni.fidObjectMouseOver = env->GetFieldID(mcClass, "s", "Lauh;");
    if (!g_jni.fidObjectMouseOver || env->ExceptionCheck()) {
        env->ExceptionClear();
        g_jni.fidObjectMouseOver = env->GetFieldID(mcClass, "objectMouseOver",
            "Lnet/minecraft/util/MovingObjectPosition;");
    }
    g_jni.fidThePlayer = env->GetFieldID(mcClass, "h", "Lbew;");
    if (!g_jni.fidThePlayer || env->ExceptionCheck()) {
        env->ExceptionClear();
        g_jni.fidThePlayer = env->GetFieldID(mcClass, "thePlayer",
            "Lnet/minecraft/client/entity/EntityPlayerSP;");
    }
    g_jni.fidDisplayWidth = env->GetFieldID(mcClass, "d", "I");
    g_jni.fidDisplayHeight = env->GetFieldID(mcClass, "e", "I");
    g_jni.fidFontRenderer = env->GetFieldID(mcClass, "k", "Lavn;");
    g_jni.fidTextureManager = env->GetFieldID(mcClass, "R", "Lbmj;");
    if (!g_jni.fidTextureManager || env->ExceptionCheck()) {
        env->ExceptionClear();
        g_jni.fidTextureManager = env->GetFieldID(mcClass, "renderEngine",
            "Lnet/minecraft/client/renderer/texture/TextureManager;");
    }
    if (!g_jni.fidTextureManager || env->ExceptionCheck()) {
        env->ExceptionClear();
        g_jni.fidTextureManager = nullptr;
    }
    g_jni.midGetTextureManager = env->GetMethodID(mcClass, "P", "()Lbmj;");
    if (!g_jni.midGetTextureManager || env->ExceptionCheck()) {
        env->ExceptionClear();
        g_jni.midGetTextureManager = env->GetMethodID(mcClass, "getTextureManager",
            "()Lnet/minecraft/client/renderer/texture/TextureManager;");
    }
    if (!g_jni.midGetTextureManager || env->ExceptionCheck()) {
        env->ExceptionClear();
        g_jni.midGetTextureManager = nullptr;
    }
    g_jni.fidCurrentScreen = env->GetFieldID(mcClass, "m", "Laxu;");

    jclass mopClass = env->FindClass("auh");
    jclass guiClass = env->FindClass("avp");
    jclass texClass = env->FindClass("bmj");
    if (!texClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        texClass = env->FindClass("net/minecraft/client/renderer/texture/TextureManager");
    }
    jclass fontClass = env->FindClass("avn");
    jclass scaledClass = env->FindClass("avr");
    jclass profileClass = env->FindClass("com/mojang/authlib/GameProfile");
#endif
    if (env->ExceptionCheck()) env->ExceptionClear();

    if (mopClass && !env->ExceptionCheck()) {
#ifdef VIGIL_LUNAR
        g_jni.fidEntityHit = env->GetFieldID(mopClass, "entityHit", "Lnet/minecraft/entity/Entity;");
#else
        g_jni.fidEntityHit = env->GetFieldID(mopClass, "d", "Lpk;");
        if (!g_jni.fidEntityHit || env->ExceptionCheck()) {
            env->ExceptionClear();
            g_jni.fidEntityHit = env->GetFieldID(mopClass, "entityHit", "Lnet/minecraft/entity/Entity;");
        }
#endif
        env->DeleteLocalRef(mopClass);
    } else {
        env->ExceptionClear();
    }

    jclass livingLocal = nullptr;
    jclass abstractLocal = nullptr;
#ifdef VIGIL_LUNAR
    livingLocal = McFindClass(env, "net/minecraft/entity/EntityLivingBase");
    abstractLocal = McFindClass(env, "net/minecraft/client/entity/AbstractClientPlayer");
#else
    livingLocal = env->FindClass("pr");
    abstractLocal = env->FindClass("bet");
    if (!abstractLocal || env->ExceptionCheck()) {
        env->ExceptionClear();
        abstractLocal = env->FindClass("bex");
    }
#endif
    if (env->ExceptionCheck()) env->ExceptionClear();

    if (livingLocal && !env->ExceptionCheck()) {
#ifdef VIGIL_LUNAR
        g_jni.midGetHealth = env->GetMethodID(livingLocal, "getHealth", "()F");
#else
        g_jni.midGetHealth = env->GetMethodID(livingLocal, "bn", "()F");
        if (!g_jni.midGetHealth || env->ExceptionCheck()) {
            env->ExceptionClear();
            g_jni.midGetHealth = env->GetMethodID(livingLocal, "getHealth", "()F");
        }
#endif
        g_jni.clsLivingBase = (jclass)env->NewGlobalRef(livingLocal);
        env->DeleteLocalRef(livingLocal);
    } else {
        env->ExceptionClear();
    }

    if (abstractLocal && !env->ExceptionCheck()) {
#ifdef VIGIL_LUNAR
        g_jni.midGetLocationSkin = env->GetMethodID(abstractLocal, "getLocationSkin",
            "()Lnet/minecraft/util/ResourceLocation;");
        g_jni.midGetLocationSkinAlt = env->GetMethodID(abstractLocal, "getLocationCape",
            "()Lnet/minecraft/util/ResourceLocation;");
        g_jni.midGetPlayerInfo = env->GetMethodID(abstractLocal, "getPlayerInfo",
            "()Lnet/minecraft/client/network/NetworkPlayerInfo;");
        g_jni.midGetGameProfile = env->GetMethodID(abstractLocal, "getGameProfile",
            "()Lcom/mojang/authlib/GameProfile;");
#else
        g_jni.midGetLocationSkin = env->GetMethodID(abstractLocal, "k", "()Ljy;");
        if (!g_jni.midGetLocationSkin || env->ExceptionCheck()) {
            env->ExceptionClear();
            g_jni.midGetLocationSkin = env->GetMethodID(abstractLocal, "getLocationSkin",
                "()Lnet/minecraft/util/ResourceLocation;");
        }
        g_jni.midGetLocationSkinAlt = env->GetMethodID(abstractLocal, "i", "()Ljy;");
        if (!g_jni.midGetLocationSkinAlt || env->ExceptionCheck()) {
            env->ExceptionClear();
            g_jni.midGetLocationSkinAlt = env->GetMethodID(abstractLocal, "getLocationCape",
                "()Lnet/minecraft/util/ResourceLocation;");
        }
        g_jni.midGetPlayerInfo = env->GetMethodID(abstractLocal, "b", "()Lbdc;");
        if (!g_jni.midGetPlayerInfo || env->ExceptionCheck()) {
            env->ExceptionClear();
            g_jni.midGetPlayerInfo = env->GetMethodID(abstractLocal, "getPlayerInfo",
                "()Lnet/minecraft/client/network/NetworkPlayerInfo;");
        }
#endif
        if (env->ExceptionCheck()) env->ExceptionClear();

        jclass npiClass = nullptr;
#ifdef VIGIL_LUNAR
        npiClass = McFindClass(env, "net/minecraft/client/network/NetworkPlayerInfo");
#else
        npiClass = env->FindClass("bdc");
#endif
        if (npiClass && !env->ExceptionCheck()) {
#ifdef VIGIL_LUNAR
            g_jni.midNpiGetLocationSkin = env->GetMethodID(npiClass, "getLocationSkin",
                "()Lnet/minecraft/util/ResourceLocation;");
#else
            g_jni.midNpiGetLocationSkin = env->GetMethodID(npiClass, "g", "()Ljy;");
            if (!g_jni.midNpiGetLocationSkin || env->ExceptionCheck()) {
                env->ExceptionClear();
                g_jni.midNpiGetLocationSkin = env->GetMethodID(npiClass, "getLocationSkin",
                    "()Lnet/minecraft/util/ResourceLocation;");
            }
#endif
            env->DeleteLocalRef(npiClass);
        } else {
            env->ExceptionClear();
        }

        jclass rlClass = nullptr;
#ifdef VIGIL_LUNAR
        rlClass = McFindClass(env, "net/minecraft/util/ResourceLocation");
#else
        rlClass = env->FindClass("jy");
#endif
        if (rlClass && !env->ExceptionCheck()) {
#ifdef VIGIL_LUNAR
            g_jni.midRlGetPath = env->GetMethodID(rlClass, "getResourcePath", "()Ljava/lang/String;");
#else
            g_jni.midRlGetPath = env->GetMethodID(rlClass, "a", "()Ljava/lang/String;");
            if (!g_jni.midRlGetPath || env->ExceptionCheck()) {
                env->ExceptionClear();
                g_jni.midRlGetPath = env->GetMethodID(rlClass, "getResourcePath", "()Ljava/lang/String;");
            }
#endif
            env->DeleteLocalRef(rlClass);
        } else {
            env->ExceptionClear();
        }

#ifndef VIGIL_LUNAR
        jclass entityPlayerClass = env->FindClass("wn");
        if (entityPlayerClass && !env->ExceptionCheck()) {
            g_jni.midGetGameProfile = env->GetMethodID(entityPlayerClass, "cd",
                "()Lcom/mojang/authlib/GameProfile;");
            if (!g_jni.midGetGameProfile || env->ExceptionCheck()) {
                env->ExceptionClear();
                g_jni.midGetGameProfile = env->GetMethodID(entityPlayerClass, "getGameProfile",
                    "()Lcom/mojang/authlib/GameProfile;");
            }
            env->DeleteLocalRef(entityPlayerClass);
        } else {
            env->ExceptionClear();
        }
        if (!g_jni.midGetGameProfile || env->ExceptionCheck()) {
            env->ExceptionClear();
            g_jni.midGetGameProfile = env->GetMethodID(abstractLocal, "cd",
                "()Lcom/mojang/authlib/GameProfile;");
        }
        if (!g_jni.midGetGameProfile || env->ExceptionCheck()) {
            env->ExceptionClear();
            g_jni.midGetGameProfile = env->GetMethodID(abstractLocal, "getGameProfile",
                "()Lcom/mojang/authlib/GameProfile;");
        }
#endif
        if (env->ExceptionCheck()) env->ExceptionClear();
        g_jni.clsAbstractClientPlayer = (jclass)env->NewGlobalRef(abstractLocal);
        env->DeleteLocalRef(abstractLocal);
    } else {
        env->ExceptionClear();
    }

    if (profileClass && !env->ExceptionCheck()) {
        g_jni.midProfileGetName = env->GetMethodID(profileClass, "getName", "()Ljava/lang/String;");
        env->DeleteLocalRef(profileClass);
    } else {
        env->ExceptionClear();
    }

    if (guiClass && !env->ExceptionCheck()) {
#ifdef VIGIL_LUNAR
        g_jni.midGuiDrawRect = env->GetStaticMethodID(guiClass, "drawRect", "(IIIII)V");
        g_jni.midGuiDrawScaledCustomRect = env->GetStaticMethodID(guiClass,
            "drawScaledCustomSizeModalRect", "(IIFFIIFF)V");
#else
        g_jni.midGuiDrawRect = env->GetStaticMethodID(guiClass, "a", "(IIIII)V");
        g_jni.midGuiDrawScaledCustomRect = env->GetStaticMethodID(guiClass, "a", "(IIFFIIFF)V");
        if (!g_jni.midGuiDrawScaledCustomRect || env->ExceptionCheck()) {
            env->ExceptionClear();
            g_jni.midGuiDrawScaledCustomRect = env->GetStaticMethodID(guiClass,
                "drawScaledCustomSizeModalRect", "(IIFFIIFF)V");
        }
#endif
        g_jni.clsGui = (jclass)env->NewGlobalRef(guiClass);
        env->DeleteLocalRef(guiClass);
    } else {
        env->ExceptionClear();
    }

    if (texClass && !env->ExceptionCheck()) {
#ifdef VIGIL_LUNAR
        g_jni.midBindTexture = env->GetMethodID(texClass, "bindTexture",
            "(Lnet/minecraft/util/ResourceLocation;)V");
#else
        g_jni.midBindTexture = env->GetMethodID(texClass, "a", "(Ljy;)V");
#endif
        env->DeleteLocalRef(texClass);
    } else {
        env->ExceptionClear();
    }

    if (fontClass && !env->ExceptionCheck()) {
#ifdef VIGIL_LUNAR
        g_jni.midDrawStringShadow = env->GetMethodID(fontClass, "drawStringWithShadow",
            "(Ljava/lang/String;FFI)I");
#else
        g_jni.midDrawStringShadow = FindFontDrawMethod(env, fontClass, true);
        if (!g_jni.midDrawStringShadow) {
            g_jni.midDrawStringShadow = FindFontDrawMethod(env, fontClass, false);
        }
#endif
        env->DeleteLocalRef(fontClass);
    } else {
        env->ExceptionClear();
    }

    if (scaledClass && !env->ExceptionCheck()) {
#ifdef VIGIL_LUNAR
        g_jni.midScaledResCtor = env->GetMethodID(scaledClass, "<init>", "(Lnet/minecraft/client/Minecraft;)V");
        g_jni.midScaledWidth = env->GetMethodID(scaledClass, "getScaledWidth", "()I");
        g_jni.midScaledHeight = env->GetMethodID(scaledClass, "getScaledHeight", "()I");
#else
        g_jni.midScaledResCtor = env->GetMethodID(scaledClass, "<init>", "(Lave;)V");
        if (!g_jni.midScaledResCtor || env->ExceptionCheck()) {
            env->ExceptionClear();
            g_jni.midScaledResCtor = env->GetMethodID(scaledClass, "<init>",
                "(Lnet/minecraft/client/Minecraft;)V");
        }
        g_jni.midScaledWidth = env->GetMethodID(scaledClass, "a", "()I");
        if (!g_jni.midScaledWidth || env->ExceptionCheck()) {
            env->ExceptionClear();
            g_jni.midScaledWidth = env->GetMethodID(scaledClass, "getScaledWidth", "()I");
        }
        g_jni.midScaledHeight = env->GetMethodID(scaledClass, "b", "()I");
        if (!g_jni.midScaledHeight || env->ExceptionCheck()) {
            env->ExceptionClear();
            g_jni.midScaledHeight = env->GetMethodID(scaledClass, "getScaledHeight", "()I");
        }
#endif
        g_jni.clsScaledRes = (jclass)env->NewGlobalRef(scaledClass);
        env->DeleteLocalRef(scaledClass);
    } else {
        env->ExceptionClear();
    }

    const bool coreReady = g_jni.fidObjectMouseOver && g_jni.fidEntityHit && g_jni.fidThePlayer &&
        g_jni.clsLivingBase && g_jni.midGetHealth &&
        g_jni.clsGui && g_jni.midGuiDrawRect &&
        g_jni.fidFontRenderer &&
        g_jni.clsScaledRes && g_jni.midScaledResCtor &&
        g_jni.midScaledWidth && g_jni.midScaledHeight;
    g_jni.ready = coreReady;
    if (!g_jni.ready) {
        TradeLogOncef("jni_fail", "FATAL: JNI incomplet (gui=%d font=%d)",
            g_jni.clsGui ? 1 : 0, g_jni.fidFontRenderer ? 1 : 0);
    }
    return g_jni.ready;
}

std::string ReadLivingName(JNIEnv* env, jobject living, jclass livingClass) {
    if (!env || !living || !livingClass) return "";
    if (g_jni.midGetGameProfile && g_jni.midProfileGetName) {
        jobject profile = env->CallObjectMethod(living, g_jni.midGetGameProfile);
        if (profile && !env->ExceptionCheck()) {
            jstring js = (jstring)env->CallObjectMethod(profile, g_jni.midProfileGetName);
            env->DeleteLocalRef(profile);
            if (js && !env->ExceptionCheck()) {
                const char* c = env->GetStringUTFChars(js, nullptr);
                std::string name = c ? c : "";
                if (c) env->ReleaseStringUTFChars(js, c);
                env->DeleteLocalRef(js);
                if (!name.empty()) return StripMinecraftFormatting(name);
            }
            env->ExceptionClear();
        } else {
            env->ExceptionClear();
        }
    }

    const std::string display = GetEntityDisplayNameFormatted(env, living, livingClass);
    if (!display.empty()) return StripMinecraftFormatting(display);

    static const char* kNameMethods[] = {"e_", "getName"};
    for (const char* methodName : kNameMethods) {
        jmethodID midName = env->GetMethodID(livingClass, methodName, "()Ljava/lang/String;");
        if (!midName || env->ExceptionCheck()) {
            env->ExceptionClear();
            continue;
        }
        jstring js = (jstring)env->CallObjectMethod(living, midName);
        if (!js || env->ExceptionCheck()) {
            env->ExceptionClear();
            continue;
        }
        const char* c = env->GetStringUTFChars(js, nullptr);
        std::string name = c ? c : "";
        if (c) env->ReleaseStringUTFChars(js, c);
        env->DeleteLocalRef(js);
        if (!name.empty()) return StripMinecraftFormatting(name);
    }
    return "";
}

bool IsTeammate(JNIEnv* env, jobject mcObj, jclass mcClass, jobject self, jobject target) {
    if (!env || !mcObj || !mcClass || !self || !target) return false;
    jclass selfClass = env->GetObjectClass(self);
    jclass targetClass = env->GetObjectClass(target);
    const std::string selfTeam = GetEntityTeamColorName(env, mcObj, mcClass, self, selfClass);
    const std::string targetTeam = GetEntityTeamColorName(env, mcObj, mcClass, target, targetClass);
    env->DeleteLocalRef(selfClass);
    env->DeleteLocalRef(targetClass);
    if (selfTeam.empty() || targetTeam.empty()) return false;
    if (selfTeam == "white" && targetTeam == "white") return false;
    return LowerAscii(selfTeam) == LowerAscii(targetTeam);
}

jobject ResolveCrosshairLiving(JNIEnv* env, jobject mcObj, jclass mcClass, jclass& outTargetClass,
                               const char*& outSkipReason) {
    outTargetClass = nullptr;
    outSkipReason = "ok";
    if (!g_jni.fidObjectMouseOver || !g_jni.fidEntityHit) {
        outSkipReason = "no_mouseover_fields";
        return nullptr;
    }

    jobject mouseOver = env->GetObjectField(mcObj, g_jni.fidObjectMouseOver);
    if (!mouseOver || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (mouseOver) env->DeleteLocalRef(mouseOver);
        outSkipReason = "mouseover_null";
        return nullptr;
    }

    jobject entity = env->GetObjectField(mouseOver, g_jni.fidEntityHit);
    env->DeleteLocalRef(mouseOver);
    if (!entity || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (entity) env->DeleteLocalRef(entity);
        outSkipReason = "entity_hit_null";
        return nullptr;
    }

    if (!g_jni.clsLivingBase || !env->IsInstanceOf(entity, g_jni.clsLivingBase)) {
        env->DeleteLocalRef(entity);
        outSkipReason = "not_living";
        return nullptr;
    }

    if (!g_jni.clsAbstractClientPlayer || !env->IsInstanceOf(entity, g_jni.clsAbstractClientPlayer)) {
        env->DeleteLocalRef(entity);
        outSkipReason = "not_player";
        return nullptr;
    }

    jobject self = env->GetObjectField(mcObj, g_jni.fidThePlayer);
    if (self && env->IsSameObject(entity, self)) {
        env->DeleteLocalRef(self);
        env->DeleteLocalRef(entity);
        outSkipReason = "self";
        return nullptr;
    }
    if (self) {
        if (IsTeammate(env, mcObj, mcClass, self, entity)) {
            env->DeleteLocalRef(self);
            env->DeleteLocalRef(entity);
            outSkipReason = "teammate";
            return nullptr;
        }
        env->DeleteLocalRef(self);
    }

    outTargetClass = env->GetObjectClass(entity);
    return entity;
}

void DrawGuiRect(JNIEnv* env, int left, int top, int right, int bottom, int color) {
    if (!g_jni.clsGui || !g_jni.midGuiDrawRect) return;
    env->CallStaticVoidMethod(g_jni.clsGui, g_jni.midGuiDrawRect,
        (jint)left, (jint)top, (jint)right, (jint)bottom, (jint)color);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

void DrawGuiText(JNIEnv* env, jobject fontRenderer, const std::string& text, float x, float y, int color) {
    if (text.empty() || !fontRenderer || !g_jni.midDrawStringShadow) return;
    jstring js = env->NewStringUTF(text.c_str());
    if (!js) return;
    env->CallIntMethod(fontRenderer, g_jni.midDrawStringShadow, js, x, y, (jint)color);
    env->DeleteLocalRef(js);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

void DrawGuiTextScaled(JNIEnv* env, jobject fontRenderer, const char* text, float x, float y,
                       int color, float scale) {
    if (!text || !text[0] || !fontRenderer || scale <= 0.0f) return;
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glScalef(scale, scale, 1.0f);
    DrawGuiText(env, fontRenderer, text, 0.0f, 0.0f, color);
    glPopMatrix();
}

bool IsCapeResourcePath(const std::string& path) {
    const std::string lower = LowerAscii(path);
    return lower.find("cape") != std::string::npos || lower.find("cloak") != std::string::npos;
}

std::string GetResourceLocationPath(JNIEnv* env, jobject resource) {
    if (!env || !resource || !g_jni.midRlGetPath) return "";
    jstring js = (jstring)env->CallObjectMethod(resource, g_jni.midRlGetPath);
    if (!js || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }
    const char* c = env->GetStringUTFChars(js, nullptr);
    std::string path = c ? c : "";
    if (c) env->ReleaseStringUTFChars(js, c);
    env->DeleteLocalRef(js);
    return path;
}

bool IsPlayerSkinResource(JNIEnv* env, jobject resource) {
    if (!resource) return false;
    const std::string path = GetResourceLocationPath(env, resource);
    if (path.empty()) return true;
    return !IsCapeResourcePath(path);
}

jobject TryPlayerSkinMethod(JNIEnv* env, jobject player, jmethodID mid) {
    if (!env || !player || !mid) return nullptr;
    jobject resource = env->CallObjectMethod(player, mid);
    if (!resource || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (resource) env->DeleteLocalRef(resource);
        return nullptr;
    }
    if (!IsPlayerSkinResource(env, resource)) {
        env->DeleteLocalRef(resource);
        return nullptr;
    }
    return resource;
}

jobject ResolvePlayerSkinResource(JNIEnv* env, jobject player) {
    if (!env || !player || !g_jni.clsAbstractClientPlayer ||
        !env->IsInstanceOf(player, g_jni.clsAbstractClientPlayer)) {
        return nullptr;
    }

    if (g_jni.midGetPlayerInfo && g_jni.midNpiGetLocationSkin) {
        jobject playerInfo = env->CallObjectMethod(player, g_jni.midGetPlayerInfo);
        if (playerInfo && !env->ExceptionCheck()) {
            jobject skin = env->CallObjectMethod(playerInfo, g_jni.midNpiGetLocationSkin);
            env->DeleteLocalRef(playerInfo);
            if (skin && !env->ExceptionCheck() && IsPlayerSkinResource(env, skin)) {
                return skin;
            }
            env->ExceptionClear();
            if (skin) env->DeleteLocalRef(skin);
        } else {
            env->ExceptionClear();
            if (playerInfo) env->DeleteLocalRef(playerInfo);
        }
    }

    jobject skin = TryPlayerSkinMethod(env, player, g_jni.midGetLocationSkin);
    if (skin) return skin;

    if (g_jni.midGetLocationSkinAlt) {
        skin = TryPlayerSkinMethod(env, player, g_jni.midGetLocationSkinAlt);
        if (skin) return skin;
    }
    return nullptr;
}

void DrawPlayerHead(JNIEnv* env, jobject textureManager, jobject player, int x, int y, int size) {
    if (!player || !g_jni.midGuiDrawScaledCustomRect) return;
    if (!ResolveBindTexture(env, textureManager)) return;

    jobject skin = ResolvePlayerSkinResource(env, player);
    if (!skin) return;

    env->CallVoidMethod(textureManager, g_jni.midBindTexture, skin);
    env->DeleteLocalRef(skin);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    // Sample only the 8x8 face region (like tab overlay), scale up to desired size.
    const float headScale = (size > 0) ? ((float)size / 8.0f) : 1.0f;
    glPushMatrix();
    glTranslatef((float)x, (float)y, 0.0f);
    glScalef(headScale, headScale, 1.0f);
    env->CallStaticVoidMethod(g_jni.clsGui, g_jni.midGuiDrawScaledCustomRect,
        (jint)0, (jint)0, 8.0f, 8.0f, (jint)8, (jint)8, 64.0f, 64.0f);
    if (env->ExceptionCheck()) env->ExceptionClear();
    glPopMatrix();
}

struct TradeHudGlArgs {
    JNIEnv* env = nullptr;
    jobject fontRenderer = nullptr;
    int displayW = 0;
    int displayH = 0;
    int textX = 0;
    int textY = 0;
    int outcomeColor = 0;
    float textScale = 1.0f;
    char outcome[16]{};
};

static void TradeRestoreGlState() {
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
}

static void TradeRenderHudGl(const TradeHudGlArgs& args) {
    g_tradeCrashStage = "gl_push";
    glPushAttrib(kTradeGlAttribMask);
    glPushMatrix();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();

    g_tradeCrashStage = "gl_ortho";
    SetupTradeOrtho(args.displayW, args.displayH);

    g_tradeCrashStage = "gl_text_diff";
    DrawGuiTextScaled(args.env, args.fontRenderer, args.outcome,
        (float)args.textX, (float)args.textY, args.outcomeColor, args.textScale);

    g_tradeCrashStage = "gl_pop";
    TradeRestoreGlState();
    g_tradeCrashStage = "gl_done";
}

static void TradeRenderHudGlSafe(const TradeHudGlArgs& args) {
    __try {
        TradeRenderHudGl(args);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        TradeLogCrash(g_tradeCrashStage, GetExceptionCode(),
            "crash OpenGL/JNI pendant le dessin du HUD");
        __try {
            TradeRestoreGlState();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

} // namespace

void TradeIndicatorTick(JNIEnv* env, jobject mcObj, jclass mcClass, jclass customGuiClass) {
    (void)env;
    (void)mcObj;
    (void)mcClass;
    if (!customGuiClass) return;
    TradeResetLogFile();
}

void TradeIndicatorRender(JNIEnv* env) {
    g_tradeCrashStage = "entree";
    if (!env) return;
    if (!VigilIsHypixelServerActive()) return;
    if (!g_customGuiClassGlobal) return;
    const bool tradeOn = ReadGuiStaticBool(env, g_customGuiClassGlobal, "tradeIndicator", false);
    const bool hudEditMode = ReadGuiStaticBool(env, g_customGuiClassGlobal, "hudEditMode", false);
    if (!tradeOn && !hudEditMode) return;

    jclass mcClass = g_mcClassGlobal;
    bool ownsMcClass = false;
    g_tradeCrashStage = "resolve_mc_class";
    if (!mcClass) {
#ifdef VIGIL_LUNAR
        mcClass = McFindClass(env, "net/minecraft/client/Minecraft");
#else
        mcClass = env->FindClass("ave");
#endif
        ownsMcClass = mcClass != nullptr;
    }
    if (!mcClass || !ResolveTargetHudJni(env, mcClass)) {
        if (ownsMcClass && mcClass) env->DeleteLocalRef(mcClass);
        return;
    }

    g_tradeCrashStage = "get_minecraft";
    jmethodID midGetMc = g_midGetMinecraftStatic;
    if (!midGetMc) {
#ifdef VIGIL_LUNAR
        midGetMc = env->GetStaticMethodID(mcClass, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
#else
        midGetMc = env->GetStaticMethodID(mcClass, "A", "()Lave;");
#endif
    }
    if (!midGetMc || env->ExceptionCheck()) {
        TradeLogJniError(env, "getMinecraft");
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return;
    }

    jobject mcObj = env->CallStaticObjectMethod(mcClass, midGetMc);
    if (!mcObj || env->ExceptionCheck()) {
        TradeLogJniError(env, "instance Minecraft");
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return;
    }

    g_tradeCrashStage = "check_screen";
    if (g_jni.fidCurrentScreen && !hudEditMode) {
        jobject screen = env->GetObjectField(mcObj, g_jni.fidCurrentScreen);
        if (screen && !env->ExceptionCheck()) {
            env->DeleteLocalRef(screen);
            env->DeleteLocalRef(mcObj);
            if (ownsMcClass) env->DeleteLocalRef(mcClass);
            return;
        }
        env->ExceptionClear();
    }

    g_tradeCrashStage = "resolve_target";
    jclass targetClass = nullptr;
    const char* skipReason = "ok";
    jobject target = nullptr;
    if (!hudEditMode) {
        target = ResolveCrosshairLiving(env, mcObj, mcClass, targetClass, skipReason);
        if (!target) {
            env->DeleteLocalRef(mcObj);
            if (ownsMcClass) env->DeleteLocalRef(mcClass);
            return;
        }
    }

    g_tradeCrashStage = "read_health";
    float enemyHp = 20.0f;
    float selfHp = 20.0f;
    if (target) {
        enemyHp = env->CallFloatMethod(target, g_jni.midGetHealth);
        jobject self = env->GetObjectField(mcObj, g_jni.fidThePlayer);
        if (self && !env->ExceptionCheck()) {
            selfHp = env->CallFloatMethod(self, g_jni.midGetHealth);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                selfHp = 20.0f;
            }
        } else {
            env->ExceptionClear();
        }
        if (self) env->DeleteLocalRef(self);

        if (env->ExceptionCheck()) {
            TradeLogJniError(env, "lecture HP");
            if (targetClass) env->DeleteLocalRef(targetClass);
            env->DeleteLocalRef(target);
            env->DeleteLocalRef(mcObj);
            if (ownsMcClass) env->DeleteLocalRef(mcClass);
            return;
        }
    }

    g_tradeCrashStage = "read_diff";
    char outcome[16]{};
    int outcomeColor = 0xFFFFFF;
    if (hudEditMode && !target) {
        std::snprintf(outcome, sizeof(outcome), "TRADE");
        outcomeColor = 0x55FFFF;
    } else {
        FormatOutcomeLabel(selfHp, enemyHp, outcome, sizeof(outcome));
        outcomeColor = OutcomeColor(selfHp, enemyHp);
    }

    g_tradeCrashStage = "scaled_resolution";
    jobject scaledRes = env->NewObject(g_jni.clsScaledRes, g_jni.midScaledResCtor, mcObj);
    if (!scaledRes || env->ExceptionCheck()) {
        TradeLogJniError(env, "ScaledResolution");
        if (scaledRes) env->DeleteLocalRef(scaledRes);
        env->DeleteLocalRef(targetClass);
        env->DeleteLocalRef(target);
        env->DeleteLocalRef(mcObj);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return;
    }

    const int scaledW = env->CallIntMethod(scaledRes, g_jni.midScaledWidth);
    const int scaledH = env->CallIntMethod(scaledRes, g_jni.midScaledHeight);
    env->DeleteLocalRef(scaledRes);

    g_tradeCrashStage = "font";
    jobject fontRenderer = env->GetObjectField(mcObj, g_jni.fidFontRenderer);
    if (scaledW <= 0 || scaledH <= 0 || !fontRenderer || env->ExceptionCheck()) {
        TradeLogOnce("bad_layout", "FATAL: taille ecran ou font invalide");
        env->ExceptionClear();
        if (fontRenderer) env->DeleteLocalRef(fontRenderer);
        env->DeleteLocalRef(targetClass);
        env->DeleteLocalRef(target);
        env->DeleteLocalRef(mcObj);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return;
    }
    if (!ResolveFontDrawFromRenderer(env, fontRenderer)) {
        TradeLogOnce("font_fail", "FATAL: impossible de resoudre FontRenderer.drawString");
        env->DeleteLocalRef(fontRenderer);
        env->DeleteLocalRef(targetClass);
        env->DeleteLocalRef(target);
        env->DeleteLocalRef(mcObj);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return;
    }

    const int displayW = env->GetIntField(mcObj, g_jni.fidDisplayWidth);
    const int displayH = env->GetIntField(mcObj, g_jni.fidDisplayHeight);
    if (displayW <= 0 || displayH <= 0) {
        TradeLogOnce("bad_display", "FATAL: displayWidth/displayHeight invalides");
        env->DeleteLocalRef(fontRenderer);
        env->DeleteLocalRef(targetClass);
        env->DeleteLocalRef(target);
        env->DeleteLocalRef(mcObj);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return;
    }

    const float layoutScaleX = (float)displayW / (float)scaledW;
    const float layoutScaleY = (float)displayH / (float)scaledH;
    int posX = ReadGuiStaticInt(env, g_customGuiClassGlobal, "tradeIndicatorPosX", 520);
    int posY = ReadGuiStaticInt(env, g_customGuiClassGlobal, "tradeIndicatorPosY", 470);
    if (posX < 0) posX = 0;
    if (posX > 1000) posX = 1000;
    if (posY < 0) posY = 0;
    if (posY > 1000) posY = 1000;
    const int textX = (int)((posX / 1000.0f) * (float)scaledW * layoutScaleX);
    const int textY = (int)((posY / 1000.0f) * (float)scaledH * layoutScaleY);

    TradeHudGlArgs glArgs{};
    glArgs.env = env;
    glArgs.fontRenderer = fontRenderer;
    glArgs.displayW = displayW;
    glArgs.displayH = displayH;
    glArgs.textX = textX;
    glArgs.textY = textY;
    glArgs.outcomeColor = outcomeColor;
    glArgs.textScale = kHudTextScale;
    std::snprintf(glArgs.outcome, sizeof(glArgs.outcome), "%s", outcome);

    TradeRenderHudGlSafe(glArgs);

    env->DeleteLocalRef(fontRenderer);
    if (targetClass) env->DeleteLocalRef(targetClass);
    if (target) env->DeleteLocalRef(target);
    env->DeleteLocalRef(mcObj);
    if (ownsMcClass) env->DeleteLocalRef(mcClass);
    g_tradeCrashStage = "termine";
}
