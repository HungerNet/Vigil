// Speed — horizontal movement faster than the vanilla envelope.

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

class Speed final : public Check {
public:
    Speed()
        : Check("speed", "Speed", CheckCategory::Movement, "Movement faster than vanilla allows",
                "Measures horizontal speed from the server-relayed positions over windows long enough to cancel packet "
                "bunching, and compares it with the vanilla envelope: about 0.28 blocks per tick sprinting on the "
                "ground and 0.36 sprint-jumping, scaled by Speed potions. Players are ignored after knockback, "
                "teleports, on ice or slime, in liquids, on ladders, in cobwebs, while riding and while flying.") {
        defAlert_ = 72.f;
        defHalfLife_ = 25.f;
        defMinEvidence_ = 3;
    }
    std::unique_ptr<PlayerCheckState> createState() const override { return std::make_unique<SpeedState>(); }
    std::vector<std::string> requirements() const override { return {"Server positions", "Potion effects", "Surface blocks"}; }

protected:
    void registerSettings() override {
        iSustained_ = addIndicator("sustained", "Sustained speed",
                                   "Average speed over one second exceeds the sprint-jump envelope.", 1.0f);
        iGround_ = addIndicator("ground", "Ground speed", "Speed while staying on the ground exceeds sprinting.", 1.2f);
        iBurst_ = addIndicator("burst", "Burst", "A short movement far beyond any vanilla speed without a teleport.", 0.6f);

        airLimit_ = &addFloat("limit", "Sprint-jump limit", 0.40, {0.36, 0.7, 0.005, 3, "b/tick"},
                              "One-second average speed above which movement counts as evidence. Vanilla sprint-jumping "
                              "averages 0.356 b/tick. Speed potions raise this limit automatically.");
        preset(*airLimit_, 0.43, 0.40, 0.385);
        groundLimit_ = &addFloat("groundLimit", "Ground limit", 0.31, {0.28, 0.6, 0.005, 3, "b/tick"},
                                 "Average speed while on the ground above which movement counts as evidence. Vanilla "
                                 "sprinting is 0.281 b/tick.");
        preset(*groundLimit_, 0.33, 0.31, 0.30);
        burstLimit_ = &addFloat("burst", "Burst limit", 1.0, {0.6, 3.0, 0.05, 2, "b/tick"},
                                "Four-tick average speed treated as a burst. Movement above 4 b/tick is treated as a "
                                "teleport and ignored.", "Detection", true);
    }

public:
    void onTick(CheckContext& ctx, PlayerTrack& t, PlayerCheckState* base) override {
        auto* st = static_cast<SpeedState*>(base);
        const Exemptions& ex = ctx.exemptions(t);
        if (ex.movement() || ex.ice || ex.slime) return;
        if (t.size() < 24) return;
        const PlayerFrame& f = t.cur();
        const double mult = speedMultiplier(f);
        const Tick now = ctx.tick();

        // Sustained (20 ticks).
        const double avg = t.horizontalSpeed(20);
        const double limit = airLimit_->value * mult;
        if (avg > limit && now - st->lastSustained >= 20) {
            st->lastSustained = now;
            const float sev = static_cast<float>(std::clamp((avg / limit - 1.0) / 0.25 + 0.3, 0.3, 1.0));
            ctx.emit(t, iSustained_, sev, fmt("Averaged %.2f b/tick for one second", avg),
                     fmt("%s covered %.1f blocks in one second (%.1f m/s). Vanilla sprint-jumping averages 7.1 m/s%s; "
                         "the limit in effect was %.2f b/tick.",
                         t.name().c_str(), avg * 20.0, avg * 20.0,
                         f.potionsKnown ? (f.potions.speed >= 0 ? fmt(", Speed %d active", f.potions.speed + 1).c_str() : "")
                                        : " (potion data unavailable; conservative limit)",
                         limit),
                     {{"Average speed", avg, "b/tick", limit, true},
                      {"Potion multiplier", mult, "x"},
                      {"On ground", f.onGround ? 1.0 : 0.0, ""}});
        }

        // Ground (12 ticks continuously on ground, not sneaking).
        if (t.groundTicks() >= 12 && !f.sneaking) {
            const double g = t.horizontalSpeed(12);
            const double glimit = groundLimit_->value * mult;
            if (g > glimit && now - st->lastGround >= 20) {
                st->lastGround = now;
                const float sev = static_cast<float>(std::clamp((g / glimit - 1.0) / 0.25 + 0.35, 0.35, 1.0));
                ctx.emit(t, iGround_, sev, fmt("%.2f b/tick without leaving the ground", g),
                         fmt("For 12 ticks %s stayed on the ground and moved %.2f blocks per tick. Sprinting on flat "
                             "ground tops out at 0.28; the limit in effect was %.2f.",
                             t.name().c_str(), g, glimit),
                         {{"Ground speed", g, "b/tick", glimit, true}, {"Potion multiplier", mult, "x"}});
            }
        }

        // Burst (4 ticks).
        const double b = t.horizontalSpeed(4);
        if (b > burstLimit_->value * mult && b < 4.0 && now - st->lastBurst >= 40) {
            st->lastBurst = now;
            ctx.emit(t, iBurst_, static_cast<float>(std::clamp(b / (burstLimit_->value * mult * 2.0), 0.3, 1.0)),
                     fmt("Moved %.2f b/tick for 4 ticks", b),
                     "A short burst beyond any vanilla movement speed that was not large enough to be a teleport.",
                     {{"Burst speed", b, "b/tick", burstLimit_->value * mult, true}});
        }
    }

private:
    int iSustained_ = 0, iGround_ = 0, iBurst_ = 0;
    Setting* airLimit_ = nullptr;
    Setting* groundLimit_ = nullptr;
    Setting* burstLimit_ = nullptr;
};

}  // namespace

std::unique_ptr<Check> makeSpeed() { return std::make_unique<Speed>(); }

}  // namespace vigil::detection
