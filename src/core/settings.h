#pragma once
// Vigil — typed settings registry.
//
// Every configurable value in Vigil is declared exactly once with its metadata. The registry is
// the source of truth for defaults, ranges, tooltips, persistence and UI generation: the Checks
// and Settings screens render controls directly from these declarations.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vigil {

enum class SettingType { Bool, Int, Float, Choice, Key, Text };

struct SettingRange {
    double min = 0.0;
    double max = 1.0;
    double step = 0.0;     // 0 = continuous
    int decimals = 2;      // display precision for Float
    const char* unit = ""; // "blocks", "ticks", "%", "s"
};

struct Setting {
    std::string key;          // "checks.reach.threshold"
    std::string label;        // "Reach threshold"
    std::string tooltip;      // what it does (never the recommended value)
    std::string warning;      // shown when the setting is in a quality-reducing state
    std::string group;        // UI section, e.g. "Detection", "Confidence", "Sampling"
    SettingType type = SettingType::Bool;
    SettingRange range;
    std::vector<std::string> choices;  // Choice labels
    bool advanced = false;
    // When set, `warning` is shown while this predicate returns true.
    std::function<bool(const Setting&)> warnWhen;

    double value = 0.0;   // Bool (0/1), Int, Float, Choice index, Key (virtual-key code)
    double def = 0.0;
    std::string text;     // Text
    std::string textDef;

    bool b() const { return value != 0.0; }
    int i() const { return static_cast<int>(value); }
    float f() const { return static_cast<float>(value); }

    bool isDefault() const;
    void reset();
    void set(double v);                 // clamps, bumps revision
    void setText(const std::string& v);
    bool warningActive() const { return !warning.empty() && warnWhen && warnWhen(*this); }
};

class Settings {
public:
    static Settings& instance();

    Setting& addBool(const std::string& key, const std::string& label, bool def, const std::string& tooltip);
    Setting& addInt(const std::string& key, const std::string& label, int def, SettingRange range, const std::string& tooltip);
    Setting& addFloat(const std::string& key, const std::string& label, double def, SettingRange range, const std::string& tooltip);
    Setting& addChoice(const std::string& key, const std::string& label, int def, std::vector<std::string> choices, const std::string& tooltip);
    Setting& addKey(const std::string& key, const std::string& label, int defVk, const std::string& tooltip);
    Setting& addText(const std::string& key, const std::string& label, const std::string& def, const std::string& tooltip);

    Setting* find(const std::string& key);
    const Setting* find(const std::string& key) const;
    bool getBool(const std::string& key, bool fallback = false) const;
    double getNumber(const std::string& key, double fallback = 0.0) const;

    // All settings whose key starts with `prefix`, in declaration order.
    std::vector<Setting*> withPrefix(const std::string& prefix);
    const std::vector<std::unique_ptr<Setting>>& all() const { return settings_; }

    // Persistence.
    void setPath(const std::string& path) { path_ = path; }
    bool load();
    bool save();
    // Call periodically; persists at most once per second after a change.
    void tick(double nowMs);

    std::uint64_t revision() const { return revision_; }
    void touch() { ++revision_; dirty_ = true; }

    // Imports a legacy key/value file once (bool fields only). Returns number of keys applied.
    int importLegacy(const std::string& legacyJsonPath,
                     const std::vector<std::pair<std::string, std::string>>& mapping);

private:
    Setting& add(std::unique_ptr<Setting> s);

    std::vector<std::unique_ptr<Setting>> settings_;
    std::string path_;
    std::uint64_t revision_ = 1;
    bool dirty_ = false;
    double dirtySinceMs_ = 0.0;
};

}  // namespace vigil
