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

Setting& Check::addFloat(const std::string& key, const std::string& label, double def, SettingRange r, const std::string& tip,
                         const std::string& group, bool advanced) {
    Setting& s = Settings::instance().addFloat(settingsPrefix() + key, label, def, r, tip);
    s.group = group;
    s.advanced = advanced;
    return s;
}

Setting& Check::addInt(const std::string& key, const std::string& label, int def, SettingRange r, const std::string& tip,
                       const std::string& group, bool advanced) {
    Setting& s = Settings::instance().addInt(settingsPrefix() + key, label, def, r, tip);
    s.group = group;
    s.advanced = advanced;
    return s;
}

Setting& Check::addBool(const std::string& key, const std::string& label, bool def, const std::string& tip,
                        const std::string& group, bool advanced) {
    Setting& s = Settings::instance().addBool(settingsPrefix() + key, label, def, tip);
    s.group = group;
    s.advanced = advanced;
    return s;
}

void Check::preset(Setting& s, double relaxed, double balanced, double strict) {
    presets_.push_back({&s, relaxed, balanced, strict});
}

int Check::addIndicator(const std::string& id, const std::string& name, const std::string& description, float weight) {
    Indicator ind;
    ind.id = id;
    ind.name = name;
    ind.description = description;
    ind.defaultWeight = weight;
    Setting& en = addBool("ind." + id + ".enabled", name, true, description, "Indicators");
    en.warning = "Turning an indicator off removes that evidence source; detections will rely on the remaining indicators.";
    en.warnWhen = [](const Setting& st) { return !st.b(); };
    Setting& w = addFloat("ind." + id + ".weight", name + " weight", weight, {0.0, 3.0, 0.05, 2, "x"},
                          "How strongly one observation from this indicator raises confidence, relative to other indicators.",
                          "Indicators", true);
    ind.enabled = &en;
    ind.weight = &w;
    indicators_.push_back(ind);
    return static_cast<int>(indicators_.size()) - 1;
}

void Check::registerAll() {
    auto& S = Settings::instance();
    enabled_ = &addBool("enabled", "Enabled", true, summary_, "General");

    preset_ = &S.addChoice(settingsPrefix() + "preset", "Sensitivity", 1, {"Relaxed", "Balanced", "Strict", "Custom"},
                           "Adjusts thresholds and evidence requirements together. Relaxed favors certainty; Strict "
                           "flags earlier at a higher false-positive risk. Editing any controlled value switches to Custom.");
    preset_->group = "General";

    alert_ = &addFloat("alert", "Alert threshold", defAlert_, {20.0, 100.0, 1.0, 0, "%"},
                       "Confidence a player must reach before Vigil reports a detection.", "Confidence");
    preset(*alert_, std::min(95.f, defAlert_ + 12.f), defAlert_, std::max(35.f, defAlert_ - 12.f));
    minEvidence_ = &addInt("minEvidence", "Evidence required", defMinEvidence_, {1.0, 12.0, 1.0, 0, "items"},
                           "Number of separate observations needed inside the evidence window before a detection may fire. "
                           "Prevents single outliers from flagging anyone.", "Confidence");
    minEvidence_->warning = "With a single observation allowed, one anomalous measurement can produce a detection.";
    minEvidence_->warnWhen = [](const Setting& st) { return st.i() <= 1; };
    preset(*minEvidence_, defMinEvidence_ + 1, defMinEvidence_, std::max(1, defMinEvidence_ - 1));
    minIndicators_ = &addInt("minIndicators", "Distinct indicators", defMinIndicators_, {1.0, 4.0, 1.0, 0, ""},
                             "How many different kinds of evidence must agree before a detection may fire.", "Confidence");
    halfLife_ = &addFloat("halfLife", "Confidence half-life", defHalfLife_, {5.0, 300.0, 1.0, 0, "s"},
                          "Time for confidence to fall by half when no new evidence arrives. Longer values remember "
                          "past behavior; shorter values forgive faster.", "Confidence");
    window_ = &addFloat("window", "Evidence window", defWindow_, {10.0, 600.0, 5.0, 0, "s"},
                        "How far back observations count toward the evidence requirement.", "Confidence", true);
    cooldown_ = &addFloat("cooldown", "Alert cooldown", defCooldown_, {0.0, 300.0, 1.0, 0, "s"},
                          "Minimum time between two detections of this check for the same player.", "Confidence", true);
    gain_ = &addFloat("gain", "Evidence gain", 0.25, {0.05, 0.6, 0.01, 2, ""},
                      "Fraction of the remaining confidence headroom that one full-strength, weight-1 observation adds.",
                      "Confidence", true);

    registerSettings();

    samplingEnabled_ = &addBool("sampling.enabled", "Sampling", false,
                                "Analyze a subset of ticks and only nearby players to reduce CPU usage in crowded lobbies.",
                                "Sampling");
    samplingEnabled_->warning = "Sampling skips ticks: short behaviors (single bursts, brief hops) can be missed and detections arrive later.";
    samplingEnabled_->warnWhen = [](const Setting& st) { return st.b(); };
    samplingInterval_ = &addInt("sampling.interval", "Analyze every", 2, {1.0, 10.0, 1.0, 0, "ticks"},
                                "When sampling is on, each player is analyzed once every N ticks.", "Sampling");
    samplingRadius_ = &addFloat("sampling.radius", "Radius", 48.0, {8.0, 128.0, 4.0, 0, "blocks"},
                                "When sampling is on, players farther than this from you are not analyzed.", "Sampling");
    debug_ = &addBool("debug", "Debug output", false,
                      "Records the raw inputs of every evaluation into the Evidence Explorer debug log and detections.log.",
                      "Debug");
    // Invariant: every preset's "Balanced" value equals the setting default, so a fresh install
    // reports Balanced with nothing marked as modified.
}

ConfidenceParams Check::confidenceParams() const {
    ConfidenceParams p;
    p.alertThreshold = alert_->f();
    p.halfLifeSec = halfLife_->f();
    p.minEvidence = minEvidence_->i();
    p.minIndicators = minIndicators_->i();
    p.windowSec = window_->f();
    p.cooldownSec = cooldown_->f();
    p.gain = gain_->f();
    return p;
}

bool Check::indicatorOn(int i) const {
    return i >= 0 && i < static_cast<int>(indicators_.size()) && indicators_[i].enabled->b();
}

float Check::indicatorWeight(int i) const {
    return (i >= 0 && i < static_cast<int>(indicators_.size())) ? indicators_[i].weight->f() : 0.f;
}

void Check::applyPreset(int preset) {
    if (preset < 0 || preset > 2) return;
    for (const PresetEntry& e : presets_) {
        e.setting->set(preset == 0 ? e.relaxed : preset == 1 ? e.balanced : e.strict);
    }
    if (preset_) preset_->set(preset);
}

bool Check::isPresetControlled(const Setting* s) const {
    return std::any_of(presets_.begin(), presets_.end(), [s](const PresetEntry& e) { return e.setting == s; });
}

}  // namespace vigil::detection
