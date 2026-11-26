#pragma once
// Vigil game layer — chat presentation of detections and match briefings.
//
// Chat lines use the suspicion language's shapes (src/ui/suspicion.h) next to names, so they read
// without color — under rank colors, chat mods and color-blindness: ◇ elevated, ◆ high, ⚠ critical,
// ✖ confirmed by you, ◎ watching, ≈ nicked. Colors only reinforce.

#include "../detection/session.h"

#include <string>

namespace vigil::game {

struct ChatMarks {
    std::string glyph;        // tier glyph ("" below Elevated)
    bool confirmed = false;   // the user's verdict
    bool watched = false;
};

// Builds the Minecraft JSON chat component for a detection alert:
//   [Vigil] ◆ Name · KillAura 82%
// Hovering shows the evidence that produced the detection; clicking optionally runs /wdr.
std::string buildAlertJson(const detection::Alert& alert, const std::string& checkName, bool clickToReport, const ChatMarks& marks);

// Plain legacy-formatted line (fallback when JSON components are unavailable).
std::string buildAlertLegacy(const detection::Alert& alert, const std::string& checkName, const ChatMarks& marks);

// Prefix for Vigil-originated chat lines.
std::string chatPrefix();

}  // namespace vigil::game
