// BHop — redesigned around airborne-segment physics.
//
// The previous version sampled interpolated positions on a 50 ms wall clock and flagged jumps
// that started less than 350 ms apart with a small Y step, which matches ordinary sprint-jumping
// over uneven terrain. This version analyzes each complete jump (takeoff → landing) against the
// vanilla jump arc and vanilla air control.

#include "../check.h"

#include <algorithm>
#include <cmath>

namespace vigil::detection {
namespace {

struct BHopState : PlayerCheckState {
    int chain = 0;
    Tick chainStart = 0;
    Tick lastChainEvidence = -10000;
};

class BHop final : public Check {
public:
    BHop()
        : Check("bhop", "BHop", CheckCategory::Movement, "Jump physics that vanilla cannot produce",
                "Analyzes every complete jump, from takeoff to landing. A vanilla jump on flat ground stays airborne "
                "for about 12 ticks; in the air, a player can accelerate by only 0.026 blocks per tick while "
                "momentum decays by 9% each tick. Vigil flags jumps that land too early on flat ground (low hops), "
                "chains of hops faster than vanilla, and mid-air turns or acceleration beyond vanilla air control. "
                "Jumps under ceilings, with Jump Boost, after knockback or with missing data are skipped.") {
        defAlert_ = 70.f;
        defHalfLife_ = 30.f;
        defMinEvidence_ = 3;
    }
    std::unique_ptr<PlayerCheckState> createState() const override { return std::make_unique<BHopState>(); }
    bool supportsTickSampling() const override { return false; }
    std::vector<std::string> requirements() const override { return {"Server positions", "Ground state", "Surface blocks"}; }

protected:
    void registerSettings() override {
        iShort_ = addIndicator("short_air", "Short air time",
                               "A jump on flat ground landed several ticks earlier than the vanilla arc allows.", 1.2f);
        iChain_ = addIndicator("hop_speed", "Hop speed", "A chain of hops faster than vanilla sprint-jumping.", 1.0f);
        iStrafe_ = addIndicator("air_strafe", "Air strafe",
                                "Mid-air change of direction beyond vanilla air control while keeping speed.", 1.3f);
        iAccel_ = addIndicator("air_accel", "Air acceleration", "Speed gained in the air beyond vanilla air control.", 1.0f);

        maxAir_ = &addInt("maxAirTicks", "Short hop limit", 8, {5, 11, 1, 0, "ticks"},
                          "Flat-ground jumps that land within this many ticks count as low hops. Vanilla is 12 ticks; "
                          "packet timing can shorten an observed jump by up to two.");
        preset(*maxAir_, 7, 8, 9);
        hopLimit_ = &addFloat("hopLimit", "Hop speed limit", 0.40, {0.36, 0.7, 0.005, 3, "b/tick"},
                              "Average speed across three or more consecutive hops above which the chain counts as "
                              "evidence. Scaled by Speed potions.");
        preset(*hopLimit_, 0.43, 0.40, 0.385);
        tolerance_ = &addFloat("airTolerance", "Air control tolerance", 0.06, {0.02, 0.2, 0.005, 3, "b/tick"},
                               "Extra velocity change allowed in the air on top of vanilla air control, covering the "
                               "1/32-block precision of position packets.", "Detection", true);
        tolerance_->warning = "Low tolerance lets position-packet rounding look like air control.";
        tolerance_->warnWhen = [](const Setting& s) { return s.value < 0.04; };
    }

public:
    void onTick(CheckContext& ctx, PlayerTrack& t, PlayerCheckState* base) override {
        auto* st = static_cast<BHopState*>(base);
        if (!t.landed() || t.airSegments().empty()) return;
        const AirSegment& seg = t.airSegments().back();
        const Exemptions& ex = ctx.exemptions(t);
        if (!seg.clean || ex.movement() || ex.ice || ex.slime) { st->chain = 0; return; }
        if (!seg.jumped) { st->chain = 0; return; }
        if (seg.dataGaps > seg.airTicks / 2) return;  // too little data to judge the arc

        const PlayerFrame& f = t.cur();
        const double mult = speedMultiplier(f);

        if (ctx.debugEnabled()) {
            ctx.debug(t, fmt("jump air=%d dy=%.3f apex=%.3f dist=%.2f v0=%.3f v1=%.3f gaps=%d ground=%d", seg.airTicks,
                             seg.landY - seg.takeoffY, seg.apexY - seg.takeoffY, seg.horizontalDistance,
                             seg.startVelocity.horizontalLength(), seg.endVelocity.horizontalLength(), seg.dataGaps,
                             seg.groundTicksBefore));
        }

        // 1) Short air time on flat ground.
        const bool flat = std::fabs(seg.landY - seg.takeoffY) < 0.1;
        if (flat && !seg.ceilingAtTakeoff && seg.jumpBoost < 0 && seg.airTicks <= maxAir_->i() && seg.airTicks >= 2) {
            const float sev = static_cast<float>(std::clamp((maxAir_->i() + 1.0 - seg.airTicks) / 4.0, 0.35, 1.0));
            ctx.emit(t, iShort_, sev, fmt("Jump landed after %d ticks on flat ground", seg.airTicks),
                     fmt("%s jumped and landed at the same height %d ticks later, peaking %.2f blocks up. A vanilla "
                         "jump on flat ground stays in the air for 12 ticks and peaks at 1.25 blocks; nothing was "
                         "above their head.",
                         t.name().c_str(), seg.airTicks, seg.apexY - seg.takeoffY),
                     {{"Air time", static_cast<double>(seg.airTicks), "ticks", static_cast<double>(maxAir_->i()), true},
                      {"Apex", seg.apexY - seg.takeoffY, "blocks"},
                      {"Height change", seg.landY - seg.takeoffY, "blocks"}});
        }

        // 2) Hop chains.
        if (seg.groundTicksBefore <= 2) {
            if (st->chain == 0) st->chainStart = seg.takeoffTick;
            ++st->chain;
        } else {
            st->chain = 1;
            st->chainStart = seg.takeoffTick;
        }
        // Head-hitter chains (two-block ceilings) and stair climbing are legitimately faster.
        const bool ceilingChain = seg.ceilingAtTakeoff || ctx.tick() - t.lastCeilingTick() <= 20;
        const double terrain = ctx.tick() - t.lastStepUpTick() <= 20 ? 1.12 : 1.0;
        if (st->chain >= 3 && !ceilingChain && ctx.tick() - st->lastChainEvidence > 40) {
            const int window = static_cast<int>(std::min<Tick>(ctx.tick() - st->chainStart, t.size() - 1));
            if (window >= 20) {
                const double avg = t.horizontalSpeed(window);
                const double limit = hopLimit_->value * mult * terrain;
                if (avg > limit) {
                    st->lastChainEvidence = ctx.tick();
                    const float sev = static_cast<float>(std::clamp((avg / limit - 1.0) / 0.25 + 0.3, 0.3, 1.0));
                    ctx.emit(t, iChain_, sev, fmt("%d hops at %.2f b/tick", st->chain, avg),
                             fmt("%s chained %d jumps with almost no ground time and averaged %.2f blocks per tick "
                                 "(%.1f m/s). Vanilla sprint-jumping averages 0.356; the limit in effect was %.2f.",
                                 t.name().c_str(), st->chain, avg, avg * 20.0, limit),
                             {{"Average speed", avg, "b/tick", limit, true},
                              {"Hops in chain", static_cast<double>(st->chain), ""}});
                }
            }
        }

        // 3) Air control envelope between ~tick 3 after takeoff and ~2 ticks before landing.
        // Start sample is centered ~2 ticks after takeoff, end sample ~2 ticks before landing.
        const int n = seg.airTicks - 4;
        const double v0 = seg.startVelocity.horizontalLength();
        if (n >= 3 && v0 >= 0.18) {
            const double decay = std::pow(mc::kAirFriction, n);
            const double control = mc::kAirControlSprint * (1.0 - decay) / (1.0 - mc::kAirFriction);
            const Vec3 dir = seg.startVelocity * (1.0 / v0);
            const Vec3 v1 = seg.endVelocity;
            const double along = v1.dot(dir);
            const double lateral = std::sqrt(std::max(0.0, v1.dot(v1) - along * along));
            const double tol = tolerance_->value;
            const double maxLateral = control + tol;
            const double maxSpeed = v0 * decay + control + tol;
            const double v1len = v1.horizontalLength();
            if (lateral > maxLateral && v1len >= 0.85 * v0) {
                const double turn = std::atan2(lateral, std::max(1e-6, along)) * 180.0 / 3.14159265358979323846;
                ctx.emit(t, iStrafe_, static_cast<float>(std::clamp((lateral - maxLateral) / 0.15 + 0.4, 0.4, 1.0)),
                         fmt("Turned %.0f° mid-air keeping %.0f%% of its speed", turn, v1len / v0 * 100.0),
                         fmt("Between takeoff and landing %s's direction changed by %.0f° while speed went from %.2f to "
                             "%.2f b/tick. Vanilla air control could add at most %.2f b/tick sideways over those %d ticks.",
                             t.name().c_str(), turn, v0, v1len, maxLateral, n),
                         {{"Sideways velocity gained", lateral, "b/tick", maxLateral, true},
                          {"Speed retained", v1len / v0 * 100.0, "%"},
                          {"Ticks", static_cast<double>(n), ""}});
            } else if (v1len > maxSpeed) {
                ctx.emit(t, iAccel_, static_cast<float>(std::clamp((v1len - maxSpeed) / 0.15 + 0.4, 0.4, 1.0)),
                         fmt("Accelerated to %.2f b/tick in the air", v1len),
                         fmt("%s left the ground at %.2f b/tick and was moving %.2f b/tick before landing. With 9%% "
                             "drag per tick and 0.026 b/tick of air control, vanilla allows at most %.2f after %d ticks.",
                             t.name().c_str(), v0, v1len, maxSpeed, n),
                         {{"Speed before landing", v1len, "b/tick", maxSpeed, true}, {"Speed after takeoff", v0, "b/tick"}});
            }
        }
    }

private:
    int iShort_ = 0, iChain_ = 0, iStrafe_ = 0, iAccel_ = 0;
    Setting* maxAir_ = nullptr;
    Setting* hopLimit_ = nullptr;
    Setting* tolerance_ = nullptr;
};

}  // namespace

std::unique_ptr<Check> makeBHop() { return std::make_unique<BHop>(); }

}  // namespace vigil::detection
