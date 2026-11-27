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
                "Measures every attributed hit from the attacker's eyes to the nearest point of the victim's hitbox, "
                "taking the closest the two were over the latency window, so the reading is always the most "
                "charitable one. Vanilla reach is 3.00 blocks.") {
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
        threshold_ = addFloat("threshold", "Reach threshold", 3.30, {3.0, 4.5, 0.05, 2, "blocks"},
                              "Latency-compensated distance beyond which a single hit counts as evidence.");
        spec(threshold_).warning = "Thresholds this close to 3.00 blocks leave almost no room for measurement error.";
        spec(threshold_).warnWhen = [](const ParamView& s) { return s.value < 3.15; };
        preset(threshold_, 3.45, 3.30, 3.20);
        consistentLimit_ = addFloat("consistentLimit", "Consistency limit", 3.12, {3.0, 3.6, 0.01, 2, "blocks"},
                                    "Distance used by the consistency indicator to decide whether a hit is extended.",
                                    "Detection", true);
        consistentShare_ = addFloat("consistentShare", "Consistency share", 0.7, {0.3, 1.0, 0.05, 2, ""},
                                    "Fraction of the last 10 hits that must exceed the consistency limit.", "Detection", true);
    }

public:
    void onCombat(CheckContext& ctx, const CombatEvent& ev, PlayerTrack& attacker, PlayerTrack& victim,
                  PlayerCheckState* stateBase) override {
        auto* st = static_cast<ReachState*>(stateBase);
        const EngineConfig& cfg = ctx.config();
        if (ev.candidates != 1) return;  // reach needs certainty about the attacker
        const Exemptions& ex = ctx.exemptions(attacker);
        if (ex.teleport || ex.riding || ex.dead) return;
        if (ctx.exemptions(victim).teleport) return;
        // Victim moving unusually fast (e.g. launched) makes positions unreliable.
        if (victim.horizontalSpeed(3) > 1.2) return;

        const double limit = cfg.value(threshold_);
        if (ctx.debugEnabled()) {
            ctx.debug(attacker, fmt("hit %s reach=%.3f raw=%.3f aim=%.1f°", victim.name().c_str(), ev.reach, ev.reachRaw, ev.aimOutside));
        }
        if (ev.reach > limit) {
            const float sev = static_cast<float>(std::clamp((ev.reach - limit) / 0.5 + 0.35, 0.35, 1.0));
            ctx.emit(attacker, iDist_, sev, fmt("Hit %s from %.2f blocks", victim.name().c_str(), ev.reach),
                     fmt("Vanilla reach is 3.00 blocks. Even the most favorable reading across the latency window "
                         "(eyes to the nearest point of the hitbox) is %.2f blocks beyond it.",
                         ev.reach - mc::kVanillaReach),
                     {{"Compensated reach", ev.reach, "blocks", limit, true},
                      {"Vanilla reach", mc::kVanillaReach, "blocks", 0.0, false, EvidenceValue::Role::Expected},
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
            for (double r : sorted) if (r > cfg.value(consistentLimit_)) ++over;
            const double share = over / 10.0;
            const double median = (sorted[4] + sorted[5]) * 0.5;
            if (share >= cfg.value(consistentShare_) && median > cfg.value(consistentLimit_)) {
                st->sinceConsistency = 0;
                ctx.emit(attacker, iConsistent_, static_cast<float>(std::clamp(share, 0.5, 1.0)),
                         fmt("%d of last 10 hits beyond %.2f blocks (median %.2f)", over, cfg.value(consistentLimit_), median),
                         "Legitimate players land most hits well inside 3 blocks; a distribution centered just past "
                         "vanilla range is characteristic of a small, constant reach extension.",
                         {{"Median reach", median, "blocks", cfg.value(consistentLimit_), true},
                          {"Share beyond limit", share * 100.0, "%", cfg.value(consistentShare_) * 100.0, true}});
            }
        }
    }

private:
    int iDist_ = 0, iConsistent_ = 0;
    ParamId threshold_ = kNoParam;
    ParamId consistentLimit_ = kNoParam;
    ParamId consistentShare_ = kNoParam;
};

}  // namespace

std::unique_ptr<Check> makeReach() { return std::make_unique<Reach>(); }

}  // namespace vigil::detection
