#include "roster.h"

#include <cctype>

namespace vigil::detection {

const char* entityKindName(EntityKind k) {
    switch (k) {
        case EntityKind::Npc: return "NPC";
        case EntityKind::Duplicate: return "Duplicate";
        default: return "Player";
    }
}

int uuidVersionOf(const std::string& uuid) {
    std::string hex;
    for (char c : uuid) {
        if (c == '-') continue;
        if (!std::isxdigit(static_cast<unsigned char>(c))) return 0;
        hex.push_back(c);
    }
    if (hex.size() != 32) return 0;
    const char v = hex[12];
    return std::isdigit(static_cast<unsigned char>(v)) ? v - '0' : 0;
}

bool isValidPlayerName(const std::string& name) {
    if (name.empty() || name.size() > 16) return false;
    for (char c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    }
    return true;
}

namespace {
bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}
}  // namespace

EntityVerdict classifyEntity(const PlayerFrame& f, const EntityObservation& o, const std::string& localName, bool replay) {
    EntityVerdict v;
    if (f.isLocal) return v;
    // Hypixel creates its NPCs with version-2 UUIDs; real accounts are v4 and nicks are v1.
    if (uuidVersionOf(f.uuid) == 2) {
        v.kind = EntityKind::Npc;
        v.reason = "Server NPC";
        return v;
    }
    // NPC names carry spaces, brackets or formatting; a real account's name never does. The
    // adapter names entities "#<id>" only until their profile is read.
    if (!f.name.empty() && f.name[0] != '#' && !isValidPlayerName(f.name)) {
        v.kind = EntityKind::Npc;
        v.reason = "Not a player name";
        return v;
    }
    if (!localName.empty() && equalsIgnoreCase(f.name, localName)) {
        v.kind = EntityKind::Npc;
        v.reason = "Uses your name";
        return v;
    }
    // Absent from the tab list for a while AND behaving like a prop or a hidden bot: never moved,
    // or invisible. Either alone is ordinary (AFK players, invisibility potions).
    if (!replay && f.tabKnown && o.ticksTracked >= kUnlistedGraceTicks && o.ticksUnlisted >= kUnlistedGraceTicks &&
        (o.maxDisplacement < 0.5 || o.everInvisible)) {
        v.kind = EntityKind::Npc;
        v.reason = o.everInvisible ? "Hidden bot (not in the player list)" : "Not in the player list";
        return v;
    }
    return v;
}

}  // namespace vigil::detection
