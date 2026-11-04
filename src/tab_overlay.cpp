#include "tab_overlay.h"
#include "player_api.h"
#include "denick.h"
#include "number_denicker.h"
#include "utils.h"
#include "jni_helpers.h"
#include "globals.h"
#include "class_finder.h"
#include <functional>
#ifdef FRENCHIFY_LUNAR
#include "class_finder_log.h"
#endif
#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <cstdarg>
#include <climits>
#include <cmath>
#include <deque>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <regex>
#include <GL/gl.h>

namespace taboverlay {

static GameResourceStats s_lastGoodResources;
static std::atomic<bool> s_forceResourcesClear{false};
static OverlayController* g_glRestoreController = nullptr;

struct BedDisconnectEntry {
    std::string playerNameLower;
    std::string teamLetter;
    ULONGLONG expiresAtMs = 0;
    bool hiddenByReconnect = false;
};

static std::mutex s_bedDcMu;
static std::vector<BedDisconnectEntry> s_bedDisconnects;
static std::unordered_map<std::string, std::string> s_bedDcNameTeamCache;
static std::unordered_map<std::string, std::string> s_bedDcOriginalSuffixByTeam;
static std::string s_bedDcLocrawMode;
static ULONGLONG s_bedDcLastSidebarLogAt = 0;
static ULONGLONG s_bedDcLastSkipLogAt = 0;

static void BedDcLog(const std::string& msg) {
    (void)msg;
}

// Logged-in account name from Minecraft Session (unchanged by Hypixel /nick).
static std::string g_localAccountName;
static std::string g_localAccountNameLower;

static void RefreshLocalAccountNameFromSession(JNIEnv* env, jobject mcInstance) {
    if (!env || !mcInstance) return;

    jclass mcClass = env->GetObjectClass(mcInstance);
    if (!mcClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    static const char* kSessionSigs[] = {
        "Lnet/minecraft/util/Session;",
        "Lavm;",
    };
    jfieldID fidSession = JniResolveFieldIDFlex(env, mcClass, "session", "ae", kSessionSigs, 2);
    jobject session = nullptr;
    if (fidSession && !env->ExceptionCheck()) {
        session = env->GetObjectField(mcInstance, fidSession);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            session = nullptr;
        }
    } else {
        env->ExceptionClear();
        jmethodID midGetSession = JniResolveInstanceMethod(
            env, mcClass, "()Lnet/minecraft/util/Session;", "getSession", nullptr);
        if (!midGetSession) {
            env->ExceptionClear();
            midGetSession = env->GetMethodID(mcClass, "getSession", "()Lavm;");
        }
        if (midGetSession && !env->ExceptionCheck()) {
            session = env->CallObjectMethod(mcInstance, midGetSession);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                session = nullptr;
            }
        } else {
            env->ExceptionClear();
        }
    }
    env->DeleteLocalRef(mcClass);
    if (!session) return;

    jclass sessionClass = env->GetObjectClass(session);
    jmethodID midGetUsername = sessionClass
        ? JniResolveInstanceMethod(env, sessionClass, "()Ljava/lang/String;", "getUsername", "c")
        : nullptr;
    if (!midGetUsername && sessionClass) {
        env->ExceptionClear();
        midGetUsername = env->GetMethodID(sessionClass, "getUsername", "()Ljava/lang/String;");
    }
    if (midGetUsername && !env->ExceptionCheck()) {
        jstring jName = (jstring)env->CallObjectMethod(session, midGetUsername);
        if (jName && !env->ExceptionCheck()) {
            const char* raw = env->GetStringUTFChars(jName, nullptr);
            const std::string name = Trim(StripMinecraftFormatting(raw ? raw : ""));
            if (raw) env->ReleaseStringUTFChars(jName, raw);
            env->DeleteLocalRef(jName);
            if (!name.empty()) {
                g_localAccountName = name;
                g_localAccountNameLower = LowerAscii(name);
            }
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }
    if (sessionClass) env->DeleteLocalRef(sessionClass);
    env->DeleteLocalRef(session);
}

static bool IsBedDisconnectFeatureContextActive() {
    if (GetOverlayActiveGamemode() == OverlayGamemode::Bedwars) return true;
    if (!s_bedDcLocrawMode.empty()) return true;
    const std::string mode = LowerAscii(g_modeState);
    return mode.find("ingame") != std::string::npos || g_redState == "Ingame";
}

static std::string StripMcFormatting(const std::string& in);
static char TeamLetterFromSidebarYouLine(const std::string& plainLower);
static bool IsFrifyInjectedResourceLine(const std::string& raw);
static bool AcquireSidebarObjective(JNIEnv* env,
                                    jobject mcInstance,
                                    const JniCache& cache,
                                    jobject& outScoreboard,
                                    jobject& outObjective);

static int BedDisconnectThresholdForMode(const std::string& modeRaw) {
    const std::string mode = LowerAscii(modeRaw);
    // Solo / doubles / threes / fours from Hypixel locraw "mode" (e.g. BEDWARS_EIGHT_TWO).
    if (mode.find("eight_one") != std::string::npos || mode.find("solo") != std::string::npos) return 1;
    if (mode.find("eight_two") != std::string::npos || mode.find("double") != std::string::npos) return 2;
    if (mode.find("four_three") != std::string::npos || mode.find("three") != std::string::npos) return 3;
    if (mode.find("four_four") != std::string::npos || mode.find("two_four") != std::string::npos ||
        mode.find("4v4") != std::string::npos) return 4;
    // Keep last known threshold only when mode string is empty — caller should pass last locraw.
    return 1;
}

static void PruneExpiredBedDisconnectsLocked(ULONGLONG now) {
    s_bedDisconnects.erase(
        std::remove_if(s_bedDisconnects.begin(), s_bedDisconnects.end(),
                       [now](const BedDisconnectEntry& e) { return e.expiresAtMs <= now; }),
        s_bedDisconnects.end());
}

static bool TryParseBedwarsPlayerEventName(const std::string& msg,
                                           const std::regex& re,
                                           std::string& outName) {
    const std::string plain = Trim(StripMinecraftFormatting(msg));
    std::smatch m;
    if (std::regex_match(plain, m, re) && m.size() >= 2) {
        outName = m[1].str();
        return true;
    }
    return false;
}

static bool TryParseDisconnectPlayerName(const std::string& msg, std::string& outName) {
    // Exact Hypixel lines like "Larp108 disconnected." — not help text with "get disconnected".
    static const std::regex re(R"(^\s*([A-Za-z0-9_]{2,16}) disconnected\.\s*$)");
    return TryParseBedwarsPlayerEventName(msg, re, outName);
}

static bool TryParseReconnectPlayerName(const std::string& msg, std::string& outName) {
    static const std::regex re(R"(^\s*([A-Za-z0-9_]{2,16}) reconnected\.\s*$)");
    return TryParseBedwarsPlayerEventName(msg, re, outName);
}

static std::string FormatBedDisconnectTimerText(const std::string& teamLetter) {
    if (teamLetter.empty() || teamLetter == "?") return "";
    std::string modeCopy;
    {
        std::lock_guard<std::mutex> lk(s_bedDcMu);
        modeCopy = s_bedDcLocrawMode;
    }
    const int threshold = BedDisconnectThresholdForMode(modeCopy);
    const ULONGLONG now = GetTickCount64();
    int activeCount = 0;
    ULONGLONG minRemainingMs = 0;
    {
        std::lock_guard<std::mutex> lk(s_bedDcMu);
        PruneExpiredBedDisconnectsLocked(now);
        for (const BedDisconnectEntry& e : s_bedDisconnects) {
            if (e.teamLetter != teamLetter) continue;
            if (e.hiddenByReconnect) continue;
            if (e.expiresAtMs <= now) continue;
            ++activeCount;
            const ULONGLONG rem = e.expiresAtMs - now;
            if (minRemainingMs == 0 || rem < minRemainingMs) minRemainingMs = rem;
        }
    }
    if (activeCount < threshold || minRemainingMs == 0) return "";
    const int secLeft = (int)((minRemainingMs + 999) / 1000);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", secLeft / 60, secLeft % 60);
    return buf;
}

static char TeamLetterFromBedwarsSidebarLine(const std::string& plainLower) {
    struct TeamNameLetter { const char* name; char letter; };
    static const TeamNameLetter kTeams[] = {
        {"red", 'R'}, {"blue", 'B'}, {"green", 'G'}, {"yellow", 'Y'},
        {"aqua", 'A'}, {"white", 'W'}, {"pink", 'P'}, {"gray", 'S'}, {"grey", 'S'},
    };
    for (const auto& t : kTeams) {
        if (plainLower.find(t.name) != std::string::npos) return t.letter;
    }
    if (plainLower.size() >= 2 && plainLower[1] == ' ') {
        const char c = plainLower[0];
        if (c >= 'a' && c <= 'z') return (char)std::toupper((unsigned char)c);
    }
    return 0;
}

static bool SidebarLineHasBedAliveCheckmark(const std::string& plain) {
    // Hypixel uses U+2713 (✓). Also accept heavy check / emoji / ballot variants.
    return plain.find("\xe2\x9c\x93") != std::string::npos || // ✓
           plain.find("\xe2\x9c\x94") != std::string::npos || // ✔
           plain.find("\xe2\x9c\x85") != std::string::npos || // ✅
           plain.find("\xe2\x98\x91") != std::string::npos || // ☑
           plain.find("\xe2\x9c\xb3") != std::string::npos || // ✳ (seen on some clients)
           plain.find("[+]") != std::string::npos ||
           plain.find("(✓)") != std::string::npos;
}

static bool SidebarLineHasEliminatedMark(const std::string& plainLower) {
    if (plainLower.find("\xe2\x9c\x97") != std::string::npos ||
        plainLower.find("\xe2\x9c\x98") != std::string::npos ||
        plainLower.find("\xe2\x9d\x8c") != std::string::npos) {
        return true;
    }
    const size_t colon = plainLower.find(':');
    if (colon != std::string::npos && colon + 1 < plainLower.size()) {
        const std::string tail = Trim(plainLower.substr(colon + 1));
        if (tail == "x" || tail == "\xe2\x9c\x97" || tail == "\xe2\x9c\x98") return true;
        // Bed destroyed → player count only (e.g. "red: 2"), no checkmark.
        if (!tail.empty() && tail.find_first_not_of("0123456789") == std::string::npos) return true;
    }
    return false;
}

static char TeamLetterFromSidebarTeamStatusLine(const std::string& plain, const std::string& plainLower) {
    // Prefer alive-bed lines (checkmark). Also accept team name lines that are not eliminated.
    if (SidebarLineHasEliminatedMark(plainLower)) return 0;
    if (SidebarLineHasBedAliveCheckmark(plain)) {
        return TeamLetterFromBedwarsSidebarLine(plainLower);
    }
    // Fallback: "Red" / "R Red" style without unicode check (some Lunar font paths strip it).
    const char letter = TeamLetterFromBedwarsSidebarLine(plainLower);
    if (letter == 0) return 0;
    // Require a team-ish line (name or "r red") and reject pure player-count lines.
    if (plainLower.find(':') != std::string::npos) {
        const size_t colon = plainLower.find(':');
        const std::string tail = Trim(plainLower.substr(colon + 1));
        if (!tail.empty() && tail.find_first_not_of("0123456789") == std::string::npos) return 0;
    }
    return letter;
}

static std::string StripBedDcTimerFromSuffix(const std::string& suffix) {
    static const std::regex re(R"(\s*\xC2\xA7c\s*\d+:\d{2}\s*$)");
    return std::regex_replace(suffix, re, "");
}

struct ScoreboardTeamJni {
    jmethodID getPlayersTeam = nullptr;
    jmethodID formatPlayerName = nullptr;
    jclass teamClass = nullptr;
};

static bool ResolveScoreboardTeamJni(JNIEnv* env, jclass scoreboardClass, ScoreboardTeamJni& out) {
    out = {};
    if (!env || !scoreboardClass) return false;

    static const char* kGetTeamNames[] = {"getPlayersTeam", "h"};
    static const char* kGetTeamSigs[] = {
        "(Ljava/lang/String;)Lnet/minecraft/scoreboard/ScorePlayerTeam;",
        "(Ljava/lang/String;)Lnet/minecraft/scoreboard/Team;",
        "(Ljava/lang/String;)Laul;",
        "(Ljava/lang/String;)Lauq;",
    };
    for (const char* name : kGetTeamNames) {
        for (const char* sig : kGetTeamSigs) {
            out.getPlayersTeam = env->GetMethodID(scoreboardClass, name, sig);
            if (out.getPlayersTeam && !env->ExceptionCheck()) break;
            env->ExceptionClear();
            out.getPlayersTeam = nullptr;
        }
        if (out.getPlayersTeam) break;
    }
    if (!out.getPlayersTeam) return false;

    static const char* kTeamClasses[] = {
        "net/minecraft/scoreboard/ScorePlayerTeam",
        "net/minecraft/scoreboard/Team",
        "aul",
        "auq",
    };
    for (const char* cn : kTeamClasses) {
        out.teamClass = McFindClass(env, cn);
        if (out.teamClass && !env->ExceptionCheck()) break;
        env->ExceptionClear();
        out.teamClass = env->FindClass(cn);
        if (out.teamClass && !env->ExceptionCheck()) break;
        env->ExceptionClear();
        out.teamClass = nullptr;
    }
    if (!out.teamClass) return false;

    static const char* kFormatNames[] = {"formatPlayerName", "a"};
    static const char* kFormatSigs[] = {
        "(Lnet/minecraft/scoreboard/ScorePlayerTeam;Ljava/lang/String;)Ljava/lang/String;",
        "(Lnet/minecraft/scoreboard/Team;Ljava/lang/String;)Ljava/lang/String;",
        "(Laul;Ljava/lang/String;)Ljava/lang/String;",
        "(Lauq;Ljava/lang/String;)Ljava/lang/String;",
    };
    for (const char* name : kFormatNames) {
        for (const char* sig : kFormatSigs) {
            out.formatPlayerName = env->GetStaticMethodID(out.teamClass, name, sig);
            if (out.formatPlayerName && !env->ExceptionCheck()) break;
            env->ExceptionClear();
            out.formatPlayerName = nullptr;
        }
        if (out.formatPlayerName) break;
    }
    return out.getPlayersTeam && out.formatPlayerName && out.teamClass;
}

static void SyncScoreboardBedDisconnectTimers(JNIEnv* env,
                                              jobject mcInstance,
                                              const JniCache& cache) {
    if (!env || !mcInstance || !cache.initialized || !g_customGuiClassGlobal) return;

    const bool toggleOn = ReadGuiStaticBool(env, g_customGuiClassGlobal, "bedDisconnectTimer", false);
    if (!toggleOn) {
        const ULONGLONG now = GetTickCount64();
        if (now - s_bedDcLastSkipLogAt >= 10000) {
            s_bedDcLastSkipLogAt = now;
            BedDcLog("sync skipped: bedDisconnectTimer toggle off");
        }
        return;
    }

    if (!IsBedDisconnectFeatureContextActive()) {
        const ULONGLONG now = GetTickCount64();
        if (now - s_bedDcLastSkipLogAt >= 5000) {
            s_bedDcLastSkipLogAt = now;
            BedDcLog("sync skipped: context inactive gamemode=" +
                     OverlayGamemodeToString(GetOverlayActiveGamemode()) +
                     " locrawMode='" + s_bedDcLocrawMode + "' modeState=" + g_modeState +
                     " redState=" + g_redState);
        }
        return;
    }

    jobject scoreboard = nullptr;
    jobject objective = nullptr;
    if (!AcquireSidebarObjective(env, mcInstance, cache, scoreboard, objective)) {
        const ULONGLONG now = GetTickCount64();
        if (now - s_bedDcLastSkipLogAt >= 5000) {
            s_bedDcLastSkipLogAt = now;
            BedDcLog("sync skipped: sidebar objective unavailable");
        }
        return;
    }

    jclass scoreboardClass = env->GetObjectClass(scoreboard);
    ScoreboardTeamJni teamJni{};
    const bool teamJniOk = ResolveScoreboardTeamJni(env, scoreboardClass, teamJni);
    const jmethodID midGetPlayersTeam = teamJni.getPlayersTeam;
    const jmethodID midFormatPlayerName = teamJni.formatPlayerName;
    jclass scorePlayerTeamClass = teamJni.teamClass;

    jmethodID midGetSortedScores = scoreboardClass
        ? env->GetMethodID(scoreboardClass, "getSortedScores",
                           "(Lnet/minecraft/scoreboard/ScoreObjective;)Ljava/util/Collection;")
        : nullptr;
    if (!midGetSortedScores || env->ExceptionCheck()) {
        env->ExceptionClear();
        midGetSortedScores = scoreboardClass
            ? env->GetMethodID(scoreboardClass, "i", "(Lauk;)Ljava/util/Collection;")
            : nullptr;
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    if (teamJniOk && midGetPlayersTeam && midFormatPlayerName && midGetSortedScores && scorePlayerTeamClass) {
        int sidebarLineCount = 0;
        int teamLineCount = 0;
        int suffixWriteCount = 0;
        std::string sidebarDump;
        jobject scoresCollection = env->CallObjectMethod(scoreboard, midGetSortedScores, objective);
        if (!env->ExceptionCheck() && scoresCollection) {
            jobject scoreIter = env->CallObjectMethod(scoresCollection, cache.midCollectionIterator);
            if (!env->ExceptionCheck() && scoreIter) {
                while (env->CallBooleanMethod(scoreIter, cache.midIteratorHasNext)) {
                    jobject scoreObj = env->CallObjectMethod(scoreIter, cache.midIteratorNext);
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        break;
                    }
                    if (!scoreObj) continue;

                    jclass scoreClass = env->GetObjectClass(scoreObj);
                    jmethodID midScoreName = scoreClass ? env->GetMethodID(scoreClass, "getPlayerName", "()Ljava/lang/String;") : nullptr;
                    if (!midScoreName || env->ExceptionCheck()) {
                        env->ExceptionClear();
                        midScoreName = scoreClass ? env->GetMethodID(scoreClass, "e", "()Ljava/lang/String;") : nullptr;
                        if (env->ExceptionCheck()) env->ExceptionClear();
                    }

                    std::string scoreName;
                    if (midScoreName) {
                        jstring jScoreName = (jstring)env->CallObjectMethod(scoreObj, midScoreName);
                        if (!env->ExceptionCheck() && jScoreName) {
                            const char* raw = env->GetStringUTFChars(jScoreName, nullptr);
                            scoreName = raw ? raw : "";
                            if (raw) env->ReleaseStringUTFChars(jScoreName, raw);
                            env->DeleteLocalRef(jScoreName);
                        } else {
                            env->ExceptionClear();
                        }
                    }

                    if (!scoreName.empty() && scoreName[0] != '#' && !IsFrifyInjectedResourceLine(scoreName)) {
                        jstring jRawName = env->NewStringUTF(scoreName.c_str());
                        if (jRawName) {
                            jobject teamObj = env->CallObjectMethod(scoreboard, midGetPlayersTeam, jRawName);
                            if (env->ExceptionCheck()) {
                                env->ExceptionClear();
                                teamObj = nullptr;
                            }

                            std::string line = scoreName;
                            jstring jFormatted = teamObj
                                ? (jstring)env->CallStaticObjectMethod(scorePlayerTeamClass, midFormatPlayerName, teamObj, jRawName)
                                : nullptr;
                            if (env->ExceptionCheck()) {
                                env->ExceptionClear();
                                jFormatted = nullptr;
                            }
                            if (jFormatted) {
                                const char* fmt = env->GetStringUTFChars(jFormatted, nullptr);
                                line = fmt ? fmt : line;
                                if (fmt) env->ReleaseStringUTFChars(jFormatted, fmt);
                                env->DeleteLocalRef(jFormatted);
                            }

                            const std::string currentSuffix = teamObj ? GetScoreboardTeamSuffix(env, teamObj) : "";
                            const std::string plain = StripMcFormatting(line + currentSuffix);
                            const std::string plainLower = LowerAscii(plain);
                            ++sidebarLineCount;
                            if (!sidebarDump.empty()) sidebarDump += " | ";
                            sidebarDump += scoreName + "=>" + plain;
                            const char teamLetter = TeamLetterFromSidebarTeamStatusLine(plain, plainLower);
                            if (teamLetter != 0 && teamObj) {
                                ++teamLineCount;
                                const std::string teamKey(1, teamLetter);
                                const std::string timer = FormatBedDisconnectTimerText(teamKey);
                                if (s_bedDcOriginalSuffixByTeam.find(teamKey) == s_bedDcOriginalSuffixByTeam.end()) {
                                    s_bedDcOriginalSuffixByTeam[teamKey] = StripBedDcTimerFromSuffix(currentSuffix);
                                    BedDcLog("team " + teamKey + " baseSuffix='" + s_bedDcOriginalSuffixByTeam[teamKey] + "'");
                                }
                                const std::string& baseSuffix = s_bedDcOriginalSuffixByTeam[teamKey];
                                std::string desiredSuffix = baseSuffix;
                                if (!timer.empty()) {
                                    desiredSuffix += std::string(1, (char)0xC2) + (char)0xA7 + "c" + timer;
                                }
                                if (desiredSuffix != currentSuffix) {
                                    const bool ok = SetScoreboardTeamSuffix(env, teamObj, desiredSuffix);
                                    BedDcLog("suffix team=" + teamKey + " timer=" + (timer.empty() ? "-" : timer) +
                                             " ok=" + (ok ? "1" : "0") + " desired='" + desiredSuffix + "'");
                                    if (ok) ++suffixWriteCount;
                                }
                            }

                            if (teamObj) env->DeleteLocalRef(teamObj);
                            env->DeleteLocalRef(jRawName);
                        }
                    }

                    if (scoreClass) env->DeleteLocalRef(scoreClass);
                    env->DeleteLocalRef(scoreObj);
                }
                env->DeleteLocalRef(scoreIter);
            } else {
                env->ExceptionClear();
            }
            env->DeleteLocalRef(scoresCollection);
        } else {
            env->ExceptionClear();
            BedDcLog("sync failed: getSortedScores exception");
        }

        const ULONGLONG now = GetTickCount64();
        if (now - s_bedDcLastSidebarLogAt >= 3000) {
            s_bedDcLastSidebarLogAt = now;
            int activeDc = 0;
            {
                std::lock_guard<std::mutex> lk(s_bedDcMu);
                PruneExpiredBedDisconnectsLocked(now);
                activeDc = (int)s_bedDisconnects.size();
            }
            BedDcLog("sidebar lines=" + std::to_string(sidebarLineCount) +
                     " teamLines=" + std::to_string(teamLineCount) +
                     " suffixWrites=" + std::to_string(suffixWriteCount) +
                     " activeDc=" + std::to_string(activeDc) +
                     " threshold=" + std::to_string(BedDisconnectThresholdForMode(s_bedDcLocrawMode)) +
                     " mode='" + s_bedDcLocrawMode + "'");
            if (!sidebarDump.empty()) BedDcLog("scoreboard: " + sidebarDump);
        }
    } else {
        BedDcLog("sync failed: JNI methods missing getTeam=" + std::to_string(midGetPlayersTeam != nullptr) +
                 " format=" + std::to_string(midFormatPlayerName != nullptr) +
                 " scores=" + std::to_string(midGetSortedScores != nullptr) +
                 " teamClass=" + std::to_string(scorePlayerTeamClass != nullptr) +
                 " resolve=" + std::to_string(teamJniOk));
    }

    if (scorePlayerTeamClass) env->DeleteLocalRef(scorePlayerTeamClass);
    if (scoreboardClass) env->DeleteLocalRef(scoreboardClass);
    env->DeleteLocalRef(objective);
    env->DeleteLocalRef(scoreboard);
}

void SetBedwarsLocrawMode(const std::string& locrawModeRaw) {
    if (locrawModeRaw.empty()) return;
    {
        std::lock_guard<std::mutex> lk(s_bedDcMu);
        s_bedDcLocrawMode = locrawModeRaw;
    }
    BedDcLog("locraw mode='" + locrawModeRaw + "' threshold=" +
             std::to_string(BedDisconnectThresholdForMode(locrawModeRaw)));
}

void UpdateBedDisconnectTeamCache(const std::string& playerName, const std::string& teamLetter) {
    if (playerName.empty() || teamLetter.empty() || teamLetter == "?") return;
    const std::string key = LowerAscii(Trim(StripMinecraftFormatting(playerName)));
    if (key.empty()) return;
    std::string letter = teamLetter.substr(0, 1);
    if (!letter.empty()) letter[0] = (char)std::toupper((unsigned char)letter[0]);

    std::lock_guard<std::mutex> lk(s_bedDcMu);
    const auto prev = s_bedDcNameTeamCache.find(key);
    // White is often missing a color code. Never overwrite a known colored team with W
    // from a no-color false positive (e.g. tab line briefly without § codes).
    if (prev != s_bedDcNameTeamCache.end() && letter == "W" &&
        prev->second != "W" && prev->second != "?") {
        return;
    }
    s_bedDcNameTeamCache[key] = letter;
    if (prev == s_bedDcNameTeamCache.end() || prev->second != letter) {
        BedDcLog("cache " + key + " -> team " + letter);
    }
}

static std::string LookupBedDisconnectTeamForPlayer(const std::string& nameKey) {
    std::lock_guard<std::mutex> lk(s_bedDcMu);
    auto it = s_bedDcNameTeamCache.find(nameKey);
    return it != s_bedDcNameTeamCache.end() ? it->second : "";
}

void OnBedwarsReconnectChatLine(const std::string& cleanMsg) {
    const std::string plain = Trim(StripMinecraftFormatting(cleanMsg));
    BedDcLog("chat: raw='" + cleanMsg + "' plain='" + plain + "'");

    if (!IsBedDisconnectFeatureContextActive()) return;

    std::string playerName;
    if (!TryParseReconnectPlayerName(cleanMsg, playerName)) {
        BedDcLog("reconnect parse failed");
        return;
    }

    const std::string nameKey = LowerAscii(playerName);
    const ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> lk(s_bedDcMu);
    PruneExpiredBedDisconnectsLocked(now);
    bool found = false;
    for (BedDisconnectEntry& e : s_bedDisconnects) {
        if (e.playerNameLower != nameKey || e.expiresAtMs <= now) continue;
        e.hiddenByReconnect = true;
        found = true;
        const int secLeft = (int)((e.expiresAtMs - now + 999) / 1000);
        BedDcLog("reconnect " + playerName + " team=" + e.teamLetter +
                 " timer hidden, continues " + std::to_string(secLeft) + "s in background");
    }
    if (!found) {
        BedDcLog("reconnect " + playerName + ": no active timer");
    }
}

void OnBedwarsDisconnectChatLine(const std::string& cleanMsg) {
    const std::string plain = Trim(StripMinecraftFormatting(cleanMsg));
    BedDcLog("chat: raw='" + cleanMsg + "' plain='" + plain + "'");

    if (!IsBedDisconnectFeatureContextActive()) {
        BedDcLog("disconnect ignored: inactive context gamemode=" +
                 OverlayGamemodeToString(GetOverlayActiveGamemode()) +
                 " locrawMode='" + s_bedDcLocrawMode + "'");
        return;
    }

    std::string playerName;
    if (!TryParseDisconnectPlayerName(cleanMsg, playerName)) {
        BedDcLog("disconnect parse failed");
        return;
    }

    const std::string nameKey = LowerAscii(playerName);
    std::string teamLetter = LookupBedDisconnectTeamForPlayer(nameKey);
    if (teamLetter.empty()) {
        std::string cacheDump;
        {
            std::lock_guard<std::mutex> lk(s_bedDcMu);
            for (const auto& kv : s_bedDcNameTeamCache) {
                if (!cacheDump.empty()) cacheDump += ", ";
                cacheDump += kv.first + ":" + kv.second;
            }
        }
        BedDcLog("disconnect " + playerName + ": team unknown (cache=[" + cacheDump + "])");
        return;
    }

    const ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> lk(s_bedDcMu);
    PruneExpiredBedDisconnectsLocked(now);
    for (BedDisconnectEntry& e : s_bedDisconnects) {
        if (e.playerNameLower != nameKey || e.expiresAtMs <= now) continue;
        e.hiddenByReconnect = false;
        if (!teamLetter.empty()) e.teamLetter = teamLetter;
        const int secLeft = (int)((e.expiresAtMs - now + 999) / 1000);
        BedDcLog("disconnect " + playerName + " team=" + e.teamLetter +
                 " resumed existing timer rem=" + std::to_string(secLeft) + "s activeOnTeam=" +
                 std::to_string((int)std::count_if(s_bedDisconnects.begin(), s_bedDisconnects.end(),
                     [&](const BedDisconnectEntry& entry) {
                         return entry.teamLetter == e.teamLetter && !entry.hiddenByReconnect &&
                                entry.expiresAtMs > now;
                     })) +
                 " threshold=" + std::to_string(BedDisconnectThresholdForMode(s_bedDcLocrawMode)));
        return;
    }

    BedDisconnectEntry entry;
    entry.playerNameLower = nameKey;
    entry.teamLetter = teamLetter;
    entry.expiresAtMs = now + 2ULL * 60ULL * 1000ULL;
    s_bedDisconnects.push_back(std::move(entry));
    BedDcLog("disconnect " + playerName + " team=" + teamLetter +
             " expiresIn=120s activeOnTeam=" +
             std::to_string((int)std::count_if(s_bedDisconnects.begin(), s_bedDisconnects.end(),
                 [&](const BedDisconnectEntry& e) {
                     return e.teamLetter == teamLetter && !e.hiddenByReconnect && e.expiresAtMs > now;
                 })) +
             " threshold=" + std::to_string(BedDisconnectThresholdForMode(s_bedDcLocrawMode)));
}

void ClearBedDisconnectTimers() {
    BedDcLog("clear timers");
    std::lock_guard<std::mutex> lk(s_bedDcMu);
    s_bedDisconnects.clear();
    s_bedDcNameTeamCache.clear();
    s_bedDcOriginalSuffixByTeam.clear();
    // Keep last locraw mode so per-team DC threshold survives world load until the next /locraw.
}

void ForceClearResourcesCache() {
    s_forceResourcesClear.store(true, std::memory_order_release);
}

ResourceDisplayLocation ParseResourceDisplayLocation(const std::string& value) {
    const std::string v = LowerAscii(Trim(value));
    if (v == "scoreboard" || v == "sidebar" || v == "sb") {
        return ResourceDisplayLocation::Scoreboard;
    }
    return ResourceDisplayLocation::Tab;
}

static std::atomic<int> g_overlayActiveGamemode{(int)OverlayGamemode::Unknown};

void SetOverlayActiveGamemode(OverlayGamemode mode) {
    g_overlayActiveGamemode.store((int)mode, std::memory_order_relaxed);
}

OverlayGamemode GetOverlayActiveGamemode() {
    return (OverlayGamemode)g_overlayActiveGamemode.load(std::memory_order_relaxed);
}

std::string OverlayGamemodeToString(OverlayGamemode mode) {
    switch (mode) {
        case OverlayGamemode::Bedwars: return "bedwars";
        case OverlayGamemode::Skywars: return "skywars";
        case OverlayGamemode::Duel: return "duel";
        case OverlayGamemode::BedwarsDuels: return "bedwarsduels";
        case OverlayGamemode::MurderMystery: return "murdermystery";
        default: return "unknown";
    }
}

OverlayGamemode OverlayGamemodeFromString(const std::string& s) {
    const std::string l = LowerAscii(s);
    if (l == "bedwars") return OverlayGamemode::Bedwars;
    if (l == "skywars") return OverlayGamemode::Skywars;
    if (l == "duel") return OverlayGamemode::Duel;
    if (l == "bedwarsduels" || l == "bedwarsduel" || l == "bwduels") return OverlayGamemode::BedwarsDuels;
    if (l == "murdermystery" || l == "murder_mystery" || l == "murder" || l == "mm") return OverlayGamemode::MurderMystery;
    return OverlayGamemode::Unknown;
}

static int TeamOrderRank(const std::string& teamLetter) {
    if (teamLetter.empty()) return 99;
    const char c = (char)std::toupper((unsigned char)teamLetter[0]);
    switch (c) {
        case 'R': return 0;
        case 'B': return 1;
        case 'G': return 2;
        case 'Y': return 3;
        case 'A': return 4;
        case 'W': return 5; // white (§f)
        case 'P': return 6;
        case 'S': return 7; // gray (§7/§8) — light vs dark via TeamOrderRankForPlayer
        default: return 99;
    }
}

static int TeamOrderRankForPlayer(const PlayerBedwarsStats& p) {
    int rank = TeamOrderRank(p.teamLetter);
    // §7 gray (light) sorts below §8 dark_gray when both use team letter S.
    if (!p.teamLetter.empty() && std::toupper((unsigned char)p.teamLetter[0]) == 'S' &&
        (p.nameColor & 0xFFFFFF) == 0xAAAAAA) {
        rank += 1;
    }
    return rank;
}

static void ApplyHypixelStatsToPlayer(PlayerBedwarsStats& p, const CachedHypixelStats& h);
static void ApplySkywarsStatsToPlayer(PlayerBedwarsStats& p, const CachedHypixelStats& h);
static void ApplyDuelStatsToPlayer(PlayerBedwarsStats& p, const CachedHypixelStats& h);
static void ApplyBedwarsDuelsStatsToPlayer(PlayerBedwarsStats& p, const CachedHypixelStats& h);
static void ApplyOverlayStatsForGamemode(PlayerBedwarsStats& p, const CachedHypixelStats& h, OverlayGamemode mode);
static void ApplySeraphTagToPlayer(PlayerBedwarsStats& p, const CachedSeraphTag& tag);
static void MaybeAnnounceBlacklist(JNIEnv* env, jobject mcInstance, jclass mcClass,
                                   const PlayerBedwarsStats& p, const CachedSeraphTag& tag);
static void MaybeAnnounceChatOverlay(JNIEnv* env, jobject mcInstance, jclass mcClass,
                                     const PlayerBedwarsStats& p, const CachedHypixelStats& h,
                                     bool isNick = false);
static void MaybeAnnounceNickOverlay(JNIEnv* env, jobject mcInstance, jclass mcClass,
                                     const PlayerBedwarsStats& p);

static std::string StripMcFormatting(const std::string& in) {
    return StripMinecraftFormatting(in);
}

static std::string ShrinkForLog(const std::string& s, size_t maxLen = 160) {
    std::string out;
    out.reserve((std::min)(s.size(), maxLen));
    for (char c : s) {
        if (c == '\r' || c == '\n' || c == '\t') out.push_back(' ');
        else out.push_back(c);
        if (out.size() >= maxLen) break;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    if (s.size() > maxLen) out += "...";
    return out;
}

static std::string EscapeFooterForLog(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '\r') {
            out += "\\r";
        } else if (c == '\n') {
            out += "\\n";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

static bool ParseResourceEntry(const std::string& plainLower,
                               const std::string& keywordLower,
                               int& outCount,
                               int& outValue) {
    const size_t kpos = plainLower.find(keywordLower);
    if (kpos == std::string::npos) return false;

    const size_t maxScan = (std::min)(plainLower.size(), kpos + 96);
    const std::string segment = plainLower.substr(kpos, maxScan - kpos);

    // Expected shapes seen in tab footer:
    // 1) "emeralds (21): 41"
    // 2) "emeralds: 41"
    std::smatch m;
    if (std::regex_search(segment, m, std::regex(R"(\((\d+)\)\s*:\s*(\d+))"))) {
        outCount = atoi(m[1].str().c_str());
        outValue = atoi(m[2].str().c_str());
        return true;
    }
    if (std::regex_search(segment, m, std::regex(R"(:\s*(\d+))"))) {
        outCount = 0;
        outValue = atoi(m[1].str().c_str());
        return true;
    }

    return false;
}

static void ParseResourceTimersFromFooter(const std::string& footerRaw,
                                          int& emeraldSeconds,
                                          int& diamondSeconds,
                                          int& emeraldCount,
                                          int& diamondCount) {
    emeraldSeconds = 0;
    diamondSeconds = 0;
    emeraldCount = 0;
    diamondCount = 0;
    if (footerRaw.empty()) return;

    const std::string plainLower = LowerAscii(StripMcFormatting(footerRaw));
    ParseResourceEntry(plainLower, "emerald", emeraldCount, emeraldSeconds);
    ParseResourceEntry(plainLower, "diamond", diamondCount, diamondSeconds);
}

static bool ParseSidebarTimerSeconds(const std::string& plainLower,
                                     const std::string& keywordLower,
                                     int& outSeconds) {
    const size_t kpos = plainLower.find(keywordLower);
    if (kpos == std::string::npos) return false;

    const size_t maxScan = (std::min)(plainLower.size(), kpos + 96);
    const std::string segment = plainLower.substr(kpos, maxScan - kpos);

    std::smatch m;
    if (std::regex_search(segment, m, std::regex(R"((\d+):(\d{2}))"))) {
        const int mm = atoi(m[1].str().c_str());
        const int ss = atoi(m[2].str().c_str());
        outSeconds = mm * 60 + ss;
        return true;
    }
    if (std::regex_search(segment, m, std::regex(R"(in\s*(\d+)\s*s)"))) {
        outSeconds = atoi(m[1].str().c_str());
        return true;
    }
    return false;
}

struct StageEventTime {
    const char* name;
    int timeSec;
};

struct SpawnCalcResult {
    int count = 0;
    int next = 0;
};

static bool TryExtractRemainingSeconds(const std::string& plain, size_t fromPos, int& outSecondsLeft) {
    if (fromPos >= plain.size()) return false;
    const std::string tailLower = LowerAscii(plain.substr(fromPos));

    std::smatch m;
    if (std::regex_search(tailLower, m, std::regex(R"((\d{1,2})\s*:\s*(\d{1,2}))"))) {
        const int mm = atoi(m[1].str().c_str());
        const int ss = atoi(m[2].str().c_str());
        if (mm >= 0 && ss >= 0 && ss <= 59) {
            outSecondsLeft = mm * 60 + ss;
            return true;
        }
    }

    size_t inPos = tailLower.find(" in ");
    const size_t digitStart = (inPos != std::string::npos) ? inPos + 4 : 0;
    std::vector<int> nums;
    std::string cur;
    for (size_t i = digitStart; i < tailLower.size(); ++i) {
        const char c = tailLower[i];
        if (c >= '0' && c <= '9') {
            cur += c;
        } else if (!cur.empty()) {
            nums.push_back(atoi(cur.c_str()));
            cur.clear();
        }
    }
    if (!cur.empty()) nums.push_back(atoi(cur.c_str()));

    if (nums.size() >= 2) {
        const int ss = nums.back();
        const int mm = nums[nums.size() - 2];
        if (ss <= 59) {
            outSecondsLeft = mm * 60 + ss;
            return true;
        }
    }
    if (nums.size() == 1 && nums[0] <= 59) {
        outSecondsLeft = nums[0];
        return true;
    }
    return false;
}

static bool TryParseStageRemaining(const std::string& sidebarLine, std::string& outEventName, int& outSecondsLeft) {
    const std::string plain = Trim(StripMcFormatting(sidebarLine));
    if (plain.empty()) return false;

    static const StageEventTime kOrderedEvents[] = {
        {"Emerald III", 24 * 60},
        {"Diamond III", 18 * 60},
        {"Sudden Death", 40 * 60},
        {"More Dragons", 40 * 60},
        {"Emerald II", 12 * 60},
        {"Diamond II", 6 * 60},
        {"Game End", 50 * 60},
        {"Bed gone", 30 * 60},
    };

    const std::string lower = LowerAscii(plain);
    const StageEventTime* bestEvent = nullptr;
    size_t bestPos = std::string::npos;
    size_t bestNameLen = 0;
    for (const auto& it : kOrderedEvents) {
        const std::string n = LowerAscii(it.name);
        const size_t p = lower.find(n);
        if (p == std::string::npos) continue;
        if (!bestEvent || p < bestPos || (p == bestPos && n.size() > bestNameLen)) {
            bestPos = p;
            bestNameLen = n.size();
            bestEvent = &it;
        }
    }

    if (bestEvent) {
        int secs = -1;
        if (TryExtractRemainingSeconds(plain, bestPos, secs)) {
            outEventName = bestEvent->name;
            outSecondsLeft = secs;
            return true;
        }
        if (LowerAscii(bestEvent->name) == "bed gone") {
            outEventName = bestEvent->name;
            outSecondsLeft = 0;
            return true;
        }
    }

    const std::string lowerLegacy = lower;
    size_t sepPos = lowerLegacy.find(" in ");
    if (sepPos == std::string::npos) sepPos = lowerLegacy.find(" - ");
    if (sepPos == std::string::npos) {
        const size_t colonPos = lowerLegacy.rfind(':');
        if (colonPos != std::string::npos && colonPos > 0) {
            const std::string tail = plain.substr(colonPos - 2);
            std::smatch mColon;
            if (std::regex_search(tail, mColon, std::regex(R"((\d{1,2}):(\d{2}))"))) {
                sepPos = colonPos - 2 + (size_t)mColon.position();
            }
        }
    }
    if (sepPos == std::string::npos) return false;

    int secs = -1;
    if (!TryExtractRemainingSeconds(plain, sepPos, secs)) return false;

    outEventName = Trim(plain.substr(0, sepPos));
    outSecondsLeft = secs;
    return !outEventName.empty();
}

static int FindStageEventTimeSec(const std::string& eventName) {
    static const StageEventTime kEvents[] = {
        {"Diamond II", 6 * 60},
        {"Emerald II", 12 * 60},
        {"Diamond III", 18 * 60},
        {"Emerald III", 24 * 60},
        {"Bed gone", 30 * 60},
        {"Sudden Death", 40 * 60},
        {"More Dragons", 40 * 60},
        {"Game End", 50 * 60},
    };

    const std::string e = LowerAscii(Trim(eventName));
    int best = -1;
    size_t bestLen = 0;
    for (const auto& it : kEvents) {
        const std::string n = LowerAscii(it.name);
        if (e == n || e.find(n) != std::string::npos) {
            if (n.size() >= bestLen) {
                bestLen = n.size();
                best = it.timeSec;
            }
        }
    }
    return best;
}

static int GetTierAtTime(int elapsedSec, const std::vector<int>& upgradesSec) {
    if (upgradesSec.size() > 1 && elapsedSec >= upgradesSec[1]) return 3;
    if (!upgradesSec.empty() && elapsedSec >= upgradesSec[0]) return 2;
    return 1;
}

static int EmeraldIntervalFromTier(bool eightMode, int tier) {
    if (eightMode) {
        if (tier == 2) return 50;
        if (tier == 3) return 35;
        return 65;
    }
    if (tier == 2) return 40;
    if (tier == 3) return 27;
    return 55;
}

static int DiamondIntervalFromTier(int tier) {
    if (tier == 2) return 23;
    if (tier == 3) return 12;
    return 30;
}

static SpawnCalcResult CalculateSpawnsByElapsed(int elapsedSec,
                                                int firstSpawnSec,
                                                const std::vector<int>& upgradesSec,
                                                const std::function<int(int)>& intervalFromTier) {
    SpawnCalcResult out;
    if (elapsedSec < firstSpawnSec) {
        out.count = 0;
        out.next = firstSpawnSec - elapsedSec;
        return out;
    }

    int count = 1;
    int last = firstSpawnSec;
    size_t upIdx = 0;
    int nextUp = upgradesSec.empty() ? 0x3fffffff : upgradesSec[0];

    while (true) {
        const int tier = GetTierAtTime(last, upgradesSec);
        const int interval = intervalFromTier(tier);
        const int next = last + interval;
        if (nextUp <= elapsedSec && nextUp < next) {
            count++;
            last = nextUp;
            upIdx++;
            nextUp = (upIdx < upgradesSec.size()) ? upgradesSec[upIdx] : 0x3fffffff;
            continue;
        }
        if (next > elapsedSec) {
            out.count = count;
            out.next = (std::max)(1, next - elapsedSec);
            return out;
        }
        count++;
        last = next;
    }
}

static bool ComputeResourcesFromStageSidebar(const std::vector<std::string>& sidebarLines,
                                             bool eightMode,
                                             int& outEmeraldCount,
                                             int& outEmeraldNext,
                                             int& outDiamondCount,
                                             int& outDiamondNext) {
    int bestElapsed = -1;
    bool found = false;
    for (const auto& line : sidebarLines) {
        std::string parsedEvent;
        int parsedLeft = -1;
        if (!TryParseStageRemaining(line, parsedEvent, parsedLeft)) continue;
        const int eventTime = FindStageEventTimeSec(parsedEvent);
        if (eventTime < 0) continue;
        const int elapsed = eventTime - parsedLeft;
        if (elapsed > bestElapsed) {
            bestElapsed = elapsed;
            found = true;
        }
    }
    if (!found || bestElapsed < 0) return false;

    const int timeForCalc = bestElapsed + 1;

    const std::vector<int> diamondUpgrades = {6 * 60, 18 * 60};
    const std::vector<int> emeraldUpgrades = {12 * 60, 24 * 60};

    const SpawnCalcResult emerald = CalculateSpawnsByElapsed(
        timeForCalc,
        31,
        emeraldUpgrades,
        [eightMode](int t) { return EmeraldIntervalFromTier(eightMode, t); });
    const SpawnCalcResult diamond = CalculateSpawnsByElapsed(
        timeForCalc,
        1,
        diamondUpgrades,
        [](int t) { return DiamondIntervalFromTier(t); });

    outEmeraldCount = emerald.count;
    outEmeraldNext = emerald.next;
    outDiamondCount = diamond.count;
    outDiamondNext = diamond.next;
    return true;
}

static std::string FilterFooterKeepFinalKillsLine(const std::string& s) {
    if (s.empty()) return s;

    std::vector<std::string> lines;
    std::string cur;
    for (char c : s) {
        if (c == '\r') continue;
        if (c == '\n') {
            lines.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty() || lines.empty()) lines.push_back(cur);

    std::vector<std::string> kept;
    kept.reserve(lines.size());

    for (const auto& line : lines) {
        const std::string plain = LowerAscii(StripMcFormatting(line));
        if (plain.find("final kills") != std::string::npos) {
            kept.push_back(line);
        }
    }

    if (kept.empty()) {
        for (const auto& line : lines) {
            const std::string plain = LowerAscii(StripMcFormatting(line));
            const bool promo = (plain.find("ranks") != std::string::npos) ||
                               (plain.find("boosters") != std::string::npos) ||
                               (plain.find("store.hypixel.net") != std::string::npos);
            if (!promo && !plain.empty()) {
                kept.push_back(line);
                break;
            }
        }
    }

    std::string out;
    for (size_t i = 0; i < kept.size(); ++i) {
        if (i > 0) out.push_back('\n');
        out += kept[i];
    }
    return out;
}

static int UuidVersion(const std::string& uuid) {
    if (uuid.size() < 15) return 0;
    char c = (char)std::tolower((unsigned char)uuid[14]);
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return 0;
}

static bool LooksLikeMinecraftIgn(const std::string& s) {
    if (s.empty() || s.size() > 16) return false;
    for (unsigned char c : s) {
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

static void ReplayLogf(const char*, ...) {}
static void ReplayLogOnce(const std::string&, const char*, ...) {}

static std::string ResolveReplayApiUuid(const std::string& name, const std::string& tabUuid) {
    if (!LooksLikeMinecraftIgn(name)) return {};
    PlayerApi::AllowPlayerForApi(name);
    const std::string byName = PlayerApi::GetCachedUuidForName(name);
    if (!byName.empty()) return byName;
    if (UuidVersion(tabUuid) == 4) {
        PlayerApi::OnTabPlayer(name, tabUuid);
        return tabUuid;
    }
    return {};
}

static bool TryApplyDenickOverlayStats(JNIEnv* env, jobject mcInstance, jclass mcClass, PlayerBedwarsStats& p) {
    NumberDenickerOverlayData numDenick{};
    if (NumberDenickerGetOverlayData(p.name, numDenick) && numDenick.resolved && numDenick.hypOk) {
        ApplyOverlayStatsForGamemode(p, numDenick.hyp, GetOverlayActiveGamemode());
        MaybeAnnounceChatOverlay(env, mcInstance, mcClass, p, numDenick.hyp, true);
        return true;
    }
    DenickOverlayData denick{};
    if (DenickIsEnabled() && DenickGetOverlayData(p.name, denick) &&
        denick.state == DenickOverlayData::State::Denicked && denick.hypOk) {
        ApplyOverlayStatsForGamemode(p, denick.hyp, GetOverlayActiveGamemode());
        MaybeAnnounceChatOverlay(env, mcInstance, mcClass, p, denick.hyp, true);
        if (!denick.realUuid.empty()) p.playerId = denick.realUuid;
        if (denick.seraphOk) {
            ApplySeraphTagToPlayer(p, denick.seraph);
            MaybeAnnounceBlacklist(env, mcInstance, mcClass, p, denick.seraph);
        }
        return true;
    }
    return false;
}

static int HpToColor(int hp) {
    if (hp >= 21) return 0x0000AA;
    if (hp == 20) return 0x00AA00;
    if (hp >= 10) return 0x55FF55;
    if (hp >= 5) return 0xFFAA00;
    return 0xAA0000;
}

static int ColorFromThresholds(double v, const std::vector<std::pair<double, int>>& t) {
    for (const auto& kv : t) {
        if (v >= kv.first) return kv.second;
    }
    return 0xAAAAAA;
}

static int McCodeToRgb(char code);

static int ColorForFkdr(double v) {
    return McCodeToRgb(FkdrColorCode(v));
}

static int ColorForIndex(double v) {
    static const std::vector<std::pair<double, int>> t = {
        {2500000.0, 0xAA00AA}, {1250000.0, 0xFF55FF}, {360000.0, 0xAA0000}, {120000.0, 0xFF5555},
        {20000.0, 0xFFA500}, {4900.0, 0xFFFF55}, {1875.0, 0x00AA00}, {450.0, 0x55FF55},
        {25.0, 0xFFFFFF}, {0.0, 0xAAAAAA}
    };
    return ColorFromThresholds(v, t);
}

static int ColorForSwordKD(double v) {
    static const std::vector<std::pair<double, int>> t = {
        {50.0, 0xAA00AA}, {25.0, 0xFF55FF}, {10.0, 0xAA0000}, {5.0, 0xFF5555},
        {3.0, 0xFFA500}, {2.0, 0xFFFF55}, {1.5, 0x00AA00}, {0.75, 0x55FF55},
        {0.25, 0xFFFFFF}, {0.0, 0xAAAAAA}
    };
    return ColorFromThresholds(v, t);
}


static int ColorForRequeue(double v) {
    static const std::vector<std::pair<double, int>> t = {
        {80.0, 0xAA00AA}, {60.0, 0xFF55FF}, {40.0, 0xAA0000}, {30.0, 0xFF5555},
        {20.0, 0xFFA500}, {15.0, 0xFFFF55}, {10.0, 0x00AA00}, {5.0, 0x55FF55},
        {1.0, 0xFFFFFF}, {0.0, 0xAAAAAA}
    };
    return ColorFromThresholds(v, t);
}

static int ColorForWlr(double v) {
    static const std::vector<std::pair<double, int>> t = {
        {30.0, 0xAA00AA}, {15.0, 0xFF55FF}, {9.0, 0xAA0000}, {6.0, 0xFF5555},
        {3.0, 0xFFA500}, {2.1, 0xFFFF55}, {1.5, 0x00AA00}, {0.9, 0x55FF55},
        {0.3, 0xFFFFFF}, {0.0, 0xAAAAAA}
    };
    return ColorFromThresholds(v, t);
}

static int ColorForWins(double v) {
    static const std::vector<std::pair<double, int>> t = {
        {30000.0, 0xAA00AA}, {15000.0, 0xFF55FF}, {7500.0, 0xAA0000}, {4500.0, 0xFF5555},
        {2250.0, 0xFFA500}, {1500.0, 0xFFFF55}, {450.0, 0x00AA00}, {300.0, 0x55FF55},
        {150.0, 0xFFFFFF}, {0.0, 0xAAAAAA}
    };
    return ColorFromThresholds(v, t);
}

static int ColorForFinalKills(double v) {
    static const std::vector<std::pair<double, int>> t = {
        {100000.0, 0xAA00AA}, {50000.0, 0xFF55FF}, {25000.0, 0xAA0000}, {15000.0, 0xFF5555},
        {7500.0, 0xFFA500}, {5000.0, 0xFFFF55}, {2500.0, 0x00AA00}, {1000.0, 0x55FF55},
        {500.0, 0xFFFFFF}, {0.0, 0xAAAAAA}
    };
    return ColorFromThresholds(v, t);
}

static int ColorForBedsBroken(double v) {
    static const std::vector<std::pair<double, int>> t = {
        {50000.0, 0xAA00AA}, {25000.0, 0xFF55FF}, {12500.0, 0xAA0000}, {7500.0, 0xFF5555},
        {3750.0, 0xFFA500}, {2500.0, 0xFFFF55}, {1250.0, 0x00AA00}, {500.0, 0x55FF55},
        {250.0, 0xFFFFFF}, {0.0, 0xAAAAAA}
    };
    return ColorFromThresholds(v, t);
}

static int ColorForBblr(double v) {
    static const std::vector<std::pair<double, int>> t = {
        {20.0, 0xAA00AA}, {10.0, 0xFF55FF}, {6.0, 0xAA0000}, {4.0, 0xFF5555},
        {2.0, 0xFFA500}, {1.4, 0xFFFF55}, {1.0, 0x00AA00}, {0.6, 0x55FF55},
        {0.2, 0xFFFFFF}, {0.0, 0xAAAAAA}
    };
    return ColorFromThresholds(v, t);
}

static int ColorForWinstreak(double v) {
    static const std::vector<std::pair<double, int>> t = {
        {500.0, 0xAA00AA}, {250.0, 0xFF55FF}, {100.0, 0xAA0000}, {75.0, 0xFF5555},
        {50.0, 0xFFA500}, {40.0, 0xFFFF55}, {25.0, 0x00AA00}, {15.0, 0x55FF55},
        {5.0, 0xFFFFFF}, {0.0, 0xAAAAAA}
    };
    return ColorFromThresholds(v, t);
}

static int ColorForSeens(double v) {
    static const std::vector<std::pair<double, int>> t = {
        {100.0, 0xAA00AA}, {50.0, 0xFF55FF}, {30.0, 0xAA0000}, {20.0, 0xFF5555},
        {15.0, 0xFFA500}, {10.0, 0xFFFF55}, {7.0, 0x00AA00}, {4.0, 0x55FF55},
        {2.0, 0xFFFFFF}, {0.0, 0xAAAAAA}
    };
    return ColorFromThresholds(v, t);
}

static int ColorForSessionText(const std::string& s) {
    if (s == "API" || s == "OFFLINE") return 0x555555;
    int d = 0, h = 0, m = 0;
    if (sscanf_s(s.c_str(), "%dd %dh", &d, &h) >= 2) {
        double hours = d * 24.0 + h;
        if (hours >= 8.0) return 0x55FF55;
        if (hours >= 4.0) return 0x00AA00;
        if (hours >= 2.0) return 0xFFFF55;
        if (hours >= 1.0) return 0xFFA500;
        if (hours >= 0.5) return 0xFF5555;
        return 0xAA0000;
    }
    if (sscanf_s(s.c_str(), "%dh %dm", &h, &m) >= 1) {
        double hours = h + (m / 60.0);
        if (hours >= 8.0) return 0x55FF55;
        if (hours >= 4.0) return 0x00AA00;
        if (hours >= 2.0) return 0xFFFF55;
        if (hours >= 1.0) return 0xFFA500;
        if (hours >= 0.5) return 0xFF5555;
        return 0xAA0000;
    }
    if (sscanf_s(s.c_str(), "%dm", &m) == 1) {
        double hours = m / 60.0;
        if (hours >= 0.5) return 0xFF5555;
        return 0xAA0000;
    }
    return 0x555555;
}

static int SeraphColorFromReportType(const std::string& reportType) {
    if (reportType.empty()) return -1;
    const std::string l = LowerAscii(reportType);
    if (l == "cheater" || l == "cheat" || l == "blatant" || l == "closet") return 0xAA0000;
    if (l == "sniper" || l == "streamsniper") return 0xFFA500;
    if (l == "bot" || l == "autobot") return 0x959595;
    if (l == "scammer") return 0xFF55FF;
    if (l == "booster" || l == "boosting") return 0x55FFFF;
    if (l == "alts" || l == "alt") return 0xAAAAAA;
    return -1;
}

static bool ExtractBlacklistString(const std::string& json, const std::string& key, std::string& out) {
    std::regex rgx("\"blacklist\"\\s*:\\s*\\{[\\s\\S]*?\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch m;
    if (!std::regex_search(json, m, rgx)) return false;
    out = m[1].str();
    return true;
}

static bool ExtractBlacklistBool(const std::string& json, const std::string& key, bool& out) {
    std::regex rgx("\"blacklist\"\\s*:\\s*\\{[\\s\\S]*?\"" + key + "\"\\s*:\\s*(true|false)");
    std::smatch m;
    if (!std::regex_search(json, m, rgx)) return false;
    out = (m[1].str() == "true");
    return true;
}

static bool ExtractBlacklistInt64(const std::string& json, const std::string& key, long long& out) {
    std::regex rgx("\"blacklist\"\\s*:\\s*\\{[\\s\\S]*?\"" + key + "\"\\s*:\\s*(-?[0-9]+)");
    std::smatch m;
    if (!std::regex_search(json, m, rgx)) return false;
    out = _strtoi64(m[1].str().c_str(), nullptr, 10);
    return true;
}

static void ParseSeraphTooltipDetails(const std::string& tooltip,
                                      const std::string& reportType,
                                      std::string& outReason,
                                      std::string& outMeta) {
    outReason.clear();
    outMeta.clear();
    std::string t = Trim(tooltip);
    if (t.empty()) return;

    if (!reportType.empty()) {
        const std::string tLower = LowerAscii(t);
        const std::string typeLower = LowerAscii(reportType);
        if (tLower.rfind(typeLower, 0) == 0) {
            t = Trim(t.substr(reportType.size()));
            if (!t.empty() && t[0] == ':') t = Trim(t.substr(1));
        }
    }

    const size_t l = t.find('(');
    const size_t r = t.rfind(')');
    if (l != std::string::npos && r != std::string::npos && r > l) {
        outReason = Trim(t.substr(0, l));
        outMeta = Trim(t.substr(l + 1, r - l - 1));
    } else {
        outReason = Trim(t);
    }
}

struct SeraphRenderTag {
    int color;
    char mcCode;
    const char* text;
};

static SeraphRenderTag ResolveSeraphRender(const std::string& reportType) {
    static const std::unordered_map<std::string, SeraphRenderTag> kTags = {
        {"sniper", {0xAA0000, '4', "S"}},
        {"possible sniper", {0xFFAA00, '6', "PS"}},
        {"potential sniper", {0xFFAA00, '6', "PS"}},
        {"alt", {0xFFFF55, 'e', "Alt"}},
        {"legit sniper", {0xFFFF55, 'e', "LS"}},
        {"legit sniping", {0xFFFF55, 'e', "LS"}},
        {"confirmed cheater", {0xAA0000, '4', "C"}},
        {"blatant cheater", {0xAA0000, '4', "BC"}},
        {"closet cheater", {0xFF5555, 'c', "CC"}},
        {"caution", {0xFFAA00, '6', "!"}},
        {"account", {0xAAAAAA, '7', "i"}},
        {"info", {0xAAAAAA, '7', "i"}},
        {"cheater", {0xAA0000, '4', "C"}},
        {"beamed", {0xFFAA00, '6', "R"}},
        {"risky", {0xFFAA00, '6', "R"}},
        {"bot", {0xAAAAAA, '7', "B"}},
        {"annoying", {0xFFFF55, 'e', "A"}},
        {"sniping", {0xAA0000, '4', "S"}},
        {"blatant cheating", {0xAA0000, '4', "BC"}},
        {"closet cheating", {0xFF5555, 'c', "CC"}}
    };

    const std::string l = LowerAscii(reportType);
    auto it = kTags.find(l);
    if (it != kTags.end()) return it->second;

    if (l.find("cheat") != std::string::npos) return {0xAA0000, '4', "C"};
    if (l.find("sniper") != std::string::npos) return {0xFFA500, '6', "S"};
    if (l.find("bot") != std::string::npos) return {0x959595, '7', "B"};
    return {0xFF5555, 'c', "?"};
}

static std::string FormatFixed2(double v) {
    char buf[64];
    sprintf_s(buf, "%.2f", v);
    return std::string(buf);
}

bool QueryPlayerStatsNow(const std::string& playerName,
                         const std::string& hypixelApiKey,
                         const std::string& seraphApiKey,
                         const std::string& lookupMode,
                         PlayerLookupStats& out) {
    return PlayerApi::FetchStatsNow(playerName, hypixelApiKey, seraphApiKey, lookupMode, out);
}

static std::vector<ColumnConfig> BuildAllColumns() {
    return {
        {"name", "Name", 5, 128, TextAlign::Left, true, 0},
        {"hp", "HP", 136, 24, TextAlign::Center, true, 1},
        {"stars", "Stars", 164, 40, TextAlign::Left, true, 2},
        {"fkdr", "FKDR", 206, 36, TextAlign::Center, true, 3},
        {"wlr", "WLR", 244, 34, TextAlign::Center, true, 4},
        {"kdr", "KDR", 280, 34, TextAlign::Center, true, 5},
        {"swordKD", "SKD", 316, 36, TextAlign::Center, true, 6},
        {"bblr", "BBLR", 354, 40, TextAlign::Center, true, 7},
        {"index", "IDX", 396, 42, TextAlign::Center, true, 8},
        {"wins", "Wins", 440, 32, TextAlign::Center, true, 9},
        {"finalKills", "Finals", 474, 34, TextAlign::Center, true, 10},
        {"bedsBroken", "Beds", 510, 34, TextAlign::Center, true, 11},
        {"requeuePct", "Requeue%", 546, 64, TextAlign::Center, true, 12},
        {"winstreak", "WS", 612, 34, TextAlign::Center, true, 13},
        {"sw_stars", "Stars", 164, 40, TextAlign::Left, true, 14},
        {"sw_kdr", "KDR", 210, 34, TextAlign::Center, true, 15},
        {"sw_wlr", "WLR", 252, 34, TextAlign::Center, true, 16},
        {"sw_wins", "Wins", 294, 32, TextAlign::Center, true, 17},
        {"sw_kills", "Kills", 336, 34, TextAlign::Center, true, 18},
        {"duel_wins", "Wins", 164, 32, TextAlign::Center, true, 19},
        {"duel_wlr", "WLR", 206, 34, TextAlign::Center, true, 20},
        {"duel_kdr", "KDR", 248, 34, TextAlign::Center, true, 21},
        {"bwd_wlr", "WLR", 164, 34, TextAlign::Center, true, 22},
        {"bwd_index", "IDX", 214, 42, TextAlign::Center, true, 23},
        {"mm_kdr", "KDR", 164, 34, TextAlign::Center, true, 29},
        {"mm_wlr", "WLR", 206, 34, TextAlign::Center, true, 30},
        {"mm_kills", "Kills", 248, 34, TextAlign::Center, true, 31},
        {"mm_wins", "Wins", 292, 32, TextAlign::Center, true, 32},
        {"mm_icon", "Prefix", 336, 72, TextAlign::Center, true, 33},
        {"seens", "Seen", 650, 40, TextAlign::Center, true, 24},
        {"session", "Session", 692, 48, TextAlign::Center, true, 25},
        {"ping", "Ping", 740, 34, TextAlign::Center, true, 26},
        {"pingvar", "PingVar", 776, 48, TextAlign::Center, true, 27},
        {"seraph", "Seraph", 826, 42, TextAlign::Center, true, 28}
    };
}

static std::string NormalizeOverlayColumnId(const std::string& id, OverlayGamemode mode) {
    if (id == "magicRatio" || id == "magicratio") return "requeuePct";
    if (id == "tags") return "";
    if (id == "PingVar" || id == "pingVar") return "pingvar";
    if (mode == OverlayGamemode::Skywars) {
        if (id == "stars") return "sw_stars";
        if (id == "kdr") return "sw_kdr";
        if (id == "wlr") return "sw_wlr";
        if (id == "wins") return "sw_wins";
        if (id == "kills") return "sw_kills";
    } else if (mode == OverlayGamemode::Duel) {
        if (id == "wins") return "duel_wins";
        if (id == "wlr") return "duel_wlr";
        if (id == "kdr") return "duel_kdr";
    } else if (mode == OverlayGamemode::BedwarsDuels) {
        if (id == "wlr") return "bwd_wlr";
        if (id == "index") return "bwd_index";
    } else if (mode == OverlayGamemode::MurderMystery) {
        if (id == "kdr") return "mm_kdr";
        if (id == "wlr") return "mm_wlr";
        if (id == "kills") return "mm_kills";
        if (id == "wins") return "mm_wins";
        if (id == "icon") return "mm_icon";
    }
    return id;
}

static std::vector<std::string> DefaultColumnsForGamemode(OverlayGamemode mode) {
    switch (mode) {
        case OverlayGamemode::Skywars:
            return {"sw_stars", "sw_kdr", "sw_wlr", "sw_wins", "sw_kills", "seens", "session", "ping", "pingvar", "seraph"};
        case OverlayGamemode::Duel:
            return {"duel_wins", "duel_wlr", "duel_kdr", "seens", "session", "ping", "pingvar", "seraph"};
        case OverlayGamemode::BedwarsDuels:
            return {"bwd_wlr", "bwd_index", "seens", "session", "ping", "pingvar", "seraph"};
        case OverlayGamemode::MurderMystery:
            return {"mm_kdr", "mm_wlr", "mm_kills", "mm_wins", "mm_icon", "seens", "session", "ping", "pingvar", "seraph"};
        case OverlayGamemode::Bedwars:
        default:
            return {"stars", "fkdr", "wlr", "kdr", "swordKD", "bblr", "index", "wins", "finalKills",
                    "bedsBroken", "requeuePct", "winstreak", "seens", "session", "ping", "pingvar", "seraph"};
    }
}

static std::string PrimaryStatColumnForGamemode(OverlayGamemode mode) {
    switch (mode) {
        case OverlayGamemode::Skywars: return "sw_stars";
        case OverlayGamemode::Duel: return "duel_wins";
        case OverlayGamemode::BedwarsDuels: return "bwd_index";
        case OverlayGamemode::MurderMystery: return "mm_wins";
        default: return "stars";
    }
}

static void SetPrimaryStatText(PlayerBedwarsStats& p, const std::string& text, int color = 0) {
    const std::string key = PrimaryStatColumnForGamemode(GetOverlayActiveGamemode());
    p.statsRaw[key] = text;
    if (color) p.statsColor[key] = color;
}

static int McCodeToRgb(char code) {
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
        default: return 0xFFFFFF;
    }
}

static char McColorCodeFromRgb(int rgb) {
    switch (rgb & 0xFFFFFF) {
        case 0x000000: return '0';
        case 0x0000AA: return '1';
        case 0x00AA00: return '2';
        case 0x00AAAA: return '3';
        case 0xAA0000: return '4';
        case 0xAA00AA: return '5';
        case 0xFFAA00: return '6';
        case 0xAAAAAA: return '7';
        case 0x555555: return '8';
        case 0x5555FF: return '9';
        case 0x55FF55: return 'a';
        case 0x55FFFF: return 'b';
        case 0xFF5555: return 'c';
        case 0xFF55FF: return 'd';
        case 0xFFFF55: return 'e';
        default: return 'f';
    }
}

static void ApplyHypixelStatsToPlayer(PlayerBedwarsStats& p, const CachedHypixelStats& h) {
    p.statsRaw["stars"] = GetPrestigeColorText(h.stars);
    const double fkdr = (double)h.finalKills / (double)((std::max)(1, h.finalDeaths));
    const double wlr = (double)h.wins / (double)((std::max)(1, h.losses));
    const double bblr = (double)h.bedsBroken / (double)((std::max)(1, h.bedsLost));
    const int swordNumerator = h.entityAttackFinalKills + h.entityAttackKills;
    const int swordDenominator = h.entityAttackFinalDeaths + h.entityAttackDeaths;
    const double swordKD = (double)swordNumerator / (double)((std::max)(1, swordDenominator));
    const double requeuePct = ((double)h.magicFinalDeaths / (double)((std::max)(1, h.gamesPlayed))) * 100.0;
    const double index = (double)h.stars * fkdr * fkdr;
    const double kdr = (double)h.kills / (double)((std::max)(1, h.deaths));
    p.statsRaw["fkdr"] = FormatRatio(fkdr);
    p.statsRaw["kdr"] = FormatFixed2(kdr);
    p.statsRaw["wlr"] = FormatRatio(wlr);
    p.statsRaw["wins"] = std::to_string(h.wins);
    p.statsRaw["finalKills"] = std::to_string(h.finalKills);
    p.statsRaw["bedsBroken"] = std::to_string(h.bedsBroken);
    p.statsRaw["bblr"] = FormatRatio(bblr);
    p.statsRaw["swordKD"] = FormatFixed2(swordKD);
    p.statsRaw["index"] = FormatRatio(index);
    p.statsRaw["requeuePct"] = FormatRatio(requeuePct) + "%";

    if (h.winstreak >= 0) p.statsRaw["winstreak"] = std::to_string(h.winstreak);
    else p.statsRaw["winstreak"] = "API";

    if (h.lastLogin <= 0) {
        p.statsRaw["session"] = "API";
    } else {
        const long long nowEpochMs = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const bool online = (h.lastLogin - h.lastLogout > -2000);
        if (online) {
            long long durMs = nowEpochMs - h.lastLogin;
            if (durMs < 0) durMs = 0;
            const long long mins = durMs / 60000;
            const long long hours = mins / 60;
            const long long days = hours / 24;
            if (hours >= 24) p.statsRaw["session"] = std::to_string(days) + "d " + std::to_string(hours % 24) + "h";
            else if (hours >= 1) p.statsRaw["session"] = std::to_string(hours) + "h " + std::to_string(mins % 60) + "m";
            else p.statsRaw["session"] = std::to_string(mins) + "m";
        } else {
            p.statsRaw["session"] = "OFFLINE";
        }
    }

    p.statsColor["fkdr"] = ColorForFkdr(fkdr);
    p.statsColor["kdr"] = McCodeToRgb(KdrColorCode(kdr));
    p.statsColor["index"] = ColorForIndex(index);
    p.statsColor["swordKD"] = ColorForSwordKD(swordKD);
    p.statsColor["requeuePct"] = ColorForRequeue(requeuePct);
    p.statsColor["wlr"] = ColorForWlr(wlr);
    p.statsColor["wins"] = ColorForWins((double)h.wins);
    p.statsColor["finalKills"] = ColorForFinalKills((double)h.finalKills);
    p.statsColor["bedsBroken"] = ColorForBedsBroken((double)h.bedsBroken);
    p.statsColor["bblr"] = ColorForBblr(bblr);
    if (h.winstreak >= 0) p.statsColor["winstreak"] = ColorForWinstreak((double)h.winstreak);
    else p.statsColor["winstreak"] = 0x555555;
    p.statsColor["session"] = ColorForSessionText(p.statsRaw["session"]);
}

static int McCodeToRgbLocal(char code) {
    return McCodeToRgb(code);
}

static void ApplySkywarsStatsToPlayer(PlayerBedwarsStats& p, const CachedHypixelStats& h) {
    if (!h.hasSkywars) return;
    const double kdr = (double)h.swKills / (double)((std::max)(1, h.swDeaths));
    const double wlr = (double)h.swWins / (double)((std::max)(1, h.swLosses));
    p.statsRaw["sw_stars"] = !h.swStarsFormatted.empty()
        ? h.swStarsFormatted
        : FormatSkywarsStarsDisplay(h.swLevel, h.swStars);
    p.statsRaw["sw_kdr"] = FormatFixed2(kdr);
    p.statsRaw["sw_wlr"] = FormatFixed2(wlr);
    p.statsRaw["sw_wins"] = std::to_string(h.swWins);
    p.statsRaw["sw_kills"] = std::to_string(h.swKills);
    p.statsColor["sw_kdr"] = McCodeToRgbLocal(KdrColorCode(kdr));
    p.statsColor["sw_wlr"] = McCodeToRgbLocal(WlrColorCode(wlr));
    p.statsColor["sw_wins"] = ColorForWins((double)h.swWins);
    p.statsColor["sw_kills"] = ColorForFinalKills((double)h.swKills);
}

static void ApplyDuelStatsToPlayer(PlayerBedwarsStats& p, const CachedHypixelStats& h) {
    if (!h.hasDuels) return;
    const double wlr = (double)h.duelWins / (double)((std::max)(1, h.duelLosses));
    const double kdr = (double)h.duelWins / (double)((std::max)(1, h.duelDeaths));
    p.statsRaw["duel_wins"] = std::to_string(h.duelWins);
    p.statsRaw["duel_wlr"] = FormatDuelRatio(wlr);
    p.statsRaw["duel_kdr"] = FormatDuelRatio(kdr);
    p.statsColor["duel_wins"] = McCodeToRgbLocal(DuelWinsColorCode(h.duelWins));
    p.statsColor["duel_wlr"] = McCodeToRgbLocal(WlrColorCode(wlr));
    p.statsColor["duel_kdr"] = McCodeToRgbLocal(DuelKdrColorCode(kdr));
}

static void ApplyBedwarsDuelsStatsToPlayer(PlayerBedwarsStats& p, const CachedHypixelStats& h) {
    if (!h.hasDuels) return;
    const int starsBw = h.ok ? h.stars : BedwarsExpToStars(h.bwExperience);
    const double fkdrBw = h.ok
        ? ((double)h.finalKills / (double)((std::max)(1, h.finalDeaths)))
        : 0.0;
    const double wlrDuel = (double)h.duelWins / (double)((std::max)(1, h.duelLosses));
    const double kdrDuel = (double)h.duelKills / (double)((std::max)(1, h.duelDeaths));
    const double wlrBwDuel = (double)h.duelWinsBw / (double)((std::max)(1, h.duelLossesBw));

    double part1 = std::cbrt(wlrDuel * kdrDuel * fkdrBw);
    part1 = std::pow(part1, 0.6);
    double part2 = std::sqrt((double)h.duelWins * (double)starsBw);
    part2 = std::pow(part2, 0.4);
    const double scoreFinal = part1 * part2;

    p.statsRaw["bwd_index"] = FormatFixed2(scoreFinal);
    p.statsRaw["bwd_wlr"] = FormatFixed2(wlrBwDuel);
    p.statsColor["bwd_index"] = McCodeToRgbLocal(BwdScoreColorCode(scoreFinal));
    p.statsColor["bwd_wlr"] = McCodeToRgbLocal(WlrColorCode(wlrBwDuel));
}

static std::string FormatMurderMysteryIconLabel(std::string raw) {
    // Default = Bedwars 1000+ star (star_white_circled): empty star in a filled circle ✪
    static const std::string kDefaultStar = "\xE2\x9C\xAA";
    raw = Trim(raw);
    if (raw.empty()) return kDefaultStar;
    std::string id = LowerAscii(raw);
    if (id == "none") return kDefaultStar;
    if (id.rfind("prefixicon_", 0) == 0) id = id.substr(11);
    // Hypixel MM emblem/prefix symbols (wiki Murder Mystery/Cosmetics).
    if (id == "divine") return std::string("\xCE\xA6");           // Φ
    if (id == "zero") return std::string("\xE2\x88\x85");         // ∅
    if (id == "sigma") return std::string("\xCE\xA3");            // Σ
    if (id == "omega") return std::string("\xCE\xA9");            // Ω
    if (id == "alpha") return std::string("\xCE\xB1");            // α
    if (id == "equivalence") return std::string("\xE2\x89\xA1");  // ≡
    if (id == "rich") return "$";
    if (id == "podium") return std::string("\xCF\x80");           // π
    if (id == "florin") return std::string("\xC6\x92");           // ƒ
    if (id == "star" || id == "star_white_circled" || id == "star_black_open" ||
        id == "default" || id == "none_icon") {
        return kDefaultStar;
    }
    return kDefaultStar;
}

static char MurderMysteryColorToMcCode(const std::string& colorName) {
    char c = TeamColorNameToMcCode(LowerAscii(Trim(colorName)));
    if (!c) {
        std::string u = LowerAscii(Trim(colorName));
        for (char& ch : u) if (ch == ' ') ch = '_';
        c = TeamColorNameToMcCode(u);
    }
    return c ? c : '7'; // default light gray (§7)
}

// 97800 → "97.8k", 6385 → "6.4k", 999 → "999", 1500000 → "1.5M"
static std::string FormatMmCompactStat(int n) {
    if (n < 0) return "0";
    if (n < 1000) return std::to_string(n);
    char buf[32];
    if (n < 1000000) {
        const double v = (double)n / 1000.0;
        std::snprintf(buf, sizeof(buf), "%.1fk", v);
        std::string s(buf);
        if (s.size() >= 4 && s[s.size() - 3] == '.' && s[s.size() - 2] == '0') {
            s.erase(s.size() - 3, 2);
        }
        return s;
    }
    const double v = (double)n / 1000000.0;
    std::snprintf(buf, sizeof(buf), "%.1fM", v);
    std::string s(buf);
    if (s.size() >= 4 && s[s.size() - 3] == '.' && s[s.size() - 2] == '0') {
        s.erase(s.size() - 3, 2);
    }
    return s;
}

// color[active_prefixstat icon] — number from active_prefixstat only; kills underlined.
// Default (no custom prefix): light-gray [✪] (same star as Bedwars 1000+).
static std::string FormatMurderMysteryPrefix(const CachedHypixelStats& h) {
    static const std::string kDefaultStar = "\xE2\x9C\xAA"; // ✪
    const std::string glyph = FormatMurderMysteryIconLabel(h.mmIcon);
    const char colorCode = MurderMysteryColorToMcCode(h.mmEmblemColor);
    const std::string C = SectionColor(colorCode);
    const char openB = h.mmPrefixOpen ? h.mmPrefixOpen : '[';
    const char closeB = h.mmPrefixClose ? h.mmPrefixClose : ']';
    const std::string icon = glyph.empty() ? kDefaultStar : glyph;

    // No active prefixstat/icon from API → still show default [✪] in light gray.
    if (!h.mmHasPrefix) {
        return SectionColor('7') + "[" + kDefaultStar + "]";
    }

    std::string out = C;
    out.push_back(openB);
    if (h.mmPrefixStat >= 0) {
        const std::string num = FormatMmCompactStat(h.mmPrefixStat);
        if (h.mmPrefixIsKills) {
            out += SectionColor('n');
            out += num;
            out += SectionColor('r');
            out += C;
        } else {
            out += num;
        }
        out.push_back(' ');
    }
    out += icon;
    out.push_back(closeB);
    return out;
}

static void ApplyMurderMysteryStatsToPlayer(PlayerBedwarsStats& p, const CachedHypixelStats& h) {
    if (!h.hasMurderMystery) return;
    const double kdr = (double)h.mmKills / (double)((std::max)(1, h.mmDeaths));
    const double wlr = (double)h.mmWins / (double)((std::max)(1, h.mmLosses));
    p.statsRaw["mm_kdr"] = FormatFixed2(kdr);
    p.statsRaw["mm_wlr"] = FormatFixed2(wlr);
    p.statsRaw["mm_kills"] = FormatMmCompactStat(h.mmKills);
    p.statsRaw["mm_wins"] = FormatMmCompactStat(h.mmWins);
    p.statsRaw["mm_icon"] = FormatMurderMysteryPrefix(h);
    p.statsColor["mm_kdr"] = McCodeToRgbLocal(KdrColorCode(kdr));
    p.statsColor["mm_wlr"] = McCodeToRgbLocal(WlrColorCode(wlr));
    p.statsColor["mm_kills"] = ColorForFinalKills((double)h.mmKills);
    p.statsColor["mm_wins"] = ColorForWins((double)h.mmWins);
    p.statsColor.erase("mm_icon");
}

static void ClearOverlayHypixelStats(PlayerBedwarsStats& p) {
    static const char* kKeys[] = {
        "stars", "fkdr", "kdr", "index", "swordKD", "requeuePct", "wlr", "wins",
        "finalKills", "bedsBroken", "bblr", "winstreak", "session",
        "sw_stars", "sw_kdr", "sw_wlr", "sw_wins", "sw_kills",
        "duel_wins", "duel_wlr", "duel_kdr",
        "bwd_wlr", "bwd_index",
        "mm_kdr", "mm_wlr", "mm_kills", "mm_wins", "mm_icon"
    };
    for (const char* key : kKeys) {
        p.statsRaw[key] = "";
        p.statsColor.erase(key);
    }
}

static void ApplyOverlayStatsForGamemode(PlayerBedwarsStats& p, const CachedHypixelStats& h, OverlayGamemode mode) {
    ClearOverlayHypixelStats(p);
    switch (mode) {
        case OverlayGamemode::Skywars:
            ApplySkywarsStatsToPlayer(p, h);
            break;
        case OverlayGamemode::Duel:
            ApplyDuelStatsToPlayer(p, h);
            break;
        case OverlayGamemode::BedwarsDuels:
            ApplyBedwarsDuelsStatsToPlayer(p, h);
            break;
        case OverlayGamemode::MurderMystery:
            ApplyMurderMysteryStatsToPlayer(p, h);
            break;
        case OverlayGamemode::Bedwars:
        default:
            ApplyHypixelStatsToPlayer(p, h);
            break;
    }
}

static int ColorForPing(int ping) {
    if (ping < 0) return 0x555555;
    return McCodeToRgb(PingColorCode(ping));
}

static int ColorForPingVar(int pingVar) {
    if (pingVar < 0) return 0x555555;
    if (pingVar <= 15) return McCodeToRgb('a');
    if (pingVar <= 40) return McCodeToRgb('e');
    if (pingVar <= 80) return McCodeToRgb('6');
    return McCodeToRgb('c');
}

static void ApplyWinstreakPingToPlayer(PlayerBedwarsStats& p, const CachedWinstreakPing& ping) {
    if (!ping.resolved) {
        p.statsRaw["ping"] = "API";
        p.statsColor["ping"] = 0x555555;
        p.statsRaw["pingvar"] = "API";
        p.statsColor["pingvar"] = 0x555555;
        return;
    }
    if (ping.averagePing < 0) {
        p.statsRaw["ping"] = "-";
        p.statsColor["ping"] = 0xAAAAAA;
    } else {
        p.statsRaw["ping"] = std::to_string(ping.averagePing);
        p.statsColor["ping"] = ColorForPing(ping.averagePing);
    }
    if (ping.pingVar < 0) {
        p.statsRaw["pingvar"] = "-";
        p.statsColor["pingvar"] = 0xAAAAAA;
    } else {
        p.statsRaw["pingvar"] = std::to_string(ping.pingVar);
        p.statsColor["pingvar"] = ColorForPingVar(ping.pingVar);
    }
}

static void ApplySeraphTagToPlayer(PlayerBedwarsStats& p, const CachedSeraphTag& tag) {
    if (!tag.resolved || !tag.blacklisted || tag.tag.empty()) return;
    const SeraphRenderTag renderTag = ResolveSeraphRender(tag.reportType.empty() ? tag.tag : tag.reportType);
    std::string formatted = std::string(1, (char)0xC2) + (char)0xA7 + renderTag.mcCode + renderTag.text;
    if (tag.verified) {
        const std::string sec = std::string(1, (char)0xC2) + (char)0xA7;
        formatted += " " + sec + "a" + "\xE2\x9C\x94" + sec + "r";
    }
    p.statsRaw["tags"] = formatted;
    p.statsRaw["seraph"] = formatted;
    p.statsColor["seraph"] = renderTag.color;
    p.statsColor["tags"] = p.statsColor["seraph"];
}

static std::string JsonEscapeChat(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

static const char* McCodeToJsonColorName(char code) {
    switch ((char)std::tolower((unsigned char)code)) {
        case '0': return "black";
        case '1': return "dark_blue";
        case '2': return "dark_green";
        case '3': return "dark_aqua";
        case '4': return "dark_red";
        case '5': return "dark_purple";
        case '6': return "gold";
        case '7': return "gray";
        case '8': return "dark_gray";
        case '9': return "blue";
        case 'a': return "green";
        case 'b': return "aqua";
        case 'c': return "red";
        case 'd': return "light_purple";
        case 'e': return "yellow";
        default: return "white";
    }
}

struct ChatOverlaySettings {
    bool enabled = false;
    bool compactBlacklist = false;
    bool ignoreTeammates = false;
    int minStars = 100;
    double minFkdr = 5.0;
    double minSwKdr = 1.5;
};

static char FindFirstMcColorCode(const std::string& s);

static bool PlayerHasResolvedTeamColor(int nameColor, const std::string& teamLetter) {
    if (teamLetter.empty() || teamLetter == "?") return false;
    if (teamLetter == "W" && nameColor == 0xFFFFFF) return false;
    return true;
}

static char TeamLetterFromSidebarYouLine(const std::string& plainLower) {
    if (plainLower.find(" you") == std::string::npos) return 0;
    struct TeamNameLetter { const char* name; char letter; };
    static const TeamNameLetter kTeams[] = {
        {"red", 'R'}, {"blue", 'B'}, {"green", 'G'}, {"yellow", 'Y'},
        {"aqua", 'A'}, {"white", 'W'}, {"pink", 'P'}, {"gray", 'S'}, {"grey", 'S'},
    };
    for (const auto& t : kTeams) {
        if (plainLower.find(t.name) != std::string::npos) return t.letter;
    }
    if (plainLower.size() >= 2 && plainLower[1] == ' ') {
        const char c = plainLower[0];
        if (c >= 'a' && c <= 'z') return (char)std::toupper((unsigned char)c);
    }
    return 0;
}

static std::string TeamLetterFromScoreboardName(JNIEnv* env,
                                                jobject scoreboard,
                                                jmethodID midGetPlayersTeam,
                                                jclass scorePlayerTeamClass,
                                                jmethodID midFormatPlayerName,
                                                const std::string& playerName) {
    if (!env || !scoreboard || playerName.empty() || !midGetPlayersTeam || !midFormatPlayerName || !scorePlayerTeamClass) {
        return "";
    }
    jstring jRawName = env->NewStringUTF(playerName.c_str());
    if (!jRawName) return "";
    jobject teamObj = env->CallObjectMethod(scoreboard, midGetPlayersTeam, jRawName);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        teamObj = nullptr;
    }
    jstring jFormatted = (jstring)env->CallStaticObjectMethod(scorePlayerTeamClass, midFormatPlayerName, teamObj, jRawName);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        jFormatted = nullptr;
    }
    std::string letter;
    if (jFormatted) {
        const char* fmt = env->GetStringUTFChars(jFormatted, nullptr);
        const std::string formatted = fmt ? fmt : "";
        if (fmt) env->ReleaseStringUTFChars(jFormatted, fmt);
        env->DeleteLocalRef(jFormatted);
        const char colorCode = FindFirstMcColorCode(formatted);
        char teamLetter = TeamLetterFromColorCode(colorCode);
        // No § color → White team (Hypixel White has no color code).
        if (teamLetter == '-' && colorCode == 0) teamLetter = 'W';
        if (teamLetter != '-') letter = std::string(1, teamLetter);
    }
    if (teamObj) env->DeleteLocalRef(teamObj);
    env->DeleteLocalRef(jRawName);
    return letter;
}

static void UpdateLocalPlayerIdentity(JNIEnv* env,
                                      jobject thePlayer,
                                      jobject scoreboard,
                                      jmethodID midGetPlayersTeam,
                                      jclass scorePlayerTeamClass,
                                      jmethodID midFormatPlayerName,
                                      jmethodID midUUIDToString,
                                      char sidebarTeamLetter) {
    if (!env || !thePlayer) return;
    jclass playerClass = env->GetObjectClass(thePlayer);
    if (!playerClass) return;
    jmethodID midProfile = env->GetMethodID(playerClass, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;");
    if (!midProfile || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(playerClass);
        return;
    }
    jobject profile = env->CallObjectMethod(thePlayer, midProfile);
    if (!profile || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(playerClass);
        return;
    }
    jclass profileClass = env->GetObjectClass(profile);
    jmethodID midGetName = profileClass ? env->GetMethodID(profileClass, "getName", "()Ljava/lang/String;") : nullptr;
    jmethodID midGetId = profileClass ? env->GetMethodID(profileClass, "getId", "()Ljava/util/UUID;") : nullptr;
    std::string localName;
    if (midGetName) {
        jstring jName = (jstring)env->CallObjectMethod(profile, midGetName);
        if (!env->ExceptionCheck() && jName) {
            const char* raw = env->GetStringUTFChars(jName, nullptr);
            localName = StripMcFormatting(raw ? raw : "");
            if (raw) env->ReleaseStringUTFChars(jName, raw);
            env->DeleteLocalRef(jName);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }
    if (midGetId && midUUIDToString) {
        jobject uuidObj = env->CallObjectMethod(profile, midGetId);
        if (!env->ExceptionCheck() && uuidObj) {
            jstring jUuid = (jstring)env->CallObjectMethod(uuidObj, midUUIDToString);
            if (!env->ExceptionCheck() && jUuid) {
                const char* rawUuid = env->GetStringUTFChars(jUuid, nullptr);
                if (rawUuid && rawUuid[0]) {
                    g_localPlayerIdLower = LowerAscii(rawUuid);
                }
                if (rawUuid) env->ReleaseStringUTFChars(jUuid, rawUuid);
                env->DeleteLocalRef(jUuid);
            } else {
                env->ExceptionClear();
            }
            env->DeleteLocalRef(uuidObj);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }
    if (profileClass) env->DeleteLocalRef(profileClass);
    env->DeleteLocalRef(profile);
    env->DeleteLocalRef(playerClass);

    if (!localName.empty()) {
        g_localPlayerNameLower = LowerAscii(Trim(localName));
    }

    // BedWars team color only — SkyWars/Duels teams come from /who team lines.
    const OverlayGamemode activeMode = GetOverlayActiveGamemode();
    if (activeMode == OverlayGamemode::Bedwars) {
        std::string teamLetter = TeamLetterFromScoreboardName(env, scoreboard, midGetPlayersTeam,
                                                              scorePlayerTeamClass, midFormatPlayerName, localName);
        if (sidebarTeamLetter != 0) {
            g_localTeamColorResolved = true;
            g_localTeamLetter = std::string(1, sidebarTeamLetter);
        } else if (!teamLetter.empty() && teamLetter != "?") {
            g_localTeamLetter = teamLetter;
            g_localTeamColorResolved = true;
        }
    }
}

static bool IsLocalPlayer(const PlayerBedwarsStats& p) {
    if (!g_localPlayerIdLower.empty() && !p.playerId.empty()) {
        if (LowerAscii(p.playerId) == g_localPlayerIdLower) return true;
    }
    if (!g_localPlayerNameLower.empty()) {
        const std::string nameKey = LowerAscii(Trim(StripMcFormatting(p.name)));
        return !nameKey.empty() && nameKey == g_localPlayerNameLower;
    }
    return false;
}

static bool IsLocalPlayerOrTeammate(const PlayerBedwarsStats& p) {
    if (!g_localPlayerIdLower.empty() && !p.playerId.empty()) {
        if (LowerAscii(p.playerId) == g_localPlayerIdLower) return true;
    }
    const std::string nameKey = LowerAscii(Trim(StripMcFormatting(p.name)));
    if (!g_localPlayerNameLower.empty() && !nameKey.empty() && nameKey == g_localPlayerNameLower) {
        return true;
    }
    if (g_whoTeamResolved) {
        return g_whoTeammateNamesLower.find(nameKey) != g_whoTeammateNamesLower.end();
    }
    if (g_localTeamColorResolved &&
        !g_localTeamLetter.empty() && g_localTeamLetter != "?") {
        if (p.nameColor != 0xFFFFFF && !p.teamLetter.empty() && p.teamLetter != "?") {
            return std::toupper((unsigned char)p.teamLetter[0]) ==
                   std::toupper((unsigned char)g_localTeamLetter[0]);
        }
        if (PlayerHasResolvedTeamColor(p.nameColor, p.teamLetter)) {
            return std::toupper((unsigned char)p.teamLetter[0]) ==
                   std::toupper((unsigned char)g_localTeamLetter[0]);
        }
    }
    return false;
}

static const char* TeamLetterLabel(char letter) {
    switch (std::toupper((unsigned char)letter)) {
        case 'R': return "Red";
        case 'B': return "Blue";
        case 'G': return "Green";
        case 'Y': return "Yellow";
        case 'A': return "Aqua";
        case 'P': return "Pink";
        case 'W': return "White";
        case 'S': return "Gray";
        default: return "Unknown";
    }
}

static std::string s_lastTeamLogSig;

static void MaybeLogLocalTeamAndTeammates(const std::vector<PlayerBedwarsStats>& players) {
    (void)players;
}

static bool ShouldIgnoreTeammate(JNIEnv* env, const PlayerBedwarsStats& p) {
    if (!env || !g_customGuiClassGlobal) return false;
    if (!ReadGuiStaticBool(env, g_customGuiClassGlobal, "ignoreTeammates", false)) return false;
    return IsLocalPlayerOrTeammate(p);
}

static double ParseGuiDoubleLocal(const std::string& s, double fallback) {
    if (s.empty()) return fallback;
    try { return std::stod(s); } catch (...) { return fallback; }
}

static int ParseGuiIntLocal(const std::string& s, int fallback) {
    if (s.empty()) return fallback;
    try { return (int)std::stod(s); } catch (...) { return fallback; }
}

static ChatOverlaySettings ReadChatOverlaySettings(JNIEnv* env) {
    ChatOverlaySettings cfg;
    jclass gui = g_customGuiClassGlobal;
    if (!env || !gui) return cfg;
    cfg.enabled = ReadGuiStaticBool(env, gui, "chatOverlay", false);
    cfg.compactBlacklist = ReadGuiStaticBool(env, gui, "compactBlacklist", false);
    cfg.ignoreTeammates = ReadGuiStaticBool(env, gui, "ignoreTeammates", false);
    cfg.minStars = ParseGuiIntLocal(ReadGuiStaticString(env, gui, "chatOverlayMinStars", "100"), 100);
    cfg.minFkdr = ParseGuiDoubleLocal(ReadGuiStaticString(env, gui, "chatOverlayMinFkdr", "5"), 5.0);
    cfg.minSwKdr = ParseGuiDoubleLocal(ReadGuiStaticString(env, gui, "chatOverlayMinSwKdr", "1.5"), 1.5);
    return cfg;
}

static std::unordered_set<std::string> g_coDebugOnce;

static void CoDebugOnce(const std::string& key, const char* fmt, ...) {
    (void)key;
    (void)fmt;
}

static bool ShouldShowChatOverlay(OverlayGamemode mode, const CachedHypixelStats& h,
                                  const ChatOverlaySettings& cfg, const std::string& playerKey,
                                  const char* displayName) {
    const char* name = (displayName && displayName[0]) ? displayName : "?";
    switch (mode) {
        case OverlayGamemode::Unknown:
            CoDebugOnce("co:mode:unknown:" + playerKey,
                        "[Overlay] %s stats skip: gamemode unknown\n", name);
            return false;
        case OverlayGamemode::Duel:
            if (!h.hasDuels) {
                CoDebugOnce("co:noduel:" + playerKey,
                            "[Overlay] %s stats skip: no duels data\n", name);
                return false;
            }
            return true;
        case OverlayGamemode::BedwarsDuels:
            if (!h.hasDuels) {
                CoDebugOnce("co:nobwduel:" + playerKey,
                            "[Overlay] %s stats skip: no bw-duels data\n", name);
                return false;
            }
            return true;
        case OverlayGamemode::Skywars: {
            if (!h.hasSkywars) {
                CoDebugOnce("co:nosw:" + playerKey,
                            "[Overlay] %s stats skip: no skywars data\n", name);
                return false;
            }
            const double kdr = (double)h.swKills / (double)((std::max)(1, h.swDeaths));
            if (kdr < cfg.minSwKdr) {
                CoDebugOnce("co:swkdr:" + playerKey,
                            "[Overlay] %s stats skip: SW KDR %.2f < min %.2f\n",
                            name, kdr, cfg.minSwKdr);
                return false;
            }
            return true;
        }
        case OverlayGamemode::MurderMystery:
            if (!h.hasMurderMystery) {
                CoDebugOnce("co:nomm:" + playerKey,
                            "[Overlay] %s stats skip: no murder mystery data\n", name);
                return false;
            }
            return true;
        case OverlayGamemode::Bedwars:
        default: {
            if (!h.ok) {
                CoDebugOnce("co:nobw:" + playerKey,
                            "[Overlay] %s stats skip: no bedwars data\n", name);
                return false;
            }
            const double fkdr = (double)h.finalKills / (double)((std::max)(1, h.finalDeaths));
            if (h.stars < cfg.minStars && fkdr < cfg.minFkdr) {
                CoDebugOnce("co:bwthresh:" + playerKey,
                            "[Overlay] %s stats skip: stars %d < %d and FKDR %.2f < %.2f\n",
                            name, h.stars, cfg.minStars, fkdr, cfg.minFkdr);
                return false;
            }
            return true;
        }
    }
}

static std::string Sec() {
    return std::string(1, (char)0xC2) + (char)0xA7;
}

static std::string ColoredPlayerName(const PlayerBedwarsStats& p) {
    const std::string sec = Sec();
    const char nameCode = McColorCodeFromRgb(p.nameColor);
    return sec + std::string(1, nameCode) + (p.name.empty() ? "Player" : p.name);
}

static void SetPlayerDenickDisplayName(PlayerBedwarsStats& p, const std::string& realName, bool suspected) {
    if (realName.empty()) return;
    const std::string sec = Sec();
    const char nameCode = McColorCodeFromRgb(p.nameColor);
    std::string shown = realName;
    if (suspected) shown += "?";
    // Stats/chat label keeps nick (real). Name column uses tabFmt.
    p.statsRaw["name"] = sec + std::string(1, nameCode) + p.name + sec + "7 (" + sec + "f" + shown + sec + "7)";
    if (IsLocalPlayer(p)) {
        // Local player nicked: show real username in tab overlay instead of the nick.
        p.statsRaw["tabFmt"] = sec + std::string(1, nameCode) + shown;
    } else {
        p.statsRaw["tabFmt"] = sec + std::string(1, nameCode) + p.name + sec + "7 (" + sec + "f" + shown + sec + "7)";
    }
}

static void SetPlayerRealNameInTab(PlayerBedwarsStats& p, const std::string& realName, bool suspected) {
    if (realName.empty()) return;
    const std::string sec = Sec();
    const char nameCode = McColorCodeFromRgb(p.nameColor);
    std::string shown = realName;
    if (suspected) shown += "?";
    p.statsRaw["tabFmt"] = sec + std::string(1, nameCode) + shown;
}

static void ApplyLocalPlayerRealNameInTab(PlayerBedwarsStats& p) {
    if (!IsLocalPlayer(p) || g_localAccountName.empty()) return;
    const std::string nameKey = LowerAscii(Trim(StripMinecraftFormatting(p.name)));
    if (nameKey.empty() || nameKey == g_localAccountNameLower) return;
    SetPlayerDenickDisplayName(p, g_localAccountName, false);
}

static std::string ChatOverlayPlayerLabel(const PlayerBedwarsStats& p, bool isNick) {
    auto it = p.statsRaw.find("name");
    if (it != p.statsRaw.end() && !it->second.empty() && it->second.find('(') != std::string::npos) {
        return it->second;
    }
    const std::string sec = Sec();
    std::string label = ColoredPlayerName(p);
    if (isNick) label = sec + "c[NICK]" + sec + "r " + label;
    return label;
}

static std::string FrenchifyPrefix() {
    const std::string sec = Sec();
    return sec + "7[" + sec + "1Fre" + sec + "fnch" + sec + "cify" + sec + "7] ";
}

PlayerLookupChatOutput FormatPlayerLookupChatMessage(const PlayerLookupStats& st) {
    PlayerLookupChatOutput out;
    if (!st.resolved) return out;

    const CachedHypixelStats& h = st.hypixel;
    const OverlayGamemode mode = OverlayGamemodeFromString(st.lookupMode);
    const std::string sec = Sec();
    const std::string prefix = FrenchifyPrefix();
    const std::string correctedName = st.displayName.empty() ? st.playerName : st.displayName;
    const std::string nameColored = sec + std::string(1, st.rankColor) + correctedName;
    std::string prefixStars;
    std::string statDisplay;
    std::string body;

    if (!st.modeError.empty()) {
        body = prefix + sec + "c" + st.modeError;
    } else {
    switch (mode) {
        case OverlayGamemode::Bedwars: {
            const double fkdr = (double)h.finalKills / (double)((std::max)(1, h.finalDeaths));
            const std::string fkdrText = FormatRatio(fkdr);
            statDisplay = GetPrestigeColorText(h.stars) + " " +
                          sec + std::string(1, FkdrColorCode(fkdr)) + fkdrText + sec + "7FKDR";
            break;
        }
        case OverlayGamemode::Duel: {
            const double wlr = (double)h.duelWins / (double)((std::max)(1, h.duelLosses));
            const double kdr = (double)h.duelKills / (double)((std::max)(1, h.duelDeaths));
            const std::string wlrText = FormatDuelRatio(wlr);
            const std::string kdrText = FormatDuelRatio(kdr);
            statDisplay = "Wins: " + sec + std::string(1, DuelWinsColorCode(h.duelWins)) + std::to_string(h.duelWins) +
                          sec + "7 WLR: " + sec + std::string(1, WlrColorCode(wlr)) + wlrText +
                          sec + "7 KDR: " + sec + std::string(1, DuelKdrColorCode(kdr)) + kdrText;
            break;
        }
        case OverlayGamemode::Skywars: {
            prefixStars = (!h.swStarsFormatted.empty()
                ? h.swStarsFormatted
                : FormatSkywarsStarsDisplay(h.swLevel, h.swStars)) + " ";
            const double kdr = (double)h.swKills / (double)((std::max)(1, h.swDeaths));
            const double wlr = (double)h.swWins / (double)((std::max)(1, h.swLosses));
            const std::string kdrText = FormatFixed2(kdr);
            const std::string wlrText = FormatFixed2(wlr);
            statDisplay = "KDR: " + sec + std::string(1, KdrColorCode(kdr)) + kdrText +
                          sec + "7 WLR: " + sec + std::string(1, WlrColorCode(wlr)) + wlrText;
            break;
        }
        case OverlayGamemode::BedwarsDuels: {
            const int starsBw = h.ok ? h.stars : BedwarsExpToStars(h.bwExperience);
            const double fkdrBw = h.ok
                ? ((double)h.finalKills / (double)((std::max)(1, h.finalDeaths)))
                : 0.0;
            const double wlrDuel = (double)h.duelWins / (double)((std::max)(1, h.duelLosses));
            const double kdrDuel = (double)h.duelKills / (double)((std::max)(1, h.duelDeaths));
            const double wlrBwDuel = (double)h.duelWinsBw / (double)((std::max)(1, h.duelLossesBw));
            double part1 = std::cbrt(wlrDuel * kdrDuel * fkdrBw);
            part1 = std::pow(part1, 0.6);
            double part2 = std::sqrt((double)h.duelWins * (double)starsBw);
            part2 = std::pow(part2, 0.4);
            const double scoreFinal = part1 * part2;
            statDisplay = sec + "7BW-Duels: " + sec + std::string(1, BwdScoreColorCode(scoreFinal)) +
                          FormatFixed2(scoreFinal) + sec + "7 WLR: " +
                          sec + std::string(1, WlrColorCode(wlrBwDuel)) + FormatFixed2(wlrBwDuel);
            break;
        }
        default:
            body = prefix + sec + "cMode \"" + st.lookupMode + "\" not supported in /stats.";
            break;
    }
        if (body.empty()) {
            body = prefix + prefixStars + nameColored + sec + "7 - " + statDisplay;
        }
    }

    if (st.blacklisted) {
        const std::string rawType = !st.blacklistType.empty()
            ? st.blacklistType
            : (!st.seraph.tag.empty() ? st.seraph.tag : "Blacklisted");
        const SeraphRenderTag renderTag = ResolveSeraphRender(rawType);
        std::string typeText = FormatSeraphReportTitleCase(rawType);
        if (typeText.empty()) typeText = "Blacklisted";
        const std::string reasonText = StripMcFormatting(
            st.blacklistReason.empty() ? typeText : st.blacklistReason);
        const std::string daysText = (st.blacklistDaysAgo >= 0)
            ? (std::to_string(st.blacklistDaysAgo) + " days ago")
            : "unknown time";
        const std::string hoverValue =
            sec + std::string(1, renderTag.mcCode) + typeText + sec + "r\n" +
            reasonText + "\n" + sec + "7- " + daysText;
        out.isJson = true;
        out.message =
            "{\"text\":\"\",\"extra\":["
            "{\"text\":\"" + JsonEscapeChat(body) + "\"},"
            "{\"text\":\" [\",\"color\":\"gray\"},"
            "{\"text\":\"" + JsonEscapeChat(typeText) + "\",\"color\":\"" +
            McCodeToJsonColorName(renderTag.mcCode) + "\","
            "\"hoverEvent\":{\"action\":\"show_text\",\"value\":\"" + JsonEscapeChat(hoverValue) + "\"}},"
            "{\"text\":\"]\",\"color\":\"gray\"}"
            "]}";
        return out;
    }

    out.message = body + sec + "r";
    return out;
}

static std::unordered_set<std::string> g_blAnnouncedKeys;
static std::unordered_set<std::string> g_coAnnouncedKeys;

static std::string OverlayPlayerKey(const PlayerBedwarsStats& p) {
    const std::string nameKey = LowerAscii(Trim(StripMcFormatting(p.name)));
    return nameKey.empty() ? "?" : nameKey;
}

static bool ShouldAnnounceChatOnce(const std::string& key) {
    if (key.empty()) return false;
    return g_coAnnouncedKeys.insert(key).second;
}

static OverlayGamemode ResolveChatOverlayGamemode() {
    return GetOverlayActiveGamemode();
}

static std::string BlacklistAnnounceKey(const PlayerBedwarsStats& p) {
    const std::string nameKey = LowerAscii(Trim(StripMcFormatting(p.name)));
    if (!nameKey.empty()) return "n:" + nameKey;
    const std::string uuidKey = LowerAscii(p.playerId);
    return uuidKey.empty() ? std::string() : ("u:" + uuidKey);
}

static bool ShouldAnnounceBlacklistOnce(const PlayerBedwarsStats& p) {
    const std::string key = BlacklistAnnounceKey(p);
    if (key.empty()) return false;
    return g_blAnnouncedKeys.insert(key).second;
}

void ClearBlacklistChatAnnounced() {
    g_blAnnouncedKeys.clear();
}

void ClearChatOverlayAnnounced() {
    g_coAnnouncedKeys.clear();
    g_coDebugOnce.clear();
}

void ClearWhoTeamFromWho() {
    g_whoTeammateNamesLower.clear();
    g_whoTeamResolved = false;
    s_lastTeamLogSig.clear();
}

void ApplyWhoTeamLine(const std::vector<std::string>& teamNames) {
    if (teamNames.empty() || g_localPlayerNameLower.empty()) return;
    bool localOnTeam = false;
    for (const auto& n : teamNames) {
        if (LowerAscii(Trim(StripMcFormatting(n))) == g_localPlayerNameLower) {
            localOnTeam = true;
            break;
        }
    }
    if (!localOnTeam) return;

    g_whoTeammateNamesLower.clear();
    for (const auto& n : teamNames) {
        const std::string key = LowerAscii(Trim(StripMcFormatting(n)));
        if (!key.empty() && key != g_localPlayerNameLower) {
            g_whoTeammateNamesLower.insert(key);
        }
    }
    g_whoTeamResolved = true;
}

void ClearLocalPlayerTeam() {
    g_localPlayerNameLower.clear();
    g_localPlayerIdLower.clear();
    g_localTeamLetter.clear();
    g_localTeamColorResolved = false;
    ClearWhoTeamFromWho();
    ClearBedDisconnectTimers();
}

static void MaybeAnnounceBlacklist(JNIEnv* env,
                                   jobject mcInstance,
                                   jclass mcClass,
                                   const PlayerBedwarsStats& p,
                                   const CachedSeraphTag& tag) {
    if (!env || !mcInstance || !mcClass || !tag.resolved || !tag.blacklisted || tag.tag.empty()) return;
    if (ShouldIgnoreTeammate(env, p)) {
        (void)ShouldAnnounceBlacklistOnce(p);
        return;
    }
    if (!ShouldAnnounceBlacklistOnce(p)) return;

    const SeraphRenderTag renderTag = ResolveSeraphRender(tag.reportType.empty() ? tag.tag : tag.reportType);
    std::string typeText = StripMcFormatting(tag.reportType.empty() ? tag.tag : tag.reportType);
    if (typeText.empty()) typeText = "unknown";
    std::string daysText = (tag.daysAgo >= 0) ? (std::to_string(tag.daysAgo) + " days ago") : "unknown time";
    std::string reasonText = StripMcFormatting(tag.reason.empty() ? "unknown" : tag.reason);
    std::string metaText;
    if (!tag.tooltip.empty()) {
        std::string tipReason;
        ParseSeraphTooltipDetails(tag.tooltip, typeText, tipReason, metaText);
        if (!tipReason.empty() && reasonText == "unknown") reasonText = StripMcFormatting(tipReason);
    }
    const std::string whenText = StripMcFormatting(!metaText.empty() ? metaText : daysText);
    const ChatOverlaySettings cfg = ReadChatOverlaySettings(env);
    PlayBlacklistPlingSound(env, mcInstance, mcClass);

    if (cfg.compactBlacklist) {
        const std::string sec = Sec();
        const std::string playerNameColored = ColoredPlayerName(p);
        const std::string hoverValue =
            sec + std::string(1, renderTag.mcCode) + typeText + sec + "r\n" +
            reasonText + "\n" + sec + "7- " + whenText;
        std::string json =
            "{\"text\":\"\",\"extra\":["
            "{\"text\":\"" + JsonEscapeChat(FrenchifyPrefix()) + "\"},"
            "{\"text\":\"" + JsonEscapeChat(playerNameColored + " ") + "\"},"
            "{\"text\":\"[\",\"color\":\"gray\"},"
            "{\"text\":\"" + JsonEscapeChat(std::string(renderTag.text)) + "\",\"color\":\"" +
            McCodeToJsonColorName(renderTag.mcCode) + "\","
            "\"hoverEvent\":{\"action\":\"show_text\",\"value\":\"" + JsonEscapeChat(hoverValue) + "\"}},"
            "{\"text\":\"]\",\"color\":\"gray\"}";
        if (tag.verified) {
            json += ",{\"text\":\" \xE2\x9C\x94\",\"color\":\"green\"}";
        }
        json += "]}";
        PrintJsonChat(env, mcInstance, mcClass, json);
        return;
    }

    const std::string sec = Sec();
    const std::string playerNameColored = ColoredPlayerName(p);
    const std::string msg = FrenchifyPrefix() +
                          playerNameColored + sec + "r" + sec + "7 is blacklisted for " +
                          sec + std::string(1, renderTag.mcCode) + typeText +
                          (tag.verified ? (std::string(" ") + sec + "a" + "\xE2\x9C\x94") : std::string()) +
                          sec + "r" + sec + "7 (" + sec + "f" + whenText + sec + "r" + sec + "7) Reason: " +
                          sec + "f" + reasonText + sec + "r";
    PrintLocalChat(env, mcInstance, mcClass, msg);
}

static void MaybeAnnounceChatOverlay(JNIEnv* env,
                                     jobject mcInstance,
                                     jclass mcClass,
                                     const PlayerBedwarsStats& p,
                                     const CachedHypixelStats& h,
                                     bool isNick) {
    if (!env || !mcInstance || !mcClass) return;
    const std::string playerKey = OverlayPlayerKey(p);
    const char* displayName = p.name.empty() ? "?" : p.name.c_str();
    // SkyWars ingame can keep "Mode:" on scoreboard (g_modeState=Prequeue) while g_redState=Ingame.
    // Tab overlay uses g_tabOverlayStateAllowed; chat must follow ingame, not modeState alone.
    if (g_redState != "Ingame") {
        if (g_modeState == "Prequeue") {
            CoDebugOnce("co:prequeue:stats:" + playerKey,
                        "[Overlay] %s stats skip: prequeue (not ingame)\n", displayName);
        }
        return;
    }
    const ChatOverlaySettings cfg = ReadChatOverlaySettings(env);
    if (!cfg.enabled) {
        CoDebugOnce("co:disabled:stats",
                    "[Overlay] stats skip: chatOverlay disabled in GUI\n");
        return;
    }
    if (cfg.ignoreTeammates && IsLocalPlayerOrTeammate(p)) {
        CoDebugOnce("co:teammate:stats:" + playerKey,
                    "[Overlay] %s stats skip: teammate ignored\n", displayName);
        return;
    }

    const OverlayGamemode mode = ResolveChatOverlayGamemode();
    CoDebugOnce("co:trymode:" + playerKey + ":" + OverlayGamemodeToString(mode),
                "[Overlay] %s stats try mode=%s ok=%d sw=%d duel=%d\n",
                displayName, OverlayGamemodeToString(mode).c_str(),
                h.ok ? 1 : 0, h.hasSkywars ? 1 : 0, h.hasDuels ? 1 : 0);
    if (!ShouldShowChatOverlay(mode, h, cfg, playerKey, displayName)) return;

    const std::string sec = Sec();
    const std::string prefix = FrenchifyPrefix();
    const std::string playerName = ChatOverlayPlayerLabel(p, isNick);
    std::string msg;

    switch (mode) {
        case OverlayGamemode::Duel: {
            if (!h.hasDuels) {
                CoDebugOnce("co:noduel2:" + playerKey,
                            "[Overlay] %s stats skip: no duels data (switch)\n", displayName);
                return;
            }
            const double wlr = (double)h.duelWins / (double)((std::max)(1, h.duelLosses));
            const double kdr = (double)h.duelWins / (double)((std::max)(1, h.duelDeaths));
            const std::string wlrText = FormatDuelRatio(wlr);
            const std::string kdrText = FormatDuelRatio(kdr);
            msg = prefix + playerName + sec + "r" +
                  sec + "7 - Wins: " + sec + std::string(1, DuelWinsColorCode(h.duelWins)) + std::to_string(h.duelWins) +
                  sec + "7 WLR: " + sec + std::string(1, WlrColorCode(wlr)) + wlrText +
                  sec + "7 KDR: " + sec + std::string(1, DuelKdrColorCode(kdr)) + kdrText + sec + "r";
            break;
        }
        case OverlayGamemode::Skywars: {
            if (!h.hasSkywars) {
                CoDebugOnce("co:nosw2:" + playerKey,
                            "[Overlay] %s stats skip: no skywars data (switch)\n", displayName);
                return;
            }
            const double kdr = (double)h.swKills / (double)((std::max)(1, h.swDeaths));
            const double wlr = (double)h.swWins / (double)((std::max)(1, h.swLosses));
            const std::string stars = !h.swStarsFormatted.empty()
                ? h.swStarsFormatted
                : FormatSkywarsStarsDisplay(h.swLevel, h.swStars);
            const std::string kdrText = FormatFixed2(kdr);
            const std::string wlrText = FormatFixed2(wlr);
            msg = prefix + stars + " " + playerName + sec + "r" +
                  sec + "7 - KDR: " + sec + std::string(1, KdrColorCode(kdr)) + kdrText +
                  sec + "7 WLR: " + sec + std::string(1, WlrColorCode(wlr)) + wlrText + sec + "r";
            break;
        }
        case OverlayGamemode::BedwarsDuels: {
            if (!h.hasDuels) {
                CoDebugOnce("co:nobwduel2:" + playerKey,
                            "[Overlay] %s stats skip: no bw-duels data (switch)\n", displayName);
                return;
            }
            const int starsBw = h.ok ? h.stars : BedwarsExpToStars(h.bwExperience);
            const double fkdrBw = h.ok
                ? ((double)h.finalKills / (double)((std::max)(1, h.finalDeaths)))
                : 0.0;
            const double wlrDuel = (double)h.duelWins / (double)((std::max)(1, h.duelLosses));
            const double kdrDuel = (double)h.duelKills / (double)((std::max)(1, h.duelDeaths));
            const double wlrBwDuel = (double)h.duelWinsBw / (double)((std::max)(1, h.duelLossesBw));
            double part1 = std::cbrt(wlrDuel * kdrDuel * fkdrBw);
            part1 = std::pow(part1, 0.6);
            double part2 = std::sqrt((double)h.duelWins * (double)starsBw);
            part2 = std::pow(part2, 0.4);
            const double scoreFinal = part1 * part2;
            const std::string scoreText = FormatFixed2(scoreFinal);
            const std::string wlrText = FormatFixed2(wlrBwDuel);
            msg = prefix + playerName + sec + "r" + sec + "7 - BW-Duels: " +
                  sec + std::string(1, BwdScoreColorCode(scoreFinal)) + scoreText +
                  sec + "7 WLR: " + sec + std::string(1, WlrColorCode(wlrBwDuel)) + wlrText + sec + "r";
            break;
        }
        case OverlayGamemode::Bedwars: {
            if (!h.ok) {
                CoDebugOnce("co:nobw2:" + playerKey,
                            "[Overlay] %s stats skip: no bedwars data (switch)\n", displayName);
                return;
            }
            const double fkdr = (double)h.finalKills / (double)((std::max)(1, h.finalDeaths));
            const std::string fkdrText = FormatRatio(fkdr);
            msg = prefix + GetPrestigeColorText(h.stars) + " " + playerName + sec + "r" +
                  sec + "7 - FKDR: " + sec + std::string(1, FkdrColorCode(fkdr)) + fkdrText + sec + "r";
            break;
        }
        default:
            return;
    }

    if (msg.empty()) return;

    std::string announceKey = LowerAscii(p.playerId);
    if (isNick || announceKey.empty()) {
        const std::string nameKey = LowerAscii(Trim(StripMcFormatting(p.name)));
        if (!nameKey.empty()) announceKey = "stats:" + nameKey;
    } else {
        announceKey = "stats:" + announceKey;
    }
    if (!ShouldAnnounceChatOnce(announceKey)) {
        CoDebugOnce("co:dup:stats:" + announceKey,
                    "[Overlay] %s stats skip: already announced\n", displayName);
        return;
    }
    CoDebugOnce("co:ok:stats:" + announceKey,
                "[Overlay] %s stats announced mode=%s nick=%d\n",
                displayName, OverlayGamemodeToString(mode).c_str(), isNick ? 1 : 0);
    PrintLocalChat(env, mcInstance, mcClass, msg);
}

static void MaybeAnnounceNickOverlay(JNIEnv* env,
                                     jobject mcInstance,
                                     jclass mcClass,
                                     const PlayerBedwarsStats& p) {
    // Chat announces for denick are handled by DenickTryConsume* / number denicker
    // as "nick -> real". Do not emit a separate "is nicked" line (it stole the
    // nickmsg: once-slot and blocked the real denick message).
    (void)env;
    (void)mcInstance;
    (void)mcClass;
    (void)p;
}

static int FindFirstMcColor(const std::string& s) {
    for (size_t i = 0; i + 1 < s.size(); ++i) {
        if (s[i] == '\xA7') return McCodeToRgb(s[i + 1]);
    }
    for (size_t i = 0; i + 2 < s.size(); ++i) {
        if (s[i] == '\xC2' && s[i + 1] == '\xA7') return McCodeToRgb(s[i + 2]);
    }
    return 0xFFFFFF;
}

static char FindFirstMcColorCode(const std::string& s) {
    for (size_t i = 0; i + 1 < s.size(); ++i) {
        if (s[i] == '\xA7') {
            const char c = (char)std::tolower((unsigned char)s[i + 1]);
            if (IsMinecraftColorCode(c)) return c;
        }
    }
    for (size_t i = 0; i + 2 < s.size(); ++i) {
        if (s[i] == '\xC2' && s[i + 1] == '\xA7') {
            const char c = (char)std::tolower((unsigned char)s[i + 2]);
            if (IsMinecraftColorCode(c)) return c;
        }
    }
    return 0; // no color present — Bedwars White team has no color code
}

static void ApplyTabPlayerTeamColor(JNIEnv* env,
                                    jobject teamObj,
                                    const std::string& formatted,
                                    PlayerBedwarsStats& p,
                                    bool atlasReplayMode) {
    p.nameColor = FindFirstMcColor(formatted);
    char colorCode = FindFirstMcColorCode(formatted);
    const bool hadExplicitColor = (colorCode != 0);

    if (teamObj) {
        const int teamRgb = GetScoreboardTeamRgb(env, teamObj);
        if (teamRgb > 0 && teamRgb != 0xFFFFFF) {
            if (atlasReplayMode) {
                p.nameColor = teamRgb;
                colorCode = McColorCodeFromRgb(teamRgb);
            } else if (p.nameColor == 0xFFFFFF || !hadExplicitColor) {
                p.nameColor = teamRgb;
                colorCode = McColorCodeFromRgb(teamRgb);
            }
        }
    }

    char teamLetter = TeamLetterFromColorCode(colorCode);
    // Hypixel White has no color code — treat missing color as White.
    if (teamLetter == '-' && !hadExplicitColor) teamLetter = 'W';
    if (teamLetter == '-' && colorCode == 'f') teamLetter = 'W';
    if (teamLetter != '-') p.teamLetter = std::string(1, teamLetter);
}

static std::vector<ColumnConfig> BuildColumnsFromOrder(const std::vector<std::string>& orderedIds) {
    const std::vector<ColumnConfig> all = BuildAllColumns();
    std::unordered_map<std::string, ColumnConfig> byId;
    byId.reserve(all.size());
    for (const auto& col : all) byId[col.id] = col;

    std::vector<std::string> ids;
    ids.reserve(2 + orderedIds.size());
    ids.push_back("name");
    ids.push_back("hp");
    const OverlayGamemode activeMode = GetOverlayActiveGamemode();
    for (const auto& rawId : orderedIds) {
        if (rawId == "name" || rawId == "hp") continue;
        const std::string id = NormalizeOverlayColumnId(rawId, activeMode);
        if (id.empty()) continue;
        if (byId.find(id) != byId.end()) ids.push_back(id);
    }
    if (ids.size() == 2) {
        const std::vector<std::string> defaults = DefaultColumnsForGamemode(activeMode);
        ids.insert(ids.end(), defaults.begin(), defaults.end());
    }

    std::vector<ColumnConfig> out;
    out.reserve(ids.size());
    int x = 5;
    for (size_t i = 0; i < ids.size(); ++i) {
        auto it = byId.find(ids[i]);
        if (it == byId.end()) continue;
        ColumnConfig c = it->second;
        c.order = (int)i;
        c.enabled = true;
        c.xOffset = x;
        x += c.width + 3;
        out.push_back(c);
    }
    return out;
}

void ColumnManager::setColumns(const std::vector<ColumnConfig>& columns) {
    columns_ = columns;
    std::sort(columns_.begin(), columns_.end(), [](const ColumnConfig& a, const ColumnConfig& b) {
        return a.order < b.order;
    });
}

std::vector<ColumnConfig> ColumnManager::getActiveColumns() const {
    std::vector<ColumnConfig> active;
    active.reserve(columns_.size());
    for (const auto& c : columns_) {
        if (c.enabled) active.push_back(c);
    }
    return active;
}

int ColumnManager::computeTextX(int startX, const ColumnConfig& col, int textWidth) const {
    const int baseX = startX + col.xOffset;
    switch (col.align) {
        case TextAlign::Left:
            return baseX;
        case TextAlign::Center:
            return baseX + ((col.width - textWidth) / 2);
        case TextAlign::Right:
            return baseX + (col.width - textWidth);
        default:
            return baseX;
    }
}

void SnapshotStore::publish(const OverlaySnapshot& snapshot) {
    auto copy = std::make_shared<OverlaySnapshot>(snapshot);
    // OverlaySnapshot may carry JNI global refs for skins. SnapshotStore does not
    // own JNI lifetime, so drop these refs in stored copies to prevent leaks/UAF.
    for (auto& p : copy->players) {
        p.skinResourceGlobal = nullptr;
    }
    std::lock_guard<std::mutex> guard(mutex_);
    latest_ = copy;
}

std::shared_ptr<OverlaySnapshot> SnapshotStore::readLatest() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return latest_;
}

bool JniCache::init(JNIEnv* env) {
    if (initialized || !env) return initialized;

    jclass localMc = McFindClass(env, "net/minecraft/client/Minecraft");
    if (!localMc || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    clsMinecraft = (jclass)env->NewGlobalRef(localMc);
    env->DeleteLocalRef(localMc);

    if (!clsMinecraft) return false;

    jclass localScaled = McFindClass(env, "net/minecraft/client/gui/ScaledResolution");
    jclass localGui = McFindClass(env, "net/minecraft/client/gui/Gui");
    jclass localGuiIngame = McFindClass(env, "net/minecraft/client/gui/GuiIngame");
    jclass localTabOverlay = McFindClass(env, "net/minecraft/client/gui/GuiPlayerTabOverlay");
    jclass localIChatComponent = McFindClass(env, "net/minecraft/util/IChatComponent");
    jclass localFr = McFindClass(env, "net/minecraft/client/gui/FontRenderer");
    jclass localTm = McFindClass(env, "net/minecraft/client/renderer/texture/TextureManager");
    jclass localNhp = McFindClass(env, "net/minecraft/client/network/NetHandlerPlayClient");
    jclass localNpi = McFindClass(env, "net/minecraft/client/network/NetworkPlayerInfo");
    jclass localCollection = env->FindClass("java/util/Collection");
    jclass localIterator = env->FindClass("java/util/Iterator");
    jclass localGameProfile = McFindClass(env, "com/mojang/authlib/GameProfile");
    jclass localUUID = env->FindClass("java/util/UUID");

    if (env->ExceptionCheck() || !localScaled || !localGui || !localGuiIngame || !localTabOverlay || !localIChatComponent || !localFr || !localTm || !localNhp || !localNpi || !localCollection || !localIterator || !localGameProfile || !localUUID) {
        env->ExceptionClear();
        shutdown(env);
        return false;
    }

    clsScaledResolution = (jclass)env->NewGlobalRef(localScaled);
    clsGui = (jclass)env->NewGlobalRef(localGui);
    clsGuiIngame = (jclass)env->NewGlobalRef(localGuiIngame);
    clsPlayerTabOverlay = (jclass)env->NewGlobalRef(localTabOverlay);
    clsIChatComponent = (jclass)env->NewGlobalRef(localIChatComponent);
    clsFontRenderer = (jclass)env->NewGlobalRef(localFr);
    clsTextureManager = (jclass)env->NewGlobalRef(localTm);
    clsNetHandlerPlayClient = (jclass)env->NewGlobalRef(localNhp);
    clsNetworkPlayerInfo = (jclass)env->NewGlobalRef(localNpi);
    clsCollection = (jclass)env->NewGlobalRef(localCollection);
    clsIterator = (jclass)env->NewGlobalRef(localIterator);
    clsGameProfile = (jclass)env->NewGlobalRef(localGameProfile);
    clsUUID = (jclass)env->NewGlobalRef(localUUID);

    env->DeleteLocalRef(localScaled);
    env->DeleteLocalRef(localGui);
    env->DeleteLocalRef(localGuiIngame);
    env->DeleteLocalRef(localTabOverlay);
    env->DeleteLocalRef(localIChatComponent);
    env->DeleteLocalRef(localFr);
    env->DeleteLocalRef(localTm);
    env->DeleteLocalRef(localNhp);
    env->DeleteLocalRef(localNpi);
    env->DeleteLocalRef(localCollection);
    env->DeleteLocalRef(localIterator);
    env->DeleteLocalRef(localGameProfile);
    env->DeleteLocalRef(localUUID);

    midGetMinecraft = env->GetStaticMethodID(clsMinecraft, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");

    midScaledResCtor = env->GetMethodID(clsScaledResolution, "<init>", "(Lnet/minecraft/client/Minecraft;)V");
    midScaledWidth = env->GetMethodID(clsScaledResolution, "getScaledWidth", "()I");
    midScaledHeight = env->GetMethodID(clsScaledResolution, "getScaledHeight", "()I");

    midFontWidth = env->GetMethodID(clsFontRenderer, "getStringWidth", "(Ljava/lang/String;)I");
    midDrawStringShadow = env->GetMethodID(clsFontRenderer, "drawStringWithShadow", "(Ljava/lang/String;FFI)I");

    midGuiDrawScaledCustomRect = env->GetStaticMethodID(clsGui, "drawModalRectWithCustomSizedTexture", "(IIFFIIFF)V");
    midGuiDrawRect = env->GetStaticMethodID(clsGui, "drawRect", "(IIIII)V");

    midBindTexture = env->GetMethodID(clsTextureManager, "bindTexture", "(Lnet/minecraft/util/ResourceLocation;)V");

    midGetPlayerInfoMap = env->GetMethodID(clsNetHandlerPlayClient, "getPlayerInfoMap", "()Ljava/util/Collection;");
    midCollectionIterator = env->GetMethodID(clsCollection, "iterator", "()Ljava/util/Iterator;");
    midIteratorHasNext = env->GetMethodID(clsIterator, "hasNext", "()Z");
    midIteratorNext = env->GetMethodID(clsIterator, "next", "()Ljava/lang/Object;");

    midGetLocationSkin = env->GetMethodID(clsNetworkPlayerInfo, "getLocationSkin", "()Lnet/minecraft/util/ResourceLocation;");
    midGetGameProfile = env->GetMethodID(clsNetworkPlayerInfo, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;");
    midGetPing = env->GetMethodID(clsNetworkPlayerInfo, "getResponseTime", "()I");
    midGetDisplayName = env->GetMethodID(clsNetworkPlayerInfo, "getDisplayName", "()Lnet/minecraft/util/IChatComponent;");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        midGetDisplayName = nullptr;
    }
    midGetPlayerTeam = env->GetMethodID(clsNetworkPlayerInfo, "getPlayerTeam", "()Lnet/minecraft/scoreboard/ScorePlayerTeam;");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        midGetPlayerTeam = nullptr;
    }

    midGetProfileName = env->GetMethodID(clsGameProfile, "getName", "()Ljava/lang/String;");
    midGetProfileId = env->GetMethodID(clsGameProfile, "getId", "()Ljava/util/UUID;");
    midUUIDToString = env->GetMethodID(clsUUID, "toString", "()Ljava/lang/String;");
    midChatComponentGetText = env->GetMethodID(clsIChatComponent, "getUnformattedText", "()Ljava/lang/String;");
    midChatComponentGetFormattedText = env->GetMethodID(clsIChatComponent, "getFormattedText", "()Ljava/lang/String;");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        midChatComponentGetFormattedText = nullptr;
    }

    fidLocationFontTexture = env->GetFieldID(clsFontRenderer, "locationFontTexture", "Lnet/minecraft/util/ResourceLocation;");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        fidLocationFontTexture = nullptr;
    }

    jclass localGsm = McFindClass(env, "net/minecraft/client/renderer/GlStateManager");
    if (localGsm && !env->ExceptionCheck()) {
        clsGlStateManager = (jclass)env->NewGlobalRef(localGsm);
        midGsmResetColor = env->GetStaticMethodID(clsGlStateManager, "G", "()V");
        midGsmEnableTexture2D = env->GetStaticMethodID(clsGlStateManager, "w", "()V");
        midGsmEnableBlend = env->GetStaticMethodID(clsGlStateManager, "l", "()V");
        midGsmBlendFunc = env->GetStaticMethodID(clsGlStateManager, "b", "(II)V");
        midGsmEnableDepth = env->GetStaticMethodID(clsGlStateManager, "j", "()V");
        midGsmDepthMask = env->GetStaticMethodID(clsGlStateManager, "a", "(Z)V");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            midGsmResetColor = nullptr;
            midGsmEnableTexture2D = nullptr;
            midGsmEnableBlend = nullptr;
            midGsmBlendFunc = nullptr;
            midGsmEnableDepth = nullptr;
            midGsmDepthMask = nullptr;
        }
        env->DeleteLocalRef(localGsm);
    } else {
        env->ExceptionClear();
    }

    fidDisplayWidth = env->GetFieldID(clsMinecraft, "displayWidth", "I");
    fidDisplayHeight = env->GetFieldID(clsMinecraft, "displayHeight", "I");
    fidThePlayer = env->GetFieldID(clsMinecraft, "thePlayer", "Lnet/minecraft/client/entity/EntityPlayerSP;");
    fidIngameGui = env->GetFieldID(clsMinecraft, "ingameGUI", "Lnet/minecraft/client/gui/GuiIngame;");
    fidFontRenderer = env->GetFieldID(clsMinecraft, "fontRendererObj", "Lnet/minecraft/client/gui/FontRenderer;");
    fidTextureManager = env->GetFieldID(clsMinecraft, "renderEngine", "Lnet/minecraft/client/renderer/texture/TextureManager;");
    fidGuiTabOverlay = env->GetFieldID(clsGuiIngame, "overlayPlayerList", "Lnet/minecraft/client/gui/GuiPlayerTabOverlay;");
    fidTabFooter = env->GetFieldID(clsPlayerTabOverlay, "footer", "Lnet/minecraft/util/IChatComponent;");

    jclass clsPlayerSp = McFindClass(env, "net/minecraft/client/entity/EntityPlayerSP");
    if (clsPlayerSp && !env->ExceptionCheck()) {
        fidSendQueue = env->GetFieldID(clsPlayerSp, "sendQueue", "Lnet/minecraft/client/network/NetHandlerPlayClient;");
        env->DeleteLocalRef(clsPlayerSp);
    } else {
        env->ExceptionClear();
    }

    if (!midGetMinecraft || !midScaledResCtor || !midScaledWidth || !midScaledHeight || !midFontWidth || !midDrawStringShadow ||
        !midGuiDrawScaledCustomRect || !midGuiDrawRect || !midBindTexture || !midGetPlayerInfoMap || !midCollectionIterator || !midIteratorHasNext ||
        !midIteratorNext || !midGetLocationSkin || !midGetGameProfile || !midGetProfileName || !midGetProfileId || !midUUIDToString || !midGetPing || !fidDisplayWidth ||
        !midChatComponentGetText || !fidDisplayHeight || !fidThePlayer || !fidIngameGui || !fidGuiTabOverlay || !fidTabFooter || !fidFontRenderer || !fidTextureManager || !fidSendQueue || env->ExceptionCheck()) {
        env->ExceptionClear();
#ifdef FRENCHIFY_LUNAR
        ClassFinderLogLine("[FAIL] TabOverlay JniCache init (field/method missing — see JNI MISS lines)");
#endif
        shutdown(env);
        return false;
    }

#ifdef FRENCHIFY_LUNAR
    ClassFinderLogLine("[OK] TabOverlay JniCache init");
#endif
    initialized = true;
    return true;
}

void JniCache::shutdown(JNIEnv* env) {
    initialized = false;

    midGetMinecraft = nullptr;
    midScaledResCtor = nullptr;
    midScaledWidth = nullptr;
    midScaledHeight = nullptr;
    midFontWidth = nullptr;
    midDrawStringShadow = nullptr;
    midGuiDrawScaledCustomRect = nullptr;
    midGuiDrawRect = nullptr;
    midBindTexture = nullptr;
    midGetPlayerInfoMap = nullptr;
    midCollectionIterator = nullptr;
    midIteratorHasNext = nullptr;
    midIteratorNext = nullptr;
    midGetLocationSkin = nullptr;
    midGetGameProfile = nullptr;
    midGetProfileName = nullptr;
    midGetProfileId = nullptr;
    midUUIDToString = nullptr;
    midGetPing = nullptr;
    midGetDisplayName = nullptr;
    midGetPlayerTeam = nullptr;
    midChatComponentGetText = nullptr;
    midChatComponentGetFormattedText = nullptr;
    midGsmResetColor = nullptr;
    midGsmEnableTexture2D = nullptr;
    midGsmEnableBlend = nullptr;
    midGsmBlendFunc = nullptr;
    midGsmEnableDepth = nullptr;
    midGsmDepthMask = nullptr;

    fidDisplayWidth = nullptr;
    fidDisplayHeight = nullptr;
    fidFontRenderer = nullptr;
    fidTextureManager = nullptr;
    fidIngameGui = nullptr;
    fidGuiTabOverlay = nullptr;
    fidTabFooter = nullptr;
    fidThePlayer = nullptr;
    fidSendQueue = nullptr;
    fidLocationFontTexture = nullptr;

    if (!env) return;

    if (clsMinecraft) {
        env->DeleteGlobalRef(clsMinecraft);
        clsMinecraft = nullptr;
    }
    if (clsScaledResolution) {
        env->DeleteGlobalRef(clsScaledResolution);
        clsScaledResolution = nullptr;
    }
    if (clsGui) {
        env->DeleteGlobalRef(clsGui);
        clsGui = nullptr;
    }
    if (clsGuiIngame) {
        env->DeleteGlobalRef(clsGuiIngame);
        clsGuiIngame = nullptr;
    }
    if (clsPlayerTabOverlay) {
        env->DeleteGlobalRef(clsPlayerTabOverlay);
        clsPlayerTabOverlay = nullptr;
    }
    if (clsIChatComponent) {
        env->DeleteGlobalRef(clsIChatComponent);
        clsIChatComponent = nullptr;
    }
    if (clsFontRenderer) {
        env->DeleteGlobalRef(clsFontRenderer);
        clsFontRenderer = nullptr;
    }
    if (clsTextureManager) {
        env->DeleteGlobalRef(clsTextureManager);
        clsTextureManager = nullptr;
    }
    if (clsNetHandlerPlayClient) {
        env->DeleteGlobalRef(clsNetHandlerPlayClient);
        clsNetHandlerPlayClient = nullptr;
    }
    if (clsNetworkPlayerInfo) {
        env->DeleteGlobalRef(clsNetworkPlayerInfo);
        clsNetworkPlayerInfo = nullptr;
    }
    if (clsCollection) {
        env->DeleteGlobalRef(clsCollection);
        clsCollection = nullptr;
    }
    if (clsIterator) {
        env->DeleteGlobalRef(clsIterator);
        clsIterator = nullptr;
    }
    if (clsGameProfile) {
        env->DeleteGlobalRef(clsGameProfile);
        clsGameProfile = nullptr;
    }
    if (clsUUID) {
        env->DeleteGlobalRef(clsUUID);
        clsUUID = nullptr;
    }
    if (clsGlStateManager) {
        env->DeleteGlobalRef(clsGlStateManager);
        clsGlStateManager = nullptr;
    }
}

bool OverlayController::initialize(JNIEnv* env) {
    if (!cache_.init(env)) return false;

    g_glRestoreController = this;
    columnManager_.setColumns(BuildColumnsFromOrder({}));
    return true;
}

void OverlayController::applyColumnOrder(const std::vector<std::string>& orderedIds) {
    columnManager_.setColumns(BuildColumnsFromOrder(orderedIds));
}

void OverlayController::setApiKeys(const std::string& hypixelApiKey, const std::string& seraphApiKey,
                                   const std::string& winstreakApiKey, bool vegaProxy) {
    PlayerApi::SetKeys(hypixelApiKey, seraphApiKey, winstreakApiKey, vegaProxy);
}

void OverlayController::setOverlayOpacityPercent(int opacityPercent) {
    if (opacityPercent < 0) opacityPercent = 0;
    if (opacityPercent > 95) opacityPercent = 95;
    overlayOpacityPercent_.store(opacityPercent, std::memory_order_relaxed);
}

void OverlayController::setOverlayThemeColorRgb(int rgb) {
    rgb &= 0xFFFFFF;
    overlayThemeColorRgb_.store(rgb, std::memory_order_relaxed);
}

void OverlayController::setResourceHeaderEnabled(bool enabled) {
    resourceHeaderEnabled_.store(enabled, std::memory_order_relaxed);
}

void OverlayController::setResourceDiamondsEnabled(bool enabled) {
    resourceDiamondsEnabled_.store(enabled, std::memory_order_relaxed);
}

void OverlayController::setResourceEmeraldsEnabled(bool enabled) {
    resourceEmeraldsEnabled_.store(enabled, std::memory_order_relaxed);
}

void OverlayController::setResourceDisplayLocation(ResourceDisplayLocation location) {
    resourceDisplayLocation_.store((int)location, std::memory_order_relaxed);
}

struct ResourceDisplayOptions {
    bool showDiamonds = true;
    bool showEmeralds = true;
    bool compactLabels = true;
};

static std::string BuildResourceDisplayLine(const GameResourceStats& res, const ResourceDisplayOptions& opts) {
    if (!opts.showDiamonds && !opts.showEmeralds) return {};

    const std::string sec = "\xC2\xA7";
    const bool compact = opts.compactLabels && opts.showDiamonds && opts.showEmeralds;
    const std::string dLabel = compact ? "D" : "Diamonds";
    const std::string eLabel = compact ? "E" : "Emeralds";
    std::string out;

    if (opts.showDiamonds) {
        const std::string dTimer = res.diamonds > 0 ? std::to_string(res.diamonds) : "-";
        out += sec + "b" + dLabel + " (" + sec + "f" + std::to_string(res.diamondCount) + sec + "b" + "): " +
                sec + "7" + dTimer;
    }
    if (opts.showEmeralds) {
        if (!out.empty()) out += "  ";
        const std::string eTimer = res.emeralds > 0 ? std::to_string(res.emeralds) : "-";
        out += sec + "a" + eLabel + " (" + sec + "f" + std::to_string(res.emeraldCount) + sec + "a" + "): " +
                sec + "7" + eTimer;
    }
    return out;
}

// Score holder names must be unique and render invisibly (soft hyphens).
static const char kFrifyDiamondKey[] = "\xC2\xAD\xC2\xAD";
static const char kFrifyEmeraldKey[] = "\xC2\xAD\xC2\xAE";
static const char kFrifyLegacyFmtDiamondKey[] = "\xC2\xA7""0\xC2\xA7""1\xC2\xA7""r";
static const char kFrifyLegacyFmtEmeraldKey[] = "\xC2\xA7""0\xC2\xA7""2\xC2\xA7""r";
static const char kFrifyLegacyDiamondKey[] = "\x01""FRD";
static const char kFrifyLegacyEmeraldKey[] = "\x01""FRE";
static const char kFrifyLegacyZwspDiamondKey[] = "\xE2\x80\x8B""FRD";
static const char kFrifyLegacyZwspEmeraldKey[] = "\xE2\x80\x8B""FRE";
static const char kFrifyTeamDiamond[] = "frifydia";
static const char kFrifyTeamEmerald[] = "frifyeme";

static const char* kAllFrifyResourceScoreKeys[] = {
    kFrifyDiamondKey,
    kFrifyEmeraldKey,
    kFrifyLegacyFmtDiamondKey,
    kFrifyLegacyFmtEmeraldKey,
    kFrifyLegacyDiamondKey,
    kFrifyLegacyEmeraldKey,
    kFrifyLegacyZwspDiamondKey,
    kFrifyLegacyZwspEmeraldKey,
};

static bool IsFrifyResourceScoreKey(const std::string& name) {
    for (const char* key : kAllFrifyResourceScoreKeys) {
        if (name == key) return true;
    }
    return false;
}

static bool IsFrifyInjectedResourceLine(const std::string& raw) {
    if (IsFrifyResourceScoreKey(raw)) return true;
    const std::string plain = StripMcFormatting(raw);
    if (plain.rfind("D (", 0) == 0 && plain.find("): ") != std::string::npos) return true;
    if (plain.rfind("Diamonds (", 0) == 0 && plain.find("): ") != std::string::npos) return true;
    if (plain.rfind("Emeralds (", 0) == 0 && plain.find("): ") != std::string::npos) return true;
    return false;
}

static int GetMinSidebarScorePoints(JNIEnv* env,
                                    jobject scoreboard,
                                    jobject objective,
                                    const JniCache& cache) {
    int minPoints = INT_MAX;
    bool found = false;
    jclass scoreboardClass = env->GetObjectClass(scoreboard);
    jmethodID midGetSortedScores = scoreboardClass ? env->GetMethodID(scoreboardClass, "getSortedScores", "(Lnet/minecraft/scoreboard/ScoreObjective;)Ljava/util/Collection;") : nullptr;
    if (!midGetSortedScores || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (scoreboardClass) env->DeleteLocalRef(scoreboardClass);
        return 1;
    }

    jobject scoresCollection = env->CallObjectMethod(scoreboard, midGetSortedScores, objective);
    if (!env->ExceptionCheck() && scoresCollection) {
        jobject scoreIter = env->CallObjectMethod(scoresCollection, cache.midCollectionIterator);
        if (!env->ExceptionCheck() && scoreIter) {
            while (env->CallBooleanMethod(scoreIter, cache.midIteratorHasNext)) {
                jobject scoreObj = env->CallObjectMethod(scoreIter, cache.midIteratorNext);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    break;
                }
                if (!scoreObj) continue;

                jclass scoreClass = env->GetObjectClass(scoreObj);
                jmethodID midPlayerName = scoreClass ? env->GetMethodID(scoreClass, "getPlayerName", "()Ljava/lang/String;") : nullptr;
                jmethodID midScorePoints = scoreClass ? env->GetMethodID(scoreClass, "getScorePoints", "()I") : nullptr;
                std::string playerName;
                if (midPlayerName) {
                    jstring jName = (jstring)env->CallObjectMethod(scoreObj, midPlayerName);
                    if (!env->ExceptionCheck() && jName) {
                        const char* n = env->GetStringUTFChars(jName, nullptr);
                        playerName = n ? n : "";
                        if (n) env->ReleaseStringUTFChars(jName, n);
                        env->DeleteLocalRef(jName);
                    } else {
                        env->ExceptionClear();
                    }
                }
                if (!IsFrifyInjectedResourceLine(playerName) && midScorePoints) {
                    const int pts = env->CallIntMethod(scoreObj, midScorePoints);
                    if (!env->ExceptionCheck()) {
                        minPoints = (std::min)(minPoints, pts);
                        found = true;
                    }
                }
                if (scoreClass) env->DeleteLocalRef(scoreClass);
                env->DeleteLocalRef(scoreObj);
            }
            env->DeleteLocalRef(scoreIter);
        } else {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(scoresCollection);
    } else {
        env->ExceptionClear();
    }

    env->DeleteLocalRef(scoreboardClass);
    return found ? minPoints : 1;
}

static bool AcquireSidebarObjective(JNIEnv* env,
                                    jobject mcInstance,
                                    const JniCache& cache,
                                    jobject& outScoreboard,
                                    jobject& outObjective) {
    outScoreboard = nullptr;
    outObjective = nullptr;
    if (!env || !mcInstance || !cache.initialized) return false;

    jobject thePlayer = env->GetObjectField(mcInstance, cache.fidThePlayer);
    if (!thePlayer || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jclass playerClass = env->GetObjectClass(thePlayer);
    jmethodID midGetWorldScoreboard = playerClass ? env->GetMethodID(playerClass, "getWorldScoreboard", "()Lnet/minecraft/scoreboard/Scoreboard;") : nullptr;
    if (!midGetWorldScoreboard || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (playerClass) env->DeleteLocalRef(playerClass);
        env->DeleteLocalRef(thePlayer);
        return false;
    }

    outScoreboard = env->CallObjectMethod(thePlayer, midGetWorldScoreboard);
    if (!outScoreboard || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (playerClass) env->DeleteLocalRef(playerClass);
        env->DeleteLocalRef(thePlayer);
        return false;
    }

    jclass scoreboardClass = env->GetObjectClass(outScoreboard);
    jmethodID midGetObjectiveInSlot = scoreboardClass ? env->GetMethodID(scoreboardClass, "getObjectiveInDisplaySlot", "(I)Lnet/minecraft/scoreboard/ScoreObjective;") : nullptr;
    if (!midGetObjectiveInSlot || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(scoreboardClass);
        env->DeleteLocalRef(outScoreboard);
        outScoreboard = nullptr;
        if (playerClass) env->DeleteLocalRef(playerClass);
        env->DeleteLocalRef(thePlayer);
        return false;
    }

    outObjective = env->CallObjectMethod(outScoreboard, midGetObjectiveInSlot, (jint)1);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(scoreboardClass);
    if (playerClass) env->DeleteLocalRef(playerClass);
    env->DeleteLocalRef(thePlayer);
    if (!outObjective) {
        env->DeleteLocalRef(outScoreboard);
        outScoreboard = nullptr;
        return false;
    }
    return true;
}

static void SyncScoreboardResourceLineDirect(JNIEnv* env,
                                             jobject scoreboard,
                                             jobject objective,
                                             const std::string& displayLine,
                                             int sortScore) {
    if (displayLine.empty()) return;

    jclass scoreboardClass = env->GetObjectClass(scoreboard);
    jmethodID midGetValue = scoreboardClass ? env->GetMethodID(scoreboardClass, "getValueFromObjective", "(Ljava/lang/String;Lnet/minecraft/scoreboard/ScoreObjective;)Lnet/minecraft/scoreboard/Score;") : nullptr;
    if (!midGetValue || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(scoreboardClass);
        return;
    }

    jstring jKey = env->NewStringUTF(displayLine.c_str());
    if (!jKey) {
        env->DeleteLocalRef(scoreboardClass);
        return;
    }

    jobject scoreObj = env->CallObjectMethod(scoreboard, midGetValue, jKey, objective);
    if (!env->ExceptionCheck() && scoreObj) {
        jclass scoreClass = env->GetObjectClass(scoreObj);
        jmethodID midSetPoints = scoreClass ? env->GetMethodID(scoreClass, "setScorePoints", "(I)V") : nullptr;
        if (midSetPoints) {
            env->CallVoidMethod(scoreObj, midSetPoints, (jint)sortScore);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        if (scoreClass) env->DeleteLocalRef(scoreClass);
        env->DeleteLocalRef(scoreObj);
    } else {
        env->ExceptionClear();
    }

    env->DeleteLocalRef(jKey);
    env->DeleteLocalRef(scoreboardClass);
}

static void RemoveScoreboardResourceLine(JNIEnv* env,
                                         jobject scoreboard,
                                         jobject objective,
                                         const char* playerKey) {
    jclass scoreboardClass = env->GetObjectClass(scoreboard);
    jmethodID midRemoveFromTeams = scoreboardClass ? env->GetMethodID(scoreboardClass, "removePlayerFromTeams", "(Ljava/lang/String;)Z") : nullptr;
    jmethodID midRemove = scoreboardClass ? env->GetMethodID(scoreboardClass, "removeObjectiveFromEntity", "(Ljava/lang/String;Lnet/minecraft/scoreboard/ScoreObjective;)V") : nullptr;
    jstring jKey = env->NewStringUTF(playerKey);
    if (jKey) {
        if (midRemoveFromTeams) {
            env->CallBooleanMethod(scoreboard, midRemoveFromTeams, jKey);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        if (midRemove) {
            env->CallVoidMethod(scoreboard, midRemove, jKey, objective);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        env->DeleteLocalRef(jKey);
    }
    env->DeleteLocalRef(scoreboardClass);
}

static void RemoveAllFrifyResourceScoreLines(JNIEnv* env, jobject scoreboard, jobject objective) {
    for (const char* key : kAllFrifyResourceScoreKeys) {
        RemoveScoreboardResourceLine(env, scoreboard, objective, key);
    }
}

void OverlayController::addPlayersFromWhoList(const std::vector<std::string>& players) {
    PlayerApi::SetWhoAllowlist(players);
}

void OverlayController::mergePlayersFromWhoList(const std::vector<std::string>& players) {
    PlayerApi::MergeWhoAllowlist(players);
}

void OverlayController::resetWhoSession() {
    ClearWhoTeamFromWho();
    PlayerApi::ResetWhoSession();
}

void OverlayController::clearApiAllowlist() {
    PlayerApi::ClearAllowlist();
}

void OverlayController::clearPrequeueGhosts() {
    PlayerApi::ClearPrequeueGhosts();
}

void OverlayController::addOnlinePlayerForApi(const std::string& player, int chatNameColor) {
    if (player.empty()) return;
    PlayerApi::AddPrequeueSpeaker(player, chatNameColor);
}

void OverlayController::addSpeakerUuidForApi(const std::string& player, const std::string& playerUuid) {
    PlayerApi::SetSpeakerUuid(player, playerUuid);
}

void OverlayController::shutdown(JNIEnv* env) {
    if (env) {
        if (cachedSnapshotValid_) {
            releaseSnapshotResources(env, cachedSnapshot_);
            cachedSnapshotValid_ = false;
        }
        auto latest = snapshots_.readLatest();
        if (latest) {
            releaseSnapshotResources(env, *latest);
        }
    }
    auto empty = OverlaySnapshot{};
    snapshots_.publish(empty);
    PlayerApi::Shutdown();
    if (g_glRestoreController == this) {
        g_glRestoreController = nullptr;
    }
    cache_.shutdown(env);
}

void OverlayController::publishSnapshot(const OverlaySnapshot& snapshot) {
    snapshots_.publish(snapshot);
}

static std::string OverlayHeaderTitleForGamemode() {
    switch (GetOverlayActiveGamemode()) {
        case OverlayGamemode::Skywars: return "SkyWars Stats";
        case OverlayGamemode::Duel: return "Duels Stats";
        case OverlayGamemode::BedwarsDuels: return "BW Duels Stats";
        default: return "Bedwars Stats";
    }
}

OverlaySnapshot OverlayController::buildBasicSnapshot(int screenWidth, int screenHeight) const {
    OverlaySnapshot snap;
    snap.screenWidth = screenWidth;
    snap.screenHeight = screenHeight;
    snap.updatedAtMs = GetTickCount64();
    snap.session.headerTitle = OverlayHeaderTitleForGamemode();
    snap.session.resources.emeralds = 0;
    snap.session.resources.emeraldCount = 0;
    snap.session.resources.diamonds = 0;
    snap.session.resources.diamondCount = 0;
    snap.session.footerText = "";
    snap.session.matchState = "InGame";
    snap.columns = columnManager_.getActiveColumns();
    return snap;
}

OverlaySnapshot OverlayController::buildSnapshotFromMinecraft(JNIEnv* env, jobject mcInstance) const {
    OverlaySnapshot snap;
    snap.updatedAtMs = GetTickCount64();
    snap.session.headerTitle = OverlayHeaderTitleForGamemode();
    snap.session.matchState = "InGame";
    snap.columns = columnManager_.getActiveColumns();
    const ULONGLONG nowMs = GetTickCount64();

    if (!env || !mcInstance || !cache_.initialized) return snap;

    RefreshLocalAccountNameFromSession(env, mcInstance);

    // Use ScaledResolution (GUI space), not raw display pixels.
    jobject scaledRes = env->NewObject(cache_.clsScaledResolution, cache_.midScaledResCtor, mcInstance);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (scaledRes) {
        snap.screenWidth = env->CallIntMethod(scaledRes, cache_.midScaledWidth);
        snap.screenHeight = env->CallIntMethod(scaledRes, cache_.midScaledHeight);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(scaledRes);
    }
    if (snap.screenWidth <= 0 || snap.screenHeight <= 0) {
        snap.screenWidth = env->GetIntField(mcInstance, cache_.fidDisplayWidth);
        snap.screenHeight = env->GetIntField(mcInstance, cache_.fidDisplayHeight);
    }

    snap.session.resources.emeralds = 0;
    snap.session.resources.emeraldCount = 0;
    snap.session.resources.diamonds = 0;
    snap.session.resources.diamondCount = 0;
    snap.session.footerText = "";

    jobject ingameGui = env->GetObjectField(mcInstance, cache_.fidIngameGui);
    if (!env->ExceptionCheck() && ingameGui) {
        jobject tabOverlayObj = env->GetObjectField(ingameGui, cache_.fidGuiTabOverlay);
        if (!env->ExceptionCheck() && tabOverlayObj) {
            jobject footerComp = env->GetObjectField(tabOverlayObj, cache_.fidTabFooter);
            if (!env->ExceptionCheck() && footerComp) {
                const jmethodID midFooterText = cache_.midChatComponentGetFormattedText
                    ? cache_.midChatComponentGetFormattedText
                    : cache_.midChatComponentGetText;
                jstring jFooter = (jstring)env->CallObjectMethod(footerComp, midFooterText);
                if (!env->ExceptionCheck() && jFooter) {
                    const char* cFooter = env->GetStringUTFChars(jFooter, nullptr);
                    snap.session.footerText = cFooter ? cFooter : "";
                    if (cFooter) env->ReleaseStringUTFChars(jFooter, cFooter);
                    ParseResourceTimersFromFooter(
                        snap.session.footerText,
                        snap.session.resources.emeralds,
                        snap.session.resources.diamonds,
                        snap.session.resources.emeraldCount,
                        snap.session.resources.diamondCount);
                    snap.session.footerText = FilterFooterKeepFinalKillsLine(snap.session.footerText);
                    env->DeleteLocalRef(jFooter);
                } else {
                    env->ExceptionClear();
                }
                env->DeleteLocalRef(footerComp);
            } else {
                env->ExceptionClear();
            }
            env->DeleteLocalRef(tabOverlayObj);
        } else {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(ingameGui);
    } else {
        env->ExceptionClear();
    }

    jobject thePlayer = env->GetObjectField(mcInstance, cache_.fidThePlayer);
    if (!thePlayer) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return snap;
    }

    jobject sendQueue = env->GetObjectField(thePlayer, cache_.fidSendQueue);
    if (!sendQueue) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(thePlayer);
        return snap;
    }

    jobject infoCollection = env->CallObjectMethod(sendQueue, cache_.midGetPlayerInfoMap);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (!infoCollection) {
        env->DeleteLocalRef(sendQueue);
        env->DeleteLocalRef(thePlayer);
        return snap;
    }

    jobject iter = env->CallObjectMethod(infoCollection, cache_.midCollectionIterator);
        jobject scoreboard = nullptr;
        jclass scoreboardClass = nullptr;
        jmethodID midGetObjectiveInSlot = nullptr;
        jmethodID midGetSortedScores = nullptr;
        jmethodID midGetPlayersTeam = nullptr;
        jclass scorePlayerTeamClass = nullptr;
        jmethodID midFormatPlayerName = nullptr;
        std::unordered_map<std::string, int> hpByName;
        char sidebarTeamLetter = 0;

        jclass playerClass = env->GetObjectClass(thePlayer);
        jmethodID midGetWorldScoreboard = playerClass ? env->GetMethodID(playerClass, "getWorldScoreboard", "()Lnet/minecraft/scoreboard/Scoreboard;") : nullptr;
        if (midGetWorldScoreboard && !env->ExceptionCheck()) {
            scoreboard = env->CallObjectMethod(thePlayer, midGetWorldScoreboard);
            if (env->ExceptionCheck()) env->ExceptionClear();
        } else {
            env->ExceptionClear();
        }

        if (scoreboard) {
            scoreboardClass = env->GetObjectClass(scoreboard);
            midGetObjectiveInSlot = scoreboardClass ? env->GetMethodID(scoreboardClass, "getObjectiveInDisplaySlot", "(I)Lnet/minecraft/scoreboard/ScoreObjective;") : nullptr;
            midGetSortedScores = scoreboardClass ? env->GetMethodID(scoreboardClass, "getSortedScores", "(Lnet/minecraft/scoreboard/ScoreObjective;)Ljava/util/Collection;") : nullptr;
            midGetPlayersTeam = scoreboardClass ? env->GetMethodID(scoreboardClass, "getPlayersTeam", "(Ljava/lang/String;)Lnet/minecraft/scoreboard/ScorePlayerTeam;") : nullptr;
            scorePlayerTeamClass = McFindClass(env, "net/minecraft/scoreboard/ScorePlayerTeam");
            midFormatPlayerName = scorePlayerTeamClass
                ? env->GetStaticMethodID(scorePlayerTeamClass, "formatPlayerName", "(Lnet/minecraft/scoreboard/Team;Ljava/lang/String;)Ljava/lang/String;")
                : nullptr;
            if (env->ExceptionCheck()) env->ExceptionClear();

            if (midGetObjectiveInSlot && midGetSortedScores) {
                jobject objective0 = env->CallObjectMethod(scoreboard, midGetObjectiveInSlot, (jint)0);
                if (!env->ExceptionCheck() && objective0) {
                    jobject scoresCollection = env->CallObjectMethod(scoreboard, midGetSortedScores, objective0);
                    if (!env->ExceptionCheck() && scoresCollection) {
                        jobject scoreIter = env->CallObjectMethod(scoresCollection, cache_.midCollectionIterator);
                        if (!env->ExceptionCheck() && scoreIter) {
                            while (env->CallBooleanMethod(scoreIter, cache_.midIteratorHasNext)) {
                                jobject scoreObj = env->CallObjectMethod(scoreIter, cache_.midIteratorNext);
                                if (env->ExceptionCheck()) {
                                    env->ExceptionClear();
                                    break;
                                }
                                if (!scoreObj) continue;

                                jclass scoreClass = env->GetObjectClass(scoreObj);
                                jmethodID midScoreName = scoreClass ? env->GetMethodID(scoreClass, "getPlayerName", "()Ljava/lang/String;") : nullptr;
                                jmethodID midScorePoints = scoreClass ? env->GetMethodID(scoreClass, "getScorePoints", "()I") : nullptr;
                                std::string scoreName;
                                int scorePoints = 0;

                                if (midScoreName) {
                                    jstring jScoreName = (jstring)env->CallObjectMethod(scoreObj, midScoreName);
                                    if (!env->ExceptionCheck() && jScoreName) {
                                        const char* raw = env->GetStringUTFChars(jScoreName, nullptr);
                                        scoreName = raw ? raw : "";
                                        if (raw) env->ReleaseStringUTFChars(jScoreName, raw);
                                        env->DeleteLocalRef(jScoreName);
                                    } else {
                                        env->ExceptionClear();
                                    }
                                }
                                if (midScorePoints) {
                                    scorePoints = env->CallIntMethod(scoreObj, midScorePoints);
                                    if (env->ExceptionCheck()) {
                                        env->ExceptionClear();
                                        scorePoints = 0;
                                    }
                                }

                                if (!scoreName.empty() && scoreName[0] != '#') {
                                    hpByName[StripMcFormatting(scoreName)] = scorePoints;
                                }

                                if (scoreClass) env->DeleteLocalRef(scoreClass);
                                env->DeleteLocalRef(scoreObj);
                            }
                            env->DeleteLocalRef(scoreIter);
                        } else {
                            env->ExceptionClear();
                        }
                        env->DeleteLocalRef(scoresCollection);
                    } else {
                        env->ExceptionClear();
                    }
                    env->DeleteLocalRef(objective0);
                } else {
                    env->ExceptionClear();
                }

                // Resource timers from sidebar stage lines (Bed gone / Sudden Death included).
                if (g_modeState == "Prequeue") {
                    snap.session.resources = {};
                    s_lastGoodResources = {};
                } else {
                    std::vector<std::string> sidebarLines;
                    bool sidebarHasPink = false;
                    bool sidebarSawBedGone = false;
                    jobject objective1 = env->CallObjectMethod(scoreboard, midGetObjectiveInSlot, (jint)1);
                    if (!env->ExceptionCheck() && objective1) {
                        jobject scoresCollection = env->CallObjectMethod(scoreboard, midGetSortedScores, objective1);
                        if (!env->ExceptionCheck() && scoresCollection) {
                            jobject scoreIter = env->CallObjectMethod(scoresCollection, cache_.midCollectionIterator);
                            if (!env->ExceptionCheck() && scoreIter) {
                                while (env->CallBooleanMethod(scoreIter, cache_.midIteratorHasNext)) {
                                    jobject scoreObj = env->CallObjectMethod(scoreIter, cache_.midIteratorNext);
                                    if (env->ExceptionCheck()) {
                                        env->ExceptionClear();
                                        break;
                                    }
                                    if (!scoreObj) continue;

                                    jclass scoreClass = env->GetObjectClass(scoreObj);
                                    jmethodID midScoreName = scoreClass ? env->GetMethodID(scoreClass, "getPlayerName", "()Ljava/lang/String;") : nullptr;
                                    std::string scoreName;

                                    if (midScoreName) {
                                        jstring jScoreName = (jstring)env->CallObjectMethod(scoreObj, midScoreName);
                                        if (!env->ExceptionCheck() && jScoreName) {
                                            const char* raw = env->GetStringUTFChars(jScoreName, nullptr);
                                            scoreName = raw ? raw : "";
                                            if (raw) env->ReleaseStringUTFChars(jScoreName, raw);
                                            env->DeleteLocalRef(jScoreName);
                                        } else {
                                            env->ExceptionClear();
                                        }
                                    }

                                    if (!scoreName.empty() && scoreName[0] != '#') {
                                        if (IsFrifyInjectedResourceLine(scoreName)) {
                                            if (scoreClass) env->DeleteLocalRef(scoreClass);
                                            env->DeleteLocalRef(scoreObj);
                                            continue;
                                        }
                                        // Sidebar entries are often split between team prefix/suffix and name.
                                        // Rebuild full rendered line with ScorePlayerTeam formatter when possible.
                                        std::string line = scoreName;
                                        if (midGetPlayersTeam && midFormatPlayerName) {
                                            jstring jRawName = env->NewStringUTF(scoreName.c_str());
                                            if (jRawName) {
                                                jobject teamObj = env->CallObjectMethod(scoreboard, midGetPlayersTeam, jRawName);
                                                if (env->ExceptionCheck()) {
                                                    env->ExceptionClear();
                                                    teamObj = nullptr;
                                                }
                                                jstring jFormatted = (jstring)env->CallStaticObjectMethod(scorePlayerTeamClass, midFormatPlayerName, teamObj, jRawName);
                                                if (!env->ExceptionCheck() && jFormatted) {
                                                    const char* fmt = env->GetStringUTFChars(jFormatted, nullptr);
                                                    line = fmt ? fmt : line;
                                                    if (fmt) env->ReleaseStringUTFChars(jFormatted, fmt);
                                                    env->DeleteLocalRef(jFormatted);
                                                } else {
                                                    env->ExceptionClear();
                                                }
                                                if (teamObj) env->DeleteLocalRef(teamObj);
                                                env->DeleteLocalRef(jRawName);
                                            }
                                        }

                                        const std::string plainLower = LowerAscii(StripMcFormatting(line));
                                        sidebarLines.push_back(line);
                                        if (!sidebarSawBedGone && plainLower.find("bed gone") != std::string::npos) {
                                            sidebarSawBedGone = true;
                                        }
                                        if (sidebarTeamLetter == 0) {
                                            sidebarTeamLetter = TeamLetterFromSidebarYouLine(plainLower);
                                        }
                                        if (plainLower.find("pink:") != std::string::npos) {
                                            sidebarHasPink = true;
                                        }
                                        int parsedSec = 0;
                                        if (snap.session.resources.emeralds <= 0 &&
                                            ParseSidebarTimerSeconds(plainLower, "emerald", parsedSec)) {
                                            snap.session.resources.emeralds = parsedSec;
                                        }
                                        if (snap.session.resources.diamonds <= 0 &&
                                            ParseSidebarTimerSeconds(plainLower, "diamond", parsedSec)) {
                                            snap.session.resources.diamonds = parsedSec;
                                        }
                                    }

                                    if (scoreClass) env->DeleteLocalRef(scoreClass);
                                    env->DeleteLocalRef(scoreObj);
                                }
                                env->DeleteLocalRef(scoreIter);
                            } else {
                                env->ExceptionClear();
                            }
                            env->DeleteLocalRef(scoresCollection);
                        } else {
                            env->ExceptionClear();
                        }
                        env->DeleteLocalRef(objective1);
                    } else {
                        env->ExceptionClear();
                    }

                    // If the sidebar exposes only stage timer lines like
                    // "Diamond II in 5:51", derive full spawn counters and next timers.
                    int eCount = 0;
                    int eNext = 0;
                    int dCount = 0;
                    int dNext = 0;
                    const bool stageComputed =
                        ComputeResourcesFromStageSidebar(sidebarLines, sidebarHasPink, eCount, eNext, dCount, dNext);
                    if (stageComputed) {
                        snap.session.resources.emeraldCount = eCount;
                        snap.session.resources.emeralds = eNext;
                        snap.session.resources.diamondCount = dCount;
                        snap.session.resources.diamonds = dNext;
                    }

                    const bool resourcesEmpty =
                        snap.session.resources.emeralds <= 0 &&
                        snap.session.resources.diamonds <= 0 &&
                        snap.session.resources.emeraldCount <= 0 &&
                        snap.session.resources.diamondCount <= 0;
                    // Freeze only when "Bed gone" has no parseable timer (not obfuscated mm:ss).
                    bool bedGoneUntimedFreeze = sidebarSawBedGone && resourcesEmpty && !stageComputed;
                    if (bedGoneUntimedFreeze) {
                        s_lastGoodResources = {};
                    }

                    // Chat sometimes reports "Bed gone" before the sidebar line is visible (or at all).
                    // When that happens, forcibly invalidate cached resources to prevent a freeze
                    // until the next sidebar stage line arrives.
                    if (s_forceResourcesClear.exchange(false, std::memory_order_acq_rel)) {
                        snap.session.resources = {};
                        s_lastGoodResources = {};
                        bedGoneUntimedFreeze = true;
                    }

                    if (snap.session.resources.emeralds > 0 || snap.session.resources.diamonds > 0 ||
                        snap.session.resources.emeraldCount > 0 || snap.session.resources.diamondCount > 0) {
                        s_lastGoodResources = snap.session.resources;
                    } else if (!bedGoneUntimedFreeze &&
                               (s_lastGoodResources.emeralds > 0 || s_lastGoodResources.diamonds > 0 ||
                                s_lastGoodResources.emeraldCount > 0 || s_lastGoodResources.diamondCount > 0)) {
                        snap.session.resources = s_lastGoodResources;
                    }
                }
            }
        }
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (!iter) {
        env->DeleteLocalRef(infoCollection);
        env->DeleteLocalRef(sendQueue);
        env->DeleteLocalRef(thePlayer);
        return snap;
    }

    UpdateLocalPlayerIdentity(env, thePlayer, scoreboard, midGetPlayersTeam,
                              scorePlayerTeamClass, midFormatPlayerName,
                              cache_.midUUIDToString, sidebarTeamLetter);

    if (g_allowNickV2Display && !hpByName.empty()) {
        ReplayLogOnce("replay:sb:" + g_atlasReplayViewMode + ":" + std::to_string(hpByName.size()),
                      "scoreboard names=%d mode=%s", (int)hpByName.size(), g_atlasReplayViewMode.c_str());
        for (const auto& pr : hpByName) {
            if (pr.first.empty() || pr.first[0] == '#') continue;
            if (!LooksLikeMinecraftIgn(pr.first)) {
                ReplayLogOnce("sbskip:" + pr.first, "scoreboard skip not-ign name=%s hp=%d",
                              pr.first.c_str(), pr.second);
                continue;
            }
            PlayerApi::AllowPlayerForApi(pr.first);
            ReplayLogOnce("sballow:" + LowerAscii(pr.first),
                          "scoreboard allow name=%s hp=%d uuid=%s",
                          pr.first.c_str(), pr.second,
                          PlayerApi::GetCachedUuidForName(pr.first).c_str());
        }
    }

    std::unordered_set<std::string> snapshotNamesLower;
    int dbgPlayers = 0;
    int dbgNickPlayers = 0;
    int dbgNickNoTextures = 0;
    int dbgNickV1 = 0;
    int dbgNickV2 = 0;
    int dbgApiAllowed = 0;
    int dbgHypixelCacheOk = 0;
    int dbgNoApiAllowed = 0;

    while (env->CallBooleanMethod(iter, cache_.midIteratorHasNext)) {
        jobject infoObj = env->CallObjectMethod(iter, cache_.midIteratorNext);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            break;
        }
        if (!infoObj) continue;

        PlayerBedwarsStats p;
        dbgPlayers++;

        jobject gpObj = env->CallObjectMethod(infoObj, cache_.midGetGameProfile);
        if (env->ExceptionCheck()) env->ExceptionClear();

        std::string profileTextures;
        if (gpObj && DenickIsEnabled()) {
            profileTextures = DenickGetTexturesFromProfile(env, gpObj);
        }

        if (gpObj) {
            jstring jName = (jstring)env->CallObjectMethod(gpObj, cache_.midGetProfileName);
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (jName) {
                const char* cName = env->GetStringUTFChars(jName, nullptr);
                p.name = StripMcFormatting(cName ? cName : "");
                if (cName) env->ReleaseStringUTFChars(jName, cName);
                env->DeleteLocalRef(jName);
            }

            jobject uuidObj = env->CallObjectMethod(gpObj, cache_.midGetProfileId);
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (uuidObj) {
                jstring jUuid = (jstring)env->CallObjectMethod(uuidObj, cache_.midUUIDToString);
                if (!env->ExceptionCheck() && jUuid) {
                    const char* cUuid = env->GetStringUTFChars(jUuid, nullptr);
                    p.playerId = cUuid ? cUuid : "";
                    if (cUuid) env->ReleaseStringUTFChars(jUuid, cUuid);
                    env->DeleteLocalRef(jUuid);
                } else {
                    env->ExceptionClear();
                }
                env->DeleteLocalRef(uuidObj);
            }

            env->DeleteLocalRef(gpObj);
        }

        const int uuidVer = UuidVersion(p.playerId);
        if (uuidVer == 2 && !g_allowNickV2Display) {
            env->DeleteLocalRef(infoObj);
            continue;
        }

        p.ping = env->CallIntMethod(infoObj, cache_.midGetPing);
        if (env->ExceptionCheck()) env->ExceptionClear();

        jobject skinLoc = env->CallObjectMethod(infoObj, cache_.midGetLocationSkin);
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (skinLoc) {
            p.skinResourceGlobal = env->NewGlobalRef(skinLoc);
            env->DeleteLocalRef(skinLoc);
        }

        p.teamLetter = "?";
        p.stars = "[ERR]";
        p.fdRatio = "ERR";
        p.hp = 20;
        p.nameColor = 0xFFFFFF;

        const bool atlasReplayMode = g_allowNickV2Display;
        jobject tabTeamObj = nullptr;
        std::string formatted;

        if (atlasReplayMode && cache_.midGetDisplayName && cache_.midChatComponentGetFormattedText) {
            jobject displayComp = env->CallObjectMethod(infoObj, cache_.midGetDisplayName);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                displayComp = nullptr;
            }
            if (displayComp) {
                jstring jFmt = (jstring)env->CallObjectMethod(displayComp, cache_.midChatComponentGetFormattedText);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    jFmt = nullptr;
                }
                if (jFmt) {
                    const char* fmt = env->GetStringUTFChars(jFmt, nullptr);
                    formatted = fmt ? fmt : "";
                    if (fmt) env->ReleaseStringUTFChars(jFmt, fmt);
                    env->DeleteLocalRef(jFmt);
                }
                env->DeleteLocalRef(displayComp);
            }
        }

        if (atlasReplayMode && cache_.midGetPlayerTeam) {
            jobject npiTeam = env->CallObjectMethod(infoObj, cache_.midGetPlayerTeam);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                npiTeam = nullptr;
            }
            if (npiTeam) {
                if (tabTeamObj) env->DeleteLocalRef(tabTeamObj);
                tabTeamObj = npiTeam;
            }
        }

        if (formatted.empty() && scoreboard && midGetPlayersTeam && midFormatPlayerName && !p.name.empty()) {
            jstring jRawName = env->NewStringUTF(p.name.c_str());
            if (jRawName) {
                if (!tabTeamObj) {
                    tabTeamObj = env->CallObjectMethod(scoreboard, midGetPlayersTeam, jRawName);
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        tabTeamObj = nullptr;
                    }
                }

                jstring jFormatted = (jstring)env->CallStaticObjectMethod(scorePlayerTeamClass, midFormatPlayerName, tabTeamObj, jRawName);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    jFormatted = nullptr;
                }

                if (jFormatted) {
                    const char* fmt = env->GetStringUTFChars(jFormatted, nullptr);
                    formatted = fmt ? fmt : "";
                    if (fmt) env->ReleaseStringUTFChars(jFormatted, fmt);
                    env->DeleteLocalRef(jFormatted);
                }

                env->DeleteLocalRef(jRawName);
            }
        }

        if (!formatted.empty()) {
            if (atlasReplayMode) {
                p.statsRaw["tabFmt"] = formatted;
            }
            ApplyTabPlayerTeamColor(env, tabTeamObj, formatted, p, atlasReplayMode);
            if (atlasReplayMode && (p.teamLetter.empty() || p.teamLetter == "?" || p.teamLetter == "W")) {
                const char code = FindFirstMcColorCode(formatted);
                char tl = TeamLetterFromColorCode(code);
                if (tl == '-' && code == 0) tl = 'W';
                if (tl != '-') p.teamLetter = std::string(1, tl);
            }
        } else if (atlasReplayMode && tabTeamObj) {
            ApplyTabPlayerTeamColor(env, tabTeamObj, "", p, atlasReplayMode);
        }
        if ((p.teamLetter.empty() || p.teamLetter == "?") &&
            scoreboard && midGetPlayersTeam && midFormatPlayerName && !p.name.empty()) {
            const std::string tl = TeamLetterFromScoreboardName(env, scoreboard, midGetPlayersTeam,
                                                                  scorePlayerTeamClass, midFormatPlayerName, p.name);
            if (!tl.empty()) p.teamLetter = tl;
        }
        if (tabTeamObj) env->DeleteLocalRef(tabTeamObj);

        auto hpIt = hpByName.find(p.name);
        if (hpIt != hpByName.end()) {
            p.hp = hpIt->second;
        }
        p.hpColor = HpToColor(p.hp);
        // White team often has no § color — assign W before caching for Bed DC.
        if (p.teamLetter.empty() || p.teamLetter == "?") {
            if (!FindFirstMcColorCode(formatted) || p.nameColor == 0xFFFFFF) {
                p.teamLetter = "W";
            }
        }

        UpdateBedDisconnectTeamCache(p.name, p.teamLetter);

        {
            const bool ignoreTeammateFlags = g_customGuiClassGlobal &&
                ReadGuiStaticBool(env, g_customGuiClassGlobal, "ignoreTeammates", false) &&
                IsLocalPlayerOrTeammate(p);
            if (!ignoreTeammateFlags) {
                std::lock_guard<std::mutex> lock(g_flaggedPlayersMutex);
                const PlayerAcFlagCounts* matchedCounts = nullptr;
                const std::string* matchedCheck = nullptr;

                if (!p.playerId.empty()) {
                    auto countIt = g_flaggedPlayerCountsByUuid.find(p.playerId);
                    if (countIt != g_flaggedPlayerCountsByUuid.end() && AcWarningThresholdMet(countIt->second)) {
                        matchedCounts = &countIt->second;
                        auto flagIt = g_flaggedPlayersLastCheckByUuid.find(p.playerId);
                        if (flagIt != g_flaggedPlayersLastCheckByUuid.end()) matchedCheck = &flagIt->second;
                    }
                }
                if (!matchedCounts && atlasReplayMode && !p.name.empty()) {
                    const std::string nameKey = LowerAscii(Trim(StripMinecraftFormatting(p.name)));
                    auto countIt = g_flaggedPlayerCountsByNameLower.find(nameKey);
                    if (countIt != g_flaggedPlayerCountsByNameLower.end() && AcWarningThresholdMet(countIt->second)) {
                        matchedCounts = &countIt->second;
                        auto flagIt = g_flaggedPlayersLastCheckByNameLower.find(nameKey);
                        if (flagIt != g_flaggedPlayersLastCheckByNameLower.end()) matchedCheck = &flagIt->second;
                    }
                }

                if (matchedCounts) {
                    p.flagged = true;
                    p.flagCheck = matchedCheck ? *matchedCheck : "Keep-y";
                    if      (p.flagCheck == "LegitScaff") p.flagColor = 0xFFFF55;
                    else if (p.flagCheck == "Keep-y" || p.flagCheck == "ScaffoldA")  p.flagColor = 0xFF55FF;
                    else if (p.flagCheck == "NoSlow")    p.flagColor = 0xAA55FF;
                    else if (p.flagCheck == "AutoBlock") p.flagColor = 0xAA0000;
                    else if (p.flagCheck == "FlatScaff" || p.flagCheck == "ScaffoldB") p.flagColor = 0xFF5555;
                    else                                   p.flagColor = 0xFF5555;
                    (void)matchedCounts;
                }
            }
        }

        const int seenCount = PlayerApi::GetSeenCount(p.name.empty() ? std::string("?") : p.name);

        p.statsRaw["tags"] = "-";
        p.statsRaw["name"] = p.name;
        p.statsRaw["hp"] = std::to_string(p.hp);
        p.statsRaw["stars"] = "";
        p.statsRaw["fkdr"] = "";
        p.statsRaw["kdr"] = "";
        p.statsRaw["index"] = "";
        p.statsRaw["swordKD"] = "";
        p.statsRaw["requeuePct"] = "";
        p.statsRaw["wlr"] = "";
        p.statsRaw["wins"] = "";
        p.statsRaw["finalKills"] = "";
        p.statsRaw["bedsBroken"] = "";
        p.statsRaw["bblr"] = "";
        p.statsRaw["winstreak"] = "";
        p.statsRaw["sw_stars"] = "";
        p.statsRaw["sw_kdr"] = "";
        p.statsRaw["sw_wlr"] = "";
        p.statsRaw["sw_wins"] = "";
        p.statsRaw["sw_kills"] = "";
        p.statsRaw["duel_wins"] = "";
        p.statsRaw["duel_wlr"] = "";
        p.statsRaw["duel_kdr"] = "";
        p.statsRaw["bwd_wlr"] = "";
        p.statsRaw["bwd_index"] = "";
        p.statsRaw["seens"] = std::to_string(seenCount);
        p.statsRaw["session"] = "";
        p.statsRaw["ping"] = "";
        p.statsRaw["pingvar"] = "";
        p.statsRaw["seraph"] = "";

        const bool isNick = (uuidVer == 1) ||
            (atlasReplayMode && LooksLikeMinecraftIgn(p.name) && PlayerApi::IsPrequeueNick(p.name));
        if (atlasReplayMode && isNick) {
            ReplayLogOnce("nick:" + LowerAscii(p.name),
                          "nick name=%s ver=%d", p.name.c_str(), uuidVer);
        }
        if (isNick) {
            CoDebugOnce("co:nickseen:" + OverlayPlayerKey(p),
                        "[Overlay] %s detected nick (uuid v%d)\n", p.name.c_str(), uuidVer);
            dbgNickPlayers++;
            if (uuidVer == 1) dbgNickV1++;
            else if (uuidVer == 2) dbgNickV2++;
            std::string skinHashRl;
            if (p.skinResourceGlobal) {
                skinHashRl = DenickExtractSkinHashFromResourceLocation(env, p.skinResourceGlobal);
            }
            if (DenickIsEnabled() && skinHashRl.empty() && profileTextures.empty()) {
                DenickEntitySkinProbe entProbe{};
                if (DenickResolveSkinFromEntityPlayer(env, mcInstance, p.name, entProbe) && entProbe.found) {
                    if (!entProbe.texturesBase64.empty()) profileTextures = entProbe.texturesBase64;
                    if (!entProbe.skinHash.empty()) skinHashRl = entProbe.skinHash;
                }
            }
            if (DenickIsEnabled() && skinHashRl.empty() && profileTextures.empty()) {
                jobject gpForSkin = env->CallObjectMethod(infoObj, cache_.midGetGameProfile);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    gpForSkin = nullptr;
                }
                if (gpForSkin) {
                    skinHashRl = DenickGetSkinHashFromSkinManager(env, mcInstance, gpForSkin);
                    if (skinHashRl.empty()) {
                        const std::string texFromMgr = DenickGetTexturesFromProfile(env, gpForSkin);
                        if (!texFromMgr.empty()) {
                            profileTextures = texFromMgr;
                        }
                    }
                    env->DeleteLocalRef(gpForSkin);
                }
            }
            DenickLogTabNickSeen(p.name, uuidVer, profileTextures.size(), skinHashRl.size());
            NumberDenickerRegisterNick(p.name);
            if (DenickIsEnabled()) {
                if (profileTextures.empty() && skinHashRl.empty()) {
                    dbgNickNoTextures++;
                } else {
                    DenickOnNickPlayer(p.name, profileTextures, skinHashRl, p.playerId);
                }
            }
            bool denickApplied = false;
            NumberDenickerOverlayData numDenick{};
            if (NumberDenickerGetOverlayData(p.name, numDenick)) {
                if (numDenick.resolved && numDenick.hypOk) {
                    ApplyOverlayStatsForGamemode(p, numDenick.hyp, GetOverlayActiveGamemode());
                    MaybeAnnounceChatOverlay(env, mcInstance, cache_.clsMinecraft, p, numDenick.hyp, true);
                    denickApplied = true;
                }
            }
            DenickOverlayData denick{};
            if (!denickApplied && DenickIsEnabled() && DenickGetOverlayData(p.name, denick)) {
                if (denick.state == DenickOverlayData::State::Denicked && denick.hypOk) {
                    ApplyOverlayStatsForGamemode(p, denick.hyp, GetOverlayActiveGamemode());
                    MaybeAnnounceChatOverlay(env, mcInstance, cache_.clsMinecraft, p, denick.hyp, true);
                    if (!denick.realUuid.empty()) {
                        p.playerId = denick.realUuid;
                    }
                    if (denick.seraphOk) {
                        CachedSeraphTag seraph = denick.seraph;
                        ApplySeraphTagToPlayer(p, seraph);
                        MaybeAnnounceBlacklist(env, mcInstance, cache_.clsMinecraft, p, seraph);
                    }
                    denickApplied = true;
                }
            }
            if (atlasReplayMode && denickApplied) {
                ReplayLogOnce("denick:" + LowerAscii(p.name), "denick stats name=%s", p.name.c_str());
            }
            if (!denickApplied) {
                // Unresolved nick: keep [NICK]. Local player still shows Session/real account name.
                SetPrimaryStatText(p, "\xC2\xA7""c[NICK]", 0xFF5555);
                p.statsRaw["tags"] = "NICK";
                p.statsColor["tags"] = 0xFF5555;
                if (IsLocalPlayer(p)) {
                    std::string realName;
                    if (denick.state == DenickOverlayData::State::Denicked && !denick.realName.empty()) {
                        realName = denick.realName;
                    }
                    if (realName.empty()) realName = g_localAccountName;
                    if (!realName.empty()) {
                        SetPlayerDenickDisplayName(p, realName, false);
                    }
                }
            }
            MaybeAnnounceNickOverlay(env, mcInstance, cache_.clsMinecraft, p);
        } else if (!p.playerId.empty()) {
            std::string statsUuid = p.playerId;
            bool apiAllowed = PlayerApi::IsAllowed(p.name);
            if (atlasReplayMode) {
                statsUuid = ResolveReplayApiUuid(p.name, p.playerId);
                apiAllowed = LooksLikeMinecraftIgn(p.name);
                ReplayLogOnce(std::string("tab:") + LowerAscii(p.name) + (statsUuid.empty() ? ":wait" : ":uuid"),
                              "tab name=%s tabUuid=%s ver=%d statsUuid=%s",
                              p.name.c_str(), p.playerId.c_str(), uuidVer,
                              statsUuid.empty() ? "-" : statsUuid.c_str());
                if (apiAllowed) dbgApiAllowed++;
            } else if (apiAllowed) {
                PlayerApi::OnTabPlayer(p.name, p.playerId);
                dbgApiAllowed++;
            } else {
                dbgNoApiAllowed++;
            }

            if (DenickIsEnabled() && !profileTextures.empty()) {
                if (atlasReplayMode) {
                    if (!statsUuid.empty()) DenickRegisterRealSkin(p.name, profileTextures);
                } else if (uuidVer == 4) {
                    DenickRegisterRealSkin(p.name, profileTextures);
                }
            }

            CachedHypixelStats h{};
            const bool hHas = !statsUuid.empty() && PlayerApi::GetHypixel(statsUuid, h);
            CachedSeraphTag s{};
            const bool sHas = !statsUuid.empty() && PlayerApi::GetSeraph(statsUuid, s);

            if (hHas && (h.ok || h.hasSkywars || h.hasDuels || h.hasMurderMystery)) {
                dbgHypixelCacheOk++;
                if (atlasReplayMode) {
                    ReplayLogOnce("hyp:" + LowerAscii(p.name),
                                  "stats ok name=%s uuid=%s",
                                  p.name.c_str(), statsUuid.c_str());
                }
                ApplyOverlayStatsForGamemode(p, h, GetOverlayActiveGamemode());
                MaybeAnnounceChatOverlay(env, mcInstance, cache_.clsMinecraft, p, h, false);
            } else if (apiAllowed || atlasReplayMode) {
                SetPrimaryStatText(p, "\xC2\xA7""7-", 0xAAAAAA);
                if (apiAllowed) {
                    if (!hHas) {
                        CoDebugOnce("co:waitapi:" + OverlayPlayerKey(p),
                                    "[Overlay] %s waiting Hypixel API (allowlisted)\n", p.name.c_str());
                    } else {
                        CoDebugOnce("co:emptyapi:" + OverlayPlayerKey(p),
                                    "[Overlay] %s API ok=%d sw=%d duel=%d\n",
                                    p.name.c_str(), h.ok ? 1 : 0, h.hasSkywars ? 1 : 0, h.hasDuels ? 1 : 0);
                    }
                }
            } else {
                SetPrimaryStatText(p, "\xC2\xA7""7-", 0xAAAAAA);
                CoDebugOnce("co:notallow:" + OverlayPlayerKey(p),
                            "[Overlay] %s not on /who allowlist\n", p.name.c_str());
            }

            if (sHas) {
                ApplySeraphTagToPlayer(p, s);
                MaybeAnnounceBlacklist(env, mcInstance, cache_.clsMinecraft, p, s);
            }

            CachedWinstreakPing ws{};
            const bool wsHas = !statsUuid.empty() && PlayerApi::GetWinstreakPing(statsUuid, ws);
            if (wsHas) {
                ApplyWinstreakPingToPlayer(p, ws);
            } else if (!p.playerId.empty() && apiAllowed) {
                p.statsRaw["ping"] = "API";
                p.statsColor["ping"] = 0x555555;
                p.statsRaw["pingvar"] = "API";
                p.statsColor["pingvar"] = 0x555555;
            }
        }

        p.statsColor["seens"] = ColorForSeens((double)seenCount);
        if (p.statsRaw["winstreak"] == "API") p.statsColor["winstreak"] = 0x555555;
        if (p.statsRaw["session"].empty()) p.statsColor["session"] = 0x555555;
        if (p.statsRaw["tags"] == "NICK") p.statsColor["tags"] = 0xFF5555;

        // Local nicked player: always prefer real account name in the name column.
        ApplyLocalPlayerRealNameInTab(p);

        if (!p.name.empty()) {
            snapshotNamesLower.insert(LowerAscii(Trim(StripMinecraftFormatting(p.name))));
        }

        std::string nickChat, realChat;
        if (DenickTryConsumeDenickChat(p.name, nickChat, realChat)) {
            const std::string sec = std::string(1, (char)0xC2) + (char)0xA7;
            const char nameCode = McColorCodeFromRgb(p.nameColor);
            const std::string msg = sec + "7[" + sec + "1Fre" + sec + "fnch" + sec + "cify" + sec + "7] " +
                                    sec + std::string(1, nameCode) + p.name + sec + "r" + sec + "7 -> " +
                                    sec + "f" + realChat;
            PrintLocalChat(env, mcInstance, cache_.clsMinecraft, msg);
        } else if (NumberDenickerTryConsumeDenickChat(p.name, nickChat, realChat)) {
            const std::string sec = std::string(1, (char)0xC2) + (char)0xA7;
            const char nameCode = McColorCodeFromRgb(p.nameColor);
            const std::string msg = sec + "7[" + sec + "1Fre" + sec + "fnch" + sec + "cify" + sec + "7] " +
                                    sec + std::string(1, nameCode) + p.name + sec + "r" + sec + "7 -> " +
                                    sec + "f" + realChat;
            PrintLocalChat(env, mcInstance, cache_.clsMinecraft, msg);
        } else if (g_redState == "Ingame") {
            // Known nick skin only (unresolved): clear pending, no chat.
            // Chat is reserved for resolved denicks: nick -> realName.
            std::string knownNick;
            (void)DenickTryConsumeKnownNickChat(p.name, knownNick);
        }

        snap.players.push_back(std::move(p));
        env->DeleteLocalRef(infoObj);
    }

    if (g_allowNickV2Display) {
        {
            static ULONGLONG s_lastReplaySnapLog = 0;
            const ULONGLONG nowLog = GetTickCount64();
            if (nowLog - s_lastReplaySnapLog >= 2000) {
                s_lastReplaySnapLog = nowLog;
                std::string sb;
                for (const auto& kv : hpByName) {
                    if (!sb.empty()) sb += ",";
                    sb += kv.first;
                    if (sb.size() > 400) {
                        sb += "...";
                        break;
                    }
                }
                ReplayLogf("snap mode=%s tab=%d api=%d hypOk=%d sbNames=%d pending=%d allow=%d sb=[%s]",
                           g_atlasReplayViewMode.c_str(), dbgPlayers, dbgApiAllowed, dbgHypixelCacheOk,
                           (int)hpByName.size(), PlayerApi::PendingCount(), PlayerApi::AllowlistSize(),
                           sb.c_str());
            }
        }
        for (const auto& kv : hpByName) {
            const std::string plainName = kv.first;
            if (plainName.empty() || plainName[0] == '#') continue;
            if (IsFrifyInjectedResourceLine(plainName)) continue;
            if (!LooksLikeMinecraftIgn(plainName)) continue;
            const std::string nameLower = LowerAscii(Trim(plainName));
            if (snapshotNamesLower.count(nameLower)) continue;

            PlayerBedwarsStats ghost;
            ghost.name = plainName;
            ghost.teamLetter = "";
            ghost.hp = kv.second;
            ghost.hpColor = HpToColor(ghost.hp);
            ghost.nameColor = 0xFFFFFF;
            ghost.statsRaw["name"] = ghost.name;
            ghost.statsRaw["hp"] = std::to_string(ghost.hp);
            ghost.statsRaw["stars"] = "";
            ghost.statsRaw["stars_new"] = "";
            ghost.statsRaw["fkdr"] = "";
            ghost.statsRaw["kdr"] = "";
            ghost.statsRaw["index"] = "";
            ghost.statsRaw["swordKD"] = "";
            ghost.statsRaw["requeuePct"] = "";
            ghost.statsRaw["wlr"] = "";
            ghost.statsRaw["wins"] = "";
            ghost.statsRaw["finalKills"] = "";
            ghost.statsRaw["bedsBroken"] = "";
            ghost.statsRaw["bblr"] = "";
            ghost.statsRaw["winstreak"] = "";
            ghost.statsRaw["sw_stars"] = "";
            ghost.statsRaw["sw_kdr"] = "";
            ghost.statsRaw["sw_wlr"] = "";
            ghost.statsRaw["sw_wins"] = "";
            ghost.statsRaw["sw_kills"] = "";
            ghost.statsRaw["duel_wins"] = "";
            ghost.statsRaw["duel_wlr"] = "";
            ghost.statsRaw["duel_kdr"] = "";
            ghost.statsRaw["bwd_wlr"] = "";
            ghost.statsRaw["bwd_index"] = "";
            ghost.statsRaw["seens"] = std::to_string(PlayerApi::GetSeenCount(plainName));
            ghost.statsRaw["session"] = "";
            ghost.statsRaw["ping"] = "";
            ghost.statsRaw["pingvar"] = "";
            ghost.statsRaw["seraph"] = "";
            ghost.statsRaw["tags"] = "-";
            ghost.statsColor["seens"] = ColorForSeens(1.0);
            ghost.statsColor["ping"] = 0x555555;
            ghost.statsColor["pingvar"] = 0x555555;
            ghost.statsColor["seraph"] = 0x555555;
            ghost.statsColor["session"] = 0x555555;

            PlayerApi::AllowPlayerForApi(plainName);
            std::string uuid = PlayerApi::GetCachedUuidForName(plainName);
            ReplayLogOnce(std::string("ghost:") + nameLower + (uuid.empty() ? ":wait" : ":uuid"),
                          "ghost name=%s uuid=%s", plainName.c_str(), uuid.empty() ? "-" : uuid.c_str());
            if (!uuid.empty()) {
                ghost.playerId = uuid;
                PlayerApi::OnTabPlayer(plainName, uuid);
            }

            const bool ghostNick = PlayerApi::IsPrequeueNick(plainName);
            if (ghostNick) NumberDenickerRegisterNick(plainName);
            if (ghostNick && TryApplyDenickOverlayStats(env, mcInstance, cache_.clsMinecraft, ghost)) {
                ReplayLogOnce("ghostdenick:" + nameLower, "ghost denick stats name=%s", plainName.c_str());
            } else if (ghostNick) {
                SetPrimaryStatText(ghost, "\xC2\xA7""c[NICK]", 0xFF5555);
                ghost.statsRaw["tags"] = "NICK";
                ghost.statsColor["tags"] = 0xFF5555;
                ReplayLogOnce("ghostnick:" + nameLower, "ghost nick pending name=%s", plainName.c_str());
            } else {
                CachedHypixelStats h{};
                if (!uuid.empty() &&
                    PlayerApi::GetHypixel(uuid, h) &&
                    (h.ok || h.hasSkywars || h.hasDuels || h.hasMurderMystery)) {
                    ApplyOverlayStatsForGamemode(ghost, h, GetOverlayActiveGamemode());
                } else {
                    SetPrimaryStatText(ghost, "\xC2\xA7""7-", 0xAAAAAA);
                }
                CachedSeraphTag s{};
                if (!uuid.empty() && PlayerApi::GetSeraph(uuid, s)) {
                    ApplySeraphTagToPlayer(ghost, s);
                }
            }

            snap.players.push_back(std::move(ghost));
        }
    }

    std::string ndChat;
    bool ndChatJson = false;
    while (NumberDenickerTryConsumeChat(ndChat, ndChatJson)) {
        if (ndChatJson) {
            PrintJsonChat(env, mcInstance, cache_.clsMinecraft, ndChat);
        } else {
            PrintLocalChat(env, mcInstance, cache_.clsMinecraft, ndChat);
        }
        ndChat.clear();
    }

    if (g_redState != "Ingame") {
        std::vector<PlayerApi::PrequeueGhost> ghosts;
        PlayerApi::CollectGhosts(nowMs, snapshotNamesLower, ghosts);
        for (const auto& g : ghosts) {
                    PlayerBedwarsStats ghost;
            ghost.name = g.displayName;
            ghost.teamLetter = "";
                    ghost.hp = 20;
                    ghost.hpColor = HpToColor(ghost.hp);
            ghost.nameColor = g.nameColor;
            ghost.playerId = g.uuidHint;
                    ghost.statsRaw["name"] = ghost.name;
                    ghost.statsRaw["hp"] = std::to_string(ghost.hp);
                    ghost.statsRaw["stars"] = "";
                    ghost.statsRaw["stars_new"] = "";
                    ghost.statsRaw["fkdr"] = "";
                    ghost.statsRaw["kdr"] = "";
                    ghost.statsRaw["index"] = "";
                    ghost.statsRaw["swordKD"] = "";
                    ghost.statsRaw["requeuePct"] = "";
                    ghost.statsRaw["wlr"] = "";
                    ghost.statsRaw["wins"] = "";
                    ghost.statsRaw["finalKills"] = "";
                    ghost.statsRaw["bedsBroken"] = "";
                    ghost.statsRaw["bblr"] = "";
                    ghost.statsRaw["winstreak"] = "";
                    ghost.statsRaw["sw_stars"] = "";
                    ghost.statsRaw["sw_kdr"] = "";
                    ghost.statsRaw["sw_wlr"] = "";
                    ghost.statsRaw["sw_wins"] = "";
                    ghost.statsRaw["sw_kills"] = "";
                    ghost.statsRaw["duel_wins"] = "";
                    ghost.statsRaw["duel_wlr"] = "";
                    ghost.statsRaw["duel_kdr"] = "";
                    ghost.statsRaw["bwd_wlr"] = "";
                    ghost.statsRaw["bwd_index"] = "";
                    ghost.statsRaw["seens"] = "1";
                    ghost.statsRaw["session"] = "";
                    ghost.statsRaw["ping"] = "";
                    ghost.statsRaw["pingvar"] = "";
                    ghost.statsRaw["seraph"] = "";
                    ghost.statsRaw["tags"] = "-";
                    ghost.statsColor["seens"] = ColorForSeens(1.0);
                    ghost.statsColor["ping"] = 0x555555;
                    ghost.statsColor["pingvar"] = 0x555555;
                    ghost.statsColor["seraph"] = 0x555555;
                    ghost.statsColor["session"] = 0x555555;

            const bool isPqNick = !g.manual && PlayerApi::IsPrequeueNick(g.displayName);
            bool ghostDenickApplied = false;
            if (!g.manual && isPqNick) {
                NumberDenickerOverlayData numDenick{};
                if (NumberDenickerGetOverlayData(g.displayName, numDenick) &&
                    numDenick.resolved && numDenick.hypOk) {
                    ApplyOverlayStatsForGamemode(ghost, numDenick.hyp, GetOverlayActiveGamemode());
                    ghostDenickApplied = true;
                }
                DenickOverlayData denick{};
                if (!ghostDenickApplied && DenickIsEnabled() &&
                    DenickGetOverlayData(g.displayName, denick) &&
                    denick.state == DenickOverlayData::State::Denicked && denick.hypOk) {
                    ApplyOverlayStatsForGamemode(ghost, denick.hyp, GetOverlayActiveGamemode());
                    if (!denick.realUuid.empty()) ghost.playerId = denick.realUuid;
                    if (denick.seraphOk) {
                        ApplySeraphTagToPlayer(ghost, denick.seraph);
                        MaybeAnnounceBlacklist(env, mcInstance, cache_.clsMinecraft, ghost, denick.seraph);
                    }
                    ghostDenickApplied = true;
                }
            }
            // Apply markers AFTER wiping columns, otherwise stars_new/sw_stars/etc. clear [NICK].
            if (g.manual) {
                SetPrimaryStatText(ghost, "\xC2\xA7""6[MANUAL]", 0xFFCC00);
            } else if (isPqNick && !ghostDenickApplied) {
                SetPrimaryStatText(ghost, "\xC2\xA7""c[NICK]", 0xFF5555);
                ghost.statsRaw["tags"] = "NICK";
                ghost.statsColor["tags"] = 0xFF5555;
            } else if (!ghostDenickApplied) {
                SetPrimaryStatText(ghost, "\xC2\xA7""7-", 0xAAAAAA);
            }

            std::string uuid = g.uuidHint;
            if (uuid.empty()) uuid = PlayerApi::GetCachedUuidForName(g.displayName);
            if (ghost.playerId.empty()) ghost.playerId = uuid;
            // Nicks: keep [NICK] unless denick resolved above; otherwise fill Hypixel like normal.
            if (!uuid.empty() && !isPqNick && !g.manual) {
                        CachedHypixelStats h{};
                if (PlayerApi::GetHypixel(uuid, h) && (h.ok || h.hasSkywars || h.hasDuels || h.hasMurderMystery)) {
                    ApplyOverlayStatsForGamemode(ghost, h, GetOverlayActiveGamemode());
                    MaybeAnnounceChatOverlay(env, mcInstance, cache_.clsMinecraft, ghost, h, false);
                }
                        CachedSeraphTag s{};
                if (PlayerApi::GetSeraph(uuid, s)) {
                    ApplySeraphTagToPlayer(ghost, s);
                    MaybeAnnounceBlacklist(env, mcInstance, cache_.clsMinecraft, ghost, s);
                }
                CachedWinstreakPing ws{};
                if (PlayerApi::GetWinstreakPing(uuid, ws)) {
                    ApplyWinstreakPingToPlayer(ghost, ws);
                } else {
                    ghost.statsRaw["ping"] = "API";
                    ghost.statsColor["ping"] = 0x555555;
                    ghost.statsRaw["pingvar"] = "API";
                    ghost.statsColor["pingvar"] = 0x555555;
                    PlayerApi::OnTabPlayer(g.displayName, uuid);
                }
            }
                    snap.players.push_back(std::move(ghost));
        }
    }

    std::stable_sort(snap.players.begin(), snap.players.end(), [](const PlayerBedwarsStats& a, const PlayerBedwarsStats& b) {
        const int ar = TeamOrderRankForPlayer(a);
        const int br = TeamOrderRankForPlayer(b);
        if (ar != br) return ar < br;
        return LowerAscii(a.name) < LowerAscii(b.name);
    });

    env->DeleteLocalRef(iter);
    env->DeleteLocalRef(infoCollection);
    if (scorePlayerTeamClass) env->DeleteLocalRef(scorePlayerTeamClass);
    if (scoreboardClass) env->DeleteLocalRef(scoreboardClass);
    if (scoreboard) env->DeleteLocalRef(scoreboard);
    if (playerClass) env->DeleteLocalRef(playerClass);
    env->DeleteLocalRef(sendQueue);
    env->DeleteLocalRef(thePlayer);

    MaybeLogLocalTeamAndTeammates(snap.players);
    return snap;
}

void OverlayController::drawStringShadow(JNIEnv* env, jobject fontRenderer, const std::string& text, float x, float y, int color) const {
    if (!env || !fontRenderer) return;
    jstring jText = env->NewStringUTF(text.c_str());
    if (!jText) return;
    env->CallIntMethod(fontRenderer, cache_.midDrawStringShadow, jText, x, y, color);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(jText);
}

int OverlayController::measureString(JNIEnv* env, jobject fontRenderer, const std::string& text) const {
    if (!env || !fontRenderer) return 0;
    jstring jText = env->NewStringUTF(text.c_str());
    if (!jText) return 0;
    jint w = env->CallIntMethod(fontRenderer, cache_.midFontWidth, jText);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        w = 0;
    }
    env->DeleteLocalRef(jText);
    return (int)w;
}

LayoutResult OverlayController::computeLayout(const OverlaySnapshot& snapshot) const {
    LayoutResult out;

    int dynamicWidth = layoutConfig_.tableWidth;
    for (const auto& col : snapshot.columns) {
        dynamicWidth = (std::max)(dynamicWidth, col.xOffset + col.width + 12);
    }
    out.totalWidth = dynamicWidth;
    int footerLines = 0;
    if (!snapshot.session.footerText.empty()) {
        footerLines = 1;
        for (char c : snapshot.session.footerText) {
            if (c == '\n') footerLines++;
        }
    }
    const int dynamicFooterHeight = (footerLines > 0) ? ((footerLines * 10) + 4) : 6;

    out.totalHeight = layoutConfig_.headerHeight + dynamicFooterHeight +
        (int(snapshot.players.size()) * layoutConfig_.rowHeight) + layoutConfig_.extraMargins;

    if (snapshot.screenWidth > 0 && out.totalWidth > snapshot.screenWidth - 12) {
        out.totalWidth = (std::max)(80, snapshot.screenWidth - 12);
    }

    out.startX = (snapshot.screenWidth > 0) ? ((snapshot.screenWidth - out.totalWidth) / 2) : 10;
    if (out.startX < 6) out.startX = 6;
    out.startY = layoutConfig_.topMargin;

    out.headerY = out.startY;
    out.firstRowY = out.startY + layoutConfig_.firstRowOffsetY;
    out.footerY = out.startY + out.totalHeight - dynamicFooterHeight;

    // Keep the panel on screen even with large tab lists.
    int maxVisibleHeight = (snapshot.screenHeight > 0) ? (snapshot.screenHeight - out.startY - 6) : out.totalHeight;
    if (out.totalHeight > maxVisibleHeight) {
        out.totalHeight = (std::max)(60, maxVisibleHeight);
        out.footerY = out.startY + out.totalHeight - dynamicFooterHeight;
    }

    return out;
}

void OverlayController::renderBackgroundLayer(JNIEnv* env, const LayoutResult& layout) {
    if (!env || !cache_.clsGui || !cache_.midGuiDrawRect) return;

    const int rgb = overlayThemeColorRgb_.load(std::memory_order_relaxed) & 0xFFFFFF;
    const int opacityPercent = overlayOpacityPercent_.load(std::memory_order_relaxed);
    int alpha = (opacityPercent * 255) / 100;
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;
    const int color = (alpha << 24) | rgb;

    env->CallStaticVoidMethod(cache_.clsGui, cache_.midGuiDrawRect,
        (jint)layout.startX, (jint)layout.startY,
        (jint)(layout.startX + layout.totalWidth), (jint)(layout.startY + layout.totalHeight),
        (jint)color);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

static bool IsResourceTimerGameVisible() {
    return g_redState == "Ingame"
        && GetOverlayActiveGamemode() == OverlayGamemode::Bedwars;
}

void OverlayController::renderHeader(JNIEnv* env, jobject fontRenderer, const OverlaySnapshot& snapshot, const LayoutResult& layout) {
    if (!env || !fontRenderer) return;
    if (!resourceHeaderEnabled_.load(std::memory_order_relaxed)) return;
    if (resourceDisplayLocation_.load(std::memory_order_relaxed) != (int)ResourceDisplayLocation::Tab) return;
    if (!IsResourceTimerGameVisible()) return;

    ResourceDisplayOptions opts;
    opts.showDiamonds = resourceDiamondsEnabled_.load(std::memory_order_relaxed);
    opts.showEmeralds = resourceEmeraldsEnabled_.load(std::memory_order_relaxed);
    opts.compactLabels = false;
    const std::string line = BuildResourceDisplayLine(snapshot.session.resources, opts);
    if (line.empty()) return;

    const int tw = measureString(env, fontRenderer, line);
    const float x = (float)(layout.startX + (layout.totalWidth - tw) / 2);
    const float y = (float)(layout.headerY + 4);

    drawStringShadow(env, fontRenderer, line, x, y, 0xFFFFFF);
}

void OverlayController::renderColumnTitles(JNIEnv* env, jobject fontRenderer, const OverlaySnapshot& snapshot, const LayoutResult& layout) {
    const int y = layout.startY + layoutConfig_.columnTitleOffsetY;
    for (const auto& col : snapshot.columns) {
        int tw = measureString(env, fontRenderer, col.displayName);
        int x = columnManager_.computeTextX(layout.startX, col, tw);
        drawStringShadow(env, fontRenderer, col.displayName, (float)x, (float)y, 0xAAAAAA);
    }
}

void OverlayController::renderPlayerRows(JNIEnv* env, jobject fontRenderer, const OverlaySnapshot& snapshot, const LayoutResult& layout) {
    int currentY = layout.firstRowY;
    for (const auto& p : snapshot.players) {
        for (const auto& col : snapshot.columns) {
            if (col.id == "name") {
                const int baseX = layout.startX + col.xOffset + 11;
                const std::string letter = p.teamLetter.empty() ? std::string("?") : p.teamLetter.substr(0, 1);
                drawStringShadow(env, fontRenderer, letter, (float)baseX, (float)currentY, p.nameColor);
                drawStringShadow(env, fontRenderer, letter, (float)(baseX + 1), (float)currentY, p.nameColor);
                const int lw = measureString(env, fontRenderer, letter);
                const int nameX = baseX + lw + 2;
                auto tabFmtIt = p.statsRaw.find("tabFmt");
                if (tabFmtIt != p.statsRaw.end() && !tabFmtIt->second.empty()) {
                    drawStringShadow(env, fontRenderer, tabFmtIt->second, (float)nameX, (float)currentY, 0xFFFFFF);
                    if (p.flagged) {
                        const int nameWidth = measureString(env, fontRenderer, tabFmtIt->second);
                        const std::string warn = " \xE2\x9A\xA0";
                        drawStringShadow(env, fontRenderer, warn,
                                         (float)(nameX + nameWidth), (float)currentY, p.flagColor);
                    }
                } else {
                    drawStringShadow(env, fontRenderer, p.name, (float)nameX, (float)currentY, p.nameColor);
                    if (p.flagged) {
                        const int nameWidth = measureString(env, fontRenderer, p.name);
                        const std::string warn = " \xE2\x9A\xA0";
                        drawStringShadow(env, fontRenderer, warn,
                                         (float)(nameX + nameWidth), (float)currentY, p.flagColor);
                    }
                }
                continue;
            }

            auto it = p.statsRaw.find(col.id);
            const std::string value = (it != p.statsRaw.end()) ? it->second : "";
            const int tw = measureString(env, fontRenderer, value);
            const int x = columnManager_.computeTextX(layout.startX, col, tw);
            int color = (col.id == "hp") ? p.hpColor : 0xFFFFFF;
            auto cIt = p.statsColor.find(col.id);
            if (cIt != p.statsColor.end()) color = cIt->second;
            drawStringShadow(env, fontRenderer, value, (float)x, (float)currentY, color);
        }
        currentY += layoutConfig_.rowHeight;
    }
}

void OverlayController::renderSkinHeads(JNIEnv* env, jobject textureManager, const OverlaySnapshot& snapshot, const LayoutResult& layout) {
    if (!env || !textureManager) return;

    int headX = layout.startX + 7;
    for (const auto& col : snapshot.columns) {
        if (col.id == "name") {
            headX = layout.startX + col.xOffset + 2;
            break;
        }
    }

    if (cache_.clsGlStateManager && cache_.midGsmResetColor) {
        env->CallStaticVoidMethod(cache_.clsGlStateManager, cache_.midGsmResetColor);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    int currentY = layout.firstRowY;
    for (const auto& p : snapshot.players) {
        if (p.skinResourceGlobal) {
            env->CallVoidMethod(textureManager, cache_.midBindTexture, p.skinResourceGlobal);
            if (env->ExceptionCheck()) env->ExceptionClear();
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
            // Base face layer (8,8) then hat/overlay layer (40,8) — same as vanilla tab list.
            env->CallStaticVoidMethod(cache_.clsGui, cache_.midGuiDrawScaledCustomRect,
                headX, currentY,
                8.0f, 8.0f,
                8, 8,
                64.0f, 64.0f);
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->CallStaticVoidMethod(cache_.clsGui, cache_.midGuiDrawScaledCustomRect,
                headX, currentY,
                40.0f, 8.0f,
                8, 8,
                64.0f, 64.0f);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        currentY += layoutConfig_.rowHeight;
    }
}

void OverlayController::renderFooter(JNIEnv* env, jobject fontRenderer, const OverlaySnapshot& snapshot, const LayoutResult& layout) {
    if (snapshot.session.footerText.empty()) return;
    std::vector<std::string> lines;
    std::string cur;
    for (char c : snapshot.session.footerText) {
        if (c == '\r') continue;
        if (c == '\n') {
            lines.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty() || lines.empty()) lines.push_back(cur);

    float y = (float)(layout.footerY + 2);
    for (const auto& line : lines) {
        int w = measureString(env, fontRenderer, line);
        float x = (float)(layout.startX + (layout.totalWidth - w) / 2);
        drawStringShadow(env, fontRenderer, line, x, y, 0xFFFFFF);
        y += 10.0f;
    }
}

void OverlayController::releaseSnapshotResources(JNIEnv* env, OverlaySnapshot& snapshot) const {
    if (!env) return;
    for (auto& p : snapshot.players) {
        if (p.skinResourceGlobal) {
            env->DeleteGlobalRef(p.skinResourceGlobal);
            p.skinResourceGlobal = nullptr;
        }
    }
}

void OverlayController::restoreVanillaGuiGlState(JNIEnv* env, jobject fontRenderer, jobject textureManager) const {
    if (!env || !cache_.initialized) return;

    if (cache_.clsGlStateManager) {
        if (cache_.midGsmResetColor) {
            env->CallStaticVoidMethod(cache_.clsGlStateManager, cache_.midGsmResetColor);
        }
        if (cache_.midGsmEnableTexture2D) {
            env->CallStaticVoidMethod(cache_.clsGlStateManager, cache_.midGsmEnableTexture2D);
        }
        if (cache_.midGsmEnableBlend) {
            env->CallStaticVoidMethod(cache_.clsGlStateManager, cache_.midGsmEnableBlend);
        }
        if (cache_.midGsmBlendFunc) {
            env->CallStaticVoidMethod(cache_.clsGlStateManager, cache_.midGsmBlendFunc, 770, 771);
        }
        if (cache_.midGsmEnableDepth) {
            env->CallStaticVoidMethod(cache_.clsGlStateManager, cache_.midGsmEnableDepth);
        }
        if (cache_.midGsmDepthMask) {
            env->CallStaticVoidMethod(cache_.clsGlStateManager, cache_.midGsmDepthMask, JNI_TRUE);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    if (fontRenderer && textureManager && cache_.fidLocationFontTexture && cache_.midBindTexture) {
        jobject locFont = env->GetObjectField(fontRenderer, cache_.fidLocationFontTexture);
        if (locFont) {
            env->CallVoidMethod(textureManager, cache_.midBindTexture, locFont);
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(locFont);
        }
    }

    if (fontRenderer && cache_.midDrawStringShadow) {
        jstring jSpace = env->NewStringUTF(" ");
        if (jSpace) {
            env->CallIntMethod(fontRenderer, cache_.midDrawStringShadow, jSpace, -9999.0f, -9999.0f, (jint)0xFFFFFFFF);
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(jSpace);
        }
    }
}

void RestoreVanillaGuiGlState(JNIEnv* env) {
    if (!env || !g_glRestoreController || !g_glRestoreController->isInitialized()) return;
    g_glRestoreController->syncVanillaGuiGlState(env);
}

void OverlayController::syncVanillaGuiGlState(JNIEnv* env) const {
    if (!env || !cache_.initialized || !cache_.clsMinecraft || !cache_.midGetMinecraft) return;

    jobject mcObj = env->CallStaticObjectMethod(cache_.clsMinecraft, cache_.midGetMinecraft);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (!mcObj) return;

    jobject fontRenderer = env->GetObjectField(mcObj, cache_.fidFontRenderer);
    jobject textureManager = env->GetObjectField(mcObj, cache_.fidTextureManager);
    if (fontRenderer && textureManager) {
        restoreVanillaGuiGlState(env, fontRenderer, textureManager);
    }
    if (fontRenderer) env->DeleteLocalRef(fontRenderer);
    if (textureManager) env->DeleteLocalRef(textureManager);
    env->DeleteLocalRef(mcObj);
}

void OverlayController::renderSnapshot(JNIEnv* env, const OverlaySnapshot& snap) {
    if (!env || !cache_.initialized) return;

    jobject mcObj = env->CallStaticObjectMethod(cache_.clsMinecraft, cache_.midGetMinecraft);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (!mcObj) return;

    jobject fontRenderer = env->GetObjectField(mcObj, cache_.fidFontRenderer);
    jobject textureManager = env->GetObjectField(mcObj, cache_.fidTextureManager);
    if (!fontRenderer || !textureManager) {
        if (fontRenderer) env->DeleteLocalRef(fontRenderer);
        if (textureManager) env->DeleteLocalRef(textureManager);
        env->DeleteLocalRef(mcObj);
        return;
    }

    const LayoutResult layout = computeLayout(snap);
    renderBackgroundLayer(env, layout);
    renderHeader(env, fontRenderer, snap, layout);
    renderColumnTitles(env, fontRenderer, snap, layout);
    restoreVanillaGuiGlState(env, fontRenderer, textureManager);
    renderPlayerRows(env, fontRenderer, snap, layout);
    renderFooter(env, fontRenderer, snap, layout);
    renderSkinHeads(env, textureManager, snap, layout);
    restoreVanillaGuiGlState(env, fontRenderer, textureManager);

    env->DeleteLocalRef(fontRenderer);
    env->DeleteLocalRef(textureManager);
    env->DeleteLocalRef(mcObj);
}

void OverlayController::renderFrame(JNIEnv* env) {
    if (!env || !cache_.initialized) return;

    auto snapPtr = snapshots_.readLatest();
    if (!snapPtr) return;
    renderSnapshot(env, *snapPtr);
}

void OverlayController::tickSession(JNIEnv* env, jobject mcInstance) {
    if (!env || !mcInstance || !cache_.initialized) return;

    if (cachedSnapshotValid_) {
        releaseSnapshotResources(env, cachedSnapshot_);
        cachedSnapshotValid_ = false;
    }

    cachedSnapshot_ = buildSnapshotFromMinecraft(env, mcInstance);
    cachedSnapshotValid_ = true;
    publishSnapshot(cachedSnapshot_);
}

void OverlayController::renderCustomTab(JNIEnv* env, jobject mcInstance) {
    if (!env || !mcInstance) return;

    // Safety: this overlay uses OpenGL immediate mode and must run only on a
    // thread that currently owns a valid GL context.
    if (wglGetCurrentContext() == nullptr) {
        return;
    }

    if (!cachedSnapshotValid_) {
        tickSession(env, mcInstance);
    }
    if (!cachedSnapshotValid_) return;

    renderSnapshot(env, cachedSnapshot_);
}

void OverlayController::clearScoreboardResourceLines(JNIEnv* env, jobject mcInstance) {
    if (!env || !mcInstance || !cache_.initialized) return;

    jobject scoreboard = nullptr;
    jobject objective = nullptr;
    if (!AcquireSidebarObjective(env, mcInstance, cache_, scoreboard, objective)) {
        scoreboardResourceLinesActive_ = false;
        return;
    }

    RemoveAllFrifyResourceScoreLines(env, scoreboard, objective);
    if (!lastResourceScoreLine_.empty()) {
        RemoveScoreboardResourceLine(env, scoreboard, objective, lastResourceScoreLine_.c_str());
    }
    lastResourceScoreLine_.clear();
    scoreboardResourceLinesActive_ = false;

    env->DeleteLocalRef(objective);
    env->DeleteLocalRef(scoreboard);
}

void OverlayController::renderScoreboardResources(JNIEnv* env, jobject mcInstance) {
    if (!env || !mcInstance || !cache_.initialized) return;

    const bool enabled = resourceHeaderEnabled_.load(std::memory_order_relaxed);
    const bool scoreboardMode =
        resourceDisplayLocation_.load(std::memory_order_relaxed) == (int)ResourceDisplayLocation::Scoreboard;

    if (!enabled || !scoreboardMode || !IsResourceTimerGameVisible()) {
        if (scoreboardResourceLinesActive_) {
            clearScoreboardResourceLines(env, mcInstance);
        }
        return;
    }

    if (!cachedSnapshotValid_) {
        tickSession(env, mcInstance);
    }
    if (!cachedSnapshotValid_) return;

    const GameResourceStats& res = cachedSnapshot_.session.resources;

    ResourceDisplayOptions opts;
    opts.showDiamonds = resourceDiamondsEnabled_.load(std::memory_order_relaxed);
    opts.showEmeralds = resourceEmeraldsEnabled_.load(std::memory_order_relaxed);
    const std::string resourceLine = BuildResourceDisplayLine(res, opts);
    if (resourceLine.empty()) {
        if (scoreboardResourceLinesActive_) {
            clearScoreboardResourceLines(env, mcInstance);
        }
        return;
    }

    jobject scoreboard = nullptr;
    jobject objective = nullptr;
    if (!AcquireSidebarObjective(env, mcInstance, cache_, scoreboard, objective)) {
        if (scoreboardResourceLinesActive_) {
            clearScoreboardResourceLines(env, mcInstance);
        }
        return;
    }

    RemoveAllFrifyResourceScoreLines(env, scoreboard, objective);

    const int minPts = GetMinSidebarScorePoints(env, scoreboard, objective, cache_);

    if (lastResourceScoreLine_ != resourceLine) {
        if (!lastResourceScoreLine_.empty()) {
            RemoveScoreboardResourceLine(env, scoreboard, objective, lastResourceScoreLine_.c_str());
        }
        lastResourceScoreLine_ = resourceLine;
    }
    SyncScoreboardResourceLineDirect(env, scoreboard, objective, resourceLine, minPts - 1);
    scoreboardResourceLinesActive_ = true;

    env->DeleteLocalRef(objective);
    env->DeleteLocalRef(scoreboard);
}

void OverlayController::renderScoreboardBedDisconnectTimers(JNIEnv* env, jobject mcInstance) {
    if (!env || !mcInstance || !cache_.initialized) return;
    SyncScoreboardBedDisconnectTimers(env, mcInstance, cache_);
}

} // namespace taboverlay
