// AutoClicker — machine-regular click timing.
//
// Observers never see clicks, only arm swings relayed by the server. On vanilla-derived servers a
// swing is relayed only once the previous swing passed its halfway point (≈ every 3 ticks), so
// raw CPS above ~6.7 is invisible. Vigil therefore measures the *structure* of swing timing at
// tick resolution, and learns the server's relay floor from the session's data.

#include "../check.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>

namespace vigil::detection {
namespace {

struct ClickerState : PlayerCheckState {
    std::deque<int> intervals;
    Tick lastSwing = -1;
    int sinceEvidence = 0;
    Tick lastRateEvidence = -10000;
};

class AutoClicker final : public Check {
public:
    AutoClicker()
        : Check("autoclicker", "AutoClicker", CheckCategory::Combat, "Machine-regular click timing",
                "Measures the time between a player's arm swings at tick resolution during combat. Human clicking "
                "drifts: intervals spread across neighboring ticks. Clicking software produces intervals that repeat "
                "exactly. Because servers relay at most one swing every few ticks, Vigil reports timing structure "
                "rather than raw clicks per second and ignores intervals at the relay floor, where every fast "
                "clicker looks identical. Block placements are excluded.") {
        defAlert_ = 75.f;
        defHalfLife_ = 60.f;
        defMinEvidence_ = 3;
    }
    std::unique_ptr<PlayerCheckState> createState() const override { return std::make_unique<ClickerState>(); }
    bool supportsTickSampling() const override { return false; }

protected:
    void registerSettings() override {
        iMode_ = addIndicator("consistency", "Timing consistency",
                              "Nearly every swing interval in a long sample is the same number of ticks.", 1.0f);
        iStreak_ = addIndicator("streak", "Identical intervals",
                                "A long unbroken run of identical swing intervals.", 1.4f);
        iRate_ = addIndicator("rate", "Rate ceiling", "Sustained swing rate above what human clicking sustains.", 0.8f);

        sample_ = &addInt("sample", "Sample size", 30, {10, 80, 1, 0, "intervals"},
                          "Number of swing intervals analyzed together for timing consistency.");
        modeShare_ = &addFloat("modeShare", "Consistency limit", 0.92, {0.6, 1.0, 0.01, 2, ""},
                               "Fraction of intervals sharing the same tick length above which timing is considered robotic.");
        preset(*modeShare_, 0.96, 0.92, 0.88);
        streak_ = &addInt("streak", "Identical run", 20, {6, 60, 1, 0, "intervals"},
                          "Length of an unbroken run of identical intervals that counts as evidence.");
        preset(*streak_, 26, 20, 15);
        maxCps_ = &addFloat("maxCps", "Rate ceiling", 18.0, {8.0, 25.0, 0.5, 1, "swings/s"},
                            "Swings per second, sustained over 40 swings, considered beyond human clicking. Only "
                            "reachable when the server relays every swing.", "Detection", true);
        maxInterval_ = &addInt("maxInterval", "Clicking regime", 6, {3, 12, 1, 0, "ticks"},
                               "Only swing intervals up to this length count as clicking. Slower, rhythmic hits (one per "
                               "hurt cycle) are combat timing, not clicking, and are ignored.");
        maxInterval_->warning = "Long intervals include hit timing; steady combo rhythm can then look machine-regular.";
        maxInterval_->warnWhen = [](const Setting& s) { return s.i() > 8; };
        combatOnly_ = &addBool("combatOnly", "Combat clicks only", true,
                               "Only analyze swings while another player is within 6 blocks.");
        combatOnly_->warning = "Swings while mining or idling will be analyzed and can mix unrelated rhythms.";
        combatOnly_->warnWhen = [](const Setting& s) { return !s.b(); };
    }

public:
    void onTick(CheckContext& ctx, PlayerTrack& t, PlayerCheckState* base) override {
        auto* st = static_cast<ClickerState*>(base);
        if (!t.swingStarted()) return;
        const PlayerFrame& f = t.cur();
        if (mc::isBlockItem(f.heldItem)) return;  // block placement swings
        if (combatOnly_->b() && ctx.exemptions(t).nearestOpponent > 6.0) { st->lastSwing = -1; return; }

        const Tick now = ctx.tick();
        if (st->lastSwing >= 0) {
            const int interval = static_cast<int>(now - st->lastSwing);
            if (interval > maxInterval_->i()) {
                st->intervals.clear();  // a pause (or slow hit rhythm) starts a new burst
            } else if (interval > 0) {
                st->intervals.push_back(interval);
                while (static_cast<int>(st->intervals.size()) > 80) st->intervals.pop_front();
                ++st->sinceEvidence;
            }
        }
        st->lastSwing = now;

        const int floor = std::max(1, ctx.swingRelayFloor());
        const auto& iv = st->intervals;

        // Identical run (excluding the relay floor, where fast humans saturate).
        int run = 0, best = 0, bestValue = 0;
        for (std::size_t i = 1; i < iv.size(); ++i) {
            run = (iv[i] == iv[i - 1]) ? run + 1 : 0;
            if (run > best && iv[i] > floor) { best = run; bestValue = iv[i]; }
        }
        if (best + 1 >= streak_->i() && st->sinceEvidence >= streak_->i()) {
            st->sinceEvidence = 0;
            const float sev = static_cast<float>(std::clamp((best + 1.0) / (streak_->i() * 1.5), 0.5, 1.0));
            ctx.emit(t, iStreak_, sev, fmt("%d identical swing intervals of %d ticks", best + 1, bestValue),
                     fmt("%s swung exactly every %d ticks (%.0f ms) %d times in a row. Human clicking at this pace "
                         "drifts by at least a tick every few clicks.",
                         t.name().c_str(), bestValue, bestValue * 50.0, best + 1),
                     {{"Run length", best + 1.0, "intervals", static_cast<double>(streak_->i()), true},
                      {"Interval", bestValue * 50.0, "ms"},
                      {"Server relay floor", floor * 50.0, "ms"}});
            return;
        }

        // Consistency over the sample.
        const int n = sample_->i();
        if (static_cast<int>(iv.size()) >= n && st->sinceEvidence >= n) {
            std::map<int, int> hist;
            double mean = 0.0;
            for (std::size_t i = iv.size() - static_cast<std::size_t>(n); i < iv.size(); ++i) {
                hist[iv[i]]++;
                mean += iv[i];
            }
            mean /= n;
            int modeValue = 0, modeCount = 0;
            for (const auto& kv : hist) if (kv.second > modeCount) { modeCount = kv.second; modeValue = kv.first; }
            const double share = static_cast<double>(modeCount) / n;
            if (ctx.debugEnabled()) ctx.debug(t, fmt("intervals n=%d mode=%d share=%.2f mean=%.2f floor=%d", n, modeValue, share, mean, floor));
            if (modeValue > floor && share >= modeShare_->value) {
                st->sinceEvidence = 0;
                const float sev = static_cast<float>(std::clamp((share - modeShare_->value) / (1.0 - modeShare_->value + 1e-6) * 0.6 + 0.4, 0.4, 1.0));
                ctx.emit(t, iMode_, sev, fmt("%.0f%% of swings exactly %d ticks apart", share * 100.0, modeValue),
                         fmt("Over the last %d swings, %d were spaced exactly %d ticks (%.0f ms) apart. Human clicking "
                             "at %.1f swings per second typically spreads across two or three neighboring tick lengths.",
                             n, modeCount, modeValue, modeValue * 50.0, 20.0 / std::max(1.0, mean)),
                         {{"Dominant interval share", share * 100.0, "%", modeShare_->value * 100.0, true},
                          {"Dominant interval", modeValue * 50.0, "ms"},
                          {"Mean rate", 20.0 / std::max(1.0, mean), "swings/s"}});
            }
        }

        // Rate ceiling over the last 40 swings.
        const auto& sw = t.swingTicks();
        if (sw.size() >= 40 && now - st->lastRateEvidence > 100) {
            const double span = static_cast<double>(sw.back() - sw[sw.size() - 40]) * mc::kTickSeconds;
            const double cps = span > 0 ? 39.0 / span : 0.0;
            if (cps >= maxCps_->value) {
                st->lastRateEvidence = now;
                ctx.emit(t, iRate_, static_cast<float>(std::clamp(cps / (maxCps_->value * 1.3), 0.4, 1.0)),
                         fmt("Sustained %.1f swings per second", cps),
                         "Forty consecutive swings at this rate exceed what human clicking sustains.",
                         {{"Swing rate", cps, "swings/s", maxCps_->value, true}});
            }
        }
    }

private:
    int iMode_ = 0, iStreak_ = 0, iRate_ = 0;
    Setting* sample_ = nullptr;
    Setting* modeShare_ = nullptr;
    Setting* streak_ = nullptr;
    Setting* maxCps_ = nullptr;
    Setting* maxInterval_ = nullptr;
    Setting* combatOnly_ = nullptr;
};

}  // namespace

std::unique_ptr<Check> makeAutoClicker() { return std::make_unique<AutoClicker>(); }

}  // namespace vigil::detection
