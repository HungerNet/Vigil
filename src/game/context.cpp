#include "context.h"

#include "../core/log.h"
#include "class_finder.h"
#include "jvm.h"
#include "mc.h"
#include "minecraft_util.h"

namespace vigil::game {

using detection::GameContext;

GameContext ContextDetector::classify(const std::string& titleLower, const std::string& lines, std::string& outDetail) {
    outDetail.clear();
    if (titleLower.find("atlas") != std::string::npos) { outDetail = "Atlas"; return GameContext::Replay; }
    if (titleLower.find("replay") != std::string::npos) { outDetail = "Replay"; return GameContext::Replay; }

    auto has = [&](const char* s) { return lines.find(s) != std::string::npos; };
    const bool lobby = has("tokens") || has("lobby:") || has(" lobby") || lines.rfind("lobby", 0) == 0;
    // Bed Wars: team lines ("red:"), Duels: "time left:", SkyWars: "players left:".
    const bool inGame = !lobby && (has("red:") || has(" red ") || has("time left:") || has("players left:") ||
                                   has("opponent:"));
    const bool pregame = !lobby && !inGame && (has("mode") || has("starting in") || has("players:") || has("waiting"));

    std::string title = titleLower;
    for (char& c : title) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    outDetail = Trim(title);
    if (inGame) return GameContext::InGame;
    if (pregame) return GameContext::Pregame;
    if (lobby) return GameContext::Lobby;
    return GameContext::Unknown;
}

void ContextDetector::poll(JNIEnv* env, jobject mc, double nowMs) {
    if (nowMs - lastPollMs_ < 500.0) return;
    lastPollMs_ = nowMs;
    if (!env || !mc) return;

    jclass mcClass = env->GetObjectClass(mc);
    static const char* kSelfSigs[] = {"Lnet/minecraft/client/entity/EntityPlayerSP;"};
    jfieldID fidSelf = JniResolveFieldIDFlex(env, mcClass, "thePlayer", "h", kSelfSigs, 1);
    env->DeleteLocalRef(mcClass);
    jobject self = fidSelf ? env->GetObjectField(mc, fidSelf) : nullptr;
    if (!self || jniFailed(env)) { context_ = GameContext::Unknown; detail_.clear(); return; }

    std::string title;
    std::string lines;
    jclass selfClass = env->GetObjectClass(self);
    jmethodID midScoreboard = env->GetMethodID(selfClass, "getWorldScoreboard", "()Lnet/minecraft/scoreboard/Scoreboard;");
    jobject sb = (midScoreboard && !jniFailed(env)) ? env->CallObjectMethod(self, midScoreboard) : nullptr;
    if (jniFailed(env)) sb = nullptr;
    if (sb) {
        jclass sbClass = env->GetObjectClass(sb);
        jmethodID midSlot = env->GetMethodID(sbClass, "getObjectiveInDisplaySlot", "(I)Lnet/minecraft/scoreboard/ScoreObjective;");
        jmethodID midSorted = env->GetMethodID(sbClass, "getSortedScores", "(Lnet/minecraft/scoreboard/ScoreObjective;)Ljava/util/Collection;");
        jmethodID midTeam = env->GetMethodID(sbClass, "getPlayersTeam", "(Ljava/lang/String;)Lnet/minecraft/scoreboard/ScorePlayerTeam;");
        jniFailed(env);
        jobject objective = midSlot ? env->CallObjectMethod(sb, midSlot, 1) : nullptr;
        if (jniFailed(env)) objective = nullptr;
        if (objective) {
            jclass objClass = env->GetObjectClass(objective);
            jmethodID midName = env->GetMethodID(objClass, "getDisplayName", "()Ljava/lang/String;");
            if (midName && !jniFailed(env)) {
                jstring jt = static_cast<jstring>(env->CallObjectMethod(objective, midName));
                if (jt && !jniFailed(env)) title = StripMinecraftFormatting(jstringToUtf8(env, jt));
                if (jt) env->DeleteLocalRef(jt);
            }
            env->DeleteLocalRef(objClass);

            jclass teamClass = McFindClass(env, "net/minecraft/scoreboard/ScorePlayerTeam");
            jmethodID midFormat = teamClass ? env->GetStaticMethodID(teamClass, "formatPlayerName",
                                                                     "(Lnet/minecraft/scoreboard/Team;Ljava/lang/String;)Ljava/lang/String;")
                                            : nullptr;
            jniFailed(env);
            jobject scores = midSorted ? env->CallObjectMethod(sb, midSorted, objective) : nullptr;
            if (jniFailed(env)) scores = nullptr;
            if (scores) {
                jclass coll = env->GetObjectClass(scores);
                jmethodID toArray = env->GetMethodID(coll, "toArray", "()[Ljava/lang/Object;");
                jobjectArray arr = (toArray && !jniFailed(env)) ? static_cast<jobjectArray>(env->CallObjectMethod(scores, toArray)) : nullptr;
                if (jniFailed(env)) arr = nullptr;
                if (arr) {
                    const jsize n = env->GetArrayLength(arr);
                    for (jsize i = 0; i < n && i < 16; ++i) {
                        jobject score = env->GetObjectArrayElement(arr, i);
                        if (!score) continue;
                        jclass sc = env->GetObjectClass(score);
                        jmethodID midPlayer = env->GetMethodID(sc, "getPlayerName", "()Ljava/lang/String;");
                        if (midPlayer && !jniFailed(env)) {
                            jstring jp = static_cast<jstring>(env->CallObjectMethod(score, midPlayer));
                            if (jp && !jniFailed(env)) {
                                std::string line = jstringToUtf8(env, jp);
                                if (midTeam && midFormat) {
                                    jobject team = env->CallObjectMethod(sb, midTeam, jp);
                                    if (jniFailed(env)) team = nullptr;
                                    jstring jf = static_cast<jstring>(env->CallStaticObjectMethod(teamClass, midFormat, team, jp));
                                    if (jf && !jniFailed(env)) line = jstringToUtf8(env, jf);
                                    else jniFailed(env);
                                    if (jf) env->DeleteLocalRef(jf);
                                    if (team) env->DeleteLocalRef(team);
                                }
                                lines += LowerAscii(StripMinecraftFormatting(line));
                                lines += '\n';
                            }
                            if (jp) env->DeleteLocalRef(jp);
                        }
                        env->DeleteLocalRef(sc);
                        env->DeleteLocalRef(score);
                    }
                    env->DeleteLocalRef(arr);
                }
                env->DeleteLocalRef(coll);
                env->DeleteLocalRef(scores);
            }
            if (teamClass) env->DeleteLocalRef(teamClass);
            env->DeleteLocalRef(objective);
        }
        env->DeleteLocalRef(sbClass);
        env->DeleteLocalRef(sb);
    }
    env->DeleteLocalRef(selfClass);
    env->DeleteLocalRef(self);

    mode_.clear();
    for (std::size_t pos = 0; pos < lines.size();) {
        const std::size_t end = lines.find('\n', pos);
        const std::string line = Trim(lines.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
        if (line.rfind("mode:", 0) == 0) { mode_ = Trim(line.substr(5)); break; }
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    const GameContext before = context_;
    if (title.empty() && lines.empty()) {
        context_ = GameContext::Unknown;
        detail_.clear();
    } else {
        context_ = classify(LowerAscii(title), lines, detail_);
    }
    if (context_ != before) {
        log::writef("runtime", "context: %s (%s)", detection::gameContextName(context_), detail_.c_str());
    }
}

}  // namespace vigil::game
