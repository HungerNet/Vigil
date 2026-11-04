#include "jni_helpers.h"
#ifdef VIGIL_LUNAR
#include "class_finder_log.h"
#endif
#include "class_finder.h"
#include <GL/gl.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>

static bool g_suppressChatHook = false;

void SetSuppressChatHook(bool suppress) {
    g_suppressChatHook = suppress;
}

bool IsChatHookSuppressed() {
    return g_suppressChatHook;
}

jmethodID JniResolveInstanceMethod(JNIEnv* env, jclass cls, const char* signature,
                                   const char* mcpName, const char* obfName) {
    if (!env || !cls || !signature) return nullptr;
    if (mcpName && mcpName[0]) {
        jmethodID mid = env->GetMethodID(cls, mcpName, signature);
        if (mid && !env->ExceptionCheck()) return mid;
        env->ExceptionClear();
    }
    if (obfName && obfName[0]) {
        jmethodID mid = env->GetMethodID(cls, obfName, signature);
        if (mid && !env->ExceptionCheck()) return mid;
        env->ExceptionClear();
    }
    return nullptr;
}

jmethodID JniResolveStaticMethod(JNIEnv* env, jclass cls, const char* signature,
                                 const char* mcpName, const char* obfName) {
    if (!env || !cls || !signature) return nullptr;
    if (mcpName && mcpName[0]) {
        jmethodID mid = env->GetStaticMethodID(cls, mcpName, signature);
        if (mid && !env->ExceptionCheck()) return mid;
        env->ExceptionClear();
    }
    if (obfName && obfName[0]) {
        jmethodID mid = env->GetStaticMethodID(cls, obfName, signature);
        if (mid && !env->ExceptionCheck()) return mid;
        env->ExceptionClear();
    }
    return nullptr;
}

jfieldID JniResolveFieldID(JNIEnv* env, jclass cls, const char* signature,
                           const char* mcpName, const char* obfName) {
    if (!env || !cls || !signature) return nullptr;
    if (mcpName && mcpName[0]) {
        jfieldID fid = env->GetFieldID(cls, mcpName, signature);
        if (fid && !env->ExceptionCheck()) return fid;
        env->ExceptionClear();
    }
    if (obfName && obfName[0]) {
        jfieldID fid = env->GetFieldID(cls, obfName, signature);
        if (fid && !env->ExceptionCheck()) return fid;
        env->ExceptionClear();
    }
    return nullptr;
}

jfieldID JniResolveFieldIDFlex(JNIEnv* env, jclass cls,
                               const char* mcpName, const char* obfName,
                               const char* const* signatures, int signatureCount) {
    if (!env || !cls || !signatures || signatureCount <= 0) return nullptr;
    for (int i = 0; i < signatureCount; ++i) {
        const char* sig = signatures[i];
        if (!sig) continue;
        if (mcpName && mcpName[0]) {
            jfieldID fid = env->GetFieldID(cls, mcpName, sig);
            if (fid && !env->ExceptionCheck()) return fid;
            env->ExceptionClear();
        }
        if (obfName && obfName[0]) {
            jfieldID fid = env->GetFieldID(cls, obfName, sig);
            if (fid && !env->ExceptionCheck()) return fid;
            env->ExceptionClear();
        }
    }
    return nullptr;
}

void JniLogMiss(const char* kind, const char* owner, const char* name, const char* signature) {
#ifdef VIGIL_LUNAR
    ClassFinderLogMemberMiss(kind, owner, name, signature);
#else
    (void)kind;
    (void)owner;
    (void)name;
    (void)signature;
#endif
}

jmethodID JniResolveInstanceMethodAny(JNIEnv* env, jclass cls,
                                      const char* const* names, const char* const* signatures,
                                      int candidateCount) {
    if (!env || !cls || !names || !signatures || candidateCount <= 0) return nullptr;
    for (int i = 0; i < candidateCount; ++i) {
        if (!names[i] || !signatures[i]) continue;
        jmethodID mid = env->GetMethodID(cls, names[i], signatures[i]);
        if (mid && !env->ExceptionCheck()) return mid;
        env->ExceptionClear();
    }
    return nullptr;
}

jobject ResolveChatGuiDelegate(JNIEnv* env, jobject chatGui) {
    if (!env || !chatGui) return chatGui;

    jclass cls = env->GetObjectClass(chatGui);
    if (!cls || env->ExceptionCheck()) {
        env->ExceptionClear();
        return chatGui;
    }

    static const char* kDelegateSigs[] = {
        "Lnet/minecraft/client/gui/GuiNewChat;",
        "Lavt;",
    };
    jfieldID fidDelegate = JniResolveFieldIDFlex(env, cls, "delegate", "delegate", kDelegateSigs, 2);
    if (!fidDelegate) {
        env->DeleteLocalRef(cls);
        return chatGui;
    }

    jobject inner = env->GetObjectField(chatGui, fidDelegate);
    env->DeleteLocalRef(cls);
    if (!inner || env->ExceptionCheck()) {
        env->ExceptionClear();
        return chatGui;
    }

    env->DeleteLocalRef(chatGui);
    return inner;
}

static bool LunarResolveChatGui(JNIEnv* env, jobject mcObj, jclass mcClass,
                                jobject& outIngameGui, jclass& outIngameGuiClass,
                                jobject& outChatGui) {
    outIngameGui = nullptr;
    outIngameGuiClass = nullptr;
    outChatGui = nullptr;
    if (!env || !mcObj || !mcClass) return false;

    static const char* kIngameSigs[] = {
        "Lnet/minecraft/client/gui/GuiIngame;",
        "Lavo;",
    };
    jfieldID fidIngameGui = JniResolveFieldIDFlex(env, mcClass, "ingameGUI", "q", kIngameSigs, 2);
    if (!fidIngameGui) return false;

    jobject ingameGui = env->GetObjectField(mcObj, fidIngameGui);
    if (!ingameGui || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jclass ingameGuiClass = env->GetObjectClass(ingameGui);
    static const char* kGetChatNames[] = { "getChatGUI", "d" };
    static const char* kGetChatSigs[] = {
        "()Lnet/minecraft/client/gui/GuiNewChat;",
        "()Lavt;",
    };
    jmethodID midGetChatGui = JniResolveInstanceMethodAny(env, ingameGuiClass, kGetChatNames, kGetChatSigs, 2);
    if (!midGetChatGui) {
        env->DeleteLocalRef(ingameGuiClass);
        env->DeleteLocalRef(ingameGui);
        return false;
    }

    jobject chatGui = env->CallObjectMethod(ingameGui, midGetChatGui);
    if (!chatGui || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(ingameGuiClass);
        env->DeleteLocalRef(ingameGui);
        return false;
    }

    outIngameGui = ingameGui;
    outIngameGuiClass = ingameGuiClass;
    outChatGui = chatGui;
    return true;
}

static jmethodID LunarResolvePrintChat(JNIEnv* env, jclass chatGuiClass) {
    if (!env || !chatGuiClass) return nullptr;
    static const char* kNames[] = { "printChatMessage", "a" };
    static const char* kSigs[] = {
        "(Lnet/minecraft/util/IChatComponent;)V",
        "(Leu;)V",
    };
    return JniResolveInstanceMethodAny(env, chatGuiClass, kNames, kSigs, 2);
}

bool ReadGuiBool(JNIEnv* env, jclass guiClass, const char* name, bool fallback) {
    if (!guiClass || !name) return fallback;
    jfieldID fid = env->GetStaticFieldID(guiClass, name, "Z");
    if (!fid || env->ExceptionCheck()) {
        env->ExceptionClear();
        return fallback;
    }
    return env->GetStaticBooleanField(guiClass, fid);
}

std::string ReadGuiString(JNIEnv* env, jclass guiClass, const char* name, const std::string& fallback) {
    if (!guiClass || !name) return fallback;
    jfieldID fid = env->GetStaticFieldID(guiClass, name, "Ljava/lang/String;");
    if (!fid || env->ExceptionCheck()) {
        env->ExceptionClear();
        return fallback;
    }
    jstring js = (jstring)env->GetStaticObjectField(guiClass, fid);
    if (!js || env->ExceptionCheck()) {
        env->ExceptionClear();
        return fallback;
    }
    const char* c = env->GetStringUTFChars(js, nullptr);
    std::string out = c ? c : fallback;
    if (c) env->ReleaseStringUTFChars(js, c);
    env->DeleteLocalRef(js);
    return out;
}

void WriteTogglesToFile(JNIEnv* env, jclass guiClass) {
    if (g_dllDir.empty()) return;
    auto readBool = [&](const char* name) -> bool {
        if (!guiClass) return false;
        jfieldID fid = env->GetStaticFieldID(guiClass, name, "Z");
        if (!fid) { env->ExceptionClear(); return false; }
        return env->GetStaticBooleanField(guiClass, fid);
    };
    auto readString = [&](const char* name) -> std::string {
        if (!guiClass) return "";
        jfieldID fid = env->GetStaticFieldID(guiClass, name, "Ljava/lang/String;");
        if (!fid) { env->ExceptionClear(); return ""; }
        jstring js = (jstring)env->GetStaticObjectField(guiClass, fid);
        if (!js) return "";
        const char* chars = env->GetStringUTFChars(js, nullptr);
        std::string out = chars ? chars : "";
        if (chars) env->ReleaseStringUTFChars(js, chars);
        env->DeleteLocalRef(js);
        return out;
    };
    auto jsonEscape = [](const std::string& s) -> std::string {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            if      (c == '\\') out += "\\\\";
            else if (c == '"')  out += "\\\"";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else out += c;
        }
        return out;
    };

    bool autoWho            = readBool("autoWho");
    bool partyDetector      = readBool("partyDetector");
    bool partyQueueDetector = readBool("partyQueueDetector");
    bool bedDisconnectTimer = readBool("bedDisconnectTimer");

    bool numberDenicker     = readBool("numberDenicker");
    bool autoGL             = readBool("autoGL");
    bool fastBuy            = readBool("fastBuy");
    bool coloredHitboxes    = readBool("coloredHitboxes");
    bool dragonHitboxes     = readBool("dragonHitboxes");
    bool fireballHitboxes   = readBool("fireballHitboxes");
    std::string fireballHitboxColor = readString("fireballHitboxColor");
    if (fireballHitboxColor.empty()) fireballHitboxColor = "c";
    bool arrowDistance      = readBool("arrowDistance");
    bool tradeIndicator     = readBool("tradeIndicator");
    bool george             = readBool("george");
    bool nickQol            = readBool("nickQol");
    bool tabOverlay         = readBool("tabOverlay");
    bool overlayResourceHeader = readBool("overlayResourceHeader");
    bool overlayResourceDiamonds = readBool("overlayResourceDiamonds");
    bool overlayResourceEmeralds = readBool("overlayResourceEmeralds");
    std::string overlayResourceLocation = readString("overlayResourceLocation");
    if (overlayResourceLocation.empty()) overlayResourceLocation = "tab";
    bool anticheat          = readBool("anticheat");
    bool ac_flagsound       = readBool("ac_flagsound");
    bool ac_autoblock       = readBool("ac_autoblock");
    bool ac_legitscaff      = readBool("ac_legitscaff");
    bool ac_scaffold        = readBool("ac_scaffold");
    bool ac_scaffoldb       = readBool("ac_scaffoldb");
    bool ac_noslow          = readBool("ac_noslow");
    bool compactBlacklist   = readBool("compactBlacklist");
    bool chatOverlay        = readBool("chatOverlay");
    bool ignoreTeammates    = readBool("ignoreTeammates");
    bool sessionStats       = readBool("sessionStats");
    bool sessionStatsGame   = readBool("sessionStatsGame");
    bool sessionStatsFinalKills = readBool("sessionStatsFinalKills");
    bool sessionStatsBeds   = readBool("sessionStatsBeds");
    bool sessionStatsKills  = readBool("sessionStatsKills");
    bool sessionStatsWins   = readBool("sessionStatsWins");
    bool sessionStatsWinstreak = readBool("sessionStatsWinstreak");
    bool sessionStatsSessionGames = readBool("sessionStatsSessionGames");
    bool sessionStatsGameTime = readBool("sessionStatsGameTime");
    bool sessionStatsAvgTime = readBool("sessionStatsAvgTime");
    bool sessionStatsSessionTime = readBool("sessionStatsSessionTime");
    bool sessionStatsQueueEta = readBool("sessionStatsQueueEta");
    bool sessionStatsSlumberTickets = readBool("sessionStatsSlumberTickets");
    bool sessionStatsTextShadow = readBool("sessionStatsTextShadow");
    std::string apiHypixel     = readString("api_hypixel");
    std::string apiSeraph      = readString("api_seraph");
    std::string apiAurora      = readString("api_aurora");
    bool vegaProxy             = readBool("vegaProxy");
    std::string glMessage      = readString("gl_message");
    std::string chatOverlayMinStars = readString("chatOverlayMinStars");
    std::string chatOverlayMinFkdr = readString("chatOverlayMinFkdr");
    std::string chatOverlayMinSwKdr = readString("chatOverlayMinSwKdr");
    if (chatOverlayMinStars.empty()) chatOverlayMinStars = "100";
    if (chatOverlayMinFkdr.empty()) chatOverlayMinFkdr = "5";
    if (chatOverlayMinSwKdr.empty()) chatOverlayMinSwKdr = "1.5";
    std::string configThemeColor = readString("configThemeColor");
    if (configThemeColor.empty()) configThemeColor = "1A2227";
    std::string configTextColor = readString("configTextColor");
    if (configTextColor.empty()) configTextColor = "D6DEE4";
    int overlayOpacity = 60;
    if (guiClass) {
        jfieldID fidOverlayOpacity = env->GetStaticFieldID(guiClass, "overlayOpacity", "I");
        if (fidOverlayOpacity && !env->ExceptionCheck()) {
            overlayOpacity = (int)env->GetStaticIntField(guiClass, fidOverlayOpacity);
        } else {
            env->ExceptionClear();
        }
    }
    if (overlayOpacity < 0) overlayOpacity = 0;
    if (overlayOpacity > 95) overlayOpacity = 95;
    int hitboxThickness = 1;
    if (guiClass) {
        jfieldID fidHitboxThickness = env->GetStaticFieldID(guiClass, "hitboxThickness", "I");
        if (fidHitboxThickness && !env->ExceptionCheck()) {
            hitboxThickness = (int)env->GetStaticIntField(guiClass, fidHitboxThickness);
        } else {
            env->ExceptionClear();
        }
    }

    if (hitboxThickness < 1) hitboxThickness = 1;
    if (hitboxThickness > 10) hitboxThickness = 10;
    int sessionStatsBgOpacity = 100;
    int sessionStatsScale = 100;
    int sessionStatsPosX = 1000;
    int sessionStatsPosY = 1000;
    int tradeIndicatorPosX = 520;
    int tradeIndicatorPosY = 470;
    int hkAutoWho = 0, hkPartyDetector = 0, hkNumberDenicker = 0, hkAutoGL = 0, hkFastBuy = 0;
    int hkColoredHitboxes = 0, hkDragonHitboxes = 0, hkArrowDistance = 0;
    int hkBedDisconnectTimer = 0, hkTradeIndicator = 0, hkFireballHitboxes = 0;
    int hkGeorge = 0, hkNickQol = 0, selectedTheme = 1;
    if (guiClass) {
        jfieldID fidSessionBg = env->GetStaticFieldID(guiClass, "sessionStatsBgOpacity", "I");
        if (fidSessionBg && !env->ExceptionCheck()) {
            sessionStatsBgOpacity = (int)env->GetStaticIntField(guiClass, fidSessionBg);
        } else {
            env->ExceptionClear();
        }
        jfieldID fidSessionScale = env->GetStaticFieldID(guiClass, "sessionStatsScale", "I");
        if (fidSessionScale && !env->ExceptionCheck()) {
            sessionStatsScale = (int)env->GetStaticIntField(guiClass, fidSessionScale);
        } else {
            env->ExceptionClear();
        }
        jfieldID fidSessionPosX = env->GetStaticFieldID(guiClass, "sessionStatsPosX", "I");
        if (fidSessionPosX && !env->ExceptionCheck()) {
            sessionStatsPosX = (int)env->GetStaticIntField(guiClass, fidSessionPosX);
        } else {
            env->ExceptionClear();
        }
        jfieldID fidSessionPosY = env->GetStaticFieldID(guiClass, "sessionStatsPosY", "I");
        if (fidSessionPosY && !env->ExceptionCheck()) {
            sessionStatsPosY = (int)env->GetStaticIntField(guiClass, fidSessionPosY);
        } else {
            env->ExceptionClear();
        }
        auto readIntField = [&](const char* name, int& out) {
            jfieldID fid = env->GetStaticFieldID(guiClass, name, "I");
            if (fid && !env->ExceptionCheck()) {
                out = (int)env->GetStaticIntField(guiClass, fid);
            } else {
                env->ExceptionClear();
            }
        };
        readIntField("tradeIndicatorPosX", tradeIndicatorPosX);
        readIntField("tradeIndicatorPosY", tradeIndicatorPosY);
        readIntField("hkAutoWho", hkAutoWho);
        readIntField("hkPartyDetector", hkPartyDetector);
        readIntField("hkNumberDenicker", hkNumberDenicker);
        readIntField("hkAutoGL", hkAutoGL);
        readIntField("hkFastBuy", hkFastBuy);
        readIntField("hkColoredHitboxes", hkColoredHitboxes);
        readIntField("hkDragonHitboxes", hkDragonHitboxes);
        readIntField("hkArrowDistance", hkArrowDistance);
        readIntField("hkBedDisconnectTimer", hkBedDisconnectTimer);
        readIntField("hkTradeIndicator", hkTradeIndicator);
        readIntField("hkFireballHitboxes", hkFireballHitboxes);
        readIntField("hkGeorge", hkGeorge);
        readIntField("hkNickQol", hkNickQol);
        readIntField("selectedTheme", selectedTheme);
    }
    if (sessionStatsBgOpacity < 0) sessionStatsBgOpacity = 0;
    if (sessionStatsBgOpacity > 255) sessionStatsBgOpacity = 255;
    if (sessionStatsScale < 50) sessionStatsScale = 50;
    if (sessionStatsScale > 150) sessionStatsScale = 150;
    if (sessionStatsPosX < 0) sessionStatsPosX = 0;
    if (sessionStatsPosX > 1000) sessionStatsPosX = 1000;
    if (sessionStatsPosY < 0) sessionStatsPosY = 0;
    if (sessionStatsPosY > 1000) sessionStatsPosY = 1000;
    if (tradeIndicatorPosX < 0) tradeIndicatorPosX = 0;
    if (tradeIndicatorPosX > 1000) tradeIndicatorPosX = 1000;
    if (tradeIndicatorPosY < 0) tradeIndicatorPosY = 0;
    if (tradeIndicatorPosY > 1000) tradeIndicatorPosY = 1000;
    std::string overlayThemeColor = readString("overlayThemeColor");
    if (overlayThemeColor.empty()) overlayThemeColor = "0F1316";
    std::string overlayColumnsConfig;
    std::string overlayColumnsBedwars;
    std::string overlayColumnsSkywars;
    std::string overlayColumnsDuel;
    std::string overlayColumnsBedwarsduels;
    std::string overlayColumnsMurdermystery;
    std::string overlayGamemodeEdit = "bedwars";
    std::string overlayGamemodeActive = "unknown";
    if (guiClass) {
        jmethodID midSnap = env->GetStaticMethodID(guiClass, "snapshotOverlayColumnsEditMode", "()V");
        if (midSnap && !env->ExceptionCheck()) {
            env->CallStaticVoidMethod(guiClass, midSnap);
            if (env->ExceptionCheck()) env->ExceptionClear();
        } else {
            env->ExceptionClear();
        }

        auto readStaticString = [&](const char* methodName, std::string& out) {
            jmethodID mid = env->GetStaticMethodID(guiClass, methodName, "()Ljava/lang/String;");
            if (!mid || env->ExceptionCheck()) {
                env->ExceptionClear();
                return;
            }
            jstring js = (jstring)env->CallStaticObjectMethod(guiClass, mid);
            if (!env->ExceptionCheck() && js) {
                const char* chars = env->GetStringUTFChars(js, nullptr);
                out = chars ? chars : "";
                if (chars) env->ReleaseStringUTFChars(js, chars);
                env->DeleteLocalRef(js);
            } else {
                env->ExceptionClear();
            }
        };

        readStaticString("getOverlayColumnsConfig", overlayColumnsConfig);
        readStaticString("getOverlayColumnsBedwars", overlayColumnsBedwars);
        readStaticString("getOverlayColumnsSkywars", overlayColumnsSkywars);
        readStaticString("getOverlayColumnsDuel", overlayColumnsDuel);
        readStaticString("getOverlayColumnsBedwarsduels", overlayColumnsBedwarsduels);
        readStaticString("getOverlayColumnsMurdermystery", overlayColumnsMurdermystery);

        jfieldID fidEdit = env->GetStaticFieldID(guiClass, "overlayGamemodeEdit", "Ljava/lang/String;");
        if (fidEdit && !env->ExceptionCheck()) {
            jstring jEdit = (jstring)env->GetStaticObjectField(guiClass, fidEdit);
            if (!env->ExceptionCheck() && jEdit) {
                const char* chars = env->GetStringUTFChars(jEdit, nullptr);
                if (chars && chars[0]) overlayGamemodeEdit = chars;
                if (chars) env->ReleaseStringUTFChars(jEdit, chars);
                env->DeleteLocalRef(jEdit);
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }

        jfieldID fidActive = env->GetStaticFieldID(guiClass, "overlayGamemodeActive", "Ljava/lang/String;");
        if (fidActive && !env->ExceptionCheck()) {
            jstring jActive = (jstring)env->GetStaticObjectField(guiClass, fidActive);
            if (!env->ExceptionCheck() && jActive) {
                const char* chars = env->GetStringUTFChars(jActive, nullptr);
                if (chars && chars[0]) overlayGamemodeActive = chars;
                if (chars) env->ReleaseStringUTFChars(jActive, chars);
                env->DeleteLocalRef(jActive);
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }
    }

    std::string filePath = g_dllDir + "\\toggles.json";
    std::ofstream fout(filePath);
    if (fout.is_open()) {
        fout << "{";
        fout << "\"autoWho\":"          << (autoWho          ? "true" : "false") << ",";
        fout << "\"partyDetector\":"    << (partyDetector    ? "true" : "false") << ",";
        fout << "\"partyQueueDetector\":" << (partyQueueDetector ? "true" : "false") << ",";
        fout << "\"bedDisconnectTimer\":" << (bedDisconnectTimer ? "true" : "false") << ",";

        fout << "\"numberDenicker\":"   << (numberDenicker   ? "true" : "false") << ",";
        fout << "\"autoglEnabled\":"    << (autoGL           ? "true" : "false") << ",";
        fout << "\"fastBuy\":"          << (fastBuy          ? "true" : "false") << ",";
        fout << "\"coloredHitboxes\":"  << (coloredHitboxes  ? "true" : "false") << ",";
        fout << "\"dragonHitboxes\":"   << (dragonHitboxes   ? "true" : "false") << ",";
        fout << "\"fireballHitboxes\":" << (fireballHitboxes ? "true" : "false") << ",";
        fout << "\"fireballHitboxColor\":\"" << jsonEscape(fireballHitboxColor) << "\",";
        fout << "\"hitboxThickness\":"  << hitboxThickness << ",";
        fout << "\"arrowDistance\":"    << (arrowDistance    ? "true" : "false") << ",";
        fout << "\"tradeIndicator\":"   << (tradeIndicator   ? "true" : "false") << ",";
        fout << "\"george\":"           << (george           ? "true" : "false") << ",";
        fout << "\"nickQol\":"          << (nickQol          ? "true" : "false") << ",";
        fout << "\"tradeIndicatorPosX\":" << tradeIndicatorPosX << ",";
        fout << "\"tradeIndicatorPosY\":" << tradeIndicatorPosY << ",";
        fout << "\"hkAutoWho\":" << hkAutoWho << ",";
        fout << "\"hkPartyDetector\":" << hkPartyDetector << ",";
        fout << "\"hkNumberDenicker\":" << hkNumberDenicker << ",";
        fout << "\"hkAutoGL\":" << hkAutoGL << ",";
        fout << "\"hkFastBuy\":" << hkFastBuy << ",";
        fout << "\"hkColoredHitboxes\":" << hkColoredHitboxes << ",";
        fout << "\"hkDragonHitboxes\":" << hkDragonHitboxes << ",";
        fout << "\"hkArrowDistance\":" << hkArrowDistance << ",";
        fout << "\"hkBedDisconnectTimer\":" << hkBedDisconnectTimer << ",";
        fout << "\"hkTradeIndicator\":" << hkTradeIndicator << ",";
        fout << "\"hkFireballHitboxes\":" << hkFireballHitboxes << ",";
        fout << "\"hkGeorge\":" << hkGeorge << ",";
        fout << "\"hkNickQol\":" << hkNickQol << ",";
        fout << "\"selectedTheme\":" << selectedTheme << ",";
        fout << "\"tabOverlay\":"       << (tabOverlay       ? "true" : "false") << ",";
        fout << "\"overlayResourceHeader\":" << (overlayResourceHeader ? "true" : "false") << ",";
        fout << "\"overlayResourceDiamonds\":" << (overlayResourceDiamonds ? "true" : "false") << ",";
        fout << "\"overlayResourceEmeralds\":" << (overlayResourceEmeralds ? "true" : "false") << ",";
        fout << "\"overlayResourceLocation\":\"" << jsonEscape(overlayResourceLocation) << "\",";
        fout << "\"anticheat\":"        << (anticheat        ? "true" : "false") << ",";
        fout << "\"ac_flagsound\":"     << (ac_flagsound     ? "true" : "false") << ",";
        fout << "\"ac_autoblock\":"     << (ac_autoblock     ? "true" : "false") << ",";
        fout << "\"ac_legitscaff\":"    << (ac_legitscaff    ? "true" : "false") << ",";
        fout << "\"ac_scaffold\":"      << (ac_scaffold      ? "true" : "false") << ",";
        fout << "\"ac_scaffoldb\":"     << (ac_scaffoldb     ? "true" : "false") << ",";
        fout << "\"ac_noslow\":"        << (ac_noslow        ? "true" : "false") << ",";
        fout << "\"compactBlacklist\":"  << (compactBlacklist  ? "true" : "false") << ",";
        fout << "\"chatOverlay\":"       << (chatOverlay       ? "true" : "false") << ",";
        fout << "\"ignoreTeammates\":"   << (ignoreTeammates   ? "true" : "false") << ",";
        fout << "\"sessionStats\":"      << (sessionStats      ? "true" : "false") << ",";
        fout << "\"sessionStatsGame\":"  << (sessionStatsGame  ? "true" : "false") << ",";
        fout << "\"sessionStatsFinalKills\":" << (sessionStatsFinalKills ? "true" : "false") << ",";
        fout << "\"sessionStatsBeds\":"  << (sessionStatsBeds  ? "true" : "false") << ",";
        fout << "\"sessionStatsKills\":" << (sessionStatsKills ? "true" : "false") << ",";
        fout << "\"sessionStatsWins\":"  << (sessionStatsWins  ? "true" : "false") << ",";
        fout << "\"sessionStatsWinstreak\":" << (sessionStatsWinstreak ? "true" : "false") << ",";
        fout << "\"sessionStatsSessionGames\":" << (sessionStatsSessionGames ? "true" : "false") << ",";
        fout << "\"sessionStatsGameTime\":" << (sessionStatsGameTime ? "true" : "false") << ",";
        fout << "\"sessionStatsAvgTime\":" << (sessionStatsAvgTime ? "true" : "false") << ",";
        fout << "\"sessionStatsSessionTime\":" << (sessionStatsSessionTime ? "true" : "false") << ",";
        fout << "\"sessionStatsQueueEta\":" << (sessionStatsQueueEta ? "true" : "false") << ",";
        fout << "\"sessionStatsSlumberTickets\":" << (sessionStatsSlumberTickets ? "true" : "false") << ",";
        fout << "\"sessionStatsTextShadow\":" << (sessionStatsTextShadow ? "true" : "false") << ",";
        fout << "\"sessionStatsBgOpacity\":" << sessionStatsBgOpacity << ",";
        fout << "\"sessionStatsScale\":" << sessionStatsScale << ",";
        fout << "\"sessionStatsPosX\":" << sessionStatsPosX << ",";
        fout << "\"sessionStatsPosY\":" << sessionStatsPosY << ",";
        fout << "\"chatOverlayMinStars\":\"" << jsonEscape(chatOverlayMinStars) << "\",";
        fout << "\"chatOverlayMinFkdr\":\"" << jsonEscape(chatOverlayMinFkdr) << "\",";
        fout << "\"chatOverlayMinSwKdr\":\"" << jsonEscape(chatOverlayMinSwKdr) << "\",";
        fout << "\"api_hypixel\":\""    << jsonEscape(apiHypixel)    << "\",";
        fout << "\"api_seraph\":\""     << jsonEscape(apiSeraph)     << "\",";
        fout << "\"api_aurora\":\""     << jsonEscape(apiAurora)     << "\",";
        fout << "\"vegaProxy\":"        << (vegaProxy ? "true" : "false") << ",";
        fout << "\"glMessage\":\""      << jsonEscape(glMessage)     << "\",";
        fout << "\"configThemeColor\":\"" << jsonEscape(configThemeColor) << "\",";
        fout << "\"configTextColor\":\"" << jsonEscape(configTextColor) << "\",";
        fout << "\"overlayOpacity\":"    << overlayOpacity << ",";
        fout << "\"overlayThemeColor\":\"" << jsonEscape(overlayThemeColor) << "\",";
        fout << "\"overlayColumnsConfig\":\"" << jsonEscape(overlayColumnsConfig) << "\",";
        fout << "\"overlayColumnsBedwars\":\"" << jsonEscape(overlayColumnsBedwars) << "\",";
        fout << "\"overlayColumnsSkywars\":\"" << jsonEscape(overlayColumnsSkywars) << "\",";
        fout << "\"overlayColumnsDuel\":\"" << jsonEscape(overlayColumnsDuel) << "\",";
        fout << "\"overlayColumnsBedwarsduels\":\"" << jsonEscape(overlayColumnsBedwarsduels) << "\",";
        fout << "\"overlayColumnsMurdermystery\":\"" << jsonEscape(overlayColumnsMurdermystery) << "\",";
        fout << "\"overlayGamemodeEdit\":\"" << jsonEscape(overlayGamemodeEdit) << "\",";
        fout << "\"overlayGamemodeActive\":\"" << jsonEscape(overlayGamemodeActive) << "\"";
        fout << "}";
    }
}

bool RemoveLatestChatLine(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (!env || !mcObj || !mcClass) return false;

    jobject ingameGui = nullptr;
    jclass ingameGuiClass = nullptr;
    jobject chatGui = nullptr;
    if (!LunarResolveChatGui(env, mcObj, mcClass, ingameGui, ingameGuiClass, chatGui)) return false;

    jobject chatStorage = ResolveChatGuiDelegate(env, chatGui);

    jclass chatGuiClass = env->GetObjectClass(chatStorage);
    auto removeIndex0 = [&](jfieldID fidList) -> bool {
        if (!fidList || env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }
        jobject listObj = env->GetObjectField(chatStorage, fidList);
        if (!listObj || env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }
        jclass listClass = env->GetObjectClass(listObj);
        jmethodID midSize = listClass ? env->GetMethodID(listClass, "size", "()I") : nullptr;
        jmethodID midRemove = listClass ? env->GetMethodID(listClass, "remove", "(I)Ljava/lang/Object;") : nullptr;
        bool ok = false;
        if (midSize && midRemove && !env->ExceptionCheck()) {
            const int size = env->CallIntMethod(listObj, midSize);
            if (!env->ExceptionCheck() && size > 0) {
                jobject removedLine = env->CallObjectMethod(listObj, midRemove, 0);
                if (removedLine) {
                    env->DeleteLocalRef(removedLine);
                    ok = true;
                }
                if (env->ExceptionCheck()) env->ExceptionClear();
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }
        if (listClass) env->DeleteLocalRef(listClass);
        env->DeleteLocalRef(listObj);
        return ok;
    };

    jfieldID fidChatLines = chatGuiClass
        ? JniResolveFieldID(env, chatGuiClass, "Ljava/util/List;", "chatLines", "h") : nullptr;
    jfieldID fidDrawnLines = chatGuiClass
        ? JniResolveFieldID(env, chatGuiClass, "Ljava/util/List;", "drawnChatLines", "i") : nullptr;
    bool removed = removeIndex0(fidChatLines);
    if (removeIndex0(fidDrawnLines)) removed = true;
    env->DeleteLocalRef(chatGuiClass);
    env->DeleteLocalRef(chatStorage);
    env->DeleteLocalRef(ingameGuiClass);
    env->DeleteLocalRef(ingameGui);
    return removed;
}

bool PrintLocalChat(JNIEnv* env, jobject mcObj, jclass mcClass, const std::string& message) {
    if (!env || !mcObj || !mcClass || message.empty()) return false;

    jobject ingameGui = nullptr;
    jclass ingameGuiClass = nullptr;
    jobject chatGui = nullptr;
    if (!LunarResolveChatGui(env, mcObj, mcClass, ingameGui, ingameGuiClass, chatGui)) return false;

    jclass chatGuiClass = env->GetObjectClass(chatGui);
    jmethodID midPrintChat = LunarResolvePrintChat(env, chatGuiClass);

    bool ok = false;
    if (midPrintChat) {
        jclass cctClass = McFindClass(env, "net/minecraft/util/ChatComponentText");
        if (!cctClass) cctClass = env->FindClass("fa");
        if (cctClass && !env->ExceptionCheck()) {
            jmethodID cctCtor = env->GetMethodID(cctClass, "<init>", "(Ljava/lang/String;)V");
            if (cctCtor && !env->ExceptionCheck()) {
                jstring jsMsg = env->NewStringUTF(message.c_str());
                if (jsMsg) {
                    jobject chatComp = env->NewObject(cctClass, cctCtor, jsMsg);
                    if (chatComp && !env->ExceptionCheck()) {
                        SetSuppressChatHook(true);
                        env->CallVoidMethod(chatGui, midPrintChat, chatComp);
                        ok = !env->ExceptionCheck();
                        if (!ok) env->ExceptionClear();
                        SetSuppressChatHook(false);
                        env->DeleteLocalRef(chatComp);
                    } else { env->ExceptionClear(); }
                    env->DeleteLocalRef(jsMsg);
                } else { env->ExceptionClear(); }
            } else { env->ExceptionClear(); }
            env->DeleteLocalRef(cctClass);
        } else { env->ExceptionClear(); }
    } else { env->ExceptionClear(); }

    env->DeleteLocalRef(chatGuiClass);
    env->DeleteLocalRef(chatGui);
    env->DeleteLocalRef(ingameGuiClass);
    env->DeleteLocalRef(ingameGui);
    return ok;
}

void PrintChatComponent(JNIEnv* env, jobject mcObj, jclass mcClass, jobject chatComponent) {
    if (!env || !mcObj || !mcClass || !chatComponent) return;

    jobject ingameGui = nullptr;
    jclass ingameGuiClass = nullptr;
    jobject chatGui = nullptr;
    if (!LunarResolveChatGui(env, mcObj, mcClass, ingameGui, ingameGuiClass, chatGui)) return;

    jclass chatGuiClass = env->GetObjectClass(chatGui);
    jmethodID midPrintChat = LunarResolvePrintChat(env, chatGuiClass);
    if (midPrintChat && !env->ExceptionCheck()) {
        SetSuppressChatHook(true);
        env->CallVoidMethod(chatGui, midPrintChat, chatComponent);
        SetSuppressChatHook(false);
        if (env->ExceptionCheck()) env->ExceptionClear();
    } else {
        env->ExceptionClear();
    }

    env->DeleteLocalRef(chatGuiClass);
    env->DeleteLocalRef(chatGui);
    env->DeleteLocalRef(ingameGuiClass);
    env->DeleteLocalRef(ingameGui);
}

bool PrintJsonChat(JNIEnv* env, jobject mcObj, jclass mcClass, const std::string& json) {
    if (!env || !mcObj || !mcClass || json.empty()) return false;

    jclass serClass = McFindClass(env, "net/minecraft/util/IChatComponent$Serializer");
    if (!serClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        serClass = env->FindClass("eu$a");
        if (!serClass || env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }
    }

    jmethodID midFromJson = env->GetStaticMethodID(serClass, "jsonToComponent",
        "(Ljava/lang/String;)Lnet/minecraft/util/IChatComponent;");
    if (!midFromJson || env->ExceptionCheck()) {
        env->ExceptionClear();
        midFromJson = env->GetStaticMethodID(serClass, "a", "(Ljava/lang/String;)Leu;");
        if (!midFromJson || env->ExceptionCheck()) {
            env->ExceptionClear();
            midFromJson = nullptr;
        }
    }
    if (!midFromJson) {
        env->DeleteLocalRef(serClass);
        return false;
    }

    jstring jsJson = env->NewStringUTF(json.c_str());
    if (!jsJson) { env->DeleteLocalRef(serClass); return false; }
    jobject chatComp = env->CallStaticObjectMethod(serClass, midFromJson, jsJson);
    env->DeleteLocalRef(jsJson);
    env->DeleteLocalRef(serClass);
    if (!chatComp || env->ExceptionCheck()) { env->ExceptionClear(); return false; }

    PrintChatComponent(env, mcObj, mcClass, chatComp);
    env->DeleteLocalRef(chatComp);
    return true;
}

static void PlayResourceSound(JNIEnv* env, jobject mcObj, jclass mcClass,
                              const char* soundName, float pitch) {
    if (!env || !mcObj || !mcClass || !soundName || !soundName[0]) return;

    jmethodID midGetSH = JniResolveInstanceMethod(env, mcClass,
        "()Lnet/minecraft/client/audio/SoundHandler;", "getSoundHandler", "W");
    if (!midGetSH) {
        midGetSH = env->GetMethodID(mcClass, "W", "()Lbpz;");
        if (!midGetSH || env->ExceptionCheck()) { env->ExceptionClear(); return; }
    }
    jobject soundHandler = env->CallObjectMethod(mcObj, midGetSH);
    if (!soundHandler || env->ExceptionCheck()) { env->ExceptionClear(); return; }

    jclass rlClass = McFindClass(env, "net/minecraft/util/ResourceLocation");
    if (!rlClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        rlClass = env->FindClass("jy");
        if (!rlClass || env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(soundHandler);
            return;
        }
    }
    jmethodID rlCtor = env->GetMethodID(rlClass, "<init>", "(Ljava/lang/String;)V");
    if (!rlCtor || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(rlClass);
        env->DeleteLocalRef(soundHandler);
        return;
    }
    jstring jsSoundName = env->NewStringUTF(soundName);
    if (!jsSoundName) { env->DeleteLocalRef(rlClass); env->DeleteLocalRef(soundHandler); return; }
    jobject rl = env->NewObject(rlClass, rlCtor, jsSoundName);
    env->DeleteLocalRef(jsSoundName);
    env->DeleteLocalRef(rlClass);
    if (!rl || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(soundHandler); return; }

    jclass psrClass = McFindClass(env, "net/minecraft/client/audio/PositionedSoundRecord");
    if (!psrClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        psrClass = env->FindClass("bpf");
        if (!psrClass || env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(rl);
            env->DeleteLocalRef(soundHandler);
            return;
        }
    }

    jobject sound = nullptr;
    if (pitch > 0.0f) {
        jmethodID midCreatePitched = env->GetStaticMethodID(psrClass, "createRecordSoundAtPosition",
            "(Lnet/minecraft/util/ResourceLocation;F)Lnet/minecraft/client/audio/PositionedSoundRecord;");
        if (!midCreatePitched || env->ExceptionCheck()) {
            env->ExceptionClear();
            // 1.8.9 MCP: create(ResourceLocation, float) — verify; fallback obf.
            midCreatePitched = env->GetStaticMethodID(psrClass, "create",
                "(Lnet/minecraft/util/ResourceLocation;F)Lnet/minecraft/client/audio/PositionedSoundRecord;");
            if (!midCreatePitched || env->ExceptionCheck()) {
                env->ExceptionClear();
                midCreatePitched = env->GetStaticMethodID(psrClass, "a", "(Ljy;F)Lbpf;");
                if (!midCreatePitched || env->ExceptionCheck()) {
                    env->ExceptionClear();
                    midCreatePitched = nullptr;
                }
            }
        }
        if (midCreatePitched) {
            sound = env->CallStaticObjectMethod(psrClass, midCreatePitched, rl, pitch);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                sound = nullptr;
            }
        }
    }
    if (!sound) {
        jmethodID midCreate = env->GetStaticMethodID(psrClass, "create",
            "(Lnet/minecraft/util/ResourceLocation;)Lnet/minecraft/client/audio/PositionedSoundRecord;");
        if (!midCreate || env->ExceptionCheck()) {
            env->ExceptionClear();
            midCreate = env->GetStaticMethodID(psrClass, "a", "(Ljy;)Lbpf;");
            if (!midCreate || env->ExceptionCheck()) {
                env->ExceptionClear();
                env->DeleteLocalRef(psrClass);
                env->DeleteLocalRef(rl);
                env->DeleteLocalRef(soundHandler);
                return;
            }
        }
        sound = env->CallStaticObjectMethod(psrClass, midCreate, rl);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(psrClass);
            env->DeleteLocalRef(rl);
            env->DeleteLocalRef(soundHandler);
            return;
        }
    }

    env->DeleteLocalRef(psrClass);
    env->DeleteLocalRef(rl);
    if (!sound) { env->DeleteLocalRef(soundHandler); return; }

    jclass shClass = env->GetObjectClass(soundHandler);
    jmethodID midPlay = JniResolveInstanceMethod(env, shClass,
        "(Lnet/minecraft/client/audio/ISound;)V", "playSound", "a");
    if (!midPlay) {
        midPlay = env->GetMethodID(shClass, "a", "(Lbpj;)V");
        if (env->ExceptionCheck()) { env->ExceptionClear(); midPlay = nullptr; }
    }
    env->DeleteLocalRef(shClass);
    if (midPlay) {
        env->CallVoidMethod(soundHandler, midPlay, sound);
        if (env->ExceptionCheck()) env->ExceptionClear();
    } else {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(sound);
    env->DeleteLocalRef(soundHandler);
}

void PlayAlertSound(JNIEnv* env, jobject mcObj, jclass mcClass) {
    PlayResourceSound(env, mcObj, mcClass, "random.orb", 0.0f);
}

void PlayBlacklistPlingSound(JNIEnv* env, jobject mcObj, jclass mcClass) {
    // note.harp = classic note block "pling" (1.8.9)
    PlayResourceSound(env, mcObj, mcClass, "note.harp", 1.92f);
}

namespace {

static std::atomic<ULONGLONG> g_injectToastStartAt{0};
static std::mutex g_toastTextMu;
static std::string g_toastHeadline = "\xC2\xA7""l\xC2\xA7""aClient injected";
static std::string g_toastDetail = "\xC2\xA7""7Vigil is ready";

constexpr ULONGLONG kInjectToastDurationMs = 5500;
constexpr ULONGLONG kInjectToastFadeInMs = 300;
constexpr ULONGLONG kInjectToastFadeOutMs = 900;
constexpr float kInjectToastTextScale = 2.0f;
constexpr float kInjectToastDetailScale = 1.6f;
constexpr int kInjectToastMinBoxW = 320;
constexpr int kInjectToastCornerMargin = 20;

struct InjectToastJni {
    jclass mcClass = nullptr;
    jmethodID midGetMc = nullptr;
    jfieldID fidDisplayWidth = nullptr;
    jfieldID fidDisplayHeight = nullptr;
    jfieldID fidFontRenderer = nullptr;
    jmethodID midFontWidth = nullptr;
    jmethodID midDrawStringShadow = nullptr;
    bool resolved = false;
    bool ok = false;
};

static InjectToastJni& InjectToastCache() {
    static InjectToastJni cache;
    return cache;
}

static bool ResolveInjectToastJni(JNIEnv* env) {
    auto& cache = InjectToastCache();
    if (cache.resolved) return cache.ok;
    cache.resolved = true;
    if (!env) return false;

    jclass mcClass = McFindClass(env, "net/minecraft/client/Minecraft");
    if (!mcClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jmethodID midGetMc = env->GetStaticMethodID(mcClass, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
    jfieldID fidDisplayWidth = env->GetFieldID(mcClass, "displayWidth", "I");
    jfieldID fidDisplayHeight = env->GetFieldID(mcClass, "displayHeight", "I");
    jfieldID fidFontRenderer = env->GetFieldID(mcClass, "fontRendererObj", "Lnet/minecraft/client/gui/FontRenderer;");
    if (!midGetMc || !fidDisplayWidth || !fidDisplayHeight || !fidFontRenderer || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(mcClass);
        return false;
    }

    jclass fontClass = McFindClass(env, "net/minecraft/client/gui/FontRenderer");
    if (!fontClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(mcClass);
        return false;
    }

    jmethodID midFontWidth = env->GetMethodID(fontClass, "getStringWidth", "(Ljava/lang/String;)I");
    jmethodID midDrawStringShadow = env->GetMethodID(fontClass, "drawStringWithShadow", "(Ljava/lang/String;FFI)I");
    if (!midFontWidth || !midDrawStringShadow || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(fontClass);
        env->DeleteLocalRef(mcClass);
        return false;
    }
    env->DeleteLocalRef(fontClass);

    cache.mcClass = (jclass)env->NewGlobalRef(mcClass);
    cache.midGetMc = midGetMc;
    cache.fidDisplayWidth = fidDisplayWidth;
    cache.fidDisplayHeight = fidDisplayHeight;
    cache.fidFontRenderer = fidFontRenderer;
    cache.midFontWidth = midFontWidth;
    cache.midDrawStringShadow = midDrawStringShadow;
    cache.ok = cache.mcClass != nullptr;
    env->DeleteLocalRef(mcClass);
    return cache.ok;
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

static void DrawMcString(JNIEnv* env, jobject fontRenderer, jmethodID midDraw, const std::string& text,
                         float x, float y, int color) {
    if (!env || !fontRenderer || !midDraw || text.empty()) return;
    jstring jText = env->NewStringUTF(text.c_str());
    if (!jText) return;
    env->CallIntMethod(fontRenderer, midDraw, jText, x, y, color);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(jText);
}

static int ColorWithAlpha(int rgb, int alpha) {
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;
    return (alpha << 24) | (rgb & 0xFFFFFF);
}

static float InjectToastAlpha(ULONGLONG elapsedMs) {
    if (elapsedMs >= kInjectToastDurationMs) return 0.0f;
    if (elapsedMs < kInjectToastFadeInMs) {
        return (float)elapsedMs / (float)kInjectToastFadeInMs;
    }
    const ULONGLONG fadeStart = kInjectToastDurationMs - kInjectToastFadeOutMs;
    if (elapsedMs > fadeStart) {
        return (float)(kInjectToastDurationMs - elapsedMs) / (float)kInjectToastFadeOutMs;
    }
    return 1.0f;
}

static void DrawMcStringScaled(JNIEnv* env, jobject fontRenderer, jmethodID midDraw,
                               const std::string& text, float x, float y, int color, float scale) {
    if (scale <= 1.01f) {
        DrawMcString(env, fontRenderer, midDraw, text, x, y, color);
        return;
    }
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glScalef(scale, scale, 1.0f);
    DrawMcString(env, fontRenderer, midDraw, text, 0.0f, 0.0f, color);
    glPopMatrix();
}

static int MeasureMcStringScaled(JNIEnv* env, jobject fontRenderer, jmethodID midFontWidth,
                                 const std::string& text, float scale) {
    const int base = MeasureMcString(env, fontRenderer, midFontWidth, text);
    return (int)(base * scale + 0.5f);
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

} // namespace

bool RenderInjectToast(JNIEnv* env) {
    if (!env) return false;

    const ULONGLONG now = GetTickCount64();
    ULONGLONG startAt = g_injectToastStartAt.load(std::memory_order_relaxed);
    if (startAt != 0 && now - startAt >= kInjectToastDurationMs) {
        return true;
    }

    if (!ResolveInjectToastJni(env)) return false;
    auto& cache = InjectToastCache();

    jobject mcObj = env->CallStaticObjectMethod(cache.mcClass, cache.midGetMc);
    if (!mcObj || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    const int screenW = env->GetIntField(mcObj, cache.fidDisplayWidth);
    const int screenH = env->GetIntField(mcObj, cache.fidDisplayHeight);
    jobject fontRenderer = env->GetObjectField(mcObj, cache.fidFontRenderer);
    if (screenW <= 0 || screenH <= 0 || !fontRenderer || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (fontRenderer) env->DeleteLocalRef(fontRenderer);
        env->DeleteLocalRef(mcObj);
        return false;
    }

    if (startAt == 0) {
        g_injectToastStartAt.store(now, std::memory_order_relaxed);
        startAt = now;
    }

    const ULONGLONG elapsed = now - startAt;
    const float alphaF = InjectToastAlpha(elapsed);
    const int alpha = (int)(alphaF * 255.0f);

    static const std::string kTitle =
        "\xC2\xA7""7[\xC2\xA7""1Fre\xC2\xA7""fnch\xC2\xA7""cify\xC2\xA7""7]";
    std::string headline;
    std::string detail;
    {
        std::lock_guard<std::mutex> lk(g_toastTextMu);
        headline = g_toastHeadline;
        detail = g_toastDetail;
    }

    const int titleW = MeasureMcStringScaled(env, fontRenderer, cache.midFontWidth, kTitle, kInjectToastTextScale);
    const int headlineW = MeasureMcStringScaled(env, fontRenderer, cache.midFontWidth, headline, kInjectToastTextScale);
    const int detailW = MeasureMcStringScaled(env, fontRenderer, cache.midFontWidth, detail, kInjectToastDetailScale);
    const int contentW = (std::max)({titleW, headlineW, detailW});
    const int padX = 22;
    const int padY = 18;
    const int accentW = 6;
    const int innerGap = 4;
    const int boxW = (std::max)(kInjectToastMinBoxW, contentW + padX * 2 + accentW + 8);
    const int boxH = padY * 2
        + (int)(10.0f * kInjectToastTextScale)
        + innerGap
        + (int)(10.0f * kInjectToastTextScale)
        + innerGap
        + (int)(10.0f * kInjectToastDetailScale);
    const float boxX = (float)(screenW - boxW - kInjectToastCornerMargin);
    const float boxY = (float)(screenH - boxH - kInjectToastCornerMargin);
    const float shadowOff = 4.0f;

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushMatrix();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)screenW, (double)screenH, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);

    const GLubyte bgAlpha = (GLubyte)(alphaF * 220.0f);
    const GLubyte shadowAlpha = (GLubyte)(alphaF * 120.0f);
    const GLubyte borderAlpha = (GLubyte)(alphaF * 180.0f);

    DrawFilledRect(boxX + shadowOff, boxY + shadowOff, (float)boxW, (float)boxH, 0, 0, 0, shadowAlpha);
    DrawFilledRect(boxX, boxY, (float)boxW, (float)boxH, 18, 20, 28, bgAlpha);
    DrawFilledRect(boxX, boxY, (float)accentW, (float)boxH, 85, 255, 85, (GLubyte)alpha);
    DrawFilledRect(boxX, boxY, (float)boxW, 1.0f, 120, 130, 150, borderAlpha);
    DrawFilledRect(boxX, boxY + (float)boxH - 1.0f, (float)boxW, 1.0f, 50, 55, 70, borderAlpha);
    DrawFilledRect(boxX, boxY, 1.0f, (float)boxH, 50, 55, 70, borderAlpha);
    DrawFilledRect(boxX + (float)boxW - 1.0f, boxY, 1.0f, (float)boxH, 50, 55, 70, borderAlpha);

    glEnable(GL_TEXTURE_2D);

    const float textX = boxX + (float)padX + (float)accentW;
    float textY = boxY + (float)padY;
    DrawMcStringScaled(env, fontRenderer, cache.midDrawStringShadow, kTitle, textX, textY,
                       ColorWithAlpha(0xFFFFFF, alpha), kInjectToastTextScale);
    textY += 10.0f * kInjectToastTextScale + (float)innerGap;
    DrawMcStringScaled(env, fontRenderer, cache.midDrawStringShadow, headline, textX, textY,
                       ColorWithAlpha(0x55FF55, alpha), kInjectToastTextScale);
    textY += 10.0f * kInjectToastTextScale + (float)innerGap;
    DrawMcStringScaled(env, fontRenderer, cache.midDrawStringShadow, detail, textX, textY,
                       ColorWithAlpha(0xAAAAAA, alpha), kInjectToastDetailScale);

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
    return false;
}

void ShowVigilToast(const std::string& headline, const std::string& detail) {
    {
        std::lock_guard<std::mutex> lk(g_toastTextMu);
        g_toastHeadline = headline.empty()
            ? "\xC2\xA7""l\xC2\xA7""aClient injected"
            : ("\xC2\xA7""l\xC2\xA7""a" + headline);
        g_toastDetail = "\xC2\xA7""7" + detail;
    }
    g_injectToastStartAt.store(GetTickCount64(), std::memory_order_relaxed);
}

bool VigilToastIsShowing() {
    const ULONGLONG startAt = g_injectToastStartAt.load(std::memory_order_relaxed);
    if (startAt == 0) return false;
    return (GetTickCount64() - startAt) < kInjectToastDurationMs;
}
