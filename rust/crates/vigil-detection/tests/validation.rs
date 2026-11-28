//! tests/validation_tests.cpp, mirrored test for test (P3B_PLAN.md §3.1): properties that must hold
//! for every release — confidence invariants, the match lifecycle, the player-state table,
//! envelopes, determinism, traces and the golden trace, and the engine configuration. Each test
//! carries its C++ name and counts its expectations like `EXPECT` (tools/ci/test_parity.py).

#[macro_use]
mod common;

use common::registry::{Registry, SettingsSync};
use common::scenarios::golden_arena;
use common::sim::{Sim, World, low_hop_step, setup_local};
use common::{Expect, TempFile, read_all, repo, without_cr};
use vigil_detection::config::ConfigRead;
use vigil_detection::engine::Engine;
use vigil_detection::evidence::{ConfidenceModel, ConfidenceParams, EvidenceValue, derive_envelope};
use vigil_detection::matchtracker::GamePhase;
use vigil_detection::replay::{replay_frames, summarize};
use vigil_detection::types::GameContext;
use vigil_formats::config::{ENGINE_CONFIG_MAX_BYTES, EngineConfig, ParamType};
use vigil_formats::json;
use vigil_formats::outcome::{self, DEFAULT_TOLERANCE};
use vigil_formats::trace::{self, TraceHeader, WorldFrame};

fn params() -> ConfidenceParams {
    ConfidenceParams {
        alert_threshold: 70.0,
        half_life_sec: 30.0,
        min_evidence: 3,
        min_indicators: 1,
        window_sec: 60.0,
        cooldown_sec: 20.0,
        gain: 0.25,
    }
}

/// Bit-exact equality of two configurations (`EngineConfig::operator==`).
fn same_config(a: &EngineConfig, b: &EngineConfig) -> bool {
    a.values.len() == b.values.len() && a.values.iter().zip(&b.values).all(|(x, y)| x.to_bits() == y.to_bits())
}

/// Every preset-controlled parameter at its Strict value, built without the registry.
fn strict_config(e: &Engine) -> EngineConfig {
    let mut c = EngineConfig::defaults(e.schema());
    for chk in e.checks() {
        for p in &chk.base.presets {
            c.values[p.param as usize] = p.strict;
        }
    }
    c
}

fn key(e: &Engine, k: &str) -> usize {
    e.schema().find(k.as_bytes()).unwrap_or_else(|| panic!("no parameter {k}"))
}

// ── Confidence ──────────────────────────────────────────────────────────────────────────────

#[test]
fn confidence_invariants() {
    let mut x = Expect::new("confidence invariants");
    let p = params();
    let mut m = ConfidenceModel::default();
    let mut last = 0.0f32;
    let (mut monotone, mut bounded) = (true, true);
    for i in 0..200 {
        m.add(f64::from(i) * 10.0, 4.0, 1.0, i % 3, &p); // maximal evidence, 10 ms apart (no decay)
        monotone = monotone && m.value() >= last;
        bounded = bounded && m.value() >= 0.0 && m.value() <= 100.0;
        last = m.value();
    }
    expect!(x, monotone, "evidence never lowers confidence");
    expect!(x, bounded && m.value() <= 100.0, "confidence stays within 0..100 ({:.2})", m.value());

    let mut d = ConfidenceModel::default();
    d.add(0.0, 2.0, 1.0, 0, &p);
    let v0 = d.value();
    d.decay_to(30000.0, p.half_life_sec);
    expect!(x, (d.value() - v0 * 0.5).abs() < 0.05, "one half-life halves confidence ({v0:.2} → {:.2})", d.value());
    d.decay_to(30000.0 + 30000.0 * 20.0, p.half_life_sec);
    expect!(x, d.value() == 0.0, "long quiet periods return to exactly 0");

    // The gate: a single huge observation can never detect.
    let mut g = ConfidenceModel::default();
    g.add(0.0, 4.0, 1.0, 0, &p);
    g.add(10.0, 4.0, 1.0, 0, &p);
    expect!(
        x,
        !g.can_detect(20.0, &p) || g.value() < p.alert_threshold,
        "two observations are below the evidence gate"
    );
    g.add(20.0, 4.0, 1.0, 0, &p);
    expect!(x, g.can_detect(30.0, &p), "the third observation completes the gate ({:.1}%)", g.value());
    // Carried confidence (a new game) empties the gate: no detection without new evidence.
    g.resume(40.0, 0.5);
    expect!(x, !g.can_detect(50.0, &p), "carried confidence cannot complete a detection");
    // Cooldown.
    let mut c = ConfidenceModel::default();
    for i in 0..6 {
        c.add(f64::from(i) * 10.0, 4.0, 1.0, 0, &p);
    }
    c.mark_detected(60.0);
    c.add(70.0, 4.0, 1.0, 0, &p);
    expect!(x, !c.can_detect(80.0, &p), "cooldown between detections");
    expect!(x, c.can_detect(60.0 + f64::from(p.cooldown_sec) * 1000.0 + 1.0, &p), "after the cooldown");
    x.finish();
}

// ── Lifecycle ───────────────────────────────────────────────────────────────────────────────

#[test]
fn match_lifecycle() {
    let mut x = Expect::new("match lifecycle");
    let mut w = World::new();
    let mut local = Sim::new(1, "Observer");
    setup_local(&mut local);
    let _local = w.add(local);
    let cheat = w.add(Sim::new(2, "Hopper"));
    // Queue: listed, not judged.
    w.context = GameContext::Pregame;
    for _ in 0..200 {
        low_hop_step(&mut w[cheat]);
        w.frame();
    }
    expect!(x, w.engine.phase() == GamePhase::Queue, "queue phase");
    expect!(x, w.rec(cheat).is_some_and(|r| r.evidence.is_empty()), "queue players are listed, never judged");
    // Match: judged; the queue and the match are one game.
    w.context = GameContext::InGame;
    let queue_scene = w.engine.scene();
    for _ in 0..400 {
        low_hop_step(&mut w[cheat]);
        w.frame();
    }
    expect!(
        x,
        w.engine.phase() == GamePhase::Match && w.engine.match_tracker().match_count() == 1,
        "match started once"
    );
    expect!(x, w.engine.scene() != queue_scene, "a match is a new scene");
    expect!(x, w.engine.in_this_game(w.rec(cheat).unwrap()), "queue players are in this game");
    expect!(x, !w.rec(cheat).unwrap().evidence.is_empty(), "match players are judged");
    // Post-game: nothing judged.
    w.chat.push(b"                         1st Killer - Hopper - 7".to_vec());
    w.frame();
    let before = w.rec(cheat).unwrap().evidence.len();
    for _ in 0..300 {
        low_hop_step(&mut w[cheat]);
        w.frame();
    }
    expect!(x, w.engine.phase() == GamePhase::PostGame, "post-game");
    expect!(x, w.rec(cheat).unwrap().evidence.len() == before, "post-game is never judged");
    // New world: a new scene, the player is from an earlier game until seen again, confidence halved.
    let held = w.rec(cheat).unwrap().suspicion;
    let held_by_check: Vec<f32> = w.rec(cheat).unwrap().checks.iter().map(|c| c.model.value()).collect();
    w.world_changed = true;
    w.hidden.push(cheat);
    w.context = GameContext::Lobby;
    for _ in 0..60 {
        w.frame();
    }
    expect!(
        x,
        !w.engine.in_this_game(w.rec(cheat).unwrap()),
        "after the world changes the player is from an earlier game"
    );
    let s = w.rec(cheat).unwrap().suspicion;
    expect!(x, (s - held).abs() < 0.5, "confidence holds while away ({s:.1} vs {held:.1})");
    expect!(x, !w.engine.stalled(), "a world change ends any stall window");
    w.hidden.clear();
    w.context = GameContext::InGame;
    for _ in 0..2 {
        w.frame();
    }
    let r = w.rec(cheat).unwrap();
    let mut halved = true;
    for (i, h) in held_by_check.iter().enumerate() {
        halved = halved && r.checks[i].model.value() <= h * 0.5 + 0.01;
    }
    expect!(
        x,
        halved && r.suspicion < held,
        "each check's confidence carries into the new game at half ({:.1}% overall, was {held:.1}%)",
        r.suspicion
    );
    expect!(x, w.engine.match_tracker().match_count() == 2, "second match");
    x.finish();
}

// ── Player state ────────────────────────────────────────────────────────────────────────────

#[test]
fn player_state_table() {
    let mut x = Expect::new("player state table");
    let marks = TempFile::new("vigil_test_state_marks.json");
    let mut w = World::new();
    w.engine.set_marks_path(&marks.path);
    let mut local = Sim::new(1, "Observer");
    setup_local(&mut local);
    let mut mate = Sim::new(3, "Mate");
    mate.f.is_teammate = true;
    let mut npc = Sim::new(5, "Shopkeeper");
    npc.f.uuid = b"00000000-0000-2000-8000-000000000005".to_vec(); // version 2: server NPC
    let local = w.add(local);
    let plain = w.add(Sim::new(2, "Plain"));
    let mate = w.add(mate);
    let exempt = w.add(Sim::new(4, "Buddy"));
    let npc = w.add(npc);
    let exempt_uuid = w[exempt].f.uuid.clone();
    w.engine.marks_mut().set_exempt(&exempt_uuid, b"Buddy", true);
    for _ in 0..300 {
        for s in [plain, mate, exempt] {
            low_hop_step(&mut w[s]);
        }
        w.frame();
    }
    let rows = [
        ("plain player", plain, true, true, true),
        ("teammate", mate, true, false, false),
        ("exempt player", exempt, true, false, false),
        ("local player", local, true, false, false),
        ("server NPC", npc, false, false, false),
    ];
    for (who, s, listed, judged, evidence) in rows {
        let r = w.rec(s);
        expect!(x, r.is_some() == listed, "{who}: listed={}", r.is_some());
        let Some(r) = r else { continue };
        let st = w.engine.player_state(r);
        expect!(x, st.judged == judged, "{who}: judged={} expected {judged}", st.judged);
        expect!(x, !r.evidence.is_empty() == evidence, "{who}: evidence={}", r.evidence.len());
    }
    let others = w.engine.alerts().iter().filter(|a| a.name != b"Plain").count();
    expect!(x, others == 0, "alerts only for judged players ({others} others)");
    x.finish();
}

// ── Envelopes ───────────────────────────────────────────────────────────────────────────────

#[test]
fn envelopes() {
    let mut x = Expect::new("envelopes");
    let mut w = World::new();
    let mut local = Sim::new(1, "Observer");
    setup_local(&mut local);
    w.add(local);
    let cheat = w.add(Sim::new(2, "Hopper"));
    for _ in 0..600 {
        low_hop_step(&mut w[cheat]);
        w.frame();
    }
    let r = w.rec(cheat);
    expect!(x, r.is_some_and(|r| !r.evidence.is_empty()), "sanity: evidence");
    let Some(r) = r else {
        x.finish();
        return;
    };
    let (mut with_envelope, mut with_expected, mut outside) = (0, 0, 0);
    for e in &r.evidence {
        if !e.envelope.valid() {
            continue;
        }
        with_envelope += 1;
        let en = &e.envelope;
        if en.has_expected {
            with_expected += 1;
        }
        // Limits are inclusive thresholds: evidence sits at or beyond the edge, never inside.
        if if en.upper { en.observed >= en.limit } else { en.observed <= en.limit } {
            outside += 1;
        }
        if en.has_expected {
            // The vanilla expectation sits on the legitimate side of the limit.
            expect!(
                x,
                if en.upper { en.expected <= en.limit } else { en.expected >= en.limit },
                "expected {:.3} on the legitimate side of {:.3} ({})",
                en.expected,
                en.limit,
                String::from_utf8_lossy(&e.summary)
            );
        }
    }
    expect!(
        x,
        with_envelope == r.evidence.len(),
        "every observation has an envelope ({with_envelope} of {})",
        r.evidence.len()
    );
    expect!(x, with_expected > 0, "physics checks state the vanilla expectation ({with_expected})");
    expect!(
        x,
        outside == with_envelope,
        "evidence is never recorded inside the legitimate envelope ({outside} of {with_envelope})"
    );

    // A lower-bound envelope (knockback taken) derives the right side.
    let v = vec![
        EvidenceValue::limited("Push away from attacker", 0.05, "blocks", 0.3),
        EvidenceValue::expected("Vanilla push", 1.4, "blocks"),
        EvidenceValue::plain("Victim ping", 80.0, "ms"),
    ];
    let e = derive_envelope(&v);
    expect!(x, e.valid() && !e.upper && e.has_expected && e.expected == 1.4, "lower-bound envelope");
    expect!(x, (e.deviation() - (0.3 - 0.05) / 0.3).abs() < 1e-9, "deviation below a lower bound");
    expect!(
        x,
        e.context.len() == 1 && e.context[0] == b"Victim ping 80.0 ms",
        "context ({})",
        e.context.first().map(|c| String::from_utf8_lossy(c).into_owned()).unwrap_or_default()
    );
    x.finish();
}

// ── Determinism and traces ──────────────────────────────────────────────────────────────────

#[test]
fn determinism() {
    let mut x = Expect::new("determinism");
    let frames = golden_arena();
    let (mut a, mut b) = (Engine::new(), Engine::new());
    let oa = replay_frames(&mut a, &frames);
    let ob = replay_frames(&mut b, &frames);
    let diffs = outcome::compare(&oa, &ob, 0.0);
    expect!(
        x,
        diffs.is_empty(),
        "two engines, same frames, same outcome ({} differences: {})",
        diffs.len(),
        diffs.first().cloned().unwrap_or_default()
    );
    // The scenario must actually exercise the engine.
    expect!(x, oa.players.get(&b"LowHopper"[..]).is_some_and(|p| !p.detections.is_empty()), "the cheater is detected");
    expect!(
        x,
        oa.players.get(&b"Sprinter"[..]).is_some_and(|p| p.detections.is_empty()),
        "the legitimate player is not"
    );
    x.finish();
}

#[test]
fn trace_round_trip() {
    let mut x = Expect::new("trace round trip");
    let frames = golden_arena();
    let h = TraceHeader { vigil: b"test".to_vec(), ..TraceHeader::default() };
    let hb = trace::parse_header(&trace::header_line(&h));
    expect!(x, hb.as_ref().is_ok_and(|hb| hb.vigil == b"test"), "header ({:?})", hb.as_ref().err());
    let mut back: Vec<WorldFrame> = Vec::new();
    for f in &frames {
        match trace::parse_frame(&trace::frame_line(f)) {
            Ok(g) => back.push(g),
            Err(e) => {
                expect!(x, false, "frame parse: {e}");
                x.finish();
                return;
            }
        }
    }
    let mut exact = back.len() == frames.len();
    for i in 0..frames.len() {
        if !exact {
            break;
        }
        exact = trace::frame_line(&frames[i]) == trace::frame_line(&back[i])
            && frames[i].players.len() == back[i].players.len();
        for (p, q) in frames[i].players.iter().zip(&back[i].players) {
            if !exact {
                break;
            }
            exact = p.pos == q.pos
                && p.yaw == q.yaw
                && p.on_ground == q.on_ground
                && p.uuid == q.uuid
                && p.potions.speed == q.potions.speed
                && p.tab_listed == q.tab_listed
                && p.ping_ms == q.ping_ms;
        }
    }
    expect!(x, exact, "frames survive the trace format exactly");
    let (mut a, mut b) = (Engine::new(), Engine::new());
    let diffs = outcome::compare(&replay_frames(&mut a, &frames), &replay_frames(&mut b, &back), 0.0);
    expect!(
        x,
        diffs.is_empty(),
        "a replayed trace reproduces the session ({})",
        diffs.first().cloned().unwrap_or_default()
    );
    expect!(x, trace::parse_frame(br#"{"pl":[{"e":1}]}"#).is_err(), "a player without a position is refused");
    expect!(
        x,
        trace::parse_header(br#"{"kind":"vigil-trace","format":99}"#).is_err(),
        "a newer trace format is refused"
    );
    x.finish();
}

/// The committed golden trace replays to its committed outcome, and it is the scenario the Rust
/// simulator produces: the proof that tests/common/sim.rs is tests/sim.h.
#[test]
fn golden_trace() {
    let mut x = Expect::new("golden trace");
    let dir = repo().join("tests/traces");
    let t = trace::read(&read_all(&dir.join("golden-arena.vtrace")));
    expect!(x, t.is_ok(), "golden trace readable ({:?})", t.as_ref().err());
    let frames = t.map(|t| t.frames).unwrap_or_default();
    let expected = outcome::from_json(&read_all(&dir.join("golden-arena.expect.json")));
    expect!(x, expected.is_ok(), "expectation readable ({:?})", expected.as_ref().err());
    let mut e = Engine::new();
    let diffs = outcome::compare(&expected.unwrap_or_default(), &replay_frames(&mut e, &frames), DEFAULT_TOLERANCE);
    for d in &diffs {
        println!("      {d}");
    }
    expect!(x, diffs.is_empty(), "golden trace outcome ({} differences)", diffs.len());
    // The committed trace is the scenario as written today: if the simulator changes, regenerate.
    let now = golden_arena();
    expect!(
        x,
        now.len() == frames.len()
            && (frames.is_empty()
                || trace::frame_line(now.last().unwrap()) == trace::frame_line(frames.last().unwrap())),
        "the committed trace matches tests/scenarios.h"
    );
    x.finish();
}

// ── Engine configuration ────────────────────────────────────────────────────────────────────

#[test]
fn config_schema() {
    let mut x = Expect::new("config schema");
    let e = Engine::new();
    let s = e.schema();
    let unique = s.params.iter().enumerate().all(|(i, p)| s.find(&p.key) == Some(i));
    expect!(x, unique && !s.params.is_empty(), "{} parameters, each key declared once", s.params.len());
    let (mut preset_issues, mut indicator_issues) = (0, 0);
    for c in e.checks() {
        let pp = c.base.preset_param();
        expect!(
            x,
            pp >= 0 && s.params[pp as usize].key == format!("{}preset", c.base.settings_prefix()).as_bytes(),
            "{} has a Sensitivity choice",
            c.base.id
        );
        for p in &c.base.presets {
            let spec = &s.params[p.param as usize];
            // Balanced is the default, bit for bit; every preset value is inside the range.
            if p.balanced.to_bits() != spec.default.to_bits() {
                preset_issues += 1;
            }
            for v in [p.relaxed, p.strict] {
                if v < spec.min || v > spec.max {
                    preset_issues += 1;
                }
            }
        }
        for ind in &c.base.indicators {
            if ind.enabled < 0
                || ind.weight < 0
                || s.params[ind.enabled as usize].ty != ParamType::Bool
                || s.params[ind.weight as usize].ty != ParamType::Float
            {
                indicator_issues += 1;
            }
        }
    }
    expect!(
        x,
        preset_issues == 0,
        "presets: Balanced equals the default and every value is in range ({preset_issues} issues)"
    );
    expect!(x, indicator_issues == 0, "every indicator declares a switch and a weight ({indicator_issues} issues)");
    // The engine's defaults are exactly what the registry holds for an untouched install
    // (registry stand-in: registerEngineSettings + configFromSettings).
    let mut reg = Registry::register(s);
    reg.reset_engine_settings();
    expect!(x, same_config(e.config(), &reg.config()), "engine defaults equal the registry's defaults");
    // The declaration is committed (tests/fixtures/engine-schema.json, shared with C++).
    let committed = without_cr(&read_all(&repo().join("tests/fixtures/engine-schema.json")));
    let mut mine = s.to_json();
    mine.push(b'\n');
    expect!(x, !committed.is_empty() && mine == committed, "the declaration matches tests/fixtures/engine-schema.json");
    let committed = without_cr(&read_all(&repo().join("tests/fixtures/engine-config.default.json")));
    let mut mine = e.config().to_json(s, 2);
    mine.push(b'\n');
    expect!(
        x,
        !committed.is_empty() && mine == committed,
        "the default configuration matches tests/fixtures/engine-config.default.json"
    );
    x.finish();
}

/// Registry changes reach the engine only through a sync, between frames, and arrive bit for bit
/// (registry stand-in for the C++ `Settings` + `SettingsSync`).
#[test]
fn config_sync_between_frames() {
    let mut x = Expect::new("config sync between frames");
    let mut e = Engine::new();
    let mut reg = Registry::register(e.schema());
    reg.reset_engine_settings();
    let mut sync = SettingsSync::default();
    expect!(x, sync.sync(&mut e, &reg), "the first sync hands the engine a snapshot");
    expect!(x, !sync.sync(&mut e, &reg), "nothing changed, nothing handed over");
    expect!(x, e.monitoring_enabled(), "monitoring is on by default");
    reg.set_key("general.monitoring", 0.0);
    expect!(x, e.monitoring_enabled(), "a registry change does not reach a running engine by itself");
    expect!(x, !e.monitoring_enabled_in(&reg.config()), "the registry's own view changes at once (what the UI shows)");
    expect!(x, sync.sync(&mut e, &reg) && !e.monitoring_enabled(), "the next sync applies it");
    let reach = reg.find("checks.reach.threshold").unwrap();
    reg.set(reach, 3.35); // snapped to the 0.05 step by the registry
    sync.sync(&mut e, &reg);
    let in_engine = e.config().values[key(&e, "checks.reach.threshold")];
    expect!(
        x,
        in_engine.to_bits() == reg.values[reach].to_bits(),
        "values arrive bit for bit ({:.17})",
        reg.values[reach]
    );
    let bhop = e.check_by_id("bhop").unwrap().base.clone();
    reg.apply_preset(&bhop, 2);
    sync.sync(&mut e, &reg);
    expect!(
        x,
        e.config().i(key(&e, "checks.bhop.maxAirTicks") as i32) == 9 && reg.preset_of(&bhop) == 2,
        "a preset applied in the registry reaches the engine"
    );
    expect!(
        x,
        bhop.is_preset_controlled(key(&e, "checks.bhop.hopLimit") as i32)
            && !bhop.is_preset_controlled(key(&e, "checks.bhop.airTolerance") as i32),
        "preset membership"
    );
    // Snapshots for another schema are refused; an identical schema of another engine is adopted.
    let other = EngineConfig { values: vec![1.0] };
    expect!(
        x,
        !e.set_config(&other) && !e.set_config(&EngineConfig { values: Vec::new() }),
        "a snapshot of another schema is refused"
    );
    let twin = Engine::new();
    expect!(
        x,
        e.set_config(twin.config()) && e.monitoring_enabled() && same_config(e.config(), twin.config()),
        "a twin engine's snapshot is adopted"
    );
    x.finish();
}

/// Two engines with different configurations, fed the same frames alternately, each reach exactly
/// the outcome they reach alone. (The C++ test's fifth expectation — running engines never touch
/// the settings registry — is host-only: the Rust crate has no registry to touch.)
#[test]
fn config_engines_share_nothing() {
    let mut x = Expect::new("config engines share nothing");
    let frames = golden_arena();
    let (mut a, mut b, mut solo_a, mut solo_b) = (Engine::new(), Engine::new(), Engine::new(), Engine::new());
    let strict = strict_config(&b);
    let accepted = b.set_config(&strict);
    expect!(x, accepted && solo_b.set_config(&strict), "strict configuration accepted");
    a.reset_session(frames[0].time_ms);
    b.reset_session(frames[0].time_ms);
    for f in &frames {
        a.on_frame(f);
        b.on_frame(f);
    }
    let n = frames.len() as i32;
    let (oa, ob) = (summarize(&a, n), summarize(&b, n));
    let da = outcome::compare(&replay_frames(&mut solo_a, &frames), &oa, 0.0);
    let db = outcome::compare(&replay_frames(&mut solo_b, &frames), &ob, 0.0);
    expect!(
        x,
        da.is_empty(),
        "default engine unaffected by its neighbour ({})",
        da.first().cloned().unwrap_or_default()
    );
    expect!(
        x,
        db.is_empty(),
        "strict engine unaffected by its neighbour ({})",
        db.first().cloned().unwrap_or_default()
    );
    expect!(x, !outcome::compare(&oa, &ob, 0.0).is_empty(), "the two configurations really differ in outcome");
    x.finish();
}

/// Configuration documents: exact round trips (text and bits) and refusal of hostile input.
#[test]
fn config_documents() {
    let mut x = Expect::new("config documents");
    let e = Engine::new();
    let schema = e.schema();
    let mut odd = EngineConfig::defaults(schema);
    for (i, p) in schema.params.iter().enumerate() {
        odd.values[i] = match p.ty {
            ParamType::Bool => {
                if p.default != 0.0 {
                    0.0
                } else {
                    1.0
                }
            }
            ParamType::Float => p.default + 0.1 + 1.0 / 3.0, // digits that need 17 significant places
            _ => p.default + 1.0,
        };
    }
    let mut exact = 0;
    for c in [e.config().clone(), strict_config(&e), odd.clone()] {
        for indent in [2, -1] {
            let text = c.to_json(schema, indent);
            match EngineConfig::from_json(&text, schema) {
                Ok((back, report))
                    if same_config(&back, &c)
                        && back.to_json(schema, indent) == text
                        && report.unknown.is_empty()
                        && report.missing.is_empty() =>
                {
                    exact += 1
                }
                other => expect!(x, false, "round trip failed: {:?}", other.err()),
            }
        }
    }
    expect!(x, exact == 6, "defaults, strict and every-parameter-moved survive the document exactly ({exact} of 6)");
    let sj = json::parse(&schema.to_json());
    expect!(
        x,
        sj.as_ref().is_ok_and(|sj| sj.get("kind").as_bytes() == b"vigil-engine-schema"
            && sj.get("parameters").size() == schema.params.len()),
        "the declaration is a well-formed document"
    );

    let head = r#"{"kind":"vigil-engine-config","format":1,"values":"#.to_string();
    let refused: Vec<(&str, String)> = vec![
        ("malformed JSON", "{\"kind\":".into()),
        ("not an object", "[1,2]".into()),
        ("another kind", r#"{"kind":"vigil-trace","format":1,"values":{}}"#.into()),
        ("no format", r#"{"kind":"vigil-engine-config","values":{}}"#.into()),
        ("fractional format", r#"{"kind":"vigil-engine-config","format":1.5,"values":{}}"#.into()),
        ("newer format", r#"{"kind":"vigil-engine-config","format":2,"values":{}}"#.into()),
        ("no values", r#"{"kind":"vigil-engine-config","format":1}"#.into()),
        ("values not an object", format!("{head}[]}}")),
        ("number for a switch", format!(r#"{head}{{"general.monitoring":1}}}}"#)),
        ("switch for a number", format!(r#"{head}{{"checks.reach.threshold":true}}}}"#)),
        ("fraction for a whole number", format!(r#"{head}{{"exempt.knockbackTicks":2.5}}}}"#)),
        ("text for a number", format!(r#"{head}{{"checks.reach.threshold":"3.3"}}}}"#)),
        ("infinite number", format!(r#"{head}{{"checks.reach.threshold":1e999}}}}"#)),
        ("oversized document", format!("{head}{{\"pad\":\"{}\"}}}}", "x".repeat(ENGINE_CONFIG_MAX_BYTES))),
    ];
    let mut refused_cleanly = 0;
    for (what, text) in &refused {
        let out = odd.clone();
        match EngineConfig::from_json(text.as_bytes(), schema) {
            Err(err) if !err.is_empty() && same_config(&out, &odd) => refused_cleanly += 1,
            _ => expect!(x, false, "{what}: not refused cleanly"),
        }
    }
    expect!(
        x,
        refused_cleanly == refused.len(),
        "hostile documents are refused, leaving the target untouched ({refused_cleanly})"
    );
    // Another version's document: unknown keys are ignored and listed, missing ones keep defaults.
    let doc = format!(r#"{head}{{"future.parameter":1,"checks.reach.threshold":3.5}}}}"#);
    let read = EngineConfig::from_json(doc.as_bytes(), schema);
    expect!(x, read.is_ok(), "a document from another version is read ({:?})", read.as_ref().err());
    let (tolerant, report) = read.unwrap_or_else(|_| (EngineConfig::defaults(schema), Default::default()));
    expect!(
        x,
        report.unknown.len() == 1
            && report.unknown[0] == b"future.parameter"
            && report.missing.len() == schema.params.len() - 1,
        "unknown and missing keys are reported"
    );
    let limit = key(&e, "checks.speed.limit");
    expect!(
        x,
        tolerant.values[key(&e, "checks.reach.threshold")] == 3.5
            && tolerant.values[limit] == schema.params[limit].default,
        "given values are taken, the rest are defaults"
    );
    x.finish();
}
