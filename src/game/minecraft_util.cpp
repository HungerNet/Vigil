// Vigil - Minecraft text, team and server helpers (JNI).
// Extracted from the legacy utils module; overlay, HTTP and Bed Wars helpers were removed.

#include "minecraft_util.h"

#include <windows.h>
#include <iomanip>
#include <sstream>
#include <string>

#include "class_finder.h"
#include "mc.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <vector>

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r')) b++;
    while (e > b && (s[e-1] == ' ' || s[e-1] == '\t' || s[e-1] == '\n' || s[e-1] == '\r')) e--;
    return s.substr(b, e - b);
}

static int JsonHexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void AppendUtf8Codepoint(std::string& out, unsigned int cp) {
    if (cp <= 0x7F) {
        out += (char)cp;
    } else if (cp <= 0x7FF) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

std::string DecodeJsonStringEscapes(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '\\' || i + 1 >= in.size()) {
            out += in[i];
            continue;
        }
        const char n = in[i + 1];
        if (n == 'u' && i + 5 < in.size()) {
            const int h0 = JsonHexVal(in[i + 2]);
            const int h1 = JsonHexVal(in[i + 3]);
            const int h2 = JsonHexVal(in[i + 4]);
            const int h3 = JsonHexVal(in[i + 5]);
            if (h0 >= 0 && h1 >= 0 && h2 >= 0 && h3 >= 0) {
                const unsigned int cp = (unsigned)(h0 << 12 | h1 << 8 | h2 << 4 | h3);
                AppendUtf8Codepoint(out, cp);
                i += 5;
                continue;
            }
        }
        if (n == '\\') { out += '\\'; i++; continue; }
        if (n == '"')  { out += '"';  i++; continue; }
        if (n == 'n')  { out += '\n'; i++; continue; }
        if (n == 'r')  { out += '\r'; i++; continue; }
        if (n == 't')  { out += '\t'; i++; continue; }
        if (n == '/')  { out += '/';  i++; continue; }
        out += in[i];
    }
    return out;
}

bool IsValidIgn(const std::string& s) {
    if (s.size() < 3 || s.size() > 16) return false;
    for (char c : s) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

std::string LowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return s;
}

std::string FormatRatio(double v) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(v < 10.0 ? 2 : 1) << v;
    return ss.str();
}

std::string PadRight(const std::string& s, size_t width) {
    if (s.size() >= width) return s;
    return s + std::string(width - s.size(), ' ');
}

size_t VisibleTextWidth(const std::string& s) {
    size_t w = 0;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char b = (unsigned char)s[i];
        if (b == 0xC2 && i + 2 < s.size() && (unsigned char)s[i+1] == 0xA7) {
            i += 3;
            continue;
        }
        if (b == 0xA7 && i + 1 < s.size()) {
            i += 2;
            continue;
        }
        if ((b & 0xC0) != 0x80) w++;
        i++;
    }
    return w;
}

std::string PadRightVisible(const std::string& s, size_t width) {
    size_t w = VisibleTextWidth(s);
    if (w >= width) return s;
    return s + std::string(width - w, ' ');
}

std::string TruncateVisible(const std::string& s, size_t width) {
    if (width == 0) return "";
    size_t visible = 0;
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if ((unsigned char)s[i] == 0xC2 && i + 2 < s.size() && (unsigned char)s[i+1] == 0xA7) {
            out += s[i]; out += s[i+1]; out += s[i+2];
            i += 2;
            continue;
        }
        if (visible >= width) break;
        out += s[i];
        ++visible;
    }
    return out;
}

std::string StripMinecraftFormatting(const std::string& text) {
    std::string out;
    for (std::string::size_type i = 0; i < text.size(); ) {
        unsigned char b = (unsigned char)text[i];
        if (b == 0xC2 && i + 1 < text.size() && (unsigned char)text[i+1] == 0xA7) {
            i += (i + 2 < text.size()) ? 3 : 2;
        } else if (b == 0xA7) {
            i += (i + 1 < text.size()) ? 2 : 1;
        } else {
            out += text[i++];
        }
    }
    return out;
}

char TeamLetterFromColorCode(char c) {
    switch (c) {
        case 'c': case '4': return 'R';
        case '9': case '1': return 'B';
        case 'a': case '2': return 'G';
        case 'e': case '6': return 'Y';
        case 'b': case '3': return 'A';
        case 'd': case '5': return 'P';
        case 'f': return 'W'; // white
        case '8': return 'S'; // dark gray (section sign 8)
        case '7': return 'S'; // light gray (section sign 7) - same letter, sorted below section sign 8
        default: return '-';
    }
}

bool IsMinecraftColorCode(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

bool IsMinecraftFormatCode(char c) {
    return c == 'k' || c == 'l' || c == 'm' || c == 'n' || c == 'o' || c == 'r';
}

static char TeamColorNameToMcChar(const std::string& colorName) {
    const std::string c = LowerAscii(colorName);
    if (c == "black") return '0';
    if (c == "dark_blue") return '1';
    if (c == "dark_green") return '2';
    if (c == "dark_aqua") return '3';
    if (c == "dark_red") return '4';
    if (c == "dark_purple") return '5';
    if (c == "gold") return '6';
    if (c == "gray") return '7';
    if (c == "dark_gray") return '8';
    if (c == "blue") return '9';
    if (c == "green") return 'a';
    if (c == "aqua") return 'b';
    if (c == "red") return 'c';
    if (c == "light_purple") return 'd';
    if (c == "yellow") return 'e';
    if (c == "white") return 'f';
    return 0;
}

char TeamColorNameToMcCode(const std::string& colorName) {
    return TeamColorNameToMcChar(colorName);
}

char McColorCodeFromRgb(int rgb) {
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

static int ColorNameFromSectionCode(char code, std::string& outName) {
    switch (code) {
        case '0': outName = "black"; return 1;
        case '1': outName = "dark_blue"; return 1;
        case '2': outName = "dark_green"; return 1;
        case '3': outName = "dark_aqua"; return 1;
        case '4': outName = "dark_red"; return 1;
        case '5': outName = "dark_purple"; return 1;
        case '6': outName = "gold"; return 1;
        case '7': outName = "gray"; return 1;
        case '8': outName = "dark_gray"; return 1;
        case '9': outName = "blue"; return 1;
        case 'a': outName = "green"; return 1;
        case 'b': outName = "aqua"; return 1;
        case 'c': outName = "red"; return 1;
        case 'd': outName = "light_purple"; return 1;
        case 'e': outName = "yellow"; return 1;
        case 'f': outName = "white"; return 1;
        default: return 0;
    }
}

static std::string ParseLastSectionColorName(const std::string& text) {
    std::string last;
    for (size_t i = 0; i < text.size(); ) {
        if ((unsigned char)text[i] == 0xC2 && i + 1 < text.size() && (unsigned char)text[i + 1] == 0xA7 &&
            i + 2 < text.size()) {
            std::string color;
            if (ColorNameFromSectionCode(text[i + 2], color)) {
                last = color;
            }
            i += 3;
        } else {
            ++i;
        }
    }
    return last;
}

static std::string ParseFirstMeaningfulColorName(const std::string& text) {
    std::string firstAny;
    for (size_t i = 0; i < text.size(); ) {
        if ((unsigned char)text[i] == 0xC2 && i + 1 < text.size() && (unsigned char)text[i + 1] == 0xA7 &&
            i + 2 < text.size()) {
            std::string color;
            if (ColorNameFromSectionCode(text[i + 2], color)) {
                if (firstAny.empty()) firstAny = color;
                if (color != "white" && color != "gray") return color;
            }
            i += 3;
        } else {
            ++i;
        }
    }
    return firstAny;
}

static bool IsDefaultTeamFormattingRgb(int rgb) {
    return rgb == 0xFFFFFF || rgb == 0xAAAAAA || rgb == 0x000000;
}

static bool IsFormattingOnlySectionCode(char code) {
    return code == 'k' || code == 'l' || code == 'm' || code == 'n' || code == 'o' || code == 'r';
}

static bool PrefixDeclaresWhiteTeam(const std::string& text) {
    for (size_t i = 0; i < text.size(); ) {
        if ((unsigned char)text[i] == 0xC2 && i + 1 < text.size() && (unsigned char)text[i + 1] == 0xA7 &&
            i + 2 < text.size()) {
            const char code = text[i + 2];
            if (code == 'f') return true;
            if (IsFormattingOnlySectionCode(code)) {
                i += 3;
                continue;
            }
            return false;
        }
        ++i;
    }
    return false;
}

static bool IsExplicitWhiteTeam(JNIEnv* env, jobject team, const std::string& prefix,
                                const std::string& suffix) {
    if (PrefixDeclaresWhiteTeam(prefix) || PrefixDeclaresWhiteTeam(suffix)) return true;
    if (!team) return false;

    jclass teamClass = env->GetObjectClass(team);
    jmethodID midGetColor = teamClass ? env->GetMethodID(teamClass, "getChatFormat", "()Lnet/minecraft/util/EnumChatFormatting;") : nullptr;
    if (teamClass) env->DeleteLocalRef(teamClass);
    if (!midGetColor || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jobject formatting = env->CallObjectMethod(team, midGetColor);
    if (!formatting || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jclass fmtClass = McFindClass(env, "net/minecraft/util/EnumChatFormatting");
    jmethodID midIdx = fmtClass ? env->GetMethodID(fmtClass, "getColorIndex", "()I") : nullptr;
    int colorIndex = -1;
    if (midIdx && !env->ExceptionCheck()) {
        colorIndex = env->CallIntMethod(formatting, midIdx);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            colorIndex = -1;
        }
    } else {
        env->ExceptionClear();
    }
    if (fmtClass) env->DeleteLocalRef(fmtClass);
    env->DeleteLocalRef(formatting);
    return colorIndex == 15;
}

// Forward declarations (helpers are defined later in file).
static int TeamFormattingColorToRgb(JNIEnv* env, jobject team);
static std::string GetTeamPrefixOrSuffix(JNIEnv* env, jobject team, bool suffix);

static jobject GetNetworkPlayerInfoForEntity(JNIEnv* env, jobject mcObj, jclass mcClass,
                                             jobject entity, jclass entityClass) {
    if (!env || !mcObj || !mcClass || !entity || !entityClass) return nullptr;

    jmethodID midProfile = env->GetMethodID(entityClass, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;");
    if (!midProfile || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    jobject profile = env->CallObjectMethod(entity, midProfile);
    if (!profile || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }

    jclass profileClass = env->GetObjectClass(profile);
    jmethodID midGetId = profileClass ? env->GetMethodID(profileClass, "getId", "()Ljava/util/UUID;") : nullptr;
    if (!midGetId || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (profileClass) env->DeleteLocalRef(profileClass);
        env->DeleteLocalRef(profile);
        return nullptr;
    }
    jobject uuidObj = env->CallObjectMethod(profile, midGetId);
    if (profileClass) env->DeleteLocalRef(profileClass);
    env->DeleteLocalRef(profile);
    if (!uuidObj || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }

    jmethodID midNetHandler = env->GetMethodID(mcClass, "getNetHandler", "()Lnet/minecraft/client/network/NetHandlerPlayClient;");
    if (!midNetHandler || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(uuidObj);
        return nullptr;
    }
    jobject netHandler = env->CallObjectMethod(mcObj, midNetHandler);
    if (!netHandler || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(uuidObj);
        return nullptr;
    }

    jclass nhClass = env->GetObjectClass(netHandler);
    jmethodID midGetInfoByUUID = nhClass ? env->GetMethodID(nhClass, "getPlayerInfo", "(Ljava/util/UUID;)Lnet/minecraft/client/network/NetworkPlayerInfo;") : nullptr;
    if (!midGetInfoByUUID || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (nhClass) env->DeleteLocalRef(nhClass);
        env->DeleteLocalRef(netHandler);
        env->DeleteLocalRef(uuidObj);
        return nullptr;
    }
    jobject info = env->CallObjectMethod(netHandler, midGetInfoByUUID, uuidObj);
    if (nhClass) env->DeleteLocalRef(nhClass);
    env->DeleteLocalRef(netHandler);
    env->DeleteLocalRef(uuidObj);
    if (!info || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return info;
}

static jobject GetTeamFromNetworkInfo(JNIEnv* env, jobject info) {
    if (!env || !info) return nullptr;
    jclass infoClass = env->GetObjectClass(info);
    jmethodID midGetTeam = infoClass ? env->GetMethodID(infoClass, "getPlayerTeam", "()Lnet/minecraft/scoreboard/ScorePlayerTeam;") : nullptr;
    if (!midGetTeam || env->ExceptionCheck()) {
        env->ExceptionClear();
        midGetTeam = infoClass ? env->GetMethodID(infoClass, "i", "()Lnet/minecraft/scoreboard/Team;") : nullptr;
    }
    jobject team = nullptr;
    if (midGetTeam && !env->ExceptionCheck()) {
        team = env->CallObjectMethod(info, midGetTeam);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            team = nullptr;
        }
    } else {
        env->ExceptionClear();
    }
    if (infoClass) env->DeleteLocalRef(infoClass);
    return team;
}

static jobject GetScoreboardTeamForEntity(JNIEnv* env, jobject mcObj, jclass mcClass,
                                          jobject entity, jclass entityClass) {
    jobject info = GetNetworkPlayerInfoForEntity(env, mcObj, mcClass, entity, entityClass);
    if (!info) return nullptr;
    jobject team = GetTeamFromNetworkInfo(env, info);
    env->DeleteLocalRef(info);
    return team;
}

static jobject GetEntityTeamFromEntity(JNIEnv* env, jobject entity, jclass entityClass) {
    if (!env || !entity) return nullptr;

    jmethodID midGetTeam = entityClass
        ? env->GetMethodID(entityClass, "getTeam", "()Lnet/minecraft/scoreboard/Team;")
        : nullptr;
    if (!midGetTeam || env->ExceptionCheck()) {
        env->ExceptionClear();
        jclass livingClass = McFindClass(env, "net/minecraft/entity/EntityLivingBase");
        if (livingClass) {
            midGetTeam = env->GetMethodID(livingClass, "getTeam", "()Lnet/minecraft/scoreboard/Team;");
            env->DeleteLocalRef(livingClass);
        }
        if (!midGetTeam || env->ExceptionCheck()) {
            env->ExceptionClear();
            return nullptr;
        }
    }

    jobject team = env->CallObjectMethod(entity, midGetTeam);
    if (!team || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return team;
}

static jobject GetEntityTeamFromClientPlayerInfo(JNIEnv* env, jobject entity, jclass entityClass) {
    if (!env || !entity || !entityClass) return nullptr;

    jclass clientPlayerClass = McFindClass(env, "net/minecraft/client/entity/AbstractClientPlayer");
    if (!clientPlayerClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    const bool isClientPlayer = env->IsInstanceOf(entity, clientPlayerClass);
    env->DeleteLocalRef(clientPlayerClass);
    if (!isClientPlayer) return nullptr;

    jmethodID midGetInfo = env->GetMethodID(entityClass, "getPlayerInfo", "()Lnet/minecraft/client/network/NetworkPlayerInfo;");
    if (!midGetInfo || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }

    jobject info = env->CallObjectMethod(entity, midGetInfo);
    if (!info || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }

    jobject team = GetTeamFromNetworkInfo(env, info);
    env->DeleteLocalRef(info);
    return team;
}

static jobject GetEntityTeamFromScoreboardName(JNIEnv* env, jobject world, const char* playerName) {
    if (!env || !world || !playerName || !playerName[0]) return nullptr;

    jclass worldClass = env->GetObjectClass(world);
    jmethodID midGetScoreboard = worldClass ? env->GetMethodID(worldClass, "getScoreboard", "()Lnet/minecraft/scoreboard/Scoreboard;") : nullptr;
    if (worldClass) env->DeleteLocalRef(worldClass);
    if (!midGetScoreboard || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }

    jobject scoreboard = env->CallObjectMethod(world, midGetScoreboard);
    if (!scoreboard || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }

    jclass sbClass = env->GetObjectClass(scoreboard);
    jmethodID midGetPlayersTeam = sbClass ? env->GetMethodID(sbClass, "getPlayersTeam", "(Ljava/lang/String;)Lnet/minecraft/scoreboard/ScorePlayerTeam;") : nullptr;
    if (sbClass) env->DeleteLocalRef(sbClass);
    if (!midGetPlayersTeam || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(scoreboard);
        return nullptr;
    }

    jstring jname = env->NewStringUTF(playerName);
    jobject team = env->CallObjectMethod(scoreboard, midGetPlayersTeam, jname);
    env->DeleteLocalRef(jname);
    env->DeleteLocalRef(scoreboard);
    if (!team || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return team;
}

static std::string GetEntityPlainName(JNIEnv* env, jobject entity, jclass entityClass) {
    if (!env || !entity) return "";
    jmethodID midName = entityClass ? env->GetMethodID(entityClass, "getName", "()Ljava/lang/String;") : nullptr;
    if (!midName || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }
    jstring js = (jstring)env->CallObjectMethod(entity, midName);
    if (!js || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }
    const char* p = env->GetStringUTFChars(js, nullptr);
    std::string out = p ? p : "";
    if (p) env->ReleaseStringUTFChars(js, p);
    env->DeleteLocalRef(js);
    return out;
}

static int RgbFromTeamObject(JNIEnv* env, jobject team, std::string* outPrefix = nullptr) {
    if (!env || !team) return 0;

    const std::string prefix = GetTeamPrefixOrSuffix(env, team, false);
    const std::string suffix = GetTeamPrefixOrSuffix(env, team, true);
    if (outPrefix) *outPrefix = prefix;

    std::string colorName = ParseFirstMeaningfulColorName(prefix);
    if (colorName.empty()) colorName = ParseFirstMeaningfulColorName(suffix);
    if (colorName.empty()) colorName = ParseLastSectionColorName(prefix);
    if (colorName.empty()) colorName = ParseLastSectionColorName(suffix);
    if (!colorName.empty()) {
        return TeamColorNameToRgb(colorName);
    }

    const int fmtRgb = TeamFormattingColorToRgb(env, team);
    if (fmtRgb > 0 && !IsDefaultTeamFormattingRgb(fmtRgb)) {
        return fmtRgb;
    }
    return 0;
}

int GetScoreboardTeamRgb(JNIEnv* env, jobject team) {
    return RgbFromTeamObject(env, team, nullptr);
}

static int RgbFromEntityDisplayName(JNIEnv* env, jobject entity, jclass entityClass) {
    if (!env || !entity) return 0;
    jmethodID midDisplay = entityClass ? env->GetMethodID(entityClass, "getDisplayName", "()Lnet/minecraft/util/IChatComponent;") : nullptr;
    if (!midDisplay || env->ExceptionCheck()) {
        env->ExceptionClear();
        return 0;
    }
    jobject comp = env->CallObjectMethod(entity, midDisplay);
    if (!comp || env->ExceptionCheck()) {
        env->ExceptionClear();
        return 0;
    }

    jclass compClass = env->GetObjectClass(comp);
    jmethodID midFmt = compClass ? env->GetMethodID(compClass, "getFormattedText", "()Ljava/lang/String;") : nullptr;
    if (compClass) env->DeleteLocalRef(compClass);
    if (!midFmt || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(comp);
        return 0;
    }

    jstring js = (jstring)env->CallObjectMethod(comp, midFmt);
    env->DeleteLocalRef(comp);
    if (!js || env->ExceptionCheck()) {
        env->ExceptionClear();
        return 0;
    }
    const char* p = env->GetStringUTFChars(js, nullptr);
    const std::string formatted = p ? p : "";
    if (p) env->ReleaseStringUTFChars(js, p);
    env->DeleteLocalRef(js);

    std::string colorName = ParseFirstMeaningfulColorName(formatted);
    if (colorName.empty()) colorName = ParseLastSectionColorName(formatted);
    if (colorName.empty()) return 0;
    return TeamColorNameToRgb(colorName);
}

struct TeamRgbResolveResult {
    int rgb = 0xFFFFFF;
    const char* source = "fallback";
    std::string prefix;
    std::string playerName;
};

static TeamRgbResolveResult ResolveEntityTeamRgbDetailed(JNIEnv* env, jobject mcObj, jclass mcClass,
                                                         jobject entity, jclass entityClass) {
    TeamRgbResolveResult result;
    result.playerName = GetEntityPlainName(env, entity, entityClass);

    auto tryTeam = [&](jobject team, const char* source) -> bool {
        if (!team) return false;
        std::string prefix;
        const std::string suffix = GetTeamPrefixOrSuffix(env, team, true);
        const int rgb = RgbFromTeamObject(env, team, &prefix);
        if (rgb > 0 && !IsDefaultTeamFormattingRgb(rgb)) {
            result.rgb = rgb;
            result.source = source;
            result.prefix = prefix;
            env->DeleteLocalRef(team);
            return true;
        }
        if (rgb == 0xFFFFFF && IsExplicitWhiteTeam(env, team, prefix, suffix)) {
            result.rgb = 0xFFFFFF;
            result.source = source;
            result.prefix = prefix;
            env->DeleteLocalRef(team);
            return true;
        }
        env->DeleteLocalRef(team);
        return false;
    };

    jobject teamEntity = GetEntityTeamFromEntity(env, entity, entityClass);
    if (tryTeam(teamEntity, "entity.bO")) return result;

    jfieldID fidWorld = env->GetFieldID(mcClass, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");
    if (fidWorld && !env->ExceptionCheck()) {
        jobject world = env->GetObjectField(mcObj, fidWorld);
        if (world && !env->ExceptionCheck() && !result.playerName.empty()) {
            jobject teamSb = GetEntityTeamFromScoreboardName(env, world, result.playerName.c_str());
            if (tryTeam(teamSb, "scoreboard.name")) {
                env->DeleteLocalRef(world);
                return result;
            }
        } else {
            env->ExceptionClear();
        }
        if (world) env->DeleteLocalRef(world);
    } else {
        env->ExceptionClear();
    }

    jobject teamClient = GetEntityTeamFromClientPlayerInfo(env, entity, entityClass);
    if (tryTeam(teamClient, "clientPlayer.info")) return result;

    jobject teamTab = GetScoreboardTeamForEntity(env, mcObj, mcClass, entity, entityClass);
    if (tryTeam(teamTab, "tab.uuid")) return result;

    const int displayRgb = RgbFromEntityDisplayName(env, entity, entityClass);
    if (displayRgb > 0 && !IsDefaultTeamFormattingRgb(displayRgb)) {
        result.rgb = displayRgb;
        result.source = "displayName";
        return result;
    }

    result.rgb = 0;
    result.source = "fallback";
    return result;
}

bool ShouldDrawHitboxTeamColor(const HitboxTeamColorInfo& info) {
    if (!info.source || std::strcmp(info.source, "fallback") == 0) return false;
    if (info.rgb <= 0) return false;
    if ((info.rgb & 0xFFFFFF) != 0xFFFFFF) return true;
    return PrefixDeclaresWhiteTeam(info.prefix);
}

static int TeamFormattingColorToRgb(JNIEnv* env, jobject team) {
    if (!env || !team) return 0;
    jclass teamClass = env->GetObjectClass(team);
    if (!teamClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return 0;
    }

    jmethodID midGetColor = env->GetMethodID(teamClass, "getChatFormat", "()Lnet/minecraft/util/EnumChatFormatting;");
    if (!midGetColor || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(teamClass);
        return 0;
    }

    jobject formatting = env->CallObjectMethod(team, midGetColor);
    env->DeleteLocalRef(teamClass);
    if (!formatting || env->ExceptionCheck()) {
        env->ExceptionClear();
        return 0;
    }

    jclass fmtClass = McFindClass(env, "net/minecraft/util/EnumChatFormatting");
    jmethodID midRgb = fmtClass ? env->GetMethodID(fmtClass, "getColorIndex", "()I") : nullptr;
    int colorIndex = -1;
    if (midRgb && !env->ExceptionCheck()) {
        colorIndex = env->CallIntMethod(formatting, midRgb);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            colorIndex = -1;
        }
    } else {
        env->ExceptionClear();
    }
    if (fmtClass) env->DeleteLocalRef(fmtClass);
    env->DeleteLocalRef(formatting);

    // EnumChatFormatting.getColorIndex() returns 0-15 (not RGB).
    // Map to classic chat RGB palette (same ones we already use elsewhere).
    if (colorIndex < 0 || colorIndex > 15) return 0;
    static const int idxToRgb[16] = {
        0x000000, // 0 black
        0x0000AA, // 1 dark_blue
        0x00AA00, // 2 dark_green
        0x00AAAA, // 3 dark_aqua
        0xAA0000, // 4 dark_red
        0xAA00AA, // 5 dark_purple
        0xFFAA00, // 6 gold
        0xAAAAAA, // 7 gray
        0x555555, // 8 dark_gray
        0x5555FF, // 9 blue
        0x55FF55, // 10 green
        0x55FFFF, // 11 aqua
        0xFF5555, // 12 red
        0xFF55FF, // 13 light_purple
        0xFFFF55, // 14 yellow
        0xFFFFFF, // 15 white
    };
    return idxToRgb[colorIndex];
}

static std::string GetTeamPrefixOrSuffix(JNIEnv* env, jobject team, bool suffix) {
    if (!env || !team) return "";
    jclass teamClass = env->GetObjectClass(team);
    if (!teamClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }

    // Lunar MCP: getPrefix/getSuffix. Vanilla obfuscated: e/f.
    jmethodID mid = nullptr;
    static const char* kSuffixNames[] = {"getSuffix", "f", "getColorSuffix"};
    static const char* kPrefixNames[] = {"getPrefix", "e", "getColorPrefix"};
    const char* const* names = suffix ? kSuffixNames : kPrefixNames;
    const int nameCount = 3;
    for (int i = 0; i < nameCount; ++i) {
        mid = env->GetMethodID(teamClass, names[i], "()Ljava/lang/String;");
        if (mid && !env->ExceptionCheck()) break;
        env->ExceptionClear();
        mid = nullptr;
    }
    if (!mid && !suffix) {
        mid = env->GetMethodID(teamClass, "getRegisteredName", "()Ljava/lang/String;");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            mid = nullptr;
        }
    }
    env->DeleteLocalRef(teamClass);
    if (!mid) return "";

    jstring js = (jstring)env->CallObjectMethod(team, mid);
    if (!js || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }
    const char* p = env->GetStringUTFChars(js, nullptr);
    std::string out = p ? p : "";
    if (p) env->ReleaseStringUTFChars(js, p);
    env->DeleteLocalRef(js);
    return out;
}

std::string GetScoreboardTeamSuffix(JNIEnv* env, jobject team) {
    return GetTeamPrefixOrSuffix(env, team, true);
}

bool SetScoreboardTeamSuffix(JNIEnv* env, jobject team, const std::string& suffix) {
    if (!env || !team) return false;
    jclass teamClass = env->GetObjectClass(team);
    if (!teamClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    static const char* kMethodNames[] = {"setSuffix", "c", "d", "b"};
    jmethodID mid = nullptr;
    for (const char* name : kMethodNames) {
        mid = env->GetMethodID(teamClass, name, "(Ljava/lang/String;)V");
        if (mid && !env->ExceptionCheck()) break;
        env->ExceptionClear();
        mid = nullptr;
    }
    env->DeleteLocalRef(teamClass);
    if (!mid) return false;

    jstring js = env->NewStringUTF(suffix.c_str());
    if (!js) return false;
    env->CallVoidMethod(team, mid, js);
    const bool ok = !env->ExceptionCheck();
    if (!ok) env->ExceptionClear();
    env->DeleteLocalRef(js);
    return ok;
}

std::string TeamRgbToColorName(int rgb) {
    if (rgb == 0xFF5555) return "red";
    if (rgb == 0x5555FF) return "blue";
    if (rgb == 0x55FF55) return "green";
    if (rgb == 0xFFFF55) return "yellow";
    if (rgb == 0xFF55FF) return "light_purple";
    if (rgb == 0x55FFFF) return "aqua";
    if (rgb == 0xFFFFFF) return "white";
    if (rgb == 0xFFAA00) return "gold";
    if (rgb == 0xAAAAAA) return "gray";
    if (rgb == 0x00AAAA) return "dark_aqua";
    if (rgb == 0x00AA00) return "dark_green";
    if (rgb == 0x0000AA) return "dark_blue";
    if (rgb == 0xAA0000) return "dark_red";
    if (rgb == 0x000000) return "black";
    if (rgb == 0xAA00AA) return "dark_purple";
    if (rgb == 0x555555) return "dark_gray";
    return "";
}

static std::string RgbToColorName(int rgb) {
    return TeamRgbToColorName(rgb);
}

HitboxTeamColorInfo ResolveEntityTeamColor(JNIEnv* env, jobject mcObj, jclass mcClass,
                                           jobject entity, jclass entityClass) {
    const TeamRgbResolveResult detailed =
        ResolveEntityTeamRgbDetailed(env, mcObj, mcClass, entity, entityClass);
    HitboxTeamColorInfo info;
    info.rgb = detailed.rgb;
    info.source = detailed.source;
    info.prefix = detailed.prefix;
    info.playerName = detailed.playerName;
    return info;
}

static std::string ReadEntityStringMethod(JNIEnv* env, jobject entity, jclass entityClass,
                                          const char* const* methodNames, size_t methodCount) {
    if (!env || !entity || !entityClass || !methodNames || methodCount == 0) return "";
    for (size_t i = 0; i < methodCount; ++i) {
        jmethodID mid = env->GetMethodID(entityClass, methodNames[i], "()Ljava/lang/String;");
        if (!mid || env->ExceptionCheck()) {
            env->ExceptionClear();
            continue;
        }
        jstring js = (jstring)env->CallObjectMethod(entity, mid);
        if (!js || env->ExceptionCheck()) {
            env->ExceptionClear();
            continue;
        }
        const char* p = env->GetStringUTFChars(js, nullptr);
        std::string out = p ? p : "";
        if (p) env->ReleaseStringUTFChars(js, p);
        env->DeleteLocalRef(js);
        return out;
    }
    return "";
}

static std::string BedwarsTeamNameFromMcColorCode(char c) {
    switch ((char)std::tolower((unsigned char)c)) {
        case 'c': case '4': return "Red";
        case '9': case '1': return "Blue";
        case 'a': case '2': return "Green";
        case 'e': case '6': return "Yellow";
        case 'b': case '3': return "Aqua";
        case 'f': return "White";
        case 'd': case '5': return "Pink";
        case '8': case '7': return "Gray";
        default: return "";
    }
}

std::string BedwarsTeamNameFromTeamLetter(const std::string& teamLetter) {
    if (teamLetter.empty()) return "";
    switch ((char)std::toupper((unsigned char)teamLetter[0])) {
        case 'R': return "Red";
        case 'B': return "Blue";
        case 'G': return "Green";
        case 'Y': return "Yellow";
        case 'A': return "Aqua";
        case 'W': return "White";
        case 'P': return "Pink";
        case 'S': return "Gray";
        default: return "";
    }
}

std::string GetBedwarsTeamNameFromTabDisplay(const std::string& displayFormatted) {
    if (displayFormatted.empty()) return "";

    for (size_t i = 0; i < displayFormatted.size(); ++i) {
        char code = 0;
        if (i + 2 < displayFormatted.size() &&
            (unsigned char)displayFormatted[i] == 0xC2 &&
            (unsigned char)displayFormatted[i + 1] == 0xA7) {
            code = displayFormatted[i + 2];
            i += 2;
        } else if ((unsigned char)displayFormatted[i] == 0xA7 && i + 1 < displayFormatted.size()) {
            code = displayFormatted[i + 1];
            i += 1;
        } else {
            continue;
        }

        const std::string team = BedwarsTeamNameFromMcColorCode(code);
        if (!team.empty()) return team;
    }
    return "";
}

std::string GetEntityDisplayNameFormatted(JNIEnv* env, jobject entity, jclass entityClass) {
    if (!env || !entity) return "";
    jmethodID midDisplay = entityClass ? env->GetMethodID(entityClass, "getDisplayName", "()Lnet/minecraft/util/IChatComponent;") : nullptr;
    if (!midDisplay || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }
    jobject comp = env->CallObjectMethod(entity, midDisplay);
    if (!comp || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }

    jclass compClass = env->GetObjectClass(comp);
    jmethodID midFmt = compClass ? env->GetMethodID(compClass, "getFormattedText", "()Ljava/lang/String;") : nullptr;
    if (compClass) env->DeleteLocalRef(compClass);
    if (!midFmt || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(comp);
        return "";
    }

    jstring js = (jstring)env->CallObjectMethod(comp, midFmt);
    env->DeleteLocalRef(comp);
    if (!js || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }
    const char* p = env->GetStringUTFChars(js, nullptr);
    std::string out = p ? p : "";
    if (p) env->ReleaseStringUTFChars(js, p);
    env->DeleteLocalRef(js);
    return out;
}

static std::string GetEntityCustomNameTag(JNIEnv* env, jobject entity, jclass entityClass) {
    static const char* kMethods[] = {"aK", "getCustomNameTag"};
    return ReadEntityStringMethod(env, entity, entityClass, kMethods, 2);
}

static bool TryParseColorFromFormattedText(const std::string& formatted, int& outRgb) {
    if (formatted.empty()) return false;
    std::string colorName = ParseFirstMeaningfulColorName(formatted);
    if (colorName.empty()) colorName = ParseLastSectionColorName(formatted);
    if (colorName.empty()) return false;
    outRgb = TeamColorNameToRgb(colorName);
    return outRgb > 0;
}

bool ShouldDrawDragonHitboxTeamColor(const HitboxTeamColorInfo& info) {
    if (ShouldDrawHitboxTeamColor(info)) return true;
    if (!info.source || info.rgb <= 0) return false;
    return std::strncmp(info.source, "dragon.", 7) == 0;
}

HitboxTeamColorInfo ResolveDragonHitboxTeamColor(JNIEnv* env, jobject mcObj, jclass mcClass,
                                                 jobject entity, jclass entityClass) {
    HitboxTeamColorInfo info = ResolveEntityTeamColor(env, mcObj, mcClass, entity, entityClass);
    if (ShouldDrawDragonHitboxTeamColor(info)) return info;

    const std::string customTag = GetEntityCustomNameTag(env, entity, entityClass);
    if (!customTag.empty()) {
        int rgb = 0;
        if (TryParseColorFromFormattedText(customTag, rgb)) {
            info.rgb = rgb;
            info.source = "dragon.customTag";
            info.prefix = customTag;
            return info;
        }
    }

    const std::string display = GetEntityDisplayNameFormatted(env, entity, entityClass);
    if (!display.empty()) {
        int rgb = 0;
        if (TryParseColorFromFormattedText(display, rgb)) {
            info.rgb = rgb;
            info.source = "dragon.displayName";
            info.prefix = display;
            return info;
        }
    }

    return info;
}

std::string GetEntityTeamColorName(JNIEnv* env, jobject mcObj, jclass mcClass,
                                   jobject entity, jclass entityClass) {
    const HitboxTeamColorInfo info = ResolveEntityTeamColor(env, mcObj, mcClass, entity, entityClass);
    const std::string fromRgb = RgbToColorName(info.rgb);
    if (!fromRgb.empty() && fromRgb != "white") return fromRgb;
    std::string fromPrefix = ParseFirstMeaningfulColorName(info.prefix);
    if (!fromPrefix.empty()) return fromPrefix;
    return fromRgb;
}

int GetEntityTeamRgb(JNIEnv* env, jobject mcObj, jclass mcClass,
                     jobject entity, jclass entityClass) {
    return ResolveEntityTeamColor(env, mcObj, mcClass, entity, entityClass).rgb;
}

int TeamColorNameToRgb(const std::string& colorName) {
    const std::string c = LowerAscii(colorName);
    if (c == "black") return 0x000000;
    if (c == "dark_blue") return 0x0000AA;
    if (c == "dark_green") return 0x00AA00;
    if (c == "dark_aqua") return 0x00AAAA;
    if (c == "dark_red") return 0xAA0000;
    if (c == "dark_purple") return 0xAA00AA;
    if (c == "gold") return 0xFFAA00;
    if (c == "gray") return 0xAAAAAA;
    if (c == "dark_gray") return 0x555555;
    if (c == "blue") return 0x5555FF;
    if (c == "green") return 0x55FF55;
    if (c == "aqua") return 0x55FFFF;
    if (c == "red") return 0xFF5555;
    if (c == "light_purple") return 0xFF55FF;
    if (c == "yellow") return 0xFFFF55;
    return 0xFFFFFF;
}

namespace {
std::atomic<bool> g_vigilHypixelServerActive{false};

static bool JstringToStd(JNIEnv* env, jstring js, std::string& out) {
    out.clear();
    if (!env || !js) return false;
    const char* chars = env->GetStringUTFChars(js, nullptr);
    if (!chars) return false;
    out = chars;
    env->ReleaseStringUTFChars(js, chars);
    return true;
}

static bool ReadServerIpFromServerData(JNIEnv* env, jobject serverData, std::string& outHost) {
    outHost.clear();
    if (!env || !serverData) return false;
    jclass sdClass = env->GetObjectClass(serverData);
    if (!sdClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jfieldID fidIp = env->GetFieldID(sdClass, "serverIP", "Ljava/lang/String;");
    if (!fidIp || env->ExceptionCheck()) {
        env->ExceptionClear();
        fidIp = env->GetFieldID(sdClass, "b", "Ljava/lang/String;");
        if (!fidIp || env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(sdClass);
            return false;
        }
    }

    jstring jIp = (jstring)env->GetObjectField(serverData, fidIp);
    env->DeleteLocalRef(sdClass);
    if (!jIp || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    const bool ok = JstringToStd(env, jIp, outHost);
    env->DeleteLocalRef(jIp);
    return ok && !outHost.empty();
}
} // namespace

bool HostEndsWithHypixelNet(const std::string& hostOrIp) {
    std::string host = LowerAscii(Trim(hostOrIp));
    if (host.empty()) return false;
    const size_t colon = host.find(':');
    if (colon != std::string::npos) host = host.substr(0, colon);
    while (!host.empty() && (host.back() == '.' || host.back() == ' ')) host.pop_back();
    if (host == "hypixel.net") return true;
    static const std::string kSuffix = ".hypixel.net";
    return host.size() > kSuffix.size() &&
           host.compare(host.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0;
}

bool ReadMinecraftServerHost(JNIEnv* env, jobject mcObj, jclass mcClass, std::string& outHost) {
    outHost.clear();
    if (!env || !mcObj || !mcClass) return false;

    jobject serverData = nullptr;

    jmethodID midGet = env->GetMethodID(mcClass, "getCurrentServerData",
                                        "()Lnet/minecraft/client/multiplayer/ServerData;");
    if (!midGet || env->ExceptionCheck()) {
        env->ExceptionClear();
        midGet = env->GetMethodID(mcClass, "D", "()Lbde;");
        if (!midGet || env->ExceptionCheck()) {
            env->ExceptionClear();
            midGet = nullptr;
        }
    }
    if (midGet) {
        serverData = env->CallObjectMethod(mcObj, midGet);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            serverData = nullptr;
        }
    }

    if (!serverData) {
        jfieldID fid = env->GetFieldID(mcClass, "currentServerData",
                                       "Lnet/minecraft/client/multiplayer/ServerData;");
        if (!fid || env->ExceptionCheck()) {
            env->ExceptionClear();
            fid = env->GetFieldID(mcClass, "Q", "Lbde;");
            if (!fid || env->ExceptionCheck()) {
                env->ExceptionClear();
                fid = nullptr;
            }
        }
        if (fid) {
            serverData = env->GetObjectField(mcObj, fid);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                serverData = nullptr;
            }
        }
    }

    if (!serverData) return false;
    const bool ok = ReadServerIpFromServerData(env, serverData, outHost);
    env->DeleteLocalRef(serverData);
    return ok;
}

bool VigilIsHypixelServerActive() {
    return g_vigilHypixelServerActive.load(std::memory_order_relaxed);
}

void VigilUpdateHypixelServerWhitelist(JNIEnv* env, jobject mcObj, jclass mcClass) {
    std::string host;
    const bool readOk = ReadMinecraftServerHost(env, mcObj, mcClass, host);
    static ULONGLONG s_lastHypixelHostOkAt = 0;
    bool onHypixel = false;

    if (readOk) {
        if (HostEndsWithHypixelNet(host)) {
            onHypixel = true;
            s_lastHypixelHostOkAt = GetTickCount64();
        } else {
            // Explicitly connected elsewhere - disable immediately.
            s_lastHypixelHostOkAt = 0;
            onHypixel = false;
        }
    } else {
        // ServerData briefly null during world/server transfers on Hypixel.
        // Keep last known for a short grace so session HUD keeps ticking.
        constexpr ULONGLONG kGraceMs = 2500;
        const ULONGLONG now = GetTickCount64();
        if (s_lastHypixelHostOkAt != 0 && now - s_lastHypixelHostOkAt < kGraceMs) {
            onHypixel = true;
        } else {
            s_lastHypixelHostOkAt = 0;
            onHypixel = false;
        }
    }

    g_vigilHypixelServerActive.store(onHypixel, std::memory_order_relaxed);
}
