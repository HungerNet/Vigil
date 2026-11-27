// Speed — horizontal movement faster than the vanilla envelope.
//
// Measurement. Speeds are taken between the ticks positions were *received* (see
// PlayerTrack::horizontalSpeedFresh), so a lag catch-up is spread over the time it really covers
// instead of being squeezed into the window. Evidence strength is scaled by how regular the
// position updates were.
//
// Limits scale with the largest movement multiplier seen over the window (movement attribute,
// Speed effects, conservative allowance when effect data is stale), so a potion that just ended
// still explains the movement it produced. Movement states that legitimately exceed the flat-ground
// envelope are exempt or tolerated:
//   * knockback, teleports, riding, liquids, ladders, cobwebs, flight, server stalls (shared)
//   * ice and slime                                                   (momentum surfaces)
//   * head-hitter jumping under a two-block ceiling                   (short, fast jumps)
//   * repeated step-up jumps on stairs, slabs and climbs              (shorter air time)

#include "../check.h"

#include <algorithm>
#include <cmath>

namespace vigil::detection {
namespace {

struct SpeedState : PlayerCheckState {
    Tick lastSustained = -10000;
    Tick lastGround = -10000;
    Tick lastBurst = -10000;
};

// Head-hitter sprint-jumping reaches ~0.5 b/tick; step-up jumping ~0.42.
constexpr int kCeilingGraceTicks = 20;
constexpr int kStepUpGraceTicks = 20;
constexpr double kStepUpTolerance = 1.12;
constexpr int kMultiplierWindow = 30;   // window + momentum after a modifier ends

class Speed final : public Check {
public:
    Speed()
        : Check("speed", "Speed", CheckCategory::Movement, "Movement faster than vanilla allows",
                "Compares horizontal speed with the vanilla envelope: about 0.28 blocks per tick sprinting on the "
                "ground and 0.36 sprint-jumping, scaled by the player's movement-speed attribute and Speed effects. "
                "Speeds are measured between received positions, so lag catch-ups don't count. Knockback, "
                "teleports, ice, slime, liquids, ladders, cobwebs, riding, flight, low ceilings and server lag are "
                "skipped; stairs and slabs are tolerated.") {
        defAlert_ = 72.f;
        defHalfLife_ = 25.f;
        defMinEvidence_ = 3;
    }
    std::unique_ptr<PlayerCheckState> createState() const override { return std::make_unique<SpeedState>(); }
    std::vector<std::string> requirements() const override {
        return {"Server positions", "Movement attribute or potion effects", "Surface blocks"};
    }

protected:
    void registerSettings() override {
        iSustained_ = addIndicator("sustained", "Sustained speed",
                                   "Average speed over one second exceeds the sprint-jump envelope.", 1.0f);
        iGround_ = addIndicator("ground", "Ground speed", "Speed while staying on the ground exceeds sprinting.", 1.2f);
        iBurst_ = addIndicator("burst", "Burst", "A short movement far beyond any vanilla speed without a teleport.", 0.6f);

        airLimit_ = addFloat("limit", "Sprint-jump limit", 0.40, {0.36, 0.7, 0.005, 3, "b/tick"},
                             "One-second average speed above which movement counts as evidence. Vanilla sprint-jumping "
                             "averages 0.356 b/tick. Movement modifiers raise this limit automatically.");
        preset(airLimit_, 0.43, 0.40, 0.385);
        groundLimit_ = addFloat("groundLimit", "Ground limit", 0.31, {0.28, 0.6, 0.005, 3, "b/tick"},
                                "Average speed while on the ground above which movement counts as evidence. Vanilla "
                                "sprinting is 0.281 b/tick.");
        preset(groundLimit_, 0.33, 0.31, 0.30);
        burstLimit_ = addFloat("burst", "Burst limit", 1.0, {0.6, 3.0, 0.05, 2, "b/tick"},
                               "Four-tick average speed treated as a burst. Movement above 4 b/tick is treated as a "
                               "teleport and ignored.", "Detection", true);
    }

public:
    void onTick(CheckContext& ctx, PlayerTrack& t, PlayerCheckState* base) override {
        auto* st = static_cast<SpeedState*>(base);
        const EngineConfig& cfg = ctx.config();
        const Exemptions& ex = ctx.exemptions(t);
        if (ex.movement() || ex.ice || ex.slime) return;
        if (t.size() < 24) return;
        const PlayerFrame& f = t.cur();
        const Tick now = ctx.tick();
        const double mult = t.maxSpeedMultiplier(kMultiplierWindow);
        const bool underCeiling = now - t.lastCeilingTick() <= kCeilingGraceTicks;
        const bool steppingUp = now - t.lastStepUpTick() <= kStepUpGraceTicks;
        const double terrain = steppingUp ? kStepUpTolerance : 1.0;
        const std::string modifier = speedMultiplierSource(f);

        // Sustained (20 ticks). Head-hitter jumping legitimately exceeds the envelope.
        const double avg = t.horizontalSpeedFresh(20);
        const float q20 = t.dataQuality(20);
        const double limit = cfg.value(airLimit_) * mult * terrain;
        if (!underCeiling && q20 >= 0.3f && avg > limit && now - st->lastSustained >= 20) {
            st->lastSustained = now;
            const float sev = static_cast<float>(std::clamp((avg / limit - 1.0) / 0.25 + 0.3, 0.3, 1.0)) * (0.6f + 0.4f * q20);
            ctx.emit(t, iSustained_, sev, fmt("Averaged %.2f b/tick for a second", avg),
                     fmt("%.1f m/s over one second; the limit with %.2fx movement (%s)%s is %.1f m/s.", avg * 20.0, mult,
                         modifier.c_str(), steppingUp ? " and stair tolerance" : "", limit * 20.0),
                     {{"Average speed", avg, "b/tick", limit, true},
                      {"Vanilla sprint-jump", mc::kSprintJumpAverage * mult, "b/tick", 0.0, false, EvidenceValue::Role::Expected},
                      {"Movement multiplier", mult, "x"},
                      {"Data quality", q20 * 100.0, "%"}});
        }

        // Ground (12 ticks continuously on ground, not sneaking).
        if (!underCeiling && t.groundTicks() >= 12 && !f.sneaking) {
            const double g = t.horizontalSpeedFresh(12);
            const float q12 = t.dataQuality(12);
            const double glimit = cfg.value(groundLimit_) * mult;
            if (q12 >= 0.3f && g > glimit && now - st->lastGround >= 20) {
                st->lastGround = now;
                const float sev = static_cast<float>(std::clamp((g / glimit - 1.0) / 0.25 + 0.35, 0.35, 1.0)) * (0.6f + 0.4f * q12);
                ctx.emit(t, iGround_, sev, fmt("%.2f b/tick without leaving the ground", g),
                         fmt("Stayed on the ground for 12 ticks at %.2f b/tick. Sprinting tops out at 0.28; with %.2fx "
                             "movement (%s) the limit is %.2f.",
                             g, mult, modifier.c_str(), glimit),
                         {{"Ground speed", g, "b/tick", glimit, true},
                          {"Vanilla sprint", mc::kSprintGroundSpeed * mult, "b/tick", 0.0, false, EvidenceValue::Role::Expected},
                          {"Movement multiplier", mult, "x"},
                          {"Data quality", q12 * 100.0, "%"}});
            }
        }

        // Burst (4 ticks), only on regular data: a burst is exactly what a lag catch-up looks like.
        const double b = t.horizontalSpeedFresh(4);
        const double blimit = cfg.value(burstLimit_) * mult;
        if (t.dataQuality(8) >= 0.9f && b > blimit && b < 4.0 && now - st->lastBurst >= 40) {
            st->lastBurst = now;
            ctx.emit(t, iBurst_, static_cast<float>(std::clamp(b / (blimit * 2.0), 0.3, 1.0)),
                     fmt("Moved %.2f b/tick for 4 ticks", b),
                     "A short burst beyond any vanilla speed, too small to be a teleport, with regular position updates.",
                     {{"Burst speed", b, "b/tick", blimit, true}, {"Movement multiplier", mult, "x"}});
        }
    }

private:
    int iSustained_ = 0, iGround_ = 0, iBurst_ = 0;
    ParamId airLimit_ = kNoParam;
    ParamId groundLimit_ = kNoParam;
    ParamId burstLimit_ = kNoParam;
};

}  // namespace

std::unique_ptr<Check> makeSpeed() { return std::make_unique<Speed>(); }

}  // namespace vigil::detection
