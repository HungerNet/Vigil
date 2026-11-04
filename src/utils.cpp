#include "utils.h"
#include "class_finder.h"
#include "jni_helpers.h"
#include <curl/curl.h>
#include <mutex>
#include <atomic>
#include <cmath>
#include <vector>
#include <cstdio>
#include <cstring>

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
        case '8': return 'S'; // dark gray (§8)
        case '7': return 'S'; // light gray (§7) — same letter, sorted below §8
        default: return '-';
    }
}

bool IsMinecraftColorCode(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

bool IsMinecraftFormatCode(char c) {
    return c == 'k' || c == 'l' || c == 'm' || c == 'n' || c == 'o' || c == 'r';
}

std::string SectionColor(char code) {
    std::string sec = "\xC2\xA7";
    return sec + std::string(1, code);
}

char FkdrColorCode(double v) {
    if (v > 1000.0) return '5';
    if (v > 100.0)  return '4';
    if (v > 10.0)   return 'c';
    if (v > 5.0)    return '6';
    if (v > 2.4)    return 'e';
    if (v > 1.4)    return 'f';
    return '7';
}

char KdrColorCode(double v) {
    if (v > 10.0) return '5';
    if (v > 5.0)  return '4';
    if (v > 3.5)  return '6';
    if (v > 2.5)  return 'e';
    if (v > 1.5)  return '2';
    if (v > 0.5)  return 'f';
    return '7';
}

char WlrColorCode(double v) {
    if (v > 10.0) return '5';
    if (v > 5.0)  return '4';
    if (v > 3.0)  return '6';
    if (v > 1.0)  return 'e';
    if (v > 0.5)  return '2';
    if (v > 0.3)  return 'f';
    return '7';
}

char DuelKdrColorCode(double v) {
    if (v > 50.0) return '5';
    if (v > 25.0) return 'd';
    if (v > 10.0) return '4';
    if (v > 5.0)  return '6';
    if (v > 3.5)  return 'e';
    if (v > 2.0)  return '2';
    if (v > 0.5)  return 'f';
    return '7';
}

char DuelWinsColorCode(int wins) {
    if (wins > 100000) return '5';
    if (wins > 50000)  return 'd';
    if (wins > 25000)  return '4';
    if (wins > 10000)  return 'c';
    if (wins > 5000)   return '6';
    if (wins > 2500)   return 'e';
    if (wins > 1000)   return '2';
    if (wins > 500)    return 'f';
    return '7';
}

char PingColorCode(int ping) {
    if (ping < 0)    return '7';
    if (ping <= 100) return 'a';
    if (ping <= 150) return '2';
    if (ping <= 200) return '6';
    if (ping <= 250) return 'c';
    return '4';
}

char BowDistanceColorCode(double blocks) {
    // Swap white and light-green per request.
    if (blocks < 8.0)  return 'f';
    if (blocks < 15.0) return 'a';
    if (blocks < 25.0) return 'e';
    if (blocks < 35.0) return '6';
    if (blocks < 50.0) return 'c';
    return '4';
}

static const char* McColorCharToEnumField(char code) {
    switch (code) {
        case '0': return "a";
        case '1': return "b";
        case '2': return "c";
        case '3': return "d";
        case '4': return "e";
        case '5': return "f";
        case '6': return "g";
        case '7': return "h";
        case '8': return "i";
        case '9': return "j";
        case 'a': return "k";
        case 'b': return "l";
        case 'c': return "m";
        case 'd': return "n";
        case 'e': return "o";
        case 'f': return "p";
        default:  return "h";
    }
}

int BedwarsExpToStars(int experience) {
    if (experience < 0) experience = 0;
    return (int)std::floor((std::sqrt(2.0 * (double)experience + 30625.0) - 175.0) / 50.0);
}

int SkywarsExpToLevel(int experience) {
    if (experience < 0) experience = 0;
    return (int)std::floor((std::sqrt(2.0 * (double)experience + 3125.0) + 50.0) / 100.0);
}

static char SkywarsLevelColorCode(int level) {
    if (level >= 300) return '0';
    if (level >= 250) return '1';
    if (level >= 200) return '2';
    if (level >= 150) return '3';
    if (level >= 100) return '4';
    if (level >= 75)  return '5';
    if (level >= 50)  return '6';
    if (level >= 35)  return 'c';
    if (level >= 25)  return 'd';
    if (level >= 15)  return '9';
    if (level >= 10)  return '3';
    if (level >= 5)   return 'f';
    return '7';
}

std::string FormatSkywarsLevelWithBrackets(int level) {
    const int n = level < 0 ? 0 : level;
    return std::string(SectionColor(SkywarsLevelColorCode(n))) + "[" + std::to_string(n) + "]" + SectionColor('r');
}

std::string GetSkywarsStarText(int stars) {
    const int n = stars < 0 ? 0 : stars;
    return FormatSkywarsLevelWithBrackets(n);
}

std::string FormatSkywarsStarsDisplay(int swLevel, int rawAchievementStars) {
    if (swLevel > 0) {
        return FormatSkywarsLevelWithBrackets(swLevel);
    }
    if (rawAchievementStars > 0) {
        return SectionColor('b') + "[" + std::to_string(rawAchievementStars) + "]" + SectionColor('r');
    }
    return SectionColor('7') + "[0]" + SectionColor('r');
}

bool IsHpChatMessage(const std::string& cleanMsg) {
    return cleanMsg.find(" is on ") != std::string::npos && cleanMsg.find(" HP!") != std::string::npos;
}

bool IsBowDistanceChatMessage(const std::string& cleanMsg) {
    return cleanMsg.find(" blocks)") != std::string::npos;
}

bool IsHypixelLocrawChatLine(const std::string& cleanMsg) {
    const std::string trimmed = Trim(cleanMsg);
    if (trimmed.empty() || trimmed[0] != '{') return false;
    const std::string lower = LowerAscii(trimmed);
    if (lower.find("\"server\":") == std::string::npos) return false;
    if (lower.find("\"gametype\":") != std::string::npos) return true;
    return lower.find("\"limbo\"") != std::string::npos;
}

bool IsIgnoredChatForWhoParty(const std::string& cleanMsg) {
    // HP lines (raw or bow-modified) must never schedule /who or party tracker.
    return IsHpChatMessage(cleanMsg) || IsBowDistanceChatMessage(cleanMsg) ||
           IsHypixelLocrawChatLine(cleanMsg);
}

std::string FormatDuelRatio(double v) {
    char buf[64];
    if (v < 10.0) sprintf_s(buf, "%.2f", v);
    else sprintf_s(buf, "%.1f", v);
    return std::string(buf);
}

char HypixelRankColorCode(const std::string& newPackageRank,
                          const std::string& monthlyPackageRank) {
    if (newPackageRank == "MVP_PLUS" && monthlyPackageRank == "SUPERSTAR") return '6';
    if (newPackageRank == "MVP" || newPackageRank == "MVP_PLUS") return 'b';
    if (newPackageRank == "VIP" || newPackageRank == "VIP_PLUS") return 'a';
    return '7';
}

std::string JsonEscapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}

void JsonLineLog(const std::string& line) {
    FrenchifyFileLog("json", line.c_str());
}

void FrenchifyFileLog(const char* tag, const char* msg) {
#if !FRENCHIFY_DEBUG_LOG
    (void)tag;
    (void)msg;
    return;
#else
    if (!msg || !msg[0]) return;
    static std::mutex s_logMu;
    std::lock_guard<std::mutex> lk(s_logMu);
    if (g_dllDir.empty()) return;

    const std::string path = g_dllDir + "\\frenchify_debug.log";
    std::ofstream fout(path, std::ios::out | std::ios::app);
    if (!fout) return;

    SYSTEMTIME st{};
    GetLocalTime(&st);
    char ts[32];
    std::snprintf(ts, sizeof(ts), "%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    fout << "[" << ts << "][" << (tag ? tag : "?") << "] " << msg << "\n";
#endif
}

void AppendFrenchifyNamedLog(const char* filename, const std::string& msg) {
    if (!filename || !filename[0] || msg.empty()) return;
    for (const char* p = filename; *p; ++p) {
        if (*p == '\\' || *p == '/' || *p == ':') return;
    }
    static std::mutex s_namedLogMu;
    std::lock_guard<std::mutex> lk(s_namedLogMu);
    if (g_dllDir.empty()) return;

    const std::string path = g_dllDir + "\\" + filename;
    std::ofstream fout(path, std::ios::out | std::ios::app);
    if (!fout) return;

    SYSTEMTIME st{};
    GetLocalTime(&st);
    char ts[32];
    std::snprintf(ts, sizeof(ts), "%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    fout << "[" << ts << "] " << msg << "\n";
}

std::string FormatSeraphReportTitleCase(const std::string& reportType) {
    std::string s;
    s.reserve(reportType.size());
    for (char c : reportType) s.push_back(c == '_' ? ' ' : c);
    bool cap = true;
    for (char& c : s) {
        if (cap && c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
            cap = false;
        } else if (c == ' ') {
            cap = true;
        } else {
            cap = false;
        }
    }
    return s;
}

char BwdScoreColorCode(double score) {
    if (score > 500.0) return '5';
    if (score > 250.0) return 'd';
    if (score > 100.0) return '4';
    if (score > 50.0)  return '6';
    if (score > 25.0)  return 'e';
    if (score > 10.0)  return '2';
    if (score > 5.0)   return 'f';
    return '7';
}

std::string GetPrestigeColorText(int stars) {
    int n = stars < 0 ? 0 : stars;
    int prestige = (n / 100) * 100;
    std::string s = std::to_string(n);
    std::string nums = s;
    if (nums.size() < 4) nums = std::string(4 - nums.size(), '0') + nums;
    auto C = [](char code) { return SectionColor(code); };
    const std::string symStar4 = "\xE2\x9C\xAB"; // âœ«
    const std::string symStar5 = "\xE2\x9C\xAA"; // âœª
    const std::string symStar6 = "\xE2\x9A\x9D"; // âš
    const std::string symStar7 = "\xE2\x9C\xA5"; // âœ¥

    switch (prestige) {
        case 0: return C('7') + "[" + s + symStar4 + "]";
        case 100: return C('f') + "[" + s + symStar4 + "]";
        case 200: return C('6') + "[" + s + symStar4 + "]";
        case 300: return C('b') + "[" + s + symStar4 + "]";
        case 400: return C('2') + "[" + s + symStar4 + "]";
        case 500: return C('3') + "[" + s + symStar4 + "]";
        case 600: return C('4') + "[" + s + symStar4 + "]";
        case 700: return C('d') + "[" + s + symStar4 + "]";
        case 800: return C('9') + "[" + s + symStar4 + "]";
        case 900: return C('5') + "[" + s + symStar4 + "]";
        case 1000: return C('c') + "[" + C('6') + nums[0] + C('e') + nums[1] + C('a') + nums[2] + C('b') + nums[3] + C('d') + symStar4 + C('5') + "]";
        case 1100: return C('7') + "[" + C('f') + s + C('7') + symStar5 + "]";
        case 1200: return C('7') + "[" + C('e') + s + C('6') + symStar5 + C('7') + "]";
        case 1300: return C('7') + "[" + C('b') + s + C('3') + symStar5 + C('7') + "]";
        case 1400: return C('7') + "[" + C('a') + s + C('2') + symStar5 + C('7') + "]";
        case 1500: return C('7') + "[" + C('3') + s + C('9') + symStar5 + C('7') + "]";
        case 1600: return C('7') + "[" + C('c') + s + C('4') + symStar5 + C('7') + "]";
        case 1700: return C('7') + "[" + C('d') + s + C('5') + symStar5 + C('7') + "]";
        case 1800: return C('7') + "[" + C('9') + s + C('1') + symStar5 + C('7') + "]";
        case 1900: return C('7') + "[" + C('5') + s + C('8') + symStar5 + C('7') + "]";
        case 2000: return C('8') + "[" + C('7') + nums[0] + C('f') + nums[1] + nums[2] + C('7') + nums[3] + symStar5 + C('8') + "]";
        case 2100: return C('f') + "[" + nums[0] + C('e') + nums[1] + nums[2] + C('6') + nums[3] + C('l') + symStar6 + C('r') + C('6') + "]";
        case 2200: return C('6') + "[" + nums[0] + C('f') + nums[1] + nums[2] + C('b') + nums[3] + C('3') + C('l') + symStar6 + C('r') + C('3') + "]";
        case 2300: return C('5') + "[" + nums[0] + C('d') + nums[1] + nums[2] + C('6') + nums[3] + C('e') + C('l') + symStar6 + C('r') + C('e') + "]";
        case 2400: return C('b') + "[" + nums[0] + C('f') + nums[1] + nums[2] + C('7') + nums[3] + C('l') + symStar6 + C('r') + C('8') + "]";
        case 2500: return C('f') + "[" + nums[0] + C('a') + nums[1] + nums[2] + C('2') + nums[3] + C('l') + symStar6 + C('r') + C('2') + "]";
        case 2600: return C('4') + "[" + nums[0] + C('c') + nums[1] + nums[2] + C('d') + nums[3] + C('l') + symStar6 + C('r') + C('d') + "]";
        case 2700: return C('e') + "[" + nums[0] + C('f') + nums[1] + nums[2] + C('8') + nums[3] + C('l') + symStar6 + C('r') + C('8') + "]";
        case 2800: return C('a') + "[" + nums[0] + C('2') + nums[1] + nums[2] + C('6') + nums[3] + C('l') + symStar6 + C('r') + C('e') + "]";
        case 2900: return C('b') + "[" + nums[0] + C('3') + nums[1] + nums[2] + C('9') + nums[3] + C('l') + symStar6 + C('r') + C('1') + "]";
        case 3000: return C('e') + "[" + nums[0] + C('6') + nums[1] + nums[2] + C('c') + nums[3] + C('l') + symStar6 + C('r') + C('4') + "]";
        case 3100: return C('9') + "[" + nums[0] + C('2') + nums[1] + nums[2] + C('6') + nums[3] + C('l') + symStar7 + C('r') + C('e') + "]";
        case 3200: return C('c') + "[" + C('4') + nums[0] + C('7') + nums[1] + nums[2] + C('4') + nums[3] + C('c') + C('l') + symStar7 + C('r') + C('c') + "]";
        case 3300: return C('9') + "[" + nums[0] + nums[1] + C('d') + nums[2] + C('c') + nums[3] + C('l') + symStar7 + C('r') + C('4') + "]";
        case 3400: return C('2') + "[" + C('a') + nums[0] + C('d') + nums[1] + nums[2] + C('5') + nums[3] + C('l') + symStar7 + C('r') + C('2') + "]";
        case 3500: return C('c') + "[" + C('4') + nums[0] + nums[1] + nums[2] + C('2') + nums[3] + C('a') + C('l') + symStar7 + C('r') + C('a') + "]";
        case 3600: return C('a') + "[" + nums[0] + nums[1] + C('b') + nums[2] + C('9') + nums[3] + C('l') + symStar7 + C('r') + C('1') + "]";
        case 3700: return C('4') + "[" + C('c') + nums[0] + nums[1] + nums[2] + C('b') + nums[3] + C('3') + C('l') + symStar7 + C('r') + C('3') + "]";
        case 3800: return C('1') + "[" + C('9') + nums[0] + C('3') + nums[1] + C('b') + nums[2] + nums[3] + C('d') + C('l') + symStar7 + C('r') + C('1') + "]";
        case 3900: return C('c') + "[" + C('a') + nums[0] + nums[1] + nums[2] + C('3') + nums[3] + C('9') + C('l') + symStar7 + C('r') + C('9') + "]";
        case 4000: return C('5') + "[" + C('c') + nums[0] + nums[1] + nums[2] + C('6') + nums[3] + C('l') + symStar7 + C('r') + C('e') + "]";
        case 4100: return C('e') + "[" + C('6') + nums[0] + nums[1] + nums[2] + C('d') + nums[3] + C('l') + symStar7 + C('r') + C('5') + "]";
        case 4200: return C('1') + "[" + C('9') + nums[0] + C('3') + nums[1] + nums[2] + C('b') + nums[3] + C('7') + C('l') + symStar7 + C('r') + C('7') + "]";
        case 4300: return C('0') + "[" + C('5') + nums[0] + C('8') + nums[1] + nums[2] + C('5') + nums[3] + C('l') + symStar7 + C('r') + C('0') + "]";
        case 4400: return C('2') + "[" + C('a') + nums[0] + C('e') + nums[1] + nums[2] + C('5') + nums[3] + C('l') + symStar7 + C('r') + C('d') + "]";
        case 4500: return C('f') + "[" + C('b') + nums[0] + nums[1] + nums[2] + C('2') + nums[3] + C('l') + symStar7 + C('r') + C('2') + "]";
        case 4600: return C('2') + "[" + C('b') + nums[0] + C('e') + nums[1] + nums[2] + C('6') + nums[3] + C('l') + symStar7 + C('r') + C('5') + "]";
        case 4700: return C('f') + "[" + C('c') + nums[0] + nums[1] + nums[2] + C('9') + nums[3] + C('1') + C('l') + symStar7 + C('r') + C('9') + "]";
        case 4800: return C('5') + "[" + C('c') + nums[0] + C('6') + nums[1] + C('e') + nums[2] + nums[3] + C('b') + C('l') + symStar7 + C('r') + C('3') + "]";
        case 4900: return C('2') + "[" + C('a') + nums[0] + C('f') + nums[1] + nums[2] + C('a') + nums[3] + C('l') + symStar7 + C('r') + C('2') + "]";
        case 5000: return C('4') + "[" + C('5') + nums[0] + C('9') + nums[1] + nums[2] + nums[3] + C('1') + C('l') + symStar7 + C('r') + C('0') + "]";
        default: return C('4') + "[" + C('5') + nums[0] + C('9') + nums[1] + nums[2] + nums[3] + C('1') + C('l') + symStar7 + C('r') + C('0') + "]";
    }
}

static void AddUniqueIgn(std::vector<std::string>& out, std::set<std::string>& seen, std::string name) {
    name = Trim(name);
    while (!name.empty() && (name.back() == '.' || name.back() == ';' || name.back() == ']' || name.back() == '[')) {
        name.pop_back();
    }
    if (!IsValidIgn(name)) return;
    const std::string k = LowerAscii(name);
    if (seen.insert(k).second) out.push_back(name);
}

static void SplitCommaNames(const std::string& payload, std::vector<std::string>& out, std::set<std::string>& seen) {
    std::string cur;
    for (size_t i = 0; i <= payload.size(); ++i) {
        const char ch = (i < payload.size()) ? payload[i] : ',';
        if (ch == ',') {
            AddUniqueIgn(out, seen, cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
}

static std::vector<std::string> ParseBedwarsOnlineNames(const std::string& cleanMsg) {
    std::vector<std::string> out;
    std::string upper = cleanMsg;
    for (char& c : upper) {
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    }

    const size_t pos = upper.find("ONLINE:");
    if (pos == std::string::npos) return out;
    std::string payload = cleanMsg.substr(pos + 7);

    size_t cut = std::string::npos;
    const size_t offPos = payload.find(" OFFLINE:");
    if (offPos != std::string::npos) cut = offPos;
    const size_t inPos = payload.find(" IN GAME:");
    if (inPos != std::string::npos) cut = (cut == std::string::npos ? inPos : (std::min)(cut, inPos));
    if (cut != std::string::npos) payload = payload.substr(0, cut);

    std::set<std::string> seen;
    SplitCommaNames(payload, out, seen);
    return out;
}

static std::vector<std::string> ParseDuelTeamNames(const std::string& cleanMsg) {
    std::vector<std::string> out;
    const size_t teamPos = cleanMsg.find("Team #");
    if (teamPos == std::string::npos) return out;

    const size_t bracketOpen = cleanMsg.find('[', teamPos);
    const size_t bracketClose = (bracketOpen != std::string::npos) ? cleanMsg.find(']', bracketOpen) : std::string::npos;
    if (bracketOpen == std::string::npos || bracketClose == std::string::npos || bracketClose <= bracketOpen) return out;

    std::set<std::string> seen;
    SplitCommaNames(cleanMsg.substr(bracketOpen + 1, bracketClose - bracketOpen - 1), out, seen);
    return out;
}

static std::vector<std::string> ParseSkywarsTeamNames(const std::string& cleanMsg) {
    std::vector<std::string> out;
    const size_t teamPos = cleanMsg.find("Team #");
    if (teamPos == std::string::npos) return out;
    if (cleanMsg.find('[', teamPos) != std::string::npos) return out;

    const size_t colonPos = cleanMsg.find(':', teamPos);
    if (colonPos == std::string::npos) return out;

    std::set<std::string> seen;
    SplitCommaNames(cleanMsg.substr(colonPos + 1), out, seen);
    return out;
}

bool IsWhoFullOnlineLine(const std::string& cleanMsg) {
    std::string upper = cleanMsg;
    for (char& c : upper) {
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    }
    return upper.find("ONLINE:") != std::string::npos;
}

bool IsWhoTeamsHeaderLine(const std::string& cleanMsg) {
    const std::string t = Trim(cleanMsg);
    if (t.empty()) return false;
    std::string lower = LowerAscii(t);
    return lower == "teams:" || lower == "teams";
}

bool IsWhoModeHeaderLine(const std::string& cleanMsg) {
    const std::string t = Trim(cleanMsg);
    if (t.empty()) return false;
    std::string lower = LowerAscii(t);
    return lower.rfind("mode:", 0) == 0;
}

std::vector<std::string> ParseOnlineNamesFromMessage(const std::string& cleanMsg) {
    if (cleanMsg.empty()) return {};

    std::vector<std::string> out = ParseBedwarsOnlineNames(cleanMsg);
    if (!out.empty()) return out;

    out = ParseDuelTeamNames(cleanMsg);
    if (!out.empty()) return out;

    return ParseSkywarsTeamNames(cleanMsg);
}

static constexpr long kHttpTimeoutSec = 4L;
static thread_local DWORD g_httpLastErr = 0;
static std::once_flag g_curlOnce;
static thread_local CURL* g_curlTls = nullptr;

static void EnsureCurlGlobal() {
    std::call_once(g_curlOnce, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

static size_t CurlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    const size_t total = size * nmemb;
    if (userdata && total > 0) {
        static_cast<std::string*>(userdata)->append(ptr, total);
    }
    return total;
}

bool HttpGetHttps(const std::string& url, std::string& body, DWORD& status, const std::string& extraHeaderLine) {
    body.clear();
    status = 0;
    g_httpLastErr = 0;
    if (url.empty()) return false;

    EnsureCurlGlobal();
    if (!g_curlTls) {
        g_curlTls = curl_easy_init();
        if (!g_curlTls) {
            g_httpLastErr = (DWORD)CURLE_FAILED_INIT;
            return false;
        }
    }
    CURL* curl = g_curlTls;
    curl_easy_reset(curl);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "FrenchifyInject/1.5");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kHttpTimeoutSec);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kHttpTimeoutSec);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

    struct curl_slist* headers = nullptr;
    if (!extraHeaderLine.empty()) {
        headers = curl_slist_append(headers, extraHeaderLine.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    const CURLcode res = curl_easy_perform(curl);
    if (headers) curl_slist_free_all(headers);

    long httpCode = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        status = (DWORD)httpCode;
    } else {
        g_httpLastErr = (DWORD)res;
    }

    return res == CURLE_OK && status > 0;
}

DWORD HttpGetLastErrorCode() { return g_httpLastErr; }

// ===== Bow distance (ArrowLoose + "is on X HP!" chat) =====

static bool g_bowDistanceWaiting = false;
static std::string g_bowLastHpChatHandled;
static double g_bowPlayerX = 0.0;
static double g_bowPlayerY = 0.0;
static double g_bowPlayerZ = 0.0;
static ULONGLONG g_bowShotAt = 0;
static const ULONGLONG kBowHpChatTimeoutMs = 8000;

static double Distance3D(double x1, double y1, double z1, double x2, double y2, double z2) {
    const double dx = x1 - x2;
    const double dy = y1 - y2;
    const double dz = z1 - z2;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

static int BowGetHeldItemId(JNIEnv* env, jobject entity, jclass entityClass) {
    jmethodID midHeld = env->GetMethodID(entityClass, "getCurrentEquippedItem", "()Lnet/minecraft/item/ItemStack;");
    if (!midHeld) {
        env->ExceptionClear();
        midHeld = env->GetMethodID(entityClass, "getHeldItem", "()Lnet/minecraft/item/ItemStack;");
    }
    if (!midHeld) {
        env->ExceptionClear();
        return -1;
    }
    jobject stack = env->CallObjectMethod(entity, midHeld);
    if (!stack || env->ExceptionCheck()) {
        env->ExceptionClear();
        return -1;
    }
    jclass stackClass = env->GetObjectClass(stack);
    jmethodID midGetItem = env->GetMethodID(stackClass, "getItem", "()Lnet/minecraft/item/Item;");
    if (!midGetItem) {
        env->ExceptionClear();
        env->DeleteLocalRef(stackClass);
        env->DeleteLocalRef(stack);
        return -1;
    }
    jobject item = env->CallObjectMethod(stack, midGetItem);
    if (!item || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(stackClass);
        env->DeleteLocalRef(stack);
        return -1;
    }
    jclass itemClass = env->GetObjectClass(item);
    jmethodID midGetId = JniResolveStaticMethod(env, itemClass, "(Lnet/minecraft/item/Item;)I",
                                                "getIdFromItem", "b");
    if (!midGetId) {
        midGetId = JniResolveStaticMethod(env, itemClass, "(Lnet/minecraft/item/Item;)I", nullptr, "b");
    }
    int itemId = -1;
    if (midGetId) {
        itemId = env->CallStaticIntMethod(itemClass, midGetId, item);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            itemId = -1;
        }
    } else {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(itemClass);
    env->DeleteLocalRef(item);
    env->DeleteLocalRef(stackClass);
    env->DeleteLocalRef(stack);
    return itemId;
}

static std::string BowGetEntityProfileName(JNIEnv* env, jobject entity, jclass entityClass) {
    jmethodID midProfile = env->GetMethodID(entityClass, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;");
    if (!midProfile || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }
    jobject profile = env->CallObjectMethod(entity, midProfile);
    if (!profile || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }
    jclass profileClass = env->GetObjectClass(profile);
    jmethodID midGetName = env->GetMethodID(profileClass, "getName", "()Ljava/lang/String;");
    std::string name;
    if (midGetName && !env->ExceptionCheck()) {
        jstring jname = (jstring)env->CallObjectMethod(profile, midGetName);
        if (jname && !env->ExceptionCheck()) {
            const char* n = env->GetStringUTFChars(jname, nullptr);
            if (n) name = n;
            if (n) env->ReleaseStringUTFChars(jname, n);
            env->DeleteLocalRef(jname);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(profileClass);
    env->DeleteLocalRef(profile);
    return name;
}

static bool ParseHpChatMessage(const std::string& cleanMsg, std::string& username, std::string& health) {
    const std::string mid = " is on ";
    const std::string suffix = " HP!";
    const size_t midPos = cleanMsg.find(mid);
    if (midPos == std::string::npos) return false;
    const size_t sufPos = cleanMsg.find(suffix, midPos + mid.size());
    if (sufPos == std::string::npos) return false;
    username = Trim(cleanMsg.substr(0, midPos));
    health = Trim(cleanMsg.substr(midPos + mid.size(), sufPos - (midPos + mid.size())));
    return !username.empty() && !health.empty();
}

static std::string StripBowDistanceSuffix(const std::string& text) {
    std::string out = text;
    const std::string tail = " blocks)";
    for (;;) {
        const size_t tailPos = out.rfind(tail);
        if (tailPos == std::string::npos) break;
        const size_t open = out.rfind(" (", tailPos);
        if (open == std::string::npos) break;
        out.erase(open, tailPos + tail.size() - open);
    }
    return out;
}

static std::string BowHpDedupeKey(const std::string& username, const std::string& health) {
    return LowerAscii(StripMinecraftFormatting(username)) + "|" + health;
}

void RunBowDistanceShotTracker(JNIEnv* env, jobject mcObj, jclass mcClass, bool enabled) {
    if (!enabled) {
        g_bowDistanceWaiting = false;
        return;
    }
    if (!env || !mcObj || !mcClass) return;

    const ULONGLONG now = GetTickCount64();
    if (g_bowDistanceWaiting && g_bowShotAt != 0 && (now - g_bowShotAt) > kBowHpChatTimeoutMs) {
        g_bowDistanceWaiting = false;
    }

    static bool s_wasUsingBow = false;
    static jfieldID s_fidPosX = nullptr;
    static jfieldID s_fidPosY = nullptr;
    static jfieldID s_fidPosZ = nullptr;
    static bool s_posResolved = false;

    jfieldID fidSelf = env->GetFieldID(mcClass, "thePlayer", "Lnet/minecraft/client/entity/EntityPlayerSP;");
    if (!fidSelf || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    jobject selfPlayer = env->GetObjectField(mcObj, fidSelf);
    if (!selfPlayer || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    jclass selfClass = env->GetObjectClass(selfPlayer);
    const int heldId = BowGetHeldItemId(env, selfPlayer, selfClass);
    jmethodID midUsing = JniResolveInstanceMethod(env, selfClass, "()Z", "isUsingItem", "bS");
    bool usingItem = false;
    if (midUsing && !env->ExceptionCheck()) {
        usingItem = (bool)env->CallBooleanMethod(selfPlayer, midUsing);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            usingItem = false;
        }
    } else {
        env->ExceptionClear();
    }

    const bool usingBowNow = usingItem && heldId == 261;
    if (s_wasUsingBow && !usingBowNow) {
        if (!s_posResolved) {
            s_posResolved = true;
            jclass baseEntityClass = McFindClass(env, "net/minecraft/entity/Entity");
            if (baseEntityClass && !env->ExceptionCheck()) {
                s_fidPosX = env->GetFieldID(baseEntityClass, "posX", "D");
                if (env->ExceptionCheck()) { env->ExceptionClear(); s_fidPosX = nullptr; }
                s_fidPosY = env->GetFieldID(baseEntityClass, "posY", "D");
                if (env->ExceptionCheck()) { env->ExceptionClear(); s_fidPosY = nullptr; }
                s_fidPosZ = env->GetFieldID(baseEntityClass, "posZ", "D");
                if (env->ExceptionCheck()) { env->ExceptionClear(); s_fidPosZ = nullptr; }
                env->DeleteLocalRef(baseEntityClass);
            } else {
                env->ExceptionClear();
            }
        }
        if (s_fidPosX && s_fidPosY && s_fidPosZ) {
            g_bowPlayerX = env->GetDoubleField(selfPlayer, s_fidPosX);
            g_bowPlayerY = env->GetDoubleField(selfPlayer, s_fidPosY);
            g_bowPlayerZ = env->GetDoubleField(selfPlayer, s_fidPosZ);
            if (!env->ExceptionCheck()) {
                g_bowDistanceWaiting = true;
                g_bowShotAt = now;
                g_bowLastHpChatHandled.clear();
            } else {
                env->ExceptionClear();
            }
        }
    }
    s_wasUsingBow = usingBowNow;

    env->DeleteLocalRef(selfClass);
    env->DeleteLocalRef(selfPlayer);
}

static std::string GetChatComponentFormattedText(JNIEnv* env, jobject chatComponent) {
    if (!env || !chatComponent) return "";
    jclass cc = env->GetObjectClass(chatComponent);
    if (!cc || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }
    jmethodID midFmt = env->GetMethodID(cc, "getFormattedText", "()Ljava/lang/String;");
    if (!midFmt || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(cc);
        return "";
    }
    jstring jFmt = (jstring)env->CallObjectMethod(chatComponent, midFmt);
    env->DeleteLocalRef(cc);
    if (!jFmt || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }
    const char* c = env->GetStringUTFChars(jFmt, nullptr);
    std::string out = c ? c : "";
    if (c) env->ReleaseStringUTFChars(jFmt, c);
    env->DeleteLocalRef(jFmt);
    return out;
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

bool ReadGuiStaticBool(JNIEnv* env, jclass customGuiClass, const char* fieldName, bool fallback) {
    if (!env || !customGuiClass || !fieldName) return fallback;
    jfieldID fid = env->GetStaticFieldID(customGuiClass, fieldName, "Z");
    if (!fid || env->ExceptionCheck()) {
        env->ExceptionClear();
        return fallback;
    }
    return (bool)env->GetStaticBooleanField(customGuiClass, fid);
}

int ReadGuiStaticInt(JNIEnv* env, jclass customGuiClass, const char* fieldName, int fallback) {
    if (!env || !customGuiClass || !fieldName) return fallback;
    jfieldID fid = env->GetStaticFieldID(customGuiClass, fieldName, "I");
    if (!fid || env->ExceptionCheck()) {
        env->ExceptionClear();
        return fallback;
    }
    return (int)env->GetStaticIntField(customGuiClass, fid);
}

std::string ReadGuiStaticString(JNIEnv* env, jclass customGuiClass, const char* fieldName, const std::string& fallback) {
    if (!env || !customGuiClass || !fieldName) return fallback;
    jfieldID fid = env->GetStaticFieldID(customGuiClass, fieldName, "Ljava/lang/String;");
    if (!fid || env->ExceptionCheck()) {
        env->ExceptionClear();
        return fallback;
    }
    jstring js = (jstring)env->GetStaticObjectField(customGuiClass, fid);
    if (!js || env->ExceptionCheck()) {
        env->ExceptionClear();
        return fallback;
    }
    const char* chars = env->GetStringUTFChars(js, nullptr);
    std::string out = chars ? chars : fallback;
    if (chars) env->ReleaseStringUTFChars(js, chars);
    env->DeleteLocalRef(js);
    return out;
}

namespace {
std::atomic<bool> g_frenchifyHypixelServerActive{false};

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

bool FrenchifyIsHypixelServerActive() {
    return g_frenchifyHypixelServerActive.load(std::memory_order_relaxed);
}

void FrenchifyUpdateHypixelServerWhitelist(JNIEnv* env, jobject mcObj, jclass mcClass) {
    std::string host;
    const bool readOk = ReadMinecraftServerHost(env, mcObj, mcClass, host);
    static ULONGLONG s_lastHypixelHostOkAt = 0;
    bool onHypixel = false;

    if (readOk) {
        if (HostEndsWithHypixelNet(host)) {
            onHypixel = true;
            s_lastHypixelHostOkAt = GetTickCount64();
        } else {
            // Explicitly connected elsewhere — disable immediately.
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

    g_frenchifyHypixelServerActive.store(onHypixel, std::memory_order_relaxed);
}

static jobject BowEnumFormatting(JNIEnv* env, char colorCode);

static void BowSetComponentColor(JNIEnv* env, jobject comp, char colorCode) {
    if (!comp || colorCode == 0) return;

    jclass ezClass = McFindClass(env, "net/minecraft/util/ChatStyle");
    jmethodID ezCtor = ezClass ? env->GetMethodID(ezClass, "<init>", "()V") : nullptr;
    jmethodID setColor = ezClass ? env->GetMethodID(ezClass, "setColor", "(Lnet/minecraft/util/EnumChatFormatting;)Lnet/minecraft/util/ChatStyle;") : nullptr;
    jmethodID midSetStyle = JniResolveInstanceMethod(env, env->GetObjectClass(comp),
        "(Lnet/minecraft/util/ChatStyle;)Lnet/minecraft/util/IChatComponent;",
        "setChatStyle", "a");
    if (!midSetStyle) {
        midSetStyle = JniResolveInstanceMethod(env, env->GetObjectClass(comp),
            "(Lnet/minecraft/util/ChatStyle;)Lnet/minecraft/util/IChatComponent;", nullptr, "a");
    }
    if (!ezClass || !ezCtor || !setColor || !midSetStyle || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (ezClass) env->DeleteLocalRef(ezClass);
        return;
    }

    jobject enumFmt = BowEnumFormatting(env, colorCode);
    jobject style = env->NewObject(ezClass, ezCtor);
    if (style && enumFmt && !env->ExceptionCheck()) {
        jobject styled = env->CallObjectMethod(style, setColor, enumFmt);
        if (styled && !env->ExceptionCheck()) {
            env->CallObjectMethod(comp, midSetStyle, styled);
            if (env->ExceptionCheck()) env->ExceptionClear();
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    if (enumFmt) env->DeleteLocalRef(enumFmt);
    if (style) env->DeleteLocalRef(style);
    env->DeleteLocalRef(ezClass);
}

static void BowRecolorUsernameInChat(JNIEnv* env, jobject comp, const std::string& targetLower, char nameColor) {
    if (!env || !comp || nameColor == 0) return;

    jclass euClass = McFindClass(env, "net/minecraft/util/IChatComponent");
    if (!euClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    jmethodID midGetText = env->GetMethodID(euClass, "getUnformattedText", "()Ljava/lang/String;");
    jmethodID midGetSiblings = env->GetMethodID(euClass, "getSiblings", "()Ljava/util/List;");
    if (!midGetText || !midGetSiblings || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(euClass);
        return;
    }

    jstring jText = (jstring)env->CallObjectMethod(comp, midGetText);
    if (jText && !env->ExceptionCheck()) {
        const char* t = env->GetStringUTFChars(jText, nullptr);
        const std::string plain = StripMinecraftFormatting(t ? t : "");
        if (t) env->ReleaseStringUTFChars(jText, t);
        env->DeleteLocalRef(jText);
        if (!plain.empty() && LowerAscii(plain) == targetLower) {
            BowSetComponentColor(env, comp, nameColor);
        }
    } else {
        env->ExceptionClear();
    }

    jobject siblings = env->CallObjectMethod(comp, midGetSiblings);
    if (siblings && !env->ExceptionCheck()) {
        jclass listClass = env->GetObjectClass(siblings);
        jmethodID midSize = listClass ? env->GetMethodID(listClass, "size", "()I") : nullptr;
        jmethodID midGet = listClass ? env->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;") : nullptr;
        if (midSize && midGet && !env->ExceptionCheck()) {
            const int n = env->CallIntMethod(siblings, midSize);
            for (int i = 0; i < n; ++i) {
                jobject child = env->CallObjectMethod(siblings, midGet, i);
                if (child && !env->ExceptionCheck()) {
                    BowRecolorUsernameInChat(env, child, targetLower, nameColor);
                    env->DeleteLocalRef(child);
                } else {
                    env->ExceptionClear();
                }
            }
        } else {
            env->ExceptionClear();
        }
        if (listClass) env->DeleteLocalRef(listClass);
        env->DeleteLocalRef(siblings);
    } else {
        env->ExceptionClear();
    }

    env->DeleteLocalRef(euClass);
}

static jobject BowEnumFormatting(JNIEnv* env, char colorCode) {
    jclass enumClass = McFindClass(env, "net/minecraft/util/EnumChatFormatting");
    if (!enumClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    const char* fieldName = McColorCharToEnumField(colorCode);
    jfieldID fid = env->GetStaticFieldID(enumClass, fieldName, "Lnet/minecraft/util/EnumChatFormatting;");
    if (!fid || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(enumClass);
        return nullptr;
    }
    jobject value = env->GetStaticObjectField(enumClass, fid);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        value = nullptr;
    }
    env->DeleteLocalRef(enumClass);
    return value;
}

static jobject BowNewTextWithColor(JNIEnv* env, const char* text, char colorCode) {
    if (!env || !text) return nullptr;

    jclass textClass = McFindClass(env, "net/minecraft/util/ChatComponentText");
    if (!textClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    jmethodID textCtor = env->GetMethodID(textClass, "<init>", "(Ljava/lang/String;)V");
    if (!textCtor || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(textClass);
        return nullptr;
    }
    jstring jText = env->NewStringUTF(text);
    if (!jText) {
        env->DeleteLocalRef(textClass);
        return nullptr;
    }
    jobject component = env->NewObject(textClass, textCtor, jText);
    env->DeleteLocalRef(jText);
    if (!component || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(textClass);
        return nullptr;
    }

    jobject enumFmt = BowEnumFormatting(env, colorCode);
    if (enumFmt) {
        jclass styleClass = McFindClass(env, "net/minecraft/util/ChatStyle");
        jmethodID styleCtor = styleClass ? env->GetMethodID(styleClass, "<init>", "()V") : nullptr;
        jmethodID setColor = styleClass ? env->GetMethodID(styleClass, "setColor", "(Lnet/minecraft/util/EnumChatFormatting;)Lnet/minecraft/util/ChatStyle;") : nullptr;
        if (styleCtor && setColor && !env->ExceptionCheck()) {
            jobject style = env->NewObject(styleClass, styleCtor);
            if (style && !env->ExceptionCheck()) {
                jobject styled = env->CallObjectMethod(style, setColor, enumFmt);
                if (styled && !env->ExceptionCheck()) {
                    jmethodID setStyle = env->GetMethodID(textClass, "setChatStyle", "(Lnet/minecraft/util/ChatStyle;)Lnet/minecraft/util/IChatComponent;");
                    if (setStyle && !env->ExceptionCheck()) {
                        env->CallObjectMethod(component, setStyle, styled);
                        if (env->ExceptionCheck()) env->ExceptionClear();
                    } else {
                        env->ExceptionClear();
                    }
                } else {
                    env->ExceptionClear();
                }
                env->DeleteLocalRef(style);
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }
        if (styleClass) env->DeleteLocalRef(styleClass);
        env->DeleteLocalRef(enumFmt);
    }

    env->DeleteLocalRef(textClass);
    return component;
}

static jobject BowBuildHpChatMessage(JNIEnv* env, jobject original, const char* distText, char distColor,
                                     const std::string& targetLower, char nameColor) {
    if (!env || !original || !distText) return nullptr;

    jclass chatClass = McFindClass(env, "net/minecraft/util/IChatComponent");
    if (!chatClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    jmethodID midCopy = env->GetMethodID(chatClass, "createCopy", "()Lnet/minecraft/util/IChatComponent;");
    jmethodID midAppend = env->GetMethodID(chatClass, "appendSibling", "(Lnet/minecraft/util/IChatComponent;)Lnet/minecraft/util/IChatComponent;");
    if (!midCopy || !midAppend || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(chatClass);
        return nullptr;
    }

    jobject copy = env->CallObjectMethod(original, midCopy);
    if (!copy || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(chatClass);
        return nullptr;
    }

    if (nameColor != 0) {
        BowRecolorUsernameInChat(env, copy, targetLower, nameColor);
    }

    jobject openParen = BowNewTextWithColor(env, " (", '7');
    jobject distPart = BowNewTextWithColor(env, distText, distColor);
    jobject closePart = BowNewTextWithColor(env, " blocks)", '7');
    if (openParen && distPart && closePart) {
        env->CallObjectMethod(copy, midAppend, openParen);
        env->CallObjectMethod(copy, midAppend, distPart);
        env->CallObjectMethod(copy, midAppend, closePart);
        if (env->ExceptionCheck()) env->ExceptionClear();
    } else {
        env->ExceptionClear();
        env->DeleteLocalRef(copy);
        copy = nullptr;
    }

    if (openParen) env->DeleteLocalRef(openParen);
    if (distPart) env->DeleteLocalRef(distPart);
    if (closePart) env->DeleteLocalRef(closePart);
    env->DeleteLocalRef(chatClass);
    return copy;
}

bool TryHandleBowDistanceHpChat(JNIEnv* env, jobject mcObj, jclass mcClass, jobject chatComponent,
                                const std::string& rawMsg, const std::string& cleanMsg) {
    if (!g_bowDistanceWaiting || !env || !mcObj || !mcClass) return false;

    const ULONGLONG now = GetTickCount64();
    if (g_bowShotAt == 0 || (now - g_bowShotAt) > kBowHpChatTimeoutMs) {
        g_bowDistanceWaiting = false;
        return false;
    }

    const std::string baseClean = StripBowDistanceSuffix(cleanMsg);

    std::string username;
    std::string health;
    if (!ParseHpChatMessage(baseClean, username, health)) return false;

    const std::string dedupeKey = BowHpDedupeKey(username, health);
    if (dedupeKey == g_bowLastHpChatHandled) return true;

    const std::string targetName = LowerAscii(StripMinecraftFormatting(username));

    jfieldID fidWorld = env->GetFieldID(mcClass, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");
    if (!fidWorld || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    jobject world = env->GetObjectField(mcObj, fidWorld);
    if (!world || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jclass worldClass = env->GetObjectClass(world);
    jfieldID fidPlayers = worldClass ? env->GetFieldID(worldClass, "playerEntities", "Ljava/util/List;") : nullptr;
    if (!fidPlayers || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (worldClass) env->DeleteLocalRef(worldClass);
        env->DeleteLocalRef(world);
        return false;
    }

    jobject playerList = env->GetObjectField(world, fidPlayers);
    if (worldClass) env->DeleteLocalRef(worldClass);
    env->DeleteLocalRef(world);
    if (!playerList || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jclass listClass = env->GetObjectClass(playerList);
    jmethodID midSize = listClass ? env->GetMethodID(listClass, "size", "()I") : nullptr;
    jmethodID midGet = listClass ? env->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;") : nullptr;
    if (listClass) env->DeleteLocalRef(listClass);
    if (!midSize || !midGet || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(playerList);
        return false;
    }

    static jfieldID s_fidPosX = nullptr;
    static jfieldID s_fidPosY = nullptr;
    static jfieldID s_fidPosZ = nullptr;
    static bool s_posResolved = false;
    if (!s_posResolved) {
        s_posResolved = true;
        jclass baseEntityClass = McFindClass(env, "net/minecraft/entity/Entity");
        if (baseEntityClass && !env->ExceptionCheck()) {
            s_fidPosX = env->GetFieldID(baseEntityClass, "posX", "D");
            if (env->ExceptionCheck()) { env->ExceptionClear(); s_fidPosX = nullptr; }
            s_fidPosY = env->GetFieldID(baseEntityClass, "posY", "D");
            if (env->ExceptionCheck()) { env->ExceptionClear(); s_fidPosY = nullptr; }
            s_fidPosZ = env->GetFieldID(baseEntityClass, "posZ", "D");
            if (env->ExceptionCheck()) { env->ExceptionClear(); s_fidPosZ = nullptr; }
            env->DeleteLocalRef(baseEntityClass);
        } else {
            env->ExceptionClear();
        }
    }

    bool found = false;
    char nameColor = 0;
    double enemyX = 0.0, enemyY = 0.0, enemyZ = 0.0;
    const int count = env->CallIntMethod(playerList, midSize);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(playerList);
        return false;
    }

    for (int i = 0; i < count; ++i) {
        jobject player = env->CallObjectMethod(playerList, midGet, i);
        if (!player || env->ExceptionCheck()) {
            env->ExceptionClear();
            continue;
        }
        jclass playerClass = env->GetObjectClass(player);
        const std::string profileName = BowGetEntityProfileName(env, player, playerClass);
        if (LowerAscii(StripMinecraftFormatting(profileName)) != targetName) {
            env->DeleteLocalRef(playerClass);
            env->DeleteLocalRef(player);
            continue;
        }
        const std::string teamColorName = GetEntityTeamColorName(env, mcObj, mcClass, player, playerClass);
        nameColor = TeamColorNameToMcChar(teamColorName);
        env->DeleteLocalRef(playerClass);
        if (s_fidPosX && s_fidPosY && s_fidPosZ) {
            enemyX = env->GetDoubleField(player, s_fidPosX);
            enemyY = env->GetDoubleField(player, s_fidPosY);
            enemyZ = env->GetDoubleField(player, s_fidPosZ);
            if (!env->ExceptionCheck()) {
                found = true;
            } else {
                env->ExceptionClear();
            }
        }
        env->DeleteLocalRef(player);
        break;
    }
    env->DeleteLocalRef(playerList);

    if (!found) return false;

    g_bowLastHpChatHandled = dedupeKey;

    double arrowDistance = Distance3D(g_bowPlayerX, g_bowPlayerY, g_bowPlayerZ, enemyX, enemyY, enemyZ);
    arrowDistance = std::round(arrowDistance * 10.0) / 10.0;

    char distBuf[32];
    std::snprintf(distBuf, sizeof(distBuf), "%.1f", arrowDistance);
    const char distColor = BowDistanceColorCode(arrowDistance);

    RemoveLatestChatLine(env, mcObj, mcClass);

    bool printed = false;
    if (chatComponent) {
        jobject outComponent = BowBuildHpChatMessage(env, chatComponent, distBuf, distColor, targetName, nameColor);
        if (outComponent) {
            PrintChatComponent(env, mcObj, mcClass, outComponent);
            env->DeleteLocalRef(outComponent);
            printed = true;
        }
    }

    if (!printed) {
        const std::string plainName = StripMinecraftFormatting(username);
        const std::string mid = " is on ";
        const std::string suffix = " HP!";
        const size_t midPos = baseClean.find(mid);
        const size_t sufPos = baseClean.find(suffix, midPos != std::string::npos ? midPos + mid.size() : 0);
        std::string outMsg;
        if (midPos != std::string::npos && sufPos != std::string::npos) {
            const std::string tail = baseClean.substr(sufPos + suffix.size());
            const char nc = nameColor ? nameColor : 'f';
            outMsg = SectionColor(nc) + plainName + baseClean.substr(midPos, sufPos + suffix.size() - midPos) +
                     SectionColor('7') + " (" + SectionColor(distColor) + distBuf + SectionColor('7') + " blocks)" + tail;
        } else {
            std::string formatted = chatComponent ? GetChatComponentFormattedText(env, chatComponent) : rawMsg;
            if (formatted.empty()) formatted = rawMsg;
            outMsg = formatted + SectionColor('7') + " (" + SectionColor(distColor) + distBuf +
                     SectionColor('7') + " blocks)";
        }
        PrintLocalChat(env, mcObj, mcClass, outMsg);
    }

    g_bowDistanceWaiting = false;
    return true;
}
