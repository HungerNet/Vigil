#pragma once

#include "tab_overlay.h"
#include <string>
#include <vector>
#include <unordered_set>

namespace taboverlay {

// Prequeue: Hypixel par pseudo -> [NICK] ou stats. Who/ONLINE: allowlist + UUID tab -> Hypixel + Seraph.
class PlayerApi {
public:
    static void SetKeys(const std::string& hypixelKey, const std::string& seraphKey,
                        const std::string& winstreakKey = "",
                        bool vegaProxy = false);
    static void EnsureWorker();
    static void Shutdown();

    static void ClearAllowlist();
    static void SetWhoAllowlist(const std::vector<std::string>& players);
    static void MergeWhoAllowlist(const std::vector<std::string>& players);
    static void ResetWhoSession();
    static void ClearPrequeueGhosts();
    static void AddPrequeueSpeaker(const std::string& player, int chatNameColor = -1);
    static void SetSpeakerUuid(const std::string& player, const std::string& uuid);

    static bool IsAllowed(const std::string& ign);
    // Replay/Atlas: allow + queue Hypixel lookup without /who.
    static void AllowPlayerForApi(const std::string& ign);
    static void OnTabPlayer(const std::string& ign, const std::string& uuid);

    static bool GetHypixel(const std::string& uuid, CachedHypixelStats& out);
    static bool GetSeraph(const std::string& uuid, CachedSeraphTag& out);
    static bool GetWinstreakPing(const std::string& uuid, CachedWinstreakPing& out);
    static bool IsPrequeueNick(const std::string& ign);
    static std::string GetCachedUuidForName(const std::string& ign);

    static int AllowlistSize();
    static std::vector<std::string> GetAllowlistNames();
    static int PendingCount();
    static int GetSeenCount(const std::string& ign);

    struct PrequeueGhost {
        std::string nameKey;
        std::string displayName;
        int nameColor = 0xFFFFFF;
        std::string uuidHint;
        bool manual = false;
    };
    static void CollectGhosts(ULONGLONG nowMs,
                              const std::unordered_set<std::string>& namesInTabLower,
                              std::vector<PrequeueGhost>& out);

    static bool FetchStatsNow(const std::string& playerName,
                              const std::string& hypixelKey,
                              const std::string& seraphKey,
                              const std::string& lookupMode,
                              PlayerLookupStats& out);

    static bool FetchHypixelByIgn(const std::string& hypixelKey,
                                  const std::string& ign,
                                  CachedHypixelStats& out,
                                  std::string& outUuid,
                                  bool& noPlayer);

    // Returns true once per invalid-key detection; clears the pending flag.
    static bool ConsumeInvalidHypixelKeyNotice();
    static bool ConsumeInvalidSeraphKeyNotice();
};

} // namespace taboverlay
