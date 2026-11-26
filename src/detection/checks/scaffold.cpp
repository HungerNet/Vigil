// Scaffold family — ported from the original Keep-Y, FlatScaff and LegitScaff checks.
//
// The originals counted every tick of the arm-swing animation as a block placement, which
// inflated "placements" roughly fivefold; their thresholds were tuned against that error. These
// ports count real swing starts (each right-click placement swings once) and are re-tuned.

#include "../check.h"

#include <algorithm>
#include <cmath>
#include <deque>

namespace vigil::detection {
namespace {

bool placementSwing(const PlayerTrack& t, double minPitch) {
    const PlayerFrame& f = t.cur();
    return t.swingStarted() && mc::isBlockItem(f.heldItem) && f.pitch >= minPitch;
}

// ── Keep-Y ────────────────────────────────────────────────────────────────────────────────────

struct KeepYState : PlayerCheckState {
    bool active = false;
    int placements = 0;
    double startY = 0.0;
    Tick start = 0;
};

class KeepY final : public Check {
public:
    KeepY()
        : Check("keepy", "Scaffold (Keep-Y)", CheckCategory::World, "Bridging while jumping without gaining height",
                "Watches jumps taken while looking down with blocks in hand. Placing several blocks during one jump "
                "and landing back at the starting height, at sprinting speed, is the signature of scaffolds that "
                "keep the player's Y level fixed. Legitimate jump-bridging is slower; the speed requirement and the "
                "evidence gate separate the two.") {
        defAlert_ = 72.f;
        defHalfLife_ = 30.f;
        defMinEvidence_ = 4;
    }
    std::unique_ptr<PlayerCheckState> createState() const override { return std::make_unique<KeepYState>(); }
    bool supportsTickSampling() const override { return false; }

protected:
    void registerSettings() override {
        iJump_ = addIndicator("flat_jump", "Flat jump bridge", "Several placements in one jump that landed at the takeoff height.", 1.0f);
        pitch_ = &addFloat("pitch", "Look-down angle", 55.0, {30.0, 90.0, 1.0, 0, "°"},
                           "Minimum pitch (looking down) for a jump to be considered a bridging jump.");
        placed_ = &addInt("placements", "Placements per jump", 3, {2, 8, 1, 0, "blocks"},
                          "Blocks that must be placed during one jump.");
        preset(*placed_, 4, 3, 2);
        speed_ = &addFloat("minSpeed", "Minimum speed", 0.24, {0.1, 0.5, 0.01, 2, "b/tick"},
                           "Average horizontal speed during the jump. Separates scaffolds from legitimate jump-bridging.");
        maxGain_ = &addFloat("maxGain", "Height tolerance", 0.3, {0.05, 1.0, 0.05, 2, "blocks"},
                             "Maximum height difference between takeoff and landing.", "Detection", true);
    }

public:
    void onTick(CheckContext& ctx, PlayerTrack& t, PlayerCheckState* base) override {
        auto* st = static_cast<KeepYState*>(base);
        const PlayerFrame& f = t.cur();
        const Exemptions& ex = ctx.exemptions(t);
        if (ex.movement()) { st->active = false; return; }
        if (t.tookOff() && t.currentAir().jumped && mc::isBlockItem(f.heldItem) && f.pitch >= pitch_->value) {
            st->active = true;
            st->placements = 0;
            st->startY = t.currentAir().takeoffY;
            st->start = ctx.tick();
        }
        if (!st->active) return;
        if (placementSwing(t, pitch_->value)) ++st->placements;
        if (ctx.tick() - st->start > 40) { st->active = false; return; }
        if (!t.landed()) return;
        st->active = false;
        const AirSegment& seg = t.airSegments().back();
        const double gain = seg.landY - st->startY;
        const double speed = seg.airTicks > 0 ? seg.horizontalDistance / seg.airTicks : 0.0;
        if (ctx.debugEnabled()) ctx.debug(t, fmt("keepy placed=%d gain=%.2f speed=%.3f air=%d", st->placements, gain, speed, seg.airTicks));
        if (st->placements >= placed_->i() && std::fabs(gain) <= maxGain_->value && speed >= speed_->value) {
            ctx.emit(t, iJump_, static_cast<float>(std::clamp(st->placements / (placed_->value * 1.6), 0.4, 1.0)),
                     fmt("Placed %d blocks in one jump at %.2f b/tick", st->placements, speed),
                     fmt("Placed %d blocks during one jump and landed level (%+.2f), moving %.2f b/tick while looking "
                         "%.0f° down. Keeping height and pace while placing mid-jump needs instant, perfect aim.",
                         st->placements, gain, speed, f.pitch),
                     {{"Placements", static_cast<double>(st->placements), "", static_cast<double>(placed_->i()), true},
                      {"Speed", speed, "b/tick", speed_->value, true},
                      {"Height change", gain, "blocks"}});
        }
    }

private:
    int iJump_ = 0;
    Setting* pitch_ = nullptr;
    Setting* placed_ = nullptr;
    Setting* speed_ = nullptr;
    Setting* maxGain_ = nullptr;
};

// ── Flat scaffold ─────────────────────────────────────────────────────────────────────────────

struct FlatState : PlayerCheckState {
    std::deque<Tick> placements;
    Tick lastEvidence = -10000;
};

class FlatScaffold final : public Check {
public:
    FlatScaffold()
        : Check("flatscaff", "Scaffold (Flat)", CheckCategory::World, "Fast bridging without sneaking or jumping",
                "Looks for players placing blocks under themselves while looking steeply down, without sneaking or "
                "jumping, at a speed only reachable while sprinting forward. Legitimate bridging is done backwards, "
                "which caps speed at walking pace.") {
        defAlert_ = 72.f;
        defHalfLife_ = 30.f;
        defMinEvidence_ = 3;
    }
    std::unique_ptr<PlayerCheckState> createState() const override { return std::make_unique<FlatState>(); }

protected:
    void registerSettings() override {
        iFast_ = addIndicator("fast_bridge", "Fast flat bridge", "Placing blocks underfoot faster than backward walking allows.", 1.0f);
        pitch_ = &addFloat("pitch", "Look-down angle", 65.0, {40.0, 90.0, 1.0, 0, "°"},
                           "Minimum pitch while bridging.");
        speed_ = &addFloat("minSpeed", "Speed limit", 0.25, {0.15, 0.5, 0.005, 3, "b/tick"},
                           "Average speed above which flat bridging counts. Walking backwards tops out at 0.216 b/tick.");
        preset(*speed_, 0.27, 0.25, 0.235);
        placements_ = &addInt("placements", "Placements", 3, {2, 10, 1, 0, "blocks"},
                              "Blocks placed within one second.");
    }

public:
    void onTick(CheckContext& ctx, PlayerTrack& t, PlayerCheckState* base) override {
        auto* st = static_cast<FlatState*>(base);
        const PlayerFrame& f = t.cur();
        if (placementSwing(t, pitch_->value)) st->placements.push_back(ctx.tick());
        while (!st->placements.empty() && ctx.tick() - st->placements.front() > 20) st->placements.pop_front();
        const Exemptions& ex = ctx.exemptions(t);
        if (ex.movement() || ex.ice || f.sneaking || !f.onGround || t.groundTicks() < 16) return;
        if (static_cast<int>(st->placements.size()) < placements_->i() || ctx.tick() - st->lastEvidence < 20) return;
        double pitchSum = 0.0;
        for (int k = 0; k < 16; ++k) {
            if (t.back(k).sneaking) return;
            pitchSum += t.back(k).pitch;
        }
        const double pitch = pitchSum / 16.0;
        if (pitch < pitch_->value) return;
        const double sp = t.horizontalSpeed(16);
        if (sp <= speed_->value * speedMultiplier(f)) return;
        st->lastEvidence = ctx.tick();
        ctx.emit(t, iFast_, static_cast<float>(std::clamp((sp / speed_->value - 1.0) / 0.3 + 0.4, 0.4, 1.0)),
                 fmt("Bridged %d blocks at %.2f b/tick", static_cast<int>(st->placements.size()), sp),
                 fmt("%d blocks in one second at %.2f b/tick, on the ground and never sneaking, looking %.0f° down. "
                     "Bridging this fast by hand needs sneaking or jump timing at the edge.",
                     static_cast<int>(st->placements.size()), sp, pitch),
                 {{"Speed", sp, "b/tick", speed_->value, true},
                  {"Placements per second", static_cast<double>(st->placements.size()), ""},
                  {"Average pitch", pitch, "°"}});
    }

private:
    int iFast_ = 0;
    Setting* pitch_ = nullptr;
    Setting* speed_ = nullptr;
    Setting* placements_ = nullptr;
};

// ── LegitScaff (automated edge sneaking) ──────────────────────────────────────────────────────

struct EagleState : PlayerCheckState {
    Tick sneakStart = -1;
    std::deque<Tick> taps;          // start ticks of qualifying taps
    std::deque<int> durations;
    Tick lastPlacement = -10000;
};

class LegitScaff final : public Check {
public:
    LegitScaff()
        : Check("legitscaff", "LegitScaff", CheckCategory::World, "Automated edge sneaking while bridging (eagle)",
                "Edge-sneaking scaffolds tap sneak for a tick or two exactly at each block edge. Vigil records the "
                "sneak taps a bridging player makes next to block placements and looks for taps that are both "
                "minimal in duration and metronome-regular, which skilled humans approach but do not sustain.") {
        defAlert_ = 75.f;
        defHalfLife_ = 40.f;
        defMinEvidence_ = 3;
    }
    std::unique_ptr<PlayerCheckState> createState() const override { return std::make_unique<EagleState>(); }
    bool supportsTickSampling() const override { return false; }

protected:
    void registerSettings() override {
        iTaps_ = addIndicator("edge_taps", "Edge-perfect taps", "Minimal, perfectly regular sneak taps at each placement.", 1.0f);
        taps_ = &addInt("taps", "Taps analyzed", 6, {4, 20, 1, 0, "taps"}, "Consecutive taps analyzed together.");
        preset(*taps_, 8, 6, 5);
        maxTap_ = &addInt("maxTapTicks", "Tap duration", 2, {1, 5, 1, 0, "ticks"},
                          "Sneak taps no longer than this are considered minimal.");
        maxCv_ = &addFloat("maxCv", "Regularity limit", 0.10, {0.02, 0.4, 0.01, 2, ""},
                           "Coefficient of variation of the interval between taps below which tapping is considered "
                           "machine-regular.", "Detection", true);
        pitch_ = &addFloat("pitch", "Look-down angle", 70.0, {40.0, 90.0, 1.0, 0, "°"}, "Minimum pitch while bridging.",
                           "Detection", true);
    }

public:
    void onTick(CheckContext& ctx, PlayerTrack& t, PlayerCheckState* base) override {
        auto* st = static_cast<EagleState*>(base);
        if (!t.has(1)) return;
        const PlayerFrame& f = t.cur();
        const PlayerFrame& p = t.back(1);
        const Tick now = ctx.tick();
        if (placementSwing(t, pitch_->value)) st->lastPlacement = now;
        if (ctx.exemptions(t).movement()) { st->taps.clear(); st->durations.clear(); return; }

        if (f.sneaking && !p.sneaking) st->sneakStart = now;
        if (!f.sneaking && p.sneaking && st->sneakStart >= 0) {
            const int dur = static_cast<int>(now - st->sneakStart);
            const bool bridging = mc::isBlockItem(f.heldItem) && f.pitch >= pitch_->value && std::llabs(now - st->lastPlacement) <= 4;
            if (!bridging || dur > maxTap_->i()) {
                st->taps.clear();
                st->durations.clear();
            } else {
                if (!st->taps.empty() && st->sneakStart - st->taps.back() > 30) { st->taps.clear(); st->durations.clear(); }
                st->taps.push_back(st->sneakStart);
                st->durations.push_back(dur);
            }
            st->sneakStart = -1;
        }

        const int n = taps_->i();
        if (static_cast<int>(st->taps.size()) < n) return;
        double mean = 0.0;
        std::vector<double> iv;
        for (std::size_t i = st->taps.size() - static_cast<std::size_t>(n) + 1; i < st->taps.size(); ++i) {
            iv.push_back(static_cast<double>(st->taps[i] - st->taps[i - 1]));
            mean += iv.back();
        }
        mean /= iv.size();
        double var = 0.0;
        for (double v : iv) var += (v - mean) * (v - mean);
        const double cv = mean > 0.0 ? std::sqrt(var / iv.size()) / mean : 1.0;
        if (ctx.debugEnabled()) ctx.debug(t, fmt("eagle taps=%d mean=%.2f cv=%.3f", n, mean, cv));
        if (cv <= maxCv_->value) {
            ctx.emit(t, iTaps_, static_cast<float>(std::clamp(1.0 - cv / (maxCv_->value * 2.0), 0.4, 1.0)),
                     fmt("%d minimal sneak taps, %.1f%% timing variation", n, cv * 100.0),
                     fmt("%d sneak taps of at most %d ticks, each right at a placement, with timing varying only %.1f%%. "
                         "Human sneak-bridging varies far more.",
                         n, maxTap_->i(), cv * 100.0),
                     {{"Interval variation", cv * 100.0, "%", maxCv_->value * 100.0, true},
                      {"Mean interval", mean, "ticks"},
                      {"Taps", static_cast<double>(n), ""}});
            st->taps.clear();
            st->durations.clear();
        }
    }

private:
    int iTaps_ = 0;
    Setting* taps_ = nullptr;
    Setting* maxTap_ = nullptr;
    Setting* maxCv_ = nullptr;
    Setting* pitch_ = nullptr;
};

}  // namespace

std::unique_ptr<Check> makeKeepY() { return std::make_unique<KeepY>(); }
std::unique_ptr<Check> makeFlatScaffold() { return std::make_unique<FlatScaffold>(); }
std::unique_ptr<Check> makeLegitScaff() { return std::make_unique<LegitScaff>(); }

}  // namespace vigil::detection
