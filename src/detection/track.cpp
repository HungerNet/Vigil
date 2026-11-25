#include "track.h"

#include <algorithm>

namespace vigil::detection {

const PlayerFrame& PlayerTrack::back(int k) const {
    static const PlayerFrame empty;
    if (frames_.empty()) return empty;
    k = std::clamp(k, 0, size() - 1);
    return frames_[frames_.size() - 1 - static_cast<std::size_t>(k)];
}

Tick PlayerTrack::tickAt(int k) const {
    if (ticks_.empty()) return 0;
    k = std::clamp(k, 0, size() - 1);
    return ticks_[ticks_.size() - 1 - static_cast<std::size_t>(k)];
}

double PlayerTrack::horizontalSpeed(int window) const {
    if (size() < 2) return 0.0;
    window = std::clamp(window, 1, size() - 1);
    const double ticks = static_cast<double>(tickAt(0) - tickAt(window));
    if (ticks <= 0.0) return 0.0;
    return (back(0).pos - back(window).pos).horizontalLength() / ticks;
}

Vec3 PlayerTrack::displacement(int window) const {
    if (size() < 2) return {};
    window = std::clamp(window, 1, size() - 1);
    return back(0).pos - back(window).pos;
}

Vec3 PlayerTrack::velocity(int k, int span) const {
    if (size() < 2) return {};
    span = std::max(1, span);
    const int a = std::clamp(k, 0, size() - 1);
    const int b = std::clamp(k + span, 0, size() - 1);
    const double ticks = static_cast<double>(tickAt(a) - tickAt(b));
    if (ticks <= 0.0) return {};
    return (back(a).pos - back(b).pos) * (1.0 / ticks);
}

void PlayerTrack::push(const PlayerFrame& f, Tick tick, double /*timeMs*/) {
    const PlayerFrame* prev = frames_.empty() ? nullptr : &frames_.back();
    const Tick prevTick = ticks_.empty() ? tick : ticks_.back();
    if (frames_.empty()) firstTick_ = tick;
    entityId_ = f.entityId;

    // Swing start: progress reset (new swing over an old one) or animation began.
    swingStarted_ = false;
    if (prev) {
        const bool began = f.swingInProgress && !prev->swingInProgress;
        const bool restarted = f.swingInProgress && prev->swingInProgress && f.swingProgress < prev->swingProgress;
        swingStarted_ = began || restarted;
    }
    if (swingStarted_) {
        lastSwingTick_ = tick;
        swingTicks_.push_back(tick);
        while (swingTicks_.size() > 120) swingTicks_.pop_front();
    }

    // Hurt start: hurtTime jumps upward (set to 10 by the entity status packet).
    hurtStarted_ = prev && f.hurtTime > prev->hurtTime && f.hurtTime >= mc::kHurtTimeMax - 2;
    if (hurtStarted_) lastHurtTick_ = tick;

    positionUpdated_ = prev && (std::fabs(f.pos.x - prev->pos.x) > 1e-9 || std::fabs(f.pos.y - prev->pos.y) > 1e-9 ||
                                std::fabs(f.pos.z - prev->pos.z) > 1e-9);

    teleported_ = false;
    if (prev) {
        const double jump = (f.pos - prev->pos).length();
        const double perTick = jump / std::max<Tick>(1, tick - prevTick);
        if (jump > 8.0 || perTick > 4.0) {
            teleported_ = true;
            lastTeleportTick_ = tick;
        }
    }

    if (mc::isIce(f.blockBelow)) lastIceTick_ = tick;
    if (mc::isSlime(f.blockBelow)) lastSlimeTick_ = tick;
    if (f.inWater || f.inLava) lastLiquidTick_ = tick;
    if (f.onLadder) lastClimbTick_ = tick;
    if (f.inWeb) lastWebTick_ = tick;
    if (f.riding) lastRideTick_ = tick;
    if (f.blocking && (!prev || !prev->blocking)) blockStartTick_ = tick;
    if (f.usingItem && (!prev || !prev->usingItem)) useStartTick_ = tick;

    frames_.push_back(f);
    ticks_.push_back(tick);
    while (static_cast<int>(frames_.size()) > kCapacity) {
        frames_.pop_front();
        ticks_.pop_front();
    }

    updateAir(f, prev, tick);
}

void PlayerTrack::updateAir(const PlayerFrame& f, const PlayerFrame* prev, Tick tick) {
    tookOff_ = false;
    landed_ = false;
    if (!prev) {
        airborne_ = !f.onGround;
        return;
    }
    if (teleported_ || hurtStarted_ || f.riding || f.inWater || f.inLava || f.onLadder || f.inWeb) {
        currentAir_.clean = false;
    }

    if (airborne_) {
        ++airTicks_;
        currentAir_.airTicks = static_cast<int>(tick - currentAir_.takeoffTick);
        currentAir_.apexY = std::max(currentAir_.apexY, f.pos.y);
        currentAir_.horizontalDistance += (f.pos - prev->pos).horizontalLength();
        if (!positionUpdated_) ++currentAir_.dataGaps;
        if (currentAir_.airTicks == 3) currentAir_.startVelocity = velocity(0, 2).horizontal();
        if (f.potions.jumpBoost > currentAir_.jumpBoost) currentAir_.jumpBoost = f.potions.jumpBoost;
        if (f.onGround) {
            airborne_ = false;
            landed_ = true;
            currentAir_.landTick = tick;
            currentAir_.landY = f.pos.y;
            // Velocity just before touchdown: ticks [k=1..3].
            currentAir_.endVelocity = velocity(1, 2).horizontal();
            airSegments_.push_back(currentAir_);
            while (airSegments_.size() > 24) airSegments_.pop_front();
            groundTicks_ = 1;
            airTicks_ = 0;
        }
    } else {
        if (!f.onGround) {
            airborne_ = true;
            tookOff_ = true;
            airTicks_ = 1;
            currentAir_ = AirSegment{};
            currentAir_.takeoffTick = tick - 1;
            currentAir_.takeoffY = prev->pos.y;
            currentAir_.apexY = std::max(prev->pos.y, f.pos.y);
            currentAir_.groundTicksBefore = groundTicks_;
            currentAir_.jumped = f.pos.y > prev->pos.y + 0.1;
            currentAir_.ceilingAtTakeoff = mc::isSolidish(prev->blockAboveHead);
            currentAir_.jumpBoost = f.potions.jumpBoost;
            currentAir_.speedAmp = f.potions.speed;
            currentAir_.clean = !(hurtStarted_ || teleported_);
            groundTicks_ = 0;
        } else {
            ++groundTicks_;
        }
    }
}

}  // namespace vigil::detection
