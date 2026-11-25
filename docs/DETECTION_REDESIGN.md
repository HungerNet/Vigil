# Detection redesign report

## Defects found in the original checks

| Defect | Affected | Effect |
|---|---|---|
| KillAura compared **the local player's** yaw with the bearing from the local player to the suspect | KillAura | Measured whether *you* looked at *them*. Anyone swinging near you while you faced them accrued violations. |
| "Swing" detected as `swingProgress != lastSwingProgress` | KillAura, Keep-Y, FlatScaff | Fired on every animation tick (~5× per swing). KillAura's "rapid swing" was always true; scaffold placement counts were inflated ~5× and thresholds were tuned against the error. |
| 50 ms wall-clock sampling | All | Ticks were double-sampled or skipped; time-based thresholds jittered with frame timing. |
| Interpolated client positions (`posX`) for other players | All movement | Three-tick smoothing hid and created patterns. |
| BHop "fast cycle" = jumps < 350 ms with a ≥0.15 Y step | BHop | Matches ordinary sprint-jumping over uneven terrain; no handling of stairs, ceilings, jump boost, ice or knockback. |
| AutoBlock counted "swing in progress" during a block | AutoBlock | A hit followed by blocking within 300 ms counted. |
| Thresholds hard-coded | All | No user control, no presets, no explanation. |
| Flags were binary VL counters with resets | All | No notion of confidence, decay, or evidence diversity. |

## New sampling foundation

- Exactly one `WorldFrame` per **client tick**, detected from the local player's `ticksExisted`;
  skipped ticks are counted and reported.
- Other players use **server-relayed positions** (`serverPosX/Y/Z ÷ 32`), not interpolated ones.
- Swing starts are real edges (progress reset or animation start). Hurt starts are `hurtTime`
  rising edges.

## Check by check

### KillAura (rewritten)
Judges the attacker's own aim at attributed hits. Indicators:
- **Hit outside view** (weight 2.0): angle between look direction and the victim's hitbox,
  minimized across the latency window, exceeds 40° (Relaxed 55°, Strict 30°).
- **Multi-target** (2.0): different victims hit ≤3 ticks apart at ≥60° separation.
- **Snap to target** (0.8): ≥45° rotation in the 2 ticks before a hit, landing ≤3° from center,
  then ≤3° movement.
- **Robotic tracking** (1.0): over 20 ticks after hits, mean aim error ≤1.6° while the victim
  crosses the view ≥4°/tick (quantization-aware: perfect tracking reads ≈0.7°).
Unambiguous hits only by default. Regression test: the old false-flag scenario yields no evidence.

### Reach (new)
Minimum eye→hitbox (+0.1 border) distance over the latency window at unambiguous hits.
Indicators: **Extended hit** (> 3.30 blocks; 3.45 / 3.20), **Consistent extension** (≥70% of the
last 10 hits beyond 3.12 with median beyond it).

### AutoClicker (new)
Swing-start intervals in the *clicking regime* (≤6 ticks) during combat, excluding block
placements and the server's relay floor (learned per session). Indicators: **Timing consistency**
(≥92% of 30 intervals identical), **Identical intervals** (run ≥20), **Rate ceiling**
(≥18 swings/s over 40 swings, only reachable if the server relays every swing).
The preview arena revealed that a legitimate fighter hitting once per hurt cycle (exactly every 12
ticks) looked "machine-regular"; the clicking-regime limit fixes this and a regression test
covers it.

### Velocity (new)
For unambiguous melee hits on grounded victims, follows the victim's server positions for 5 ticks.
Indicators: **Vertical knockback** (rise < 0.12 blocks; vanilla > 0.6 within 3 ticks) and
**Horizontal knockback** (push < 0.15 blocks away from the attacker). Skips ceilings, walls
(probed when hurt), liquids, webs, ladders, riding, and lag (no position updates).

### Speed (new)
Windowed speed vs. the vanilla envelope scaled by the Speed potion amplifier (unknown potion
data → conservative ×1.45). Indicators: **Sustained speed** (20-tick average > 0.40 b/t),
**Ground speed** (12 ground ticks > 0.31), **Burst** (4-tick average > 1.0, below teleport).
Exempt: knockback, teleport, ice, slime, liquid, climbing, web, riding, flying, warm-up.

### BHop (rewritten)
Analyzes complete air segments. Indicators: **Short air time** (flat landing ≤8 ticks; vanilla 12),
**Hop speed** (≥3 chained hops averaging > 0.40 b/t), **Air strafe** (sideways velocity gained
beyond `0.026·(1−0.91ⁿ)/0.09 + tolerance` while keeping ≥85% speed), **Air acceleration**
(speed beyond `v₀·0.91ⁿ + control + tolerance`). Skips ceilings, jump boost, knockback, unclean
or sparse segments.

### NoSlow (refined)
**Sprinting while using** (≥8 consecutive ticks) and the stronger new **Speed while using**
(ground speed > 0.17 b/t after 6 ticks of use; vanilla ≈ 0.06).

### AutoBlock (refined)
Counts swing **starts** during a block held ≥3 ticks and still held afterwards; needs 4
observations.

### Scaffold (Keep-Y), Scaffold (Flat), LegitScaff (ported, re-tuned)
Real placement counts (one swing per right-click placement). Keep-Y gains a minimum speed so
legitimate jump-bridging is excluded. Flat requires ≥3 placements per second at > 0.25 b/t on
the ground without sneaking (backward walking caps at 0.216). LegitScaff now looks for minimal
(≤2 tick), metronome-regular (CV ≤ 0.10) sneak taps next to placements.

## Verification

`tests/detection_tests.cpp` drives the real engine with vanilla-physics traces at server
precision (1/32 positions, 1.4° rotations). 16 scenarios / 94 expectations, all passing:

| Legitimate (must stay quiet) | Cheating (must be detected) |
|---|---|
| 2 min sprint-jumping with turns | Low-hop at 0.55 b/t (BHop + Speed) |
| Sprint-jumping with Speed II | Mid-air 90° strafes (BHop air strafe) |
| Legit duel at 2.6 blocks with knockback | Hits aimed 180° away (KillAura) |
| Player swinging at air in front of observer (old bug) | Two victims hit in one tick (KillAura) |
| Human click timing; 12-tick combo rhythm | Hits from 3.9 blocks (Reach) |
| Eating while walking slowly | No knockback (Velocity); 4-tick clicker; sprint-eating |
