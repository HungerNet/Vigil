// BHop — airborne-segment physics.
//
// Each complete jump (takeoff → landing) is compared with the vanilla jump arc and vanilla air
// control. Data quality matters more here than anywhere: positions arrive every ~2 ticks and in
// bursts, so velocities are measured over 4-tick, update-aware spans, jumps with a lag gap are not
// judged, and a mid-air wall collision (one velocity axis stopping dead) is not read as a turn.

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
                "A vanilla jump on flat ground stays airborne for about 12 ticks, and in the air a player can add only "
                "0.026 blocks per tick while momentum decays by 9% per tick. Vigil flags low hops, hop chains faster "
                "than vanilla, and mid-air turns or acceleration beyond air control. Jumps under ceilings (anywhere "
                "over the hitbox), with Jump Boost, after knockback, into walls or with lag gaps are skipped.") {
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
        if (seg.dataGaps > seg.airTicks / 2 + 1) return;  // too little data to judge the arc
        // A lag gap shifts the observed takeoff or landing by several ticks.
        if (seg.maxGap > 4) { st->chain = 0; return; }

        if (ctx.debugEnabled()) {
            ctx.debug(t, fmt("jump air=%d dy=%.3f apex=%.3f dist=%.2f v0=%.3f v1=%.3f gaps=%d maxgap=%d ground=%d", seg.airTicks,
                             seg.landY - seg.takeoffY, seg.apexY - seg.takeoffY, seg.horizontalDistance,
                             seg.startVelocity.horizontalLength(), seg.endVelocity.horizontalLength(), seg.dataGaps, seg.maxGap,
                             seg.groundTicksBefore));
        }

        // 1) Short air time on flat ground.
        const bool flat = std::fabs(seg.landY - seg.takeoffY) < 0.1;
        if (flat && !seg.ceilingAtTakeoff && seg.jumpBoost < 0 && seg.airTicks <= maxAir_->i() && seg.airTicks >= 2) {
            const float sev = static_cast<float>(std::clamp((maxAir_->i() + 1.0 - seg.airTicks) / 4.0, 0.35, 1.0));
            ctx.emit(t, iShort_, sev, fmt("Landed after %d ticks on flat ground", seg.airTicks),
                     fmt("A vanilla jump stays up 12 ticks and peaks at 1.25 blocks; this one lasted %d and peaked at %.2f, "
                         "with nothing overhead.",
                         seg.airTicks, seg.apexY - seg.takeoffY),
                     {{"Air time", static_cast<double>(seg.airTicks), "ticks", static_cast<double>(maxAir_->i()), true},
                      {"Vanilla flat jump", static_cast<double>(mc::kFlatJumpAirTicks), "ticks", 0.0, false, EvidenceValue::Role::Expected},
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
                const double avg = t.horizontalSpeedFresh(window);
                const double mult = t.maxSpeedMultiplier(window + 10);
                const float q = t.dataQuality(window);
                const double limit = hopLimit_->value * mult * terrain;
                if (q >= 0.3f && avg > limit) {
                    st->lastChainEvidence = ctx.tick();
                    const float sev = static_cast<float>(std::clamp((avg / limit - 1.0) / 0.25 + 0.3, 0.3, 1.0)) * (0.6f + 0.4f * q);
                    ctx.emit(t, iChain_, sev, fmt("%d hops at %.2f b/tick", st->chain, avg),
                             fmt("%d jumps with almost no ground time at %.1f m/s. Vanilla sprint-jumping averages 7.1 m/s; "
                                 "the limit here is %.1f m/s.",
                                 st->chain, avg * 20.0, limit * 20.0),
                             {{"Average speed", avg, "b/tick", limit, true},
                              {"Vanilla sprint-jump", mc::kSprintJumpAverage * mult, "b/tick", 0.0, false, EvidenceValue::Role::Expected},
                              {"Hops in chain", static_cast<double>(st->chain), ""},
                              {"Movement multiplier", mult, "x"},
                              {"Data quality", q * 100.0, "%"}});
                }
            }
        }

        // 3) Air control between the start sample (centered 3 ticks after takeoff) and the end
        // sample (centered 3 ticks before landing).
        const int n = seg.airTicks - 6;
        const double v0 = seg.startVelocity.horizontalLength();
        // Mid-air wall collision: one axis stops dead while the other keeps its speed. Vanilla
        // collision, not a turn.
        auto axisStopped = [](double a0, double a1) { return std::fabs(a0) > 0.08 && std::fabs(a1) < 0.012; };
        const bool collided = axisStopped(seg.startVelocity.x, seg.endVelocity.x) || axisStopped(seg.startVelocity.z, seg.endVelocity.z);
        if (n >= 3 && v0 >= 0.18 && seg.endVelocity.horizontalLength() > 0.0 && !collided) {
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
                         fmt("Turned %.0f° mid-air at %.0f%% speed", turn, v1len / v0 * 100.0),
                         fmt("Air control allows at most %.2f b/tick of sideways velocity over %d ticks; this jump "
                             "gained %.2f while keeping its speed.",
                             maxLateral, n, lateral),
                         {{"Sideways velocity gained", lateral, "b/tick", maxLateral, true},
                          {"Speed retained", v1len / v0 * 100.0, "%"},
                          {"Ticks", static_cast<double>(n), ""}});
            } else if (v1len > maxSpeed) {
                ctx.emit(t, iAccel_, static_cast<float>(std::clamp((v1len - maxSpeed) / 0.15 + 0.4, 0.4, 1.0)),
                         fmt("Sped up to %.2f b/tick in the air", v1len),
                         fmt("Left the ground at %.2f b/tick. With 9%% drag and 0.026 b/tick of air control, vanilla "
                             "allows at most %.2f after %d ticks.",
                             v0, maxSpeed, n),
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
