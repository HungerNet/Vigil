// NoSlow — moving at full speed while using an item.
//
// Item use multiplies movement input by 0.2 and ends sprinting. Two indicators: the sprint flag
// staying on during use, and ground speed that stays high long after use began. Speeds are
// update-aware (a lag catch-up while eating is not "fast"), and the limit includes the momentum
// the player had when use began, decaying with vanilla ground friction.

#include "../check.h"

#include <algorithm>
#include <cmath>

namespace vigil::detection {
namespace {

struct NoSlowState : PlayerCheckState {
    int sprintUsingTicks = 0;
    Tick lastSprintEvidence = -10000;
    Tick lastSpeedEvidence = -10000;
    Tick useStart = -10000;
    double speedAtUseStart = 0.0;
};

constexpr double kGroundFriction = 0.546;   // 0.6 slipperiness × 0.91

bool slowingItemInUse(const PlayerFrame& f) {
    if (!f.usingItem) return false;
    return mc::isSword(f.heldItem) || mc::isConsumableOrUsable(f.heldItem) || f.blocking;
}

class NoSlow final : public Check {
public:
    NoSlow()
        : Check("noslow", "NoSlow", CheckCategory::Movement, "No slowdown while eating, drinking, blocking or drawing a bow",
                "Using an item slows movement to a fifth and ends sprinting. Vigil flags players who keep sprinting "
                "for several ticks while using an item, or who keep moving fast on the ground after their momentum "
                "should have bled off. Lag catch-ups, ice, slime, knockback and server lag are skipped.") {
        defAlert_ = 70.f;
        defHalfLife_ = 30.f;
        defMinEvidence_ = 3;
    }
    std::unique_ptr<PlayerCheckState> createState() const override { return std::make_unique<NoSlowState>(); }

protected:
    void registerSettings() override {
        iSprint_ = addIndicator("sprint", "Sprinting while using", "The sprint flag stayed on during item use.", 0.8f);
        iSpeed_ = addIndicator("speed", "Speed while using", "Ground speed during item use exceeds the vanilla limit.", 1.2f);
        sprintTicks_ = addInt("sprintTicks", "Sprint overlap", 8, {3, 30, 1, 0, "ticks"},
                              "Consecutive ticks of sprinting during item use before it counts as evidence. A legitimate "
                              "player's sprint flag can lag item use by a tick or two.");
        preset(sprintTicks_, 12, 8, 6);
        speedLimit_ = addFloat("speedLimit", "Speed limit", 0.17, {0.08, 0.3, 0.005, 3, "b/tick"},
                               "Ground speed during item use above which it counts as evidence. Vanilla item-use "
                               "walking is about 0.06 b/tick.");
        preset(speedLimit_, 0.20, 0.17, 0.14);
    }

public:
    void onTick(CheckContext& ctx, PlayerTrack& t, PlayerCheckState* base) override {
        auto* st = static_cast<NoSlowState*>(base);
        const EngineConfig& cfg = ctx.config();
        const PlayerFrame& f = t.cur();
        const Exemptions& ex = ctx.exemptions(t);
        if (!slowingItemInUse(f) || f.riding || ex.movement() || ex.ice || ex.slime) { st->sprintUsingTicks = 0; return; }
        const Tick now = ctx.tick();
        if (t.lastUseStartTick() != st->useStart) {
            // Momentum carried into the item use.
            st->useStart = t.lastUseStartTick();
            st->speedAtUseStart = t.horizontalSpeedFresh(4);
        }

        if (f.sprinting) {
            ++st->sprintUsingTicks;
            if (st->sprintUsingTicks >= cfg.i(sprintTicks_) && now - st->lastSprintEvidence > 40) {
                st->lastSprintEvidence = now;
                ctx.emit(t, iSprint_, static_cast<float>(std::clamp(st->sprintUsingTicks / (cfg.value(sprintTicks_) * 2.0), 0.4, 1.0)),
                         fmt("Sprinted %d ticks while using an item", st->sprintUsingTicks),
                         fmt("Using an item ends sprinting within a tick or two; the sprint flag stayed on for %d ms.",
                             st->sprintUsingTicks * 50),
                         {{"Sprinting while using", static_cast<double>(st->sprintUsingTicks), "ticks",
                           static_cast<double>(cfg.i(sprintTicks_)), true},
                          {"Item", static_cast<double>(f.heldItem), ""}});
            }
        } else {
            st->sprintUsingTicks = 0;
        }

        const Tick usingFor = now - t.lastUseStartTick();
        if (usingFor >= 6 && t.groundTicks() >= 4 && now - st->lastSpeedEvidence > 20) {
            const double sp = t.horizontalSpeedFresh(4);
            const float q = t.dataQuality(6);
            const double mult = t.maxSpeedMultiplier(static_cast<int>(std::min<Tick>(usingFor, 30)));
            // Momentum from before the use decays by ground friction each tick (4-tick window).
            const double carry = st->speedAtUseStart * std::pow(kGroundFriction, static_cast<double>(std::max<Tick>(0, usingFor - 4)));
            const double limit = std::max(cfg.value(speedLimit_) * mult, carry);
            if (q >= 0.6f && sp > limit) {
                st->lastSpeedEvidence = now;
                ctx.emit(t, iSpeed_, static_cast<float>(std::clamp((sp / limit - 1.0) / 0.8 + 0.35, 0.35, 1.0)) * (0.6f + 0.4f * q),
                         fmt("Moved %.2f b/tick while using an item", sp),
                         fmt("%.1f s into using an item, on the ground, at %.2f b/tick. Item use slows walking to about "
                             "0.06; the limit here is %.2f.",
                             usingFor * 0.05, sp, limit),
                         {{"Speed while using", sp, "b/tick", limit, true},
                          {"Using for", static_cast<double>(usingFor), "ticks"},
                          {"Data quality", q * 100.0, "%"}});
            }
        }
    }

private:
    int iSprint_ = 0, iSpeed_ = 0;
    ParamId sprintTicks_ = kNoParam;
    ParamId speedLimit_ = kNoParam;
};

}  // namespace

std::unique_ptr<Check> makeNoSlow() { return std::make_unique<NoSlow>(); }

}  // namespace vigil::detection
