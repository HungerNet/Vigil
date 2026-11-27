#include "check.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace vigil::detection {

std::string fmt(const char* format, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, format);
    std::vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    return buf;
}

std::string Exemptions::describe() const {
    std::string s;
    auto add = [&](bool on, const char* what) {
        if (!on) return;
        if (!s.empty()) s += ", ";
        s += what;
    };
    add(knockback, "recent knockback");
    add(teleport, "teleport");
    add(riding, "riding");
    add(liquid, "in liquid");
    add(climbing, "climbing");
    add(web, "in cobweb");
    add(ice, "on ice");
    add(slime, "slime");
    add(flying, "flying");
    add(fresh, "just appeared");
    add(dead, "dead");
    add(stall, "server lag");
    return s;
}

Check::Check(std::string id, std::string name, CheckCategory category, std::string summary, std::string about)
    : id_(std::move(id)), name_(std::move(name)), summary_(std::move(summary)), about_(std::move(about)), category_(category) {}

ParamId Check::addFloat(const std::string& key, const std::string& label, double def, ParamRange r, const std::string& tip,
                        const std::string& group, bool advanced) {
    return schema_->addFloat(settingsPrefix() + key, label, def, r, tip, group, advanced);
}

ParamId Check::addInt(const std::string& key, const std::string& label, int def, ParamRange r, const std::string& tip,
                      const std::string& group, bool advanced) {
    return schema_->addInt(settingsPrefix() + key, label, def, r, tip, group, advanced);
}

ParamId Check::addBool(const std::string& key, const std::string& label, bool def, const std::string& tip,
                       const std::string& group, bool advanced) {
    return schema_->addBool(settingsPrefix() + key, label, def, tip, group, advanced);
}

void Check::preset(ParamId p, double relaxed, double balanced, double strict) {
    presets_.push_back({p, relaxed, balanced, strict});
}

int Check::addIndicator(const std::string& id, const std::string& name, const std::string& description, float weight) {
    Indicator ind;
    ind.id = id;
    ind.name = name;
    ind.description = description;
    ind.defaultWeight = weight;
    const ParamId en = addBool("ind." + id + ".enabled", name, true, description, "Indicators");
    spec(en).warning = "Turning an indicator off removes that evidence source; detections will rely on the remaining indicators.";
    spec(en).warnWhen = [](const ParamView& st) { return !st.b(); };
    const ParamId w = addFloat("ind." + id + ".weight", name + " weight", weight, {0.0, 3.0, 0.05, 2, "x"},
                               "How strongly one observation from this indicator raises confidence, relative to other indicators.",
                               "Indicators", true);
    ind.enabled = en;
    ind.weight = w;
    indicators_.push_back(ind);
    return static_cast<int>(indicators_.size()) - 1;
}

void Check::registerAll(ConfigSchema& schema) {
    schema_ = &schema;
    enabled_ = addBool("enabled", "Enabled", true, summary_, "General");

    preset_ = schema.addChoice(settingsPrefix() + "preset", "Sensitivity", 1, {"Relaxed", "Balanced", "Strict", "Custom"},
                               "Adjusts thresholds and evidence requirements together. Relaxed favors certainty; Strict "
                               "flags earlier at a higher false-positive risk. Editing any controlled value switches to Custom.",
                               "General");

    alert_ = addFloat("alert", "Alert threshold", defAlert_, {20.0, 100.0, 1.0, 0, "%"},
                      "Confidence a player must reach before Vigil reports a detection.", "Confidence");
    preset(alert_, std::min(95.f, defAlert_ + 12.f), defAlert_, std::max(35.f, defAlert_ - 12.f));
    minEvidence_ = addInt("minEvidence", "Evidence required", defMinEvidence_, {1.0, 12.0, 1.0, 0, "items"},
                          "Number of separate observations needed inside the evidence window before a detection may fire. "
                          "Prevents single outliers from flagging anyone.", "Confidence");
    spec(minEvidence_).warning = "With a single observation allowed, one anomalous measurement can produce a detection.";
    spec(minEvidence_).warnWhen = [](const ParamView& st) { return st.i() <= 1; };
    preset(minEvidence_, defMinEvidence_ + 1, defMinEvidence_, std::max(1, defMinEvidence_ - 1));
    minIndicators_ = addInt("minIndicators", "Distinct indicators", defMinIndicators_, {1.0, 4.0, 1.0, 0, ""},
                            "How many different kinds of evidence must agree before a detection may fire.", "Confidence");
    halfLife_ = addFloat("halfLife", "Confidence half-life", defHalfLife_, {5.0, 300.0, 1.0, 0, "s"},
                         "Time for confidence to fall by half when no new evidence arrives. Longer values remember "
                         "past behavior; shorter values forgive faster.", "Confidence");
    window_ = addFloat("window", "Evidence window", defWindow_, {10.0, 600.0, 5.0, 0, "s"},
                       "How far back observations count toward the evidence requirement.", "Confidence", true);
    cooldown_ = addFloat("cooldown", "Alert cooldown", defCooldown_, {0.0, 300.0, 1.0, 0, "s"},
                         "Minimum time between two detections of this check for the same player.", "Confidence", true);
    gain_ = addFloat("gain", "Evidence gain", 0.25, {0.05, 0.6, 0.01, 2, ""},
                     "Fraction of the remaining confidence headroom that one full-strength, weight-1 observation adds.",
                     "Confidence", true);

    registerSettings();

    samplingEnabled_ = addBool("sampling.enabled", "Sampling", false,
                               "Analyze a subset of ticks and only nearby players to reduce CPU usage in crowded lobbies.",
                               "Sampling");
    spec(samplingEnabled_).warning = "Sampling skips ticks: short behaviors (single bursts, brief hops) can be missed and detections arrive later.";
    spec(samplingEnabled_).warnWhen = [](const ParamView& st) { return st.b(); };
    samplingInterval_ = addInt("sampling.interval", "Analyze every", 2, {1.0, 10.0, 1.0, 0, "ticks"},
                               "When sampling is on, each player is analyzed once every N ticks.", "Sampling");
    samplingRadius_ = addFloat("sampling.radius", "Radius", 48.0, {8.0, 128.0, 4.0, 0, "blocks"},
                               "When sampling is on, players farther than this from you are not analyzed.", "Sampling");
    debug_ = addBool("debug", "Debug output", false,
                     "Records the raw inputs of every evaluation into the Evidence Explorer debug log and detections.log.",
                     "Debug");
    // Invariant: every preset's "Balanced" value equals the setting default, so a fresh install
    // reports Balanced with nothing marked as modified.
}

ConfidenceParams Check::confidenceParams(const EngineConfig& c) const {
    ConfidenceParams p;
    p.alertThreshold = c.f(alert_);
    p.halfLifeSec = c.f(halfLife_);
    p.minEvidence = c.i(minEvidence_);
    p.minIndicators = c.i(minIndicators_);
    p.windowSec = c.f(window_);
    p.cooldownSec = c.f(cooldown_);
    p.gain = c.f(gain_);
    return p;
}

bool Check::indicatorOn(const EngineConfig& c, int i) const {
    return i >= 0 && i < static_cast<int>(indicators_.size()) && c.b(indicators_[i].enabled);
}

float Check::indicatorWeight(const EngineConfig& c, int i) const {
    return (i >= 0 && i < static_cast<int>(indicators_.size())) ? c.f(indicators_[i].weight) : 0.f;
}

bool Check::isPresetControlled(ParamId p) const {
    return std::any_of(presets_.begin(), presets_.end(), [p](const PresetEntry& e) { return e.param == p; });
}

}  // namespace vigil::detection
