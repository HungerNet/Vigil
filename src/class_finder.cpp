#include "class_finder.h"
#include "class_finder_log.h"
#include "globals.h"

#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

static std::mutex g_classCacheMu;
static std::unordered_map<std::string, jclass> g_classCacheGlobal;
static jobject g_mcLoaderGlobal = nullptr;

static bool JniOk(JNIEnv* env) {
    return env && !env->ExceptionCheck();
}

static void JniClear(JNIEnv* env) {
    if (env) env->ExceptionClear();
}

static jobject LoadClassViaLoader(JNIEnv* env, jobject loader, const char* slashPath) {
    if (!env || !loader || !slashPath) return nullptr;
    jclass loaderClass = env->GetObjectClass(loader);
    if (!loaderClass) return nullptr;
    jmethodID midLoad = env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    env->DeleteLocalRef(loaderClass);
    if (!midLoad || env->ExceptionCheck()) {
        JniClear(env);
        return nullptr;
    }
    std::string dotPath = slashPath;
    for (char& c : dotPath) {
        if (c == '/') c = '.';
    }
    jstring jname = env->NewStringUTF(dotPath.c_str());
    if (!jname) return nullptr;
    jobject clsObj = env->CallObjectMethod(loader, midLoad, jname);
    env->DeleteLocalRef(jname);
    if (!JniOk(env)) {
        JniClear(env);
        return nullptr;
    }
    return clsObj;
}

static jobject FindClientThread(JNIEnv* env) {
    jclass threadCls = env->FindClass("java/lang/Thread");
    if (!threadCls) return nullptr;
    jmethodID midAll = env->GetStaticMethodID(threadCls, "getAllStackTraces", "()Ljava/util/Map;");
    if (!midAll) {
        env->DeleteLocalRef(threadCls);
        return nullptr;
    }
    jobject map = env->CallStaticObjectMethod(threadCls, midAll);
    if (!JniOk(env) || !map) {
        JniClear(env);
        env->DeleteLocalRef(threadCls);
        return nullptr;
    }

    jclass mapCls = env->GetObjectClass(map);
    jmethodID midKeySet = env->GetMethodID(mapCls, "keySet", "()Ljava/util/Set;");
    env->DeleteLocalRef(mapCls);
    if (!midKeySet) {
        env->DeleteLocalRef(map);
        env->DeleteLocalRef(threadCls);
        return nullptr;
    }
    jobject keySet = env->CallObjectMethod(map, midKeySet);
    env->DeleteLocalRef(map);
    if (!JniOk(env) || !keySet) {
        JniClear(env);
        env->DeleteLocalRef(threadCls);
        return nullptr;
    }

    jclass setCls = env->GetObjectClass(keySet);
    jmethodID midIterator = env->GetMethodID(setCls, "iterator", "()Ljava/util/Iterator;");
    env->DeleteLocalRef(setCls);
    if (!midIterator) {
        env->DeleteLocalRef(keySet);
        env->DeleteLocalRef(threadCls);
        return nullptr;
    }
    jobject it = env->CallObjectMethod(keySet, midIterator);
    env->DeleteLocalRef(keySet);
    if (!JniOk(env) || !it) {
        JniClear(env);
        env->DeleteLocalRef(threadCls);
        return nullptr;
    }

    jclass itCls = env->GetObjectClass(it);
    jmethodID midHasNext = env->GetMethodID(itCls, "hasNext", "()Z");
    jmethodID midNext = env->GetMethodID(itCls, "next", "()Ljava/lang/Object;");
    jmethodID midGetName = env->GetMethodID(threadCls, "getName", "()Ljava/lang/String;");
    env->DeleteLocalRef(itCls);

    jobject found = nullptr;
    if (midHasNext && midNext && midGetName) {
        while (env->CallBooleanMethod(it, midHasNext)) {
            if (!JniOk(env)) break;
            jobject thread = env->CallObjectMethod(it, midNext);
            if (!thread || env->ExceptionCheck()) {
                JniClear(env);
                break;
            }
            jstring jn = (jstring)env->CallObjectMethod(thread, midGetName);
            if (jn && !env->ExceptionCheck()) {
                const char* name = env->GetStringUTFChars(jn, nullptr);
                const bool match = name && (
                    strcmp(name, "Client thread") == 0 ||
                    strcmp(name, "Render thread") == 0 ||
                    strstr(name, "Client") != nullptr
                );
                if (name) env->ReleaseStringUTFChars(jn, name);
                if (match) {
                    found = thread;
                    env->DeleteLocalRef(jn);
                    break;
                }
            }
            JniClear(env);
            if (jn) env->DeleteLocalRef(jn);
            env->DeleteLocalRef(thread);
        }
    }

    env->DeleteLocalRef(it);
    env->DeleteLocalRef(threadCls);
    return found;
}

jobject LunarResolveClassLoader(JNIEnv* env) {
    if (g_mcLoaderGlobal) return g_mcLoaderGlobal;
    if (g_injectClassLoaderGlobal) {
        g_mcLoaderGlobal = env->NewGlobalRef(g_injectClassLoaderGlobal);
        return g_mcLoaderGlobal;
    }

    jobject thread = FindClientThread(env);
    if (!thread) return nullptr;

    jclass threadCls = env->GetObjectClass(thread);
    jmethodID midCtx = env->GetMethodID(threadCls, "getContextClassLoader", "()Ljava/lang/ClassLoader;");
    env->DeleteLocalRef(threadCls);
    jobject loader = nullptr;
    if (midCtx) {
        loader = env->CallObjectMethod(thread, midCtx);
        if (!JniOk(env)) {
            JniClear(env);
            loader = nullptr;
        }
    }
    env->DeleteLocalRef(thread);

    if (!loader && g_mcClassGlobal && g_midGetMinecraftStatic) {
        jobject mc = env->CallStaticObjectMethod(g_mcClassGlobal, g_midGetMinecraftStatic);
        if (mc && !env->ExceptionCheck()) {
            jclass objCls = env->FindClass("java/lang/Object");
            jclass clsCls = env->FindClass("java/lang/Class");
            if (objCls && clsCls) {
                jmethodID midGetClass = env->GetMethodID(objCls, "getClass", "()Ljava/lang/Class;");
                jmethodID midGetLoader = env->GetMethodID(clsCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
                if (midGetClass && midGetLoader) {
                    jobject mcClsObj = env->CallObjectMethod(mc, midGetClass);
                    if (mcClsObj) {
                        loader = env->CallObjectMethod(mcClsObj, midGetLoader);
                        env->DeleteLocalRef(mcClsObj);
                    }
                }
            }
            if (objCls) env->DeleteLocalRef(objCls);
            if (clsCls) env->DeleteLocalRef(clsCls);
        }
        JniClear(env);
        if (mc) env->DeleteLocalRef(mc);
    }

    if (loader) {
        g_mcLoaderGlobal = env->NewGlobalRef(loader);
        if (!g_injectClassLoaderGlobal) {
            g_injectClassLoaderGlobal = env->NewGlobalRef(loader);
        }
        env->DeleteLocalRef(loader);
    }
    return g_mcLoaderGlobal;
}

jfieldID LunarGetFieldIDFlexible(JNIEnv* env, jclass cls, const char* name,
                                 const char* const* signatures, int signatureCount) {
    if (!env || !cls || !name || !signatures || signatureCount <= 0) return nullptr;
    for (int i = 0; i < signatureCount; ++i) {
        const char* sig = signatures[i];
        if (!sig) continue;
        jfieldID fid = env->GetFieldID(cls, name, sig);
        if (fid && !env->ExceptionCheck()) return fid;
        env->ExceptionClear();
    }
    ClassFinderLogMemberMiss("field", "?", name, signatures[0]);
    return nullptr;
}

jmethodID LunarGetMethodIDFlexible(JNIEnv* env, jclass cls, const char* name,
                                     const char* const* signatures, int signatureCount) {
    if (!env || !cls || !name || !signatures || signatureCount <= 0) return nullptr;
    for (int i = 0; i < signatureCount; ++i) {
        const char* sig = signatures[i];
        if (!sig) continue;
        jmethodID mid = env->GetMethodID(cls, name, sig);
        if (mid && !env->ExceptionCheck()) return mid;
        env->ExceptionClear();
    }
    ClassFinderLogMemberMiss("method", "?", name, signatures[0]);
    return nullptr;
}

jmethodID LunarGetStaticMethodIDFlexible(JNIEnv* env, jclass cls, const char* name,
                                         const char* const* signatures, int signatureCount) {
    if (!env || !cls || !name || !signatures || signatureCount <= 0) return nullptr;
    for (int i = 0; i < signatureCount; ++i) {
        const char* sig = signatures[i];
        if (!sig) continue;
        jmethodID mid = env->GetStaticMethodID(cls, name, sig);
        if (mid && !env->ExceptionCheck()) return mid;
        env->ExceptionClear();
    }
    return nullptr;
}

jclass LunarFindClass(JNIEnv* env, const char* slashPath) {
    if (!env || !slashPath) return nullptr;

    const std::string key = slashPath;
    {
        std::lock_guard<std::mutex> lk(g_classCacheMu);
        auto it = g_classCacheGlobal.find(key);
        if (it != g_classCacheGlobal.end() && it->second) {
            return (jclass)env->NewLocalRef(it->second);
        }
    }

    jclass loaded = nullptr;
    std::string via;
    jobject loader = LunarResolveClassLoader(env);
    if (loader) {
        jobject clsObj = LoadClassViaLoader(env, loader, slashPath);
        if (clsObj) {
            loaded = (jclass)clsObj;
            via = "ClassLoader";
        } else {
            JniClear(env);
            via = "ClassLoader (miss)";
        }
    }

    if (!loaded) {
        jclass direct = env->FindClass(slashPath);
        if (direct && !env->ExceptionCheck()) {
            loaded = direct;
            if (via.empty()) via = "FindClass";
            else via += "+FindClass";
        } else {
            JniClear(env);
        }
    }

    if (via.empty()) via = "none";

    ClassFinderLogClassResolve(slashPath, via.c_str(), loaded != nullptr);

    if (!loaded) return nullptr;

    jclass global = (jclass)env->NewGlobalRef(loaded);
    env->DeleteLocalRef(loaded);
    if (!global) return nullptr;

    {
        std::lock_guard<std::mutex> lk(g_classCacheMu);
        g_classCacheGlobal[key] = global;
    }
    return (jclass)env->NewLocalRef(global);
}

void LunarClassFinderShutdown(JNIEnv* env) {
    ClassFinderLogLine("ClassFinder shutdown");
    if (env) {
        std::lock_guard<std::mutex> lk(g_classCacheMu);
        for (auto& pr : g_classCacheGlobal) {
            if (pr.second) env->DeleteGlobalRef(pr.second);
        }
        g_classCacheGlobal.clear();
    }
    if (env && g_mcLoaderGlobal) {
        env->DeleteGlobalRef(g_mcLoaderGlobal);
        g_mcLoaderGlobal = nullptr;
    }
}
