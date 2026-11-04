#include "party_queue_detector.h"

#include "globals.h"
#include "jni_helpers.h"
#include "utils.h"

#include "../java_src/VigilPacketLog.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

// Party detector file logging disabled for release builds.
void PartyLog(const std::string& msg) {
    (void)msg;
}

constexpr int kMinPartySize = 2;
constexpr int kDefaultMaxPartySize = 0;
// Queues where a game is already ongoing on the same map/server assign the joining
// players high global entity ids (>100). A queue that hasn't started yet keeps ids <100,
// where this heuristic does not work, so we ignore those.
constexpr int kQueueEntityIdThreshold = 100;
constexpr ULONGLONG kSpawnWindowMs = 6000;
constexpr ULONGLONG kAnalyzeDelayMs = 600;

// Max party size for the current queue mode (doubles=2, threes=3, fours=4).
int g_maxPartySize = kDefaultMaxPartySize;
bool g_locrawModeReceived = false;

int MaxPartySizeFromLocrawMode(const std::string& modeRaw) {
    const std::string mode = LowerAscii(modeRaw);
    if (mode.find("eight_one") != std::string::npos) return 1;  // solo (no party queue)
    if (mode.find("eight_two") != std::string::npos) return 2;  // doubles
    if (mode.find("four_three") != std::string::npos) return 3; // 3v3v3v3
    if (mode.find("four_four") != std::string::npos) return 4;  // 4v4v4v4
    if (mode.find("two_four") != std::string::npos) return 4;   // 4v4
    return kDefaultMaxPartySize;
}

struct SpawnRecord {
    int entityId = 0;
    ULONGLONG atMs = 0;
};

struct PendingPartyAnnouncement {
    int size = 0;
    int minId = 0;
    int maxId = 0;
};

struct PendingRequeueNotice {
    bool active = false;
};

std::mutex g_mu;
bool g_active = false;
std::atomic<bool> g_hookInstalled{false};
jclass g_packetLogClassGlobal = nullptr;
std::deque<SpawnRecord> g_spawns;
std::unordered_set<std::string> g_announcedPartyKeys;
std::unordered_set<int> g_announcedEntityIds;
std::vector<PendingPartyAnnouncement> g_pendingAnnouncements;
PendingRequeueNotice g_pendingRequeueNotice;
ULONGLONG g_analyzeScheduledAt = 0;
bool g_announcedRequeueNeeded = false;

static std::string PartyRangeKey(int minId, int maxId) {
    return std::to_string(minId) + "-" + std::to_string(maxId);
}

bool PartyRangeAlreadyAnnounced(int minId, int maxId) {
    if (g_announcedPartyKeys.count(PartyRangeKey(minId, maxId)) > 0) return true;
    for (int id = minId; id <= maxId; ++id) {
        if (g_announcedEntityIds.count(id)) return true;
    }
    return false;
}

void MarkPartyRangeAnnounced(int minId, int maxId) {
    g_announcedPartyKeys.insert(PartyRangeKey(minId, maxId));
    for (int id = minId; id <= maxId; ++id) {
        g_announcedEntityIds.insert(id);
    }
}

void ScheduleAnalyzeLocked(ULONGLONG now) {
    if (!g_active || !g_locrawModeReceived || g_maxPartySize < kMinPartySize) return;
    g_analyzeScheduledAt = now + kAnalyzeDelayMs;
}

std::string RunKey(const std::vector<int>& ids, size_t begin, size_t end) {
    std::string key;
    for (size_t i = begin; i < end; ++i) {
        if (!key.empty()) key += ',';
        key += std::to_string(ids[i]);
    }
    return key;
}

void PruneOldSpawnsLocked(ULONGLONG now) {
    while (!g_spawns.empty() && now - g_spawns.front().atMs > kSpawnWindowMs) {
        g_spawns.pop_front();
    }
}

void AnalyzeSpawnsLocked() {
    if (g_spawns.empty() || !g_locrawModeReceived) return;

    // Solo modes cannot be party-queued, so there is nothing to detect.
    const int maxParty = g_maxPartySize;
    if (maxParty < kMinPartySize) return;

    std::vector<int> ids;
    ids.reserve(g_spawns.size());
    bool anySpawnInWindow = false;
    bool allSpawnsLowEid = true;
    for (const SpawnRecord& rec : g_spawns) {
        anySpawnInWindow = true;
        if (rec.entityId <= kQueueEntityIdThreshold) continue;
        allSpawnsLowEid = false;
        // Ignore eids already tied to a announced party — window pruning can otherwise
        // re-form partial subsets (e.g. 4602-4604 after 4602-4605 was already announced).
        if (g_announcedEntityIds.count(rec.entityId)) continue;
        ids.push_back(rec.entityId);
    }
    if (ids.empty() && anySpawnInWindow && allSpawnsLowEid && g_active && !g_announcedRequeueNeeded) {
        g_announcedRequeueNeeded = true;
        g_pendingRequeueNotice.active = true;
        PartyLog("all recent eids <=100 -> new queue requeue needed");
        return;
    }
    if (ids.size() < (size_t)kMinPartySize) return;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    PartyLog("analyze maxPartySize=" + std::to_string(maxParty) +
             " kept eids=[" + RunKey(ids, 0, ids.size()) + "]");

    for (size_t i = 0; i < ids.size();) {
        size_t j = i + 1;
        while (j < ids.size() && ids[j] == ids[j - 1] + 1) {
            ++j;
        }
        PartyLog("  run [" + std::to_string(ids[i]) + "-" + std::to_string(ids[j - 1]) +
                 "] len=" + std::to_string(j - i));
        // A sequential run longer than max party size can't be a single party, so split it
        // into chunks of max party size. The leftover (run % maxParty) is a smaller party or
        // a solo queuer, e.g. doubles run of 4 -> 2 parties of 2, run of 3 -> 1 party + 1 solo.
        for (size_t chunkStart = i; chunkStart < j;) {
            const size_t chunkLen = (std::min)((size_t)maxParty, j - chunkStart);
            if (chunkLen >= (size_t)kMinPartySize) {
                const int minId = ids[chunkStart];
                const int maxId = ids[chunkStart + chunkLen - 1];
                const std::string key = RunKey(ids, chunkStart, chunkStart + chunkLen);
                const bool isNew = !PartyRangeAlreadyAnnounced(minId, maxId);
                PartyLog("    party of " + std::to_string(chunkLen) + " ids=[" + key + "]" +
                         (isNew ? " -> ANNOUNCE" : " -> already announced"));
                if (isNew) {
                    MarkPartyRangeAnnounced(minId, maxId);
                    PendingPartyAnnouncement ann;
                    ann.size = (int)chunkLen;
                    ann.minId = minId;
                    ann.maxId = maxId;
                    g_pendingAnnouncements.push_back(ann);
                }
            } else {
                PartyLog("    solo eid=" + std::to_string(ids[chunkStart]) + " (ignored)");
            }
            chunkStart += chunkLen;
        }
        i = (j > i + 1) ? j : i + 1;
    }
}

jobject ResolveNetHandlerForPacketHook(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (!env || !mcObj || !mcClass) return nullptr;

#ifdef VIGIL_LUNAR
    jmethodID midNetHandler = env->GetMethodID(mcClass, "getNetHandler",
        "()Lnet/minecraft/client/network/NetHandlerPlayClient;");
    if (!midNetHandler || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    jobject netHandler = env->CallObjectMethod(mcObj, midNetHandler);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return netHandler;
#else
    static const char* kMethodNames[] = {"u", "getNetHandler"};
    static const char* kMethodSigs[] = {"()Lbcy;", "()Lnet/minecraft/client/network/NetHandlerPlayClient;"};
    for (size_t i = 0; i < sizeof(kMethodNames) / sizeof(kMethodNames[0]); ++i) {
        jmethodID midNetHandler = env->GetMethodID(mcClass, kMethodNames[i], kMethodSigs[i]);
        if (!midNetHandler || env->ExceptionCheck()) {
            env->ExceptionClear();
            continue;
        }
        jobject netHandler = env->CallObjectMethod(mcObj, midNetHandler);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            continue;
        }
        if (netHandler) return netHandler;
    }
    return nullptr;
#endif
}

static bool EnsurePacketLogClass(JNIEnv* env, jobject classLoader) {
    if (g_packetLogClassGlobal) return true;
    if (!env) return false;

    jobject loader = classLoader ? classLoader : g_injectClassLoaderGlobal;
    if (!loader) return false;

    jclass packetLogClass = env->DefineClass("VigilPacketLog", loader,
        (const jbyte*)VigilPacketLog_class, VigilPacketLog_class_len);
    if (env->ExceptionCheck() || !packetLogClass) {
        env->ExceptionClear();
        jclass loaderClass = env->GetObjectClass(loader);
        if (loaderClass) {
            jmethodID midLoadClass = env->GetMethodID(loaderClass, "loadClass",
                "(Ljava/lang/String;)Ljava/lang/Class;");
            if (midLoadClass) {
                jstring className = env->NewStringUTF("VigilPacketLog");
                packetLogClass = (jclass)env->CallObjectMethod(loader, midLoadClass, className);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    packetLogClass = nullptr;
                }
                env->DeleteLocalRef(className);
            }
            env->DeleteLocalRef(loaderClass);
        }
    }

    if (!packetLogClass) return false;
    g_packetLogClassGlobal = (jclass)env->NewGlobalRef(packetLogClass);
    env->DeleteLocalRef(packetLogClass);
    return g_packetLogClassGlobal != nullptr;
}

} // namespace

extern "C" JNIEXPORT void JNICALL Java_VigilPacketLog_nativeOnSpawnPlayerEntity(JNIEnv*,
                                                                                    jclass,
                                                                                    jint entityId) {
    PartyQueueDetectorOnSpawnPlayer((int)entityId);
}

void PartyQueueDetectorReset() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_spawns.clear();
    g_announcedPartyKeys.clear();
    g_announcedEntityIds.clear();
    g_pendingAnnouncements.clear();
    g_pendingRequeueNotice = {};
    g_analyzeScheduledAt = 0;
    g_announcedRequeueNeeded = false;
    g_maxPartySize = kDefaultMaxPartySize;
    g_locrawModeReceived = false;
}

void PartyQueueDetectorSetActive(bool active) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_active == active) return;
    g_active = active;
    PartyLog(active ? "detector ACTIVE (queue joined)" : "detector INACTIVE (queue left)");
    if (active) {
        // Keep g_spawns: the party's named-entity spawns often arrive slightly BEFORE we detect
        // the queue (Prequeue/locraw), so the buffer may already hold them. Analyze once locraw mode is known.
        g_pendingAnnouncements.clear();
        g_pendingRequeueNotice = {};
        const ULONGLONG now = GetTickCount64();
        PruneOldSpawnsLocked(now);
        if (!g_spawns.empty()) {
            ScheduleAnalyzeLocked(now);
        }
    } else {
        g_hookInstalled.store(false, std::memory_order_relaxed);
        g_spawns.clear();
        g_announcedPartyKeys.clear();
        g_announcedEntityIds.clear();
        g_pendingAnnouncements.clear();
        g_pendingRequeueNotice = {};
        g_analyzeScheduledAt = 0;
        g_announcedRequeueNeeded = false;
        g_maxPartySize = kDefaultMaxPartySize;
        g_locrawModeReceived = false;
    }
}

void PartyQueueDetectorSetModeFromLocraw(const std::string& locrawModeRaw) {
    const int maxParty = MaxPartySizeFromLocrawMode(locrawModeRaw);
    bool reanalyze = false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        const bool wasUnknown = !g_locrawModeReceived;
        const bool changed = (g_maxPartySize != maxParty);
        g_maxPartySize = maxParty;
        g_locrawModeReceived = true;
        // Spawns often arrive on world load before /locraw returns. Wait for mode, then analyze once.
        if (g_active && !g_spawns.empty() && (wasUnknown || changed)) {
            ScheduleAnalyzeLocked(GetTickCount64());
            reanalyze = true;
        }
    }
    PartyLog("locraw mode='" + locrawModeRaw + "' -> maxPartySize=" + std::to_string(maxParty) +
             (reanalyze ? " (re-analyzing buffered spawns)" : ""));
}

void PartyQueueDetectorOnWorldLoad() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_maxPartySize = kDefaultMaxPartySize;
    g_locrawModeReceived = false;
    g_spawns.clear();
    g_announcedPartyKeys.clear();
    g_announcedEntityIds.clear();
    g_pendingAnnouncements.clear();
    g_pendingRequeueNotice = {};
    g_analyzeScheduledAt = 0;
    g_announcedRequeueNeeded = false;
    PartyLog("on world load (waiting for locraw mode, spawn buffer reset)");
}

unsigned long long PartyQueueDetectorAnnounceDelayMs() {
    return kAnalyzeDelayMs;
}

void PartyQueueDetectorOnSpawnPlayer(int entityId) {
    if (entityId <= 0) return;

    std::lock_guard<std::mutex> lk(g_mu);

    // Buffer spawns even while inactive: a party's spawns often arrive slightly before the queue
    // is detected. We only analyze/announce once active (Tick runs only then), but the pre-queue
    // spawns are kept so they can be picked up on activation.
    const ULONGLONG now = GetTickCount64();
    g_spawns.push_back({entityId, now});
    PruneOldSpawnsLocked(now);
    if (g_active) {
        ScheduleAnalyzeLocked(now);
    }

    std::string note = g_active ? "active" : "buffered(inactive)";
    if (g_active && !g_locrawModeReceived) note += ", waiting locraw";
    note += (entityId > kQueueEntityIdThreshold) ? ", >100" : ", <=100 filtered";
    PartyLog("spawn eid=" + std::to_string(entityId) + " (" + note + ")");
}

void PartyQueueDetectorTick(JNIEnv* env, jobject mcObj, jclass mcClass) {
    if (!env || !mcObj || !mcClass) return;

    std::vector<PendingPartyAnnouncement> pending;
    bool requeueNeeded = false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        const ULONGLONG now = GetTickCount64();
        PruneOldSpawnsLocked(now);
        if (g_analyzeScheduledAt != 0 && now >= g_analyzeScheduledAt) {
            g_analyzeScheduledAt = 0;
            AnalyzeSpawnsLocked();
        }
        pending.swap(g_pendingAnnouncements);
        requeueNeeded = g_pendingRequeueNotice.active;
        g_pendingRequeueNotice.active = false;
    }

    if (requeueNeeded) {
        std::string localMsg = "\xC2\xA7""7[\xC2\xA7""1Fre\xC2\xA7""fnch\xC2\xA7""cify\xC2\xA7""7] "
                               "\xC2\xA7""cNew queue \xC2\xA7""7- \xC2\xA7""erequeue needed";
        PrintLocalChat(env, mcObj, mcClass, localMsg);
    }

    for (const PendingPartyAnnouncement& ann : pending) {
        std::string localMsg = "\xC2\xA7""7[\xC2\xA7""1Fre\xC2\xA7""fnch\xC2\xA7""cify\xC2\xA7""7] "
                               "\xC2\xA7""7Queue party of \xC2\xA7""c" + std::to_string(ann.size) +
                               "\xC2\xA7""7 (ids \xC2\xA7""e" + std::to_string(ann.minId) + "\xC2\xA7""7-\xC2\xA7""e" +
                               std::to_string(ann.maxId) + "\xC2\xA7""7)";
        PrintLocalChat(env, mcObj, mcClass, localMsg);
    }
}

bool IsPartyQueuePacketHookInstalled() {
    return g_hookInstalled.load(std::memory_order_relaxed);
}

bool TryInstallPartyQueuePacketHook(JNIEnv* env, jobject mcObj, jclass mcClass, jobject classLoader) {
    if (!env || !mcObj || !mcClass) return false;
    if (g_hookInstalled.load(std::memory_order_relaxed)) return true;

    if (!EnsurePacketLogClass(env, classLoader)) return false;

    JNINativeMethod methods[] = {
        {(char*)"nativeOnSpawnPlayerEntity", (char*)"(I)V",
         (void*)&Java_VigilPacketLog_nativeOnSpawnPlayerEntity}
    };
    if (env->RegisterNatives(g_packetLogClassGlobal, methods, 1) != JNI_OK || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jobject netHandler = ResolveNetHandlerForPacketHook(env, mcObj, mcClass);
    if (!netHandler) return false;

    jmethodID midInstall = env->GetStaticMethodID(g_packetLogClassGlobal, "install", "(Ljava/lang/Object;)V");
    if (!midInstall || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(netHandler);
        return false;
    }

    env->CallStaticVoidMethod(g_packetLogClassGlobal, midInstall, netHandler);
    const bool ok = !env->ExceptionCheck();
    if (!ok) env->ExceptionClear();
    env->DeleteLocalRef(netHandler);

    if (ok) g_hookInstalled.store(true, std::memory_order_relaxed);
    return ok;
}
