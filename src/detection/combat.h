#pragma once
// Vigil detection framework — melee hit attribution.
//
// The client never sees "player A attacked player B". It sees B's hurt animation start and the
// arm swings of nearby players. The resolver attributes each hurt to the swinging player whose
// aim best explains it, records how certain that attribution is, and waits a few ticks for
// late position/rotation packets before publishing the event to checks.

#include "types.h"

#include <cstdint>
#include <vector>

namespace vigil::detection {

class PlayerTrack;

struct CombatEvent {
    std::uint64_t id = 0;
    Tick tick = 0;            // tick the victim's hurt animation started
    double timeMs = 0.0;
    int attacker = -1;        // entity ids
    int victim = -1;
    int candidates = 0;       // swinging players that could explain the hit
    float certainty = 0.f;    // 0..1
    double reach = 0.0;       // latency-compensated eye→hitbox distance (minimum over window)
    double reachRaw = 0.0;    // same-tick distance, uncompensated
    double aimOutside = 0.0;  // degrees between look direction and victim hitbox (min over window)
    double yawError = 0.0;    // |yaw to victim center − attacker yaw| at the hit tick
    float attackerYaw = 0.f;
    float attackerPitch = 0.f;
    Vec3 attackerEye;
    Vec3 victimPos;
    Vec3 knockbackDir;        // horizontal unit vector attacker → victim
    bool victimOnGround = false;
    bool attackerSprinting = false;
    int attackerItem = -1;
};

struct CombatSettings {
    int swingWindowTicks = 2;     // a hurt may follow the swing by this many ticks
    double candidateRange = 6.0;  // eye→hitbox, blocks
    int settleTicks = 3;          // wait for late packets before evaluating
    int compensationTicks = 5;    // victim position history used for reach
};

class CombatResolver {
public:
    // Call once per tick after all tracks were updated. `tracks` must contain every player.
    void update(Tick tick, double timeMs, const std::vector<PlayerTrack*>& tracks, const CombatSettings& s);
    // Events that became final this tick.
    const std::vector<CombatEvent>& ready() const { return ready_; }
    void reset() { pending_.clear(); ready_.clear(); }

private:
    struct Pending {
        CombatEvent ev;
        Tick resolveAt = 0;
    };
    void finalize(Pending& p, const std::vector<PlayerTrack*>& tracks, const CombatSettings& s);

    std::vector<Pending> pending_;
    std::vector<CombatEvent> ready_;
    std::uint64_t nextId_ = 1;
};

}  // namespace vigil::detection
