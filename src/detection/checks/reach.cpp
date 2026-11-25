// Reach — melee hits landed from farther than vanilla allows.

#include "../check.h"

#include <algorithm>
#include <cmath>
#include <deque>

namespace vigil::detection {
namespace {

struct ReachState : PlayerCheckState {
    std::deque<double> recent;
    int sinceConsistency = 0;
};

class Reach final : public Check {
public:
    Reach()
        : Check("reach", "Reach", CheckCategory::Combat, "Melee hits from beyond vanilla range",
                "For every hit Vigil can attribute to a single attacker, it measures the distance from the attacker's "
                "eyes to the nearest point of the victim's hitbox (grown by 0.1 blocks, exactly as the game does). "
                "Because the attacker saw the victim slightly in the past, Vigil takes the closest the two players "
                "were over a latency window, so the reported distance is the most charitable possible reading. "
                "Vanilla reach is 3.00 blocks.") {
        defAlert_ = 70.f;
        defHalfLife_ = 40.f;
        defMinEvidence_ = 3;
    }
    std::unique_ptr<PlayerCheckState> createState() const override { return std::make_unique<ReachState>(); }
    bool supportsTickSampling() const override { return false; }

protected:
    void registerSettings() override {
        iDist_ = addIndicator("distance", "Extended hit", "A hit landed beyond the reach threshold.", 1.0f);
        iConsistent_ = addIndicator("consistency", "Consistent extension",
                                    "Most recent hits sit slightly beyond vanilla range, a signature of small reach boosts.", 0.8f);
        threshold_ = &addFloat("threshold", "Reach threshold", 3.30, {3.0, 4.5, 0.05, 2, "blocks"},
                               "Latency-compensated distance beyond which a single hit counts as evidence.");
        threshold_->warning = "Thresholds this close to 3.00 blocks leave almost no room for measurement error.";
        threshold_->warnWhen = [](const Setting& s) { return s.value < 3.15; };
        preset(*threshold_, 3.45, 3.30, 3.20);
        consistentLimit_ = &addFloat("consistentLimit", "Consistency limit", 3.12, {3.0, 3.6, 0.01, 2, "blocks"},
                                     "Distance used by the consistency indicator to decide whether a hit is extended.",
                                     "Detection", true);
        consistentShare_ = &addFloat("consistentShare", "Consistency share", 0.7, {0.3, 1.0, 0.05, 2, ""},
                                     "Fraction of the last 10 hits that must exceed the consistency limit.", "Detection", true);
    }

public:
    void onCombat(CheckContext& ctx, const CombatEvent& ev, PlayerTrack& attacker, PlayerTrack& victim,
                  PlayerCheckState* stateBase) override {
        auto* st = static_cast<ReachState*>(stateBase);
        if (ev.candidates != 1) return;  // reach needs certainty about the attacker
        const Exemptions& ex = ctx.exemptions(attacker);
        if (ex.teleport || ex.riding || ex.dead) return;
        if (ctx.exemptions(victim).teleport) return;
        // Victim moving unusually fast (e.g. launched) makes positions unreliable.
        if (victim.horizontalSpeed(3) > 1.2) return;

        const double limit = threshold_->value;
        if (ctx.debugEnabled()) {
            ctx.debug(attacker, fmt("hit %s reach=%.3f raw=%.3f aim=%.1f°", victim.name().c_str(), ev.reach, ev.reachRaw, ev.aimOutside));
        }
        if (ev.reach > limit) {
            const float sev = static_cast<float>(std::clamp((ev.reach - limit) / 0.5 + 0.35, 0.35, 1.0));
            ctx.emit(attacker, iDist_, sev, fmt("Hit %s from %.2f blocks", victim.name().c_str(), ev.reach),
                     fmt("Measured from %s's eyes to the nearest point of %s's hitbox, using the closest the two were "
                         "over the latency window. Even this most favorable reading is %.2f blocks beyond vanilla reach "
                         "(3.00).",
                         attacker.name().c_str(), victim.name().c_str(), ev.reach - mc::kVanillaReach),
                     {{"Compensated reach", ev.reach, "blocks", limit, true},
                      {"Uncompensated reach", ev.reachRaw, "blocks"},
                      {"Aim outside hitbox", ev.aimOutside, "°"}},
                     victim.name());
        }

        st->recent.push_back(ev.reach);
        while (st->recent.size() > 10) st->recent.pop_front();
        ++st->sinceConsistency;
        if (st->recent.size() >= 10 && st->sinceConsistency >= 10) {
            int over = 0;
            std::vector<double> sorted(st->recent.begin(), st->recent.end());
            std::sort(sorted.begin(), sorted.end());
            for (double r : sorted) if (r > consistentLimit_->value) ++over;
            const double share = over / 10.0;
            const double median = (sorted[4] + sorted[5]) * 0.5;
            if (share >= consistentShare_->value && median > consistentLimit_->value) {
                st->sinceConsistency = 0;
                ctx.emit(attacker, iConsistent_, static_cast<float>(std::clamp(share, 0.5, 1.0)),
                         fmt("%d of last 10 hits beyond %.2f blocks (median %.2f)", over, consistentLimit_->value, median),
                         "Legitimate players land most hits well inside 3 blocks; a distribution centered just past "
                         "vanilla range is characteristic of a small, constant reach extension.",
                         {{"Median reach", median, "blocks", consistentLimit_->value, true},
                          {"Share beyond limit", share * 100.0, "%", consistentShare_->value * 100.0, true}});
            }
        }
    }

private:
    int iDist_ = 0, iConsistent_ = 0;
    Setting* threshold_ = nullptr;
    Setting* consistentLimit_ = nullptr;
    Setting* consistentShare_ = nullptr;
};

}  // namespace

std::unique_ptr<Check> makeReach() { return std::make_unique<Reach>(); }

}  // namespace vigil::detection
