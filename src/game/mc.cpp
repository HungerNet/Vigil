#include "mc.h"

#include "class_finder.h"
#include "jvm.h"

jmethodID JniResolveInstanceMethod(JNIEnv* env, jclass cls, const char* signature, const char* mcpName, const char* obfName) {
    if (!env || !cls || !signature) return nullptr;
    for (const char* n : {mcpName, obfName}) {
        if (!n || !n[0]) continue;
        jmethodID mid = env->GetMethodID(cls, n, signature);
        if (mid && !env->ExceptionCheck()) return mid;
        env->ExceptionClear();
    }
    return nullptr;
}

jmethodID JniResolveStaticMethod(JNIEnv* env, jclass cls, const char* signature, const char* mcpName, const char* obfName) {
    if (!env || !cls || !signature) return nullptr;
    for (const char* n : {mcpName, obfName}) {
        if (!n || !n[0]) continue;
        jmethodID mid = env->GetStaticMethodID(cls, n, signature);
        if (mid && !env->ExceptionCheck()) return mid;
        env->ExceptionClear();
    }
    return nullptr;
}

jfieldID JniResolveFieldID(JNIEnv* env, jclass cls, const char* signature, const char* mcpName, const char* obfName) {
    if (!env || !cls || !signature) return nullptr;
    for (const char* n : {mcpName, obfName}) {
        if (!n || !n[0]) continue;
        jfieldID fid = env->GetFieldID(cls, n, signature);
        if (fid && !env->ExceptionCheck()) return fid;
        env->ExceptionClear();
    }
    return nullptr;
}

jfieldID JniResolveFieldIDFlex(JNIEnv* env, jclass cls, const char* mcpName, const char* obfName,
                               const char* const* signatures, int signatureCount) {
    if (!env || !cls || !signatures) return nullptr;
    for (int i = 0; i < signatureCount; ++i) {
        if (jfieldID f = JniResolveFieldID(env, cls, signatures[i], mcpName, obfName)) return f;
    }
    return nullptr;
}

jmethodID JniResolveInstanceMethodAny(JNIEnv* env, jclass cls, const char* const* names, const char* const* signatures,
                                      int candidateCount) {
    if (!env || !cls || !names || !signatures) return nullptr;
    for (int i = 0; i < candidateCount; ++i) {
        if (!names[i] || !signatures[i]) continue;
        jmethodID mid = env->GetMethodID(cls, names[i], signatures[i]);
        if (mid && !env->ExceptionCheck()) return mid;
        env->ExceptionClear();
    }
    return nullptr;
}

namespace vigil::game {
namespace {

// Returns the GuiNewChat instance (local ref). Lunar wraps the chat GUI; both shapes work with
// printChatMessage.
jobject chatGui(JNIEnv* env, jobject mc) {
    if (!env || !mc) return nullptr;
    jclass mcClass = env->GetObjectClass(mc);
    static const char* kIngameSigs[] = {"Lnet/minecraft/client/gui/GuiIngame;", "Lavo;"};
    jfieldID fidIngame = JniResolveFieldIDFlex(env, mcClass, "ingameGUI", "q", kIngameSigs, 2);
    env->DeleteLocalRef(mcClass);
    if (!fidIngame) return nullptr;
    jobject ingame = env->GetObjectField(mc, fidIngame);
    if (!ingame || jniFailed(env)) return nullptr;
    jclass ingameClass = env->GetObjectClass(ingame);
    static const char* kNames[] = {"getChatGUI", "d"};
    static const char* kSigs[] = {"()Lnet/minecraft/client/gui/GuiNewChat;", "()Lavt;"};
    jmethodID mid = JniResolveInstanceMethodAny(env, ingameClass, kNames, kSigs, 2);
    jobject chat = mid ? env->CallObjectMethod(ingame, mid) : nullptr;
    if (jniFailed(env)) chat = nullptr;
    env->DeleteLocalRef(ingameClass);
    env->DeleteLocalRef(ingame);
    return chat;
}

bool printComponent(JNIEnv* env, jobject mc, jobject component) {
    jobject chat = chatGui(env, mc);
    if (!chat) return false;
    jclass chatClass = env->GetObjectClass(chat);
    static const char* kNames[] = {"printChatMessage", "a"};
    static const char* kSigs[] = {"(Lnet/minecraft/util/IChatComponent;)V", "(Leu;)V"};
    jmethodID mid = JniResolveInstanceMethodAny(env, chatClass, kNames, kSigs, 2);
    bool ok = false;
    if (mid) {
        env->CallVoidMethod(chat, mid, component);
        ok = !jniFailed(env);
    }
    env->DeleteLocalRef(chatClass);
    env->DeleteLocalRef(chat);
    return ok;
}

}  // namespace

bool printChat(JNIEnv* env, jobject mc, const std::string& legacyFormatted) {
    if (!env || !mc || legacyFormatted.empty()) return false;
    jclass cct = McFindClass(env, "net/minecraft/util/ChatComponentText");
    if (!cct) return false;
    jmethodID ctor = env->GetMethodID(cct, "<init>", "(Ljava/lang/String;)V");
    bool ok = false;
    if (ctor && !jniFailed(env)) {
        jstring js = env->NewStringUTF(legacyFormatted.c_str());
        jobject comp = js ? env->NewObject(cct, ctor, js) : nullptr;
        if (comp && !jniFailed(env)) ok = printComponent(env, mc, comp);
        if (comp) env->DeleteLocalRef(comp);
        if (js) env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(cct);
    return ok;
}

bool printJsonChat(JNIEnv* env, jobject mc, const std::string& json) {
    if (!env || !mc || json.empty()) return false;
    jclass ser = McFindClass(env, "net/minecraft/util/IChatComponent$Serializer");
    if (!ser) return false;
    jmethodID fromJson = JniResolveStaticMethod(env, ser, "(Ljava/lang/String;)Lnet/minecraft/util/IChatComponent;",
                                                "jsonToComponent", nullptr);
    bool ok = false;
    if (fromJson) {
        jstring js = env->NewStringUTF(json.c_str());
        jobject comp = js ? env->CallStaticObjectMethod(ser, fromJson, js) : nullptr;
        if (comp && !jniFailed(env)) ok = printComponent(env, mc, comp);
        else jniFailed(env);
        if (comp) env->DeleteLocalRef(comp);
        if (js) env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(ser);
    return ok;
}

bool sendChatMessage(JNIEnv* env, jobject mc, const std::string& text) {
    if (!env || !mc || text.empty()) return false;
    jclass mcClass = env->GetObjectClass(mc);
    static const char* kSelfSigs[] = {"Lnet/minecraft/client/entity/EntityPlayerSP;"};
    jfieldID fidSelf = JniResolveFieldIDFlex(env, mcClass, "thePlayer", "h", kSelfSigs, 1);
    env->DeleteLocalRef(mcClass);
    if (!fidSelf) return false;
    jobject self = env->GetObjectField(mc, fidSelf);
    if (!self || jniFailed(env)) return false;
    jclass selfClass = env->GetObjectClass(self);
    jmethodID mid = JniResolveInstanceMethod(env, selfClass, "(Ljava/lang/String;)V", "sendChatMessage", "e");
    bool ok = false;
    if (mid) {
        jstring js = env->NewStringUTF(text.c_str());
        env->CallVoidMethod(self, mid, js);
        ok = !jniFailed(env);
        env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(selfClass);
    env->DeleteLocalRef(self);
    return ok;
}

void addToSentHistory(JNIEnv* env, jobject mc, const std::string& text) {
    jobject chat = chatGui(env, mc);
    if (!chat) return;
    jclass chatClass = env->GetObjectClass(chat);
    jmethodID mid = env->GetMethodID(chatClass, "addToSentMessages", "(Ljava/lang/String;)V");
    if (mid && !jniFailed(env)) {
        jstring js = env->NewStringUTF(text.c_str());
        env->CallVoidMethod(chat, mid, js);
        jniFailed(env);
        env->DeleteLocalRef(js);
    } else {
        jniFailed(env);
    }
    env->DeleteLocalRef(chatClass);
    env->DeleteLocalRef(chat);
}

void playSound(JNIEnv* env, jobject mc, const char* soundName, float pitch) {
    if (!env || !mc || !soundName) return;
    jclass mcClass = env->GetObjectClass(mc);
    jmethodID midSh = JniResolveInstanceMethod(env, mcClass, "()Lnet/minecraft/client/audio/SoundHandler;", "getSoundHandler", "W");
    env->DeleteLocalRef(mcClass);
    if (!midSh) return;
    jobject sh = env->CallObjectMethod(mc, midSh);
    if (!sh || jniFailed(env)) return;

    jclass rlClass = McFindClass(env, "net/minecraft/util/ResourceLocation");
    jclass psrClass = McFindClass(env, "net/minecraft/client/audio/PositionedSoundRecord");
    if (rlClass && psrClass) {
        jmethodID rlCtor = env->GetMethodID(rlClass, "<init>", "(Ljava/lang/String;)V");
        jstring jname = env->NewStringUTF(soundName);
        jobject rl = (rlCtor && !jniFailed(env)) ? env->NewObject(rlClass, rlCtor, jname) : nullptr;
        jniFailed(env);
        jobject sound = nullptr;
        if (rl) {
            jmethodID create = JniResolveStaticMethod(
                env, psrClass, "(Lnet/minecraft/util/ResourceLocation;F)Lnet/minecraft/client/audio/PositionedSoundRecord;",
                "create", nullptr);
            if (create) sound = env->CallStaticObjectMethod(psrClass, create, rl, static_cast<jfloat>(pitch));
            if (jniFailed(env)) sound = nullptr;
            if (!sound) {
                jmethodID create1 = JniResolveStaticMethod(
                    env, psrClass, "(Lnet/minecraft/util/ResourceLocation;)Lnet/minecraft/client/audio/PositionedSoundRecord;",
                    "create", nullptr);
                if (create1) sound = env->CallStaticObjectMethod(psrClass, create1, rl);
                if (jniFailed(env)) sound = nullptr;
            }
        }
        if (sound) {
            jclass shClass = env->GetObjectClass(sh);
            jmethodID play = JniResolveInstanceMethod(env, shClass, "(Lnet/minecraft/client/audio/ISound;)V", "playSound", "a");
            if (play) {
                env->CallVoidMethod(sh, play, sound);
                jniFailed(env);
            }
            env->DeleteLocalRef(shClass);
            env->DeleteLocalRef(sound);
        }
        if (rl) env->DeleteLocalRef(rl);
        if (jname) env->DeleteLocalRef(jname);
    }
    if (rlClass) env->DeleteLocalRef(rlClass);
    if (psrClass) env->DeleteLocalRef(psrClass);
    env->DeleteLocalRef(sh);
}

jobject currentScreen(JNIEnv* env, jobject mc) {
    if (!env || !mc) return nullptr;
    static jfieldID fid = nullptr;
    if (!fid) {
        jclass mcClass = env->GetObjectClass(mc);
        static const char* kSigs[] = {"Lnet/minecraft/client/gui/GuiScreen;"};
        fid = JniResolveFieldIDFlex(env, mcClass, "currentScreen", "m", kSigs, 1);
        env->DeleteLocalRef(mcClass);
        if (!fid) return nullptr;
    }
    jobject s = env->GetObjectField(mc, fid);
    if (jniFailed(env)) return nullptr;
    return s;
}

bool displayScreen(JNIEnv* env, jobject mc, jobject screen) {
    if (!env || !mc) return false;
    static jmethodID mid = nullptr;
    if (!mid) {
        jclass mcClass = env->GetObjectClass(mc);
        mid = JniResolveInstanceMethod(env, mcClass, "(Lnet/minecraft/client/gui/GuiScreen;)V", "displayGuiScreen", "a");
        env->DeleteLocalRef(mcClass);
        if (!mid) return false;
    }
    env->CallVoidMethod(mc, mid, screen);
    return !jniFailed(env);
}

jobject newBlankScreen(JNIEnv* env) {
    jclass cls = McFindClass(env, "net/minecraft/client/gui/GuiScreen");
    if (!cls) return nullptr;
    jmethodID ctor = env->GetMethodID(cls, "<init>", "()V");
    jobject s = (ctor && !jniFailed(env)) ? env->NewObject(cls, ctor) : nullptr;
    if (jniFailed(env)) s = nullptr;
    env->DeleteLocalRef(cls);
    return s;
}

bool isChatScreen(JNIEnv* env, jobject screen) {
    if (!env || !screen) return false;
    jclass chat = McFindClass(env, "net/minecraft/client/gui/GuiChat");
    if (!chat) return false;
    const bool is = env->IsInstanceOf(screen, chat) == JNI_TRUE;
    env->DeleteLocalRef(chat);
    return is;
}

std::string chatInputText(JNIEnv* env, jobject chatScreen) {
    if (!env || !chatScreen) return {};
    jclass chat = McFindClass(env, "net/minecraft/client/gui/GuiChat");
    if (!chat) return {};
    static const char* kFieldSigs[] = {"Lnet/minecraft/client/gui/GuiTextField;", "Lavw;"};
    jfieldID fid = JniResolveFieldIDFlex(env, chat, "inputField", "a", kFieldSigs, 2);
    std::string out;
    if (fid && !jniFailed(env)) {
        jobject field = env->GetObjectField(chatScreen, fid);
        if (field && !jniFailed(env)) {
            jclass tf = env->GetObjectClass(field);
            static const char* kTextNames[] = {"getText", "b"};
            static const char* kTextSigs[] = {"()Ljava/lang/String;", "()Ljava/lang/String;"};
            jmethodID getText = JniResolveInstanceMethodAny(env, tf, kTextNames, kTextSigs, 2);
            if (getText && !jniFailed(env)) {
                jstring js = static_cast<jstring>(env->CallObjectMethod(field, getText));
                if (js && !jniFailed(env)) out = jstringToUtf8(env, js);
                if (js) env->DeleteLocalRef(js);
            }
            env->DeleteLocalRef(tf);
            env->DeleteLocalRef(field);
        }
    }
    env->DeleteLocalRef(chat);
    return out;
}

}  // namespace vigil::game
