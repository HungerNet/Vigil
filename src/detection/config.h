#pragma once
// Vigil detection framework — engine configuration (RUST_PLAN.md P1).
//
// The engine declares its parameters once, in a ConfigSchema: key, type, default, range and the
// metadata the settings registry shows (label, tooltip, group, warning). While it runs, the engine
// reads only an EngineConfig: an immutable snapshot holding one value per parameter. The host
// builds snapshots from the settings registry when the registry changes (config_binding.h) and
// hands them to the engine between frames, so the engine never touches the registry or any other
// global, and everything it concludes is a function of its frames and one EngineConfig.
//
// An EngineConfig is a plain value: comparable bit for bit, serializable (configToJson /
// configFromJson, format "vigil-engine-config" 1), and the configuration document a Rust engine
// receives across the C ABI (RUST_PLAN.md §6.1).

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vigil::detection {

using ParamId = int;
constexpr ParamId kNoParam = -1;

enum class ParamType : std::uint8_t { Bool, Int, Float, Choice };
const char* paramTypeName(ParamType t);

// Mirrors SettingRange (core/settings.h) field for field.
struct ParamRange {
    double min = 0.0;
    double max = 1.0;
    double step = 0.0;      // 0 = continuous
    int decimals = 2;       // display precision for Float
    const char* unit = "";
};

// One parameter value with the conversions Setting offers (b, i, f), so warning predicates and
// engine reads convert exactly as they did through Setting.
struct ParamView {
    double value = 0.0;
    bool b() const { return value != 0.0; }
    int i() const { return static_cast<int>(value); }
    float f() const { return static_cast<float>(value); }
};

struct ParamSpec {
    std::string key;                 // "checks.reach.threshold"
    ParamType type = ParamType::Bool;
    double def = 0.0;
    ParamRange range;                // Int, Float
    std::vector<std::string> choices;  // Choice labels
    // Presentation, for the settings registry and the UI; the engine never reads these.
    std::string label;
    std::string tooltip;
    std::string group;
    std::string warning;
    bool advanced = false;
    std::function<bool(const ParamView&)> warnWhen;   // warning shown while true
};

// The engine's declaration of its parameters, in registration order (globals, then each check).
class ConfigSchema {
public:
    ParamId addBool(const std::string& key, const std::string& label, bool def, const std::string& tooltip,
                    const std::string& group = {}, bool advanced = false);
    ParamId addInt(const std::string& key, const std::string& label, int def, ParamRange range, const std::string& tooltip,
                   const std::string& group = {}, bool advanced = false);
    ParamId addFloat(const std::string& key, const std::string& label, double def, ParamRange range, const std::string& tooltip,
                     const std::string& group = {}, bool advanced = false);
    ParamId addChoice(const std::string& key, const std::string& label, int def, std::vector<std::string> choices,
                      const std::string& tooltip, const std::string& group = {}, bool advanced = false);

    std::size_t size() const { return params_.size(); }
    const ParamSpec& spec(ParamId id) const { return params_[static_cast<std::size_t>(id)]; }
    ParamSpec& spec(ParamId id) { return params_[static_cast<std::size_t>(id)]; }
    const std::deque<ParamSpec>& params() const { return params_; }
    ParamId find(const std::string& key) const;   // kNoParam when unknown
    // Same keys and types in the same order (two engines of the same build).
    bool compatible(const ConfigSchema& other) const;

private:
    ParamId add(ParamSpec spec);

    std::deque<ParamSpec> params_;   // a deque keeps spec references stable while checks declare
    std::unordered_map<std::string, ParamId> index_;
};

// An immutable snapshot of every engine parameter. Built from a schema's defaults, from the
// settings registry (config_binding.h) or from a document (configFromJson); then only read.
class EngineConfig {
public:
    EngineConfig() = default;
    explicit EngineConfig(std::shared_ptr<const ConfigSchema> schema);   // the schema's defaults

    bool empty() const { return values_.empty(); }
    std::size_t size() const { return values_.size(); }
    const ConfigSchema* schema() const { return schema_.get(); }
    const std::shared_ptr<const ConfigSchema>& schemaPtr() const { return schema_; }

    // Reads, with Setting's exact conversions.
    double value(ParamId id) const { return values_[static_cast<std::size_t>(id)]; }
    bool b(ParamId id) const { return value(id) != 0.0; }
    int i(ParamId id) const { return static_cast<int>(value(id)); }
    float f(ParamId id) const { return static_cast<float>(value(id)); }

    // Building a snapshot. Values are stored exactly as given: rounding, steps and ranges are the
    // registry's business (Setting::set), and a snapshot must reproduce the registry bit for bit.
    void set(ParamId id, double v) { values_[static_cast<std::size_t>(id)] = v; }
    bool set(const std::string& key, double v);   // false when the key is unknown

    // Bit-exact equality (same keys, same value bits).
    bool operator==(const EngineConfig& other) const;
    bool operator!=(const EngineConfig& other) const { return !(*this == other); }
    // "key: a → b" for every value that differs from `other` (same schema keys assumed).
    std::vector<std::string> differences(const EngineConfig& other) const;

private:
    std::shared_ptr<const ConfigSchema> schema_;
    std::vector<double> values_;
};

// ── Documents ───────────────────────────────────────────────────────────────────────────────
// {"kind":"vigil-engine-config","format":1,"values":{"general.monitoring":true,…}} in schema
// order; Bool parameters as JSON booleans, the others as exact numbers (core/json.h).
constexpr int kEngineConfigFormat = 1;
constexpr std::size_t kEngineConfigMaxBytes = 1u << 20;

std::string configToJson(const EngineConfig& config, int indent = 2);

struct ConfigReadReport {
    std::vector<std::string> unknown;   // keys this schema does not declare (ignored)
    std::vector<std::string> missing;   // parameters absent from the document (default kept)
};

// Reads a configuration document against `schema`. Refuses (false + `error`) a document that is
// not a Vigil engine configuration, has a newer format, or holds a value of the wrong type (a
// non-boolean for a Bool parameter, a non-integral number for Int/Choice, a non-finite number).
// Unknown keys are ignored and missing ones keep their default; both are listed in `report`.
bool configFromJson(const std::string& text, const std::shared_ptr<const ConfigSchema>& schema, EngineConfig& out,
                    std::string& error, ConfigReadReport* report = nullptr);

// The declaration itself ({"kind":"vigil-engine-schema","format":1,"parameters":[…]}): keys,
// types, defaults, ranges, choices and presentation metadata (warning predicates are code and are
// reported only as "warns": true).
std::string schemaToJson(const ConfigSchema& schema, int indent = 2);

}  // namespace vigil::detection
