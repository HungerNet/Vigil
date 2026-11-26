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

Tick PlayerTrack::updateTickAt(int k) const {
    if (updateTicks_.empty()) return 0;
    k = std::clamp(k, 0, size() - 1);
    return updateTicks_[updateTicks_.size() - 1 - static_cast<std::size_t>(k)];
}

double PlayerTrack::horizontalSpeedFresh(int window) const {
    if (size() < 2) return 0.0;
    window = std::clamp(window, 1, size() - 1);
    // The current position was received at `ue`; the one held `window` ticks ago at `us`
    // (earlier than the window start when updates were missing). Standing still also sends no
    // updates, so a movement that just began reads slightly slow — never fast.
    const Tick ue = updateTickAt(0);
    const Tick us = updateTickAt(window);
    const double elapsed = static_cast<double>(ue - us);
    if (elapsed <= 0.0) return 0.0;
    return (back(0).pos - back(window).pos).horizontalLength() / elapsed;
}

Vec3 PlayerTrack::velocityFresh(int k, int span) const {
    if (size() < 2) return {};
    const int a = std::clamp(k, 0, size() - 1);
    int b = std::clamp(k + std::max(1, span), 0, size() - 1);
    const Tick ua = updateTickAt(a);
    // Reach back to a different received position (bunched updates share one arrival tick).
    while (b < size() - 1 && b < k + span + 4 && updateTickAt(b) == ua) ++b;
    const Tick ub = updateTickAt(b);
    if (ua <= ub) return {};
    return (back(a).pos - back(b).pos) * (1.0 / static_cast<double>(ua - ub));
}

int PlayerTrack::maxUpdateGap(int window) const {
    if (size() < 2) return 0;
    window = std::clamp(window, 1, size() - 1);
    Tick prev = updateTickAt(0);
    int gap = static_cast<int>(tickAt(0) - prev);   // time since the last update
    for (int k = 1; k <= window; ++k) {
        const Tick u = updateTickAt(k);
        if (u != prev) {
            gap = std::max(gap, static_cast<int>(prev - u));
            prev = u;
        }
    }
    return gap;
}

float PlayerTrack::dataQuality(int window) const {
    const int g = maxUpdateGap(window);
    if (g <= 3) return 1.f;
    return std::clamp(1.f - static_cast<float>(g - 3) / 9.f, 0.f, 1.f);
}

double PlayerTrack::maxSpeedMultiplier(int window) const {
    double m = 1.0;
    const int n = std::min(window, size() - 1);
    for (int k = 0; k <= n; ++k) m = std::max(m, speedMultiplier(back(k)));
    return m;
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
    if (mc::isSolidish(f.blockAboveHead)) lastCeilingTick_ = tick;
    if (f.blocking && (!prev || !prev->blocking)) blockStartTick_ = tick;
    if (f.usingItem && (!prev || !prev->usingItem)) useStartTick_ = tick;

    const Tick received = (!prev || positionUpdated_ || updateTicks_.empty()) ? tick : updateTicks_.back();
    frames_.push_back(f);
    ticks_.push_back(tick);
    updateTicks_.push_back(received);
    while (static_cast<int>(frames_.size()) > kCapacity) {
        frames_.pop_front();
        ticks_.pop_front();
        updateTicks_.pop_front();
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
        currentAir_.maxGap = std::max(currentAir_.maxGap, static_cast<int>(tick - updateTicks_.back()));
        // Span takeoff+1 … takeoff+5: after the jump impulse, wide enough to average bunching.
        if (currentAir_.airTicks == 5) currentAir_.startVelocity = velocityFresh(0, 4).horizontal();
        if (f.potions.jumpBoost > currentAir_.jumpBoost) currentAir_.jumpBoost = f.potions.jumpBoost;
        if (mc::isSolidish(f.blockAboveHead) && f.pos.y - currentAir_.takeoffY < 0.6) currentAir_.ceilingAtTakeoff = true;
        if (f.onGround) {
            airborne_ = false;
            landed_ = true;
            currentAir_.landTick = tick;
            currentAir_.landY = f.pos.y;
            // Velocity just before touchdown: landing−5 … landing−1.
            currentAir_.endVelocity = currentAir_.airTicks >= 9 ? velocityFresh(1, 4).horizontal() : Vec3{};
            if (currentAir_.landY > currentAir_.takeoffY + 0.4) lastStepUpTick_ = tick;
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
