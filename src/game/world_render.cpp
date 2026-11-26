// Vigil - Suspect Highlight: in-world outlines around players under suspicion.
// Evolved from the legacy colored-hitbox module; reuses its proven RenderGlobal hook. Team,
// dragon and fireball cosmetics were removed.

#include "world_render.h"

#include "../core/log.h"
#include "../core/settings.h"
#include "bytecode/VigilHitboxInstaller.h"
#include "bytecode/VigilRenderGlobal.h"
#include "class_finder.h"
#include "jvm.h"
#include "mc.h"
#include "minecraft_util.h"
#include "runtime.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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

static float GetHitboxLineWidth() {
    return ClampHitboxLineWidth(static_cast<int>(vigil::Settings::instance().getNumber("highlight.thickness", 2.0)));
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

// ── Over-head marker ─────────────────────────────────────────────────────────────────────────
// A small billboard above the player, in the same language as the halo: a confidence ring and a
// glyph whose shape (not only its color) says the tier — ◇ elevated, ◆ high, ▲! critical — or the
// user's verdict (✖ on the verdict color).

static void MarkerColor(int rgb, float a) {
    glColor4f(((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, a);
}

static void MarkerDisc(float r, float red, float green, float blue, float a) {
    glColor4f(red, green, blue, a);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(0.f, 0.f);
    for (int i = 0; i <= 32; ++i) {
        const float t = 6.2831853f * i / 32.f;
        glVertex2f(std::sin(t) * r, -std::cos(t) * r);
    }
    glEnd();
}

static void MarkerArc(float r, float w, float fraction) {
    const int seg = std::max(2, static_cast<int>(48 * fraction));
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= seg; ++i) {
        const float t = 6.2831853f * fraction * i / seg;
        const float s = std::sin(t), c = -std::cos(t);
        glVertex2f(s * (r - w * 0.5f), c * (r - w * 0.5f));
        glVertex2f(s * (r + w * 0.5f), c * (r + w * 0.5f));
    }
    glEnd();
}

static void DrawMarker(double x, double y, double z, float viewYaw, float viewPitch, const HighlightStyle& s) {
    glPushMatrix();
    glTranslated(x, y, z);
    glRotatef(-viewYaw, 0.f, 1.f, 0.f);
    glRotatef(viewPitch, 1.f, 0.f, 0.f);
    glScalef(-0.025f, -0.025f, 0.025f);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    MarkerDisc(10.5f, 0.f, 0.f, 0.f, 0.45f);
    glColor4f(1.f, 1.f, 1.f, 0.22f);
    MarkerArc(8.5f, 1.6f, 1.f);
    if (s.confidence >= 1.f) {
        MarkerColor(s.ringRgb, 0.95f);
        MarkerArc(8.5f, 2.6f, std::min(1.f, s.confidence / 100.f));
    }
    if (s.confirmed) {
        MarkerDisc(5.2f, ((s.verdictRgb >> 16) & 0xFF) / 255.f, ((s.verdictRgb >> 8) & 0xFF) / 255.f, (s.verdictRgb & 0xFF) / 255.f, 1.f);
        glColor4f(1.f, 1.f, 1.f, 1.f);
        glLineWidth(2.f);
        glBegin(GL_LINES);
        glVertex2f(-2.4f, -2.4f); glVertex2f(2.4f, 2.4f);
        glVertex2f(2.4f, -2.4f); glVertex2f(-2.4f, 2.4f);
        glEnd();
    } else if (s.tier >= 3) {
        MarkerColor(s.ringRgb, 1.f);
        glBegin(GL_TRIANGLES);
        glVertex2f(0.f, -5.2f); glVertex2f(5.0f, 3.8f); glVertex2f(-5.0f, 3.8f);
        glEnd();
        glColor4f(0.f, 0.f, 0.f, 0.85f);
        glBegin(GL_QUADS);
        glVertex2f(-0.7f, -2.2f); glVertex2f(0.7f, -2.2f); glVertex2f(0.7f, 1.0f); glVertex2f(-0.7f, 1.0f);
        glVertex2f(-0.7f, 1.8f); glVertex2f(0.7f, 1.8f); glVertex2f(0.7f, 3.0f); glVertex2f(-0.7f, 3.0f);
        glEnd();
    } else if (s.tier >= 1) {
        MarkerColor(s.ringRgb, 1.f);
        if (s.tier >= 2) {
            glBegin(GL_QUADS);
            glVertex2f(0.f, -4.6f); glVertex2f(4.6f, 0.f); glVertex2f(0.f, 4.6f); glVertex2f(-4.6f, 0.f);
            glEnd();
        } else {
            glLineWidth(2.f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(0.f, -4.4f); glVertex2f(4.4f, 0.f); glVertex2f(0.f, 4.4f); glVertex2f(-4.4f, 0.f);
            glEnd();
        }
    }
    glDepthMask(GL_TRUE);
    glPopMatrix();
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
    if (mcpSlash) {
        jclass lunarCls = McFindClass(env, mcpSlash);
        if (lunarCls && !env->ExceptionCheck()) return lunarCls;
        env->ExceptionClear();
    }
    jclass cls = LoadClassViaMc(env, mcObj, obfName);
    if (!cls) cls = env->FindClass(obfName);
    if (cls && env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return cls;
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

static void ResolveRmFieldsFromClass(JNIEnv* env, jclass rmClass) {
    if (!rmClass) return;

    // renderPosX/Y/Z (o,p,q) - used by entity/world render; viewerPos (h,i,j) is for chunk culling.
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

    // bew=EntityPlayerSP (local only); other players are bex - use bet (AbstractClientPlayer)
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
    if (!g_hitbox.ok) {
        static ULONGLONG s_lastLog = 0;
        const ULONGLONG nowLog = GetTickCount64();
        if (nowLog - s_lastLog >= 10000) {
            s_lastLog = nowLog;
            vigil::log::write("runtime", "[WARN] Hitbox JNI incomplete (world/renderGlobal/list?)");
        }
    } else {
        static bool s_loggedOk = false;
        if (!s_loggedOk) {
            s_loggedOk = true;
            vigil::log::write("runtime", "[OK] Hitbox JNI resolved");
        }
    }
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

void RenderSuspectHighlights(JNIEnv* env, jobject mcObj, const HighlightQuery& query) {
    if (!env || !mcObj || !query) return;
    if (!g_hitbox.ok) {
        TryResolveHitboxJni(env, mcObj);
        if (!g_hitbox.ok) return;
    }

    jobject renderManager = env->CallObjectMethod(mcObj, g_hitbox.midGetRenderManager);
    if (!renderManager || env->ExceptionCheck()) { env->ExceptionClear(); return; }
    jobject timer = env->GetObjectField(mcObj, g_hitbox.fidTimer);
    if (!timer || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(renderManager); return; }
    const float partialTicks = env->GetFloatField(timer, g_hitbox.fidPartialTicks);
    env->DeleteLocalRef(timer);
    jobject self = env->GetObjectField(mcObj, g_hitbox.fidSelf);
    if (env->ExceptionCheck()) { env->ExceptionClear(); self = nullptr; }

    double viewerX = 0.0, viewerY = 0.0, viewerZ = 0.0;
    if (!g_hitbox.useEntityCamera) {
        viewerX = env->GetDoubleField(renderManager, g_hitbox.fidRmViewerX);
        viewerY = env->GetDoubleField(renderManager, g_hitbox.fidRmViewerY);
        viewerZ = env->GetDoubleField(renderManager, g_hitbox.fidRmViewerZ);
    } else if (self) {
        const double t = (double)partialTicks;
        viewerX = Lerp(env->GetDoubleField(self, g_hitbox.fidLastX), env->GetDoubleField(self, g_hitbox.fidPosX), t);
        viewerY = Lerp(env->GetDoubleField(self, g_hitbox.fidLastY), env->GetDoubleField(self, g_hitbox.fidPosY), t);
        viewerZ = Lerp(env->GetDoubleField(self, g_hitbox.fidLastZ), env->GetDoubleField(self, g_hitbox.fidPosZ), t);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        if (self) env->DeleteLocalRef(self);
        env->DeleteLocalRef(renderManager);
        return;
    }

    jobject world = g_hitbox.fidWorld ? env->GetObjectField(mcObj, g_hitbox.fidWorld) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); world = nullptr; }
    jobject playerList = (world && g_hitbox.fidWorldPlayerList) ? env->GetObjectField(world, g_hitbox.fidWorldPlayerList) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); playerList = nullptr; }
    if (world) env->DeleteLocalRef(world);
    if (!playerList) {
        if (self) env->DeleteLocalRef(self);
        env->DeleteLocalRef(renderManager);
        return;
    }

    const int count = env->CallIntMethod(playerList, g_hitbox.midListSize);
    if (env->ExceptionCheck()) env->ExceptionClear();
    bool begun = false;
    for (int i = 0; i < count; ++i) {
        jobject entity = env->CallObjectMethod(playerList, g_hitbox.midListGet, i);
        if (!entity || env->ExceptionCheck()) { env->ExceptionClear(); continue; }
        if (self && env->IsSameObject(entity, self)) { env->DeleteLocalRef(entity); continue; }
        const int entityId = g_hitbox.fidEid ? env->GetIntField(entity, g_hitbox.fidEid) : -1;
        HighlightStyle style;
        if (entityId < 0 || !query(entityId, style)) { env->DeleteLocalRef(entity); continue; }
        const int rgb = style.rgb;
        const float fill = style.fillAlpha;

        const double posX = env->GetDoubleField(entity, g_hitbox.fidPosX);
        const double posY = env->GetDoubleField(entity, g_hitbox.fidPosY);
        const double posZ = env->GetDoubleField(entity, g_hitbox.fidPosZ);
        const double lastX = env->GetDoubleField(entity, g_hitbox.fidLastX);
        const double lastY = env->GetDoubleField(entity, g_hitbox.fidLastY);
        const double lastZ = env->GetDoubleField(entity, g_hitbox.fidLastZ);
        const float yaw = env->GetFloatField(entity, g_hitbox.fidYaw);
        if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(entity); continue; }
        const double ix = Lerp(lastX, posX, partialTicks);
        const double iy = Lerp(lastY, posY, partialTicks);
        const double iz = Lerp(lastZ, posZ, partialTicks);

        if (!begun) { HitboxBeginDrawPass(GetHitboxLineWidth()); begun = true; }
        double rminX = 0, rminY = 0, rminZ = 0, rmaxX = 0, rmaxY = 0, rmaxZ = 0;
        const bool drawn = DrawTeamColoredBoxVanilla(env, entity, renderManager, ix, iy, iz, viewerX, viewerY, viewerZ,
                                                     ix - viewerX, iy - viewerY, iz - viewerZ, yaw, partialTicks, rgb,
                                                     &rminX, &rminY, &rminZ, &rmaxX, &rmaxY, &rmaxZ);
        if (drawn && fill > 0.f) {
            glDepthMask(GL_FALSE);
            DrawFilledRelativeSlab(rminX, rminY, rminZ, rmaxX, rmaxY, rmaxZ, rgb, fill);
            glDepthMask(GL_TRUE);
        }
        if (style.marker && (style.tier > 0 || style.confirmed) && g_hitbox.fidRmViewYaw && g_hitbox.fidRmViewPitch) {
            // fidRmViewYaw holds playerViewX (pitch), fidRmViewPitch holds playerViewY (yaw).
            const float viewPitch = env->GetFloatField(renderManager, g_hitbox.fidRmViewYaw);
            const float viewYaw = env->GetFloatField(renderManager, g_hitbox.fidRmViewPitch);
            if (!env->ExceptionCheck()) {
                const double top = drawn ? rmaxY : (iy + 1.9 - viewerY);
                DrawMarker((drawn ? (rminX + rmaxX) * 0.5 : ix - viewerX), top + 0.55, (drawn ? (rminZ + rmaxZ) * 0.5 : iz - viewerZ),
                           viewYaw, viewPitch, style);
            } else {
                env->ExceptionClear();
            }
        }
        env->DeleteLocalRef(entity);
    }
    if (begun) HitboxEndDrawPass();

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
        vigil::game::Runtime::instance().onRenderEntities(env, mcObj, mcClass);
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
                                         kVigilRenderGlobalBytes, kVigilRenderGlobalSize);
    jclass installerClass = LoadInjectedClass(env, classLoader, "VigilHitboxInstaller",
                                              kVigilHitboxInstallerBytes, kVigilHitboxInstallerSize);
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
vigil::log::write("runtime", "[FAIL] RenderGlobal hook install");
    } else {
vigil::log::write("runtime", "[OK] RenderGlobal hook installed");
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
