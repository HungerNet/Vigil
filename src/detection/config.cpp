#include "config.h"

#include "../core/json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace vigil::detection {

const char* paramTypeName(ParamType t) {
    switch (t) {
        case ParamType::Bool: return "bool";
        case ParamType::Int: return "int";
        case ParamType::Float: return "float";
        default: return "choice";
    }
}

// ── ConfigSchema ────────────────────────────────────────────────────────────────────────────

ParamId ConfigSchema::add(ParamSpec spec) {
    // Keys are unique; declaring one twice returns the first declaration (as the registry does).
    if (const ParamId existing = find(spec.key); existing != kNoParam) return existing;
    const ParamId id = static_cast<ParamId>(params_.size());
    index_.emplace(spec.key, id);
    params_.push_back(std::move(spec));
    return id;
}

ParamId ConfigSchema::addBool(const std::string& key, const std::string& label, bool def, const std::string& tooltip,
                              const std::string& group, bool advanced) {
    ParamSpec s;
    s.key = key; s.label = label; s.tooltip = tooltip; s.group = group; s.advanced = advanced;
    s.type = ParamType::Bool;
    s.def = def ? 1.0 : 0.0;
    return add(std::move(s));
}

ParamId ConfigSchema::addInt(const std::string& key, const std::string& label, int def, ParamRange range, const std::string& tooltip,
                             const std::string& group, bool advanced) {
    ParamSpec s;
    s.key = key; s.label = label; s.tooltip = tooltip; s.group = group; s.advanced = advanced;
    s.type = ParamType::Int;
    // The same normalization Settings::addInt applies.
    if (range.step <= 0.0) range.step = 1.0;
    range.decimals = 0;
    s.range = range;
    s.def = def;
    return add(std::move(s));
}

ParamId ConfigSchema::addFloat(const std::string& key, const std::string& label, double def, ParamRange range,
                               const std::string& tooltip, const std::string& group, bool advanced) {
    ParamSpec s;
    s.key = key; s.label = label; s.tooltip = tooltip; s.group = group; s.advanced = advanced;
    s.type = ParamType::Float;
    s.range = range;
    s.def = def;
    return add(std::move(s));
}

ParamId ConfigSchema::addChoice(const std::string& key, const std::string& label, int def, std::vector<std::string> choices,
                                const std::string& tooltip, const std::string& group, bool advanced) {
    ParamSpec s;
    s.key = key; s.label = label; s.tooltip = tooltip; s.group = group; s.advanced = advanced;
    s.type = ParamType::Choice;
    s.choices = std::move(choices);
    s.def = def;
    return add(std::move(s));
}

ParamId ConfigSchema::find(const std::string& key) const {
    const auto it = index_.find(key);
    return it == index_.end() ? kNoParam : it->second;
}

bool ConfigSchema::compatible(const ConfigSchema& other) const {
    if (params_.size() != other.params_.size()) return false;
    for (std::size_t i = 0; i < params_.size(); ++i) {
        if (params_[i].key != other.params_[i].key || params_[i].type != other.params_[i].type) return false;
    }
    return true;
}

// ── EngineConfig ────────────────────────────────────────────────────────────────────────────

EngineConfig::EngineConfig(std::shared_ptr<const ConfigSchema> schema) : schema_(std::move(schema)) {
    if (!schema_) return;
    values_.reserve(schema_->size());
    for (const ParamSpec& p : schema_->params()) values_.push_back(p.def);
}

bool EngineConfig::set(const std::string& key, double v) {
    if (!schema_) return false;
    const ParamId id = schema_->find(key);
    if (id == kNoParam) return false;
    set(id, v);
    return true;
}

namespace {
bool sameBits(double a, double b) { return std::memcmp(&a, &b, sizeof(double)) == 0; }

std::string number(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}
}  // namespace

bool EngineConfig::operator==(const EngineConfig& other) const {
    if (values_.size() != other.values_.size()) return false;
    if (schema_.get() != other.schema_.get()) {
        if (!schema_ || !other.schema_ || !schema_->compatible(*other.schema_)) return false;
    }
    for (std::size_t i = 0; i < values_.size(); ++i) {
        if (!sameBits(values_[i], other.values_[i])) return false;
    }
    return true;
}

std::vector<std::string> EngineConfig::differences(const EngineConfig& other) const {
    std::vector<std::string> out;
    const std::size_t n = std::min(values_.size(), other.values_.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (sameBits(values_[i], other.values_[i])) continue;
        const std::string key = schema_ ? schema_->params()[i].key : std::to_string(i);
        out.push_back(key + ": " + number(values_[i]) + " \xE2\x86\x92 " + number(other.values_[i]));
    }
    if (values_.size() != other.values_.size()) {
        out.push_back("parameter count: " + std::to_string(values_.size()) + " \xE2\x86\x92 " + std::to_string(other.values_.size()));
    }
    return out;
}

// ── Documents ───────────────────────────────────────────────────────────────────────────────

std::string configToJson(const EngineConfig& config, int indent) {
    json::Value root = json::Value::object();
    root["kind"] = json::Value("vigil-engine-config");
    root["format"] = json::Value(kEngineConfigFormat);
    json::Value& values = root["values"];
    values = json::Value::object();
    if (const ConfigSchema* s = config.schema()) {
        for (std::size_t i = 0; i < s->size(); ++i) {
            const ParamSpec& p = s->params()[i];
            const ParamId id = static_cast<ParamId>(i);
            values[p.key] = p.type == ParamType::Bool ? json::Value(config.b(id)) : json::Value(config.value(id));
        }
    }
    return json::write(root, indent);
}

bool configFromJson(const std::string& text, const std::shared_ptr<const ConfigSchema>& schema, EngineConfig& out,
                    std::string& error, ConfigReadReport* report) {
    error.clear();
    if (!schema) { error = "no schema"; return false; }
    if (text.size() > kEngineConfigMaxBytes) { error = "document too large"; return false; }
    json::Value root;
    if (!json::parse(text, root, &error) || !root.isObject()) {
        if (error.empty()) error = "not a JSON object";
        return false;
    }
    if (root.get("kind").asString() != "vigil-engine-config") { error = "not a Vigil engine configuration"; return false; }
    const json::Value& format = root.get("format");
    if (!format.isNumber() || std::floor(format.asNumber()) != format.asNumber() || format.asNumber() < 1.0) {
        error = "missing or invalid format";
        return false;
    }
    if (format.asNumber() > kEngineConfigFormat) {
        error = "unsupported engine configuration format " + number(format.asNumber());
        return false;
    }
    const json::Value& values = root.get("values");
    if (!values.isObject()) { error = "missing values"; return false; }

    EngineConfig cfg(schema);
    std::vector<bool> seen(schema->size(), false);
    ConfigReadReport rep;
    for (const auto& kv : values.members()) {
        const ParamId id = schema->find(kv.first);
        if (id == kNoParam) { rep.unknown.push_back(kv.first); continue; }
        const ParamSpec& p = schema->spec(id);
        const json::Value& v = kv.second;
        double x = 0.0;
        if (p.type == ParamType::Bool) {
            if (!v.isBool()) { error = p.key + ": expected true or false"; return false; }
            x = v.asBool() ? 1.0 : 0.0;
        } else {
            if (!v.isNumber() || !std::isfinite(v.asNumber())) { error = p.key + ": expected a number"; return false; }
            x = v.asNumber();
            if ((p.type == ParamType::Int || p.type == ParamType::Choice) && std::floor(x) != x) {
                error = p.key + ": expected a whole number";
                return false;
            }
        }
        cfg.set(id, x);
        seen[static_cast<std::size_t>(id)] = true;
    }
    for (std::size_t i = 0; i < seen.size(); ++i) {
        if (!seen[i]) rep.missing.push_back(schema->params()[i].key);
    }
    out = std::move(cfg);
    if (report) *report = std::move(rep);
    return true;
}

std::string schemaToJson(const ConfigSchema& schema, int indent) {
    json::Value root = json::Value::object();
    root["kind"] = json::Value("vigil-engine-schema");
    root["format"] = json::Value(kEngineConfigFormat);
    json::Value params = json::Value::array();
    for (const ParamSpec& p : schema.params()) {
        json::Value j = json::Value::object();
        j["key"] = json::Value(p.key);
        j["type"] = json::Value(paramTypeName(p.type));
        j["default"] = p.type == ParamType::Bool ? json::Value(p.def != 0.0) : json::Value(p.def);
        if (p.type == ParamType::Int || p.type == ParamType::Float) {
            j["min"] = json::Value(p.range.min);
            j["max"] = json::Value(p.range.max);
            j["step"] = json::Value(p.range.step);
            j["decimals"] = json::Value(p.range.decimals);
            j["unit"] = json::Value(p.range.unit ? p.range.unit : "");
        }
        if (p.type == ParamType::Choice) {
            json::Value c = json::Value::array();
            for (const auto& s : p.choices) c.push(json::Value(s));
            j["choices"] = c;
        }
        j["label"] = json::Value(p.label);
        j["group"] = json::Value(p.group);
        j["advanced"] = json::Value(p.advanced);
        j["tooltip"] = json::Value(p.tooltip);
        if (!p.warning.empty()) j["warning"] = json::Value(p.warning);
        if (p.warnWhen) j["warns"] = json::Value(true);
        params.push(std::move(j));
    }
    root["parameters"] = std::move(params);
    return json::write(root, indent);
}

}  // namespace vigil::detection
