#pragma once
// Vigil game layer — chat presentation of detections.

#include "../detection/session.h"

#include <string>

namespace vigil::game {

// Builds the Minecraft JSON chat component for a detection alert:
//   [Vigil] Name · KillAura 82%
// Hovering shows the evidence that produced the detection; clicking optionally runs /wdr.
std::string buildAlertJson(const detection::Alert& alert, const std::string& checkName, bool clickToReport);

// Plain legacy-formatted line (fallback when JSON components are unavailable).
std::string buildAlertLegacy(const detection::Alert& alert, const std::string& checkName);

// Prefix for Vigil-originated chat lines.
std::string chatPrefix();

}  // namespace vigil::game
