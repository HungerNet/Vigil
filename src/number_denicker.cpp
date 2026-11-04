#include "number_denicker.h"
#include "denick.h"
#include "player_api.h"
#include "utils.h"
#include "globals.h"
#include <deque>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <regex>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <atomic>
#include <cstdarg>

namespace taboverlay {
namespace {

constexpr int kAuroraRange = 200;
constexpr int kAuroraMax = 5;
constexpr int kMaxNdJobs = 16;

struct NdPlayerState {
    std::string nickName;
    std::vector<std::string> potentials;
    bool finalsDone = false;
    bool bedsDone = false;
    bool finalsPending = false;
    bool bedsPending = false;
    bool resolved = false;
    bool registeredNick = false;
};

struct NdResolved {
    std::string realName;
    bool suspected = false;
    std::vector<std::string> potentials;
    CachedHypixelStats hyp{};
    bool hypOk = false;
    std::string realUuid;
};

struct NdJob {
    std::string type; // "finals" | "beds"
    std::string nickKey;
    std::string nickDisplay;
    std::string number;
};

std::mutex g_ndMu;
std::condition_variable g_ndCv;
std::deque<NdJob> g_ndJobs;
std::unordered_set<std::string> g_ndQueued;
std::unordered_map<std::string, NdPlayerState> g_ndByNick;
std::unordered_map<std::string, NdResolved> g_ndResolved;
std::unordered_set<std::string> g_ndChatPending;
std::deque<std::string> g_ndChatOut;

bool g_ndEnabled = false;
bool g_ndInGame = false;
bool g_ndGameStarted = false;

struct NdEventCounters {
    std::atomic<int> chatFinals{0};
    std::atomic<int> chatBeds{0};
    std::atomic<int> nickRegistered{0};
    std::atomic<int> auroraQueued{0};
    std::atomic<int> resolved{0};
    std::atomic<int> potentialsChat{0};
};

NdEventCounters g_ndCnt;
std::unordered_map<std::string, ULONGLONG> g_ndLogLast;

bool NdShouldLog(const std::string& key, ULONGLONG intervalMs = 4000) {
    const ULONGLONG now = GetTickCount64();
    auto it = g_ndLogLast.find(key);
    if (it != g_ndLogLast.end() && (now - it->second) < intervalMs) return false;
    g_ndLogLast[key] = now;
    return true;
}

void NdLogf(const char*, ...) {}
bool g_ndWorkerStop = false;
bool g_ndWorkerOn = false;
std::string g_ndAuroraKey;
std::string g_ndHypKey;
std::thread g_ndWorker;

std::string CanonKey(const std::string& ign) {
    return LowerAscii(Trim(StripMinecraftFormatting(ign)));
}

static void NormalizeApostrophesInPlace(std::string& s) {
    const std::string curly = "\xE2\x80\x99"; // U+2019 '
    const std::string acute = "\xC2\xB4";     // U+00B4 ´
    for (size_t pos = 0; (pos = s.find(curly, pos)) != std::string::npos; pos += 1) {
        s.replace(pos, curly.size(), "'");
    }
    for (size_t pos = 0; (pos = s.find(acute, pos)) != std::string::npos; pos += 1) {
        s.replace(pos, acute.size(), "'");
    }
}

std::string NormalizeChatLine(const std::string& message) {
    std::string out = Trim(StripMinecraftFormatting(message));
    NormalizeApostrophesInPlace(out);
    return out;
}

std::string UrlEncodeNd(const std::string& s) {
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

bool IsBedwarsStartMessage(const std::string& clean) {
    return clean == "Protect your bed and destroy the enemy beds." ||
           clean == "You will respawn because you still have a bed!";
}

bool ParseNumberFinals(const std::string& clean, std::string& outNick, std::string& outNumber) {
    // Hypixel: "Octa117 was itzalex's final #23,835. FINAL KILL!"
    static const std::regex rgx(
        "^[\\w-]+\\s+was\\s+([\\w-]+)'s\\s+final\\s*#([\\d,]+)\\.\\s*FINAL\\s+KILL!$",
        std::regex::icase);
    std::smatch m;
    if (!std::regex_match(clean, m, rgx)) return false;
    outNick = m[1].str();
    outNumber = m[2].str();
    outNumber.erase(std::remove(outNumber.begin(), outNumber.end(), ','), outNumber.end());
    return !outNick.empty() && !outNumber.empty();
}

bool ParseNumberBeds(const std::string& clean, std::string& outNick, std::string& outNumber) {
    // Hypixel: "BED DESTRUCTION > Your Bed was bed #21,362 destroyed by AidenHerrera1997!"
    static const std::regex rgx(
        "BED\\s+DESTRUCTION\\s*>.*?bed\\s*#([\\d,]+).*?destroyed\\s+by\\s+([\\w-]+)\\s*!",
        std::regex::icase);
    std::smatch m;
    if (!std::regex_search(clean, m, rgx)) return false;
    outNumber = m[1].str();
    outNumber.erase(std::remove(outNumber.begin(), outNumber.end(), ','), outNumber.end());
    outNick = m[2].str();
    return !outNick.empty() && !outNumber.empty();
}

NdPlayerState& GetStateLocked(const std::string& nickKey, const std::string& displayName) {
    auto it = g_ndByNick.find(nickKey);
    if (it == g_ndByNick.end()) {
        NdPlayerState st;
        st.nickName = displayName;
        g_ndByNick.emplace(nickKey, std::move(st));
    }
    return g_ndByNick[nickKey];
}

bool ShouldProcessNickLocked(const std::string& nickKey) {
    auto res = g_ndResolved.find(nickKey);
    if (res != g_ndResolved.end()) return false;

    DenickOverlayData denick{};
    if (DenickGetOverlayData(nickKey, denick)) {
        if (denick.state == DenickOverlayData::State::Denicked && denick.hypOk && !denick.realName.empty()) {
            return false;
        }
        if (denick.state == DenickOverlayData::State::KnownNickSkin ||
            denick.state == DenickOverlayData::State::Pending) {
            return true;
        }
    }

    auto it = g_ndByNick.find(nickKey);
    if (it != g_ndByNick.end() && it->second.registeredNick) return true;
    return false;
}

static std::string JsonEscapeNd(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '\\' || c == '"') out.push_back('\\');
        if (c == '\n') { out += "\\n"; continue; }
        out.push_back(c);
    }
    return out;
}

static std::string BuildFrenchifyChatPrefixJson() {
    return "{\"text\":\"[\",\"color\":\"gray\"},"
           "{\"text\":\"Fre\",\"color\":\"dark_blue\"},"
           "{\"text\":\"nch\",\"color\":\"white\"},"
           "{\"text\":\"ify\",\"color\":\"red\"},"
           "{\"text\":\"] \",\"color\":\"gray\"}";
}

std::string BuildPotentialsChat(const std::string& nickDisplay, int count,
                                const std::vector<std::string>& names) {
    std::string hover;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i) hover += "\\n";
        hover += JsonEscapeNd(names[i]);
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", count);
    return "{\"text\":\"\"," + BuildFrenchifyChatPrefixJson() +
           ",{\"text\":\"" + JsonEscapeNd(nickDisplay) + "\",\"color\":\"white\"},"
           "{\"text\":\" (" + std::string(buf) + " possible)\",\"color\":\"gray\","
           "\"hoverEvent\":{\"action\":\"show_text\",\"value\":{\"text\":\"" + hover + "\"}}}"
           "]}";
}

std::string BuildDenickStatsChat(const std::string& nickDisplay, const std::string& realName,
                                 const CachedHypixelStats& hyp, bool suspected) {
    const std::string showName = suspected ? (realName + "?") : realName;
    const double fkdr = (double)hyp.finalKills / (double)((std::max)(1, hyp.finalDeaths));
    char fkBuf[16];
    std::snprintf(fkBuf, sizeof(fkBuf), "%.2f", fkdr);
    const std::string hover = std::to_string(hyp.stars) + " stars - " + fkBuf + " FKDR";
    // Small chat ping: [Frenchify] nick -> real
    return "{\"text\":\"\"," + BuildFrenchifyChatPrefixJson() +
           ",{\"text\":\"" + JsonEscapeNd(nickDisplay) + "\",\"color\":\"white\"},"
           "{\"text\":\" -> \",\"color\":\"gray\"},"
           "{\"text\":\"" + JsonEscapeNd(showName) + "\",\"color\":\"green\","
           "\"hoverEvent\":{\"action\":\"show_text\",\"value\":{\"text\":\"" + hover + "\"}}}"
           "]}";
}

bool QueryAurora(const std::string& apiKey, const std::string& type, const std::string& value,
                 std::vector<std::string>& outNames) {
    outNames.clear();
    if (apiKey.empty() || type.empty() || value.empty()) return false;

    const std::string url =
        "https://bordic.xyz/api/v2/resources/lookup/" + LowerAscii(type) +
        "?key=" + UrlEncodeNd(apiKey) +
        "&value=" + UrlEncodeNd(value) +
        "&range=" + std::to_string(kAuroraRange) +
        "&max=" + std::to_string(kAuroraMax);

    std::string body;
    DWORD status = 0;
    if (!HttpGetHttps(url, body, status) || status != 200) return false;
    if (body.find("\"success\":true") == std::string::npos &&
        body.find("\"success\": true") == std::string::npos) {
        return false;
    }

    std::regex entryRgx("\\{[^{}]*\"distance\"\\s*:\\s*(-?[0-9.]+)[^{}]*\"name\"\\s*:\\s*\"([^\"]*)\"[^{}]*\\}");
    std::regex entryRgx2("\\{[^{}]*\"name\"\\s*:\\s*\"([^\"]*)\"[^{}]*\"distance\"\\s*:\\s*(-?[0-9.]+)[^{}]*\\}");
    for (std::sregex_iterator it(body.begin(), body.end(), entryRgx), end; it != end; ++it) {
        const double dist = atof(it->str(1).c_str());
        if (dist <= 0.0) outNames.push_back(it->str(2));
    }
    for (std::sregex_iterator it(body.begin(), body.end(), entryRgx2), end; it != end; ++it) {
        const double dist = atof(it->str(2).c_str());
        if (dist <= 0.0) outNames.push_back(it->str(1));
    }
    return !outNames.empty();
}

static bool NameInList(const std::string& name, const std::vector<std::string>& list) {
    const std::string k = CanonKey(name);
    for (const auto& n : list) {
        if (CanonKey(n) == k) return true;
    }
    return false;
}

void PushNdJobLocked(NdJob job) {
    if ((int)g_ndJobs.size() >= kMaxNdJobs) return;
    const std::string qk = job.nickKey + "|" + job.type + "|" + job.number;
    if (!g_ndQueued.insert(qk).second) return;
    g_ndJobs.push_back(std::move(job));
    g_ndCv.notify_one();
}

void NdWorkerLoop() {
    for (;;) {
        NdJob job;
        std::string auroraKey, hypKey;
        {
            std::unique_lock<std::mutex> lk(g_ndMu);
            g_ndCv.wait(lk, [] { return g_ndWorkerStop || !g_ndJobs.empty(); });
            if (g_ndWorkerStop) return;
            job = g_ndJobs.front();
            g_ndJobs.pop_front();
            auroraKey = g_ndAuroraKey;
            hypKey = g_ndHypKey;
        }

        // Network I/O must stay outside g_ndMu or the game freezes while chat/overlay wait on the lock.
        std::vector<std::string> matches;
        if (!QueryAurora(auroraKey, job.type, job.number, matches)) {
            std::lock_guard<std::mutex> lk(g_ndMu);
            g_ndQueued.erase(job.nickKey + "|" + job.type + "|" + job.number);
            auto it = g_ndByNick.find(job.nickKey);
            if (it != g_ndByNick.end()) {
                if (job.type == "finals") it->second.finalsPending = false;
                else it->second.bedsPending = false;
            }
            continue;
        }

        std::string potentialsChat;
        bool tryResolve = false;
        std::string nickDisplay;
        std::vector<std::string> potentialsCopy;
        {
            std::lock_guard<std::mutex> lk(g_ndMu);
            g_ndQueued.erase(job.nickKey + "|" + job.type + "|" + job.number);

            auto it = g_ndByNick.find(job.nickKey);
            if (it == g_ndByNick.end()) continue;
            NdPlayerState& player = it->second;
            nickDisplay = player.nickName;

            if (job.type == "finals") {
                player.finalsDone = true;
                player.finalsPending = false;
            } else {
                player.bedsDone = true;
                player.bedsPending = false;
            }

            if (!player.potentials.empty()) {
                std::vector<std::string> filtered;
                for (const auto& n : matches) {
                    if (NameInList(n, player.potentials)) filtered.push_back(n);
                }
                player.potentials = std::move(filtered);
            } else {
                player.potentials = matches;
            }

            if (!player.finalsDone || !player.bedsDone || player.potentials.empty()) {
                if (player.potentials.size() > 0 && (!player.finalsDone || !player.bedsDone)) {
                    potentialsChat = BuildPotentialsChat(player.nickName, (int)player.potentials.size(),
                                                         player.potentials);
                }
            } else if (!player.resolved) {
                tryResolve = true;
                potentialsCopy = player.potentials;
            }
        }

        if (!potentialsChat.empty()) {
            g_ndCnt.potentialsChat.fetch_add(1, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lk(g_ndMu);
            g_ndChatOut.push_back(potentialsChat);
        }

        if (tryResolve && !potentialsCopy.empty()) {
            const std::string realName = potentialsCopy[0];
            const bool suspected = potentialsCopy.size() > 1;

            CachedHypixelStats hyp{};
            std::string hypUuid;
            bool noPlayer = false;
            const bool hypOk = !hypKey.empty() &&
                PlayerApi::FetchHypixelByIgn(hypKey, realName, hyp, hypUuid, noPlayer) && !noPlayer;

            if (hypOk) {
                std::string resolveChat;
                {
                    std::lock_guard<std::mutex> lk(g_ndMu);
                    auto it = g_ndByNick.find(job.nickKey);
                    if (it != g_ndByNick.end() && !it->second.resolved) {
                        NdResolved resolvedEntry{};
                        resolvedEntry.realName = realName;
                        resolvedEntry.suspected = suspected;
                        resolvedEntry.potentials = potentialsCopy;
                        resolvedEntry.hyp = hyp;
                        resolvedEntry.hypOk = true;
                        resolvedEntry.realUuid = hypUuid;
                        it->second.resolved = true;
                        g_ndResolved[job.nickKey] = resolvedEntry;
                        g_ndChatPending.insert(job.nickKey);
                        resolveChat = BuildDenickStatsChat(nickDisplay.empty() ? it->second.nickName : nickDisplay,
                                                           realName, hyp, suspected);
                        g_ndCnt.resolved.fetch_add(1, std::memory_order_relaxed);
                        NdLogf("[NumberDenicker] resolved nick=%s -> %s%s stars=%d\n",
                               (nickDisplay.empty() ? it->second.nickName : nickDisplay).c_str(),
                               realName.c_str(), suspected ? "?" : "", hyp.stars);
                    }
                }
                if (!resolveChat.empty()) {
                    std::lock_guard<std::mutex> lk(g_ndMu);
                    g_ndChatOut.push_back(resolveChat);
                }
            }
        }

        Sleep(80);
    }
}

void StartNdWorkerLocked() {
    if (g_ndWorkerOn) return;
    g_ndWorkerStop = false;
    if (g_ndWorker.joinable()) g_ndWorker.join();
    g_ndWorker = std::thread(NdWorkerLoop);
    g_ndWorkerOn = true;
}

void StopNdWorker() {
    {
        std::lock_guard<std::mutex> lk(g_ndMu);
        if (!g_ndWorkerOn) return;
        g_ndWorkerStop = true;
        g_ndJobs.clear();
        g_ndQueued.clear();
    }
    g_ndCv.notify_all();
    if (g_ndWorker.joinable()) g_ndWorker.join();
    std::lock_guard<std::mutex> lk(g_ndMu);
    g_ndWorkerOn = false;
}

void QueueProcessLocked(const std::string& type, const std::string& nickDisplay, const std::string& number) {
    const std::string nickKey = CanonKey(nickDisplay);
    if (nickKey.empty() || number.empty()) return;
    if (!ShouldProcessNickLocked(nickKey)) {
        if (NdShouldLog("nd:skip:" + nickKey, 8000)) {
            NdLogf("[NumberDenicker] skip %s (nick not tracked in tab): %s\n",
                   type.c_str(), nickDisplay.c_str());
        }
        return;
    }

    NdPlayerState& player = GetStateLocked(nickKey, nickDisplay);
    if (player.resolved) return;

    const bool doneKey = (type == "finals") ? player.finalsDone : player.bedsDone;
    const bool pendingKey = (type == "finals") ? player.finalsPending : player.bedsPending;
    if (doneKey || pendingKey) return;

    if (type == "finals") player.finalsPending = true;
    else player.bedsPending = true;

    g_ndCnt.auroraQueued.fetch_add(1, std::memory_order_relaxed);
    if (NdShouldLog("nd:queue:" + nickKey)) {
        NdLogf("[NumberDenicker] queue type=%s nick=%s number=%s\n",
               type.c_str(), nickDisplay.c_str(), number.c_str());
    }
    PushNdJobLocked(NdJob{type, nickKey, nickDisplay, number});
    StartNdWorkerLocked();
}

} // namespace

void NumberDenickerSetEnabled(bool enabled) {
    std::lock_guard<std::mutex> lk(g_ndMu);
    if (g_ndEnabled != enabled) {
        NdLogf("[NumberDenicker] enabled=%d auroraKey=%d\n",
               enabled ? 1 : 0, g_ndAuroraKey.empty() ? 0 : 1,
               g_dllDir.empty() ? "(no dllDir) " : (g_dllDir + "\\").c_str());
    }
    g_ndEnabled = enabled;
    if (enabled && !g_ndAuroraKey.empty()) StartNdWorkerLocked();
}

void NumberDenickerSetKeys(const std::string& auroraKey, const std::string& hypixelKey) {
    std::lock_guard<std::mutex> lk(g_ndMu);
    g_ndAuroraKey = auroraKey;
    g_ndHypKey = hypixelKey;
    if (g_ndEnabled && !g_ndAuroraKey.empty()) StartNdWorkerLocked();
}

void NumberDenickerSetInGame(bool inGame) {
    std::lock_guard<std::mutex> lk(g_ndMu);
    g_ndInGame = inGame;
}

void NumberDenickerShutdown() { StopNdWorker(); }

void NumberDenickerOnChatLine(const std::string& msg) {
    if (!g_ndEnabled || g_ndAuroraKey.empty()) return;

    const std::string clean = NormalizeChatLine(msg);
    if (clean.empty()) return;

    // Chat can be observed multiple times (polling / duplication). Dedupe to avoid console spam.
    {
        static std::string s_lastClean;
        static ULONGLONG s_lastAt = 0;
        const ULONGLONG now = GetTickCount64();
        if (clean == s_lastClean && (now - s_lastAt) < 1500) {
            return;
        }
        s_lastClean = clean;
        s_lastAt = now;
    }

    if (IsBedwarsStartMessage(clean)) {
        std::lock_guard<std::mutex> lk(g_ndMu);
        g_ndGameStarted = true;
        g_ndByNick.clear();
        g_ndQueued.clear();
        g_ndJobs.clear();
        NdLogf("[NumberDenicker] game started — state cleared\n");
        DenickOnGameStarted();
        return;
    }

    std::string nick, number;
    if (ParseNumberFinals(clean, nick, number)) {
        std::lock_guard<std::mutex> lk(g_ndMu);
        if (!g_ndGameStarted) {
            g_ndGameStarted = true;
            DenickOnGameStarted();
        }
        g_ndCnt.chatFinals.fetch_add(1, std::memory_order_relaxed);
        NdLogf("[NumberDenicker] chat finals nick=%s number=%s clean=%s\n",
               nick.c_str(), number.c_str(), clean.c_str());
        QueueProcessLocked("finals", nick, number);
        return;
    }
    if (ParseNumberBeds(clean, nick, number)) {
        std::lock_guard<std::mutex> lk(g_ndMu);
        if (!g_ndGameStarted) {
            g_ndGameStarted = true;
            DenickOnGameStarted();
        }
        g_ndCnt.chatBeds.fetch_add(1, std::memory_order_relaxed);
        NdLogf("[NumberDenicker] chat beds nick=%s number=%s clean=%s\n",
               nick.c_str(), number.c_str(), clean.c_str());
        QueueProcessLocked("beds", nick, number);
        return;
    }

    if (!g_ndInGame && !g_ndGameStarted) return;

    if ((clean.find("FINAL KILL") != std::string::npos || clean.find("bed #") != std::string::npos) &&
        NdShouldLog("nd:parse_miss", 12000)) {
        NdLogf("[NumberDenicker] unmatched BW line inGame=%d started=%d: %s\n",
               g_ndInGame ? 1 : 0, g_ndGameStarted ? 1 : 0, clean.c_str());
    }
}

void NumberDenickerRegisterNick(const std::string& nickIgn) {
    const std::string nickKey = CanonKey(nickIgn);
    if (nickKey.empty()) return;
    std::lock_guard<std::mutex> lk(g_ndMu);
    NdPlayerState& st = GetStateLocked(nickKey, nickIgn);
    if (!st.registeredNick) {
        st.registeredNick = true;
        g_ndCnt.nickRegistered.fetch_add(1, std::memory_order_relaxed);
        if (NdShouldLog("nd:reg:" + nickKey, 8000)) {
            NdLogf("[NumberDenicker] tab nick registered=%s tracked=%zu\n",
                   nickIgn.c_str(), g_ndByNick.size());
        }
    }
}

bool NumberDenickerGetOverlayData(const std::string& nickIgn, NumberDenickerOverlayData& out) {
    out = NumberDenickerOverlayData{};
    const std::string nickKey = CanonKey(nickIgn);
    if (nickKey.empty()) return false;

    std::lock_guard<std::mutex> lk(g_ndMu);
    auto it = g_ndResolved.find(nickKey);
    if (it != g_ndResolved.end() && it->second.hypOk) {
        out.resolved = true;
        out.suspected = it->second.suspected;
        out.realName = it->second.realName;
        out.potentials = it->second.potentials;
        out.potentialCount = (int)out.potentials.size();
        out.hasPotentials = out.potentialCount > 1;
        out.hyp = it->second.hyp;
        out.hypOk = true;
        return true;
    }

    auto st = g_ndByNick.find(nickKey);
    if (st != g_ndByNick.end() && !st->second.potentials.empty()) {
        out.hasPotentials = true;
        out.potentialCount = (int)st->second.potentials.size();
        out.potentials = st->second.potentials;
        return true;
    }
    return false;
}

bool NumberDenickerTryConsumeDenickChat(const std::string& nickIgn, std::string& outNickIgn, std::string& outRealName) {
    const std::string nickKey = CanonKey(nickIgn);
    if (nickKey.empty()) return false;

    std::lock_guard<std::mutex> lk(g_ndMu);
    if (g_ndChatPending.find(nickKey) == g_ndChatPending.end()) return false;
    auto it = g_ndResolved.find(nickKey);
    if (it == g_ndResolved.end() || !it->second.hypOk || it->second.realName.empty()) return false;

    g_ndChatPending.erase(nickKey);
    outNickIgn = nickIgn;
    outRealName = it->second.realName;
    if (it->second.suspected) outRealName += "?";
    return true;
}

bool NumberDenickerTryConsumeChat(std::string& outMsg, bool& outIsJson) {
    outIsJson = false;
    std::lock_guard<std::mutex> lk(g_ndMu);
    if (g_ndChatOut.empty()) return false;
    outMsg = g_ndChatOut.front();
    g_ndChatOut.pop_front();
    outIsJson = !outMsg.empty() && outMsg[0] == '{';
    return true;
}

void NumberDenickerGetDebugSnapshot(NumberDenickerDebugSnapshot& out) {
    out = NumberDenickerDebugSnapshot{};
    out.chatFinals = g_ndCnt.chatFinals.load(std::memory_order_relaxed);
    out.chatBeds = g_ndCnt.chatBeds.load(std::memory_order_relaxed);
    out.nickRegistered = g_ndCnt.nickRegistered.load(std::memory_order_relaxed);
    out.auroraQueued = g_ndCnt.auroraQueued.load(std::memory_order_relaxed);
    out.resolved = g_ndCnt.resolved.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(g_ndMu);
    out.tracked = (int)g_ndByNick.size();
    out.jobsPending = (int)g_ndJobs.size();
    out.inGame = g_ndInGame ? 1 : 0;
    out.gameStarted = g_ndGameStarted ? 1 : 0;
}

} // namespace taboverlay
