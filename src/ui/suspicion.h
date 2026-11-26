#pragma once
// Vigil UI — the suspicion language.
//
// One representation of "how suspicious is this player" and "what have I concluded about them",
// shared by every surface: roster, profile, overview, notifications, match briefing, search, the
// chat lines Vigil prints and the world highlight (PLAN.md §9.4–9.5).
//
//   Vigil's confidence   the halo: a ring around a circular face whose arc length is the
//                        confidence and whose weight and glow grow with the tier. Shape carries
//                        the meaning; color reinforces it.
//   The user's verdict   a badge on the halo in the theme's verdict color (never part of the
//                        severity scale): a shield for "Confirmed by you", a ring for "Watching".

#include "../detection/engine.h"
#include "widgets.h"

#include <string>
#include <vector>

namespace vigil::ui {

enum class Tier { Clear, Low, Elevated, High, Critical };

Tier tierOf(float confidence);
const char* tierName(Tier t);           // "No concern", "Low", "Elevated", "High", "Critical"
Color tierColor(const Theme& t, Tier tier);
// Shapes for surfaces Vigil cannot draw on (chat). Distinct shapes, so they read without color.
const char* tierGlyph(Tier t);          // none below Elevated; ◇ Elevated, ◆ High, ⚠ Critical
inline constexpr const char* kGlyphWatching = "\xE2\x97\x8E";   // ◎
inline constexpr const char* kGlyphConfirmed = "\xE2\x9C\x96";  // ✖
inline constexpr const char* kGlyphNicked = "\xE2\x89\x88";     // ≈

struct PlayerMarks {
    bool watched = false;
    bool confirmed = false;
    bool nicked = false;
};
PlayerMarks marksOf(const detection::Engine& e, const std::string& key);

struct HaloStyle {
    float confidence = 0.f;      // Vigil's confidence 0..100
    PlayerMarks marks;
    bool present = true;         // dimmed while the player is out of view
    double pulseAt = -10.0;      // one attention pulse (new detection)
    bool badge = true;           // draw the watch / verdict badge
    Color cutout{0, 0, 0, 0};    // surface behind the badge (default: card surface)
};
// Circular face inside the confidence halo. `rc` is the square holding the whole component.
void playerHalo(Ui& ui, Host* host, Id id, const std::string& key, const std::string& name, const Rect& rc, const HaloStyle& s);

// "High · 78%" — the tier word in its color, the number secondary and tabular. Returns the width.
float suspicionLabel(Ui& ui, float x, float y, float confidence, Align align = Align::Left, bool percent = true);
float suspicionLabelWidth(Ui& ui, float confidence, bool percent = true);
// Capsule for the user's marks ("Confirmed by you" / "Watching"). Returns the width (0 = nothing).
float markChip(Ui& ui, float x, float y, const PlayerMarks& m);
float markChipWidth(Ui& ui, const PlayerMarks& m);

// ── Match briefing ───────────────────────────────────────────────────────────────────────────
struct BriefingEntry {
    std::string key, name;
    float suspicion = 0.f;
    PlayerMarks marks;
    std::string reason;          // why they are listed ("Confirmed by you · KillAura")
    int priority = 0;            // lower first
};
struct Briefing {
    int players = 0;             // players in this game
    std::vector<BriefingEntry> entries;
};
// Who in the current game is worth knowing about: confirmed, watched, detected in earlier
// sessions, carried high suspicion from an earlier game, nicked.
Briefing buildBriefing(const detection::Engine& e);

}  // namespace vigil::ui
