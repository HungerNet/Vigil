#pragma once

#include "globals.h"
#include <unordered_map>
#include <memory>
#include <atomic>

namespace taboverlay {

enum class TextAlign {
    Left,
    Center,
    Right
};

struct ColumnConfig {
    std::string id;
    std::string displayName;
    int xOffset = 0;
    int width = 0;
    TextAlign align = TextAlign::Left;
    bool enabled = true;
    int order = 0;
};

struct GameResourceStats {
    int emeralds = 0;       // secondes avant prochain spawn émeraude
    int emeraldCount = 0;   // nombre de spawns depuis début de partie
    int diamonds = 0;       // secondes avant prochain spawn diamant
    int diamondCount = 0;
};

struct PlayerBedwarsStats {
    std::string stars = "[ERR]";
    std::string teamLetter = "?";
    std::string name = "";
    int hp = 20;
    std::string fdRatio = "ERR";
    int ping = 0;
    int statusIcon = 0;
    int tagIcon = 0;
    int nameColor = 0xFFFFFF;
    int hpColor = 0x55FF55;
    bool flagged = false;
    std::string flagCheck;
    int flagColor = 0xFFFFFF;

    // Flexible key/value storage for optional columns.
    std::unordered_map<std::string, std::string> statsRaw;
    std::unordered_map<std::string, int> statsColor;

    std::string playerId;
    jobject skinResourceGlobal = nullptr;
    bool valid = true;
};

enum class OverlayGamemode {
    Unknown,
    Bedwars,
    Skywars,
    Duel,
    BedwarsDuels,
    MurderMystery
};

struct CachedHypixelStats {
    ULONGLONG fetchedAt = 0;
    bool ok = false;
    int stars = 0;
    std::string activeStar;
    std::string activePrestigeScheme;
    std::string activePrestigeBracket;
    int bwExperience = 0;
    int wins = 0;
    int losses = 1;
    int finalKills = 0;
    int finalDeaths = 1;
    int bedsBroken = 0;
    int bedsLost = 1;
    int kills = 0;
    int deaths = 1;
    int entityAttackFinalKills = 0;
    int entityAttackKills = 0;
    int entityAttackFinalDeaths = 0;
    int entityAttackDeaths = 0;
    int magicFinalDeaths = 0;
    int gamesPlayed = 1;
    int winstreak = -1;
    long long lastLogin = 0;
    long long lastLogout = 0;

    bool hasSkywars = false;
    int swStars = 0;
    int swLevel = 0;
    std::string swStarsFormatted;
    int swKills = 0;
    int swDeaths = 1;
    int swWins = 0;
    int swLosses = 1;

    bool hasDuels = false;
    int duelWins = 0;
    int duelLosses = 1;
    int duelKills = 0;
    int duelDeaths = 1;
    int duelWinsBw = 0;
    int duelLossesBw = 1;

    bool hasMurderMystery = false;
    int mmKills = 0;
    int mmDeaths = 1;
    int mmWins = 0;
    int mmLosses = 1;
    std::string mmIcon; // resolved prefix icon id (e.g. ALPHA)
    std::string mmEmblemColor; // resolved prefix/scheme color (e.g. LIGHT_PURPLE)
    int mmPrefixStat = 0; // value of active_prefixstat (e.g. kills_MURDER_CLASSIC); -1 = omit number
    bool mmPrefixIsKills = false; // true when active_prefixstat is a *kills* variant (underline)
    char mmPrefixOpen = '[';
    char mmPrefixClose = ']';
    bool mmHasPrefix = false;

    std::string displayName;
    std::string newPackageRank = "NONE";
    std::string monthlyPackageRank = "NONE";
};

void SetOverlayActiveGamemode(OverlayGamemode mode);
OverlayGamemode GetOverlayActiveGamemode();
std::string OverlayGamemodeToString(OverlayGamemode mode);
OverlayGamemode OverlayGamemodeFromString(const std::string& s);
void ClearBlacklistChatAnnounced();
void ClearChatOverlayAnnounced();
void ClearLocalPlayerTeam();
void ClearWhoTeamFromWho();
void ApplyWhoTeamLine(const std::vector<std::string>& teamNames);
// Force resource counters/timers to refresh on next snapshot (used for Bed gone chat event).
void ForceClearResourcesCache();
void SetBedwarsLocrawMode(const std::string& locrawModeRaw);
void OnBedwarsDisconnectChatLine(const std::string& cleanMsg);
void OnBedwarsReconnectChatLine(const std::string& cleanMsg);
void ClearBedDisconnectTimers();
void ResetOverlayOnWorldLoad();
void UpdateBedDisconnectTeamCache(const std::string& playerName, const std::string& teamLetter);

enum class ResourceDisplayLocation {
    Tab = 0,
    Scoreboard = 1
};

ResourceDisplayLocation ParseResourceDisplayLocation(const std::string& value);

struct CachedSeraphTag {
    ULONGLONG fetchedAt = 0;
    bool resolved = false;
    bool blacklisted = false;
    std::string tag;
    std::string reportType;
    bool verified = false;
    std::string reason;
    std::string tooltip;
    int daysAgo = -1;
};

struct PlayerLookupStats {
    bool resolved = false;
    std::string playerName;
    std::string displayName;
    std::string uuid;
    std::string stars = "N/A";
    std::string fkdr = "N/A";
    int starCount = 0;
    double fkdrValue = 0.0;
    char rankColor = '7';
    std::string lookupMode = "bedwars";
    std::string modeError;
    CachedHypixelStats hypixel;
    CachedSeraphTag seraph;
    bool blacklisted = false;
    std::string blacklistType;
    std::string blacklistReason;
    int blacklistDaysAgo = -1;
};

struct PlayerLookupChatOutput {
    bool isJson = false;
    std::string message;
};

struct CachedWinstreakPing {
    ULONGLONG fetchedAt = 0;
    bool resolved = false;
    int averagePing = -1;
    int pingVar = -1; // max(avg) - min(avg) across Bordic history
};

class ColumnManager {
public:
    void setColumns(const std::vector<ColumnConfig>& columns);
    std::vector<ColumnConfig> getActiveColumns() const;
    int computeTextX(int startX, const ColumnConfig& col, int textWidth) const;

private:
    std::vector<ColumnConfig> columns_;
};

struct SessionContext {
    std::string headerTitle;
    GameResourceStats resources;
    std::unordered_map<std::string, std::string> headerCounters;
    std::string footerText;
    std::string matchState;
    ULONGLONG updatedAtMs = 0;
};

struct LayoutConfig {
    int topMargin = 18;
    int sidePadding = 10;
    int tableWidth = 360;
    int headerHeight = 25;
    int rowHeight = 12;
    int footerHeight = 20;
    int extraMargins = 15;
    int firstRowOffsetY = 34;
    int columnTitleOffsetY = 20;
};

struct LayoutResult {
    int startX = 0;
    int startY = 0;
    int totalWidth = 0;
    int totalHeight = 0;
    int headerY = 0;
    int firstRowY = 0;
    int footerY = 0;
};

struct OverlaySnapshot {
    std::vector<PlayerBedwarsStats> players;
    SessionContext session;
    std::vector<ColumnConfig> columns;
    int screenWidth = 0;
    int screenHeight = 0;
    ULONGLONG updatedAtMs = 0;
};

class SnapshotStore {
public:
    void publish(const OverlaySnapshot& snapshot);
    std::shared_ptr<OverlaySnapshot> readLatest() const;

private:
    mutable std::mutex mutex_;
    std::shared_ptr<OverlaySnapshot> latest_;
};

struct JniCache {
    bool initialized = false;

    jclass clsMinecraft = nullptr;
    jclass clsScaledResolution = nullptr;
    jclass clsGui = nullptr;
    jclass clsFontRenderer = nullptr;
    jclass clsTextureManager = nullptr;
    jclass clsGuiIngame = nullptr;
    jclass clsPlayerTabOverlay = nullptr;
    jclass clsIChatComponent = nullptr;
    jclass clsNetHandlerPlayClient = nullptr;
    jclass clsNetworkPlayerInfo = nullptr;
    jclass clsCollection = nullptr;
    jclass clsIterator = nullptr;
    jclass clsGameProfile = nullptr;
    jclass clsUUID = nullptr;
    jclass clsGlStateManager = nullptr;

    jmethodID midGetMinecraft = nullptr;
    jmethodID midScaledResCtor = nullptr;
    jmethodID midScaledWidth = nullptr;
    jmethodID midScaledHeight = nullptr;
    jmethodID midFontWidth = nullptr;
    jmethodID midDrawStringShadow = nullptr;
    jmethodID midGuiDrawScaledCustomRect = nullptr;
    jmethodID midGuiDrawRect = nullptr;
    jmethodID midBindTexture = nullptr;
    jmethodID midGetPlayerInfoMap = nullptr;
    jmethodID midCollectionIterator = nullptr;
    jmethodID midIteratorHasNext = nullptr;
    jmethodID midIteratorNext = nullptr;
    jmethodID midGetLocationSkin = nullptr;
    jmethodID midGetGameProfile = nullptr;
    jmethodID midGetProfileName = nullptr;
    jmethodID midGetProfileId = nullptr;
    jmethodID midUUIDToString = nullptr;
    jmethodID midGetPing = nullptr;
    jmethodID midGetDisplayName = nullptr;
    jmethodID midGetPlayerTeam = nullptr;
    jmethodID midChatComponentGetText = nullptr;
    jmethodID midChatComponentGetFormattedText = nullptr;
    jmethodID midGsmResetColor = nullptr;
    jmethodID midGsmEnableTexture2D = nullptr;
    jmethodID midGsmEnableBlend = nullptr;
    jmethodID midGsmBlendFunc = nullptr;
    jmethodID midGsmEnableDepth = nullptr;
    jmethodID midGsmDepthMask = nullptr;

    jfieldID fidDisplayWidth = nullptr;
    jfieldID fidDisplayHeight = nullptr;
    jfieldID fidFontRenderer = nullptr;
    jfieldID fidTextureManager = nullptr;
    jfieldID fidIngameGui = nullptr;
    jfieldID fidGuiTabOverlay = nullptr;
    jfieldID fidTabFooter = nullptr;
    jfieldID fidThePlayer = nullptr;
    jfieldID fidSendQueue = nullptr;
    jfieldID fidLocationFontTexture = nullptr;

    bool init(JNIEnv* env);
    void shutdown(JNIEnv* env);
};

class OverlayController {
public:
    bool initialize(JNIEnv* env);
    void shutdown(JNIEnv* env);
    bool isInitialized() const { return cache_.initialized; }
    void applyColumnOrder(const std::vector<std::string>& orderedIds);
    void setApiKeys(const std::string& hypixelApiKey, const std::string& seraphApiKey,
                    const std::string& winstreakApiKey = "",
                    bool vegaProxy = false);
    void setOverlayOpacityPercent(int opacityPercent);
    void setOverlayThemeColorRgb(int rgb);
    void setResourceHeaderEnabled(bool enabled);
    void setResourceDiamondsEnabled(bool enabled);
    void setResourceEmeraldsEnabled(bool enabled);
    void setResourceDisplayLocation(ResourceDisplayLocation location);
    bool isResourceHeaderEnabled() const { return resourceHeaderEnabled_.load(std::memory_order_relaxed); }
    bool isResourceOnScoreboard() const {
        return resourceDisplayLocation_.load(std::memory_order_relaxed) == (int)ResourceDisplayLocation::Scoreboard;
    }
    void addPlayersFromWhoList(const std::vector<std::string>& players);
    void mergePlayersFromWhoList(const std::vector<std::string>& players);
    void resetWhoSession();
    void clearApiAllowlist();
    void clearPrequeueGhosts();
    void resetOnWorldLoad();
    void stripBedDisconnectTimersFromScoreboard(JNIEnv* env, jobject mcInstance);
    void addOnlinePlayerForApi(const std::string& player, int chatNameColor = -1);
    void addSpeakerUuidForApi(const std::string& player, const std::string& playerUuid);

    void publishSnapshot(const OverlaySnapshot& snapshot);
    void renderFrame(JNIEnv* env);

    OverlaySnapshot buildBasicSnapshot(int screenWidth, int screenHeight) const;
    void tickSession(JNIEnv* env, jobject mcInstance);
    void renderCustomTab(JNIEnv* env, jobject mcInstance);
    void renderScoreboardResources(JNIEnv* env, jobject mcInstance);
    void renderScoreboardBedDisconnectTimers(JNIEnv* env, jobject mcInstance);
    void clearScoreboardResourceLines(JNIEnv* env, jobject mcInstance);
    void syncVanillaGuiGlState(JNIEnv* env) const;

private:
    OverlaySnapshot buildSnapshotFromMinecraft(JNIEnv* env, jobject mcInstance) const;
    void renderSnapshot(JNIEnv* env, const OverlaySnapshot& snapshot);
    void releaseSnapshotResources(JNIEnv* env, OverlaySnapshot& snapshot) const;
    void drawStringShadow(JNIEnv* env, jobject fontRenderer, const std::string& text, float x, float y, int color) const;
    int measureString(JNIEnv* env, jobject fontRenderer, const std::string& text) const;
    LayoutResult computeLayout(const OverlaySnapshot& snapshot) const;
    void renderBackgroundLayer(JNIEnv* env, const LayoutResult& layout);
    void renderHeader(JNIEnv* env, jobject fontRenderer, const OverlaySnapshot& snapshot, const LayoutResult& layout);
    void renderColumnTitles(JNIEnv* env, jobject fontRenderer, const OverlaySnapshot& snapshot, const LayoutResult& layout);
    void renderPlayerRows(JNIEnv* env, jobject fontRenderer, const OverlaySnapshot& snapshot, const LayoutResult& layout);
    void renderSkinHeads(JNIEnv* env, jobject textureManager, const OverlaySnapshot& snapshot, const LayoutResult& layout);
    void renderFooter(JNIEnv* env, jobject fontRenderer, const OverlaySnapshot& snapshot, const LayoutResult& layout);
    void restoreVanillaGuiGlState(JNIEnv* env, jobject fontRenderer, jobject textureManager) const;

    JniCache cache_;
    ColumnManager columnManager_;
    SnapshotStore snapshots_;
    LayoutConfig layoutConfig_;
    std::atomic<int> overlayOpacityPercent_{60};
    std::atomic<int> overlayThemeColorRgb_{0x0F1316};
    std::atomic<bool> resourceHeaderEnabled_{true};
    std::atomic<bool> resourceDiamondsEnabled_{true};
    std::atomic<bool> resourceEmeraldsEnabled_{true};
    std::atomic<int> resourceDisplayLocation_{(int)ResourceDisplayLocation::Tab};
    mutable bool scoreboardResourceLinesActive_ = false;
    mutable std::string lastResourceScoreLine_;
    mutable OverlaySnapshot cachedSnapshot_{};
    mutable bool cachedSnapshotValid_ = false;
};

bool QueryPlayerStatsNow(const std::string& playerName,
                         const std::string& hypixelApiKey,
                         const std::string& seraphApiKey,
                         const std::string& lookupMode,
                         PlayerLookupStats& out);

PlayerLookupChatOutput FormatPlayerLookupChatMessage(const PlayerLookupStats& st);

// Re-sync GlStateManager + font texture after raw GL tab rendering.
void RestoreVanillaGuiGlState(JNIEnv* env);

} // namespace taboverlay
