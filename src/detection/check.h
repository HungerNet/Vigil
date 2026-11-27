#pragma once
// Vigil detection framework — check interface.
//
// A check observes player tracks (and attributed combat events), and emits Evidence through
// its indicators. It never decides on its own that a player is cheating: the engine feeds the
// evidence to the player's ConfidenceModel, which applies decay and the evidence gate.
//
// Parameters: a check declares its parameters in the engine's ConfigSchema (registerAll) and keeps
// their ParamIds; it reads their values from the EngineConfig snapshot the engine runs with
// (ctx.config()), never from the settings registry (config.h, RUST_PLAN.md P1).

#include "combat.h"
#include "config.h"
#include "evidence.h"
#include "track.h"

#include <algorithm>
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
    ParamId enabled = kNoParam;
    ParamId weight = kNoParam;
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
    // The parameter snapshot this frame runs with.
    virtual const EngineConfig& config() const = 0;
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

// A parameter the Sensitivity presets set together (values for Relaxed, Balanced, Strict).
struct PresetEntry {
    ParamId param;
    double relaxed, balanced, strict;
};

class Check {
public:
    Check(std::string id, std::string name, CheckCategory category, std::string summary, std::string about);
    virtual ~Check() = default;

    // Declares common + check-specific parameters in `schema`. Called once by the engine.
    void registerAll(ConfigSchema& schema);

    const std::string& id() const { return id_; }
    const std::string& name() const { return name_; }
    const std::string& summary() const { return summary_; }   // one line
    const std::string& about() const { return about_; }       // how it works (UI info panel)
    CheckCategory category() const { return category_; }
    int index() const { return index_; }
    void setIndex(int i) { index_ = i; }

    // Parameter reads, from the snapshot the engine runs with (or one the UI built from the registry).
    bool enabled(const EngineConfig& c) const { return enabled_ != kNoParam && c.b(enabled_); }
    ConfidenceParams confidenceParams(const EngineConfig& c) const;
    const std::vector<Indicator>& indicators() const { return indicators_; }
    bool indicatorOn(const EngineConfig& c, int i) const;
    float indicatorWeight(const EngineConfig& c, int i) const;
    bool debug(const EngineConfig& c) const { return debug_ != kNoParam && c.b(debug_); }
    bool samplingOn(const EngineConfig& c) const { return samplingEnabled_ != kNoParam && c.b(samplingEnabled_); }
    int samplingInterval(const EngineConfig& c) const { return samplingInterval_ != kNoParam ? std::max(1, c.i(samplingInterval_)) : 1; }
    double samplingRadius(const EngineConfig& c) const { return samplingRadius_ != kNoParam ? c.value(samplingRadius_) : 0.0; }
    virtual bool supportsTickSampling() const { return true; }

    std::string settingsPrefix() const { return "checks." + id_ + "."; }
    // Sensitivity presets: the preset choice parameter and the parameters it controls. Applying a
    // preset writes the registry (config_binding.h: applyPreset).
    ParamId presetParam() const { return preset_; }
    const std::vector<PresetEntry>& presets() const { return presets_; }
    bool isPresetControlled(ParamId p) const;
    // The schema this check declared its parameters in (null before registerAll).
    const ConfigSchema* schema() const { return schema_; }
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
    ParamId addFloat(const std::string& key, const std::string& label, double def, ParamRange r, const std::string& tip,
                     const std::string& group = "Detection", bool advanced = false);
    ParamId addInt(const std::string& key, const std::string& label, int def, ParamRange r, const std::string& tip,
                   const std::string& group = "Detection", bool advanced = false);
    ParamId addBool(const std::string& key, const std::string& label, bool def, const std::string& tip,
                    const std::string& group = "Detection", bool advanced = false);
    // The declaration of one of this check's parameters (to attach a warning while registering).
    ParamSpec& spec(ParamId p) { return schema_->spec(p); }
    void preset(ParamId p, double relaxed, double balanced, double strict);

    // Defaults for the common confidence settings; override in constructor before registerAll.
    float defAlert_ = 70.f, defHalfLife_ = 30.f, defCooldown_ = 20.f, defWindow_ = 60.f;
    int defMinEvidence_ = 3, defMinIndicators_ = 1;

private:
    std::string id_, name_, summary_, about_;
    CheckCategory category_;
    int index_ = -1;
    ConfigSchema* schema_ = nullptr;
    std::vector<Indicator> indicators_;
    std::vector<PresetEntry> presets_;

    ParamId enabled_ = kNoParam;
    ParamId preset_ = kNoParam;
    ParamId alert_ = kNoParam;
    ParamId halfLife_ = kNoParam;
    ParamId minEvidence_ = kNoParam;
    ParamId minIndicators_ = kNoParam;
    ParamId window_ = kNoParam;
    ParamId cooldown_ = kNoParam;
    ParamId gain_ = kNoParam;
    ParamId samplingEnabled_ = kNoParam;
    ParamId samplingInterval_ = kNoParam;
    ParamId samplingRadius_ = kNoParam;
    ParamId debug_ = kNoParam;
};

// Formatting helpers for evidence text.
std::string fmt(const char* format, ...);

}  // namespace vigil::detection
