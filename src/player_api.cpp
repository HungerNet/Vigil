#include "player_api.h"
#include "denick.h"
#include "number_denicker.h"
#include "utils.h"
#include <deque>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <unordered_set>
#include <unordered_map>
#include <regex>
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <atomic>

namespace taboverlay {
namespace {

constexpr ULONGLONG kHypixelOkTtlMs = 24ULL * 60ULL * 60ULL * 1000ULL;
constexpr ULONGLONG kPrequeueAllowTtlMs = 180ULL * 1000ULL;
constexpr int kHypWorkerCount = 16;
constexpr int kSerWorkerCount = 8;
constexpr int kWsWorkerCount = 8;
constexpr ULONGLONG kHypWorkerSpacingMs = 0ULL;
constexpr ULONGLONG kSerWorkerSpacingMs = 0ULL;
constexpr ULONGLONG kWsWorkerSpacingMs = 0ULL;
constexpr ULONGLONG kSeraphTtlMs = 10ULL * 60ULL * 1000ULL;
constexpr ULONGLONG kWinstreakPingTtlMs = 10ULL * 60ULL * 1000ULL;
constexpr ULONGLONG kApiFailRetryMs = 45ULL * 1000ULL;
constexpr int kMaxQueue = 128;

bool BodyIndicatesInvalidApiKey(const std::string& body);

void ApiFailLog(const std::string&, const char*, ...) {}

bool ApiHttpGet(const char*, const std::string& url, std::string& body, DWORD& status,
                const std::string& extraHeader = std::string()) {
    return HttpGetHttps(url, body, status, extraHeader);
}

std::atomic<bool> g_hypKeyInvalidPending{false};
std::atomic<bool> g_serKeyInvalidPending{false};
std::atomic<bool> g_hypKeyInvalidNotified{false};
std::atomic<bool> g_serKeyInvalidNotified{false};

enum class JobKind { ByUuid, ByName };

struct Job {
    JobKind kind = JobKind::ByUuid;
    std::string uuid;
    std::string nameKey;
};

std::mutex g_mu;
std::condition_variable g_cv;
std::deque<Job> g_jobs;
std::unordered_set<std::string> g_queued;
std::unordered_map<std::string, CachedHypixelStats> g_hyp;
std::unordered_map<std::string, CachedSeraphTag> g_ser;
std::unordered_map<std::string, CachedWinstreakPing> g_wsPing;
std::unordered_set<std::string> g_allow;
std::unordered_map<std::string, ULONGLONG> g_prequeueAt;
std::unordered_map<std::string, std::string> g_prequeueDisplay;
std::unordered_map<std::string, int> g_prequeueColor;
std::unordered_set<std::string> g_prequeueNick;
std::unordered_map<std::string, ULONGLONG> g_nameFailAt;
std::unordered_map<std::string, std::string> g_nameToUuid;
std::unordered_map<std::string, int> g_seen;
std::string g_hypKey;
std::string g_serKey;
std::string g_wsKey;
bool g_vegaProxy = false;
std::vector<std::thread> g_hypWorkers;
bool g_hypWorkersOn = false;
bool g_workerStop = false;
std::deque<std::string> g_serJobs;
std::unordered_set<std::string> g_serQueued;
std::vector<std::thread> g_serWorkers;
bool g_serWorkersOn = false;
bool g_serWorkerStop = false;
std::condition_variable g_serCv;
std::deque<std::string> g_wsJobs;
std::unordered_set<std::string> g_wsQueued;
std::vector<std::thread> g_wsWorkers;
bool g_wsWorkersOn = false;
bool g_wsWorkerStop = false;
std::condition_variable g_wsCv;

std::string CanonKey(const std::string& ign) {
    return LowerAscii(Trim(StripMinecraftFormatting(ign)));
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (sz <= 0) return {};
    std::wstring out((size_t)sz - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], sz);
    return out;
}

std::string UrlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

std::string RemoveDashes(const std::string& uuid) {
    std::string out;
    out.reserve(uuid.size());
    for (char c : uuid) if (c != '-') out.push_back(c);
    return out;
}

bool NormalizeUuid32(const std::string& compact, std::string& out) {
    out.clear();
    std::string c = RemoveDashes(compact);
    if (c.size() != 32) return false;
    for (char ch : c) {
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')))
            return false;
    }
    for (size_t i = 0; i < c.size(); ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) out.push_back('-');
        out.push_back((char)std::tolower((unsigned char)c[i]));
    }
    return true;
}

bool BodyIndicatesInvalidApiKey(const std::string& body) {
    if (body.empty()) return false;
    if (body.find("\"success\":true") != std::string::npos ||
        body.find("\"success\": true") != std::string::npos) {
        return false;
    }
    const size_t playerPos = body.find("\"player\"");
    const std::string scope = (playerPos != std::string::npos)
                                  ? body.substr(0, playerPos)
                                  : body.substr(0, (std::min)(body.size(), size_t{512}));
    const std::string lower = LowerAscii(scope);
    return lower.find("invalid api key") != std::string::npos ||
           lower.find("invalid key") != std::string::npos ||
           lower.find("missing api key") != std::string::npos;
}

void MarkHypixelKeyInvalid() {
    g_hypKeyInvalidPending.store(true);
}

void MarkSeraphKeyInvalid() {
    g_serKeyInvalidPending.store(true);
}

std::string NormUuidCacheKey(const std::string& uuid) {
    if (uuid.empty()) return {};
    std::string canon;
    if (NormalizeUuid32(RemoveDashes(LowerAscii(uuid)), canon)) return canon;
    return LowerAscii(uuid);
}

void StartWorkerLocked();
void StartSeraphWorkerLocked();
void StartWinstreakWorkerLocked();
void CancelNameJobsLocked(const std::string& nameKey);

bool ExtractJsonInt(const std::string& json, const std::string& key, int& out) {
    std::regex rgx("\"" + key + "\"\\s*:\\s*(-?[0-9]+)");
    std::smatch m;
    if (!std::regex_search(json, m, rgx)) return false;
    out = atoi(m[1].str().c_str());
    return true;
}

bool ExtractJsonString(const std::string& json, const std::string& key, std::string& out) {
    std::regex rgx("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch m;
    if (!std::regex_search(json, m, rgx)) return false;
    out = m[1].str();
    return true;
}

bool ExtractJsonBool(const std::string& json, const std::string& key, bool& out) {
    std::regex rgx("\"" + key + "\"\\s*:\\s*(true|false)");
    std::smatch m;
    if (!std::regex_search(json, m, rgx)) return false;
    out = (m[1].str() == "true");
    return true;
}

bool ExtractJsonObject(const std::string& json, const std::string& key, std::string& out) {
    out.clear();
    const std::string needle = "\"" + key + "\"";
    size_t keyPos = json.find(needle);
    if (keyPos == std::string::npos) return false;
    size_t colonPos = json.find(':', keyPos + needle.size());
    if (colonPos == std::string::npos) return false;
    size_t start = json.find_first_not_of(" \t\r\n", colonPos + 1);
    if (start == std::string::npos || json[start] != '{') return false;
    bool inString = false, escape = false;
    int depth = 0;
    for (size_t i = start; i < json.size(); ++i) {
        char c = json[i];
        if (inString) {
            if (escape) { escape = false; continue; }
            if (c == '\\') { escape = true; continue; }
            if (c == '"') inString = false;
            continue;
        }
        if (c == '"') { inString = true; continue; }
        if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0) {
                out.assign(json.substr(start, i - start + 1));
                return true;
            }
        }
    }
    return false;
}

bool ParseSkywarsFromPlayer(const std::string& playerObj, CachedHypixelStats& out) {
    std::string statsObj, skywarsObj, achievementsObj;
    if (!ExtractJsonObject(playerObj, "stats", statsObj)) return false;
    if (!ExtractJsonObject(statsObj, "SkyWars", skywarsObj)) return false;
    ExtractJsonInt(skywarsObj, "kills", out.swKills);
    ExtractJsonInt(skywarsObj, "deaths", out.swDeaths);
    ExtractJsonInt(skywarsObj, "wins", out.swWins);
    ExtractJsonInt(skywarsObj, "losses", out.swLosses);
    int swExp = 0;
    ExtractJsonInt(skywarsObj, "skywars_experience", swExp);
    if (out.swDeaths <= 0) out.swDeaths = 1;
    if (out.swLosses <= 0) out.swLosses = 1;
    if (ExtractJsonObject(playerObj, "achievements", achievementsObj)) {
        ExtractJsonInt(achievementsObj, "skywars_you_re_a_star", out.swStars);
    }
    out.swLevel = SkywarsExpToLevel(swExp);

    std::string apiFormatted;
    if (ExtractJsonString(skywarsObj, "levelFormattedWithBrackets", apiFormatted) ||
        ExtractJsonString(playerObj, "levelFormattedWithBrackets", apiFormatted)) {
        apiFormatted = Trim(DecodeJsonStringEscapes(apiFormatted));
    }
    if (!apiFormatted.empty()) {
        out.swStarsFormatted = apiFormatted;
    } else {
        out.swStarsFormatted = FormatSkywarsStarsDisplay(out.swLevel, out.swStars);
    }
    out.hasSkywars = true;
    return true;
}

bool ParseDuelsFromPlayer(const std::string& playerObj, CachedHypixelStats& out) {
    std::string statsObj, duelsObj;
    if (!ExtractJsonObject(playerObj, "stats", statsObj)) return false;
    if (!ExtractJsonObject(statsObj, "Duels", duelsObj)) return false;
    ExtractJsonInt(duelsObj, "wins", out.duelWins);
    ExtractJsonInt(duelsObj, "losses", out.duelLosses);
    ExtractJsonInt(duelsObj, "kills", out.duelKills);
    ExtractJsonInt(duelsObj, "deaths", out.duelDeaths);
    ExtractJsonInt(duelsObj, "wins_bedwars", out.duelWinsBw);
    ExtractJsonInt(duelsObj, "losses_bedwars", out.duelLossesBw);
    if (out.duelLosses <= 0) out.duelLosses = 1;
    if (out.duelDeaths <= 0) out.duelDeaths = 1;
    if (out.duelLossesBw <= 0) out.duelLossesBw = 1;
    out.hasDuels = true;
    return true;
}

static bool ExtractFirstJsonArrayString(const std::string& json, const std::string& key, std::string& out) {
    out.clear();
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    size_t arr = json.find('[', pos + needle.size());
    if (arr == std::string::npos) return false;
    size_t q1 = json.find('"', arr + 1);
    if (q1 == std::string::npos) return false;
    size_t q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos || q2 <= q1 + 1) return false;
    out = json.substr(q1 + 1, q2 - q1 - 1);
    return !out.empty();
}

static bool MmIsRandomCosmetic(const std::string& v) {
    const std::string l = LowerAscii(Trim(v));
    return l.empty() || l == "none" || l.find("random_favorite") != std::string::npos ||
           l.find("random_cosmetic") != std::string::npos;
}

static std::string MmSchemeToColorName(std::string scheme) {
    scheme = LowerAscii(Trim(scheme));
    if (scheme.rfind("prefixscheme_", 0) == 0) scheme = scheme.substr(13);
    static const char* kColors[] = {
        "light_purple", "dark_purple", "dark_green", "dark_aqua", "dark_red", "dark_blue", "dark_gray", "dark_grey",
        "black", "white", "gold", "yellow", "green", "aqua", "blue", "red", "gray", "grey", "pink"
    };
    for (const char* c : kColors) {
        const std::string col = c;
        if (scheme.size() >= col.size() &&
            scheme.compare(scheme.size() - col.size(), col.size(), col) == 0) {
            if (scheme.size() == col.size() || scheme[scheme.size() - col.size() - 1] == '_') {
                return col == "grey" ? "gray" : (col == "dark_grey" ? "dark_gray" : (col == "pink" ? "light_purple" : col));
            }
        }
    }
    return scheme;
}

static void ResolveMmPrefixFields(const std::string& mmObj, CachedHypixelStats& out) {
    std::string emblemObj, favoritesObj;
    ExtractJsonObject(mmObj, "emblem", emblemObj);
    ExtractJsonObject(mmObj, "favorites", favoritesObj);

    std::string emblemIcon, emblemColor;
    ExtractJsonString(emblemObj, "selected_icon", emblemIcon);
    ExtractJsonString(emblemObj, "selected_color", emblemColor);

    std::string prefixIcon, prefixScheme, prefixStat;
    ExtractJsonString(mmObj, "active_prefixicon", prefixIcon);
    ExtractJsonString(mmObj, "active_prefixscheme", prefixScheme);
    ExtractJsonString(mmObj, "active_prefixstat", prefixStat);
    prefixIcon = Trim(DecodeJsonStringEscapes(prefixIcon));
    prefixScheme = Trim(DecodeJsonStringEscapes(prefixScheme));
    prefixStat = Trim(DecodeJsonStringEscapes(prefixStat));
    emblemIcon = Trim(DecodeJsonStringEscapes(emblemIcon));
    emblemColor = Trim(DecodeJsonStringEscapes(emblemColor));

    std::string icon = prefixIcon;
    if (MmIsRandomCosmetic(icon)) icon = emblemIcon;
    if (MmIsRandomCosmetic(icon)) ExtractFirstJsonArrayString(favoritesObj, "prefixicon", icon);
    if (MmIsRandomCosmetic(icon)) icon.clear();
    out.mmIcon = icon;

    std::string color = prefixScheme;
    if (MmIsRandomCosmetic(color)) color = emblemColor;
    if (MmIsRandomCosmetic(color)) {
        std::string favScheme;
        if (ExtractFirstJsonArrayString(favoritesObj, "prefixscheme", favScheme)) color = favScheme;
    }
    if (!MmIsRandomCosmetic(color)) out.mmEmblemColor = MmSchemeToColorName(color);
    else out.mmEmblemColor.clear();

    // Number in prefix comes ONLY from active_prefixstat (never overall kills/wins).
    if (MmIsRandomCosmetic(prefixStat)) {
        std::string favStat;
        if (ExtractFirstJsonArrayString(favoritesObj, "prefixstat", favStat)) prefixStat = favStat;
    }
    std::string statId = LowerAscii(prefixStat);
    if (statId.rfind("prefixstat_", 0) == 0) statId = statId.substr(11);

    out.mmPrefixOpen = '[';
    out.mmPrefixClose = ']';
    out.mmPrefixIsKills = false;
    out.mmHasPrefix = false;
    out.mmPrefixStat = 0;
    const char* apiKey = nullptr;

    if (statId == "classic_kills" || statId == "double_up_kills" || statId == "doubleup_kills") {
        out.mmPrefixOpen = '['; out.mmPrefixClose = ']';
        out.mmPrefixIsKills = true;
        apiKey = (statId.find("double") != std::string::npos) ? "kills_MURDER_DOUBLE_UP" : "kills_MURDER_CLASSIC";
    } else if (statId == "classic_wins" || statId == "double_up_wins" || statId == "doubleup_wins") {
        out.mmPrefixOpen = '['; out.mmPrefixClose = ']';
        out.mmPrefixIsKills = false;
        apiKey = (statId.find("double") != std::string::npos) ? "wins_MURDER_DOUBLE_UP" : "wins_MURDER_CLASSIC";
    } else if (statId == "assassins_kills" || statId == "assassin_kills") {
        out.mmPrefixOpen = '('; out.mmPrefixClose = ')';
        out.mmPrefixIsKills = true;
        apiKey = "kills_MURDER_ASSASSINS";
    } else if (statId == "assassins_wins" || statId == "assassin_wins") {
        out.mmPrefixOpen = '('; out.mmPrefixClose = ')';
        out.mmPrefixIsKills = false;
        apiKey = "wins_MURDER_ASSASSINS";
    } else if (statId == "infection_kills") {
        out.mmPrefixOpen = '{'; out.mmPrefixClose = '}';
        out.mmPrefixIsKills = true;
        apiKey = "kills_MURDER_INFECTION";
    } else if (statId == "infection_wins") {
        out.mmPrefixOpen = '{'; out.mmPrefixClose = '}';
        out.mmPrefixIsKills = false;
        apiKey = "wins_MURDER_INFECTION";
    } else if (!statId.empty() && !MmIsRandomCosmetic(prefixStat)) {
        out.mmPrefixIsKills = (statId.find("kills") != std::string::npos);
        if (statId.find("infection") != std::string::npos) {
            out.mmPrefixOpen = '{'; out.mmPrefixClose = '}';
            apiKey = out.mmPrefixIsKills ? "kills_MURDER_INFECTION" : "wins_MURDER_INFECTION";
        } else if (statId.find("assassin") != std::string::npos) {
            out.mmPrefixOpen = '('; out.mmPrefixClose = ')';
            apiKey = out.mmPrefixIsKills ? "kills_MURDER_ASSASSINS" : "wins_MURDER_ASSASSINS";
        } else {
            out.mmPrefixOpen = '['; out.mmPrefixClose = ']';
            apiKey = out.mmPrefixIsKills ? "kills_MURDER_CLASSIC" : "wins_MURDER_CLASSIC";
        }
    }

    if (apiKey) {
        ExtractJsonInt(mmObj, apiKey, out.mmPrefixStat);
        out.mmHasPrefix = true;
    } else if (!out.mmIcon.empty()) {
        out.mmHasPrefix = true;
        out.mmPrefixOpen = '[';
        out.mmPrefixClose = ']';
        out.mmPrefixStat = -1;
    }
}

bool ParseMurderMysteryFromPlayer(const std::string& playerObj, CachedHypixelStats& out) {
    std::string statsObj, mmObj;
    if (!ExtractJsonObject(playerObj, "stats", statsObj)) return false;
    if (!ExtractJsonObject(statsObj, "MurderMystery", mmObj)) return false;

    ExtractJsonInt(mmObj, "kills", out.mmKills);
    ExtractJsonInt(mmObj, "deaths", out.mmDeaths);
    ExtractJsonInt(mmObj, "wins", out.mmWins);

    int games = 0;
    if (!ExtractJsonInt(mmObj, "games", games)) {
        ExtractJsonInt(mmObj, "games_played_murder_mystery", games);
    }
    int losses = 0;
    if (!ExtractJsonInt(mmObj, "losses", losses)) {
        if (games > out.mmWins) losses = games - out.mmWins;
    }
    out.mmLosses = (losses > 0) ? losses : 1;
    if (out.mmDeaths <= 0) out.mmDeaths = 1;

    ResolveMmPrefixFields(mmObj, out);
    out.hasMurderMystery = true;
    return true;
}

bool ParseBedwarsFromPlayer(const std::string& playerObj, CachedHypixelStats& out) {
    std::string statsObj, bedwarsObj, achievementsObj;
    const bool hasStats = ExtractJsonObject(playerObj, "stats", statsObj);
    const bool hasBw = hasStats && ExtractJsonObject(statsObj, "Bedwars", bedwarsObj);
    const bool hasAch = ExtractJsonObject(playerObj, "achievements", achievementsObj);
    const std::string& stats = hasBw ? bedwarsObj : playerObj;
    const std::string& ach = hasAch ? achievementsObj : playerObj;
    const bool hasStars = ExtractJsonInt(ach, "bedwars_level", out.stars) ||
                          ExtractJsonInt(playerObj, "bedwars_level", out.stars);
    int gp = 0;
    ExtractJsonInt(stats, "games_played_bedwars", gp);
    if (!hasBw || (!hasStars && gp <= 0)) return false;
    ExtractJsonInt(stats, "Experience", out.bwExperience);
    ExtractJsonInt(stats, "wins_bedwars", out.wins);
    ExtractJsonInt(stats, "losses_bedwars", out.losses);
    ExtractJsonInt(stats, "final_kills_bedwars", out.finalKills);
    ExtractJsonInt(stats, "final_deaths_bedwars", out.finalDeaths);
    ExtractJsonInt(stats, "beds_broken_bedwars", out.bedsBroken);
    ExtractJsonInt(stats, "beds_lost_bedwars", out.bedsLost);
    ExtractJsonInt(stats, "kills_bedwars", out.kills);
    ExtractJsonInt(stats, "deaths_bedwars", out.deaths);
    ExtractJsonInt(stats, "entity_attack_final_kills_bedwars", out.entityAttackFinalKills);
    ExtractJsonInt(stats, "entity_attack_kills_bedwars", out.entityAttackKills);
    ExtractJsonInt(stats, "entity_attack_final_deaths_bedwars", out.entityAttackFinalDeaths);
    ExtractJsonInt(stats, "entity_attack_deaths_bedwars", out.entityAttackDeaths);
    ExtractJsonInt(stats, "magic_final_deaths_bedwars", out.magicFinalDeaths);
    ExtractJsonInt(stats, "games_played_bedwars", out.gamesPlayed);
    ExtractJsonInt(stats, "winstreak", out.winstreak);
    if (out.losses <= 0) out.losses = 1;
    if (out.finalDeaths <= 0) out.finalDeaths = 1;
    if (out.bedsLost <= 0) out.bedsLost = 1;
    if (out.deaths <= 0) out.deaths = 1;
    out.ok = true;
    out.fetchedAt = GetTickCount64();
    return true;
}

bool ParseHypixelBody(const std::string& body, CachedHypixelStats& out, std::string& outUuid, bool& noPlayer) {
    noPlayer = false;
    outUuid.clear();
    out = CachedHypixelStats{};
    bool apiOk = (body.find("\"success\":true") != std::string::npos);
    int sn = 0;
    if (ExtractJsonInt(body, "success", sn)) apiOk = (sn != 0);
    if (!apiOk) {
        if (BodyIndicatesInvalidApiKey(body)) MarkHypixelKeyInvalid();
        noPlayer = true;
        return false;
    }
    std::string playerObj;
    if (!ExtractJsonObject(body, "player", playerObj)) {
        noPlayer = true;
        return false;
    }
    std::string rawId;
    if (ExtractJsonString(playerObj, "uuid", rawId) || ExtractJsonString(playerObj, "_id", rawId)) {
        std::string canon;
        if (NormalizeUuid32(rawId, canon)) outUuid = canon;
    }
    ExtractJsonString(playerObj, "displayname", out.displayName);
    ExtractJsonString(playerObj, "newPackageRank", out.newPackageRank);
    ExtractJsonString(playerObj, "monthlyPackageRank", out.monthlyPackageRank);
    if (out.newPackageRank.empty()) out.newPackageRank = "NONE";
    if (out.monthlyPackageRank.empty()) out.monthlyPackageRank = "NONE";

    const bool hasBw = ParseBedwarsFromPlayer(playerObj, out);
    ParseSkywarsFromPlayer(playerObj, out);
    ParseDuelsFromPlayer(playerObj, out);
    ParseMurderMysteryFromPlayer(playerObj, out);
    out.fetchedAt = GetTickCount64();
    return hasBw || out.hasSkywars || out.hasDuels || out.hasMurderMystery;
}

bool FetchHypixelByUuid(const std::string& key, const std::string& uuid, CachedHypixelStats& out, std::string& outUuid) {
    if (uuid.empty()) return false;
    const std::string ku = NormUuidCacheKey(uuid);
    if (ku.empty()) return false;

    const bool useBordic = g_vegaProxy || key.empty();
    if (!useBordic && key.empty()) return false;

    std::string body;
    DWORD status = 0;
    const std::string url = useBordic
        ? ("https://api.bordic.xyz/v3/cache/hypixel?uuid=" + UrlEncode(ku))
        : ("https://api.hypixel.net/v2/player?key=" + UrlEncode(key) + "&uuid=" + UrlEncode(ku));
    const char* tag = useBordic ? "BordicHypixel.uuid" : "Hypixel.uuid";
    if (!ApiHttpGet(tag, url, body, status)) {
        ApiFailLog("hypixel:uuid:" + ku, "[API][FAIL] Hypixel uuid=%s network error\n", ku.c_str());
        return false;
    }
    if (!useBordic && (status == 401 || status == 403 || BodyIndicatesInvalidApiKey(body))) {
        MarkHypixelKeyInvalid();
        ApiFailLog("hypixel:uuid:" + ku,
                   "[API][FAIL] Hypixel uuid=%s invalid key status=%lu (see [API] trace above)\n",
                   ku.c_str(), (unsigned long)status);
        return false;
    }
    if (status != 200) {
        ApiFailLog("hypixel:uuid:" + ku, "[API][FAIL] Hypixel uuid=%s status=%lu\n",
                   ku.c_str(), (unsigned long)status);
        return false;
    }
    bool noPlayer = false;
    return ParseHypixelBody(body, out, outUuid, noPlayer) && !noPlayer;
}

bool ResolveMojangUuidByName(const std::string& name, std::string& outUuid32, bool& noPlayer) {
    noPlayer = false;
    outUuid32.clear();
    if (name.empty()) return false;
    std::string body;
    DWORD status = 0;
    const std::string url = "https://api.mojang.com/users/profiles/minecraft/" + UrlEncode(name);
    if (!ApiHttpGet("Mojang.name", url, body, status)) {
        ApiFailLog("mojang:name:" + LowerAscii(name),
                   "[API][FAIL] Mojang name=%s network error\n", name.c_str());
        return false;
    }
    if (status == 204 || status == 404) {
        noPlayer = true;
        return true;
    }
    if (status != 200) {
        ApiFailLog("mojang:name:" + LowerAscii(name),
                   "[API][FAIL] Mojang name=%s status=%lu\n",
                   name.c_str(), (unsigned long)status);
        return false;
    }
    std::string id;
    if (!ExtractJsonString(body, "id", id) || id.empty()) {
        noPlayer = true;
        return true;
    }
    if (!NormalizeUuid32(RemoveDashes(LowerAscii(id)), outUuid32)) {
        outUuid32 = RemoveDashes(LowerAscii(id));
    }
    return !outUuid32.empty();
}

bool FetchHypixelByName(const std::string& key, const std::string& name, CachedHypixelStats& out,
                        std::string& outUuid, bool& noPlayer) {
    noPlayer = false;
    outUuid.clear();
    if (name.empty()) return false;

    const bool useBordic = g_vegaProxy || key.empty();
    if (!useBordic && key.empty()) return false;

    if (useBordic) {
        std::string uuid32;
        if (!ResolveMojangUuidByName(name, uuid32, noPlayer)) return false;
        if (noPlayer) return true;
        std::string parsedUuid;
        const bool ok = FetchHypixelByUuid(key, uuid32, out, parsedUuid);
        outUuid = !parsedUuid.empty() ? parsedUuid : uuid32;
        if (!ok) {
            noPlayer = true;
            return true;
        }
        return true;
    }

    std::string body;
    DWORD status = 0;
    const std::string url = "https://api.hypixel.net/v2/player?key=" + UrlEncode(key) +
                            "&name=" + UrlEncode(name);
    if (!ApiHttpGet("Hypixel.name", url, body, status)) {
        ApiFailLog("hypixel:name:" + LowerAscii(name),
                   "[API][FAIL] Hypixel name=%s network error\n", name.c_str());
        return false;
    }
    if (status == 401 || status == 403 || BodyIndicatesInvalidApiKey(body)) {
        MarkHypixelKeyInvalid();
        ApiFailLog("hypixel:name:" + LowerAscii(name),
                   "[API][FAIL] Hypixel name=%s invalid key status=%lu (see [API] trace above)\n",
                   name.c_str(), (unsigned long)status);
        return false;
    }
    if (status != 200) {
        ApiFailLog("hypixel:name:" + LowerAscii(name),
                   "[API][FAIL] Hypixel name=%s status=%lu\n",
                   name.c_str(), (unsigned long)status);
        return false;
    }
    return ParseHypixelBody(body, out, outUuid, noPlayer);
}

bool FetchWinstreakPing(const std::string& uuid, CachedWinstreakPing& out) {
    if (uuid.empty()) return false;
    const std::string playerUuid = NormUuidCacheKey(uuid);
    if (playerUuid.empty()) return false;
    std::string body;
    DWORD status = 0;
    const std::string url = "https://api.bordic.xyz/v3/player/ping?uuid=" + UrlEncode(playerUuid);
    if (!ApiHttpGet("BordicPing", url, body, status)) {
        ApiFailLog("bordicping:" + playerUuid,
                   "[API][FAIL] BordicPing player=%s network error\n", playerUuid.c_str());
        return false;
    }
    if (status != 200) {
        ApiFailLog("bordicping:" + playerUuid,
                   "[API][FAIL] BordicPing player=%s status=%lu\n",
                   playerUuid.c_str(), (unsigned long)status);
        return false;
    }

    bool success = false;
    if (!ExtractJsonBool(body, "success", success) || !success) {
        out = CachedWinstreakPing{};
        out.resolved = true;
        out.averagePing = -1;
        out.pingVar = -1;
        out.fetchedAt = GetTickCount64();
        return true;
    }

    // Collect every "avg" sample from data[] (newest first in API response).
    std::vector<int> avgs;
    {
        const std::regex avgRgx("\"avg\"\\s*:\\s*(-?[0-9]+)");
        const auto begin = std::sregex_iterator(body.begin(), body.end(), avgRgx);
        const auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            avgs.push_back(atoi((*it)[1].str().c_str()));
        }
    }

    out = CachedWinstreakPing{};
    out.resolved = true;
    out.fetchedAt = GetTickCount64();
    if (avgs.empty()) {
        out.averagePing = -1;
        out.pingVar = -1;
        return true;
    }

    out.averagePing = avgs.front();
    int mn = avgs.front();
    int mx = avgs.front();
    for (int v : avgs) {
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    out.pingVar = mx - mn;
    return true;
}

bool FetchSeraph(const std::string& key, const std::string& uuid, CachedSeraphTag& out) {
    if (key.empty() || uuid.empty()) return false;
    const std::string pathUuid = NormUuidCacheKey(uuid);
    if (pathUuid.empty()) return false;
    std::string body;
    DWORD status = 0;
    // Seraph v2 path: /{uuid}/blacklist?key=...
    const std::string url = "https://api.seraph.si/" + pathUuid + "/blacklist?key=" + UrlEncode(key);
    const std::string hdr = "seraph-api-key: " + key;
    if (!ApiHttpGet("Seraph", url, body, status, hdr)) {
        ApiFailLog("seraph:" + pathUuid,
                   "[API][FAIL] Seraph uuid=%s network error\n", pathUuid.c_str());
        return false;
    }
    if (status == 401 || status == 403 || BodyIndicatesInvalidApiKey(body)) {
        MarkSeraphKeyInvalid();
        ApiFailLog("seraph:" + pathUuid,
                   "[API][FAIL] Seraph uuid=%s invalid key status=%lu (see [API] trace above)\n",
                   pathUuid.c_str(), (unsigned long)status);
        return false;
    }
    if (status != 200) {
        ApiFailLog("seraph:" + pathUuid,
                   "[API][FAIL] Seraph uuid=%s status=%lu\n",
                   pathUuid.c_str(), (unsigned long)status);
        return false;
    }
    bool success = false;
    if (!ExtractJsonBool(body, "success", success) || !success) {
        if (BodyIndicatesInvalidApiKey(body)) MarkSeraphKeyInvalid();
        ApiFailLog("seraph:" + pathUuid,
                   "[API][FAIL] Seraph uuid=%s success=false\n", pathUuid.c_str());
        return false;
    }
    std::string dataObj, blObj;
    if (!ExtractJsonObject(body, "data", dataObj)) return false;
    if (!ExtractJsonObject(dataObj, "blacklist", blObj)) return false;
    bool tagged = false;
    if (!ExtractJsonBool(blObj, "tagged", tagged)) return false;
    if (!tagged) {
        out = CachedSeraphTag{};
        out.resolved = true;
        out.blacklisted = false;
        out.fetchedAt = GetTickCount64();
        return true;
    }
    std::string reportType, reason, tooltip;
    ExtractJsonString(blObj, "report_type", reportType);
    ExtractJsonString(blObj, "reason", reason);
    ExtractJsonString(blObj, "tooltip", tooltip);
    ExtractJsonBool(blObj, "verified", out.verified);
    if (reportType.empty() && reason.empty() && tooltip.empty()) return false;
    out.resolved = true;
    out.blacklisted = true;
    out.reportType = reportType;
    out.reason = reason;
    out.tooltip = tooltip;
    out.tag = reportType.empty() ? (reason.empty() ? tooltip : reason) : reportType;
    out.fetchedAt = GetTickCount64();
    std::regex rTs("\"timestamp\"\\s*:\\s*(-?[0-9]+)");
    std::smatch mTs;
    if (std::regex_search(blObj, mTs, rTs)) {
        long long ts = _strtoi64(mTs[1].str().c_str(), nullptr, 10);
        if (ts > 0) {
            long long tsMs = (ts < 100000000000LL) ? (ts * 1000LL) : ts;
            const long long nowMs = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            if (nowMs > tsMs) out.daysAgo = (int)((nowMs - tsMs) / (24LL * 60LL * 60LL * 1000LL));
        }
    }
    return true;
}

bool NeedsHypixelUuid(const std::string& uuid, ULONGLONG now) {
    auto it = g_hyp.find(uuid);
    if (it == g_hyp.end()) return true;
    ULONGLONG age = now - it->second.fetchedAt;
    if (it->second.ok) return age > kHypixelOkTtlMs;
    return age > kApiFailRetryMs;
}

bool NeedsSeraph(const std::string& uuid, ULONGLONG now) {
    auto it = g_ser.find(uuid);
    if (it == g_ser.end()) return true;
    const ULONGLONG age = now - it->second.fetchedAt;
    if (it->second.resolved) return age > kSeraphTtlMs;
    return age > kApiFailRetryMs;
}

bool NeedsWinstreakPing(const std::string& uuid, ULONGLONG now) {
    auto it = g_wsPing.find(uuid);
    if (it == g_wsPing.end()) return true;
    const ULONGLONG age = now - it->second.fetchedAt;
    if (it->second.resolved) return age > kWinstreakPingTtlMs;
    return age > kApiFailRetryMs;
}

bool NeedsPrequeueName(const std::string& nameKey, ULONGLONG now) {
    if (g_prequeueNick.find(nameKey) != g_prequeueNick.end()) return false;
    auto fail = g_nameFailAt.find(nameKey);
    if (fail != g_nameFailAt.end() && now - fail->second < kApiFailRetryMs) return false;
    auto u = g_nameToUuid.find(nameKey);
    if (u != g_nameToUuid.end() && !u->second.empty()) {
        return NeedsHypixelUuid(u->second, now) || NeedsSeraph(u->second, now) ||
               NeedsWinstreakPing(u->second, now);
    }
    return true;
}

std::string QueueKey(const Job& j) {
    return (j.kind == JobKind::ByUuid) ? ("u:" + j.uuid) : ("n:" + j.nameKey);
}

void CancelNameJobsLocked(const std::string& nameKey) {
    if (nameKey.empty()) return;
    const std::string qk = "n:" + nameKey;
    g_queued.erase(qk);
    for (auto it = g_jobs.begin(); it != g_jobs.end();) {
        if (it->kind == JobKind::ByName && it->nameKey == nameKey) it = g_jobs.erase(it);
        else ++it;
    }
}

void PushJobLocked(Job job) {
    if ((int)(g_queued.size() + g_jobs.size()) >= kMaxQueue) return;
    const std::string qk = QueueKey(job);
    if (!g_queued.insert(qk).second) return;
    g_jobs.push_back(std::move(job));
    g_cv.notify_one();
}

void QueueSeraphUuidLocked(const std::string& ku) {
    if (ku.empty() || g_serKey.empty()) return;
    if (!NeedsSeraph(ku, GetTickCount64())) return;
    if ((int)(g_serQueued.size() + g_serJobs.size()) >= kMaxQueue) return;
    if (!g_serQueued.insert(ku).second) return;
    g_serJobs.push_back(ku);
    g_serCv.notify_one();
}

void QueueWinstreakUuidLocked(const std::string& ku) {
    if (ku.empty()) return;
    if (!NeedsWinstreakPing(ku, GetTickCount64())) return;
    if ((int)(g_wsQueued.size() + g_wsJobs.size()) >= kMaxQueue) return;
    if (!g_wsQueued.insert(ku).second) return;
    g_wsJobs.push_back(ku);
    g_wsCv.notify_one();
}

void QueueUuidLocked(const std::string& uuid) {
    if (uuid.empty()) return;
    const std::string ku = NormUuidCacheKey(uuid);
    if (ku.empty()) return;
    const ULONGLONG now = GetTickCount64();
    const bool needHyp = NeedsHypixelUuid(ku, now);
    const bool needSer = !g_serKey.empty() && NeedsSeraph(ku, now);
    const bool needWs = NeedsWinstreakPing(ku, now);
    if (!needHyp && !needSer && !needWs) return;
    // Hypixel job also fetches Seraph in parallel when both are needed.
    if (needHyp) {
        Job j;
        j.kind = JobKind::ByUuid;
        j.uuid = ku;
        PushJobLocked(std::move(j));
        StartWorkerLocked();
    } else if (needSer) {
        QueueSeraphUuidLocked(ku);
        StartSeraphWorkerLocked();
    }
    if (needWs) {
        QueueWinstreakUuidLocked(ku);
        StartWinstreakWorkerLocked();
    }
}

void QueuePrequeueNameLocked(const std::string& nameKey) {
    if (nameKey.empty()) return;
    auto u = g_nameToUuid.find(nameKey);
    if (u != g_nameToUuid.end() && !u->second.empty()) {
        QueueUuidLocked(u->second);
        return;
    }
    if (!NeedsPrequeueName(nameKey, GetTickCount64())) return;
    g_prequeueNick.erase(nameKey);
    Job j;
    j.kind = JobKind::ByName;
    j.nameKey = nameKey;
    PushJobLocked(std::move(j));
    StartWorkerLocked();
}

void RunSeraphIfNeeded(const std::string& uuid, const std::string& serKey) {
    if (uuid.empty() || serKey.empty() || !NeedsSeraph(uuid, GetTickCount64())) return;
    CachedSeraphTag s{};
    if (!FetchSeraph(serKey, uuid, s)) {
        s.fetchedAt = GetTickCount64();
        s.resolved = false;
    }
    std::lock_guard<std::mutex> lk(g_mu);
    g_ser[uuid] = s;
}

void RunWinstreakIfNeeded(const std::string& uuid) {
    if (uuid.empty() || !NeedsWinstreakPing(uuid, GetTickCount64())) return;
    CachedWinstreakPing p{};
    if (!FetchWinstreakPing(uuid, p)) {
        p.fetchedAt = GetTickCount64();
        p.resolved = false;
    }
    std::lock_guard<std::mutex> lk(g_mu);
    g_wsPing[uuid] = p;
}

void WorkerLoop() {
    for (;;) {
        Job job;
        std::string hypKey, serKey;
        {
            std::unique_lock<std::mutex> lk(g_mu);
            g_cv.wait(lk, [] { return g_workerStop || !g_jobs.empty(); });
            if (g_workerStop) return;
            job = g_jobs.front();
            g_jobs.pop_front();
            hypKey = g_hypKey;
            serKey = g_serKey;
        }

        if (job.kind == JobKind::ByName) {
            std::string apiName;
            std::string cachedUuid;
            {
                std::lock_guard<std::mutex> lk(g_mu);
                auto dn = g_prequeueDisplay.find(job.nameKey);
                apiName = (dn != g_prequeueDisplay.end() && !dn->second.empty()) ? dn->second : job.nameKey;
                auto u = g_nameToUuid.find(job.nameKey);
                if (u != g_nameToUuid.end()) cachedUuid = u->second;
            }
            apiName = Trim(StripMinecraftFormatting(apiName));

            CachedHypixelStats h{};
            std::string uuid;
            bool noPlayer = false;
            CachedSeraphTag ser{};
            bool serFetched = false;

            const std::string serUuidKey = NormUuidCacheKey(cachedUuid);
            std::thread serThread;
            if (!serKey.empty() && !serUuidKey.empty()) {
                serThread = std::thread([&]() {
                    serFetched = FetchSeraph(serKey, serUuidKey, ser);
                    if (!serFetched) {
                        ser.fetchedAt = GetTickCount64();
                        ser.resolved = false;
                        serFetched = true;
                    }
                });
            }

            const bool ok = FetchHypixelByName(hypKey, apiName, h, uuid, noPlayer);
            if (serThread.joinable()) serThread.join();

            const std::string storeUuid = NormUuidCacheKey(!uuid.empty() ? uuid : cachedUuid);
            {
                std::lock_guard<std::mutex> lk(g_mu);
                if (noPlayer) {
                    g_prequeueNick.insert(job.nameKey);
                    g_nameFailAt.erase(job.nameKey);
                } else if (!ok) {
                    g_nameFailAt[job.nameKey] = GetTickCount64();
                } else {
                    g_prequeueNick.erase(job.nameKey);
                    g_nameFailAt.erase(job.nameKey);
                    if (!uuid.empty()) {
                        const std::string mappedUuid = NormUuidCacheKey(uuid);
                        g_nameToUuid[job.nameKey] = mappedUuid.empty() ? uuid : mappedUuid;
                        g_hyp[storeUuid] = h;
                        g_seen[job.nameKey]++;
                    }
                }
                if (serFetched && !storeUuid.empty()) {
                    g_ser[storeUuid] = ser;
                    g_serQueued.erase(storeUuid);
                }
            }
            if (ok && !uuid.empty()) {
                std::lock_guard<std::mutex> lk(g_mu);
                QueueWinstreakUuidLocked(uuid);
                StartWinstreakWorkerLocked();
                if (!serKey.empty() && !serFetched) {
                    QueueSeraphUuidLocked(uuid);
                    StartSeraphWorkerLocked();
                }
            }
        } else if (job.kind == JobKind::ByUuid) {
            const std::string storeUuid = NormUuidCacheKey(job.uuid);
            CachedHypixelStats h{};
            std::string uuidOut;
            CachedSeraphTag ser{};
            bool serFetched = false;

            std::thread serThread;
            if (!serKey.empty() && !storeUuid.empty()) {
                serThread = std::thread([&]() {
                    serFetched = FetchSeraph(serKey, storeUuid, ser);
                    if (!serFetched) {
                        ser.fetchedAt = GetTickCount64();
                        ser.resolved = false;
                        serFetched = true;
                    }
                });
            }

            {
                // Official Hypixel when key set & !vegaProxy, else Bordic cache (keyless).
                const bool fetched = FetchHypixelByUuid(hypKey, job.uuid, h, uuidOut);
                const std::string hypStore = NormUuidCacheKey(!uuidOut.empty() ? uuidOut : job.uuid);
                std::lock_guard<std::mutex> lk(g_mu);
                if (fetched) {
                    g_hyp[hypStore] = h;
                } else {
                    auto prev = g_hyp.find(hypStore);
                    const bool keepPrev = prev != g_hyp.end() &&
                        (prev->second.ok || prev->second.hasSkywars || prev->second.hasDuels || prev->second.hasMurderMystery);
                    if (!keepPrev) {
                        h.ok = false;
                        h.fetchedAt = GetTickCount64();
                        g_hyp[hypStore] = h;
                    }
                }
            }

            if (serThread.joinable()) serThread.join();
            if (serFetched && !storeUuid.empty()) {
                std::lock_guard<std::mutex> lk(g_mu);
                g_ser[storeUuid] = ser;
                g_serQueued.erase(storeUuid);
            }
        }

        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_queued.erase(QueueKey(job));
        }
        bool moreHyp = false;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            moreHyp = !g_jobs.empty();
        }
        if (!moreHyp) Sleep((DWORD)kHypWorkerSpacingMs);
    }
}

void WinstreakWorkerLoop() {
    for (;;) {
        std::string uuid;
        {
            std::unique_lock<std::mutex> lk(g_mu);
            g_wsCv.wait(lk, [] { return g_wsWorkerStop || !g_wsJobs.empty(); });
            if (g_wsWorkerStop) return;
            uuid = g_wsJobs.front();
            g_wsJobs.pop_front();
        }
        RunWinstreakIfNeeded(uuid);
        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_wsQueued.erase(uuid);
        }
        bool moreWs = false;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            moreWs = !g_wsJobs.empty();
        }
        if (!moreWs) Sleep((DWORD)kWsWorkerSpacingMs);
    }
}

void SeraphWorkerLoop() {
    for (;;) {
        std::string uuid;
        std::string serKey;
        {
            std::unique_lock<std::mutex> lk(g_mu);
            g_serCv.wait(lk, [] { return g_serWorkerStop || !g_serJobs.empty(); });
            if (g_serWorkerStop) return;
            uuid = g_serJobs.front();
            g_serJobs.pop_front();
            serKey = g_serKey;
        }
        RunSeraphIfNeeded(uuid, serKey);
        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_serQueued.erase(uuid);
        }
        bool moreSer = false;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            moreSer = !g_serJobs.empty();
        }
        if (!moreSer) Sleep((DWORD)kSerWorkerSpacingMs);
    }
}

void StartWinstreakWorkerLocked() {
    if (g_wsWorkersOn) return;
    g_wsWorkerStop = false;
    g_wsWorkers.clear();
    g_wsWorkers.reserve((size_t)kWsWorkerCount);
    for (int i = 0; i < kWsWorkerCount; ++i) {
        g_wsWorkers.emplace_back(WinstreakWorkerLoop);
    }
    g_wsWorkersOn = true;
    g_wsCv.notify_all();
}

void StartSeraphWorkerLocked() {
    if (g_serWorkersOn || g_serKey.empty()) return;
    g_serWorkerStop = false;
    g_serWorkers.clear();
    g_serWorkers.reserve((size_t)kSerWorkerCount);
    for (int i = 0; i < kSerWorkerCount; ++i) {
        g_serWorkers.emplace_back(SeraphWorkerLoop);
    }
    g_serWorkersOn = true;
    g_serCv.notify_all();
}

void StartWorkerLocked() {
    if (g_hypWorkersOn) return;
    g_workerStop = false;
    g_hypWorkers.clear();
    g_hypWorkers.reserve((size_t)kHypWorkerCount);
    for (int i = 0; i < kHypWorkerCount; ++i) {
        g_hypWorkers.emplace_back(WorkerLoop);
    }
    g_hypWorkersOn = true;
    g_cv.notify_all();
}

void StopWinstreakWorker() {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (!g_wsWorkersOn) return;
        g_wsWorkerStop = true;
        g_wsJobs.clear();
        g_wsQueued.clear();
    }
    g_wsCv.notify_all();
    for (auto& t : g_wsWorkers) {
        if (t.joinable()) t.join();
    }
    std::lock_guard<std::mutex> lk(g_mu);
    g_wsWorkers.clear();
    g_wsWorkersOn = false;
}

void StopSeraphWorker() {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (!g_serWorkersOn) return;
        g_serWorkerStop = true;
        g_serJobs.clear();
        g_serQueued.clear();
    }
    g_serCv.notify_all();
    for (auto& t : g_serWorkers) {
        if (t.joinable()) t.join();
    }
    std::lock_guard<std::mutex> lk(g_mu);
    g_serWorkers.clear();
    g_serWorkersOn = false;
}

void StopWorker() {
    StopWinstreakWorker();
    StopSeraphWorker();
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (!g_hypWorkersOn) return;
        g_workerStop = true;
        g_jobs.clear();
        g_queued.clear();
    }
    g_cv.notify_all();
    for (auto& t : g_hypWorkers) {
        if (t.joinable()) t.join();
    }
    std::lock_guard<std::mutex> lk(g_mu);
    g_hypWorkers.clear();
    g_hypWorkersOn = false;
}

} // namespace

void PlayerApi::SetKeys(const std::string& hypixelKey, const std::string& seraphKey,
                        const std::string& winstreakKey, bool vegaProxy) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_hypKey == hypixelKey && g_serKey == seraphKey && g_wsKey == winstreakKey &&
        g_vegaProxy == vegaProxy) return;
    if (g_hypKey != hypixelKey) {
        g_hypKeyInvalidPending.store(false);
        g_hypKeyInvalidNotified.store(false);
    }
    if (g_serKey != seraphKey) {
        g_serKeyInvalidPending.store(false);
        g_serKeyInvalidNotified.store(false);
    }
    g_hypKey = hypixelKey;
    g_serKey = seraphKey;
    g_wsKey = winstreakKey;
    g_vegaProxy = vegaProxy;
    DenickSetApiKeys(hypixelKey, seraphKey);
    StartWorkerLocked();
    if (!seraphKey.empty()) StartSeraphWorkerLocked();
    StartWinstreakWorkerLocked();
}

void PlayerApi::EnsureWorker() {
    std::lock_guard<std::mutex> lk(g_mu);
    StartWorkerLocked();
    if (!g_serKey.empty()) StartSeraphWorkerLocked();
    StartWinstreakWorkerLocked();
}

void PlayerApi::Shutdown() {
    StopWorker();
    DenickShutdown();
    NumberDenickerShutdown();
}

void PlayerApi::ClearAllowlist() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_allow.clear();
    g_prequeueAt.clear();
    g_prequeueDisplay.clear();
    g_prequeueColor.clear();
    g_prequeueNick.clear();
    g_nameFailAt.clear();
    g_nameToUuid.clear();
}

static void ApplyWhoPlayersLocked(const std::vector<std::string>& players, bool replace) {
    const ULONGLONG now = GetTickCount64();
    if (replace) {
        std::unordered_set<std::string> kept;
        for (const auto& pr : g_prequeueAt) {
            if (now - pr.second <= kPrequeueAllowTtlMs) kept.insert(pr.first);
        }
        g_allow = kept;
        g_queued.clear();
        g_jobs.clear();
        g_serQueued.clear();
        g_serJobs.clear();
        g_wsQueued.clear();
        g_wsJobs.clear();
    }
    for (const auto& p : players) {
        const std::string k = CanonKey(p);
        if (k.empty()) continue;
        const bool inserted = g_allow.insert(k).second;
        if (!inserted && !replace) continue;
        auto u = g_nameToUuid.find(k);
        if (u != g_nameToUuid.end() && !u->second.empty()) {
            QueueUuidLocked(u->second);
        }
        // UUID lookups are queued from tab scan (OnTabPlayer) once profiles are visible.
    }
    StartWorkerLocked();
    if (!g_serKey.empty()) StartSeraphWorkerLocked();
    StartWinstreakWorkerLocked();
    g_cv.notify_all();
    g_serCv.notify_all();
    g_wsCv.notify_all();
}

void PlayerApi::SetWhoAllowlist(const std::vector<std::string>& players) {
    std::lock_guard<std::mutex> lk(g_mu);
    ApplyWhoPlayersLocked(players, true);
}

void PlayerApi::MergeWhoAllowlist(const std::vector<std::string>& players) {
    std::lock_guard<std::mutex> lk(g_mu);
    ApplyWhoPlayersLocked(players, false);
}

void PlayerApi::ResetWhoSession() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_allow.clear();
    g_queued.clear();
    g_jobs.clear();
    g_serQueued.clear();
    g_serJobs.clear();
}

void PlayerApi::ClearPrequeueGhosts() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_prequeueAt.clear();
    g_prequeueDisplay.clear();
    g_prequeueColor.clear();
    g_prequeueNick.clear();
    g_nameFailAt.clear();
}

void PlayerApi::AddPrequeueSpeaker(const std::string& player, int chatNameColor) {
    const std::string k = CanonKey(player);
    if (k.empty()) return;
    std::lock_guard<std::mutex> lk(g_mu);
    g_allow.insert(k);
    g_prequeueAt[k] = GetTickCount64();
    g_prequeueDisplay[k] = player;
    if (chatNameColor >= 0) g_prequeueColor[k] = chatNameColor;
    QueuePrequeueNameLocked(k);
    if (!g_hypKey.empty()) StartWorkerLocked();
}

void PlayerApi::SetSpeakerUuid(const std::string& player, const std::string& uuid) {
    (void)player;
    (void)uuid;
}

bool PlayerApi::IsAllowed(const std::string& ign) {
    const std::string k = CanonKey(ign);
    if (k.empty()) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    return g_allow.find(k) != g_allow.end();
}

void PlayerApi::AllowPlayerForApi(const std::string& ign) {
    const std::string k = CanonKey(ign);
    if (k.empty()) return;
    std::lock_guard<std::mutex> lk(g_mu);
    g_allow.insert(k);
    QueuePrequeueNameLocked(k);
    StartWorkerLocked();
    if (!g_serKey.empty()) StartSeraphWorkerLocked();
    StartWinstreakWorkerLocked();
    g_cv.notify_all();
    g_serCv.notify_all();
    g_wsCv.notify_all();
}

void PlayerApi::OnTabPlayer(const std::string& ign, const std::string& uuid) {
    if (uuid.size() < 15) return;
    char ver = (char)std::tolower((unsigned char)uuid[14]);
    int v = (ver >= '0' && ver <= '9') ? (ver - '0') : 0;
    if (v != 4) return;
    const std::string k = CanonKey(ign);
    const std::string ku = NormUuidCacheKey(uuid);
    if (k.empty() || ku.empty()) return;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_allow.find(k) == g_allow.end()) return;
    g_nameToUuid[k] = ku;
    CancelNameJobsLocked(k);
    const ULONGLONG now = GetTickCount64();
    if (!NeedsHypixelUuid(ku, now) && !NeedsSeraph(ku, now) && !NeedsWinstreakPing(ku, now)) return;
    QueueUuidLocked(ku);
}

bool PlayerApi::GetHypixel(const std::string& uuid, CachedHypixelStats& out) {
    const std::string ku = NormUuidCacheKey(uuid);
    if (ku.empty()) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_hyp.find(ku);
    if (it == g_hyp.end()) return false;
    out = it->second;
    return true;
}

bool PlayerApi::GetSeraph(const std::string& uuid, CachedSeraphTag& out) {
    const std::string ku = NormUuidCacheKey(uuid);
    if (ku.empty()) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_ser.find(ku);
    if (it == g_ser.end()) return false;
    out = it->second;
    return true;
}

bool PlayerApi::GetWinstreakPing(const std::string& uuid, CachedWinstreakPing& out) {
    const std::string ku = NormUuidCacheKey(uuid);
    if (ku.empty()) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_wsPing.find(ku);
    if (it == g_wsPing.end()) return false;
    out = it->second;
    return true;
}

bool PlayerApi::IsPrequeueNick(const std::string& ign) {
    const std::string k = CanonKey(ign);
    if (k.empty()) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    return g_prequeueNick.find(k) != g_prequeueNick.end();
}

std::string PlayerApi::GetCachedUuidForName(const std::string& ign) {
    const std::string k = CanonKey(ign);
    if (k.empty()) return {};
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_nameToUuid.find(k);
    return (it != g_nameToUuid.end()) ? it->second : std::string();
}

int PlayerApi::AllowlistSize() {
    std::lock_guard<std::mutex> lk(g_mu);
    return (int)g_allow.size();
}

std::vector<std::string> PlayerApi::GetAllowlistNames() {
    std::lock_guard<std::mutex> lk(g_mu);
    std::vector<std::string> out;
    out.reserve(g_allow.size());
    for (const auto& k : g_allow) out.push_back(k);
    std::sort(out.begin(), out.end());
    return out;
}

int PlayerApi::PendingCount() {
    std::lock_guard<std::mutex> lk(g_mu);
    return (int)(g_queued.size() + g_jobs.size() + g_serQueued.size() + g_serJobs.size() +
                 g_wsQueued.size() + g_wsJobs.size());
}

int PlayerApi::GetSeenCount(const std::string& ign) {
    const std::string k = CanonKey(ign);
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_seen.find(k);
    return (it != g_seen.end()) ? it->second : 0;
}

void PlayerApi::CollectGhosts(ULONGLONG nowMs,
                              const std::unordered_set<std::string>& namesInTabLower,
                              std::vector<PrequeueGhost>& out) {
    out.clear();
    std::lock_guard<std::mutex> lk(g_mu);
    const ULONGLONG speakerTtl = 30ULL * 1000ULL;
    const ULONGLONG keepTtl = 60ULL * 1000ULL;
    for (auto it = g_prequeueAt.begin(); it != g_prequeueAt.end(); ) {
        ULONGLONG age = nowMs - it->second;
        if (age > keepTtl) {
            g_prequeueDisplay.erase(it->first);
            g_prequeueColor.erase(it->first);
            g_prequeueNick.erase(it->first);
            it = g_prequeueAt.erase(it);
            continue;
        }
        if (age <= speakerTtl && namesInTabLower.find(it->first) == namesInTabLower.end()) {
            PrequeueGhost g;
            g.nameKey = it->first;
            auto dn = g_prequeueDisplay.find(it->first);
            g.displayName = (dn != g_prequeueDisplay.end() && !dn->second.empty()) ? dn->second : it->first;
            auto c = g_prequeueColor.find(it->first);
            g.nameColor = (c != g_prequeueColor.end()) ? c->second : 0xFFFFFF;
            auto u = g_nameToUuid.find(it->first);
            if (u != g_nameToUuid.end()) g.uuidHint = u->second;
            g.manual = false;
            out.push_back(std::move(g));
        }
        ++it;
    }
}

bool PlayerApi::FetchStatsNow(const std::string& playerName,
                              const std::string& hypixelKey,
                              const std::string& seraphKey,
                              const std::string& lookupMode,
                              PlayerLookupStats& out) {
    out = PlayerLookupStats{};
    out.playerName = playerName;
    out.lookupMode = lookupMode.empty() ? "bedwars" : LowerAscii(lookupMode);
    const std::string clean = Trim(StripMinecraftFormatting(playerName));
    if (clean.empty()) return false;
    std::string cachedUuid;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        const auto it = g_nameToUuid.find(CanonKey(clean));
        if (it != g_nameToUuid.end()) cachedUuid = it->second;
    }

    CachedHypixelStats hs{};
    std::string uuid;
    bool noPlayer = false;
    CachedSeraphTag st{};
    bool serFetched = false;

    std::thread serThread;
    if (!seraphKey.empty() && !cachedUuid.empty()) {
        const std::string serUuid = NormUuidCacheKey(cachedUuid);
        serThread = std::thread([&, serUuid]() {
            serFetched = FetchSeraph(seraphKey, serUuid, st);
        });
    }

    const bool hypOk = FetchHypixelByName(hypixelKey, clean, hs, uuid, noPlayer) && !noPlayer;
    if (serThread.joinable()) {
        serThread.join();
    } else if (hypOk && !uuid.empty() && !seraphKey.empty()) {
        serFetched = FetchSeraph(seraphKey, uuid, st);
    }

    if (!hypOk || noPlayer) return false;
    out.resolved = true;
    out.uuid = uuid;
    out.hypixel = hs;
    out.displayName = hs.displayName.empty() ? clean : hs.displayName;
    out.rankColor = HypixelRankColorCode(hs.newPackageRank, hs.monthlyPackageRank);
    if (hs.ok) {
        out.starCount = hs.stars;
        out.fkdrValue = (double)hs.finalKills / (double)((std::max)(1, hs.finalDeaths));
        out.stars = std::to_string(hs.stars);
        out.fkdr = FormatRatio(out.fkdrValue);
    }
    if (serFetched && st.resolved) {
        out.seraph = st;
        if (st.blacklisted) {
            out.blacklisted = true;
            out.blacklistType = st.reportType.empty() ? st.tag : st.reportType;
            out.blacklistReason = st.reason.empty() ? st.tooltip : st.reason;
            out.blacklistDaysAgo = st.daysAgo;
        }
    } else if (!uuid.empty()) {
        CachedSeraphTag cached{};
        if (GetSeraph(uuid, cached) && cached.resolved) {
            out.seraph = cached;
            if (cached.blacklisted) {
                out.blacklisted = true;
                out.blacklistType = cached.reportType.empty() ? cached.tag : cached.reportType;
                out.blacklistReason = cached.reason.empty() ? cached.tooltip : cached.reason;
                out.blacklistDaysAgo = cached.daysAgo;
            }
        }
    }

    const OverlayGamemode mode = OverlayGamemodeFromString(out.lookupMode);
    switch (mode) {
        case OverlayGamemode::Bedwars:
            if (!hs.ok) out.modeError = "No Bedwars stats for \"" + out.displayName + "\"";
            break;
        case OverlayGamemode::Skywars:
            if (!hs.hasSkywars) out.modeError = "No SkyWars stats for \"" + out.displayName + "\"";
            break;
        case OverlayGamemode::Duel:
            if (!hs.hasDuels) out.modeError = "No Duel stats for \"" + out.displayName + "\"";
            break;
        case OverlayGamemode::BedwarsDuels:
            if (!hs.hasDuels) out.modeError = "No Bedwars Duels stats for \"" + out.displayName + "\"";
            break;
        case OverlayGamemode::MurderMystery:
            if (!hs.hasMurderMystery) out.modeError = "No Murder Mystery stats for \"" + out.displayName + "\"";
            break;
        default:
            out.modeError = "Mode \"" + out.lookupMode + "\" not supported in /stats.";
            break;
    }
    return true;
}

bool PlayerApi::FetchHypixelByIgn(const std::string& hypixelKey,
                                  const std::string& ign,
                                  CachedHypixelStats& out,
                                  std::string& outUuid,
                                  bool& noPlayer) {
    return FetchHypixelByName(hypixelKey, ign, out, outUuid, noPlayer);
}

bool PlayerApi::ConsumeInvalidHypixelKeyNotice() {
    if (!g_hypKeyInvalidPending.load()) return false;
    if (g_hypKeyInvalidNotified.exchange(true)) return false;
    return true;
}

bool PlayerApi::ConsumeInvalidSeraphKeyNotice() {
    if (!g_serKeyInvalidPending.load()) return false;
    if (g_serKeyInvalidNotified.exchange(true)) return false;
    return true;
}

} // namespace taboverlay
