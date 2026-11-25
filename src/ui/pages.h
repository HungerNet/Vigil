#pragma once
// Vigil UI — page interfaces shared by the shell and the individual screens.

#include "../detection/engine.h"
#include "app.h"
#include "widgets.h"

#include <cstdint>
#include <string>

namespace vigil::ui {

enum class Page { Overview, Checks, Evidence, Players, Settings };

struct AppState {
    Page page = Page::Overview;
    int selectedCheck = 0;
    std::string checkSearch;
    std::string selectedPlayer;      // PlayerRecord key
    std::string playerSearch;
    std::uint64_t selectedEvidence = 0;
    std::uint64_t selectedDetection = 0;
    int evidenceFilterCheck = -1;    // -1 = all checks
    int timelineWindow = 1;          // 0 = 1 min, 1 = 5 min, 2 = 15 min, 3 = session
    int inspectorTab = 0;            // 0 = evidence, 1 = debug
    int settingsSection = 0;
    bool showAdvanced = false;
    int playersSort = 0;             // 0 suspicion, 1 recent, 2 name
    std::uint64_t lastSeenAlert = 0;
    int unseenDetections = 0;
};

struct PageContext {
    Ui& ui;
    detection::Engine& engine;
    Host* host;
    AppState& state;
    double nowMs;                    // engine clock (ms) for "ago" labels
    void navigate(Page p) { state.page = p; }
    void openPlayer(const std::string& key) {
        state.selectedPlayer = key;
        state.selectedEvidence = 0;
        state.selectedDetection = 0;
        state.page = Page::Evidence;
    }
};

void drawOverview(PageContext& ctx, const Rect& area);
void drawChecks(PageContext& ctx, const Rect& area);
void drawEvidence(PageContext& ctx, const Rect& area);
void drawPlayers(PageContext& ctx, const Rect& area);
void drawSettings(PageContext& ctx, const Rect& area);

// Shared bits.
Icon checkIcon(const std::string& checkId);
Icon categoryIcon(detection::CheckCategory c);
std::string agoLabel(double nowMs, double thenMs);
void pageHeader(PageContext& ctx, const Rect& area, std::string_view title, std::string_view subtitle);
// Players sorted for lists (suspicion desc, present first).
std::vector<const detection::PlayerRecord*> sortedPlayers(const detection::Session& s, int sortMode, const std::string& filter,
                                                          bool includeLocal);
void sparkline(Ui& ui, const Rect& rc, const std::vector<detection::ConfidencePoint>& pts, double t0, double t1, Color c,
               bool fill = true);

}  // namespace vigil::ui
