#pragma once
// Vigil — the bridge between the settings registry (core/settings.h) and the engine's
// configuration (config.h), RUST_PLAN.md P1.
//
// The engine declares its parameters in its ConfigSchema and runs on EngineConfig snapshots; it
// never sees the registry. Hosts that persist and display parameters (the DLL runtime, the UI
// preview) use this file to:
//
//   1. registerEngineSettings  declare every engine parameter in the registry, with its label,
//                              tooltip, range and warning, in schema order (right after
//                              Engine::initialize(), before Settings::load()).
//   2. SettingsSync::sync      hand the engine a fresh snapshot whenever the registry changed,
//                              at frame boundaries (the top of every Runtime::pump).
//   3. ConfigCache             read the registry's current values as an EngineConfig without
//                              touching the engine (the UI, so a toggle shows the same frame).
//   4. presets                 apply a Sensitivity preset through the registry, as the UI does.
//
// This is the only file under src/detection that may include core/settings.h
// (tools/ci/validate_repo.py engine-globals).

#include "../core/settings.h"
#include "config.h"

#include <cstdint>
#include <memory>

namespace vigil::detection {

class Check;
class Engine;

// Declares every parameter of `schema` in `registry` (idempotent: a key that already exists keeps
// its value; its metadata is refreshed).
void registerEngineSettings(Settings& registry, const ConfigSchema& schema);

// A snapshot of the registry's current values for `schema`. A parameter missing from the
// registry keeps its default. Values are copied bit for bit.
EngineConfig configFromSettings(const Settings& registry, const std::shared_ptr<const ConfigSchema>& schema);

// The registry's values as an EngineConfig, rebuilt only when the registry changes
// (Settings::revision) or the schema does.
class ConfigCache {
public:
    const EngineConfig& get(const Settings& registry, const std::shared_ptr<const ConfigSchema>& schema);

private:
    std::uint64_t revision_ = 0;
    const ConfigSchema* schema_ = nullptr;
    EngineConfig config_;
};

// Keeps one engine's configuration in step with the registry. Call between frames; returns true
// when a new snapshot was handed to the engine.
class SettingsSync {
public:
    bool sync(Engine& engine, const Settings& registry);

private:
    std::uint64_t revision_ = 0;
    const ConfigSchema* schema_ = nullptr;
};

// The registry entry of one of a check's parameters (null when not registered).
Setting* settingFor(Settings& registry, const Check& check, ParamId param);
// The check's Sensitivity choice in the registry.
Setting* presetSetting(Settings& registry, const Check& check);
// Applies preset 0 (Relaxed), 1 (Balanced) or 2 (Strict): each controlled parameter, then the
// choice itself, exactly as the UI always has.
void applyPreset(Settings& registry, const Check& check, int preset);
// Whether a registry entry is one of the parameters the check's presets control.
bool isPresetControlled(const Check& check, const Setting& setting);

}  // namespace vigil::detection
