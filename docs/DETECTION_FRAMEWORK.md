# Detection framework

`src/detection/` is pure C++17. It has no JNI, OpenGL or Windows dependencies, so the same
code runs in the game, in the test suite and in the UI preview.

## Pipeline

```
WorldFrame (one per client tick, from game/sampler.cpp)
  └─ Engine::onFrame
       ├─ PlayerTrack::push         history ring (200 ticks), edge events, air segments
       ├─ Exemptions                 knockback, teleport, riding, liquid, climbing, web,
       │                             ice, slime, flying, warm-up, dead, nearest opponent
       ├─ Check::onTick              per player, per enabled check (optional sampling)
       ├─ CombatResolver             hurt start → attributed CombatEvent (after settle delay)
       │    ├─ Check::onCombat       judged on the attacker (KillAura, Reach)
       │    └─ Check::onHitTaken     judged on the victim (Velocity)
       ├─ ConfidenceModel            per (player, check): gain, decay, evidence gate
       ├─ Detection + Alert          when the gate opens
       └─ Session                    records, timelines, confidence history, behavior stats,
                                     journal (JSONL) and cross-session history
```

## Key types

| Type | File | Purpose |
|---|---|---|
| `PlayerFrame` | `types.h` | One tick of one player: server position (1/32 precision), head yaw, pitch, ground/sprint/sneak/use/block flags, item, swing, hurt time, potions, surface and ceiling blocks |
| `WorldFrame` | `types.h` | All players for a tick + tick number, skipped ticks, game context |
| `PlayerTrack` | `track.h` | History + derived signals: windowed velocity, swing/hurt/teleport edges, `AirSegment`s (takeoff, apex, landing, air time, start/end velocity, data gaps, cleanliness) |
| `CombatEvent` | `combat.h` | Attacker, victim, candidates, certainty, latency-compensated reach, aim outside hitbox, knockback direction |
| `Evidence` | `evidence.h` | Indicator, weight, severity, contribution, confidence after, summary, reasoning, measured values with limits, target |
| `ConfidenceModel` | `evidence.h` | Gain with diminishing returns, exponential decay, evidence gate, cooldown |
| `Check` | `check.h` | Registers its settings, declares indicators and presets, implements `onTick` / `onCombat` / `onHitTaken` |
| `Session` | `session.h` | `PlayerRecord`s with evidence, detections, timeline, per-check `CheckRecord`, `BehaviorStats` |

## Confidence model

For an observation with indicator weight `w` (0–3, user-configurable) and severity `s` (0–1):

```
fraction = 1 − (1 − gain)^(w·s)          gain defaults to 0.25
value   += (100 − value) · fraction       diminishing returns toward 100
value   *= 0.5^(Δt / halfLife)            exponential decay between observations
detect  ⇔ value ≥ alertThreshold
          ∧ observations in window ≥ minEvidence
          ∧ distinct indicators in window ≥ minIndicators
          ∧ time since last detection ≥ cooldown
```

A single observation can never produce a detection on its own (the gate), sustained behavior
dominates, and confidence returns to zero when behavior normalizes. Overall player suspicion
combines checks as `100 · (1 − Π(1 − cᵢ/100))`.

## Settings per check

Every check automatically registers:

| Group | Settings |
|---|---|
| General | Enabled, Sensitivity preset (Relaxed / Balanced / Strict / Custom) |
| Confidence | Alert threshold, Evidence required, Distinct indicators, Half-life, Evidence window*, Cooldown*, Gain* |
| Indicators | Per indicator: enabled, weight* |
| Detection | Check-specific thresholds (some advanced*) |
| Sampling | Sampling on/off (off by default, warns when on), interval, radius |
| Debug | Debug output (raw inputs of each evaluation to the Evidence Explorer and `detections.log`) |

\* advanced. Presets only touch settings registered with `preset()`, and **the Balanced value
of every preset equals the default**, so a fresh install reads "Balanced" with nothing marked as
modified (enforced by a unit test). Editing a preset-controlled value switches to Custom.

Global settings registered by the engine: monitoring, contexts (in games, replays/Atlas,
pre-game, lobbies), analyze yourself (calibration), exemption windows, hit-attribution windows,
session journal and player history.

## Hit attribution

1. A victim's `hurtTime` rises → candidate attackers are players who started a swing within
   `swingWindow` ticks, are within `candidateRange` of the victim's hitbox, and do not hold a
   ranged item.
2. The candidate whose aim best explains the hit wins; certainty is 1.0 with a single candidate.
3. After `settleTicks` (late packets), reach is the minimum eye→hitbox distance over attacker
   frames around the hit and victim frames across the `compensationTicks` window. Aim is
   measured the same way. Both are deliberately the most charitable readings.

## Client-side realities encoded

- Server positions are 1/32 block precise → windowed velocities and quantization tolerances.
- Rotations are 1.40625° precise → aim checks never require sub-quantum accuracy.
- Position packets bunch → segment- and window-based analysis instead of single-tick deltas.
- Swing relay is gated server-side → AutoClicker learns the session's relay floor and ignores it.
- `onGround` is the suspect's own claim → movement checks combine it with vertical motion.
- Missing adapter signals (e.g. potions) degrade to conservative limits, never to false positives.

## Adding a check

1. Create `src/detection/checks/mycheck.cpp` deriving from `Check`.
2. In `registerSettings()`, call `addIndicator(...)` and `addFloat/addInt/addBool(...)` with
   tooltips, and `preset(setting, relaxed, balanced=default, strict)`.
3. Implement `onTick` and/or `onCombat` / `onHitTaken`; emit evidence with
   `ctx.emit(subject, indicator, severity, summary, reasoning, values, target)`.
4. Register it in `checks/registry.cpp` and add the file to `CMakeLists.txt`.
5. Add a legit and a cheating scenario to `tests/detection_tests.cpp`.

The Checks page renders the new check's settings, indicators and evidence without UI code.
