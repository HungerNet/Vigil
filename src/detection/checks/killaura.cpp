// KillAura — redesigned from first principles.
//
// The previous implementation compared the *local* player's yaw against the bearing to the
// suspect and treated every arm-swing animation tick as a new attack, so it effectively flagged
// anyone who swung near you while you looked at them. This version judges the attacker's own
// aim at attributed hits, using four independent indicators.

#include "../check.h"

#include <algorithm>
#include <cmath>
#include <deque>

namespace vigil::detection {
namespace {

struct KillAuraState : PlayerCheckState {
    struct Hit {
        Tick tick;
        int victim;
        double bearing;  // yaw from attacker to victim at the hit
        std::string victimName;
    };
    std::deque<Hit> hits;
    // Tracking analysis against the last victim.
    int trackVictim = -1;
    Tick trackUntil = 0;
    int samples = 0;
    double errSum = 0.0;
    double motionSum = 0.0;
    double lastBearing = 0.0;
    bool haveBearing = false;
    Tick lastTrackingEvidence = -10000;
};

class KillAura final : public Check {
public:
    KillAura()
        : Check("killaura", "KillAura", CheckCategory::Combat,
                "Automated aiming and target selection",
                "Evaluates every hit Vigil can attribute to a player. It reconstructs where the attacker was looking "
                "when the victim's hurt animation began, compensating for latency and the 1.4° precision of "
                "rotation packets, and looks for behavior a human hand cannot produce: hits outside the field of "
                "view, several targets struck in the same instant, instantaneous snaps that land exactly on the "
                "hitbox, and tracking that stays locked to a moving target with machine precision.") {
        defAlert_ = 70.f;
        defHalfLife_ = 45.f;
        defMinEvidence_ = 3;
    }

    std::unique_ptr<PlayerCheckState> createState() const override { return std::make_unique<KillAuraState>(); }
    bool supportsTickSampling() const override { return false; }

protected:
    void registerSettings() override {
        iOutside_ = addIndicator("outside_view", "Hit outside view",
                                 "The attacker's aim was far from the victim's hitbox when the hit landed.", 2.0f);
        iMulti_ = addIndicator("multi_target", "Multi-target",
                               "Two different players were hit within a few ticks at widely separated angles.", 2.0f);
        iSnap_ = addIndicator("snap", "Snap to target",
                              "A large rotation in the ticks before a hit that ends exactly on the target and stops.", 0.8f);
        iTrack_ = addIndicator("tracking", "Robotic tracking",
                               "Aim stays locked onto a moving target with near-zero error for a sustained period.", 1.0f);

        unambiguous_ = &addBool("unambiguous", "Unambiguous hits only", true,
                                "Only judge hits where exactly one swinging player could have caused the damage.");
        unambiguous_->warning = "Ambiguous hits can be attributed to the wrong player in crowded fights.";
        unambiguous_->warnWhen = [](const Setting& s) { return !s.b(); };
        outsideAngle_ = &addFloat("outsideAngle", "View angle limit", 40.0, {15.0, 90.0, 1.0, 0, "°"},
                                  "How far outside the victim's hitbox the attacker's aim may be at the moment of a hit "
                                  "before it counts as evidence.");
        preset(*outsideAngle_, 55.0, 40.0, 30.0);
        multiWindow_ = &addInt("multiWindow", "Multi-target window", 3, {0, 10, 1, 0, "ticks"},
                               "Two hits on different players within this many ticks are compared for multi-targeting.");
        multiSeparation_ = &addFloat("multiSeparation", "Target separation", 60.0, {20.0, 180.0, 5.0, 0, "°"},
                                     "Minimum angle between two victims (as seen by the attacker) for near-simultaneous hits to count.");
        preset(*multiSeparation_, 75.0, 60.0, 45.0);
        snapAngle_ = &addFloat("snapAngle", "Snap rotation", 45.0, {15.0, 180.0, 5.0, 0, "°"},
                               "Rotation within two ticks before a hit that qualifies as a snap.", "Detection", true);
        snapError_ = &addFloat("snapError", "Snap landing error", 3.0, {0.5, 10.0, 0.5, 1, "°"},
                               "A snap must end within this angle of the victim's center.", "Detection", true);
        trackError_ = &addFloat("trackError", "Tracking error limit", 1.6, {0.5, 5.0, 0.1, 1, "°"},
                                "Average aim error to the victim's center below which tracking is considered robotic. "
                                "Rotation packets are 1.4° coarse, so perfect tracking reads as roughly 0.7°.",
                                "Detection", true);
        trackMotion_ = &addFloat("trackMotion", "Target motion", 4.0, {1.0, 20.0, 0.5, 1, "°/tick"},
                                 "The victim must move across the attacker's view at least this fast for tracking to be judged.",
                                 "Detection", true);
    }

public:
    void onCombat(CheckContext& ctx, const CombatEvent& ev, PlayerTrack& attacker, PlayerTrack& victim,
                  PlayerCheckState* stateBase) override {
        auto* st = static_cast<KillAuraState*>(stateBase);
        if (unambiguous_->b() && ev.candidates != 1) return;
        const Exemptions& ex = ctx.exemptions(attacker);
        if (ex.teleport || ex.riding || ex.dead) return;

        // 1) Hit outside view.
        const double limit = outsideAngle_->value;
        if (ev.aimOutside > limit) {
            const float sev = static_cast<float>(std::clamp((ev.aimOutside - limit) / 40.0 + 0.5, 0.4, 1.0));
            ctx.emit(attacker, iOutside_, sev, fmt("Hit %s while aiming %.0f° away", victim.name().c_str(), ev.aimOutside),
                     fmt("At the moment %s's hurt animation began, %s was looking %.0f° outside the hitbox. Vigil "
                         "searched the whole latency window for any aim that could explain the hit. A vanilla hit "
                         "needs the crosshair on the hitbox.",
                         victim.name().c_str(), attacker.name().c_str(), ev.aimOutside),
                     {{"Aim outside hitbox", ev.aimOutside, "°", limit, true},
                      {"Yaw error to center", ev.yawError, "°"},
                      {"Distance", ev.reach, "blocks"}},
                     victim.name());
        }

        // 2) Multi-target.
        const double bearing = yawTowards(ev.attackerEye, ev.victimPos);
        for (const auto& h : st->hits) {
            if (h.victim == ev.victim) continue;
            const Tick gap = ev.tick - h.tick;
            if (gap < 0 || gap > multiWindow_->i()) continue;
            const double sep = std::fabs(wrapDegrees(bearing - h.bearing));
            if (sep < multiSeparation_->value) continue;
            const float sev = static_cast<float>(std::clamp(0.55 + sep / 360.0 + (gap == 0 ? 0.25 : 0.0), 0.5, 1.0));
            ctx.emit(attacker, iMulti_, sev,
                     fmt("Hit %s and %s %lld tick%s apart, %.0f° apart", h.victimName.c_str(), victim.name().c_str(),
                         static_cast<long long>(gap), gap == 1 ? "" : "s", sep),
                     fmt("Two different players were struck %.0f ms apart while standing %.0f° apart in %s's view. "
                         "Turning that far and landing both hits within %lld tick%s is not achievable by hand.",
                         gap * 50.0, sep, attacker.name().c_str(), static_cast<long long>(gap), gap == 1 ? "" : "s"),
                     {{"Angle between targets", sep, "°", multiSeparation_->value, true},
                      {"Time between hits", gap * 50.0, "ms"}},
                     victim.name());
            break;
        }
        st->hits.push_back({ev.tick, ev.victim, bearing, victim.name()});
        while (st->hits.size() > 12) st->hits.pop_front();

        // 3) Snap to target: rotation in the two ticks before the hit, then stop.
        const int k = static_cast<int>(attacker.tickAt(0) - ev.tick);
        if (attacker.has(k + 2) && k >= 2) {
            const double before = std::fabs(wrapDegrees(attacker.back(k).yaw - attacker.back(k + 2).yaw));
            const double after = std::fabs(wrapDegrees(attacker.back(k - 2).yaw - attacker.back(k).yaw));
            if (before >= snapAngle_->value && ev.yawError <= snapError_->value && after <= 3.0) {
                const float sev = static_cast<float>(std::clamp(before / 120.0, 0.35, 1.0));
                ctx.emit(attacker, iSnap_, sev, fmt("Snapped %.0f° onto %s", before, victim.name().c_str()),
                         fmt("%s rotated %.0f° in the 100 ms before hitting %s, landed %.1f° from the center of the "
                             "hitbox and then stopped (%.1f° over the next 100 ms). Human flicks overshoot or keep moving.",
                             attacker.name().c_str(), before, victim.name().c_str(), ev.yawError, after),
                         {{"Rotation before hit", before, "°", snapAngle_->value, true},
                          {"Landing error", ev.yawError, "°", snapError_->value, true},
                          {"Rotation after hit", after, "°"}},
                         victim.name());
            }
        }

        // Arm tracking analysis on this victim.
        if (st->trackVictim != ev.victim) {
            st->trackVictim = ev.victim;
            st->samples = 0;
            st->errSum = st->motionSum = 0.0;
            st->haveBearing = false;
        }
        st->trackUntil = ev.tick + 20;
    }

    void onTick(CheckContext& ctx, PlayerTrack& subject, PlayerCheckState* stateBase) override {
        auto* st = static_cast<KillAuraState*>(stateBase);
        if (st->trackVictim < 0 || ctx.tick() > st->trackUntil) {
            st->trackVictim = -1;
            return;
        }
        PlayerTrack* victim = ctx.track(st->trackVictim);
        if (!victim || victim->empty()) { st->trackVictim = -1; return; }
        const PlayerFrame& a = subject.cur();
        const Vec3 eye{a.pos.x, a.pos.y + mc::kEyeHeight, a.pos.z};
        const Vec3 center = Box::player(victim->cur().pos).center();
        const double dist = (center - eye).horizontalLength();
        if (dist < 1.5 || dist > 6.0) return;
        const double bearing = yawTowards(eye, center);
        const double err = std::fabs(wrapDegrees(bearing - a.yaw));
        if (st->haveBearing) {
            st->errSum += err;
            st->motionSum += std::fabs(wrapDegrees(bearing - st->lastBearing));
            ++st->samples;
        }
        st->lastBearing = bearing;
        st->haveBearing = true;

        if (st->samples >= 20) {
            const double meanErr = st->errSum / st->samples;
            const double meanMotion = st->motionSum / st->samples;
            if (ctx.debugEnabled()) {
                ctx.debug(subject, fmt("tracking %s meanErr=%.2f° motion=%.2f°/t n=%d", victim->name().c_str(), meanErr,
                                       meanMotion, st->samples));
            }
            if (meanMotion >= trackMotion_->value && meanErr <= trackError_->value && ctx.tick() - st->lastTrackingEvidence > 40) {
                st->lastTrackingEvidence = ctx.tick();
                const float sev = static_cast<float>(std::clamp((trackError_->value - meanErr) / trackError_->value + 0.4, 0.4, 1.0));
                ctx.emit(subject, iTrack_, sev, fmt("Locked onto %s (%.1f° average error)", victim->name().c_str(), meanErr),
                         fmt("For one second %s's aim stayed within %.1f° of %s's center on average while %s moved "
                             "%.1f° per tick across the view. Rotation packets are only 1.4° precise, so this is as "
                             "close to perfect as the data can show.",
                             subject.name().c_str(), meanErr, victim->name().c_str(), victim->name().c_str(), meanMotion),
                         {{"Average aim error", meanErr, "°", trackError_->value, true},
                          {"Target angular speed", meanMotion, "°/tick", trackMotion_->value, true},
                          {"Samples", static_cast<double>(st->samples), "ticks"}},
                         victim->name());
            }
            st->samples = 0;
            st->errSum = st->motionSum = 0.0;
        }
    }

private:
    int iOutside_ = 0, iMulti_ = 0, iSnap_ = 0, iTrack_ = 0;
    Setting* unambiguous_ = nullptr;
    Setting* outsideAngle_ = nullptr;
    Setting* multiWindow_ = nullptr;
    Setting* multiSeparation_ = nullptr;
    Setting* snapAngle_ = nullptr;
    Setting* snapError_ = nullptr;
    Setting* trackError_ = nullptr;
    Setting* trackMotion_ = nullptr;
};

}  // namespace

std::unique_ptr<Check> makeKillAura() { return std::make_unique<KillAura>(); }

}  // namespace vigil::detection
