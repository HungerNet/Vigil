#include "chat.h"

#include "../core/log.h"
#include "class_finder.h"
#include "jvm.h"
#include "mc.h"
#include "minecraft_util.h"

#include <algorithm>

namespace vigil::game {

bool ChatReader::resolve(JNIEnv* env, jobject chatGui) {
    if (resolved_) return true;
    if (resolveFailed_) return false;
    jclass guiClass = env->GetObjectClass(chatGui);
    fLines_ = JniResolveFieldID(env, guiClass, "Ljava/util/List;", "chatLines", "h");
    env->DeleteLocalRef(guiClass);
    jclass list = env->FindClass("java/util/List");
    if (list) {
        mSize_ = env->GetMethodID(list, "size", "()I");
        mGet_ = env->GetMethodID(list, "get", "(I)Ljava/lang/Object;");
        env->DeleteLocalRef(list);
    }
    jniFailed(env);
    if (jclass line = McFindClass(env, "net/minecraft/client/gui/ChatLine")) {
        mComponent_ = JniResolveInstanceMethod(env, line, "()Lnet/minecraft/util/IChatComponent;", "getChatComponent", "a");
        env->DeleteLocalRef(line);
    }
    if (jclass comp = McFindClass(env, "net/minecraft/util/IChatComponent")) {
        mText_ = JniResolveInstanceMethod(env, comp, "()Ljava/lang/String;", "getUnformattedText", "c");
        env->DeleteLocalRef(comp);
    }
    resolved_ = fLines_ && mSize_ && mGet_ && mComponent_ && mText_;
    if (!resolved_) resolveFailed_ = true;
    log::writef("runtime", "chat reader: resolved=%d (lines=%d component=%d text=%d)", resolved_, fLines_ != nullptr,
                mComponent_ != nullptr, mText_ != nullptr);
    return resolved_;
}

void ChatReader::reset(JNIEnv* env) {
    if (env && newest_) env->DeleteWeakGlobalRef(newest_);
    newest_ = nullptr;
    primed_ = false;
}

std::vector<std::string> ChatReader::poll(JNIEnv* env, jobject mc) {
    std::vector<std::string> out;
    if (!env || !mc) return out;
    jobject gui = chatGuiObject(env, mc);
    if (!gui) return out;
    if (!resolve(env, gui)) {
        env->DeleteLocalRef(gui);
        return out;
    }
    jobject lines = env->GetObjectField(gui, fLines_);
    env->DeleteLocalRef(gui);
    if (!lines || jniFailed(env)) return out;
    available_ = true;
    const int n = env->CallIntMethod(lines, mSize_);
    if (jniFailed(env) || n <= 0) {
        env->DeleteLocalRef(lines);
        return out;
    }
    jobject first = env->CallObjectMethod(lines, mGet_, 0);
    if (!first || jniFailed(env)) {
        env->DeleteLocalRef(lines);
        return out;
    }
    const bool unchanged = newest_ && env->IsSameObject(first, newest_);
    if (!unchanged) {
        if (primed_) {
            // Index 0 is the newest line; walk back until the last line we have seen.
            for (int i = 0; i < std::min(n, 60); ++i) {
                jobject line = i == 0 ? env->NewLocalRef(first) : env->CallObjectMethod(lines, mGet_, i);
                if (!line || jniFailed(env)) break;
                const bool seen = newest_ && env->IsSameObject(line, newest_);
                if (seen) {
                    env->DeleteLocalRef(line);
                    break;
                }
                jobject comp = env->CallObjectMethod(line, mComponent_);
                if (comp && !jniFailed(env)) {
                    jstring js = static_cast<jstring>(env->CallObjectMethod(comp, mText_));
                    if (js && !jniFailed(env)) {
                        std::string text = Trim(StripMinecraftFormatting(jstringToUtf8(env, js)));
                        if (!text.empty() && text.rfind("[Vigil]", 0) != 0) out.push_back(std::move(text));
                    }
                    if (js) env->DeleteLocalRef(js);
                    env->DeleteLocalRef(comp);
                }
                env->DeleteLocalRef(line);
            }
            std::reverse(out.begin(), out.end());
            linesRead_ += static_cast<int>(out.size());
        }
        if (newest_) env->DeleteWeakGlobalRef(newest_);
        newest_ = env->NewWeakGlobalRef(first);
        primed_ = true;
    }
    env->DeleteLocalRef(first);
    env->DeleteLocalRef(lines);
    return out;
}

}  // namespace vigil::game
