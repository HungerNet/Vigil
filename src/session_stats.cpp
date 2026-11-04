#include "session_stats.h"

#ifdef FRENCHIFY_LUNAR
#include "../java_src/lunar_headers/FrenchifyIngameGuiLunar.h"
#include "../java_src/lunar_headers/FrenchifySessionHudLunar.h"
#include "../java_src/lunar_headers/FrenchifySessionHudInstallerLunar.h"
#include "class_finder.h"
#include "class_finder_log.h"
#else
#include "../java_src/FrenchifyIngameGui.h"
#include "../java_src/FrenchifySessionHud.h"
#include "../java_src/FrenchifySessionHudInstaller.h"
#endif
#include "globals.h"
#include "jni_helpers.h"
#include "tab_overlay.h"
#include "utils.h"

#include <GL/gl.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

static void PublishSessionHudState(JNIEnv* env);

namespace {

extern "C" JNIEXPORT void JNICALL Java_FrenchifyIngameGui_nativeSessionStatsTick(JNIEnv* env, jclass);
extern "C" JNIEXPORT void JNICALL Java_FrenchifySessionHud_nativePrepareDraw(JNIEnv* env, jclass);
extern "C" JNIEXPORT void JNICALL Java_FrenchifySessionHud_nativeDrawText(JNIEnv* env, jclass, jstring, jfloat, jfloat, jboolean);
extern "C" JNIEXPORT void JNICALL Java_FrenchifySessionHudInstaller_nativeInstallIngameGui(JNIEnv* env, jclass);

struct SessionStatsState {
    int finalKills = 0;
    int bedsBroken = 0;
    int kills = 0;
    int wins = 0;
    int winstreak = 0;
    int finalDeaths = 0;
    int bedsLost = 0;
    int deaths = 0;
    int losses = 0;
    int sessionGames = 0;
    int slumberTickets = 0;

    int gameFinals = 0;
    int gameBeds = 0;
    int gameKills = 0;
    bool gameWinCounted = false;
    bool gameLossCounted = false;

    ULONGLONG sessionStartMs = 0;
    ULONGLONG gameStartMs = 0;
    ULONGLONG gameEndMs = 0;
    ULONGLONG queueStartMs = 0;
    std::vector<ULONGLONG> gameDurationsMs;
    std::vector<ULONGLONG> queueDurationsMs;

    std::string playerName;
    std::string teamName;
    std::vector<std::string> rows;
    float startX = 0.0f;
    float endX = 0.0f;
    float startY = 0.0f;
    float endY = 0.0f;
    float baseWidth = 0.0f;
    float baseHeight = 0.0f;
    float lineHeight = 10.0f;
    float scale = 1.0f;
    int backgroundArgb = 0x64000000;
};

static SessionStatsState g_stats;
static SessionStatsSettings g_settings;
static std::atomic<bool> g_bedwarsContext{false};
static std::atomic<bool> g_sessionHudHookInstalled{false};
static std::atomic<bool> g_sessionHudClassesReady{false};
static std::atomic<bool> g_sessionHudInstallScheduled{false};
static jclass g_sessionHudHookClassGlobal = nullptr;
static jclass g_sessionHudInstallerClassGlobal = nullptr;
static jclass g_sessionHudJavaClassGlobal = nullptr;

static void SessionStatsLog(const char* msg) {
    FrenchifyFileLog("session", msg);
}

static void SessionCfLog(const char* msg) {
#ifdef FRENCHIFY_LUNAR
    if (msg) ClassFinderLogLine(msg);
#endif
    (void)msg;
}

static void SessionCfLogf(const char* fmt, ...) {
    if (!fmt) return;
    char buf[384];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    SessionCfLog(buf);
}

static void SessionStatsLogEvery(const char* key, ULONGLONG intervalMs, const char* msg) {
    static std::mutex mu;
    static std::unordered_map<std::string, ULONGLONG> lastAt;
    if (!msg) return;
    const ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> lk(mu);
    const std::string k = key ? key : msg;
    const auto it = lastAt.find(k);
    if (it != lastAt.end() && now - it->second < intervalMs) return;
    lastAt[k] = now;
    SessionStatsLog(msg);
}

static void SessionStatsLogOnce(const char* key, const char* msg) {
    static std::mutex mu;
    static std::unordered_set<std::string> seen;
    if (!msg) return;
    std::lock_guard<std::mutex> lk(mu);
    const std::string k = key ? key : msg;
    if (!seen.insert(k).second) return;
    SessionStatsLog(msg);
}

static const char* kStartMessages[] = {
    "Protect your bed and destroy the enemy beds.",
    "All generators are maxed! Your bed has three",
    "Collect Lucky Blocks from resource generators",
    "Select an ultimate in the store! They will",
    "Players swap teams at random intervals! Players",
    "Become a Soul Collector and trade in your"
};

static const char* kKillKeywords[] = {
    "against", "by", "fighting", "for", "from", "meet", "of", "seeing", "to", "was", "with"
};

struct HudRenderJni {
    jclass mcClass = nullptr;
    jclass clsScaledRes = nullptr;
    jmethodID midGetMc = nullptr;
    jmethodID midScaledCtor = nullptr;
    jmethodID midScaledWidth = nullptr;
    jmethodID midScaledHeight = nullptr;
    jfieldID fidDisplayWidth = nullptr;
    jfieldID fidDisplayHeight = nullptr;
    jfieldID fidFontRenderer = nullptr;
    jfieldID fidCurrentScreen = nullptr;
    jmethodID midFontWidth = nullptr;
    jmethodID midDrawStringShadow = nullptr;
    jmethodID midDrawString = nullptr;
    jmethodID midDrawStringFlag = nullptr;
    bool resolved = false;
    bool ok = false;
};

static HudRenderJni& HudJni() {
    static HudRenderJni cache;
    return cache;
}

static jclass LoadMcClassViaLoader(JNIEnv* env) {
    if (!env) return nullptr;
    jclass mcClass = env->FindClass("ave");
    if (mcClass && !env->ExceptionCheck()) return mcClass;
    env->ExceptionClear();
    if (!g_injectClassLoaderGlobal) return nullptr;

    jclass loaderClass = env->GetObjectClass(g_injectClassLoaderGlobal);
    jmethodID midLoadClass = loaderClass
        ? env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;")
        : nullptr;
    if (loaderClass) env->DeleteLocalRef(loaderClass);
    if (!midLoadClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }

    jstring jname = env->NewStringUTF("ave");
    jclass loaded = (jclass)env->CallObjectMethod(g_injectClassLoaderGlobal, midLoadClass, jname);
    env->DeleteLocalRef(jname);
    if (!loaded || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return loaded;
}

static jmethodID FindFontMethod(JNIEnv* env, jclass fontClass, const char* name,
                                const std::initializer_list<const char*>& sigs) {
    if (!env || !fontClass || !name) return nullptr;
    for (const char* sig : sigs) {
        jmethodID mid = env->GetMethodID(fontClass, name, sig);
        if (mid && !env->ExceptionCheck()) return mid;
        env->ExceptionClear();
    }
    return nullptr;
}

static jmethodID FindFontDrawMethod(JNIEnv* env, jclass fontClass, bool shadow) {
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
    return nullptr;
}

static jmethodID FindFontDrawWithShadowFlag(JNIEnv* env, jclass fontClass) {
    static const char* kSig = "(Ljava/lang/String;FFIZ)I";
    static const char* kNames[] = {"a", "b", "c", "d"};
    for (const char* name : kNames) {
        jmethodID mid = env->GetMethodID(fontClass, name, kSig);
        if (mid && !env->ExceptionCheck()) return mid;
        env->ExceptionClear();
    }
    return nullptr;
}

static bool ResolveHudJni(JNIEnv* env) {
    auto& cache = HudJni();
    if (cache.ok) return true;
    if (!env) return false;

    jclass mcClass = g_mcClassGlobal;
    bool ownsMcClass = false;
    if (!mcClass) {
#ifdef FRENCHIFY_LUNAR
        mcClass = McFindClass(env, "net/minecraft/client/Minecraft");
#else
        mcClass = LoadMcClassViaLoader(env);
#endif
        ownsMcClass = mcClass != nullptr;
    }
    if (!mcClass) {
        SessionStatsLogEvery("jni_mc_class", 4000, "HUD JNI FAIL: Minecraft class (ave)");
        SessionCfLog("session: HUD JNI FAIL Minecraft class");
        return false;
    }

    jmethodID midGetMc = g_midGetMinecraftStatic;
    if (!midGetMc) {
#ifdef FRENCHIFY_LUNAR
        midGetMc = env->GetStaticMethodID(mcClass, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
#else
        midGetMc = env->GetStaticMethodID(mcClass, "A", "()Lave;");
#endif
        if (!midGetMc || env->ExceptionCheck()) {
            env->ExceptionClear();
            if (ownsMcClass) env->DeleteLocalRef(mcClass);
            SessionStatsLogEvery("jni_get_mc", 4000, "HUD JNI FAIL: getMinecraft static method");
            SessionCfLog("session: HUD JNI FAIL getMinecraft");
            return false;
        }
    }

    jobject mcObj = env->CallStaticObjectMethod(mcClass, midGetMc);
    if (!mcObj || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        SessionStatsLogEvery("jni_mc_obj", 4000, "HUD JNI FAIL: Minecraft instance null");
        SessionCfLog("session: HUD JNI FAIL Minecraft instance null");
        return false;
    }

#ifdef FRENCHIFY_LUNAR
    jfieldID fidDisplayWidth = env->GetFieldID(mcClass, "displayWidth", "I");
    jfieldID fidDisplayHeight = env->GetFieldID(mcClass, "displayHeight", "I");
    jfieldID fidFontRenderer = env->GetFieldID(mcClass, "fontRendererObj", "Lnet/minecraft/client/gui/FontRenderer;");
    jfieldID fidCurrentScreen = env->GetFieldID(mcClass, "currentScreen", "Lnet/minecraft/client/gui/GuiScreen;");
#else
    jfieldID fidDisplayWidth = env->GetFieldID(mcClass, "d", "I");
    jfieldID fidDisplayHeight = env->GetFieldID(mcClass, "e", "I");
    jfieldID fidFontRenderer = env->GetFieldID(mcClass, "k", "Lavn;");
    jfieldID fidCurrentScreen = env->GetFieldID(mcClass, "m", "Laxu;");
#endif
    if (!fidDisplayWidth || !fidDisplayHeight || !fidFontRenderer || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(mcObj);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        SessionStatsLogEvery("jni_mc_fields", 4000, "HUD JNI FAIL: display/font fields on ave");
        SessionCfLog("session: HUD JNI FAIL Minecraft display/font fields");
        return false;
    }
    if (!fidCurrentScreen || env->ExceptionCheck()) {
        env->ExceptionClear();
        fidCurrentScreen = nullptr;
    }

#ifdef FRENCHIFY_LUNAR
    jclass scaledClass = McFindClass(env, "net/minecraft/client/gui/ScaledResolution");
#else
    jclass scaledClass = env->FindClass("avr");
#endif
    if (!scaledClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(mcObj);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        SessionStatsLogEvery("jni_scaled_class", 4000, "HUD JNI FAIL: ScaledResolution class");
        SessionCfLog("session: HUD JNI FAIL ScaledResolution class");
        return false;
    }
#ifdef FRENCHIFY_LUNAR
    jmethodID midScaledCtor = env->GetMethodID(scaledClass, "<init>", "(Lnet/minecraft/client/Minecraft;)V");
    jmethodID midScaledWidth = env->GetMethodID(scaledClass, "getScaledWidth", "()I");
    jmethodID midScaledHeight = env->GetMethodID(scaledClass, "getScaledHeight", "()I");
#else
    jmethodID midScaledCtor = env->GetMethodID(scaledClass, "<init>", "(Lave;)V");
    jmethodID midScaledWidth = env->GetMethodID(scaledClass, "a", "()I");
    jmethodID midScaledHeight = env->GetMethodID(scaledClass, "b", "()I");
#endif
    if (!midScaledCtor || !midScaledWidth || !midScaledHeight || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(scaledClass);
        env->DeleteLocalRef(mcObj);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        SessionStatsLogEvery("jni_scaled_methods", 4000, "HUD JNI FAIL: ScaledResolution methods");
        return false;
    }

    jobject fontRenderer = env->GetObjectField(mcObj, fidFontRenderer);
    if (!fontRenderer || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(mcObj);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        SessionStatsLogEvery("jni_font_obj", 4000, "HUD JNI FAIL: fontRenderer object null");
        return false;
    }

    jclass fontClass = env->GetObjectClass(fontRenderer);
    if (!fontClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(fontRenderer);
        env->DeleteLocalRef(mcObj);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        SessionStatsLogEvery("jni_font_class", 4000, "HUD JNI FAIL: fontRenderer class");
        return false;
    }

    jmethodID midFontWidth = nullptr;
    jmethodID midDrawStringShadow = nullptr;
    jmethodID midDrawString = nullptr;
    jmethodID midDrawStringFlag = nullptr;
#ifdef FRENCHIFY_LUNAR
    midFontWidth = env->GetMethodID(fontClass, "getStringWidth", "(Ljava/lang/String;)I");
    if (env->ExceptionCheck()) { env->ExceptionClear(); midFontWidth = nullptr; }
    midDrawStringShadow = env->GetMethodID(fontClass, "drawStringWithShadow", "(Ljava/lang/String;FFI)I");
    if (env->ExceptionCheck()) { env->ExceptionClear(); midDrawStringShadow = nullptr; }
    midDrawString = env->GetMethodID(fontClass, "drawString", "(Ljava/lang/String;FFI)I");
    if (env->ExceptionCheck()) { env->ExceptionClear(); midDrawString = nullptr; }
    midDrawStringFlag = env->GetMethodID(fontClass, "drawString", "(Ljava/lang/String;FFIZ)I");
    if (env->ExceptionCheck()) { env->ExceptionClear(); midDrawStringFlag = nullptr; }
    if (!midDrawString) midDrawString = midDrawStringShadow;
    if (!midFontWidth) SessionCfLog("session: MISS FontRenderer.getStringWidth");
    if (!midDrawStringShadow && !midDrawStringFlag) SessionCfLog("session: MISS FontRenderer.drawString*");
#else
    midFontWidth = FindFontMethod(env, fontClass, "a", {"(Ljava/lang/String;)I"});
    if (!midFontWidth) {
        midFontWidth = FindFontMethod(env, fontClass, "b", {"(Ljava/lang/String;)I"});
    }
    midDrawStringShadow = FindFontDrawMethod(env, fontClass, true);
    midDrawString = FindFontDrawMethod(env, fontClass, false);
    midDrawStringFlag = FindFontDrawWithShadowFlag(env, fontClass);
    if (!midDrawString) midDrawString = midDrawStringShadow;
#endif

    if (!midFontWidth || (!midDrawStringShadow && !midDrawStringFlag)) {
        env->DeleteLocalRef(fontClass);
        env->DeleteLocalRef(fontRenderer);
        env->DeleteLocalRef(mcObj);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        SessionStatsLogEvery("jni_font_methods", 4000, "HUD JNI FAIL: font width/draw methods");
        return false;
    }

    if (!cache.mcClass) {
        cache.mcClass = (jclass)env->NewGlobalRef(mcClass);
    }
    if (!cache.clsScaledRes) {
        cache.clsScaledRes = (jclass)env->NewGlobalRef(scaledClass);
    }
    env->DeleteLocalRef(scaledClass);
    cache.midGetMc = midGetMc;
    cache.midScaledCtor = midScaledCtor;
    cache.midScaledWidth = midScaledWidth;
    cache.midScaledHeight = midScaledHeight;
    cache.fidDisplayWidth = fidDisplayWidth;
    cache.fidDisplayHeight = fidDisplayHeight;
    cache.fidFontRenderer = fidFontRenderer;
    cache.fidCurrentScreen = fidCurrentScreen;
    cache.midFontWidth = midFontWidth;
    cache.midDrawStringShadow = midDrawStringShadow;
    cache.midDrawString = midDrawString;
    cache.midDrawStringFlag = midDrawStringFlag;
    cache.resolved = true;
    cache.ok = cache.mcClass != nullptr;
    env->DeleteLocalRef(fontClass);
    env->DeleteLocalRef(fontRenderer);
    env->DeleteLocalRef(mcObj);
    if (ownsMcClass) env->DeleteLocalRef(mcClass);

    if (cache.ok) {
        SessionStatsLogOnce("jni_ok", "HUD JNI resolved OK");
        SessionCfLog("session: HUD JNI resolved OK");
    } else {
        SessionCfLog("session: HUD JNI resolve FAILED");
    }
    return cache.ok;
}

static bool GetGuiScaledSize(JNIEnv* env, jobject mcObj, int& outW, int& outH) {
    if (!env || !mcObj || !ResolveHudJni(env)) return false;
    auto& cache = HudJni();
    if (!cache.clsScaledRes || !cache.midScaledCtor || !cache.midScaledWidth || !cache.midScaledHeight) {
        return false;
    }
    jobject scaled = env->NewObject(cache.clsScaledRes, cache.midScaledCtor, mcObj);
    if (!scaled || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    outW = env->CallIntMethod(scaled, cache.midScaledWidth);
    outH = env->CallIntMethod(scaled, cache.midScaledHeight);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(scaled);
        return false;
    }
    env->DeleteLocalRef(scaled);
    return outW > 0 && outH > 0;
}

static int MeasureMcString(JNIEnv* env, jobject fontRenderer, jmethodID midFontWidth, const std::string& text) {
    if (!env || !fontRenderer || !midFontWidth || text.empty()) return 0;
    jstring jText = env->NewStringUTF(text.c_str());
    if (!jText) return 0;
    jint w = env->CallIntMethod(fontRenderer, midFontWidth, jText);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        w = 0;
    }
    env->DeleteLocalRef(jText);
    return (int)w;
}

static void DrawMcString(JNIEnv* env, jobject fontRenderer, jmethodID midDraw,
                         const std::string& text, float x, float y, int color) {
    if (!env || !fontRenderer || !midDraw || text.empty()) return;
    jstring jText = env->NewStringUTF(text.c_str());
    if (!jText) return;
    env->CallIntMethod(fontRenderer, midDraw, jText, x, y, color);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(jText);
}

static void DrawMcStringScaled(JNIEnv* env, jobject fontRenderer, jmethodID midDraw,
                               const std::string& text, float x, float y, int color, float scale) {
    if (scale <= 0.0f) scale = 1.0f;
    if (std::fabs(scale - 1.0f) < 0.001f) {
        DrawMcString(env, fontRenderer, midDraw, text, x, y, color);
        return;
    }
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glScalef(scale, scale, 1.0f);
    DrawMcString(env, fontRenderer, midDraw, text, 0.0f, 0.0f, color);
    glPopMatrix();
}

static void ComputeHudLayout(JNIEnv* env, jobject fontRenderer,
                             int projW, int projH, float scaleX, float scaleY) {
    if (!env || !fontRenderer || projW <= 0 || projH <= 0) return;
    auto& cache = HudJni();
    static const float kBaseLineHeight = 11.0f;
    int longestString = 0;
    for (const std::string& row : g_stats.rows) {
        if (row == " ") continue;
        const int length = MeasureMcString(env, fontRenderer, cache.midFontWidth, row);
        if (length > longestString) longestString = length;
    }
    const float endXPx = (g_settings.posX / 1000.0f) * (float)projW;
    const float endYPx = (g_settings.posY / 1000.0f) * (float)projH;
    g_stats.baseWidth = (float)longestString + 4.0f;
    g_stats.baseHeight = (float)g_stats.rows.size() * kBaseLineHeight;
    g_stats.endX = endXPx * scaleX;
    g_stats.endY = endYPx * scaleY;
    const float scaledW = g_stats.baseWidth * g_stats.scale;
    const float scaledH = g_stats.baseHeight * g_stats.scale;
    g_stats.startX = g_stats.endX - scaledW * scaleX;
    g_stats.startY = g_stats.endY - scaledH * scaleY;
}

static void DrawFilledRect(float x, float y, float w, float h, GLubyte r, GLubyte g, GLubyte b, GLubyte a) {
    glColor4ub(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x, y + h);
    glVertex2f(x + w, y + h);
    glVertex2f(x + w, y);
    glEnd();
}

static jclass LoadInjectedClass(JNIEnv* env, jobject classLoader, const char* name,
                                const unsigned char* bytes, unsigned len) {
    if (!env || !name) return nullptr;
    jclass cls = classLoader
        ? env->DefineClass(name, classLoader, (const jbyte*)bytes, (jsize)len)
        : nullptr;
    if (env->ExceptionCheck() || !cls) {
        env->ExceptionClear();
        if (classLoader) {
            jclass loaderClass = env->GetObjectClass(classLoader);
            jmethodID midLoad = loaderClass
                ? env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;")
                : nullptr;
            if (loaderClass) env->DeleteLocalRef(loaderClass);
            if (midLoad) {
                jstring jname = env->NewStringUTF(name);
                cls = (jclass)env->CallObjectMethod(classLoader, midLoad, jname);
                env->DeleteLocalRef(jname);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    cls = nullptr;
                }
            }
        }
    }
    return cls;
}

static int ParseWinstreakArmorStandName(const std::string& plain) {
    static const char* kPrefix = "Current Winstreak: ";
    if (plain.rfind(kPrefix, 0) != 0) return -1;
    std::string digits;
    for (size_t i = std::strlen(kPrefix); i < plain.size(); ++i) {
        if (std::isdigit((unsigned char)plain[i])) digits += plain[i];
    }
    if (digits.empty()) return -1;
    return std::atoi(digits.c_str());
}

static void UpdateLobbyWinstreakFromArmorStands(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (!env || !mcObj || !mcClass || !g_settings.winstreak) return;
    if (g_tokensState != "Lobby") return;

    static ULONGLONG s_lastScanMs = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - s_lastScanMs < 1000) return;
    s_lastScanMs = now;

    static const char* kWorldSigs[] = {
        "Lnet/minecraft/client/multiplayer/WorldClient;",
        "Lnet/minecraft/world/WorldClient;",
        "Lbdb;",
        "Ladm;",
    };
    jfieldID fidWorld = JniResolveFieldIDFlex(env, mcClass, "theWorld", "f", kWorldSigs, 4);
    if (!fidWorld || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    jobject world = env->GetObjectField(mcObj, fidWorld);
    if (!world || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    jclass worldClass = env->GetObjectClass(world);
    jfieldID fidEntities = worldClass
        ? JniResolveFieldID(env, worldClass, "Ljava/util/List;", "loadedEntityList", "f")
        : nullptr;
    if (!fidEntities || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (worldClass) env->DeleteLocalRef(worldClass);
        env->DeleteLocalRef(world);
        return;
    }

    jobject entityList = env->GetObjectField(world, fidEntities);
    if (worldClass) env->DeleteLocalRef(worldClass);
    env->DeleteLocalRef(world);
    if (!entityList || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    jclass listClass = env->GetObjectClass(entityList);
    jmethodID midSize = listClass ? env->GetMethodID(listClass, "size", "()I") : nullptr;
    jmethodID midGet = listClass ? env->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;") : nullptr;
    if (listClass) env->DeleteLocalRef(listClass);
    if (!midSize || !midGet || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(entityList);
        return;
    }

    jclass armorStandClass = McFindClass(env, "net/minecraft/entity/item/EntityArmorStand");
    if (!armorStandClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        armorStandClass = env->FindClass("um");
    }
    if (!armorStandClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(entityList);
        return;
    }

    jclass entityClass = McFindClass(env, "net/minecraft/entity/Entity");
    if (!entityClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        entityClass = env->FindClass("pk");
    }
    jmethodID midGetName = entityClass
        ? JniResolveInstanceMethod(env, entityClass, "()Ljava/lang/String;", "getCustomNameTag", "aK")
        : nullptr;
    if (!midGetName && entityClass) {
        env->ExceptionClear();
        midGetName = JniResolveInstanceMethod(env, entityClass, "()Ljava/lang/String;", "getName", "e_");
    }
    if (!midGetName && entityClass) {
        env->ExceptionClear();
        midGetName = env->GetMethodID(entityClass, "getName", "()Ljava/lang/String;");
    }
    if (entityClass) env->DeleteLocalRef(entityClass);
    if (!midGetName || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(armorStandClass);
        env->DeleteLocalRef(entityList);
        return;
    }

    const int count = env->CallIntMethod(entityList, midSize);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(armorStandClass);
        env->DeleteLocalRef(entityList);
        return;
    }

    for (int i = 0; i < count; ++i) {
        jobject entity = env->CallObjectMethod(entityList, midGet, i);
        if (!entity || env->ExceptionCheck()) {
            env->ExceptionClear();
            continue;
        }
        if (!env->IsInstanceOf(entity, armorStandClass)) {
            env->DeleteLocalRef(entity);
            continue;
        }
        jstring jName = (jstring)env->CallObjectMethod(entity, midGetName);
        env->DeleteLocalRef(entity);
        if (!jName || env->ExceptionCheck()) {
            env->ExceptionClear();
            continue;
        }
        const char* chars = env->GetStringUTFChars(jName, nullptr);
        const std::string plain = StripMinecraftFormatting(chars ? chars : "");
        if (chars) env->ReleaseStringUTFChars(jName, chars);
        env->DeleteLocalRef(jName);

        const int parsed = ParseWinstreakArmorStandName(plain);
        if (parsed >= 0) {
            g_stats.winstreak = parsed;
            break;
        }
    }

    env->DeleteLocalRef(armorStandClass);
    env->DeleteLocalRef(entityList);
}

static std::string Colorize(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '&' && i + 1 < text.size()) {
            out += "\xC2\xA7";
            out += text[i + 1];
            ++i;
        } else {
            out += text[i];
        }
    }
    return out;
}

static std::string FormatDoubleStr(double val) {
    if (std::fabs(std::fmod(val, 1.0)) < 0.0001) {
        return std::to_string((int)val);
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", val);
    return buf;
}

static std::string FormatNumber(int number) {
    const int absoluteNumber = std::abs(number);
    if (absoluteNumber < 1000) return std::to_string(number);

    std::string numberStr = std::to_string(absoluteNumber);
    std::string formatted;
    int insertPosition = (int)numberStr.length() - 3;
    while (insertPosition > 0) {
        formatted = "," + numberStr.substr(insertPosition) + formatted;
        insertPosition -= 3;
    }
    formatted = numberStr.substr(0, insertPosition + 3) + formatted;
    if (number < 0) formatted = "-" + formatted;
    return formatted;
}

static std::string FormatDuration(long long timeSinceMs) {
    const long long hours = timeSinceMs / 3600000LL;
    const long long minutes = (timeSinceMs % 3600000LL) / 60000LL;
    const long long seconds = (timeSinceMs % 60000LL) / 1000LL;
    char buf[32];
    if (hours > 0) {
        std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld", hours, minutes, seconds);
    } else {
        std::snprintf(buf, sizeof(buf), "%02lld:%02lld", minutes, seconds);
    }
    return buf;
}

static std::string CapitalizeWord(const std::string& in) {
    if (in.empty()) return in;
    std::string out = in;
    out[0] = (char)std::toupper((unsigned char)out[0]);
    return out;
}

static bool MatchesAnyStartMessage(const std::string& msg) {
    for (const char* start : kStartMessages) {
        if (msg.find(start) != std::string::npos) return true;
    }
    return false;
}

static bool IsGameStartChatLine(const std::string& msg) {
    if (msg.empty()) return false;
    if (msg.find(':') != std::string::npos || msg.find('/') != std::string::npos) return false;
    if (!MatchesAnyStartMessage(msg)) return false;
    // Raven filters player chat with a leading space; also accept bare system lines.
    if (msg[0] == ' ') return true;
    return msg.rfind("Protect your bed", 0) == 0 ||
           msg.rfind("All generators", 0) == 0 ||
           msg.rfind("Collect Lucky", 0) == 0 ||
           msg.rfind("Select an ultimate", 0) == 0 ||
           msg.rfind("Players swap teams", 0) == 0 ||
           msg.rfind("Become a Soul", 0) == 0;
}

static bool ContainsCi(const std::string& haystack, const char* needle) {
    if (!needle || !needle[0]) return false;
    return LowerAscii(haystack).find(LowerAscii(needle)) != std::string::npos;
}

static bool IsTokenRewardLine(const std::string& msg) {
    return msg.find('+') != std::string::npos && ContainsCi(msg, "token");
}

static bool IsWinRewardLine(const std::string& msg) {
    if (ContainsCi(msg, "(win)") && IsTokenRewardLine(msg)) return true;
    const std::string trimmed = Trim(msg);
    if (trimmed == "Victory!" || trimmed.rfind("Victory!", 0) == 0) return true;
    if (ContainsCi(trimmed, "you won") && trimmed.find(':') == std::string::npos) return true;
    return false;
}

static bool IsBedDestroyedRewardLine(const std::string& msg) {
    return IsTokenRewardLine(msg) && ContainsCi(msg, "(bed destroyed)");
}

static bool IsFinalKillRewardLine(const std::string& msg) {
    return IsTokenRewardLine(msg) && ContainsCi(msg, "(final kill)");
}

static bool IsGameEndChatLine(const std::string& msg) {
    return msg.find("1st Killer") != std::string::npos;
}

static void RecordSessionGameEnd() {
    if (g_stats.gameStartMs == 0 || g_stats.gameEndMs != 0) return;
    g_stats.gameEndMs = GetTickCount64();
    g_stats.gameDurationsMs.push_back(g_stats.gameEndMs - g_stats.gameStartMs);
}

static bool DidPlayerGetKill(const std::string& playerName, const std::string& message) {
    if (playerName.empty()) return false;
    if (message.find("FINAL KILL!") != std::string::npos) return false;
    if (message.rfind("BED DESTRUCTION > ", 0) == 0) return false;
    const std::string msgLower = LowerAscii(message);
    const std::string nameLower = LowerAscii(playerName);
    for (const char* keyword : kKillKeywords) {
        const std::string needle = std::string(keyword) + " " + nameLower;
        if (msgLower.find(needle) != std::string::npos) return true;
    }
    return false;
}

static int ParseSlumberTicketAmount(const std::string& msg) {
    const std::string lower = LowerAscii(msg);
    if (lower.find("slumber ticket") == std::string::npos) return 0;

    size_t plus = msg.find('+');
    while (plus != std::string::npos) {
        size_t i = plus + 1;
        while (i < msg.size() && msg[i] == ' ') ++i;
        size_t start = i;
        while (i < msg.size() && std::isdigit((unsigned char)msg[i])) ++i;
        if (i > start) {
            const std::string digits = msg.substr(start, i - start);
            const size_t tail = lower.find(digits);
            if (tail != std::string::npos) {
                const size_t ticketPos = lower.find("slumber ticket", tail);
                if (ticketPos != std::string::npos && ticketPos - tail < 24) {
                    return std::atoi(digits.c_str());
                }
            }
        }
        plus = msg.find('+', plus + 1);
    }
    return 0;
}

static void ResetSessionStats() {
    g_stats = SessionStatsState{};
    g_stats.sessionStartMs = GetTickCount64();
}

static SessionStatsSettings ReadSettings(JNIEnv* env, jclass guiClass) {
    SessionStatsSettings cfg;
    if (!env || !guiClass) return cfg;

    cfg.enabled = ReadGuiStaticBool(env, guiClass, "sessionStats", false);
    cfg.gameStats = ReadGuiStaticBool(env, guiClass, "sessionStatsGame", true);
    cfg.finalKills = ReadGuiStaticBool(env, guiClass, "sessionStatsFinalKills", true);
    cfg.beds = ReadGuiStaticBool(env, guiClass, "sessionStatsBeds", true);
    cfg.kills = ReadGuiStaticBool(env, guiClass, "sessionStatsKills", true);
    cfg.wins = ReadGuiStaticBool(env, guiClass, "sessionStatsWins", true);
    cfg.winstreak = ReadGuiStaticBool(env, guiClass, "sessionStatsWinstreak", true);
    cfg.sessionGames = ReadGuiStaticBool(env, guiClass, "sessionStatsSessionGames", true);
    cfg.gameTime = ReadGuiStaticBool(env, guiClass, "sessionStatsGameTime", true);
    cfg.avgGameTime = ReadGuiStaticBool(env, guiClass, "sessionStatsAvgTime", true);
    cfg.sessionTime = ReadGuiStaticBool(env, guiClass, "sessionStatsSessionTime", true);
    cfg.queueEta = ReadGuiStaticBool(env, guiClass, "sessionStatsQueueEta", true);
    cfg.slumberTickets = ReadGuiStaticBool(env, guiClass, "sessionStatsSlumberTickets", true);
    cfg.textShadow = ReadGuiStaticBool(env, guiClass, "sessionStatsTextShadow", false);
    cfg.bgOpacity = ReadGuiStaticInt(env, guiClass, "sessionStatsBgOpacity", 100);
    cfg.scalePercent = ReadGuiStaticInt(env, guiClass, "sessionStatsScale", 100);
    cfg.posX = ReadGuiStaticInt(env, guiClass, "sessionStatsPosX", 1000);
    cfg.posY = ReadGuiStaticInt(env, guiClass, "sessionStatsPosY", 1000);
    cfg.resetPending = ReadGuiStaticBool(env, guiClass, "sessionStatsResetPending", false);
    if (cfg.bgOpacity < 0) cfg.bgOpacity = 0;
    if (cfg.bgOpacity > 255) cfg.bgOpacity = 255;
    if (cfg.scalePercent < 50) cfg.scalePercent = 50;
    if (cfg.scalePercent > 150) cfg.scalePercent = 150;
    if (cfg.posX < 0) cfg.posX = 0;
    if (cfg.posY < 0) cfg.posY = 0;
    if (cfg.posX > 1000) cfg.posX = 1000;
    if (cfg.posY > 1000) cfg.posY = 1000;
    return cfg;
}

static void ClearResetPendingFlag(JNIEnv* env, jclass guiClass) {
    if (!env || !guiClass) return;
    jfieldID fid = env->GetStaticFieldID(guiClass, "sessionStatsResetPending", "Z");
    if (!fid || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    env->SetStaticBooleanField(guiClass, fid, JNI_FALSE);
}

static std::string ReadLocalPlayerName(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (!env || !mcObj || !mcClass) return "";
    static const char* kPlayerSigs[] = {
        "Lnet/minecraft/client/entity/EntityPlayerSP;",
        "Lbew;",
    };
    jfieldID fidThePlayer = JniResolveFieldIDFlex(env, mcClass, "thePlayer", "h", kPlayerSigs, 2);
    if (!fidThePlayer || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }
    jobject thePlayer = env->GetObjectField(mcObj, fidThePlayer);
    if (!thePlayer || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }
    jclass playerClass = env->GetObjectClass(thePlayer);
    std::string out;

    // Prefer GameProfile (MCP on Lunar; obfuscated fallbacks for vanilla maps).
    jmethodID midProfile = playerClass
        ? JniResolveInstanceMethod(env, playerClass, "()Lcom/mojang/authlib/GameProfile;",
                                   "getGameProfile", "cd")
        : nullptr;
    if (!midProfile && playerClass) {
        env->ExceptionClear();
        midProfile = env->GetMethodID(playerClass, "cd", "()Lcom/mojang/authlib/GameProfile;");
    }
    if (midProfile && !env->ExceptionCheck()) {
        jobject profile = env->CallObjectMethod(thePlayer, midProfile);
        if (profile && !env->ExceptionCheck()) {
            jclass profileClass = env->GetObjectClass(profile);
            jmethodID midGetName = profileClass
                ? env->GetMethodID(profileClass, "getName", "()Ljava/lang/String;")
                : nullptr;
            if (midGetName) {
                jstring jName = (jstring)env->CallObjectMethod(profile, midGetName);
                if (!env->ExceptionCheck() && jName) {
                    const char* chars = env->GetStringUTFChars(jName, nullptr);
                    out = chars ? chars : "";
                    if (chars) env->ReleaseStringUTFChars(jName, chars);
                    env->DeleteLocalRef(jName);
                } else {
                    env->ExceptionClear();
                }
            } else {
                env->ExceptionClear();
            }
            if (profileClass) env->DeleteLocalRef(profileClass);
            env->DeleteLocalRef(profile);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    if (out.empty() && playerClass) {
        jmethodID midGetInfo = env->GetMethodID(playerClass, "b", "()Lbdc;");
        if (!midGetInfo || env->ExceptionCheck()) {
            env->ExceptionClear();
            midGetInfo = env->GetMethodID(playerClass, "getPlayerInfo",
                                          "()Lnet/minecraft/client/network/NetworkPlayerInfo;");
        }
        if (midGetInfo && !env->ExceptionCheck()) {
            jobject info = env->CallObjectMethod(thePlayer, midGetInfo);
            if (info && !env->ExceptionCheck()) {
                jclass infoClass = env->GetObjectClass(info);
                jmethodID midGetProfile = infoClass
                    ? JniResolveInstanceMethod(env, infoClass, "()Lcom/mojang/authlib/GameProfile;",
                                               "getGameProfile", "a")
                    : nullptr;
                if (midGetProfile && !env->ExceptionCheck()) {
                    jobject profile = env->CallObjectMethod(info, midGetProfile);
                    if (profile && !env->ExceptionCheck()) {
                        jclass profileClass = env->GetObjectClass(profile);
                        jmethodID midGetName = profileClass
                            ? env->GetMethodID(profileClass, "getName", "()Ljava/lang/String;")
                            : nullptr;
                        if (midGetName) {
                            jstring jName = (jstring)env->CallObjectMethod(profile, midGetName);
                            if (!env->ExceptionCheck() && jName) {
                                const char* chars = env->GetStringUTFChars(jName, nullptr);
                                out = chars ? chars : "";
                                if (chars) env->ReleaseStringUTFChars(jName, chars);
                                env->DeleteLocalRef(jName);
                            } else {
                                env->ExceptionClear();
                            }
                        } else {
                            env->ExceptionClear();
                        }
                        if (profileClass) env->DeleteLocalRef(profileClass);
                        env->DeleteLocalRef(profile);
                    } else {
                        env->ExceptionClear();
                    }
                } else {
                    env->ExceptionClear();
                }
                if (infoClass) env->DeleteLocalRef(infoClass);
                env->DeleteLocalRef(info);
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }
    }

    if (playerClass) env->DeleteLocalRef(playerClass);
    env->DeleteLocalRef(thePlayer);
    return out;
}

static bool FormattedStartsWithColorKey(const std::string& formatted, char colorKey) {
    const char key = (char)std::tolower((unsigned char)colorKey);
    if (formatted.size() >= 3 &&
        (unsigned char)formatted[0] == 0xC2 &&
        (unsigned char)formatted[1] == 0xA7 &&
        (char)std::tolower((unsigned char)formatted[2]) == key) {
        return true;
    }
    if (formatted.size() >= 2 &&
        (unsigned char)formatted[0] == 0xA7 &&
        (char)std::tolower((unsigned char)formatted[1]) == key) {
        return true;
    }
    return false;
}

static std::string BedwarsTeamNameFromRavenDisplayPrefix(const std::string& formatted) {
    static const struct { char key; const char* name; } kTeams[] = {
        {'c', "Red"}, {'9', "Blue"}, {'a', "Green"}, {'e', "Yellow"},
        {'b', "Aqua"}, {'f', "White"}, {'d', "Pink"}, {'8', "Gray"},
    };
    for (const auto& t : kTeams) {
        if (FormattedStartsWithColorKey(formatted, t.key)) return t.name;
    }
    return "";
}

static std::string TryReadTeamFromNetworkTab(JNIEnv* env, jobject mcObj, jclass mcClass,
                                             const std::string& localName) {
    if (!env || !mcObj || !mcClass || localName.empty()) return "";

    static const char* kPlayerSigs[] = {
        "Lnet/minecraft/client/entity/EntityPlayerSP;",
        "Lbew;",
    };
    jfieldID fidThePlayer = JniResolveFieldIDFlex(env, mcClass, "thePlayer", "h", kPlayerSigs, 2);
    if (!fidThePlayer || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }
    jobject thePlayer = env->GetObjectField(mcObj, fidThePlayer);
    if (!thePlayer || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }

    jclass playerClass = env->GetObjectClass(thePlayer);
    static const char* kSendQueueSigs[] = {
        "Lnet/minecraft/client/network/NetHandlerPlayClient;",
        "Lbcy;",
    };
    jfieldID fidSendQueue = playerClass
        ? JniResolveFieldIDFlex(env, playerClass, "sendQueue", "a", kSendQueueSigs, 2)
        : nullptr;
    if (!fidSendQueue || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (playerClass) env->DeleteLocalRef(playerClass);
        env->DeleteLocalRef(thePlayer);
        return "";
    }
    jobject sendQueue = env->GetObjectField(thePlayer, fidSendQueue);
    if (!sendQueue || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (playerClass) env->DeleteLocalRef(playerClass);
        env->DeleteLocalRef(thePlayer);
        return "";
    }

    jclass sendQueueClass = env->GetObjectClass(sendQueue);
    jmethodID midGetPlayerInfoMap = sendQueueClass
        ? JniResolveInstanceMethod(env, sendQueueClass, "()Ljava/util/Collection;",
                                   "getPlayerInfoMap", "d")
        : nullptr;
    if (!midGetPlayerInfoMap || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (sendQueueClass) env->DeleteLocalRef(sendQueueClass);
        env->DeleteLocalRef(sendQueue);
        if (playerClass) env->DeleteLocalRef(playerClass);
        env->DeleteLocalRef(thePlayer);
        return "";
    }
    jobject infoCollection = env->CallObjectMethod(sendQueue, midGetPlayerInfoMap);
    if (!infoCollection || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (sendQueueClass) env->DeleteLocalRef(sendQueueClass);
        env->DeleteLocalRef(sendQueue);
        if (playerClass) env->DeleteLocalRef(playerClass);
        env->DeleteLocalRef(thePlayer);
        return "";
    }

    jclass collectionClass = env->GetObjectClass(infoCollection);
    jmethodID midIterator = collectionClass ? env->GetMethodID(collectionClass, "iterator", "()Ljava/util/Iterator;") : nullptr;
    if (!midIterator || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (collectionClass) env->DeleteLocalRef(collectionClass);
        env->DeleteLocalRef(infoCollection);
        if (sendQueueClass) env->DeleteLocalRef(sendQueueClass);
        env->DeleteLocalRef(sendQueue);
        if (playerClass) env->DeleteLocalRef(playerClass);
        env->DeleteLocalRef(thePlayer);
        return "";
    }
    jobject iter = env->CallObjectMethod(infoCollection, midIterator);
    if (!iter || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (collectionClass) env->DeleteLocalRef(collectionClass);
        env->DeleteLocalRef(infoCollection);
        if (sendQueueClass) env->DeleteLocalRef(sendQueueClass);
        env->DeleteLocalRef(sendQueue);
        if (playerClass) env->DeleteLocalRef(playerClass);
        env->DeleteLocalRef(thePlayer);
        return "";
    }

    jclass iterClass = env->GetObjectClass(iter);
    jmethodID midHasNext = iterClass ? env->GetMethodID(iterClass, "hasNext", "()Z") : nullptr;
    jmethodID midNext = iterClass ? env->GetMethodID(iterClass, "next", "()Ljava/lang/Object;") : nullptr;
    jclass infoClass = McFindClass(env, "net/minecraft/client/network/NetworkPlayerInfo");
    if (!infoClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        infoClass = env->FindClass("bdc");
    }
    jmethodID midGetProfile = infoClass
        ? JniResolveInstanceMethod(env, infoClass, "()Lcom/mojang/authlib/GameProfile;",
                                   "getGameProfile", "a")
        : nullptr;
    jmethodID midGetDisplayName = infoClass
        ? JniResolveInstanceMethod(env, infoClass, "()Lnet/minecraft/util/IChatComponent;",
                                   "getDisplayName", "k")
        : nullptr;
    if ((!midGetDisplayName || env->ExceptionCheck()) && infoClass) {
        env->ExceptionClear();
        midGetDisplayName = env->GetMethodID(infoClass, "k", "()Leu;");
    }
    jclass compClass = McFindClass(env, "net/minecraft/util/IChatComponent");
    if (!compClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        compClass = env->FindClass("eu");
    }
    jmethodID midFmt = compClass
        ? JniResolveInstanceMethod(env, compClass, "()Ljava/lang/String;",
                                   "getFormattedText", "d")
        : nullptr;

    std::string teamFromTab;
    const std::string localLower = LowerAscii(localName);

    if (midHasNext && midNext && midGetProfile && midGetDisplayName && midFmt) {
        while (env->CallBooleanMethod(iter, midHasNext)) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                break;
            }
            jobject info = env->CallObjectMethod(iter, midNext);
            if (!info || env->ExceptionCheck()) {
                env->ExceptionClear();
                break;
            }

            jobject profile = env->CallObjectMethod(info, midGetProfile);
            if (profile && !env->ExceptionCheck()) {
                jclass profileClass = env->GetObjectClass(profile);
                jmethodID midGetName = profileClass
                    ? env->GetMethodID(profileClass, "getName", "()Ljava/lang/String;")
                    : nullptr;
                if (midGetName) {
                    jstring jName = (jstring)env->CallObjectMethod(profile, midGetName);
                    if (jName && !env->ExceptionCheck()) {
                        const char* p = env->GetStringUTFChars(jName, nullptr);
                        const std::string profileName = p ? p : "";
                        if (p) env->ReleaseStringUTFChars(jName, p);
                        env->DeleteLocalRef(jName);
                        if (LowerAscii(profileName) == localLower) {
                            jobject displayComp = env->CallObjectMethod(info, midGetDisplayName);
                            if (displayComp && !env->ExceptionCheck()) {
                                jstring jFmt = (jstring)env->CallObjectMethod(displayComp, midFmt);
                                if (jFmt && !env->ExceptionCheck()) {
                                    const char* fmt = env->GetStringUTFChars(jFmt, nullptr);
                                    const std::string formatted = fmt ? fmt : "";
                                    teamFromTab = BedwarsTeamNameFromRavenDisplayPrefix(formatted);
                                    if (teamFromTab.empty()) {
                                        teamFromTab = GetBedwarsTeamNameFromTabDisplay(formatted);
                                    }
                                    if (fmt) env->ReleaseStringUTFChars(jFmt, fmt);
                                    env->DeleteLocalRef(jFmt);
                                } else {
                                    env->ExceptionClear();
                                }
                                env->DeleteLocalRef(displayComp);
                            } else {
                                env->ExceptionClear();
                            }
                        }
                    } else {
                        env->ExceptionClear();
                    }
                } else {
                    env->ExceptionClear();
                }
                if (profileClass) env->DeleteLocalRef(profileClass);
                env->DeleteLocalRef(profile);
            } else {
                env->ExceptionClear();
            }
            env->DeleteLocalRef(info);
            if (!teamFromTab.empty()) break;
        }
    } else {
        env->ExceptionClear();
    }

    if (compClass) env->DeleteLocalRef(compClass);
    if (infoClass) env->DeleteLocalRef(infoClass);
    if (iterClass) env->DeleteLocalRef(iterClass);
    env->DeleteLocalRef(iter);
    if (collectionClass) env->DeleteLocalRef(collectionClass);
    env->DeleteLocalRef(infoCollection);
    if (sendQueueClass) env->DeleteLocalRef(sendQueueClass);
    env->DeleteLocalRef(sendQueue);
    if (playerClass) env->DeleteLocalRef(playerClass);
    env->DeleteLocalRef(thePlayer);
    return teamFromTab;
}

static bool TeamNamesEqualInsensitive(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    return _stricmp(a.c_str(), b.c_str()) == 0;
}

static bool ParseTeamEliminatedMessage(const std::string& msg, std::string& eliminatedTeam) {
    const std::string trimmed = Trim(msg);
    const std::string lower = LowerAscii(trimmed);
    const std::string needle = "team eliminated > ";
    const size_t pos = lower.find(needle);
    if (pos == std::string::npos) return false;
    eliminatedTeam = Trim(trimmed.substr(pos + needle.size()));
    const size_t sp = eliminatedTeam.find(' ');
    if (sp != std::string::npos) eliminatedTeam = eliminatedTeam.substr(0, sp);
    return !eliminatedTeam.empty();
}

static bool IsOurTeamEliminatedMessage(const std::string& msg) {
    std::string eliminatedTeam;
    if (!ParseTeamEliminatedMessage(msg, eliminatedTeam)) return false;
    if (TeamNamesEqualInsensitive(eliminatedTeam, g_stats.teamName)) return true;
    if (g_localTeamColorResolved && !g_localTeamLetter.empty()) {
        return TeamNamesEqualInsensitive(eliminatedTeam, BedwarsTeamNameFromTeamLetter(g_localTeamLetter));
    }
    return false;
}

static std::string ReadLocalTeamName(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (g_localTeamColorResolved && !g_localTeamLetter.empty()) {
        const std::string fromLetter = BedwarsTeamNameFromTeamLetter(g_localTeamLetter);
        if (!fromLetter.empty()) return fromLetter;
    }

    if (!env || !mcObj || !mcClass) return "";
    jfieldID fidThePlayer = env->GetFieldID(mcClass, "h", "Lbew;");
    if (!fidThePlayer || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }
    jobject thePlayer = env->GetObjectField(mcObj, fidThePlayer);
    if (!thePlayer || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }
    jclass playerClass = env->GetObjectClass(thePlayer);
    std::string teamFromTab;

    jmethodID midGetInfo = playerClass ? env->GetMethodID(playerClass, "b", "()Lbdc;") : nullptr;
    if (midGetInfo && !env->ExceptionCheck()) {
        jobject info = env->CallObjectMethod(thePlayer, midGetInfo);
        if (info && !env->ExceptionCheck()) {
            jclass infoClass = env->GetObjectClass(info);
            jmethodID midGetDisplayName = infoClass
                ? env->GetMethodID(infoClass, "k", "()Leu;")
                : nullptr;
            if (midGetDisplayName && !env->ExceptionCheck()) {
                jobject displayComp = env->CallObjectMethod(info, midGetDisplayName);
                if (displayComp && !env->ExceptionCheck()) {
                    jclass compClass = env->GetObjectClass(displayComp);
                    jmethodID midFmt = compClass
                        ? env->GetMethodID(compClass, "d", "()Ljava/lang/String;")
                        : nullptr;
                    if (compClass) env->DeleteLocalRef(compClass);
                    if (midFmt && !env->ExceptionCheck()) {
                        jstring jFmt = (jstring)env->CallObjectMethod(displayComp, midFmt);
                        if (jFmt && !env->ExceptionCheck()) {
                            const char* p = env->GetStringUTFChars(jFmt, nullptr);
                            const std::string formatted = p ? p : "";
                            teamFromTab = BedwarsTeamNameFromRavenDisplayPrefix(formatted);
                            if (teamFromTab.empty()) {
                                teamFromTab = GetBedwarsTeamNameFromTabDisplay(formatted);
                            }
                            if (p) env->ReleaseStringUTFChars(jFmt, p);
                            env->DeleteLocalRef(jFmt);
                        } else {
                            env->ExceptionClear();
                        }
                    } else {
                        env->ExceptionClear();
                    }
                    env->DeleteLocalRef(displayComp);
                } else {
                    env->ExceptionClear();
                }
            } else {
                env->ExceptionClear();
            }
            if (infoClass) env->DeleteLocalRef(infoClass);
            env->DeleteLocalRef(info);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    if (!teamFromTab.empty()) {
        if (playerClass) env->DeleteLocalRef(playerClass);
        env->DeleteLocalRef(thePlayer);
        return teamFromTab;
    }

    const std::string localName = ReadLocalPlayerName(env, mcObj, mcClass);
    teamFromTab = TryReadTeamFromNetworkTab(env, mcObj, mcClass, localName);
    if (!teamFromTab.empty()) {
        if (playerClass) env->DeleteLocalRef(playerClass);
        env->DeleteLocalRef(thePlayer);
        return teamFromTab;
    }

    const std::string colorName = GetEntityTeamColorName(env, mcObj, mcClass, thePlayer, playerClass);
    if (playerClass) env->DeleteLocalRef(playerClass);
    env->DeleteLocalRef(thePlayer);
    if (colorName == "light_purple") return "Pink";
    if (colorName == "dark_gray") return "Gray";
    return CapitalizeWord(colorName);
}

static void RefreshCachedTeamName(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (!env || !mcObj || !mcClass) return;
    if (g_redState != "Ingame" && !(g_stats.gameStartMs != 0 && g_stats.gameEndMs == 0)) return;

    const std::string liveTeam = ReadLocalTeamName(env, mcObj, mcClass);
    if (!liveTeam.empty()) {
        g_stats.teamName = liveTeam;
    }
}

static void ResetGameStats() {
    g_stats.gameFinals = 0;
    g_stats.gameBeds = 0;
    g_stats.gameKills = 0;
    g_stats.gameWinCounted = false;
    g_stats.gameLossCounted = false;
}

static void BuildRows(int fontHeight) {
    const std::string headerColor = "f";
    const std::string keyColor = "b";
    const std::string valueColor = "a";
    const std::string sessionString = Colorize("&" + headerColor + "&lSession  ");
    const std::string gameString = Colorize("&" + headerColor + "&lGame  ");

    const int tempFds = g_stats.finalDeaths == 0 ? 1 : g_stats.finalDeaths;
    const int tempBls = g_stats.bedsLost == 0 ? 1 : g_stats.bedsLost;
    const int tempDs = g_stats.deaths == 0 ? 1 : g_stats.deaths;
    const int tempLs = g_stats.losses == 0 ? 1 : g_stats.losses;

    std::vector<std::string> rows;
    bool anyGameStats = false;
    bool anySessionStats = false;
    bool sessionHeaderAdded = false;
    bool gap = false;

    if (g_settings.gameStats) {
        rows.push_back(gameString);
        rows.push_back(Colorize("&" + keyColor + "Finals: &" + valueColor + std::to_string(g_stats.gameFinals)));
        rows.push_back(Colorize("&" + keyColor + "Beds: &" + valueColor + std::to_string(g_stats.gameBeds)));
        rows.push_back(Colorize("&" + keyColor + "Kills: &" + valueColor + std::to_string(g_stats.gameKills)));
        anyGameStats = true;
    }

    auto addSessionHeader = [&]() {
        if (!sessionHeaderAdded) {
            rows.push_back(sessionString);
            sessionHeaderAdded = true;
            anySessionStats = true;
        }
    };

    if (g_settings.finalKills) {
        addSessionHeader();
        rows.push_back(Colorize("&" + keyColor + "Finals: &" + valueColor + FormatNumber(g_stats.finalKills) +
                               "&7 / &" + keyColor + "FKDR: &" + valueColor +
                               FormatDoubleStr((double)g_stats.finalKills / (double)tempFds)));
    }
    if (g_settings.beds) {
        addSessionHeader();
        rows.push_back(Colorize("&" + keyColor + "Beds: &" + valueColor + FormatNumber(g_stats.bedsBroken) +
                               "&7 / &" + keyColor + "BBLR: &" + valueColor +
                               FormatDoubleStr((double)g_stats.bedsBroken / (double)tempBls)));
    }
    if (g_settings.kills) {
        addSessionHeader();
        rows.push_back(Colorize("&" + keyColor + "Kills: &" + valueColor + FormatNumber(g_stats.kills) +
                               "&7 / &" + keyColor + "KDR: &" + valueColor +
                               FormatDoubleStr((double)g_stats.kills / (double)tempDs)));
    }
    if (g_settings.wins) {
        addSessionHeader();
        rows.push_back(Colorize("&" + keyColor + "Wins: &" + valueColor + FormatNumber(g_stats.wins) +
                               "&7 / &" + keyColor + "WLR: &" + valueColor +
                               FormatDoubleStr((double)g_stats.wins / (double)tempLs)));
    }

    if (g_settings.winstreak) {
        if (!gap && (anyGameStats || anySessionStats)) {
            rows.push_back(" ");
            gap = true;
        }
        rows.push_back(Colorize("&" + keyColor + "Winstreak: &" + valueColor + std::to_string(g_stats.winstreak)));
    }
    if (g_settings.sessionGames) {
        if (!gap && (anyGameStats || anySessionStats)) {
            rows.push_back(" ");
            gap = true;
        }
        rows.push_back(Colorize("&" + keyColor + "Session Games: &" + valueColor + std::to_string(g_stats.sessionGames)));
    }
    if (g_settings.slumberTickets) {
        if (!gap && (anyGameStats || anySessionStats)) {
            rows.push_back(" ");
            gap = true;
        }
        rows.push_back(Colorize("&" + keyColor + "Slumber Tickets: &" + valueColor + FormatNumber(g_stats.slumberTickets)));
    }
    if (g_settings.gameTime) {
        if (!gap && (anyGameStats || anySessionStats)) {
            rows.push_back(" ");
            gap = true;
        }
        std::string gameTime = "00:00";
        if (g_stats.gameStartMs != 0 || g_stats.gameEndMs != 0) {
            const ULONGLONG endTime = (g_stats.gameStartMs != 0 && g_stats.gameEndMs == 0)
                ? GetTickCount64()
                : g_stats.gameEndMs;
            gameTime = FormatDuration((long long)endTime - (long long)g_stats.gameStartMs);
        }
        rows.push_back(Colorize("&" + keyColor + "Game Time: &" + valueColor + gameTime));
    }
    if (g_settings.avgGameTime) {
        if (!gap && (anyGameStats || anySessionStats)) {
            rows.push_back(" ");
            gap = true;
        }
        long long totalGameTime = 0;
        int games = (int)g_stats.gameDurationsMs.size();
        for (long long t : g_stats.gameDurationsMs) totalGameTime += t;
        if (g_stats.gameStartMs != 0 && g_stats.gameEndMs == 0) {
            totalGameTime += (long long)(GetTickCount64() - g_stats.gameStartMs);
            games++;
        }
        const long long averageGameTime = games == 0 ? 0 : totalGameTime / games;
        const std::string displayTime = averageGameTime < 500 ? "00:00" : FormatDuration(averageGameTime);
        rows.push_back(Colorize("&" + keyColor + "Avg Time: &" + valueColor + displayTime));
    }
    if (g_settings.sessionTime) {
        if (!gap && (anyGameStats || anySessionStats)) {
            rows.push_back(" ");
            gap = true;
        }
        const ULONGLONG sessionStart = g_stats.sessionStartMs == 0 ? GetTickCount64() : g_stats.sessionStartMs;
        rows.push_back(Colorize("&" + keyColor + "Session Time: &" + valueColor +
                               FormatDuration((long long)(GetTickCount64() - sessionStart))));
    }
    if (g_settings.queueEta) {
        if (!gap && (anyGameStats || anySessionStats)) {
            rows.push_back(" ");
            gap = true;
        }
        long long totalQueue = 0;
        int queues = (int)g_stats.queueDurationsMs.size();
        for (ULONGLONG t : g_stats.queueDurationsMs) totalQueue += (long long)t;
        const bool inQueue = g_stats.queueStartMs != 0 && g_modeState == "Prequeue";
        long long elapsed = 0;
        if (inQueue) {
            elapsed = (long long)(GetTickCount64() - g_stats.queueStartMs);
            totalQueue += elapsed;
            queues++;
        }
        const long long avgQueue = queues == 0 ? 0 : totalQueue / queues;
        if (inQueue) {
            long long remaining = avgQueue - elapsed;
            if (remaining < 0) remaining = 0;
            if (avgQueue < 500) {
                rows.push_back(Colorize("&" + keyColor + "Queue: &" + valueColor + FormatDuration(elapsed) +
                                       " &7(ETA learning)"));
            } else {
                rows.push_back(Colorize("&" + keyColor + "Queue ETA: &" + valueColor + FormatDuration(remaining) +
                                       " &7(" + FormatDuration(elapsed) + ")"));
            }
        } else {
            const std::string display = avgQueue < 500 ? "--:--" : FormatDuration(avgQueue);
            rows.push_back(Colorize("&" + keyColor + "Queue ETA: &" + valueColor + display));
        }
    }

    g_stats.rows = std::move(rows);
    if (g_stats.rows.empty() && g_settings.enabled) {
        g_stats.rows.push_back(Colorize("&7Session Stats"));
    }
    g_stats.scale = g_settings.scalePercent / 100.0f;
    g_stats.lineHeight = (fontHeight + 2) * g_stats.scale;
    g_stats.endX = (float)g_settings.posX;
    g_stats.endY = (float)g_settings.posY;
    g_stats.startY = g_stats.endY - (g_stats.rows.size() * g_stats.lineHeight);
    g_stats.startX = g_stats.endX - 120.0f * g_stats.scale;
}

} // namespace

static void PublishSessionHudState(JNIEnv* env) {
    if (!env || !g_sessionHudJavaClassGlobal) return;

    jclass cls = g_sessionHudJavaClassGlobal;
    jfieldID fidEnabled = env->GetStaticFieldID(cls, "enabled", "Z");
    jfieldID fidBgOpacity = env->GetStaticFieldID(cls, "bgOpacity", "I");
    jfieldID fidTextShadow = env->GetStaticFieldID(cls, "textShadow", "Z");
    jfieldID fidScale = env->GetStaticFieldID(cls, "scale", "F");
    jfieldID fidStartX = env->GetStaticFieldID(cls, "startX", "F");
    jfieldID fidStartY = env->GetStaticFieldID(cls, "startY", "F");
    jfieldID fidEndX = env->GetStaticFieldID(cls, "endX", "F");
    jfieldID fidEndY = env->GetStaticFieldID(cls, "endY", "F");
    jfieldID fidBaseWidth = env->GetStaticFieldID(cls, "baseWidth", "F");
    jfieldID fidBaseHeight = env->GetStaticFieldID(cls, "baseHeight", "F");
    jfieldID fidRows = env->GetStaticFieldID(cls, "rows", "[Ljava/lang/String;");
    if (!fidEnabled || !fidBgOpacity || !fidTextShadow || !fidScale
        || !fidStartX || !fidStartY || !fidEndX || !fidEndY
        || !fidBaseWidth || !fidBaseHeight || !fidRows
        || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    // Hide completely off Hypixel, and only show when overlay mode is Bedwars.
    // HUD edit mode previews the panel on any Hypixel mode so it can be dragged.
    const bool bedwarsOverlay =
        taboverlay::GetOverlayActiveGamemode() == taboverlay::OverlayGamemode::Bedwars;
    const bool hudEditMode = g_customGuiClassGlobal
        ? ReadGuiStaticBool(env, g_customGuiClassGlobal, "hudEditMode", false)
        : false;
    const bool hudVisible =
        FrenchifyIsHypixelServerActive() &&
        ((g_settings.enabled && bedwarsOverlay) || hudEditMode);
    env->SetStaticBooleanField(cls, fidEnabled, hudVisible ? JNI_TRUE : JNI_FALSE);
    env->SetStaticIntField(cls, fidBgOpacity, (jint)g_settings.bgOpacity);
    env->SetStaticBooleanField(cls, fidTextShadow, g_settings.textShadow ? JNI_TRUE : JNI_FALSE);
    env->SetStaticFloatField(cls, fidScale, (jfloat)g_stats.scale);
    env->SetStaticFloatField(cls, fidStartX, (jfloat)g_stats.startX);
    env->SetStaticFloatField(cls, fidStartY, (jfloat)g_stats.startY);
    env->SetStaticFloatField(cls, fidEndX, (jfloat)g_stats.endX);
    env->SetStaticFloatField(cls, fidEndY, (jfloat)g_stats.endY);
    env->SetStaticFloatField(cls, fidBaseWidth, (jfloat)g_stats.baseWidth);
    env->SetStaticFloatField(cls, fidBaseHeight, (jfloat)g_stats.baseHeight);

    jclass stringClass = env->FindClass("java/lang/String");
    if (!stringClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    const jsize rowCount = hudVisible ? (jsize)g_stats.rows.size() : 0;
    jobjectArray arr = env->NewObjectArray(rowCount, stringClass, nullptr);
    env->DeleteLocalRef(stringClass);
    if (!arr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    for (jsize i = 0; i < rowCount; ++i) {
        jstring js = env->NewStringUTF(g_stats.rows[(size_t)i].c_str());
        if (js) {
            env->SetObjectArrayElement(arr, i, js);
            env->DeleteLocalRef(js);
        }
    }
    env->SetStaticObjectField(cls, fidRows, arr);
    env->DeleteLocalRef(arr);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

void SessionStatsSetBedwarsContext(bool active) {
    g_bedwarsContext.store(active, std::memory_order_relaxed);
}

bool SessionStatsGetHudRect(float& startX, float& startY, float& endX, float& endY) {
    if (g_stats.endX <= g_stats.startX || g_stats.endY <= g_stats.startY) return false;
    startX = g_stats.startX;
    startY = g_stats.startY;
    endX = g_stats.endX;
    endY = g_stats.endY;
    return true;
}

void SessionStatsApplyPosNormalized(int posX, int posY) {
    if (posX < 0) posX = 0;
    if (posX > 1000) posX = 1000;
    if (posY < 0) posY = 0;
    if (posY > 1000) posY = 1000;
    g_settings.posX = posX;
    g_settings.posY = posY;
}

bool SessionStatsGetGuiScaledSize(JNIEnv* env, jobject mcObj, int& outW, int& outH) {
    return GetGuiScaledSize(env, mcObj, outW, outH);
}

void SessionStatsOnChat(JNIEnv* env, jobject mcObj, jclass mcClass, const std::string& msgRaw) {
    if (g_customGuiClassGlobal) {
        g_settings = ReadSettings(env, g_customGuiClassGlobal);
    }
    if (!g_settings.enabled) return;

    const std::string msg = Trim(msgRaw);
    const bool activeGame = g_stats.gameStartMs != 0 && g_stats.gameEndMs == 0;
    const bool rewardLine =
        IsWinRewardLine(msg) || IsBedDestroyedRewardLine(msg) || IsFinalKillRewardLine(msg) ||
        IsTokenRewardLine(msg);

    if (msg.empty()) return;
    // Player chat / who lines often contain ':' or '/'. Keep reward lines even if extras leak in.
    if (!rewardLine && (msg.find(':') != std::string::npos || msg.find('/') != std::string::npos)) return;

    if (mcObj && mcClass) {
        const std::string liveName = ReadLocalPlayerName(env, mcObj, mcClass);
        if (!liveName.empty()) {
            g_stats.playerName = liveName;
        }
        if (g_redState == "Ingame" || activeGame) {
            RefreshCachedTeamName(env, mcObj, mcClass);
        }
    }

    if (IsGameStartChatLine(msg)) {
        g_stats.sessionGames++;
        g_stats.gameStartMs = GetTickCount64();
        g_stats.gameEndMs = 0;
        g_stats.gameFinals = 0;
        g_stats.gameBeds = 0;
        g_stats.gameKills = 0;
        g_stats.gameWinCounted = false;
        g_stats.gameLossCounted = false;
        g_stats.teamName.clear();
        return;
    }

    const bool ingame = (g_redState == "Ingame") || activeGame;
    const bool bwContext = g_bedwarsContext.load(std::memory_order_relaxed);
    // After victory screen / "1st Killer", redState and activeGame often drop before "+tokens (Win)".
    // Keep a post-game reward window while this game was started and not already lost.
    const bool postGameRewardWindow =
        g_stats.gameStartMs != 0 &&
        !g_stats.gameLossCounted &&
        (activeGame ||
         g_stats.gameEndMs == 0 ||
         (GetTickCount64() - g_stats.gameEndMs) <= 20000);

    if ((ingame || bwContext || activeGame || postGameRewardWindow) && IsOurTeamEliminatedMessage(msg)) {
        if (!g_stats.gameLossCounted) {
            g_stats.losses++;
            g_stats.winstreak = 0;
            g_stats.gameLossCounted = true;
            RecordSessionGameEnd();
        }
    }

    if (!bwContext && !activeGame && !postGameRewardWindow) {
        return;
    }

    const int slumberAmount = ParseSlumberTicketAmount(msg);
    if (slumberAmount > 0) {
        g_stats.slumberTickets += slumberAmount;
    }

    // Wins / beds / finals can arrive slightly after redState leaves Ingame (Lunar especially).
    if (!ingame && !postGameRewardWindow && !bwContext) return;

    if (IsWinRewardLine(msg)) {
        if (!g_stats.gameWinCounted && !g_stats.gameLossCounted && g_stats.gameStartMs != 0) {
            g_stats.wins++;
            g_stats.winstreak++;
            g_stats.gameWinCounted = true;
            if (g_stats.gameEndMs == 0) RecordSessionGameEnd();
        }
    } else if (IsBedDestroyedRewardLine(msg)) {
        if (ingame || activeGame || postGameRewardWindow) {
            g_stats.bedsBroken++;
            g_stats.gameBeds++;
        }
    } else if (IsFinalKillRewardLine(msg)) {
        if (ingame || activeGame || postGameRewardWindow) {
            g_stats.finalKills++;
            g_stats.gameFinals++;
        }
    } else if (msg.rfind("BED DESTRUCTION > Your Bed", 0) == 0) {
        g_stats.bedsLost++;
    } else if (DidPlayerGetKill(g_stats.playerName, msg)) {
        if (ingame || activeGame) {
            g_stats.kills++;
            g_stats.gameKills++;
        }
    } else if (msg == "You have respawned!") {
        g_stats.deaths++;
    } else if (msg == "You have been eliminated!") {
        g_stats.finalDeaths++;
    } else if (IsGameEndChatLine(msg)) {
        RecordSessionGameEnd();
    }
}

void SessionStatsTick(JNIEnv* env, jobject mcObj, jclass mcClass, jclass guiClass) {
    if (env && mcObj && mcClass) {
        FrenchifyUpdateHypixelServerWhitelist(env, mcObj, mcClass);
    }
    if (!FrenchifyIsHypixelServerActive()) {
        g_stats.rows.clear();
        PublishSessionHudState(env);
        return;
    }

    g_settings = ReadSettings(env, guiClass);
    const bool hudEditMode = ReadGuiStaticBool(env, guiClass, "hudEditMode", false);
    if (g_settings.resetPending) {
        ResetSessionStats();
        ClearResetPendingFlag(env, guiClass);
    }
    if (!g_settings.enabled && !hudEditMode) {
        PublishSessionHudState(env);
        return;
    }
    // Preview layout while editing even if the module is off.
    if (hudEditMode && !g_settings.enabled) {
        g_settings.enabled = true;
    }

    UpdateLobbyWinstreakFromArmorStands(env, mcObj, mcClass);

    if (g_stats.sessionStartMs == 0) {
        g_stats.sessionStartMs = GetTickCount64();
    }

    if (g_redState == "Ingame" || (g_stats.gameStartMs != 0 && g_stats.gameEndMs == 0)) {
        static ULONGLONG s_lastTeamRefreshMs = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - s_lastTeamRefreshMs >= 1000) {
            s_lastTeamRefreshMs = now;
            RefreshCachedTeamName(env, mcObj, mcClass);
        }

        const std::string liveName = ReadLocalPlayerName(env, mcObj, mcClass);
        if (!liveName.empty()) {
            g_stats.playerName = liveName;
        }
    }

    static ULONGLONG s_lastGameResetAt = 0;
    static std::string s_lastRedState;
    static std::string s_lastModeState;
    if (s_lastRedState != "Ingame" && g_redState == "Ingame") {
        ResetGameStats();
        s_lastGameResetAt = GetTickCount64();
    }
    s_lastRedState = g_redState;

    // Track queue wait times (Prequeue -> leave) for Queue ETA.
    {
        const bool nowPrequeue = (g_modeState == "Prequeue");
        const bool wasPrequeue = (s_lastModeState == "Prequeue");
        const ULONGLONG now = GetTickCount64();
        if (!wasPrequeue && nowPrequeue) {
            g_stats.queueStartMs = now;
        } else if (wasPrequeue && !nowPrequeue) {
            if (g_stats.queueStartMs != 0) {
                const ULONGLONG dur = now - g_stats.queueStartMs;
                if (dur >= 500 && dur < 30ULL * 60ULL * 1000ULL) {
                    g_stats.queueDurationsMs.push_back(dur);
                    if (g_stats.queueDurationsMs.size() > 40) {
                        g_stats.queueDurationsMs.erase(g_stats.queueDurationsMs.begin());
                    }
                }
                g_stats.queueStartMs = 0;
            }
        } else if (nowPrequeue && g_stats.queueStartMs == 0) {
            g_stats.queueStartMs = now;
        } else if (!nowPrequeue) {
            g_stats.queueStartMs = 0;
        }
        s_lastModeState = g_modeState;
    }
    (void)s_lastGameResetAt;

    int fontHeight = 9;
    BuildRows(fontHeight);

    if (ResolveHudJni(env) && !g_stats.rows.empty()) {
        auto& cache = HudJni();
        jobject mcObjLocal = env->CallStaticObjectMethod(cache.mcClass, cache.midGetMc);
        if (mcObjLocal && !env->ExceptionCheck()) {
            jobject fontRenderer = env->GetObjectField(mcObjLocal, cache.fidFontRenderer);
            if (fontRenderer && !env->ExceptionCheck()) {
                int scaledW = 0;
                int scaledH = 0;
                if (GetGuiScaledSize(env, mcObjLocal, scaledW, scaledH)) {
                    ComputeHudLayout(env, fontRenderer, scaledW, scaledH, 1.0f, 1.0f);
                }
                env->DeleteLocalRef(fontRenderer);
            } else {
                env->ExceptionClear();
            }
            env->DeleteLocalRef(mcObjLocal);
        } else {
            env->ExceptionClear();
        }
    }

    const int alpha = g_settings.bgOpacity;
    g_stats.backgroundArgb = (alpha << 24);

    PublishSessionHudState(env);
}

bool SessionStatsRender(JNIEnv* env) {
    if (!env) {
        SessionStatsLogEvery("render_no_env", 5000, "render skip: no JNIEnv");
        return false;
    }
    if (!FrenchifyIsHypixelServerActive()) return false;
    if (taboverlay::GetOverlayActiveGamemode() != taboverlay::OverlayGamemode::Bedwars) return false;
    if (g_customGuiClassGlobal) {
        g_settings = ReadSettings(env, g_customGuiClassGlobal);
    }
    if (!g_settings.enabled) {
        SessionStatsLogEvery("render_disabled", 8000, "render skip: sessionStats disabled");
        return false;
    }
    if (g_stats.rows.empty()) {
        SessionStatsLogEvery("render_empty_rows", 5000, "render skip: no HUD rows");
        return false;
    }
    if (!ResolveHudJni(env)) {
        SessionStatsLogEvery("render_jni", 5000, "render skip: HUD JNI unresolved");
        return false;
    }

    auto& cache = HudJni();
    jobject mcObj = env->CallStaticObjectMethod(cache.mcClass, cache.midGetMc);
    if (!mcObj || env->ExceptionCheck()) {
        env->ExceptionClear();
        SessionStatsLogEvery("render_mc", 5000, "render skip: Minecraft instance missing");
        return false;
    }

    const int displayW = env->GetIntField(mcObj, cache.fidDisplayWidth);
    const int displayH = env->GetIntField(mcObj, cache.fidDisplayHeight);
    jobject fontRenderer = env->GetObjectField(mcObj, cache.fidFontRenderer);
    if (displayW <= 0 || displayH <= 0 || !fontRenderer || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (fontRenderer) env->DeleteLocalRef(fontRenderer);
        env->DeleteLocalRef(mcObj);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "render skip: bad screen/font (%dx%d font=%d)",
                      displayW, displayH, fontRenderer ? 1 : 0);
        SessionStatsLogEvery("render_screen_size", 5000, buf);
        return false;
    }

    int projW = displayW;
    int projH = displayH;
    float layoutScaleX = 1.0f;
    float layoutScaleY = 1.0f;
    int scaledW = 0;
    int scaledH = 0;
    if (GetGuiScaledSize(env, mcObj, scaledW, scaledH) && scaledW > 0 && scaledH > 0) {
        layoutScaleX = (float)displayW / (float)scaledW;
        layoutScaleY = (float)displayH / (float)scaledH;
    }
    ComputeHudLayout(env, fontRenderer, scaledW > 0 ? scaledW : projW,
                     scaledH > 0 ? scaledH : projH, layoutScaleX, layoutScaleY);

    float startX = g_stats.startX;
    float startY = g_stats.startY;
    const float endX = g_stats.endX;
    const float endY = g_stats.endY;
    if (startX < 0.0f) startX = 0.0f;
    if (startY < 0.0f) startY = 0.0f;

    const GLubyte bgAlpha = (GLubyte)g_settings.bgOpacity;
    const float pad = 2.0f * g_stats.scale;
    const float lineHeight = g_stats.lineHeight * layoutScaleY;

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushMatrix();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)projW, (double)projH, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);

    DrawFilledRect(startX, startY, endX - startX, endY - startY, 0, 0, 0, bgAlpha);

    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    float y = startY + pad;
    for (const std::string& row : g_stats.rows) {
        if (row.empty()) {
            y += lineHeight;
            continue;
        }
        const int rowWidth = (int)(MeasureMcString(env, fontRenderer, cache.midFontWidth, row) * g_stats.scale);
        const float x = endX - (float)rowWidth * layoutScaleX - pad;
        DrawMcStringScaled(env, fontRenderer,
            g_settings.textShadow ? cache.midDrawStringShadow : cache.midDrawString,
            row, x, y, 0xFFFFFF, g_stats.scale * layoutScaleX);
        y += lineHeight;
    }

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();

    env->DeleteLocalRef(fontRenderer);
    env->DeleteLocalRef(mcObj);
    {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "render OK rows=%zu pos=%d,%d proj=%dx%d hook=%d",
                      g_stats.rows.size(), g_settings.posX, g_settings.posY, projW, projH,
                      g_sessionHudHookInstalled.load(std::memory_order_relaxed) ? 1 : 0);
        SessionStatsLogEvery("render_ok", 4000, buf);
    }
    return true;
}

bool IsSessionHudHookInstalled() {
    return g_sessionHudHookInstalled.load(std::memory_order_relaxed);
}

static bool EnsureSessionHudHookClasses(JNIEnv* env, jobject classLoader) {
    if (g_sessionHudClassesReady.load(std::memory_order_relaxed)) return true;
    if (!env) return false;
    if (!classLoader && g_injectClassLoaderGlobal) {
        classLoader = g_injectClassLoaderGlobal;
    }
    if (!classLoader) return false;

    jclass hookClass = LoadInjectedClass(env, classLoader, "FrenchifyIngameGui",
#ifdef FRENCHIFY_LUNAR
                                         FrenchifyIngameGuiLunar_class, FrenchifyIngameGuiLunar_class_len);
#else
                                         FrenchifyIngameGui_class, FrenchifyIngameGui_class_len);
#endif
    jclass installerClass = LoadInjectedClass(env, classLoader, "FrenchifySessionHudInstaller",
#ifdef FRENCHIFY_LUNAR
                                              FrenchifySessionHudInstallerLunar_class,
                                              FrenchifySessionHudInstallerLunar_class_len);
#else
                                              FrenchifySessionHudInstaller_class,
                                              FrenchifySessionHudInstaller_class_len);
#endif
    jclass hudClass = LoadInjectedClass(env, classLoader, "FrenchifySessionHud",
#ifdef FRENCHIFY_LUNAR
                                      FrenchifySessionHudLunar_class, FrenchifySessionHudLunar_class_len);
#else
                                      FrenchifySessionHud_class, FrenchifySessionHud_class_len);
#endif
    if (!hookClass || !installerClass || !hudClass) {
        SessionStatsLog("hook classes load FAILED (FrenchifyIngameGui / FrenchifySessionHud / installer)");
        SessionCfLogf("session: hook class load FAIL hook=%d installer=%d hud=%d",
            hookClass ? 1 : 0, installerClass ? 1 : 0, hudClass ? 1 : 0);
        if (hookClass) env->DeleteLocalRef(hookClass);
        if (installerClass) env->DeleteLocalRef(installerClass);
        if (hudClass) env->DeleteLocalRef(hudClass);
        return false;
    }

    JNINativeMethod hookMethods[] = {
        {(char*)"nativeSessionStatsTick", (char*)"()V",
         (void*)&Java_FrenchifyIngameGui_nativeSessionStatsTick},
    };
    JNINativeMethod installerMethods[] = {
        {(char*)"nativeInstallIngameGui", (char*)"()V",
         (void*)&Java_FrenchifySessionHudInstaller_nativeInstallIngameGui},
    };
    if (env->RegisterNatives(hookClass, hookMethods, 1) != JNI_OK || env->ExceptionCheck()) {
        SessionCfLog("session: RegisterNatives FrenchifyIngameGui FAILED");
        env->ExceptionClear();
        env->DeleteLocalRef(hookClass);
        env->DeleteLocalRef(installerClass);
        env->DeleteLocalRef(hudClass);
        return false;
    }
    if (env->RegisterNatives(installerClass, installerMethods, 1) != JNI_OK || env->ExceptionCheck()) {
        SessionCfLog("session: RegisterNatives FrenchifySessionHudInstaller FAILED");
        env->ExceptionClear();
        env->DeleteLocalRef(hookClass);
        env->DeleteLocalRef(installerClass);
        env->DeleteLocalRef(hudClass);
        return false;
    }

    JNINativeMethod hudMethods[] = {
        {(char*)"nativePrepareDraw", (char*)"()V",
         (void*)&Java_FrenchifySessionHud_nativePrepareDraw},
        {(char*)"nativeDrawText", (char*)"(Ljava/lang/String;FFZ)V",
         (void*)&Java_FrenchifySessionHud_nativeDrawText},
    };
    if (env->RegisterNatives(hudClass, hudMethods, 2) != JNI_OK || env->ExceptionCheck()) {
        env->ExceptionClear();
        SessionStatsLog("FrenchifySessionHud RegisterNatives FAILED");
        env->DeleteLocalRef(hookClass);
        env->DeleteLocalRef(installerClass);
        env->DeleteLocalRef(hudClass);
        return false;
    }

    g_sessionHudHookClassGlobal = (jclass)env->NewGlobalRef(hookClass);
    g_sessionHudInstallerClassGlobal = (jclass)env->NewGlobalRef(installerClass);
    g_sessionHudJavaClassGlobal = (jclass)env->NewGlobalRef(hudClass);
    env->DeleteLocalRef(hookClass);
    env->DeleteLocalRef(installerClass);
    env->DeleteLocalRef(hudClass);
    if (!g_sessionHudHookClassGlobal || !g_sessionHudInstallerClassGlobal || !g_sessionHudJavaClassGlobal) {
        return false;
    }

    g_sessionHudClassesReady.store(true, std::memory_order_relaxed);
    SessionStatsLogOnce("classes_ok", "hook classes loaded + natives registered");
    SessionCfLog("session: hook classes loaded + natives registered");
    return true;
}

static bool InstallSessionHudHookImpl(JNIEnv* env, jobject mcObj) {
    if (!env || !mcObj || !g_sessionHudHookClassGlobal || !g_sessionHudInstallerClassGlobal) {
        SessionCfLog("session: InstallSessionHudHookImpl missing env/gui classes");
        return false;
    }

    jclass mcClass = g_mcClassGlobal ? g_mcClassGlobal : env->GetObjectClass(mcObj);
    const bool ownsMcClass = (!g_mcClassGlobal && mcClass);
    if (!mcClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        SessionCfLog("session: InstallSessionHudHookImpl mcClass missing");
        return false;
    }

#ifdef FRENCHIFY_LUNAR
    jfieldID fidGui = env->GetFieldID(mcClass, "ingameGUI", "Lnet/minecraft/client/gui/GuiIngame;");
#else
    jfieldID fidGui = env->GetFieldID(mcClass, "q", "Lavo;");
#endif
    if (!fidGui || env->ExceptionCheck()) {
        env->ExceptionClear();
        SessionCfLog("session: MISS Minecraft.ingameGUI field");
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return false;
    }

    jobject oldGui = env->GetObjectField(mcObj, fidGui);
    if (!oldGui || env->ExceptionCheck()) {
        env->ExceptionClear();
        SessionCfLog("session: Minecraft.ingameGUI is null");
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return false;
    }

    if (env->IsInstanceOf(oldGui, g_sessionHudHookClassGlobal)) {
        env->DeleteLocalRef(oldGui);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        g_sessionHudHookInstalled.store(true, std::memory_order_relaxed);
        SessionStatsLogOnce("hook_already", "GuiIngame hook already installed");
        SessionCfLog("session: GuiIngame hook already installed");
        return true;
    }

#ifdef FRENCHIFY_LUNAR
    jmethodID ctor = env->GetMethodID(g_sessionHudHookClassGlobal, "<init>", "(Lnet/minecraft/client/Minecraft;)V");
#else
    jmethodID ctor = env->GetMethodID(g_sessionHudHookClassGlobal, "<init>", "(Lave;)V");
#endif
    if (!ctor || env->ExceptionCheck()) {
        env->ExceptionClear();
        SessionCfLog("session: MISS FrenchifyIngameGui.<init>");
        env->DeleteLocalRef(oldGui);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return false;
    }

    jobject newGui = env->NewObject(g_sessionHudHookClassGlobal, ctor, mcObj);
    if (!newGui || env->ExceptionCheck()) {
        env->ExceptionClear();
        SessionCfLog("session: FrenchifyIngameGui ctor FAILED");
        env->DeleteLocalRef(oldGui);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return false;
    }

#ifdef FRENCHIFY_LUNAR
    jmethodID midCopy = env->GetStaticMethodID(
        g_sessionHudInstallerClassGlobal, "copyGuiIngameFields",
        "(Lnet/minecraft/client/gui/GuiIngame;Lnet/minecraft/client/gui/GuiIngame;)V");
#else
    jmethodID midCopy = env->GetStaticMethodID(
        g_sessionHudInstallerClassGlobal, "copyAvoFields", "(Lavo;Lavo;)V");
#endif
    if (midCopy && !env->ExceptionCheck()) {
        env->CallStaticVoidMethod(g_sessionHudInstallerClassGlobal, midCopy, oldGui, newGui);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            SessionCfLog("session: copyGuiIngameFields threw");
            env->DeleteLocalRef(newGui);
            env->DeleteLocalRef(oldGui);
            if (ownsMcClass) env->DeleteLocalRef(mcClass);
            return false;
        }
    } else {
        env->ExceptionClear();
        SessionCfLog("session: MISS FrenchifySessionHudInstaller.copyGuiIngameFields");
    }

    env->SetObjectField(mcObj, fidGui, newGui);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        SessionCfLog("session: SetObjectField ingameGUI FAILED");
        env->DeleteLocalRef(newGui);
        env->DeleteLocalRef(oldGui);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return false;
    }

    env->DeleteLocalRef(newGui);
    env->DeleteLocalRef(oldGui);
    if (ownsMcClass) env->DeleteLocalRef(mcClass);

    g_sessionHudHookInstalled.store(true, std::memory_order_relaxed);
    SessionStatsLogOnce("hook_installed", "GuiIngame hook installed OK");
    SessionCfLog("session: GuiIngame hook installed OK");
    return true;
}

extern "C" JNIEXPORT void JNICALL Java_FrenchifyIngameGui_nativeSessionStatsTick(JNIEnv* env, jclass) {
    if (!env || !g_jvmGlobal) return;

#ifdef FRENCHIFY_LUNAR
    jclass mcClass = g_mcClassGlobal ? g_mcClassGlobal : McFindClass(env, "net/minecraft/client/Minecraft");
#else
    jclass mcClass = g_mcClassGlobal ? g_mcClassGlobal : env->FindClass("ave");
#endif
    const bool ownsMcClass = !g_mcClassGlobal;
    if (!mcClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    jmethodID midGetMc = g_midGetMinecraftStatic
        ? g_midGetMinecraftStatic
#ifdef FRENCHIFY_LUNAR
        : env->GetStaticMethodID(mcClass, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
#else
        : env->GetStaticMethodID(mcClass, "A", "()Lave;");
#endif
    if (!midGetMc || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return;
    }

    jobject mcObj = env->CallStaticObjectMethod(mcClass, midGetMc);
    if (!mcObj || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return;
    }

    SessionStatsTick(env, mcObj, mcClass, g_customGuiClassGlobal);

    env->DeleteLocalRef(mcObj);
    if (ownsMcClass) env->DeleteLocalRef(mcClass);
}

extern "C" JNIEXPORT void JNICALL Java_FrenchifySessionHud_nativePrepareDraw(JNIEnv* env, jclass) {
    taboverlay::RestoreVanillaGuiGlState(env);
}

extern "C" JNIEXPORT void JNICALL Java_FrenchifySessionHud_nativeDrawText(
    JNIEnv* env, jclass, jstring jRow, jfloat x, jfloat y, jboolean shadow) {
    if (!env || !jRow) return;
    if (!ResolveHudJni(env)) return;

    const char* utf = env->GetStringUTFChars(jRow, nullptr);
    if (!utf) return;
    const std::string row = utf;
    env->ReleaseStringUTFChars(jRow, utf);
    if (row.empty()) return;

    auto& cache = HudJni();
    jobject mcObj = env->CallStaticObjectMethod(cache.mcClass, cache.midGetMc);
    if (!mcObj || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    jobject fontRenderer = env->GetObjectField(mcObj, cache.fidFontRenderer);
    if (!fontRenderer || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(mcObj);
        return;
    }

    const bool useShadow = shadow == JNI_TRUE;
    if (cache.midDrawStringFlag) {
        jstring jText = env->NewStringUTF(row.c_str());
        if (jText) {
            env->CallIntMethod(fontRenderer, cache.midDrawStringFlag, jText, x, y, (jint)0xFFFFFF,
                               useShadow ? JNI_TRUE : JNI_FALSE);
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(jText);
        }
    } else {
        jmethodID mid = useShadow ? cache.midDrawStringShadow : cache.midDrawString;
        if (!mid) mid = cache.midDrawStringShadow;
        DrawMcString(env, fontRenderer, mid, row, (float)x, (float)y, 0xFFFFFF);
    }

    env->DeleteLocalRef(fontRenderer);
    env->DeleteLocalRef(mcObj);
}

extern "C" JNIEXPORT void JNICALL Java_FrenchifySessionHudInstaller_nativeInstallIngameGui(JNIEnv* env, jclass) {
    g_sessionHudInstallScheduled.store(false, std::memory_order_relaxed);
    if (!env || !g_mcClassGlobal || !g_midGetMinecraftStatic) return;

    jobject mcObj = env->CallStaticObjectMethod(g_mcClassGlobal, g_midGetMinecraftStatic);
    if (!mcObj || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    InstallSessionHudHookImpl(env, mcObj);
    if (g_sessionHudHookInstalled.load(std::memory_order_relaxed)) {
        SessionStatsLogOnce("install_ok", "nativeInstallIngameGui succeeded");
        SessionCfLog("session: nativeInstallIngameGui succeeded");
    } else {
        SessionStatsLog("nativeInstallIngameGui FAILED");
        SessionCfLog("session: nativeInstallIngameGui FAILED");
    }
    env->DeleteLocalRef(mcObj);
}

bool TryInstallSessionHudHook(JNIEnv* env, jobject mcObj, jobject classLoader) {
    if (g_sessionHudHookInstalled.load(std::memory_order_relaxed)) return true;
    if (!env || !mcObj) return false;

    if (!classLoader) {
        jclass objClass = env->FindClass("java/lang/Object");
        jclass classClass = env->FindClass("java/lang/Class");
        if (objClass && classClass && !env->ExceptionCheck()) {
            jmethodID midGetClass = env->GetMethodID(objClass, "getClass", "()Ljava/lang/Class;");
            jmethodID midGetClassLoader = env->GetMethodID(classClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
            if (midGetClass && midGetClassLoader) {
                jobject mcClsObj = env->CallObjectMethod(mcObj, midGetClass);
                if (!env->ExceptionCheck() && mcClsObj) {
                    classLoader = env->CallObjectMethod(mcClsObj, midGetClassLoader);
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        classLoader = nullptr;
                    }
                    env->DeleteLocalRef(mcClsObj);
                } else {
                    env->ExceptionClear();
                }
            }
        } else {
            env->ExceptionClear();
        }
        if (objClass) env->DeleteLocalRef(objClass);
        if (classClass) env->DeleteLocalRef(classClass);
    }
    if (!classLoader && g_injectClassLoaderGlobal) {
        classLoader = g_injectClassLoaderGlobal;
    }

    if (!EnsureSessionHudHookClasses(env, classLoader)) {
        SessionStatsLogEvery("install_no_classes", 5000, "TryInstallSessionHudHook: classes not ready");
        SessionCfLog("session: TryInstallSessionHudHook classes not ready");
        return false;
    }

    if (g_sessionHudInstallScheduled.load(std::memory_order_relaxed)) {
        return false;
    }

    jclass mcClass = g_mcClassGlobal ? g_mcClassGlobal : env->GetObjectClass(mcObj);
    const bool ownsMcClass = (!g_mcClassGlobal && mcClass);
    if (!mcClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        SessionCfLog("session: TryInstallSessionHudHook mcClass missing");
        return false;
    }

#ifdef FRENCHIFY_LUNAR
    jmethodID midSchedule = env->GetMethodID(
        mcClass, "addScheduledTask",
        "(Ljava/lang/Runnable;)Lcom/google/common/util/concurrent/ListenableFuture;");
#else
    jmethodID midSchedule = env->GetMethodID(
        mcClass, "a", "(Ljava/lang/Runnable;)Lcom/google/common/util/concurrent/ListenableFuture;");
#endif
    if (!midSchedule || env->ExceptionCheck()) {
        env->ExceptionClear();
        SessionCfLog("session: MISS Minecraft.addScheduledTask");
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return false;
    }

    jmethodID installerCtor = env->GetMethodID(g_sessionHudInstallerClassGlobal, "<init>", "()V");
    if (!installerCtor || env->ExceptionCheck()) {
        env->ExceptionClear();
        SessionCfLog("session: MISS FrenchifySessionHudInstaller.<init>");
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return false;
    }

    jobject installer = env->NewObject(g_sessionHudInstallerClassGlobal, installerCtor);
    if (!installer || env->ExceptionCheck()) {
        env->ExceptionClear();
        SessionCfLog("session: FrenchifySessionHudInstaller ctor FAILED");
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return false;
    }

    env->CallObjectMethod(mcObj, midSchedule, installer);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        SessionCfLog("session: addScheduledTask(installer) FAILED");
        env->DeleteLocalRef(installer);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return false;
    }

    env->DeleteLocalRef(installer);
    if (ownsMcClass) env->DeleteLocalRef(mcClass);
    g_sessionHudInstallScheduled.store(true, std::memory_order_relaxed);
    SessionStatsLogOnce("install_scheduled", "GuiIngame hook install scheduled on client thread");
    SessionCfLog("session: GuiIngame hook install scheduled on client thread");
    return true;
}
