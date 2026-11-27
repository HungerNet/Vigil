#include "config_binding.h"

#include "check.h"
#include "engine.h"

namespace vigil::detection {

namespace {

SettingRange toRange(const ParamRange& r) {
    SettingRange s;
    s.min = r.min;
    s.max = r.max;
    s.step = r.step;
    s.decimals = r.decimals;
    s.unit = r.unit;
    return s;
}

}  // namespace

void registerEngineSettings(Settings& registry, const ConfigSchema& schema) {
    for (const ParamSpec& p : schema.params()) {
        Setting* s = nullptr;
        switch (p.type) {
            case ParamType::Bool: s = &registry.addBool(p.key, p.label, p.def != 0.0, p.tooltip); break;
            case ParamType::Int: s = &registry.addInt(p.key, p.label, static_cast<int>(p.def), toRange(p.range), p.tooltip); break;
            case ParamType::Float: s = &registry.addFloat(p.key, p.label, p.def, toRange(p.range), p.tooltip); break;
            case ParamType::Choice: s = &registry.addChoice(p.key, p.label, static_cast<int>(p.def), p.choices, p.tooltip); break;
        }
        s->group = p.group;
        s->advanced = p.advanced;
        if (!p.warning.empty()) s->warning = p.warning;
        if (p.warnWhen) {
            s->warnWhen = [w = p.warnWhen](const Setting& st) { return w(ParamView{st.value}); };
        }
    }
}

EngineConfig configFromSettings(const Settings& registry, const std::shared_ptr<const ConfigSchema>& schema) {
    EngineConfig config(schema);
    if (!schema) return config;
    // One pass over the registry; each key is looked up in the schema's index.
    for (const auto& s : registry.all()) {
        if (s->type == SettingType::Text) continue;
        const ParamId id = schema->find(s->key);
        if (id != kNoParam) config.set(id, s->value);
    }
    return config;
}

const EngineConfig& ConfigCache::get(const Settings& registry, const std::shared_ptr<const ConfigSchema>& schema) {
    if (registry.revision() != revision_ || schema.get() != schema_ || config_.schema() != schema.get()) {
        config_ = configFromSettings(registry, schema);
        revision_ = registry.revision();
        schema_ = schema.get();
    }
    return config_;
}

bool SettingsSync::sync(Engine& engine, const Settings& registry) {
    const std::shared_ptr<const ConfigSchema> schema = engine.schemaPtr();
    if (!schema) return false;
    if (registry.revision() == revision_ && schema.get() == schema_) return false;
    engine.setConfig(configFromSettings(registry, schema));
    revision_ = registry.revision();
    schema_ = schema.get();
    return true;
}

Setting* settingFor(Settings& registry, const Check& check, ParamId param) {
    if (!check.schema() || param == kNoParam) return nullptr;
    return registry.find(check.schema()->spec(param).key);
}

Setting* presetSetting(Settings& registry, const Check& check) { return settingFor(registry, check, check.presetParam()); }

void applyPreset(Settings& registry, const Check& check, int preset) {
    if (preset < 0 || preset > 2) return;
    for (const PresetEntry& e : check.presets()) {
        if (Setting* s = settingFor(registry, check, e.param)) s->set(preset == 0 ? e.relaxed : preset == 1 ? e.balanced : e.strict);
    }
    if (Setting* p = presetSetting(registry, check)) p->set(preset);
}

bool isPresetControlled(const Check& check, const Setting& setting) {
    if (!check.schema()) return false;
    const ParamId id = check.schema()->find(setting.key);
    return id != kNoParam && check.isPresetControlled(id);
}

}  // namespace vigil::detection
