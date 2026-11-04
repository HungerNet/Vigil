#include "colored_hitboxes.h"
#include "utils.h"
#include "globals.h"
#include "jni_helpers.h"
#ifdef VIGIL_LUNAR
#include "class_finder_log.h"
#include "class_finder.h"
#endif
#include "../java_src/lunar_headers/VigilRenderGlobalLunar.h"
#include "../java_src/lunar_headers/VigilHitboxInstallerLunar.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <GL/gl.h>

namespace {
// MWE realSizeHitbox: AxisAlignedBB.expand(getCollisionBorderSize()) on all axes.

static float ClampHitboxLineWidth(int thickness) {
    if (thickness < 1) return 1.0f;
    if (thickness > 10) return 10.0f;
    return static_cast<float>(thickness);
}

static float GetHitboxLineWidth(JNIEnv* env, jclass customGuiClass) {
    if (!env || !customGuiClass) return 1.0f;
    jfieldID fid = env->GetStaticFieldID(customGuiClass, "hitboxThickness", "I");
    if (!fid || env->ExceptionCheck()) {
        env->ExceptionClear();
        return 1.0f;
    }
    return ClampHitboxLineWidth(static_cast<int>(env->GetStaticIntField(customGuiClass, fid)));
}

static void HitboxBeginDrawPass(GLfloat lineWidth) {
    glPushAttrib(GL_ENABLE_BIT | GL_LINE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(lineWidth);

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

static void HitboxEndDrawPass() {
    glPopAttrib();
}
} // namespace

static jfieldID ResolveEntityFloatField(JNIEnv* env, jclass cls,
                                        const char* mcpName,
                                        const char* obfPrimary,
                                        const char* obfLegacy) {
    if (!env || !cls) return nullptr;
    if (obfLegacy && obfLegacy[0]) {
        jfieldID fid = env->GetFieldID(cls, obfLegacy, "F");
        if (fid && !env->ExceptionCheck()) return fid;
        env->ExceptionClear();
    }
    if (obfPrimary && obfPrimary[0]) {
        jfieldID fid = env->GetFieldID(cls, obfPrimary, "F");
        if (fid && !env->ExceptionCheck()) return fid;
        env->ExceptionClear();
    }
    if (mcpName && mcpName[0]) {
        jfieldID fid = env->GetFieldID(cls, mcpName, "F");
        if (fid && !env->ExceptionCheck()) return fid;
        env->ExceptionClear();
    }
    return nullptr;
}

static jmethodID ResolveEntityBoundingBoxMethod(JNIEnv* env, jclass cls) {
    if (!env || !cls) return nullptr;
    static const char* kNames[] = {"S", "aR", "getEntityBoundingBox", "getCollisionBoundingBox"};
    static const char* kSigs[] = {"()Lnet/minecraft/util/AxisAlignedBB;", "()Lnet/minecraft/util/AxisAlignedBB;",
        "()Lnet/minecraft/util/AxisAlignedBB;", "()Lnet/minecraft/util/AxisAlignedBB;"};
    for (int i = 0; i < 4; ++i) {
        jmethodID mid = env->GetMethodID(cls, kNames[i], kSigs[i]);
        if (mid && !env->ExceptionCheck()) return mid;
        env->ExceptionClear();
    }
    return nullptr;
}

static jmethodID ResolveDrawOutlinedMethod(JNIEnv* env, jclass rgClass) {
    if (!env || !rgClass) return nullptr;
    jmethodID mid = JniResolveStaticMethod(env, rgClass, "(Lnet/minecraft/util/AxisAlignedBB;IIII)V", nullptr, "a");
    if (mid) return mid;
    return JniResolveStaticMethod(env, rgClass,
        "(Lnet/minecraft/util/AxisAlignedBB;IIII)V", "drawOutlinedBoundingBox", "a");
}

struct HitboxJniCache {
    bool resolved = false;
    bool ok = false;

    jmethodID midGetRenderManager = nullptr;
    jmethodID midGetTimer = nullptr;
    jmethodID midGetEntityBb = nullptr;
    jmethodID midGetCollisionBorderSize = nullptr;
    jmethodID midEntityIsInvisible = nullptr;
    jmethodID midEntityIsInvisibleToPlayer = nullptr;
    jmethodID midRenderDebugBb = nullptr;
    jmethodID midAabbCtor = nullptr;
    jmethodID midDrawOutlined = nullptr;
    jmethodID midListSize = nullptr;
    jmethodID midListGet = nullptr;

    jclass clsAabb = nullptr;
    jclass clsRenderGlobal = nullptr;
    jclass clsEntityPlayer = nullptr;
    jclass clsEntityDragon = nullptr;
    jclass clsEntityFireball = nullptr;
    jmethodID midGetHealth = nullptr;
    jmethodID midGetMaxHealth = nullptr;

    jfieldID fidTimer = nullptr;
    jfieldID fidPartialTicks = nullptr;
    jfieldID fidWorld = nullptr;
    jfieldID fidRmWorldObj = nullptr;
    jfieldID fidEntityWorldObj = nullptr;
    jfieldID fidWorldPlayerList = nullptr;
    jfieldID fidWorldEntityList = nullptr;
    jfieldID fidSelf = nullptr;

    jfieldID fidRmViewerX = nullptr;
    jfieldID fidRmViewerY = nullptr;
    jfieldID fidRmViewerZ = nullptr;
    jfieldID fidRmViewYaw = nullptr;
    jfieldID fidRmViewPitch = nullptr;

    jfieldID fidPosX = nullptr;
    jfieldID fidPosY = nullptr;
    jfieldID fidPosZ = nullptr;
    jfieldID fidLastX = nullptr;
    jfieldID fidLastY = nullptr;
    jfieldID fidLastZ = nullptr;
    jfieldID fidYaw = nullptr;
    jfieldID fidPitch = nullptr;
    jfieldID fidWidth = nullptr;
    jfieldID fidHeight = nullptr;
    jfieldID fidEid = nullptr;
    jfieldID fidDisplayW = nullptr;
    jfieldID fidDisplayH = nullptr;

    bool useEntityCamera = false;
};

static HitboxJniCache g_hitbox;




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

static void DrawOutlinedRelativeAabb(JNIEnv* env, double minX, double minY, double minZ,
                                     double maxX, double maxY, double maxZ, int rgb, int alpha) {
    if (!g_hitbox.clsAabb || !g_hitbox.midAabbCtor || !g_hitbox.clsRenderGlobal || !g_hitbox.midDrawOutlined) {
        return;
    }
    jobject aabb = env->NewObject(g_hitbox.clsAabb, g_hitbox.midAabbCtor,
                                  minX, minY, minZ, maxX, maxY, maxZ);
    if (!aabb || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    env->CallStaticVoidMethod(g_hitbox.clsRenderGlobal, g_hitbox.midDrawOutlined,
                              aabb,
                              (jint)((rgb >> 16) & 0xFF),
                              (jint)((rgb >> 8) & 0xFF),
                              (jint)(rgb & 0xFF),
                              (jint)alpha);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(aabb);
}

static int McCodeToRgbHitbox(char code) {
    switch ((char)std::tolower((unsigned char)code)) {
        case '0': return 0x000000;
        case '1': return 0x0000AA;
        case '2': return 0x00AA00;
        case '3': return 0x00AAAA;
        case '4': return 0xAA0000;
        case '5': return 0xAA00AA;
        case '6': return 0xFFAA00;
        case '7': return 0xAAAAAA;
        case '8': return 0x555555;
        case '9': return 0x5555FF;
        case 'a': return 0x55FF55;
        case 'b': return 0x55FFFF;
        case 'c': return 0xFF5555;
        case 'd': return 0xFF55FF;
        case 'e': return 0xFFFF55;
        case 'f': return 0xFFFFFF;
        default: return 0xFF5555;
    }
}

static int LerpChannel(int a, int b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return a + (int)((b - a) * t + 0.5f);
}

static int LerpRgb(int a, int b, float t) {
    return (LerpChannel((a >> 16) & 0xFF, (b >> 16) & 0xFF, t) << 16) |
           (LerpChannel((a >> 8) & 0xFF, (b >> 8) & 0xFF, t) << 8) |
           LerpChannel(a & 0xFF, b & 0xFF, t);
}

static int HealthRatioToRgb(float ratio) {
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    if (ratio >= 0.66f) {
        return LerpRgb(0xFFFF55, 0x55FF55, (ratio - 0.66f) / 0.34f);
    }
    if (ratio >= 0.33f) {
        return LerpRgb(0xFFAA00, 0xFFFF55, (ratio - 0.33f) / 0.33f);
    }
    return LerpRgb(0xFF5555, 0xFFAA00, ratio / 0.33f);
}

static void DrawFilledRelativeSlab(double minX, double minY, double minZ,
                                   double maxX, double maxY, double maxZ, int rgb, float alpha) {
    const float r = ((rgb >> 16) & 0xFF) / 255.0f;
    const float g = ((rgb >> 8) & 0xFF) / 255.0f;
    const float b = (rgb & 0xFF) / 255.0f;
    glColor4f(r, g, b, alpha);
    glBegin(GL_QUADS);
    glVertex3d(maxX, minY, minZ); glVertex3d(maxX, maxY, minZ);
    glVertex3d(maxX, maxY, maxZ); glVertex3d(maxX, minY, maxZ);
    glVertex3d(minX, minY, maxZ); glVertex3d(minX, maxY, maxZ);
    glVertex3d(minX, maxY, minZ); glVertex3d(minX, minY, minZ);
    glVertex3d(minX, maxY, minZ); glVertex3d(minX, maxY, maxZ);
    glVertex3d(maxX, maxY, maxZ); glVertex3d(maxX, maxY, minZ);
    glVertex3d(minX, minY, maxZ); glVertex3d(minX, minY, minZ);
    glVertex3d(maxX, minY, minZ); glVertex3d(maxX, minY, maxZ);
    glVertex3d(minX, minY, maxZ); glVertex3d(maxX, minY, maxZ);
    glVertex3d(maxX, maxY, maxZ); glVertex3d(minX, maxY, maxZ);
    glVertex3d(maxX, minY, minZ); glVertex3d(minX, minY, minZ);
    glVertex3d(minX, maxY, minZ); glVertex3d(maxX, maxY, minZ);
    glEnd();
}

static void DrawDragonHealthBarBeside(JNIEnv* env, jobject entity,
                                      double relMinX, double relMinY, double relMinZ,
                                      double relMaxX, double relMaxY, double relMaxZ) {
    if (!env || !entity || !g_hitbox.midGetHealth) return;
    float hp = env->CallFloatMethod(entity, g_hitbox.midGetHealth);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    float maxHp = 200.0f;
    if (g_hitbox.midGetMaxHealth) {
        maxHp = env->CallFloatMethod(entity, g_hitbox.midGetMaxHealth);
        if (env->ExceptionCheck() || maxHp <= 0.01f) {
            env->ExceptionClear();
            maxHp = 200.0f;
        }
    }
    float ratio = hp / maxHp;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    const double fullH = relMaxY - relMinY;
    if (fullH <= 0.05) return;
    const double depth = relMaxZ - relMinZ;
    const double midZ = (relMinZ + relMaxZ) * 0.5;
    const double barDepth = (depth > 0.4) ? 0.35 : (depth * 0.6);
    const double gap = 0.25;
    const double thickness = 0.22;
    const double barMinX = relMaxX + gap;
    const double barMaxX = barMinX + thickness;
    const double barMinZ = midZ - barDepth * 0.5;
    const double barMaxZ = midZ + barDepth * 0.5;
    const double fillH = fullH * (double)ratio;

    glDisable(GL_CULL_FACE);
    DrawFilledRelativeSlab(barMinX, relMinY, barMinZ, barMaxX, relMaxY, barMaxZ, 0x222222, 0.55f);
    if (fillH > 0.01) {
        DrawFilledRelativeSlab(barMinX, relMinY, barMinZ, barMaxX, relMinY + fillH, barMaxZ,
                               HealthRatioToRgb(ratio), 0.90f);
    }
    glEnable(GL_CULL_FACE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

static bool& HitboxInstallScheduledFlag() {
    static bool s_scheduled = false;
    return s_scheduled;
}

static bool& HitboxHookClassesReadyFlag() {
    static bool s_ready = false;
    return s_ready;
}

static jclass g_hitboxHookClassGlobal = nullptr;
static jclass g_hitboxInstallerClassGlobal = nullptr;

static jfieldID TryInstanceField(JNIEnv* env, jclass cls, const char* name,
                                 const char* sig1, const char* sig2 = nullptr) {
    if (!cls || !name || !sig1) return nullptr;
    jfieldID fid = env->GetFieldID(cls, name, sig1);
    if (fid && !env->ExceptionCheck()) return fid;
    env->ExceptionClear();
    if (sig2) {
        fid = env->GetFieldID(cls, name, sig2);
        if (fid && !env->ExceptionCheck()) return fid;
        env->ExceptionClear();
    }
    return nullptr;
}

static jclass LoadClassViaMc(JNIEnv* env, jobject mcObj, const char* binaryName) {
    if (!env || !mcObj || !binaryName) return nullptr;
    jclass objClass = env->FindClass("java/lang/Object");
    jclass classClass = env->FindClass("java/lang/Class");
    jclass loaderClass = env->FindClass("java/lang/ClassLoader");
    if (!objClass || !classClass || !loaderClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    jmethodID midGetClass = env->GetMethodID(objClass, "getClass", "()Ljava/lang/Class;");
    jmethodID midGetLoader = env->GetMethodID(classClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jmethodID midLoadClass = env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    env->DeleteLocalRef(objClass);
    env->DeleteLocalRef(classClass);
    env->DeleteLocalRef(loaderClass);
    if (!midGetClass || !midGetLoader || !midLoadClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }

    jobject mcClassObj = env->CallObjectMethod(mcObj, midGetClass);
    if (!mcClassObj || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    jobject loader = env->CallObjectMethod(mcClassObj, midGetLoader);
    env->DeleteLocalRef(mcClassObj);
    if (!loader || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }

    jstring jname = env->NewStringUTF(binaryName);
    jobject loaded = env->CallObjectMethod(loader, midLoadClass, jname);
    env->DeleteLocalRef(jname);
    env->DeleteLocalRef(loader);
    if (!loaded || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return (jclass)loaded;
}

static jclass LoadHitboxEntityClass(JNIEnv* env, jobject mcObj, const char* obfName, const char* mcpSlash) {
#ifdef VIGIL_LUNAR
    if (mcpSlash) {
        jclass lunarCls = McFindClass(env, mcpSlash);
        if (lunarCls && !env->ExceptionCheck()) return lunarCls;
        env->ExceptionClear();
    }
#endif
    jclass cls = LoadClassViaMc(env, mcObj, obfName);
    if (!cls) cls = env->FindClass(obfName);
    if (cls && env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return cls;
}

static bool IsEnderDragonEntity(JNIEnv* env, jobject entity) {
    return env && entity && g_hitbox.clsEntityDragon &&
           env->IsInstanceOf(entity, g_hitbox.clsEntityDragon);
}

static bool IsFireballEntity(JNIEnv* env, jobject entity) {
    return env && entity && g_hitbox.clsEntityFireball &&
           env->IsInstanceOf(entity, g_hitbox.clsEntityFireball);
}

static jmethodID ResolveEntityIsInvisibleToPlayer(JNIEnv* env, jclass entityClass) {
    if (!env || !entityClass) return nullptr;
    static const struct {
        const char* name;
        const char* sig;
    } kCandidates[] = {
        {"isInvisibleToPlayer", "(Lnet/minecraft/entity/player/EntityPlayer;)Z"},
        {"f", "(Lnet/minecraft/entity/player/EntityPlayer;)Z"},
        {"isInvisibleToPlayer", "(Lnet/minecraft/entity/player/EntityPlayer;)Z"},
        {"f", "(Lnet/minecraft/entity/player/EntityPlayer;)Z"},
        {"f", "(Lnet/minecraft/client/entity/EntityPlayerSP;)Z"},
        {"f", "(Lnet/minecraft/client/entity/EntityOtherPlayerMP;)Z"},
    };
    for (const auto& c : kCandidates) {
        jmethodID mid = env->GetMethodID(entityClass, c.name, c.sig);
        if (mid && !env->ExceptionCheck()) return mid;
        env->ExceptionClear();
    }
    return nullptr;
}

static void ResolveEntityInvisibleMethods(JNIEnv* env, jobject mcObj, jclass entityClass) {
    jclass pkClass = LoadHitboxEntityClass(env, mcObj, "pk", "net/minecraft/entity/Entity");
    if (!pkClass && entityClass) {
        pkClass = entityClass;
    }
    if (!pkClass) return;

    if (!g_hitbox.midEntityIsInvisible) {
        g_hitbox.midEntityIsInvisible = JniResolveInstanceMethod(env, pkClass, "()Z", "isInvisible", "ax");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            g_hitbox.midEntityIsInvisible = nullptr;
        }
    }

    if (!g_hitbox.midEntityIsInvisibleToPlayer) {
        g_hitbox.midEntityIsInvisibleToPlayer = ResolveEntityIsInvisibleToPlayer(env, pkClass);
    }

    if (pkClass != entityClass) {
        env->DeleteLocalRef(pkClass);
    }
}

static bool ShouldSkipInvisibleHitbox(JNIEnv* env, jobject entity, jobject localPlayer) {
    if (!entity) return true;

    if (g_hitbox.midEntityIsInvisibleToPlayer && localPlayer) {
        const jboolean hidden =
            env->CallBooleanMethod(entity, g_hitbox.midEntityIsInvisibleToPlayer, localPlayer);
        if (!env->ExceptionCheck()) {
            return hidden == JNI_TRUE;
        }
        env->ExceptionClear();
    }

    if (g_hitbox.midEntityIsInvisible) {
        const jboolean inv = env->CallBooleanMethod(entity, g_hitbox.midEntityIsInvisible);
        if (!env->ExceptionCheck() && inv == JNI_TRUE) {
            return true;
        }
        env->ExceptionClear();
    }

    return false;
}

static std::string DragonLogClip(const std::string& s, size_t maxLen = 96) {
    if (s.size() <= maxLen) return s;
    return s.substr(0, maxLen) + "...";
}

static void DragonHitboxLog(const char* msg) {
    if (msg && msg[0]) VigilFileLog("dragon", msg);
}

static void DragonHitboxLogState(int entityId, const HitboxTeamColorInfo& info,
                                 const std::string& displayName, bool drawn, const char* reason) {
    static std::unordered_map<int, std::string> s_last;
    const int rgb = info.rgb & 0xFFFFFF;
    const char* src = info.source ? info.source : "?";
    const std::string colorName = TeamRgbToColorName(rgb);
    char buf[768];
    if (drawn) {
        std::snprintf(buf, sizeof(buf),
                      "eid=%d plain='%s' display='%s' prefix='%s' source=%s rgb=#%06X team=%s -> DRAW",
                      entityId,
                      DragonLogClip(info.playerName).c_str(),
                      DragonLogClip(displayName).c_str(),
                      DragonLogClip(info.prefix).c_str(),
                      src,
                      rgb,
                      colorName.empty() ? "?" : colorName.c_str());
    } else {
        std::snprintf(buf, sizeof(buf),
                      "eid=%d plain='%s' display='%s' prefix='%s' source=%s rgb=#%06X team=%s -> SKIP (%s)",
                      entityId,
                      DragonLogClip(info.playerName).c_str(),
                      DragonLogClip(displayName).c_str(),
                      DragonLogClip(info.prefix).c_str(),
                      src,
                      rgb,
                      colorName.empty() ? "?" : colorName.c_str(),
                      reason && reason[0] ? reason : "unknown");
    }

    std::string key = std::string(src) + "|" + std::to_string(rgb) + "|" +
                      info.prefix + "|" + displayName + "|" + (drawn ? "1" : "0");
    if (s_last[entityId] == key) return;
    s_last[entityId] = key;
    DragonHitboxLog(buf);
}

static bool ShouldRenderColoredHitboxEntity(JNIEnv* env, jobject entity, bool entityListMode,
                                            bool drawPlayers, bool drawDragons, bool drawFireballs) {
    if (IsEnderDragonEntity(env, entity)) {
        return drawDragons;
    }
    if (IsFireballEntity(env, entity)) {
        return drawFireballs;
    }
    if (!drawPlayers) return false;
    if (!entityListMode) return true;
    return g_hitbox.clsEntityPlayer && env->IsInstanceOf(entity, g_hitbox.clsEntityPlayer);
}

static void ResolveRmFieldsFromClass(JNIEnv* env, jclass rmClass) {
    if (!rmClass) return;

    // renderPosX/Y/Z (o,p,q) — used by entity/world render; viewerPos (h,i,j) is for chunk culling.
    const char* renderPosTriples[][3] = {{"o", "p", "q"}, {"h", "i", "j"}};
    for (const auto& t : renderPosTriples) {
        jfieldID fx = env->GetFieldID(rmClass, t[0], "D");
        jfieldID fy = env->GetFieldID(rmClass, t[1], "D");
        jfieldID fz = env->GetFieldID(rmClass, t[2], "D");
        if (fx && fy && fz && !env->ExceptionCheck()) {
            g_hitbox.fidRmViewerX = fx;
            g_hitbox.fidRmViewerY = fy;
            g_hitbox.fidRmViewerZ = fz;
            break;
        }
        env->ExceptionClear();
    }

    g_hitbox.fidRmViewYaw = env->GetFieldID(rmClass, "playerViewX", "F");
    if (!g_hitbox.fidRmViewYaw || env->ExceptionCheck()) {
        env->ExceptionClear();
        g_hitbox.fidRmViewYaw = nullptr;
    }
    // playerViewY is biu/e (g is GameSettings on 1.8.9 MCP)
    g_hitbox.fidRmViewPitch = env->GetFieldID(rmClass, "playerViewY", "F");
    if (!g_hitbox.fidRmViewPitch || env->ExceptionCheck()) {
        env->ExceptionClear();
        g_hitbox.fidRmViewPitch = nullptr;
    }

    if (!g_hitbox.fidRmWorldObj) {
        static const char* kRmWorldSigs[] = {"Lnet/minecraft/world/World;", "Lnet/minecraft/world/World;"};
        g_hitbox.fidRmWorldObj = JniResolveFieldIDFlex(env, rmClass, "worldObj", "b", kRmWorldSigs, 2);
    }

    g_hitbox.midRenderDebugBb = env->GetMethodID(rmClass, "renderDebugBoundingBox", "(Lnet/minecraft/entity/Entity;DDDFF)V");
    if (!g_hitbox.midRenderDebugBb || env->ExceptionCheck()) {
        env->ExceptionClear();
        g_hitbox.midRenderDebugBb = nullptr;
    }
}

static bool ResolveAabbClass(JNIEnv* env, jobject mcObj) {
    jclass aabbClass = LoadClassViaMc(env, mcObj, "net.minecraft.util.AxisAlignedBB");
    if (!aabbClass) aabbClass = McFindClass(env, "net/minecraft/util/AxisAlignedBB");
    if (!aabbClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    jmethodID ctor = env->GetMethodID(aabbClass, "<init>", "(DDDDDD)V");
    if (!ctor || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(aabbClass);
        return false;
    }
    g_hitbox.midAabbCtor = ctor;
    g_hitbox.clsAabb = (jclass)env->NewGlobalRef(aabbClass);
    env->DeleteLocalRef(aabbClass);
    return g_hitbox.clsAabb != nullptr;
}

static void ClearHitboxGlobalRefs(JNIEnv* env) {
    if (!env) return;
    if (g_hitbox.clsAabb) {
        env->DeleteGlobalRef(g_hitbox.clsAabb);
        g_hitbox.clsAabb = nullptr;
    }
    if (g_hitbox.clsRenderGlobal) {
        env->DeleteGlobalRef(g_hitbox.clsRenderGlobal);
        g_hitbox.clsRenderGlobal = nullptr;
    }
    if (g_hitbox.clsEntityPlayer) {
        env->DeleteGlobalRef(g_hitbox.clsEntityPlayer);
        g_hitbox.clsEntityPlayer = nullptr;
    }
    if (g_hitbox.clsEntityDragon) {
        env->DeleteGlobalRef(g_hitbox.clsEntityDragon);
        g_hitbox.clsEntityDragon = nullptr;
    }
    if (g_hitbox.clsEntityFireball) {
        env->DeleteGlobalRef(g_hitbox.clsEntityFireball);
        g_hitbox.clsEntityFireball = nullptr;
    }
    g_hitbox.midGetHealth = nullptr;
    g_hitbox.midGetMaxHealth = nullptr;
}


bool TryResolveHitboxJni(JNIEnv* env, jobject mcObj) {
    if (g_hitbox.ok) return true;

    static ULONGLONG s_lastTryAt = 0;
    const ULONGLONG now = GetTickCount64();
    if (s_lastTryAt && now - s_lastTryAt < 2000) return false;
    s_lastTryAt = now;

    if (!mcObj) {
        return false;
    }

    ClearHitboxGlobalRefs(env);
    g_hitbox = HitboxJniCache{};

    jclass mcClass = env->GetObjectClass(mcObj);
    if (!mcClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    g_hitbox.midGetRenderManager = env->GetMethodID(mcClass, "getRenderManager", "()Lnet/minecraft/client/renderer/entity/RenderManager;");
    if (!g_hitbox.midGetRenderManager || env->ExceptionCheck()) {
        env->ExceptionClear();
        g_hitbox.midGetRenderManager = nullptr;
    }

    g_hitbox.fidTimer = env->GetFieldID(mcClass, "timer", "Lnet/minecraft/util/Timer;");
    if (g_hitbox.fidTimer && !env->ExceptionCheck()) {
        jobject timer = env->GetObjectField(mcObj, g_hitbox.fidTimer);
        if (timer && !env->ExceptionCheck()) {
            jclass timerClass = env->GetObjectClass(timer);
            if (timerClass && !env->ExceptionCheck()) {
                g_hitbox.fidPartialTicks = env->GetFieldID(timerClass, "renderPartialTicks", "F");
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    g_hitbox.fidPartialTicks = nullptr;
                }
                env->DeleteLocalRef(timerClass);
            } else {
                env->ExceptionClear();
            }
            env->DeleteLocalRef(timer);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
        g_hitbox.fidTimer = nullptr;
    }

    jobject renderManager = nullptr;
    if (g_hitbox.midGetRenderManager) {
        renderManager = env->CallObjectMethod(mcObj, g_hitbox.midGetRenderManager);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            renderManager = nullptr;
        }
    }

    jclass rmClass = nullptr;
    if (renderManager) {
        rmClass = env->GetObjectClass(renderManager);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            rmClass = nullptr;
        }
    }
    if (!rmClass) {
        rmClass = LoadClassViaMc(env, mcObj, "net.minecraft.client.renderer.entity.RenderManager");
        if (!rmClass) rmClass = McFindClass(env, "net/minecraft/client/renderer/entity/RenderManager");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            rmClass = nullptr;
        }
    }
    if (rmClass) {
        ResolveRmFieldsFromClass(env, rmClass);
        env->DeleteLocalRef(rmClass);
    }

    static const char* kMcWorldSigs[] = {
        "Lnet/minecraft/client/multiplayer/WorldClient;",
        "Lnet/minecraft/world/World;",
        "Lnet/minecraft/client/multiplayer/WorldClient;",
        "Lnet/minecraft/world/World;",
    };
    g_hitbox.fidWorld = JniResolveFieldIDFlex(env, mcClass, "theWorld", "f", kMcWorldSigs, 4);
    g_hitbox.fidSelf = env->GetFieldID(mcClass, "thePlayer", "Lnet/minecraft/client/entity/EntityPlayerSP;");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        g_hitbox.fidSelf = nullptr;
    }
    g_hitbox.fidDisplayW = env->GetFieldID(mcClass, "displayWidth", "I");
    g_hitbox.fidDisplayH = env->GetFieldID(mcClass, "displayHeight", "I");
    if (env->ExceptionCheck()) env->ExceptionClear();

    jobject self = g_hitbox.fidSelf ? env->GetObjectField(mcObj, g_hitbox.fidSelf) : nullptr;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        self = nullptr;
    }

    jclass entityBase = LoadClassViaMc(env, mcObj, "net.minecraft.entity.Entity");
    if (!entityBase) entityBase = McFindClass(env, "net/minecraft/entity/Entity");
    if (entityBase && !env->ExceptionCheck()) {
        static const char* kWorldSigs[] = {"Lnet/minecraft/world/World;", "Lnet/minecraft/world/World;"};
        g_hitbox.fidEntityWorldObj = JniResolveFieldIDFlex(env, entityBase, "worldObj", "o", kWorldSigs, 2);
        env->DeleteLocalRef(entityBase);
    } else {
        env->ExceptionClear();
    }

    jobject world = nullptr;
    if (g_hitbox.fidWorld) {
        world = env->GetObjectField(mcObj, g_hitbox.fidWorld);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            world = nullptr;
        }
    }
    if (!world && g_hitbox.fidRmWorldObj && renderManager) {
        world = env->GetObjectField(renderManager, g_hitbox.fidRmWorldObj);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            world = nullptr;
        }
    }
    if (!world && g_hitbox.fidEntityWorldObj && self) {
        world = env->GetObjectField(self, g_hitbox.fidEntityWorldObj);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            world = nullptr;
        }
    }

    if (world) {
        jclass worldClass = env->GetObjectClass(world);
        if (worldClass && !env->ExceptionCheck()) {
            g_hitbox.fidWorldPlayerList = JniResolveFieldID(env, worldClass, "Ljava/util/List;", "playerEntities", "j");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                g_hitbox.fidWorldPlayerList = nullptr;
            }
            g_hitbox.fidWorldEntityList = JniResolveFieldID(env, worldClass, "Ljava/util/List;", "loadedEntityList", "f");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                g_hitbox.fidWorldEntityList = nullptr;
            }
            env->DeleteLocalRef(worldClass);
        } else {
            env->ExceptionClear();
        }
    }
    if (world) env->DeleteLocalRef(world);

    jobject probeEntity = self;
    jobject playerList = nullptr;
    if (!probeEntity) {
        jfieldID fidList = g_hitbox.fidWorldPlayerList;
        jobject w2 = g_hitbox.fidWorld ? env->GetObjectField(mcObj, g_hitbox.fidWorld) : nullptr;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            w2 = nullptr;
        }
        if (w2 && fidList) {
            playerList = env->GetObjectField(w2, fidList);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                playerList = nullptr;
            }
        }
        if (w2) env->DeleteLocalRef(w2);
        if (playerList) {
            jclass listClass = env->GetObjectClass(playerList);
            jmethodID midSize = listClass ? env->GetMethodID(listClass, "size", "()I") : nullptr;
            jmethodID midGet = listClass ? env->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;") : nullptr;
            if (listClass) env->DeleteLocalRef(listClass);
            if (midSize && midGet && !env->ExceptionCheck()) {
                const int n = env->CallIntMethod(playerList, midSize);
                if (!env->ExceptionCheck() && n > 0) {
                    probeEntity = env->CallObjectMethod(playerList, midGet, 0);
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        probeEntity = nullptr;
                    }
                } else {
                    env->ExceptionClear();
                }
            } else {
                env->ExceptionClear();
            }
        }
    }

    if (!probeEntity && self) probeEntity = self;

    if (!g_hitbox.fidPosX) {
        jclass pkClass = LoadClassViaMc(env, mcObj, "net.minecraft.entity.Entity");
        if (!pkClass) pkClass = McFindClass(env, "net/minecraft/entity/Entity");
        if (pkClass && !env->ExceptionCheck()) {
            g_hitbox.fidPosX = env->GetFieldID(pkClass, "posX", "D");
            g_hitbox.fidPosY = env->GetFieldID(pkClass, "posY", "D");
            g_hitbox.fidPosZ = env->GetFieldID(pkClass, "posZ", "D");
            g_hitbox.fidLastX = env->GetFieldID(pkClass, "lastTickPosX", "D");
            g_hitbox.fidLastY = env->GetFieldID(pkClass, "lastTickPosY", "D");
            g_hitbox.fidLastZ = env->GetFieldID(pkClass, "lastTickPosZ", "D");
            g_hitbox.fidYaw = env->GetFieldID(pkClass, "rotationYaw", "F");
            g_hitbox.fidPitch = env->GetFieldID(pkClass, "rotationPitch", "F");
            g_hitbox.fidWidth = ResolveEntityFloatField(env, pkClass, "width", "J", "l");
            g_hitbox.fidHeight = ResolveEntityFloatField(env, pkClass, "height", "K", "m");
            g_hitbox.fidEid = env->GetFieldID(pkClass, "entityId", "I");
            g_hitbox.midGetEntityBb = ResolveEntityBoundingBoxMethod(env, pkClass);
            g_hitbox.midGetCollisionBorderSize = JniResolveInstanceMethod(
                env, pkClass, "()F", "getCollisionBorderSize", "ao");
            if (env->ExceptionCheck()) env->ExceptionClear();
            ResolveEntityInvisibleMethods(env, mcObj, pkClass);
            env->DeleteLocalRef(pkClass);
        } else {
            env->ExceptionClear();
        }
    }

    if (probeEntity) {
        jclass entityClass = env->GetObjectClass(probeEntity);
        if (entityClass && !env->ExceptionCheck()) {
            g_hitbox.fidPosX = env->GetFieldID(entityClass, "posX", "D");
            g_hitbox.fidPosY = env->GetFieldID(entityClass, "posY", "D");
            g_hitbox.fidPosZ = env->GetFieldID(entityClass, "posZ", "D");
            g_hitbox.fidLastX = env->GetFieldID(entityClass, "lastTickPosX", "D");
            g_hitbox.fidLastY = env->GetFieldID(entityClass, "lastTickPosY", "D");
            g_hitbox.fidLastZ = env->GetFieldID(entityClass, "lastTickPosZ", "D");
            g_hitbox.fidYaw = env->GetFieldID(entityClass, "rotationYaw", "F");
            g_hitbox.fidPitch = env->GetFieldID(entityClass, "rotationPitch", "F");
            g_hitbox.fidWidth = ResolveEntityFloatField(env, entityClass, "width", "J", "l");
            g_hitbox.fidHeight = ResolveEntityFloatField(env, entityClass, "height", "K", "m");
            g_hitbox.fidEid = env->GetFieldID(entityClass, "entityId", "I");
            g_hitbox.midGetEntityBb = ResolveEntityBoundingBoxMethod(env, entityClass);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                g_hitbox.midGetEntityBb = nullptr;
            }
            if (!g_hitbox.midGetCollisionBorderSize) {
                g_hitbox.midGetCollisionBorderSize = JniResolveInstanceMethod(
                    env, entityClass, "()F", "getCollisionBorderSize", "ao");
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    g_hitbox.midGetCollisionBorderSize = nullptr;
                }
            }

            ResolveEntityInvisibleMethods(env, mcObj, entityClass);

            env->DeleteLocalRef(entityClass);
        } else {
            env->ExceptionClear();
        }
    } else {
        ResolveEntityInvisibleMethods(env, mcObj, nullptr);
    }

    jfieldID fidRenderGlobal = env->GetFieldID(mcClass, "renderGlobal", "Lnet/minecraft/client/renderer/RenderGlobal;");
    if (fidRenderGlobal && !env->ExceptionCheck()) {
        jobject rg = env->GetObjectField(mcObj, fidRenderGlobal);
        if (rg && !env->ExceptionCheck()) {
            jclass rgClass = env->GetObjectClass(rg);
            if (rgClass && !env->ExceptionCheck()) {
                g_hitbox.midDrawOutlined = ResolveDrawOutlinedMethod(env, rgClass);
                if (g_hitbox.midDrawOutlined && !env->ExceptionCheck()) {
                    g_hitbox.clsRenderGlobal = (jclass)env->NewGlobalRef(rgClass);
                } else {
                    env->ExceptionClear();
                    g_hitbox.midDrawOutlined = nullptr;
                }
                env->DeleteLocalRef(rgClass);
            } else {
                env->ExceptionClear();
            }
            env->DeleteLocalRef(rg);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    if (!g_hitbox.clsAabb) {
        ResolveAabbClass(env, mcObj);
    }

    // bew=EntityPlayerSP (local only); other players are bex — use bet (AbstractClientPlayer)
    jclass playerClass = LoadClassViaMc(env, mcObj, "net.minecraft.client.entity.AbstractClientPlayer");
    if (!playerClass) playerClass = McFindClass(env, "net/minecraft/client/entity/AbstractClientPlayer");
    if (!playerClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        playerClass = LoadClassViaMc(env, mcObj, "net.minecraft.client.entity.EntityOtherPlayerMP");
        if (!playerClass) playerClass = McFindClass(env, "net/minecraft/client/entity/EntityOtherPlayerMP");
    }
    if (playerClass && !env->ExceptionCheck()) {
        g_hitbox.clsEntityPlayer = (jclass)env->NewGlobalRef(playerClass);
        env->DeleteLocalRef(playerClass);
    } else {
        env->ExceptionClear();
    }

    jclass dragonClass = LoadHitboxEntityClass(env, mcObj, "ug", "net/minecraft/entity/boss/EntityDragon");
    if (dragonClass && !env->ExceptionCheck()) {
        g_hitbox.clsEntityDragon = (jclass)env->NewGlobalRef(dragonClass);
        env->DeleteLocalRef(dragonClass);
        DragonHitboxLog("JNI EntityDragon class loaded");
    } else {
        env->ExceptionClear();
        DragonHitboxLog("[WARN] EntityDragon class not found");
    }

    // MCP 1.8.9 client: EntityFireball = ws (not ye — ye is InventoryCraftResult).
    jclass fireballClass = LoadHitboxEntityClass(env, mcObj, "ws", "net/minecraft/entity/projectile/EntityFireball");
    if ((!fireballClass || env->ExceptionCheck())) {
        env->ExceptionClear();
        fireballClass = LoadHitboxEntityClass(env, mcObj, "wu", "net/minecraft/entity/projectile/EntityLargeFireball");
    }
    if (fireballClass && !env->ExceptionCheck()) {
        g_hitbox.clsEntityFireball = (jclass)env->NewGlobalRef(fireballClass);
        env->DeleteLocalRef(fireballClass);
        DragonHitboxLog("JNI EntityFireball class loaded");
    } else {
        env->ExceptionClear();
        DragonHitboxLog("[WARN] EntityFireball class not found");
    }

    jclass livingClass = LoadHitboxEntityClass(env, mcObj, "pr", "net/minecraft/entity/EntityLivingBase");
    if (livingClass && !env->ExceptionCheck()) {
        g_hitbox.midGetHealth = env->GetMethodID(livingClass, "getHealth", "()F");
        if (!g_hitbox.midGetHealth || env->ExceptionCheck()) {
            env->ExceptionClear();
            g_hitbox.midGetHealth = env->GetMethodID(livingClass, "bn", "()F");
            if (!g_hitbox.midGetHealth || env->ExceptionCheck()) {
                env->ExceptionClear();
                g_hitbox.midGetHealth = nullptr;
            }
        }
        g_hitbox.midGetMaxHealth = env->GetMethodID(livingClass, "getMaxHealth", "()F");
        if (!g_hitbox.midGetMaxHealth || env->ExceptionCheck()) {
            env->ExceptionClear();
            g_hitbox.midGetMaxHealth = env->GetMethodID(livingClass, "bu", "()F");
            if (!g_hitbox.midGetMaxHealth || env->ExceptionCheck()) {
                env->ExceptionClear();
                g_hitbox.midGetMaxHealth = nullptr;
            }
        }
        env->DeleteLocalRef(livingClass);
    } else {
        env->ExceptionClear();
    }

    jclass listClass = env->FindClass("java/util/List");
    if (listClass && !env->ExceptionCheck()) {
        g_hitbox.midListSize = env->GetMethodID(listClass, "size", "()I");
        g_hitbox.midListGet = env->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;");
        env->DeleteLocalRef(listClass);
    } else {
        env->ExceptionClear();
    }

    if (self) env->DeleteLocalRef(self);
    if (probeEntity && probeEntity != self) env->DeleteLocalRef(probeEntity);
    if (playerList) env->DeleteLocalRef(playerList);
    if (renderManager) env->DeleteLocalRef(renderManager);
    env->DeleteLocalRef(mcClass);
    if (env->ExceptionCheck()) env->ExceptionClear();

    const bool hasWorldSource =
        g_hitbox.fidWorld || g_hitbox.fidRmWorldObj || g_hitbox.fidEntityWorldObj;
    const bool hasEntityList =
        g_hitbox.fidWorldPlayerList || g_hitbox.fidWorldEntityList;
    const bool hasRmCamera = g_hitbox.fidRmViewerX && g_hitbox.fidRmViewerY &&
        g_hitbox.fidRmViewerZ && g_hitbox.fidRmViewYaw && g_hitbox.fidRmViewPitch;
    g_hitbox.useEntityCamera = !hasRmCamera && g_hitbox.fidSelf &&
        g_hitbox.fidPosX && g_hitbox.fidLastX && g_hitbox.fidYaw && g_hitbox.fidPitch;

    g_hitbox.ok =
        g_hitbox.midGetRenderManager &&
        g_hitbox.fidTimer && g_hitbox.fidPartialTicks &&
        (hasRmCamera || g_hitbox.useEntityCamera) &&
        hasWorldSource && hasEntityList && g_hitbox.fidSelf &&
        g_hitbox.fidPosX && g_hitbox.fidPosY && g_hitbox.fidPosZ &&
        g_hitbox.fidLastX && g_hitbox.fidLastY && g_hitbox.fidLastZ &&
        g_hitbox.fidYaw && g_hitbox.fidEid &&
        g_hitbox.midGetEntityBb &&
        g_hitbox.clsAabb && g_hitbox.clsRenderGlobal && g_hitbox.clsEntityPlayer &&
        g_hitbox.midAabbCtor && g_hitbox.midDrawOutlined &&
        g_hitbox.midListSize && g_hitbox.midListGet &&
        g_hitbox.fidDisplayW && g_hitbox.fidDisplayH;

    g_hitbox.resolved = g_hitbox.ok;
#ifdef VIGIL_LUNAR
    if (!g_hitbox.ok) {
        static ULONGLONG s_lastLog = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - s_lastLog >= 10000) {
            s_lastLog = now;
            ClassFinderLogLine("[WARN] Hitbox JNI incomplete (world/renderGlobal/list?)");
        }
    } else {
        static bool s_loggedOk = false;
        if (!s_loggedOk) {
            s_loggedOk = true;
            ClassFinderLogLine("[OK] Hitbox JNI resolved");
        }
    }
#endif
    return g_hitbox.ok;
}

bool HitboxJniReady() {
    return g_hitbox.ok;
}

static double Lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

static bool ReadAxisAlignedBox(JNIEnv* env, jobject bb, double& minX, double& minY, double& minZ,
                               double& maxX, double& maxY, double& maxZ) {
    if (!bb) return false;
    jclass bbClass = env->GetObjectClass(bb);
    if (!bbClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    jfieldID fidMinX = env->GetFieldID(bbClass, "minX", "D");
    jfieldID fidMinY = env->GetFieldID(bbClass, "minY", "D");
    jfieldID fidMinZ = env->GetFieldID(bbClass, "minZ", "D");
    jfieldID fidMaxX = env->GetFieldID(bbClass, "maxX", "D");
    jfieldID fidMaxY = env->GetFieldID(bbClass, "maxY", "D");
    jfieldID fidMaxZ = env->GetFieldID(bbClass, "maxZ", "D");
    env->DeleteLocalRef(bbClass);
    if (!fidMinX || !fidMinY || !fidMinZ || !fidMaxX || !fidMaxY || !fidMaxZ || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    minX = env->GetDoubleField(bb, fidMinX);
    minY = env->GetDoubleField(bb, fidMinY);
    minZ = env->GetDoubleField(bb, fidMinZ);
    maxX = env->GetDoubleField(bb, fidMaxX);
    maxY = env->GetDoubleField(bb, fidMaxY);
    maxZ = env->GetDoubleField(bb, fidMaxZ);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return true;
}

static bool DrawTeamColoredBoxVanilla(JNIEnv* env,
                                      jobject entity,
                                      jobject renderManager,
                                      double interpX,
                                      double interpY,
                                      double interpZ,
                                      double renderPosX,
                                      double renderPosY,
                                      double renderPosZ,
                                      double renderX,
                                      double renderY,
                                      double renderZ,
                                      float yaw,
                                      float partialTicks,
                                      int rgb,
                                      double* outRelMinX = nullptr,
                                      double* outRelMinY = nullptr,
                                      double* outRelMinZ = nullptr,
                                      double* outRelMaxX = nullptr,
                                      double* outRelMaxY = nullptr,
                                      double* outRelMaxZ = nullptr) {
    double minX = 0.0;
    double minY = 0.0;
    double minZ = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    double maxZ = 0.0;
    bool haveBox = false;

    if (g_hitbox.midGetEntityBb) {
        jobject bb = env->CallObjectMethod(entity, g_hitbox.midGetEntityBb);
        if (bb && !env->ExceptionCheck()) {
            haveBox = ReadAxisAlignedBox(env, bb, minX, minY, minZ, maxX, maxY, maxZ);
            env->DeleteLocalRef(bb);
        } else {
            env->ExceptionClear();
        }
    }

    if (!haveBox) {
        float width = 0.6f;
        float height = 1.8f;
        if (g_hitbox.fidWidth) {
            const float w = env->GetFloatField(entity, g_hitbox.fidWidth);
            if (!env->ExceptionCheck() && w > 0.05f) width = w;
            else env->ExceptionClear();
        }
        if (g_hitbox.fidHeight) {
            const float h = env->GetFloatField(entity, g_hitbox.fidHeight);
            if (!env->ExceptionCheck() && h > 0.05f) height = h;
            else env->ExceptionClear();
        }
        const double halfW = (double)width * 0.5;
        minX = interpX - halfW;
        minY = interpY;
        minZ = interpZ - halfW;
        maxX = interpX + halfW;
        maxY = interpY + (double)height;
        maxZ = interpZ + halfW;
        haveBox = true;
    } else if (g_hitbox.fidPosX && g_hitbox.fidPosY && g_hitbox.fidPosZ) {
        const double posX = env->GetDoubleField(entity, g_hitbox.fidPosX);
        const double posY = env->GetDoubleField(entity, g_hitbox.fidPosY);
        const double posZ = env->GetDoubleField(entity, g_hitbox.fidPosZ);
        if (!env->ExceptionCheck()) {
            const double dx = interpX - posX;
            const double dy = interpY - posY;
            const double dz = interpZ - posZ;
            minX += dx;
            minY += dy;
            minZ += dz;
            maxX += dx;
            maxY += dy;
            maxZ += dz;
        } else {
            env->ExceptionClear();
        }
    }

    if (!haveBox) return false;

    if (g_hitbox.midGetCollisionBorderSize) {
        const float border = env->CallFloatMethod(entity, g_hitbox.midGetCollisionBorderSize);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        } else if (border > 0.0f) {
            const double pad = (double)border;
            minX -= pad;
            minY -= pad;
            minZ -= pad;
            maxX += pad;
            maxY += pad;
            maxZ += pad;
        }
    }

    // drawOutlinedBoundingBox uses camera-relative space (renderPos).
    double relMinX = minX - renderPosX;
    double relMinY = minY - renderPosY;
    double relMinZ = minZ - renderPosZ;
    double relMaxX = maxX - renderPosX;
    double relMaxY = maxY - renderPosY;
    double relMaxZ = maxZ - renderPosZ;

    if (outRelMinX) *outRelMinX = relMinX;
    if (outRelMinY) *outRelMinY = relMinY;
    if (outRelMinZ) *outRelMinZ = relMinZ;
    if (outRelMaxX) *outRelMaxX = relMaxX;
    if (outRelMaxY) *outRelMaxY = relMaxY;
    if (outRelMaxZ) *outRelMaxZ = relMaxZ;

    if (g_hitbox.clsAabb && g_hitbox.midAabbCtor && g_hitbox.clsRenderGlobal && g_hitbox.midDrawOutlined) {
        DrawOutlinedRelativeAabb(env, relMinX, relMinY, relMinZ, relMaxX, relMaxY, relMaxZ, rgb, 255);
        return true;
    }

    if (renderManager && g_hitbox.midRenderDebugBb) {
        env->CallVoidMethod(renderManager, g_hitbox.midRenderDebugBb,
                            entity, renderX, renderY, renderZ, yaw, partialTicks);
        if (!env->ExceptionCheck()) return true;
        env->ExceptionClear();
    }

    return false;
}

void RenderColoredHitboxes(JNIEnv* env, jobject mcObj, jclass mcClass, jclass customGuiClass) {
    if (!env || !mcObj || !mcClass) return;
    if (!customGuiClass) return;
    if (!VigilIsHypixelServerActive()) return;

    const bool playerHitboxes = ReadGuiStaticBool(env, customGuiClass, "coloredHitboxes", false);
    const bool dragonHitboxes = ReadGuiStaticBool(env, customGuiClass, "dragonHitboxes", false);
    const bool fireballHitboxes = ReadGuiStaticBool(env, customGuiClass, "fireballHitboxes", false);
    if (!playerHitboxes && !dragonHitboxes && !fireballHitboxes) return;

    const bool drawPlayers = playerHitboxes && g_redState == "Ingame";
    const bool drawDragons = dragonHitboxes && g_redState == "Ingame";
    const bool drawFireballs = fireballHitboxes && g_redState == "Ingame";
    if (!drawPlayers && !drawDragons && !drawFireballs) return;

    std::string fireballColorCode = ReadGuiStaticString(env, customGuiClass, "fireballHitboxColor", "c");
    if (fireballColorCode.empty()) fireballColorCode = "c";
    const int fireballRgb = McCodeToRgbHitbox(fireballColorCode[0]);

    static bool s_dragonDebugArmed = false;
    if (drawDragons && !s_dragonDebugArmed) {
        s_dragonDebugArmed = true;
        DragonHitboxLog("enabled");
    } else if (!drawDragons) {
        s_dragonDebugArmed = false;
    }

    if (!g_hitbox.ok) return;

    jobject renderManager = env->CallObjectMethod(mcObj, g_hitbox.midGetRenderManager);
    if (!renderManager || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    jobject timer = env->GetObjectField(mcObj, g_hitbox.fidTimer);
    if (!timer || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(renderManager);
        return;
    }
    const float partialTicks = env->GetFloatField(timer, g_hitbox.fidPartialTicks);
    env->DeleteLocalRef(timer);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(renderManager);
        return;
    }

    jobject self = env->GetObjectField(mcObj, g_hitbox.fidSelf);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        self = nullptr;
    }

    double viewerX = 0.0;
    double viewerY = 0.0;
    double viewerZ = 0.0;
    float viewYaw = 0.0f;
    float viewPitch = 0.0f;

    if (!g_hitbox.useEntityCamera) {
        viewerX = env->GetDoubleField(renderManager, g_hitbox.fidRmViewerX);
        viewerY = env->GetDoubleField(renderManager, g_hitbox.fidRmViewerY);
        viewerZ = env->GetDoubleField(renderManager, g_hitbox.fidRmViewerZ);
        viewYaw = env->GetFloatField(renderManager, g_hitbox.fidRmViewYaw);
        viewPitch = env->GetFloatField(renderManager, g_hitbox.fidRmViewPitch);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            if (self) env->DeleteLocalRef(self);
            env->DeleteLocalRef(renderManager);
            return;
        }
    } else if (self) {
        const double posX = env->GetDoubleField(self, g_hitbox.fidPosX);
        const double posY = env->GetDoubleField(self, g_hitbox.fidPosY);
        const double posZ = env->GetDoubleField(self, g_hitbox.fidPosZ);
        const double lastX = env->GetDoubleField(self, g_hitbox.fidLastX);
        const double lastY = env->GetDoubleField(self, g_hitbox.fidLastY);
        const double lastZ = env->GetDoubleField(self, g_hitbox.fidLastZ);
        viewYaw = env->GetFloatField(self, g_hitbox.fidYaw);
        viewPitch = env->GetFloatField(self, g_hitbox.fidPitch);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            if (self) env->DeleteLocalRef(self);
            env->DeleteLocalRef(renderManager);
            return;
        }
        const double t = (double)partialTicks;
        viewerX = Lerp(lastX, posX, t);
        viewerY = Lerp(lastY, posY, t);
        viewerZ = Lerp(lastZ, posZ, t);
    } else {
        env->DeleteLocalRef(renderManager);
        return;
    }

    jobject world = nullptr;
    if (g_hitbox.fidWorld) {
        world = env->GetObjectField(mcObj, g_hitbox.fidWorld);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            world = nullptr;
        }
    }
    if (!world && g_hitbox.fidRmWorldObj) {
        world = env->GetObjectField(renderManager, g_hitbox.fidRmWorldObj);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            world = nullptr;
        }
    }
    if (!world && g_hitbox.fidEntityWorldObj && self) {
        world = env->GetObjectField(self, g_hitbox.fidEntityWorldObj);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            world = nullptr;
        }
    }
    if (!world) {
        if (self) env->DeleteLocalRef(self);
        env->DeleteLocalRef(renderManager);
        return;
    }

    jobject playerList = nullptr;
    const char* listKind = "none";
    if (g_hitbox.fidWorldEntityList) {
        playerList = env->GetObjectField(world, g_hitbox.fidWorldEntityList);
        if (playerList && !env->ExceptionCheck()) {
            listKind = "entities";
        } else {
            env->ExceptionClear();
            playerList = nullptr;
        }
    }
    if (!playerList && g_hitbox.fidWorldPlayerList) {
        playerList = env->GetObjectField(world, g_hitbox.fidWorldPlayerList);
        if (playerList && !env->ExceptionCheck()) {
            listKind = "players";
        } else {
            env->ExceptionClear();
            playerList = nullptr;
        }
    }
    env->DeleteLocalRef(world);
    if (!playerList || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (self) env->DeleteLocalRef(self);
        env->DeleteLocalRef(renderManager);
        return;
    }

    const int count = env->CallIntMethod(playerList, g_hitbox.midListSize);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        if (self) env->DeleteLocalRef(self);
        env->DeleteLocalRef(playerList);
        env->DeleteLocalRef(renderManager);
        return;
    }

    HitboxBeginDrawPass(GetHitboxLineWidth(env, customGuiClass));

    int dragonSeen = 0;
    int dragonDrawn = 0;
    int dragonSkipped = 0;

    for (int i = 0; i < count; ++i) {
        jobject entity = env->CallObjectMethod(playerList, g_hitbox.midListGet, i);
        if (!entity || env->ExceptionCheck()) {
            env->ExceptionClear();
            continue;
        }

        if (self && env->IsSameObject(entity, self)) {
            env->DeleteLocalRef(entity);
            continue;
        }

        if (!ShouldRenderColoredHitboxEntity(env, entity, listKind[0] == 'e', drawPlayers, drawDragons, drawFireballs)) {
            env->ExceptionClear();
            env->DeleteLocalRef(entity);
            continue;
        }

        jclass entityClass = env->GetObjectClass(entity);
        if (!entityClass || env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(entity);
            continue;
        }

        // Skip invisibles (potion / vanish) — match vanilla entity visibility.
        // Fireballs are not living invisibles; skip this check (JNI can false-positive).
        const bool isFireballEarly = IsFireballEntity(env, entity);
        if (!isFireballEarly && ShouldSkipInvisibleHitbox(env, entity, self)) {
            env->DeleteLocalRef(entityClass);
            env->DeleteLocalRef(entity);
            continue;
        }

        const double posX = env->GetDoubleField(entity, g_hitbox.fidPosX);
        const double posY = env->GetDoubleField(entity, g_hitbox.fidPosY);
        const double posZ = env->GetDoubleField(entity, g_hitbox.fidPosZ);
        const double lastX = env->GetDoubleField(entity, g_hitbox.fidLastX);
        const double lastY = env->GetDoubleField(entity, g_hitbox.fidLastY);
        const double lastZ = env->GetDoubleField(entity, g_hitbox.fidLastZ);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(entityClass);
            env->DeleteLocalRef(entity);
            continue;
        }

        const double interpX = Lerp(lastX, posX, partialTicks);
        const double interpY = Lerp(lastY, posY, partialTicks);
        const double interpZ = Lerp(lastZ, posZ, partialTicks);

        const float yaw = env->GetFloatField(entity, g_hitbox.fidYaw);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(entityClass);
            env->DeleteLocalRef(entity);
            continue;
        }

        const double renderX = interpX - viewerX;
        const double renderY = interpY - viewerY;
        const double renderZ = interpZ - viewerZ;

        jclass mcRuntime = env->GetObjectClass(mcObj);
        int rgb = 0;
        const bool isDragon = IsEnderDragonEntity(env, entity);
        const bool isFireball = IsFireballEntity(env, entity);
        if (isDragon) {
            ++dragonSeen;
            const HitboxTeamColorInfo colorInfo =
                ResolveDragonHitboxTeamColor(env, mcObj, mcRuntime ? mcRuntime : mcClass, entity, entityClass);
            if (mcRuntime) env->DeleteLocalRef(mcRuntime);
            rgb = colorInfo.rgb;
            const std::string displayName = GetEntityDisplayNameFormatted(env, entity, entityClass);
            const int entityId = g_hitbox.fidEid ? env->GetIntField(entity, g_hitbox.fidEid) : -1;
            if (env->ExceptionCheck()) env->ExceptionClear();

            if (!ShouldDrawDragonHitboxTeamColor(colorInfo)) {
                ++dragonSkipped;
                DragonHitboxLogState(entityId, colorInfo, displayName, false, "no team color resolved");
                env->DeleteLocalRef(entityClass);
                env->DeleteLocalRef(entity);
                continue;
            }
            ++dragonDrawn;
            DragonHitboxLogState(entityId, colorInfo, displayName, true, nullptr);
        } else if (isFireball) {
            if (mcRuntime) env->DeleteLocalRef(mcRuntime);
            rgb = fireballRgb;
        } else {
            const HitboxTeamColorInfo colorInfo =
                ResolveEntityTeamColor(env, mcObj, mcRuntime ? mcRuntime : mcClass, entity, entityClass);
            if (mcRuntime) env->DeleteLocalRef(mcRuntime);
            rgb = colorInfo.rgb;
            if (!ShouldDrawHitboxTeamColor(colorInfo)) {
                env->DeleteLocalRef(entityClass);
                env->DeleteLocalRef(entity);
                continue;
            }
        }

        double relMinX = 0, relMinY = 0, relMinZ = 0, relMaxX = 0, relMaxY = 0, relMaxZ = 0;
        const bool drawn = DrawTeamColoredBoxVanilla(env, entity, renderManager, interpX, interpY, interpZ,
                                  viewerX, viewerY, viewerZ,
                                  renderX, renderY, renderZ, yaw, partialTicks, rgb,
                                  &relMinX, &relMinY, &relMinZ, &relMaxX, &relMaxY, &relMaxZ);
        if (drawn && isDragon) {
            DrawDragonHealthBarBeside(env, entity, relMinX, relMinY, relMinZ, relMaxX, relMaxY, relMaxZ);
        }

        env->DeleteLocalRef(entityClass);
        env->DeleteLocalRef(entity);
    }

    HitboxEndDrawPass();

    if (drawDragons) {
        static ULONGLONG s_lastDragonScanLogAt = 0;
        const ULONGLONG nowScan = GetTickCount64();
        if (nowScan - s_lastDragonScanLogAt >= 5000) {
            s_lastDragonScanLogAt = nowScan;
            char scanBuf[256];
            std::snprintf(scanBuf, sizeof(scanBuf),
                          "scan list=%s entities=%d dragons=%d drawn=%d skipped=%d redState=%s",
                          listKind, count, dragonSeen, dragonDrawn, dragonSkipped, g_redState.c_str());
            DragonHitboxLog(scanBuf);
        }
    }

    if (self) env->DeleteLocalRef(self);
    env->DeleteLocalRef(playerList);
    env->DeleteLocalRef(renderManager);
}

extern "C" JNIEXPORT void JNICALL Java_VigilHitboxInstaller_nativeInstallRenderGlobal(JNIEnv* env, jclass);

extern "C" JNIEXPORT void JNICALL Java_VigilRenderGlobal_nativeAfterRenderEntities(JNIEnv* env, jclass) {
    if (!env || !g_mcClassGlobal || !g_midGetMinecraftStatic) return;

    jobject mcObj = env->CallStaticObjectMethod(g_mcClassGlobal, g_midGetMinecraftStatic);
    if (!mcObj || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    jclass mcClass = env->GetObjectClass(mcObj);
    if (mcClass && !env->ExceptionCheck()) {
        PumpMinecraftClientLogic(env, mcObj, mcClass);
        RenderColoredHitboxes(env, mcObj, mcClass, g_customGuiClassGlobal);
        env->DeleteLocalRef(mcClass);
    } else {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(mcObj);
}

static bool& RenderGlobalHookInstalledFlag() {
    static bool s_installed = false;
    return s_installed;
}

bool IsRenderGlobalHookInstalled() {
    return RenderGlobalHookInstalledFlag();
}

static bool EnsureHitboxHookClasses(JNIEnv* env, jobject classLoader) {
    if (HitboxHookClassesReadyFlag()) return true;
    if (!env) return false;
    if (!classLoader && g_injectClassLoaderGlobal) {
        classLoader = g_injectClassLoaderGlobal;
    }
    if (!classLoader) return false;

    jclass hookClass = LoadInjectedClass(env, classLoader, "VigilRenderGlobal",
                                         VigilRenderGlobalLunar_class, VigilRenderGlobalLunar_class_len);
    jclass installerClass = LoadInjectedClass(env, classLoader, "VigilHitboxInstaller",
                                              VigilHitboxInstallerLunar_class, VigilHitboxInstallerLunar_class_len);
    if (!hookClass || !installerClass) {
        if (hookClass) env->DeleteLocalRef(hookClass);
        if (installerClass) env->DeleteLocalRef(installerClass);
        return false;
    }

    JNINativeMethod hookMethods[] = {
        {(char*)"nativeAfterRenderEntities", (char*)"()V",
         (void*)&Java_VigilRenderGlobal_nativeAfterRenderEntities},
    };
    JNINativeMethod installerMethods[] = {
        {(char*)"nativeInstallRenderGlobal", (char*)"()V",
         (void*)&Java_VigilHitboxInstaller_nativeInstallRenderGlobal},
    };
    if (env->RegisterNatives(hookClass, hookMethods, 1) != JNI_OK || env->ExceptionCheck()) {
        env->DeleteLocalRef(hookClass);
        env->DeleteLocalRef(installerClass);
        return false;
    }
    if (env->RegisterNatives(installerClass, installerMethods, 1) != JNI_OK || env->ExceptionCheck()) {
        env->DeleteLocalRef(hookClass);
        env->DeleteLocalRef(installerClass);
        return false;
    }

    g_hitboxHookClassGlobal = (jclass)env->NewGlobalRef(hookClass);
    g_hitboxInstallerClassGlobal = (jclass)env->NewGlobalRef(installerClass);
    env->DeleteLocalRef(hookClass);
    env->DeleteLocalRef(installerClass);
    if (!g_hitboxHookClassGlobal || !g_hitboxInstallerClassGlobal) {
        return false;
    }

    HitboxHookClassesReadyFlag() = true;
    return true;
}

static bool InstallRenderGlobalHookImpl(JNIEnv* env, jobject mcObj) {
    if (!env || !mcObj || !g_hitboxHookClassGlobal || !g_hitboxInstallerClassGlobal) return false;

    jclass mcClass = g_mcClassGlobal ? g_mcClassGlobal : env->GetObjectClass(mcObj);
    const bool ownsMcClass = (!g_mcClassGlobal && mcClass);
    if (!mcClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jfieldID fidRg = env->GetFieldID(mcClass, "renderGlobal", "Lnet/minecraft/client/renderer/RenderGlobal;");
    if (!fidRg || env->ExceptionCheck()) {
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return false;
    }

    jobject oldRg = env->GetObjectField(mcObj, fidRg);
    if (!oldRg || env->ExceptionCheck()) {
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return false;
    }

    if (env->IsInstanceOf(oldRg, g_hitboxHookClassGlobal)) {
        env->DeleteLocalRef(oldRg);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        RenderGlobalHookInstalledFlag() = true;
        return true;
    }

    jmethodID ctor = env->GetMethodID(g_hitboxHookClassGlobal, "<init>", "(Lnet/minecraft/client/Minecraft;)V");
    if (!ctor || env->ExceptionCheck()) {
        env->DeleteLocalRef(oldRg);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return false;
    }

    jobject newRg = env->NewObject(g_hitboxHookClassGlobal, ctor, mcObj);
    if (!newRg || env->ExceptionCheck()) {
        env->DeleteLocalRef(oldRg);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return false;
    }

    jmethodID midCopy = nullptr;
    {
        static const char* kCopyNames[] = {"copyRenderGlobalFields", "copyBfrFields"};
        static const char* kCopySigs[] = {
            "(Lnet/minecraft/client/renderer/RenderGlobal;Lnet/minecraft/client/renderer/RenderGlobal;)V",
            "(Lnet/minecraft/client/renderer/RenderGlobal;Lnet/minecraft/client/renderer/RenderGlobal;)V",
        };
        for (int i = 0; i < 2; ++i) {
            midCopy = env->GetStaticMethodID(g_hitboxInstallerClassGlobal, kCopyNames[i], kCopySigs[i]);
            if (midCopy && !env->ExceptionCheck()) break;
            env->ExceptionClear();
            midCopy = nullptr;
        }
    }
    if (midCopy && !env->ExceptionCheck()) {
        env->CallStaticVoidMethod(g_hitboxInstallerClassGlobal, midCopy, oldRg, newRg);
        if (env->ExceptionCheck()) {
            env->DeleteLocalRef(newRg);
            env->DeleteLocalRef(oldRg);
            if (ownsMcClass) env->DeleteLocalRef(mcClass);
            return false;
        }
    } else {
        env->ExceptionClear();
    }

    env->SetObjectField(mcObj, fidRg, newRg);
    if (env->ExceptionCheck()) {
        env->DeleteLocalRef(newRg);
        env->DeleteLocalRef(oldRg);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return false;
    }

    env->DeleteLocalRef(newRg);
    env->DeleteLocalRef(oldRg);
    if (ownsMcClass) env->DeleteLocalRef(mcClass);

    RenderGlobalHookInstalledFlag() = true;
    return true;
}

extern "C" JNIEXPORT void JNICALL Java_VigilHitboxInstaller_nativeInstallRenderGlobal(JNIEnv* env, jclass) {
    HitboxInstallScheduledFlag() = false;
    if (!env || !g_mcClassGlobal || !g_midGetMinecraftStatic) {
        return;
    }

    jobject mcObj = env->CallStaticObjectMethod(g_mcClassGlobal, g_midGetMinecraftStatic);
    if (!mcObj || env->ExceptionCheck()) {
        return;
    }

    if (!InstallRenderGlobalHookImpl(env, mcObj)) {
#ifdef VIGIL_LUNAR
        ClassFinderLogLine("[FAIL] RenderGlobal hook install");
#endif
    } else {
#ifdef VIGIL_LUNAR
        ClassFinderLogLine("[OK] RenderGlobal hook installed");
#endif
    }
    env->DeleteLocalRef(mcObj);
}

bool TryInstallRenderGlobalHook(JNIEnv* env, jobject mcObj, jobject classLoader) {
    static bool s_loggedScheduleFail = false;
    if (RenderGlobalHookInstalledFlag()) return true;
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

    if (!EnsureHitboxHookClasses(env, classLoader)) {
        return false;
    }

    if (HitboxInstallScheduledFlag()) {
        return false;
    }

    jclass mcClass = g_mcClassGlobal ? g_mcClassGlobal : env->GetObjectClass(mcObj);
    const bool ownsMcClass = (!g_mcClassGlobal && mcClass);
    if (!mcClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jmethodID midSchedule = env->GetMethodID(mcClass, "addScheduledTask", "(Ljava/lang/Runnable;)Lcom/google/common/util/concurrent/ListenableFuture;");
    if (!midSchedule || env->ExceptionCheck()) {
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return false;
    }

    jmethodID installerCtor = env->GetMethodID(g_hitboxInstallerClassGlobal, "<init>", "()V");
    if (!installerCtor || env->ExceptionCheck()) {
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return false;
    }

    jobject installer = env->NewObject(g_hitboxInstallerClassGlobal, installerCtor);
    if (!installer || env->ExceptionCheck()) {
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        return false;
    }

    env->CallObjectMethod(mcObj, midSchedule, installer);
    if (env->ExceptionCheck()) {
        env->DeleteLocalRef(installer);
        if (ownsMcClass) env->DeleteLocalRef(mcClass);
        if (!s_loggedScheduleFail) {
            s_loggedScheduleFail = true;
        }
        return false;
    }

    env->DeleteLocalRef(installer);
    if (ownsMcClass) env->DeleteLocalRef(mcClass);

    HitboxInstallScheduledFlag() = true;
    return false;
}
