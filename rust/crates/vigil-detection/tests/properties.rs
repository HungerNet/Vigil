//! Property tests for the invariants of docs/VALIDATION.md §5 (P3B_PLAN.md V9): each one checked
//! over seeded random inputs — operation sequences on the confidence model, random simulated
//! sessions through the engine, random marks actions and profiles — instead of the few fixed
//! cases the C++ suites use. No dependency: a small seeded generator, so every failure reproduces.
//! Every property was falsified once by breaking the engine on purpose (P3B report §5).

mod common;

use common::sim::{Sim, World, low_hop_step, setup_local};
use common::{TempFile, read_all, write_all};
use vigil_detection::evidence::{ConfidenceModel, ConfidenceParams};
use vigil_detection::marks::{MarkKind, Marks};
use vigil_detection::matchtracker::GamePhase;
use vigil_detection::session::PlayerRecord;
use vigil_detection::snapshot::{capture, catalog_of};
use vigil_detection::types::GameContext;
use vigil_formats::profile;

/// xorshift64* (as tests/engine.rs).
struct Rng(u64);
impl Rng {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 >> 12;
        self.0 ^= self.0 << 25;
        self.0 ^= self.0 >> 27;
        self.0.wrapping_mul(0x2545_F491_4F6C_DD1D)
    }
    fn below(&mut self, n: u64) -> u64 {
        self.next() % n.max(1)
    }
    fn unit(&mut self) -> f64 {
        (self.next() >> 11) as f64 / (1u64 << 53) as f64
    }
    fn range(&mut self, lo: f64, hi: f64) -> f64 {
        lo + (hi - lo) * self.unit()
    }
    fn chance(&mut self, p: f64) -> bool {
        self.unit() < p
    }
}

/// Confidence parameters anywhere in (and slightly beyond) their declared ranges.
fn random_params(r: &mut Rng) -> ConfidenceParams {
    ConfidenceParams {
        alert_threshold: r.range(20.0, 100.0) as f32,
        half_life_sec: r.range(5.0, 300.0) as f32,
        min_evidence: 1 + r.below(12) as i32,
        min_indicators: 1 + r.below(4) as i32,
        window_sec: r.range(10.0, 600.0) as f32,
        cooldown_sec: r.range(0.0, 300.0) as f32,
        gain: r.range(0.0, 1.0) as f32,
    }
}

/// One random operation on a model, applied to the model and mirrored in a shadow log of the
/// observations made since the gate was last emptied.
enum Op {
    Add { dt: f64, weight: f32, severity: f32, indicator: i32 },
    Decay { dt: f64 },
    Resume { dt: f64, carry: f32 },
    Detected,
}

fn random_op(r: &mut Rng) -> Op {
    let dt = match r.below(4) {
        0 => 0.0,
        1 => r.range(0.0, 200.0),
        2 => r.range(0.0, 20_000.0),
        _ => r.range(0.0, 600_000.0),
    };
    match r.below(10) {
        0..=5 => Op::Add {
            dt,
            weight: r.range(-1.0, 6.0) as f32,
            severity: r.range(-0.5, 1.5) as f32,
            indicator: r.below(4) as i32,
        },
        6 | 7 => Op::Decay { dt },
        8 => Op::Resume { dt, carry: if r.chance(0.5) { 1.0 } else { r.range(-0.2, 1.0) as f32 } },
        _ => Op::Detected,
    }
}

const CASES: u64 = 4000;

/// Confidence stays within 0–100, whatever the evidence, decay, carry-over and parameters.
#[test]
fn confidence_stays_within_bounds() {
    let mut r = Rng(0xB0_0D5);
    for case in 0..CASES {
        let p = random_params(&mut r);
        let mut m = ConfidenceModel::default();
        let mut now = 0.0;
        for step in 0..80 {
            match random_op(&mut r) {
                Op::Add { dt, weight, severity, indicator } => {
                    now += dt;
                    m.add(now, weight, severity, indicator, &p);
                }
                Op::Decay { dt } => {
                    now += dt;
                    m.decay_to(now, p.half_life_sec);
                }
                Op::Resume { dt, carry } => {
                    now += dt;
                    m.resume(now, carry);
                }
                Op::Detected => m.mark_detected(now),
            }
            assert!(
                (0.0..=100.0).contains(&m.value()),
                "case {case} step {step}: confidence {} out of 0..100",
                m.value()
            );
        }
    }
}

/// Evidence never lowers confidence: an observation at time t leaves the model at least where
/// decaying to t alone would.
#[test]
fn evidence_never_lowers_confidence() {
    let mut r = Rng(0x5EED_0002);
    for case in 0..CASES {
        let p = random_params(&mut r);
        let mut m = ConfidenceModel::default();
        let mut now = 0.0;
        for _ in 0..r.below(30) {
            now += r.range(0.0, 5000.0);
            m.add(now, r.range(0.0, 4.0) as f32, r.range(0.0, 1.0) as f32, r.below(3) as i32, &p);
        }
        let t = now + if r.chance(0.5) { 0.0 } else { r.range(0.0, 60_000.0) };
        let mut decayed = m.clone();
        decayed.decay_to(t, p.half_life_sec);
        let points = m.add(t, r.range(-1.0, 6.0) as f32, r.range(-0.5, 1.5) as f32, r.below(3) as i32, &p);
        assert!(points >= 0.0, "case {case}: an observation contributed {points}");
        assert!(m.value() >= decayed.value(), "case {case}: {} after evidence, {} without", m.value(), decayed.value());
    }
}

/// Quiet time halves confidence per half-life (until it falls below 0.05, which is exactly 0).
#[test]
fn confidence_halves_per_half_life() {
    let mut r = Rng(0x5EED_0003);
    for case in 0..CASES {
        let p = random_params(&mut r);
        let mut m = ConfidenceModel::default();
        m.add(0.0, r.range(0.2, 4.0) as f32, r.range(0.2, 1.0) as f32, 0, &p);
        let v = f64::from(m.value());
        let k = 1 + r.below(4) as i32;
        let hl = f64::from(p.half_life_sec.max(1.0));
        m.decay_to(hl * 1000.0 * f64::from(k), p.half_life_sec);
        let expected = v * 0.5f64.powi(k);
        let got = f64::from(m.value());
        if expected < 0.05 * 0.999 {
            assert_eq!(got, 0.0, "case {case}: {got} after {k} half-lives of {v} (below 0.05)");
        } else if expected > 0.05 * 1.001 {
            assert!(
                (got - expected).abs() <= expected * 1e-5,
                "case {case}: {got} after {k} half-lives of {v}, expected {expected}"
            );
        }
    }
}

/// After long quiet, confidence is exactly 0 — however the quiet period is split into decays.
#[test]
fn long_quiet_returns_to_exactly_zero() {
    let mut r = Rng(0x5EED_0004);
    for case in 0..CASES {
        let p = random_params(&mut r);
        let mut m = ConfidenceModel::default();
        let mut now = 0.0;
        for _ in 0..1 + r.below(20) {
            now += r.range(0.0, 2000.0);
            m.add(now, r.range(0.0, 4.0) as f32, r.range(0.0, 1.0) as f32, 0, &p);
        }
        let quiet = f64::from(p.half_life_sec.max(1.0)) * 1000.0 * r.range(12.0, 40.0);
        let end = now + quiet;
        while now < end {
            now = (now + r.range(1.0, quiet)).min(end);
            m.decay_to(now, p.half_life_sec);
        }
        assert_eq!(m.value(), 0.0, "case {case}: {} after {:.0} s of quiet", m.value(), quiet / 1000.0);
    }
}

/// A detection needs the evidence gate — at least `minEvidence` observations and `minIndicators`
/// distinct indicators inside the window since the gate was last emptied — the threshold and the
/// cooldown; carried confidence (a new game) cannot complete a detection by itself.
#[test]
fn detections_need_the_evidence_gate_and_cooldown() {
    let mut r = Rng(0x5EED_0005);
    let mut detections = 0;
    for case in 0..CASES {
        let mut p = random_params(&mut r);
        p.alert_threshold = r.range(20.0, 60.0) as f32; // reachable, so the gate is what decides
        let mut m = ConfidenceModel::default();
        let mut shadow: Vec<(f64, i32)> = Vec::new(); // observations since the gate was emptied
        let mut now = 0.0;
        let mut last_detection = f64::NEG_INFINITY;
        for step in 0..120 {
            match random_op(&mut r) {
                Op::Add { dt, weight, severity, indicator } => {
                    now += dt.min(5000.0);
                    m.add(now, weight, severity, indicator, &p);
                    shadow.push((now, indicator));
                }
                Op::Decay { dt } => {
                    now += dt.min(5000.0);
                    m.decay_to(now, p.half_life_sec);
                }
                Op::Resume { dt, carry } => {
                    now += dt.min(5000.0);
                    m.resume(now, carry);
                    if carry < 1.0 {
                        shadow.clear();
                    }
                }
                Op::Detected => {
                    if m.can_detect(now, &p) {
                        m.mark_detected(now);
                        last_detection = now;
                    }
                }
            }
            if m.can_detect(now, &p) {
                detections += 1;
                let window = f64::from(p.window_sec.max(1.0)) * 1000.0;
                let inside: Vec<i32> = shadow.iter().filter(|(t, _)| now - t <= window).map(|&(_, i)| i).collect();
                let mut kinds = inside.clone();
                kinds.sort_unstable();
                kinds.dedup();
                assert!(m.value() >= p.alert_threshold, "case {case} step {step}: below the threshold");
                assert!(
                    inside.len() as i32 >= p.min_evidence,
                    "case {case} step {step}: {} observations, gate {}",
                    inside.len(),
                    p.min_evidence
                );
                assert!(
                    kinds.len() as i32 >= p.min_indicators,
                    "case {case} step {step}: {} indicators, gate {}",
                    kinds.len(),
                    p.min_indicators
                );
                assert!(
                    now - last_detection >= f64::from(p.cooldown_sec) * 1000.0,
                    "case {case} step {step}: inside the cooldown"
                );
            }
        }
        // Carried confidence alone: a full model, carried into a new game, gets fewer
        // observations than the gate requires.
        let mut c = ConfidenceModel::default();
        for i in 0..20 {
            c.add(f64::from(i), 4.0, 1.0, i % 4, &p);
        }
        let t = 1000.0 + r.range(0.0, 1000.0);
        c.resume(t, r.range(0.0, 0.999) as f32);
        for i in 0..(p.min_evidence - 1) {
            c.add(t + f64::from(i), 4.0, 1.0, i % 4, &p);
        }
        assert!(!c.can_detect(t + 100.0, &p), "case {case}: carried confidence completed a detection");
    }
    assert!(detections > 1000, "the property must actually see detections ({detections})");
    println!("coverage: {detections} detections checked against the gate and the cooldown");
}

// ── Engine-level properties over random simulated sessions ──────────────────────────────────

#[derive(Clone, Copy, PartialEq)]
enum Role {
    Plain,
    Teammate,
    Exempt,
    Npc,
}

struct Session {
    w: World,
    sims: Vec<(usize, Role)>,
    local: usize,
}

/// A random session: low-hopping players (plenty of evidence for anyone judged) in random roles,
/// through random phases — queue, match, end announcement, new worlds, lobbies — with players
/// leaving and returning.
fn random_session(r: &mut Rng, marks: &TempFile) -> Session {
    let mut w = World::new();
    w.engine.set_marks_path(&marks.path);
    let mut local = Sim::new(1, "Observer");
    setup_local(&mut local);
    let local = w.add(local);
    let mut sims = Vec::new();
    for i in 0..2 + r.below(5) as i32 {
        let mut s = Sim::new(10 + i, &format!("Player{i}"));
        s.f.pos = [f64::from(i) * 15.0, 64.0, 0.0];
        s.move_yaw = r.range(0.0, 360.0);
        let role = match r.below(6) {
            0 => Role::Teammate,
            1 => Role::Exempt,
            2 => Role::Npc,
            _ => Role::Plain,
        };
        match role {
            Role::Teammate => s.f.is_teammate = true,
            Role::Npc => s.f.uuid = format!("00000000-0000-2000-8000-{:012}", i).into_bytes(),
            _ => {}
        }
        let uuid = s.f.uuid.clone();
        let name = s.f.name.clone();
        let h = w.add(s);
        if role == Role::Exempt {
            w.engine.marks_mut().set_exempt(&uuid, &name, true);
        }
        sims.push((h, role));
    }
    w.context = if r.chance(0.4) { GameContext::Pregame } else { GameContext::InGame };
    Session { w, sims, local }
}

fn step_session(r: &mut Rng, s: &mut Session) {
    match r.below(400) {
        0..=2 => s.w.context = GameContext::InGame,
        3 => s.w.context = GameContext::Pregame,
        4 => s.w.chat.push(b"                  1st Killer - Player0 - 3".to_vec()),
        5 => {
            s.w.world_changed = true;
            s.w.context = if r.chance(0.5) { GameContext::Lobby } else { GameContext::Pregame };
        }
        6..=20 => {
            let (h, _) = s.sims[r.below(s.sims.len() as u64) as usize];
            if let Some(i) = s.w.hidden.iter().position(|&x| x == h) {
                s.w.hidden.remove(i);
            } else {
                s.w.hidden.push(h);
            }
        }
        _ => {}
    }
    for &(h, _) in &s.sims {
        low_hop_step(&mut s.w[h]);
    }
}

fn values(r: &PlayerRecord) -> Vec<f32> {
    r.checks.iter().map(|c| c.model.value()).collect()
}

/// Queue players are listed, never judged; post-game is never judged; teammates, the local
/// player and exempt players are listed but never judged; NPCs are never listed; only judged
/// players produce alerts; confidence holds while a player is away and is halved (per check) in a
/// new game; every observation carries an envelope on the right side of its limit.
#[test]
fn engine_lifecycle_and_roles_hold_in_random_sessions() {
    let mut r = Rng(0x5EED_0006);
    let (mut frames, mut queue_frames, mut post_frames, mut away_checks, mut carried, mut evidence) =
        (0, 0, 0, 0, 0, 0usize);
    let mut returns = 0;
    for case in 0..60 {
        let marks = TempFile::new(&format!("prop-marks-{case}.json"));
        let mut s = random_session(&mut r, &marks);
        let mut alerts_seen = s.w.engine.alert_seq();
        for f in 0..200 + r.below(600) {
            step_session(&mut r, &mut s);
            let before_total = s.w.engine.session().total_evidence();
            let before: Vec<Option<(bool, i32, Vec<f32>)>> =
                s.sims.iter().map(|&(h, _)| s.w.rec(h).map(|rec| (rec.present, rec.scene, values(rec)))).collect();
            s.w.frame();
            frames += 1;
            let e = &s.w.engine;
            let gained = e.session().total_evidence() - before_total;
            // Phases that never judge (default configuration: queue not analyzed).
            if e.phase() == GamePhase::Queue {
                queue_frames += 1;
                assert_eq!(gained, 0, "case {case} frame {f}: the queue was judged");
            }
            if e.phase() == GamePhase::PostGame {
                post_frames += 1;
                assert_eq!(gained, 0, "case {case} frame {f}: post-game was judged");
            }
            for (k, &(h, role)) in s.sims.iter().enumerate() {
                let rec = s.w.rec(h);
                if role == Role::Npc {
                    assert!(rec.is_none(), "case {case} frame {f}: an NPC was listed");
                    continue;
                }
                let Some(rec) = rec else { continue };
                if role != Role::Plain {
                    assert!(rec.evidence.is_empty(), "case {case} frame {f}: a teammate or exempt player was judged");
                    assert!(
                        !e.player_state(rec).judged || e.context() == GameContext::Replay,
                        "case {case}: player state says judged"
                    );
                }
                if let Some((was_present, was_scene, was)) = &before[k] {
                    if !was_present && !rec.present {
                        // Away: nothing changes.
                        away_checks += 1;
                        assert_eq!(&values(rec), was, "case {case} frame {f}: confidence changed while away");
                    }
                    if !was_present
                        && rec.present
                        && rec.scene == *was_scene
                        && !rec.evidence.iter().any(|ev| ev.tick == s.w.tick)
                    {
                        // Back in view in the same game: nothing is charged for the absence.
                        returns += 1;
                        assert_eq!(&values(rec), was, "case {case} frame {f}: the absence was charged on return");
                    }
                    if *was_scene >= 0 && rec.scene != *was_scene && !rec.evidence.iter().any(|ev| ev.tick == s.w.tick)
                    {
                        // A new scene (and nothing observed yet this frame): halved, or held when the
                        // queue became its match.
                        let same_game = *was_scene == e.match_tracker().queue_scene()
                            && rec.scene == e.match_tracker().match_scene();
                        for (i, (now, then)) in values(rec).iter().zip(was).enumerate() {
                            let limit = if same_game { *then } else { then * 0.5 };
                            assert!(*now <= limit + 1e-4, "case {case} frame {f} check {i}: {now} carried from {then}");
                        }
                        carried += 1;
                    }
                }
            }
            let local = s.w.rec(s.local);
            assert!(local.is_none_or(|l| l.evidence.is_empty()), "case {case}: the local player was judged");
            // Only judged players produce alerts.
            for a in e.alerts() {
                if a.seq <= alerts_seen {
                    continue;
                }
                let ri = e.session().find(&a.player_key).expect("an alert's record");
                assert!(
                    e.player_state(e.session().player(ri)).judged,
                    "case {case} frame {f}: alert for an unjudged player"
                );
            }
            alerts_seen = e.alert_seq();
        }
        // Envelopes: every observation states its limit and sits at or beyond it, its vanilla
        // expectation on the legitimate side.
        for rec in s.w.engine.session().players() {
            for ev in &rec.evidence {
                evidence += 1;
                let en = &ev.envelope;
                assert!(en.valid(), "case {case}: an observation without an envelope");
                assert!(
                    if en.upper { en.observed >= en.limit } else { en.observed <= en.limit },
                    "case {case}: inside the envelope"
                );
                if en.has_expected {
                    assert!(
                        if en.upper { en.expected <= en.limit } else { en.expected >= en.limit },
                        "case {case}: expectation on the wrong side"
                    );
                }
            }
        }
    }
    assert!(
        queue_frames > 1000
            && post_frames > 100
            && away_checks > 1000
            && returns > 50
            && carried > 10
            && evidence > 1000,
        "coverage: {frames} frames, queue {queue_frames}, post-game {post_frames}, away {away_checks}, returns {returns}, carried {carried}, evidence {evidence}"
    );
    println!(
        "coverage: {frames} frames, queue {queue_frames}, post-game {post_frames}, away {away_checks}, returns {returns}, carried {carried}, evidence {evidence}"
    );
}

// ── Records: format guards and profiles ─────────────────────────────────────────────────────

/// A marks file in a newer or unreadable format is never overwritten, whatever the user does.
#[test]
fn read_only_marks_files_are_never_overwritten() {
    let mut r = Rng(0x5EED_0007);
    let files: [&[u8]; 4] = [
        br#"{"format":99,"players":{"abc":{"name":"Future","watch":true}}}"#,
        b"{ not json",
        b"[1,2,3]",
        br#"{"format":3,"players":{"k1":{"name":"Kestrel","confirmed":true,"updated":5}}}"#,
    ];
    for case in 0..400 {
        let f = TempFile::new(&format!("prop-ro-{case}.json"));
        let original = files[case % files.len()];
        write_all(&f.path, original);
        let mut m = Marks::default();
        m.set_path(&f.path);
        assert!(m.read_only(), "case {case}: opened writable");
        for _ in 0..40 {
            let key: &[u8] = [b"k1".as_slice(), b"abc", b"user:kestrel", b"00000000-0000-4000-8000-000000000001"]
                [r.below(4) as usize];
            let name: &[u8] = [b"Kestrel".as_slice(), b"Future", b"notch"][r.below(3) as usize];
            match r.below(9) {
                0 => m.set_watch(key, name, r.chance(0.5)),
                1 => m.confirm(key, name, vec![b"Reach".to_vec()], 50.0),
                2 => m.set_exempt(key, name, r.chance(0.5)),
                3 => m.set_note(key, b"a note"),
                4 => m.remove(key),
                5 => {
                    m.add_by_name(name, MarkKind::Watch, b"");
                }
                6 => {
                    m.bind_name(b"069a79f4-44e9-4726-a5be-fca90e38aaf5", name, b"069a79f4-44e9-4726-a5be-fca90e38aaf5");
                }
                7 => m.unconfirm(key),
                _ => m.remember_name(key, b"Renamed"),
            }
            assert!(!m.save(), "case {case}: a read-only store saved");
        }
        assert_eq!(read_all(&f.path), original, "case {case}: the file was overwritten");
    }
}

/// Saved profiles survive a round trip; unknown checks are dropped and listed; hostile files are
/// bounded and refused cleanly (random mutations of real profiles never panic).
#[test]
fn profiles_round_trip_and_hostile_files_are_bounded() {
    let mut r = Rng(0x5EED_0008);
    let (mut profiles, mut mutated, mut refused) = (0, 0, 0);
    for case in 0..25 {
        let marks = TempFile::new(&format!("prop-profile-marks-{case}.json"));
        let mut s = random_session(&mut r, &marks);
        for _ in 0..400 {
            step_session(&mut r, &mut s);
            s.w.frame();
        }
        let e = &s.w.engine;
        let cat = catalog_of(e);
        for rec in e.session().players() {
            let snap = capture(e, rec, b"property");
            let text = profile::to_json(&snap, &cat);
            let back = profile::from_json(&text, &cat).expect("a captured profile parses");
            assert_eq!(profile::to_json(&back, &cat), text, "case {case}: not a fixed point");
            assert_eq!(back.record.evidence.len(), rec.evidence.len().min(600));
            profiles += 1;
            // Another version's catalog: a random check removed, the rest shuffled.
            let mut other = cat.clone();
            let gone = r.below(other.checks.len() as u64) as usize;
            let gone_id = other.checks.remove(gone);
            other.indicators.remove(gone);
            for i in (1..other.checks.len()).rev() {
                let j = r.below(i as u64 + 1) as usize;
                other.checks.swap(i, j);
                other.indicators.swap(i, j);
            }
            let read = profile::from_json(&text, &other).expect("readable by another version");
            let had = rec.checks[gone].evidence != 0 || rec.checks[gone].detections != 0 || rec.checks[gone].peak > 0.0;
            assert_eq!(read.dropped_checks.contains(&gone_id), had, "case {case}: dropped checks");
            assert!(read.record.evidence.iter().all(|ev| other.checks[ev.check] != gone_id));
            assert_eq!(read.record.checks.len(), other.checks.len());
            // Hostile edits: bytes flipped, cut, repeated; parsing returns or refuses, bounded.
            for _ in 0..4 {
                let mut t = text.clone();
                match r.below(4) {
                    0 => {
                        for _ in 0..1 + r.below(8) {
                            let i = r.below(t.len() as u64) as usize;
                            t[i] = r.next() as u8;
                        }
                    }
                    1 => t.truncate(r.below(t.len() as u64) as usize),
                    2 => {
                        let i = r.below(t.len() as u64) as usize;
                        let chunk = t[i..].to_vec();
                        t.splice(i..i, chunk);
                    }
                    _ => t = t.iter().copied().filter(|&b| b != b'"' || r.chance(0.98)).collect(),
                }
                mutated += 1;
                match profile::from_json(&t, &cat) {
                    Ok(p) => {
                        assert!(
                            p.record.evidence.len() <= 600
                                && p.record.detections.len() <= 200
                                && p.record.timeline.len() <= 1500
                        );
                        assert!(p.record.checks.len() == cat.checks.len());
                    }
                    Err(e) => {
                        assert!(!e.is_empty());
                        refused += 1;
                    }
                }
            }
        }
    }
    assert!(profiles > 50 && mutated > 200 && refused > 0, "{profiles} profiles, {mutated} mutated, {refused} refused");
    println!("coverage: {profiles} profiles captured and read back, {mutated} hostile variants ({refused} refused)");
}
