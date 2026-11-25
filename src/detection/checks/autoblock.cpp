// AutoBlock — attacking while continuously blocking. Ported from the original check; now counts
// genuine swing *starts* (the original counted every animation tick of an existing swing).

#include "../check.h"

#include <algorithm>

namespace vigil::detection {
namespace {

class AutoBlock final : public Check {
public:
    AutoBlock()
        : Check("autoblock", "AutoBlock", CheckCategory::Combat, "Attacking while holding a sword block",
                "In vanilla 1.8 a player cannot swing while their sword is raised: clicks are consumed until the block "
                "is released. Vigil counts new swings that begin while the player has been blocking continuously for "
                "several ticks and is still blocking afterwards.") {
        defAlert_ = 70.f;
        defHalfLife_ = 40.f;
        defMinEvidence_ = 4;
    }

protected:
    void registerSettings() override {
        iSwing_ = addIndicator("swing_block", "Swing while blocking", "A swing began during a continuous block.", 1.0f);
        minBlock_ = &addInt("minBlockTicks", "Block duration", 3, {1, 10, 1, 0, "ticks"},
                            "How long the block must have been held before a swing counts. Fast legitimate block-hitting "
                            "releases and re-raises the sword between swings.");
        preset(*minBlock_, 4, 3, 2);
    }

public:
    void onTick(CheckContext& ctx, PlayerTrack& t, PlayerCheckState*) override {
        if (!t.swingStarted() || !t.has(1)) return;
        const PlayerFrame& f = t.cur();
        const PlayerFrame& p = t.back(1);
        if (!f.blocking || !p.blocking || !mc::isSword(f.heldItem)) return;
        const Tick held = ctx.tick() - t.lastBlockStartTick();
        if (held < minBlock_->i()) return;
        ctx.emit(t, iSwing_, std::clamp(static_cast<float>(held) / 10.f, 0.5f, 1.f),
                 fmt("Swung after blocking for %lld ticks", static_cast<long long>(held)),
                 fmt("%s started a swing while their sword had been raised for %lld ticks (%lld ms) without release.",
                     t.name().c_str(), static_cast<long long>(held), static_cast<long long>(held * 50)),
                 {{"Block held", static_cast<double>(held), "ticks", static_cast<double>(minBlock_->i()), true}});
    }

private:
    int iSwing_ = 0;
    Setting* minBlock_ = nullptr;
};

}  // namespace

std::unique_ptr<Check> makeAutoBlock() { return std::make_unique<AutoBlock>(); }

}  // namespace vigil::detection
