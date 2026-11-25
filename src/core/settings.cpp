#include "settings.h"

#include "json.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace vigil {

bool Setting::isDefault() const {
    if (type == SettingType::Text) return text == textDef;
    return std::fabs(value - def) < 1e-9;
}

void Setting::reset() {
    if (type == SettingType::Text) setText(textDef);
    else set(def);
}

void Setting::set(double v) {
    switch (type) {
        case SettingType::Bool: v = (v != 0.0) ? 1.0 : 0.0; break;
        case SettingType::Int:
        case SettingType::Choice:
        case SettingType::Key: v = std::round(v); break;
        default: break;
    }
    if (type == SettingType::Int || type == SettingType::Float) {
        if (range.step > 0.0) v = range.min + std::round((v - range.min) / range.step) * range.step;
        v = std::clamp(v, range.min, range.max);
    }
    if (type == SettingType::Choice && !choices.empty()) {
        v = std::clamp(v, 0.0, static_cast<double>(choices.size() - 1));
    }
    if (std::fabs(v - value) < 1e-12) return;
    value = v;
    Settings::instance().touch();
}

void Setting::setText(const std::string& v) {
    if (v == text) return;
    text = v;
    Settings::instance().touch();
}

Settings& Settings::instance() {
    static Settings s;
    return s;
}

Setting& Settings::add(std::unique_ptr<Setting> s) {
    if (Setting* existing = find(s->key)) return *existing;  // idempotent registration
    settings_.push_back(std::move(s));
    return *settings_.back();
}

Setting& Settings::addBool(const std::string& key, const std::string& label, bool def, const std::string& tooltip) {
    auto s = std::make_unique<Setting>();
    s->key = key; s->label = label; s->tooltip = tooltip; s->type = SettingType::Bool;
    s->value = s->def = def ? 1.0 : 0.0;
    return add(std::move(s));
}

Setting& Settings::addInt(const std::string& key, const std::string& label, int def, SettingRange range, const std::string& tooltip) {
    auto s = std::make_unique<Setting>();
    s->key = key; s->label = label; s->tooltip = tooltip; s->type = SettingType::Int;
    if (range.step <= 0.0) range.step = 1.0;
    range.decimals = 0;
    s->range = range;
    s->value = s->def = def;
    return add(std::move(s));
}

Setting& Settings::addFloat(const std::string& key, const std::string& label, double def, SettingRange range, const std::string& tooltip) {
    auto s = std::make_unique<Setting>();
    s->key = key; s->label = label; s->tooltip = tooltip; s->type = SettingType::Float;
    s->range = range;
    s->value = s->def = def;
    return add(std::move(s));
}

Setting& Settings::addChoice(const std::string& key, const std::string& label, int def, std::vector<std::string> choices, const std::string& tooltip) {
    auto s = std::make_unique<Setting>();
    s->key = key; s->label = label; s->tooltip = tooltip; s->type = SettingType::Choice;
    s->choices = std::move(choices);
    s->value = s->def = def;
    return add(std::move(s));
}

Setting& Settings::addKey(const std::string& key, const std::string& label, int defVk, const std::string& tooltip) {
    auto s = std::make_unique<Setting>();
    s->key = key; s->label = label; s->tooltip = tooltip; s->type = SettingType::Key;
    s->value = s->def = defVk;
    return add(std::move(s));
}

Setting& Settings::addText(const std::string& key, const std::string& label, const std::string& def, const std::string& tooltip) {
    auto s = std::make_unique<Setting>();
    s->key = key; s->label = label; s->tooltip = tooltip; s->type = SettingType::Text;
    s->text = s->textDef = def;
    return add(std::move(s));
}

Setting* Settings::find(const std::string& key) {
    for (auto& s : settings_) if (s->key == key) return s.get();
    return nullptr;
}

const Setting* Settings::find(const std::string& key) const {
    for (const auto& s : settings_) if (s->key == key) return s.get();
    return nullptr;
}

bool Settings::getBool(const std::string& key, bool fallback) const {
    const Setting* s = find(key);
    return s ? s->b() : fallback;
}

double Settings::getNumber(const std::string& key, double fallback) const {
    const Setting* s = find(key);
    return s ? s->value : fallback;
}

std::vector<Setting*> Settings::withPrefix(const std::string& prefix) {
    std::vector<Setting*> out;
    for (auto& s : settings_) if (s->key.rfind(prefix, 0) == 0) out.push_back(s.get());
    return out;
}

static bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool Settings::load() {
    if (path_.empty()) return false;
    std::string text;
    if (!readFile(path_, text)) return false;
    json::Value root;
    std::string err;
    if (!json::parse(text, root, &err) || !root.isObject()) {
        log::writef("vigil", "config: parse failed (%s); using defaults", err.c_str());
        return false;
    }
    const json::Value& values = root.get("settings");
    int applied = 0;
    for (const auto& kv : values.members()) {
        Setting* s = find(kv.first);
        if (!s) continue;  // unknown/obsolete keys are dropped on next save
        if (s->type == SettingType::Text) {
            if (kv.second.isString()) { s->text = kv.second.asString(); ++applied; }
        } else if (kv.second.isBool()) {
            s->value = kv.second.asBool() ? 1.0 : 0.0; ++applied;
        } else if (kv.second.isNumber()) {
            const double before = s->value;
            s->value = before;  // set() would bump revision; apply clamping manually
            s->set(kv.second.asNumber());
            ++applied;
        }
    }
    dirty_ = false;
    ++revision_;
    log::writef("vigil", "config: loaded %d values from %s", applied, path_.c_str());
    return true;
}

bool Settings::save() {
    if (path_.empty()) return false;
    json::Value root = json::Value::object();
    root["format"] = json::Value(1);
    root["product"] = json::Value("Vigil");
    json::Value& values = root["settings"];
    values = json::Value::object();
    for (const auto& s : settings_) {
        if (s->type == SettingType::Text) values[s->key] = json::Value(s->text);
        else if (s->type == SettingType::Bool) values[s->key] = json::Value(s->b());
        else values[s->key] = json::Value(s->value);
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path(), ec);
    const std::string tmp = path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << json::write(root, 2);
    }
    std::filesystem::rename(tmp, path_, ec);
    if (ec) {
        std::filesystem::remove(path_, ec);
        std::filesystem::rename(tmp, path_, ec);
    }
    dirty_ = false;
    return !ec;
}

void Settings::tick(double nowMs) {
    if (!dirty_) { dirtySinceMs_ = 0.0; return; }
    if (dirtySinceMs_ == 0.0) dirtySinceMs_ = nowMs;
    if (nowMs - dirtySinceMs_ >= 1000.0) {
        save();
        dirtySinceMs_ = 0.0;
    }
}

int Settings::importLegacy(const std::string& legacyJsonPath,
                           const std::vector<std::pair<std::string, std::string>>& mapping) {
    std::string text;
    if (!readFile(legacyJsonPath, text)) return 0;
    json::Value root;
    if (!json::parse(text, root) || !root.isObject()) return 0;
    int applied = 0;
    for (const auto& m : mapping) {
        const json::Value* v = root.find(m.first);
        Setting* s = find(m.second);
        if (!v || !s) continue;
        if (v->isBool()) { s->set(v->asBool() ? 1.0 : 0.0); ++applied; }
        else if (v->isNumber()) { s->set(v->asNumber()); ++applied; }
    }
    return applied;
}

}  // namespace vigil
