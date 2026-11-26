#pragma once
// Vigil UI — page interfaces shared by the shell and the individual screens.
//
// Information architecture (PLAN.md §3, §3.4):
//   MONITOR      Overview
//   INVESTIGATE  Players (live: profile tabs Summary · Evidence · Behavior · Identity)
//                Records (yours, kept across sessions: Confirmed · Watching · Exempt · Saved)
//   CONFIGURE    Checks · Utilities · Settings
//   SYSTEM       Status (rail footer)

#include "../detection/engine.h"
#include "app.h"
#include "motion.h"
#include "suspicion.h"
#include "widgets.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace vigil::ui {

enum class Page { Overview, Players, Records, Checks, Utilities, Settings, Status };
inline constexpr int kPageCount = 7;
enum class ProfileTab { Summary, Evidence, Behavior, Identity };

const char* pageTitle(Page p);
const char* pageSection(Page p);
const char* pagePurpose(Page p);
Icon pageIcon(Page p);

struct NavEntry {
    Page page = Page::Overview;
    std::string player;
    ProfileTab tab = ProfileTab::Summary;
    int settingsSection = 0;
    int selectedCheck = 0;
};

struct AppState {
    Page page = Page::Overview;
    int navDirection = 1;                 // +1 moving down the rail, -1 moving up (transitions)

    // Players.
    std::string selectedPlayer;           // PlayerRecord key
    ProfileTab profileTab = ProfileTab::Summary;
    ProfileTab profileTabShown = ProfileTab::Summary;
    double profileTabChangedAt = -10.0;
    std::string playerSearch;
    int rosterScope = 0;                  // 0 this game, 1 session, 2 marked, 3 nicked
    int playersSort = 0;                  // 0 suspicion, 1 recent, 2 name
    bool earlierOpen = false;             // Session scope: "Earlier this session" expanded
    std::uint64_t selectedEvidence = 0;
    std::uint64_t selectedDetection = 0;
    int evidenceFilterCheck = -1;         // -1 = all checks
    int timelineWindow = 1;               // 0 = 1 min, 1 = 5 min, 2 = 15 min, 3 = session
    int inspectorTab = 0;                 // 0 = reasoning, 1 = debug
    bool inspectorDetails = false;        // "Details" disclosure in the inspector

    // Confirm flow (profile header popover).
    std::string confirmKey;               // player whose confirm popover is open
    std::set<std::string> confirmReasons; // reasons ticked in the popover
    std::string moreKey;                  // player whose "More" menu is open

    // Records.
    int recordsTab = 0;                   // 0 confirmed, 1 watching, 2 exempt, 3 saved
    std::string recordsSearch;
    std::string selectedRecord;           // mark key (tabs 0–2) or snapshot id (tab 3)
    bool addOpen = false;                 // "Add player" popover
    std::string addQuery;
    ProfileTab snapshotTab = ProfileTab::Summary;

    // Checks.
    int selectedCheck = 0;
    std::string checkSearch;

    // Settings.
    int settingsSection = 0;
    bool showAdvanced = false;

    // Attention.
    std::set<std::string> unseenPlayers;  // players with detections not yet looked at
    double detectionPulseAt = -10.0;

    // History (Back).
    std::vector<NavEntry> history;

    // Deep links: scroll to and flash a setting row.
    std::string focusSetting;
    double focusAt = -10.0;

    // Overlays.
    bool searchOpen = false;
    double searchChangedAt = 0.0;
    std::string searchQuery;
    int searchIndex = 0;
    bool onboardingOpen = false;
    int onboardingStep = 0;
    double onboardingStepAt = 0.0;
};

struct PageContext {
    Ui& ui;
    detection::Engine& engine;
    Host* host;
    AppState& state;
    double nowMs;                    // engine clock (ms) for "ago" labels
    // Session start for clock labels. A saved profile carries its own session (records page).
    double sessionStartOverride = -1.0;
    bool snapshot = false;           // drawing a saved profile: read-only, no live actions

    double sessionStart() const { return sessionStartOverride >= 0.0 ? sessionStartOverride : engine.session().startMs(); }

    NavEntry current() const {
        return {state.page, state.selectedPlayer, state.profileTab, state.settingsSection, state.selectedCheck};
    }
    void navigate(Page p) {
        if (p == state.page) return;
        state.history.push_back(current());
        if (state.history.size() > 32) state.history.erase(state.history.begin());
        state.navDirection = static_cast<int>(p) >= static_cast<int>(state.page) ? 1 : -1;
        state.page = p;
    }
    // Opens a record (mark key or saved-profile id) on the Records page.
    void openRecord(const std::string& key, int tab) {
        if (state.page != Page::Records || state.selectedRecord != key) {
            state.history.push_back(current());
            if (state.history.size() > 32) state.history.erase(state.history.begin());
        }
        state.navDirection = static_cast<int>(Page::Records) >= static_cast<int>(state.page) ? 1 : -1;
        state.page = Page::Records;
        state.recordsTab = tab;
        state.selectedRecord = key;
    }
    void openPlayer(const std::string& key, ProfileTab tab = ProfileTab::Summary) {
        if (state.page != Page::Players || state.selectedPlayer != key) {
            state.history.push_back(current());
            if (state.history.size() > 32) state.history.erase(state.history.begin());
        }
        state.navDirection = static_cast<int>(Page::Players) >= static_cast<int>(state.page) ? 1 : -1;
        state.page = Page::Players;
        state.selectedPlayer = key;
        state.profileTab = tab;
        state.selectedEvidence = 0;
        state.selectedDetection = 0;
        state.unseenPlayers.erase(key);
    }
    bool back() {
        if (state.history.empty()) return false;
        const NavEntry e = state.history.back();
        state.history.pop_back();
        state.navDirection = static_cast<int>(e.page) >= static_cast<int>(state.page) ? 1 : -1;
        state.page = e.page;
        state.selectedPlayer = e.player;
        state.profileTab = e.tab;
        state.settingsSection = e.settingsSection;
        state.selectedCheck = e.selectedCheck;
        return true;
    }
};

// Pages.
void drawOverview(PageContext& ctx, const Rect& area);
void drawPlayers(PageContext& ctx, const Rect& area);
void drawRecords(PageContext& ctx, const Rect& area);
void drawChecks(PageContext& ctx, const Rect& area);
void drawUtilities(PageContext& ctx, const Rect& area);
void drawSettings(PageContext& ctx, const Rect& area);
void drawStatus(PageContext& ctx, const Rect& area);

// Profile tabs (page_players.cpp / page_evidence.cpp). The Records page reuses them for saved
// profiles (ctx.snapshot): the same investigation surface, read-only.
void drawEvidenceTab(PageContext& ctx, const detection::PlayerRecord& p, const Rect& area);
void drawSummaryTab(PageContext& ctx, const detection::PlayerRecord& p, const Rect& area);
void drawBehaviorTab(PageContext& ctx, const detection::PlayerRecord& p, const Rect& area);
// "Add player" picker shared by Records: suggestions from this session, or any valid username.
// Returns true when a player was chosen; `key` is their record key (empty when added by name).
bool playerPicker(PageContext& ctx, const Rect& anchor, std::string& query, std::string& name, std::string& key);
// Brief result of a Records action (save, export, import), drawn at the bottom of `rc`.
void actionBanner(PageContext& ctx, const Rect& rc);
// Status → Diagnostics: a plain-text report without player names (page_status.cpp).
std::string buildDiagnosticsReport(const PageContext& ctx);

// Overlays.
void drawSearch(PageContext& ctx, const Rect& window);          // search.cpp
void drawOnboarding(PageContext& ctx, const Rect& window);      // onboarding.cpp
// Reveals a setting: navigates to the page that owns it and flashes its row.
void revealSetting(PageContext& ctx, const std::string& key);
// Settings sections (Settings page navigation) and the section that shows a setting group.
int settingsSectionCount();
const char* settingsSectionName(int i);
int settingsSectionForGroup(const std::string& group);
int settingsSectionByName(const std::string& name);

// Health summary used by the rail card, Overview and Status.
struct Health {
    enum Level { Active, Standby, Degraded, Paused } level = Standby;
    std::string title;      // "Active", "Standing by", ...
    std::string detail;     // one line
    int issues = 0;
};
Health computeHealth(const PageContext& ctx);
Color healthColor(const Theme& t, Health::Level l);

// Shared bits.
Icon checkIcon(const std::string& checkId);
Icon categoryIcon(detection::CheckCategory c);
std::string agoLabel(double nowMs, double thenMs);
std::string epochAgoLabel(double epochMs);
// Draws the page title with its purpose on the same line; returns the y where content starts.
float pageHeader(PageContext& ctx, const Rect& area, std::string_view title, std::string_view subtitle);
// Players sorted for lists (suspicion desc, present first).
std::vector<const detection::PlayerRecord*> sortedPlayers(const detection::Engine& e, int sortMode, const std::string& filter,
                                                          bool includeLocal);
void sparkline(Ui& ui, const Rect& rc, const std::vector<detection::ConfidencePoint>& pts, double t0, double t1, Color c,
               bool fill = true);
// Confidence change over the last `windowMs` (points).
float confidenceTrend(const std::vector<detection::ConfidencePoint>& history, float current, double nowMs, double windowMs = 60e3);
// Small "+12" / "−8" trend label with an arrow; returns its width. Draws nothing for |delta| < 1.
float trendLabel(Ui& ui, float x, float y, float delta, bool alignRight = false);
// The player's halo (src/ui/suspicion.h): circular face, confidence ring, marks badge. Dimmed
// while the player is out of view. `cutout` is the surface behind the badge (default: card).
void playerAvatar(PageContext& ctx, const detection::PlayerRecord& p, const Rect& rc, Color cutout = {0, 0, 0, 0});
// "In this game" / "Left" / "Earlier game" line for a player record.
std::string presenceLabel(const PageContext& ctx, const detection::PlayerRecord& p);
std::string keyLabel(const std::string& settingKey);   // name of the key bound to a Key setting

}  // namespace vigil::ui
