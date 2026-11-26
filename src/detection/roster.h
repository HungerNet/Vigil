#pragma once
// Vigil detection framework — who counts as a player.
//
// `world.playerEntities` holds every EntityPlayer: real players, Hypixel NPCs (shopkeepers, lobby
// characters, the "your profile" NPC that wears your name), Watchdog bots and replay copies. Only
// real players may reach checks, records, notifications, highlights and investigations. The
// classifier is deliberately conservative: a real player misread as an NPC loses detection, so
// every NPC rule needs a strong signal (see PLAN.md §6.6).

#include "types.h"

#include <string>

namespace vigil::detection {

enum class EntityKind : std::uint8_t { Player, Npc, Duplicate };

const char* entityKindName(EntityKind k);

// Version nibble of a textual UUID (dashed or not); 0 when malformed.
int uuidVersionOf(const std::string& uuid);
// 1–16 characters of [A-Za-z0-9_].
bool isValidPlayerName(const std::string& name);

struct EntityObservation {
    int ticksTracked = 0;        // frames this entity has been observed
    int ticksUnlisted = 0;       // consecutive frames absent from the tab list (when the tab list is known)
    double maxDisplacement = 0;  // farthest it has moved from where it was first seen (blocks)
    bool everInvisible = false;
};

struct EntityVerdict {
    EntityKind kind = EntityKind::Player;
    std::string reason;          // short, shown to the user ("Server NPC", "Not in the player list")
};

// `localName` is the local player's name (case-insensitive comparison). `replay`: tab-list rules
// are not applied in replays, where the list does not describe the recorded players.
EntityVerdict classifyEntity(const PlayerFrame& f, const EntityObservation& o, const std::string& localName, bool replay);

// Ticks an entity must be unlisted (and tracked) before the tab-list rule may apply. Hypixel
// lists players slightly before or after spawning them.
inline constexpr int kUnlistedGraceTicks = 40;

}  // namespace vigil::detection
