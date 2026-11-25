// NoSlow — moving at full speed while using an item. Ported from the original check and
// strengthened with a speed-based indicator.

#include "../check.h"

#include <algorithm>
#include <cmath>

namespace vigil::detection {
namespace {

struct NoSlowState : PlayerCheckState {
    int sprintUsingTicks = 0;
    Tick lastSprintEvidence = -10000;
    Tick lastSpeedEvidence = -10000;
};

bool slowingItemInUse(const PlayerFrame& f) {
    if (!f.usingItem) return false;
    return mc::isSword(f.heldItem) || mc::isConsumableOrUsable(f.heldItem) || f.blocking;
}

class NoSlow final : public Check {
public:
    NoSlow()
        : Check("noslow", "NoSlow", CheckCategory::Movement, "No slowdown while eating, drinking, blocking or drawing a bow",
                "Using an item in vanilla multiplies movement input by 0.2 and stops sprinting. Vigil watches for "
                "players who keep their sprint flag for several ticks while using an item, and for players who keep "
                "moving at walking or sprinting speed on the ground long after the item use began, when momentum "
                "would already have bled off.") {
        defAlert_ = 70.f;
        defHalfLife_ = 30.f;
        defMinEvidence_ = 3;
    }
    std::unique_ptr<PlayerCheckState> createState() const override { return std::make_unique<NoSlowState>(); }

protected:
    void registerSettings() override {
        iSprint_ = addIndicator("sprint", "Sprinting while using", "The sprint flag stayed on during item use.", 0.8f);
        iSpeed_ = addIndicator("speed", "Speed while using", "Ground speed during item use exceeds the vanilla limit.", 1.2f);
        sprintTicks_ = &addInt("sprintTicks", "Sprint overlap", 8, {3, 30, 1, 0, "ticks"},
                               "Consecutive ticks of sprinting during item use before it counts as evidence. A legitimate "
                               "player's sprint flag can lag item use by a tick or two.");
        preset(*sprintTicks_, 12, 8, 6);
        speedLimit_ = &addFloat("speedLimit", "Speed limit", 0.17, {0.08, 0.3, 0.005, 3, "b/tick"},
                                "Ground speed during item use above which it counts as evidence. Vanilla item-use "
                                "walking is about 0.06 b/tick.");
        preset(*speedLimit_, 0.20, 0.17, 0.14);
    }

public:
    void onTick(CheckContext& ctx, PlayerTrack& t, PlayerCheckState* base) override {
        auto* st = static_cast<NoSlowState*>(base);
        const PlayerFrame& f = t.cur();
        const Exemptions& ex = ctx.exemptions(t);
        if (!slowingItemInUse(f) || f.riding || ex.movement()) { st->sprintUsingTicks = 0; return; }
        const Tick now = ctx.tick();

        if (f.sprinting) {
            ++st->sprintUsingTicks;
            if (st->sprintUsingTicks >= sprintTicks_->i() && now - st->lastSprintEvidence > 40) {
                st->lastSprintEvidence = now;
                ctx.emit(t, iSprint_, static_cast<float>(std::clamp(st->sprintUsingTicks / (sprintTicks_->value * 2.0), 0.4, 1.0)),
                         fmt("Sprinted for %d ticks while using an item", st->sprintUsingTicks),
                         fmt("%s kept sprinting for %d consecutive ticks (%d ms) while using item %d. Starting to use "
                             "an item ends sprinting in vanilla.",
                             t.name().c_str(), st->sprintUsingTicks, st->sprintUsingTicks * 50, f.heldItem),
                         {{"Sprinting while using", static_cast<double>(st->sprintUsingTicks), "ticks",
                           static_cast<double>(sprintTicks_->i()), true},
                          {"Item", static_cast<double>(f.heldItem), ""}});
            }
        } else {
            st->sprintUsingTicks = 0;
        }

        const Tick usingFor = now - t.lastUseStartTick();
        if (usingFor >= 6 && t.groundTicks() >= 4 && !ex.ice && now - st->lastSpeedEvidence > 20) {
            const double sp = t.horizontalSpeed(4);
            const double limit = speedLimit_->value * speedMultiplier(f);
            if (sp > limit) {
                st->lastSpeedEvidence = now;
                ctx.emit(t, iSpeed_, static_cast<float>(std::clamp((sp / limit - 1.0) / 0.8 + 0.35, 0.35, 1.0)),
                         fmt("Moved %.2f b/tick while using an item", sp),
                         fmt("%s had been using item %d for %lld ticks and was on the ground for %d, yet moved %.2f "
                             "blocks per tick. Vanilla item use slows walking to about 0.06.",
                             t.name().c_str(), f.heldItem, static_cast<long long>(usingFor), t.groundTicks(), sp),
                         {{"Speed while using", sp, "b/tick", limit, true},
                          {"Using for", static_cast<double>(usingFor), "ticks"}});
            }
        }
    }

private:
    int iSprint_ = 0, iSpeed_ = 0;
    Setting* sprintTicks_ = nullptr;
    Setting* speedLimit_ = nullptr;
};

}  // namespace

std::unique_ptr<Check> makeNoSlow() { return std::make_unique<NoSlow>(); }

}  // namespace vigil::detection
