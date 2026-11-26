// Velocity (Anti-Knockback) — players who do not move when hit.
//
// The hurt animation reaches us at once, but the victim's knockback movement only arrives after
// the victim's own round trip and the server's position relay. Judging a fixed number of ticks
// after the hurt animation therefore reads every high-ping victim as "did not move". This check
// waits for the victim's response window (tab-list ping, or a conservative default), requires
// position updates from after the earliest possible response, and compares the rise and the push
// with what vanilla physics guarantees for that hit.

#include "../check.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace vigil::detection {
namespace {

struct VelocityState : PlayerCheckState {
    struct Pending {
        Tick hitTick;
        Vec3 pos0;
        Vec3 preVelocity;   // victim's horizontal velocity just before the hit
        Vec3 dir;           // knockback direction (attacker → victim)
        bool ceiling;
        std::uint8_t walls;
        std::string attacker;
        bool sprintHit;
        int delay;          // ticks before the victim's response can be visible
        int ping;           // -1 = unknown
    };
    std::vector<Pending> pending;
};

constexpr int kUnknownPingDelay = 6;   // ≈ 150 ms victim latency + relay cadence
constexpr int kMaxDelay = 12;

// Response delay in ticks: the victim receives the knockback, moves on its next tick, the server
// processes the movement and relays it every other tick.
int responseDelay(int pingMs) {
    if (pingMs < 0) return kUnknownPingDelay;
    return std::clamp(static_cast<int>(std::ceil(pingMs / 50.0)) + 3, 3, kMaxDelay);
}

// Minimum horizontal displacement vanilla knockback produces over `ticks`, for a victim whose
// velocity along the knockback direction was `along` before the hit (negative = running at the
// attacker). Knockback halves existing motion and adds 0.4 (+0.5 for a sprint hit); in the air
// motion decays by 9 % per tick and the victim's input can work against it by 0.026 per tick.
double expectedPush(double along, bool sprintHit, int ticks) {
    double v = 0.5 * along + 0.4 + (sprintHit ? 0.5 : 0.0);
    double d = 0.0;
    for (int i = 0; i < ticks; ++i) {
        d += v;
        v = v * mc::kAirFriction - mc::kAirControlSprint;
    }
    return std::max(0.0, d);
}

class Velocity final : public Check {
public:
    Velocity()
        : Check("velocity", "Velocity", CheckCategory::Combat, "Reduced or cancelled knockback",
                "Knockback lifts a grounded player about 0.4 blocks per tick and pushes them away from the attacker. "
                "Vigil waits for the victim's response to arrive (their ping plus the server's relay) and compares "
                "the rise and push with vanilla physics. Hits that may not be melee, and victims who are burning, "
                "against a wall, under a ceiling, in water, cobwebs or on ladders, are skipped.") {
        defAlert_ = 70.f;
        defHalfLife_ = 45.f;
        defMinEvidence_ = 3;
    }
    std::unique_ptr<PlayerCheckState> createState() const override { return std::make_unique<VelocityState>(); }
    bool supportsTickSampling() const override { return false; }
    std::vector<std::string> requirements() const override { return {"Server positions", "Hurt animations", "Player latency"}; }

protected:
    void registerSettings() override {
        iVert_ = addIndicator("vertical", "Vertical knockback",
                              "The victim barely rose after a hit that lifts any grounded player.", 1.5f);
        iHoriz_ = addIndicator("horizontal", "Horizontal knockback",
                               "The victim was not pushed away from the attacker.", 1.0f);
        evalTicks_ = &addInt("evalTicks", "Observation window", 5, {3, 10, 1, 0, "ticks"},
                             "How many ticks of the victim's response are observed, after their latency.");
        minRise_ = &addFloat("minRise", "Minimum rise", 0.12, {0.02, 0.4, 0.01, 2, "blocks"},
                             "A grounded victim rising less than this after a hit counts as evidence. Vanilla knockback "
                             "rises over 0.6 blocks.");
        preset(*minRise_, 0.07, 0.12, 0.18);
        minPush_ = &addFloat("minPush", "Minimum push", 0.15, {0.02, 0.6, 0.01, 2, "blocks"},
                             "Displacement away from the attacker below which the push counts as cancelled.");
        preset(*minPush_, 0.08, 0.15, 0.22);
    }

public:
    void onHitTaken(CheckContext& ctx, const CombatEvent& ev, PlayerTrack& attacker, PlayerTrack& victim,
                    PlayerCheckState* base) override {
        auto* st = static_cast<VelocityState*>(base);
        if (ev.candidates != 1 || !ev.victimOnGround) return;
        // Only clear melee geometry: an arrow, fire tick or poison next to a swinging player is not
        // a knockback hit from that player.
        if (ev.reach > 3.6 || ev.aimOutside > 15.0) return;
        const Exemptions& ex = ctx.exemptions(victim);
        if (ex.liquid || ex.web || ex.climbing || ex.riding || ex.teleport || ex.dead || ex.fresh || ex.stall) return;
        const int k = static_cast<int>(victim.tickAt(0) - ev.tick);
        if (!victim.has(k + 1)) return;
        const PlayerFrame& atHit = victim.back(std::max(0, k));
        if (atHit.burning || victim.back(k + 1).burning) return;
        VelocityState::Pending p;
        p.hitTick = ev.tick;
        p.pos0 = victim.back(k + 1).pos;
        p.preVelocity = victim.velocityFresh(k + 1, 4).horizontal();
        p.dir = ev.knockbackDir;
        p.ceiling = mc::isSolidish(atHit.blockAboveHead);
        p.walls = atHit.wallMask;
        p.attacker = attacker.name();
        p.sprintHit = ev.attackerSprinting;
        p.ping = atHit.pingMs;
        p.delay = responseDelay(atHit.pingMs);
        st->pending.push_back(p);
    }

    void onTick(CheckContext& ctx, PlayerTrack& t, PlayerCheckState* base) override {
        auto* st = static_cast<VelocityState*>(base);
        if (st->pending.empty()) return;
        const Tick now = ctx.tick();
        const int window = evalTicks_->i();
        for (auto it = st->pending.begin(); it != st->pending.end();) {
            if (now - it->hitTick < it->delay + window) { ++it; continue; }
            evaluate(ctx, t, *it);
            it = st->pending.erase(it);
        }
    }

private:
    void evaluate(CheckContext& ctx, PlayerTrack& t, const VelocityState::Pending& p) {
        const Tick now = ctx.tick();
        const int kHit = static_cast<int>(now - p.hitTick);
        if (!t.has(kHit + 1)) return;
        const Exemptions& ex = ctx.exemptions(t);
        if (ex.liquid || ex.web || ex.climbing || ex.teleport || ex.riding || ex.stall) return;

        // Data from after the earliest possible response, without a lag gap.
        int lateUpdates = 0;
        for (int k = 0; k < kHit; ++k) {
            if (t.updateTickAt(k) > p.hitTick + p.delay - 1 && (k == 0 || t.updateTickAt(k) != t.updateTickAt(k - 1))) ++lateUpdates;
        }
        const int gap = t.maxUpdateGap(kHit);
        const float quality = t.dataQuality(kHit);
        double maxRise = -1e9, push = -1e9;
        for (int k = kHit - 1; k >= 0; --k) {
            maxRise = std::max(maxRise, t.back(k).pos.y - p.pos0.y);
            push = std::max(push, (t.back(k).pos - p.pos0).horizontal().dot(p.dir));
        }
        const int observed = kHit;
        // Only the ticks after the response could arrive count toward the expectation, so an
        // unknown (assumed high) ping lowers what is expected rather than raising it.
        const double expected = expectedPush(p.preVelocity.dot(p.dir), p.sprintHit, std::clamp(observed - p.delay, 1, 6));
        const std::string pingText = p.ping >= 0 ? fmt("%d ms", p.ping) : std::string("unknown");

        if (ctx.debugEnabled()) {
            ctx.debug(t, fmt("kb from %s rise=%.3f push=%.3f expected=%.3f delay=%d late=%d gap=%d q=%.2f ceiling=%d walls=%02x",
                             p.attacker.c_str(), maxRise, push, expected, p.delay, lateUpdates, gap, quality, p.ceiling, p.walls));
        }
        if (lateUpdates < 2 || gap > 5 || quality < 0.4f) return;   // cannot see the response

        if (!p.ceiling && maxRise < minRise_->value) {
            const float sev = static_cast<float>(std::clamp(1.0 - std::max(0.0, maxRise) / minRise_->value * 0.6, 0.4, 1.0)) *
                              (0.6f + 0.4f * quality);
            ctx.emit(t, iVert_, sev, fmt("Rose %.2f blocks after %s's hit", std::max(0.0, maxRise), p.attacker.c_str()),
                     fmt("Knockback lifts a grounded player over 0.6 blocks. %s rose %.2f within %d ticks of the response "
                         "arriving (ping %s), with nothing above them.",
                         t.name().c_str(), std::max(0.0, maxRise), observed - p.delay, pingText.c_str()),
                     {{"Rise after hit", std::max(0.0, maxRise), "blocks", minRise_->value, true},
                      {"Push away from attacker", std::max(0.0, push), "blocks"},
                      {"Response window", static_cast<double>(observed), "ticks"},
                      {"Victim ping", static_cast<double>(std::max(0, p.ping)), "ms"},
                      {"Data quality", quality * 100.0, "%"}},
                     p.attacker);
        }
        // A wall in the push direction (or either neighbouring direction) stops the push.
        const int oct = octantOf(p.dir.x, p.dir.z);
        const std::uint8_t blocking = static_cast<std::uint8_t>((1u << oct) | (1u << ((oct + 1) & 7)) | (1u << ((oct + 7) & 7)));
        if ((p.walls & blocking) == 0 && p.dir.horizontalLength() > 0.5 && expected >= 0.3 && push < minPush_->value) {
            const float sev = static_cast<float>(std::clamp(1.0 - std::max(0.0, push) / expected, 0.35, 1.0)) * (0.6f + 0.4f * quality);
            ctx.emit(t, iHoriz_, sev, fmt("Pushed %.2f blocks by %s's hit", std::max(0.0, push), p.attacker.c_str()),
                     fmt("This hit should push %s about %.2f blocks away; they moved %.2f, with no wall behind them.",
                         t.name().c_str(), expected, std::max(0.0, push)),
                     {{"Push away from attacker", std::max(0.0, push), "blocks", minPush_->value, true},
                      {"Vanilla push", expected, "blocks", 0.0, false, EvidenceValue::Role::Expected},
                      {"Rise after hit", std::max(0.0, maxRise), "blocks"},
                      {"Victim ping", static_cast<double>(std::max(0, p.ping)), "ms"},
                      {"Data quality", quality * 100.0, "%"}},
                     p.attacker);
        }
    }

    int iVert_ = 0, iHoriz_ = 0;
    Setting* evalTicks_ = nullptr;
    Setting* minRise_ = nullptr;
    Setting* minPush_ = nullptr;
};

}  // namespace

std::unique_ptr<Check> makeVelocity() { return std::make_unique<Velocity>(); }

}  // namespace vigil::detection
