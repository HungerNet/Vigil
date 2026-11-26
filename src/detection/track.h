#pragma once
// Vigil detection framework — per-player history and derived signals.

#include "types.h"

#include <deque>
#include <vector>

namespace vigil::detection {

// A completed (or in-progress) airborne segment between two ground contacts.
struct AirSegment {
    Tick takeoffTick = 0;
    Tick landTick = 0;
    double takeoffY = 0.0;
    double landY = 0.0;
    double apexY = 0.0;
    int airTicks = 0;
    double horizontalDistance = 0.0;
    // Horizontal velocities from update-aware 4-tick spans centered 3 ticks after takeoff and 3
    // ticks before landing (b/tick); zero when the segment was too short to measure.
    Vec3 startVelocity;
    Vec3 endVelocity;
    int maxGap = 0;           // longest run of ticks without a position update during the segment
    int groundTicksBefore = 0; // ground ticks between previous landing and this takeoff
    bool jumped = false;      // rose like a jump (vs. walked off an edge)
    bool clean = true;        // no hurt/teleport/exemption during the segment
    bool ceilingAtTakeoff = false;
    int jumpBoost = -1;
    int speedAmp = -1;
    int dataGaps = 0;         // ticks without a position update during the segment
};

class PlayerTrack {
public:
    static constexpr int kCapacity = 200;  // 10 s of history

    void push(const PlayerFrame& f, Tick tick, double timeMs);

    int entityId() const { return entityId_; }
    const std::string& uuid() const { return cur().uuid; }
    const std::string& name() const { return cur().name; }

    int size() const { return static_cast<int>(frames_.size()); }
    bool empty() const { return frames_.empty(); }
    // back(0) = current tick, back(1) = previous, ...
    const PlayerFrame& back(int k = 0) const;
    Tick tickAt(int k = 0) const;
    const PlayerFrame& cur() const { return back(0); }
    bool has(int k) const { return k < size(); }

    // Horizontal speed averaged over the last `window` ticks (b/tick), using tick indices.
    double horizontalSpeed(int window) const;
    Vec3 displacement(int window) const;
    // Velocity estimate centered `k` ticks back, averaged over `span` ticks.
    Vec3 velocity(int k, int span = 2) const;

    // ── Update-aware measurements ────────────────────────────────────────────────────────────
    // Server positions arrive every ~2 ticks, in bursts after lag, and sometimes not at all for a
    // while. A position held at tick t may have been received many ticks earlier. These helpers
    // measure between the ticks positions were *received*, so a lag catch-up is spread over the
    // time it really covers instead of being squeezed into the window.
    Tick updateTickAt(int k = 0) const;
    // Average horizontal speed over roughly the last `window` ticks.
    double horizontalSpeedFresh(int window) const;
    // Velocity between the positions received at/before index k and at/before index k + span.
    Vec3 velocityFresh(int k, int span = 4) const;
    // Longest run of ticks without a position update inside the last `window` ticks.
    int maxUpdateGap(int window) const;
    // Data regularity of the last `window` ticks: 1 = regular updates, 0 = unusable.
    float dataQuality(int window) const;
    // Largest movement multiplier seen over the last `window` ticks (a modifier that just ended
    // still explains the movement it produced).
    double maxSpeedMultiplier(int window) const;

    // Edge events computed on the latest push.
    bool swingStarted() const { return swingStarted_; }
    bool hurtStarted() const { return hurtStarted_; }
    bool tookOff() const { return tookOff_; }
    bool landed() const { return landed_; }
    bool teleported() const { return teleported_; }
    bool positionUpdated() const { return positionUpdated_; }

    Tick lastSwingTick() const { return lastSwingTick_; }
    Tick lastHurtTick() const { return lastHurtTick_; }
    Tick lastTeleportTick() const { return lastTeleportTick_; }
    Tick firstTick() const { return firstTick_; }
    Tick lastIceTick() const { return lastIceTick_; }
    Tick lastSlimeTick() const { return lastSlimeTick_; }
    Tick lastLiquidTick() const { return lastLiquidTick_; }
    Tick lastClimbTick() const { return lastClimbTick_; }
    Tick lastWebTick() const { return lastWebTick_; }
    Tick lastRideTick() const { return lastRideTick_; }
    // A solid block two blocks above the feet (head-hitter jumps are short and fast).
    Tick lastCeilingTick() const { return lastCeilingTick_; }
    // A jump that landed at least 0.4 blocks higher than it took off (stairs, slabs, climbing).
    Tick lastStepUpTick() const { return lastStepUpTick_; }
    Tick lastBlockStartTick() const { return blockStartTick_; }
    Tick lastUseStartTick() const { return useStartTick_; }

    const std::deque<Tick>& swingTicks() const { return swingTicks_; }
    const std::deque<AirSegment>& airSegments() const { return airSegments_; }
    bool airborne() const { return airborne_; }
    const AirSegment& currentAir() const { return currentAir_; }
    int groundTicks() const { return groundTicks_; }
    int airTicks() const { return airTicks_; }

    void markSegmentDirty() { currentAir_.clean = false; }

private:
    void updateAir(const PlayerFrame& f, const PlayerFrame* prev, Tick tick);

    int entityId_ = -1;
    std::deque<PlayerFrame> frames_;
    std::deque<Tick> ticks_;
    std::deque<Tick> updateTicks_;   // tick at which each frame's position was received
    std::deque<Tick> swingTicks_;
    std::deque<AirSegment> airSegments_;
    AirSegment currentAir_;
    bool airborne_ = false;
    int groundTicks_ = 0;
    int airTicks_ = 0;
    int groundTicksBeforeTakeoff_ = 0;

    bool swingStarted_ = false, hurtStarted_ = false, tookOff_ = false, landed_ = false;
    bool teleported_ = false, positionUpdated_ = false;
    Tick lastSwingTick_ = -1000, lastHurtTick_ = -1000, lastTeleportTick_ = -1000;
    Tick firstTick_ = 0;
    Tick lastIceTick_ = -1000, lastSlimeTick_ = -1000, lastLiquidTick_ = -1000, lastClimbTick_ = -1000;
    Tick lastWebTick_ = -1000, lastRideTick_ = -1000;
    Tick lastCeilingTick_ = -1000, lastStepUpTick_ = -1000;
    Tick blockStartTick_ = -1000, useStartTick_ = -1000;
};

}  // namespace vigil::detection
