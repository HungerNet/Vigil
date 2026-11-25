// Velocity (Anti-Knockback) — players who do not move when hit.

#include "../check.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace vigil::detection {
namespace {

struct VelocityState : PlayerCheckState {
    struct Pending {
        Tick hitTick;
        double y0;
        Vec3 pos0;
        Vec3 dir;
        bool ceiling;
        bool wall;
        std::string attacker;
        bool sprintHit;
    };
    std::vector<Pending> pending;
};

class Velocity final : public Check {
public:
    Velocity()
        : Check("velocity", "Velocity", CheckCategory::Combat, "Reduced or cancelled knockback",
                "After an attributed melee hit on a player standing on the ground, vanilla knockback lifts them about "
                "0.36 blocks per tick and pushes them away from the attacker. Vigil follows the victim's server "
                "positions for the next ticks and compares the rise and the push with what vanilla physics guarantees. "
                "Hits against walls, under ceilings, in water, cobwebs or on ladders are skipped.") {
        defAlert_ = 70.f;
        defHalfLife_ = 45.f;
        defMinEvidence_ = 3;
    }
    std::unique_ptr<PlayerCheckState> createState() const override { return std::make_unique<VelocityState>(); }
    bool supportsTickSampling() const override { return false; }

protected:
    void registerSettings() override {
        iVert_ = addIndicator("vertical", "Vertical knockback",
                              "The victim barely rose after a hit that lifts any grounded player.", 1.5f);
        iHoriz_ = addIndicator("horizontal", "Horizontal knockback",
                               "The victim was not pushed away from the attacker.", 1.0f);
        evalTicks_ = &addInt("evalTicks", "Observation window", 5, {3, 10, 1, 0, "ticks"},
                             "How many ticks after the hit the victim's response is observed.");
        minRise_ = &addFloat("minRise", "Minimum rise", 0.12, {0.02, 0.4, 0.01, 2, "blocks"},
                             "A grounded victim rising less than this after a hit counts as evidence. Vanilla knockback "
                             "rises over 0.6 blocks within three ticks.");
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
        const Exemptions& ex = ctx.exemptions(victim);
        if (ex.liquid || ex.web || ex.climbing || ex.riding || ex.teleport || ex.dead || ex.fresh) return;
        const int k = static_cast<int>(victim.tickAt(0) - ev.tick);
        if (!victim.has(k + 1)) return;
        const PlayerFrame& atHit = victim.back(std::max(0, k));
        VelocityState::Pending p;
        p.hitTick = ev.tick;
        p.y0 = victim.back(k + 1).pos.y;
        p.pos0 = victim.back(k + 1).pos;
        p.dir = ev.knockbackDir;
        p.ceiling = mc::isSolidish(atHit.blockAboveHead);
        p.wall = atHit.wallNear;
        p.attacker = attacker.name();
        p.sprintHit = ev.attackerSprinting;
        st->pending.push_back(p);
    }

    void onTick(CheckContext& ctx, PlayerTrack& t, PlayerCheckState* base) override {
        auto* st = static_cast<VelocityState*>(base);
        if (st->pending.empty()) return;
        const Tick now = ctx.tick();
        const int window = evalTicks_->i();
        for (auto it = st->pending.begin(); it != st->pending.end();) {
            if (now - it->hitTick < window) { ++it; continue; }
            evaluate(ctx, t, *it, window);
            it = st->pending.erase(it);
        }
    }

private:
    void evaluate(CheckContext& ctx, PlayerTrack& t, const VelocityState::Pending& p, int window) {
        const Tick now = ctx.tick();
        const int kHit = static_cast<int>(now - p.hitTick);
        if (!t.has(kHit + 1)) return;
        const Exemptions& ex = ctx.exemptions(t);
        if (ex.liquid || ex.web || ex.climbing || ex.teleport || ex.riding) return;

        double maxRise = -1e9;
        int updates = 0;
        for (int k = kHit - 1; k >= 0 && k >= kHit - window; --k) {
            maxRise = std::max(maxRise, t.back(k).pos.y - p.y0);
        }
        for (int k = kHit + 2; k >= 0 && k >= kHit - window; --k) {
            if (t.has(k + 1) && (t.back(k).pos - t.back(k + 1).pos).length() > 1e-9) ++updates;
        }
        if (updates == 0) return;  // no data from this player in the window (lag) → cannot judge
        const int kPush = std::max(0, kHit - 3);
        const double push = (t.back(kPush).pos - p.pos0).horizontal().dot(p.dir);

        if (ctx.debugEnabled()) {
            ctx.debug(t, fmt("kb from %s rise=%.3f push=%.3f updates=%d ceiling=%d wall=%d", p.attacker.c_str(), maxRise, push,
                             updates, p.ceiling, p.wall));
        }

        if (!p.ceiling && maxRise < minRise_->value) {
            const float sev = static_cast<float>(std::clamp(1.0 - std::max(0.0, maxRise) / minRise_->value * 0.6, 0.4, 1.0));
            ctx.emit(t, iVert_, sev, fmt("Rose %.2f blocks after a hit from %s", std::max(0.0, maxRise), p.attacker.c_str()),
                     fmt("%s was on the ground when %s hit them. Vanilla knockback lifts a grounded player more than "
                         "0.6 blocks within three ticks; over %d ticks %s rose %.2f blocks, with no ceiling above.",
                         t.name().c_str(), p.attacker.c_str(), window, t.name().c_str(), std::max(0.0, maxRise)),
                     {{"Rise after hit", std::max(0.0, maxRise), "blocks", minRise_->value, true},
                      {"Push away from attacker", push, "blocks"},
                      {"Position updates", static_cast<double>(updates), ""}},
                     p.attacker);
        }
        if (!p.wall && p.dir.horizontalLength() > 0.5 && push < minPush_->value) {
            const float sev = static_cast<float>(std::clamp(1.0 - std::max(0.0, push) / minPush_->value * 0.6, 0.35, 1.0));
            ctx.emit(t, iHoriz_, sev, fmt("Pushed %.2f blocks by a hit from %s", push, p.attacker.c_str()),
                     fmt("Knockback should push %s at least half a block away from %s within three ticks%s. The "
                         "measured displacement in the knockback direction was %.2f blocks, and no wall was in the way.",
                         t.name().c_str(), p.attacker.c_str(), p.sprintHit ? " (more for a sprint hit)" : "", push),
                     {{"Push away from attacker", push, "blocks", minPush_->value, true},
                      {"Rise after hit", std::max(0.0, maxRise), "blocks"}},
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
