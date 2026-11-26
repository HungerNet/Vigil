#pragma once
// Vigil detection framework — check interface.
//
// A check observes player tracks (and attributed combat events), and emits Evidence through
// its indicators. It never decides on its own that a player is cheating: the engine feeds the
// evidence to the player's ConfidenceModel, which applies decay and the evidence gate.

#include "../core/settings.h"
#include "combat.h"
#include "evidence.h"
#include "track.h"

#include <memory>
#include <string>
#include <vector>

namespace vigil::detection {

class Engine;

enum class CheckCategory { Combat, Movement, World };

inline const char* categoryName(CheckCategory c) {
    switch (c) {
        case CheckCategory::Combat: return "Combat";
        case CheckCategory::Movement: return "Movement";
        default: return "World";
    }
}

struct Indicator {
    std::string id;
    std::string name;
    std::string description;
    float defaultWeight = 1.f;
    Setting* enabled = nullptr;
    Setting* weight = nullptr;
};

// Shared per-player, per-tick false-positive reducers.
struct Exemptions {
    bool knockback = false;   // recently hurt (velocity from damage/explosions)
    bool teleport = false;
    bool riding = false;
    bool liquid = false;
    bool climbing = false;
    bool web = false;
    bool ice = false;
    bool slime = false;
    bool flying = false;      // sustained airborne without falling (spectator/creative/flight)
    bool fresh = false;       // tracked too briefly to judge
    bool dead = false;
    // The server (or our client) stalled: positions of everyone arrive in bursts for a moment,
    // which makes every moving player look fast. World-level, not per player.
    bool stall = false;
    double nearestOpponent = 1e9;  // distance to the nearest other player (blocks)

    bool movement() const { return knockback || teleport || riding || liquid || climbing || web || flying || fresh || dead || stall; }
    std::string describe() const;
};

struct PlayerCheckState {
    virtual ~PlayerCheckState() = default;
};

// Interface checks use to talk to the engine.
class CheckContext {
public:
    virtual ~CheckContext() = default;
    virtual Tick tick() const = 0;
    virtual double timeMs() const = 0;
    virtual const Exemptions& exemptions(const PlayerTrack& t) const = 0;
    virtual PlayerTrack* track(int entityId) const = 0;
    virtual const PlayerTrack* localTrack() const = 0;
    // Emit evidence for `subject`. Severity is 0..1.
    virtual void emit(const PlayerTrack& subject, int indicator, float severity, std::string summary,
                      std::string detail, std::vector<EvidenceValue> values, std::string target = {}) = 0;
    virtual bool debugEnabled() const = 0;
    virtual void debug(const PlayerTrack& subject, const std::string& line) = 0;
    // Swing relay floor (ticks) observed this session; 0 = unknown.
    virtual int swingRelayFloor() const = 0;
};

struct PresetEntry {
    Setting* setting;
    double relaxed, balanced, strict;
};

class Check {
public:
    Check(std::string id, std::string name, CheckCategory category, std::string summary, std::string about);
    virtual ~Check() = default;

    // Declares common + check-specific settings. Called once by the engine.
    void registerAll();

    const std::string& id() const { return id_; }
    const std::string& name() const { return name_; }
    const std::string& summary() const { return summary_; }   // one line
    const std::string& about() const { return about_; }       // how it works (UI info panel)
    CheckCategory category() const { return category_; }
    int index() const { return index_; }
    void setIndex(int i) { index_ = i; }

    bool enabled() const { return enabled_ && enabled_->b(); }
    ConfidenceParams confidenceParams() const;
    const std::vector<Indicator>& indicators() const { return indicators_; }
    bool indicatorOn(int i) const;
    float indicatorWeight(int i) const;
    bool debug() const { return debug_ && debug_->b(); }
    bool samplingOn() const { return samplingEnabled_ && samplingEnabled_->b(); }
    int samplingInterval() const { return samplingInterval_ ? std::max(1, samplingInterval_->i()) : 1; }
    double samplingRadius() const { return samplingRadius_ ? samplingRadius_->value : 0.0; }
    virtual bool supportsTickSampling() const { return true; }

    std::string settingsPrefix() const { return "checks." + id_ + "."; }
    Setting* presetSetting() const { return preset_; }
    void applyPreset(int preset);        // 0 relaxed, 1 balanced, 2 strict
    bool isPresetControlled(const Setting* s) const;
    // Signals the check needs from the adapter; shown in the UI when missing.
    virtual std::vector<std::string> requirements() const { return {}; }

    virtual std::unique_ptr<PlayerCheckState> createState() const { return nullptr; }
    virtual void onTick(CheckContext& ctx, PlayerTrack& subject, PlayerCheckState* state) { (void)ctx; (void)subject; (void)state; }
    virtual void onCombat(CheckContext& ctx, const CombatEvent& ev, PlayerTrack& attacker, PlayerTrack& victim,
                          PlayerCheckState* attackerState) {
        (void)ctx; (void)ev; (void)attacker; (void)victim; (void)attackerState;
    }
    // Called for the victim of an attributed hit (evidence is emitted for `victim`).
    virtual void onHitTaken(CheckContext& ctx, const CombatEvent& ev, PlayerTrack& attacker, PlayerTrack& victim,
                            PlayerCheckState* victimState) {
        (void)ctx; (void)ev; (void)attacker; (void)victim; (void)victimState;
    }

protected:
    virtual void registerSettings() {}
    int addIndicator(const std::string& id, const std::string& name, const std::string& description, float weight);
    Setting& addFloat(const std::string& key, const std::string& label, double def, SettingRange r, const std::string& tip,
                      const std::string& group = "Detection", bool advanced = false);
    Setting& addInt(const std::string& key, const std::string& label, int def, SettingRange r, const std::string& tip,
                    const std::string& group = "Detection", bool advanced = false);
    Setting& addBool(const std::string& key, const std::string& label, bool def, const std::string& tip,
                     const std::string& group = "Detection", bool advanced = false);
    void preset(Setting& s, double relaxed, double balanced, double strict);

    // Defaults for the common confidence settings; override in constructor before registerAll.
    float defAlert_ = 70.f, defHalfLife_ = 30.f, defCooldown_ = 20.f, defWindow_ = 60.f;
    int defMinEvidence_ = 3, defMinIndicators_ = 1;

private:
    std::string id_, name_, summary_, about_;
    CheckCategory category_;
    int index_ = -1;
    std::vector<Indicator> indicators_;
    std::vector<PresetEntry> presets_;

    Setting* enabled_ = nullptr;
    Setting* preset_ = nullptr;
    Setting* alert_ = nullptr;
    Setting* halfLife_ = nullptr;
    Setting* minEvidence_ = nullptr;
    Setting* minIndicators_ = nullptr;
    Setting* window_ = nullptr;
    Setting* cooldown_ = nullptr;
    Setting* gain_ = nullptr;
    Setting* samplingEnabled_ = nullptr;
    Setting* samplingInterval_ = nullptr;
    Setting* samplingRadius_ = nullptr;
    Setting* debug_ = nullptr;
};

// Formatting helpers for evidence text.
std::string fmt(const char* format, ...);

}  // namespace vigil::detection
