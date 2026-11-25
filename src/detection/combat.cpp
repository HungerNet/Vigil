#include "combat.h"

#include "track.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vigil::detection {
namespace {

PlayerTrack* findTrack(const std::vector<PlayerTrack*>& tracks, int id) {
    for (PlayerTrack* t : tracks) if (t && t->entityId() == id) return t;
    return nullptr;
}

Vec3 eyeOf(const PlayerFrame& f) {
    return {f.pos.x, f.pos.y + (f.sneaking ? mc::kEyeHeightSneaking : mc::kEyeHeight), f.pos.z};
}

// Index k (ticks back) in `t` whose tick is closest to `tick`.
int indexForTick(const PlayerTrack& t, Tick tick) {
    const Tick now = t.tickAt(0);
    const int k = static_cast<int>(now - tick);
    return std::clamp(k, 0, std::max(0, t.size() - 1));
}

}  // namespace

void CombatResolver::update(Tick tick, double timeMs, const std::vector<PlayerTrack*>& tracks, const CombatSettings& s) {
    ready_.clear();

    // 1) New hurts become pending events with their attribution decided now (swings are known).
    for (PlayerTrack* victim : tracks) {
        if (!victim || victim->empty() || !victim->hurtStarted()) continue;
        const PlayerFrame& vf = victim->cur();
        const Box vbox = Box::player(vf.pos, mc::kHitboxBorder);

        PlayerTrack* best = nullptr;
        double bestAngle = std::numeric_limits<double>::max();
        int candidates = 0;
        for (PlayerTrack* att : tracks) {
            if (!att || att == victim || att->empty()) continue;
            if (tick - att->lastSwingTick() > s.swingWindowTicks) continue;
            const PlayerFrame& af = att->cur();
            if (af.dead || af.riding) continue;
            if (mc::isRanged(af.heldItem)) continue;  // rods/bows swing on use; hits are projectile
            const Vec3 eye = eyeOf(af);
            if (vbox.distanceTo(eye) > s.candidateRange) continue;
            ++candidates;
            const double angle = angleOutsideBox(eye, af.yaw, af.pitch, vbox);
            const double centerErr = std::fabs(wrapDegrees(yawTowards(eye, vbox.center()) - af.yaw));
            const double score = angle * 4.0 + centerErr;
            if (score < bestAngle) { bestAngle = score; best = att; }
        }
        if (!best) continue;

        Pending p;
        p.ev.id = nextId_++;
        p.ev.tick = tick;
        p.ev.timeMs = timeMs;
        p.ev.attacker = best->entityId();
        p.ev.victim = victim->entityId();
        p.ev.candidates = candidates;
        p.ev.certainty = candidates == 1 ? 1.f : 0.45f;
        p.ev.victimOnGround = victim->has(1) ? victim->back(1).onGround : vf.onGround;
        p.ev.attackerSprinting = best->cur().sprinting;
        p.ev.attackerItem = best->cur().heldItem;
        p.resolveAt = tick + std::max(0, s.settleTicks);
        pending_.push_back(p);
    }

    // 2) Finalize events whose settle window elapsed.
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (tick >= it->resolveAt) {
            finalize(*it, tracks, s);
            if (it->ev.attacker >= 0) ready_.push_back(it->ev);
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
}

void CombatResolver::finalize(Pending& p, const std::vector<PlayerTrack*>& tracks, const CombatSettings& s) {
    PlayerTrack* att = findTrack(tracks, p.ev.attacker);
    PlayerTrack* vic = findTrack(tracks, p.ev.victim);
    if (!att || !vic || att->empty() || vic->empty()) { p.ev.attacker = -1; return; }

    const int aHit = indexForTick(*att, p.ev.tick);
    const int vHit = indexForTick(*vic, p.ev.tick);
    const PlayerFrame& afHit = att->back(aHit);
    const PlayerFrame& vfHit = vic->back(vHit);

    p.ev.attackerYaw = afHit.yaw;
    p.ev.attackerPitch = afHit.pitch;
    p.ev.attackerEye = eyeOf(afHit);
    p.ev.victimPos = vfHit.pos;
    p.ev.reachRaw = Box::player(vfHit.pos, mc::kHitboxBorder).distanceTo(p.ev.attackerEye);
    p.ev.yawError = std::fabs(wrapDegrees(yawTowards(p.ev.attackerEye, Box::player(vfHit.pos).center()) - afHit.yaw));

    // Attacker samples: one tick before the hit through the settle window (late movement packets).
    // Victim samples: compensation window before the hit through one tick after.
    double minReach = std::numeric_limits<double>::max();
    double minAim = std::numeric_limits<double>::max();
    for (int ka = std::max(0, aHit - s.settleTicks); ka <= std::min(att->size() - 1, aHit + 1); ++ka) {
        const PlayerFrame& af = att->back(ka);
        const Vec3 eye = eyeOf(af);
        for (int kv = std::max(0, vHit - 1); kv <= std::min(vic->size() - 1, vHit + s.compensationTicks); ++kv) {
            const Box box = Box::player(vic->back(kv).pos, mc::kHitboxBorder);
            minReach = std::min(minReach, box.distanceTo(eye));
            minAim = std::min(minAim, angleOutsideBox(eye, af.yaw, af.pitch, box));
        }
    }
    p.ev.reach = minReach;
    p.ev.aimOutside = minAim;

    Vec3 dir = (vfHit.pos - afHit.pos).horizontal();
    const double len = dir.horizontalLength();
    p.ev.knockbackDir = len > 1e-6 ? dir * (1.0 / len) : Vec3{};
}

}  // namespace vigil::detection
