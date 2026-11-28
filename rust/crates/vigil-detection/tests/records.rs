//! tests/records_tests.cpp, mirrored test for test (P3B_PLAN.md §3.1): the user's durable records —
//! marks (watch, confirmed by you, exempt), players added by name, saved / exported / imported
//! profiles, and the format guards that keep one Vigil version from destroying another version's
//! files. `settings keep unknown keys` tests the C++ settings registry only (host, `core/settings`)
//! and has no counterpart here (tests/test-parity.json).

#[macro_use]
mod common;

use common::sim::{PI, Sim, World, setup_local};
use common::{Expect, TempFile, read_all, temp_dir, write_all};
use vigil_detection::clock::{Clock, LocalTime};
use vigil_detection::marks::{MarkKind, Marks};
use vigil_detection::snapshot::{SnapshotStore, capture, catalog_of, envelope_of};
use vigil_detection::types::Vec3;
use vigil_formats::catalog::CheckCatalog;
use vigil_formats::json::{self, Value};
use vigil_formats::profile;

const REAL_UUID: &[u8] = b"069a79f4-44e9-4726-a5be-fca90e38aaf5"; // version 4
const NICK_UUID: &[u8] = b"0b4f2ba3-6c5d-1e8f-9a0b-1c2d3e4f5a6b"; // version 1

fn texts(v: &[&str]) -> Vec<Vec<u8>> {
    v.iter().map(|s| s.as_bytes().to_vec()).collect()
}

/// Confirm, watch and exempt are separate decisions with defined interactions.
#[test]
fn mark_semantics() {
    let mut x = Expect::new("mark semantics");
    let f = TempFile::new("vigil_test_marks2.json");
    let mut m = Marks::default();
    m.set_path(&f.path);
    m.confirm(b"k1", b"Kestrel", texts(&["KillAura"]), 82.0);
    let k1 = m.find(b"k1");
    expect!(x, k1.is_some_and(|k| k.confirmed && k.watch && !k.exempt), "confirming also watches");
    expect!(
        x,
        k1.is_some_and(|k| k.confidence_at_confirm == 82.0),
        "Vigil's confidence at the moment of confirming is kept"
    );
    m.set_exempt(b"k1", b"Kestrel", true);
    let k1 = m.find(b"k1");
    expect!(
        x,
        k1.is_some_and(|k| k.exempt && !k.confirmed && !k.watch),
        "exempting clears the confirmation and the watch"
    );
    expect!(x, m.exempt(b"k1") && !m.confirmed(b"k1") && !m.watched(b"k1"), "queries agree");
    m.confirm(b"k1", b"Kestrel", Vec::new(), -1.0);
    expect!(x, m.confirmed(b"k1") && !m.exempt(b"k1"), "confirming clears an exemption");
    m.set_exempt(b"k2", b"Friend", true);
    m.set_exempt(b"k2", b"Friend", false);
    expect!(x, m.find(b"k2").is_none(), "removing the only mark removes the record");
    m.set_note(b"k1", b"Aim snaps every fight");
    m.set_reasons(b"k1", texts(&["KillAura", "Reach"]));
    let mut back = Marks::default();
    back.set_path(&f.path);
    let b = back.find(b"k1");
    expect!(
        x,
        b.is_some_and(|b| b.note == b"Aim snaps every fight" && b.reasons.len() == 2),
        "notes and edited reasons persist"
    );
    expect!(x, back.exempt_count() == 0 && back.confirmed_count() == 1, "counts");
    x.finish();
}

#[test]
fn search_marks() {
    let mut x = Expect::new("search marks");
    let f = TempFile::new("vigil_test_marks_search.json");
    let mut m = Marks::default();
    m.set_path(&f.path);
    m.confirm(b"a", b"Kestrel", texts(&["KillAura"]), -1.0);
    m.set_watch(b"b", b"Kessler", true);
    m.set_exempt(b"c", b"Friendly", true);
    m.set_note(b"c", b"my duo partner");
    expect!(x, m.search(b"kes", -1).len() == 2, "name search ({})", m.search(b"kes", -1).len());
    expect!(x, m.search(b"duo", -1).len() == 1, "note search");
    expect!(x, m.search(b"killaura", -1).len() == 1, "reason search");
    expect!(x, m.search(b"", MarkKind::Exempt as i32).len() == 1, "kind filter: exempt");
    expect!(x, m.search(b"", MarkKind::Watch as i32).len() == 1, "kind filter: watching excludes confirmed");
    expect!(x, m.search(b"", MarkKind::Confirm as i32).len() == 1, "kind filter: confirmed");
    x.finish();
}

/// A player added by name binds to the first real account with that name — never to a nick.
#[test]
fn add_by_name_and_binding() {
    let mut x = Expect::new("add by name and binding");
    let f = TempFile::new("vigil_test_marks_bind.json");
    let mut m = Marks::default();
    m.set_path(&f.path);
    expect!(x, m.add_by_name(b"not a name!", MarkKind::Confirm, b"").is_empty(), "invalid names are refused");
    let key = m.add_by_name(b"Notch", MarkKind::Confirm, b"");
    expect!(x, key == b"user:notch", "added by name ({})", String::from_utf8_lossy(&key));
    expect!(x, m.find(&key).is_some_and(|k| k.origin == b"name" && k.confirmed), "origin recorded");
    expect!(x, !m.bind_name(NICK_UUID, b"Notch", NICK_UUID), "a nick (v1) never takes the mark");
    expect!(x, m.find(&key).is_some(), "the mark waits for the real account");
    expect!(x, m.bind_name(REAL_UUID, b"notch", REAL_UUID), "the real account binds (case-insensitive)");
    expect!(x, m.find(&key).is_none() && m.confirmed(REAL_UUID), "the mark now follows the account");
    // Adding the same person by name later updates their account mark, not a second one.
    expect!(x, m.add_by_name(b"NOTCH", MarkKind::Watch, b"") == REAL_UUID, "a known name reuses the account key");
    x.finish();
}

/// The engine never judges an exempt player, and binds name-added marks on first sight.
#[test]
fn engine_exempt_and_binding() {
    let mut x = Expect::new("engine: exempt and binding");
    let f = TempFile::new("vigil_test_marks_engine.json");
    let mut w = World::new();
    w.engine.set_marks_path(&f.path);
    let mut local = Sim::new(1, "Observer");
    setup_local(&mut local);
    let mut friend = Sim::new(2, "Buddy");
    friend.f.uuid = REAL_UUID.to_vec();
    w.add(local);
    let friend = w.add(friend);
    w.engine.marks_mut().add_by_name(b"Buddy", MarkKind::Exempt, b"");
    // Low-hopping at 0.55 b/tick: plenty of Speed and BHop evidence for anyone judged.
    for _ in 0..20 * 30 {
        let s = &mut w[friend];
        let rad = s.move_yaw * PI / 180.0;
        let fwd = Vec3::new(-rad.sin(), 0.0, rad.cos());
        if s.f.on_ground {
            s.motion.y = 0.25;
            s.set_pos(s.pos() + Vec3::new(0.0, 0.01, 0.0));
            s.f.on_ground = false;
        }
        s.set_pos(s.pos() + fwd * 0.55);
        s.f.pos[1] += s.motion.y;
        if s.f.pos[1] <= s.ground_y {
            s.f.pos[1] = s.ground_y;
            s.motion.y = 0.0;
            s.f.on_ground = true;
        } else {
            s.motion.y = (s.motion.y - 0.08) * 0.98;
        }
        s.f.sprinting = true;
        s.tick_animations();
        w.frame();
    }
    let r = w.rec(friend);
    expect!(x, r.is_some(), "an exempt player is still listed");
    expect!(x, w.engine.marks().exempt(REAL_UUID), "the name-added exemption bound to the account");
    expect!(
        x,
        r.is_some_and(|r| r.evidence.is_empty() && r.detections.is_empty()),
        "an exempt player is never judged (evidence={})",
        r.map_or(0, |r| r.evidence.len())
    );
    if let Some(r) = r {
        let s = w.engine.player_state(r);
        expect!(x, s.listed && s.exempt && !s.judged, "player state: listed, exempt, not judged");
    }
    expect!(x, w.engine.alerts().is_empty(), "no alerts for an exempt player");
    // Lifting the exemption: judged again from now on.
    w.engine.marks_mut().set_exempt(REAL_UUID, b"Buddy", false);
    for _ in 0..20 * 20 {
        let p = w[friend].pos();
        w[friend].set_pos(p + Vec3::new(0.0, 0.0, 0.55));
        w.frame();
    }
    expect!(x, w.rec(friend).is_some_and(|r| !r.evidence.is_empty()), "evidence resumes once the exemption is removed");
    x.finish();
}

/// Format 1 marks files (dev.8) load unchanged; a newer format is never overwritten.
#[test]
fn marks_format_guards() {
    let mut x = Expect::new("marks format guards");
    let f = TempFile::new("vigil_test_marks_format.json");
    write_all(
        &f.path,
        br#"{"format":1,"players":{"abc":{"name":"Old","watch":true,"confirmed":true,"confirmedAt":1700000000000,"updated":1700000000000,"reasons":["Reach"]}}}"#,
    );
    let mut m = Marks::default();
    m.set_path(&f.path);
    expect!(x, m.confirmed(b"abc") && !m.read_only(), "format 1 loads");
    expect!(x, m.find(b"abc").is_some_and(|a| a.added_epoch_ms > 0.0), "added time derived for old marks");
    write_all(&f.path, br#"{"format":99,"players":{"abc":{"name":"Future","watch":true}}}"#);
    let before = read_all(&f.path);
    let mut n = Marks::default();
    n.set_path(&f.path);
    expect!(x, n.read_only(), "a newer format opens read-only");
    n.set_watch(b"xyz", b"Someone", true);
    expect!(x, read_all(&f.path) == before, "and is never overwritten");
    write_all(&f.path, b"{ not json");
    let mut bad = Marks::default();
    bad.set_path(&f.path);
    expect!(x, bad.read_only() && !bad.load_note().is_empty(), "an unreadable file is preserved, not replaced");
    x.finish();
}

/// Numbers survive a write/parse round trip exactly (traces and profiles depend on it).
#[test]
fn json_number_round_trip() {
    let mut x = Expect::new("json number round trip");
    for v in [0.1, 0.356123456789, 1.0 / 3.0, 64.03125, -1e-9, 123456.789012345, 0.4] {
        let parsed = json::parse(&json::write(&Value::Number(v), -1));
        expect!(x, parsed.is_ok_and(|p| p.as_number() == v), "round trip {v:.17}");
    }
    let short = json::write(&Value::Number(0.4), -1);
    expect!(x, short == b"0.4", "short form kept for readability ({})", String::from_utf8_lossy(&short));
    x.finish();
}

/// A session with evidence and detections for the snapshot tests.
fn build_session() -> (World, usize) {
    let mut w = World::new();
    let mut local = Sim::new(1, "Observer");
    setup_local(&mut local);
    w.add(local);
    let cheat = w.add(Sim::new(2, "Glider"));
    for _ in 0..20 * 30 {
        let p = w[cheat].pos();
        w[cheat].set_pos(p + Vec3::new(0.0, 0.0, 0.6));
        w[cheat].f.on_ground = true;
        w[cheat].f.sprinting = true;
        w.frame();
    }
    (w, cheat)
}

#[test]
fn snapshot_round_trip() {
    let mut x = Expect::new("snapshot round trip");
    let (w, cheat) = build_session();
    let r = w.rec(cheat);
    expect!(
        x,
        r.is_some_and(|r| !r.evidence.is_empty()),
        "sanity: evidence recorded ({})",
        r.map_or(0, |r| r.evidence.len())
    );
    let Some(r) = r else {
        x.finish();
        return;
    };
    let cat = catalog_of(&w.engine);
    let mut s = capture(&w.engine, r, b"test");
    s.note = b"Saved during testing".to_vec();
    s.nicked = true;
    s.likely_account = b"RealGlider".to_vec();
    let text = profile::to_json(&s, &cat);
    let back = profile::from_json(&text, &cat);
    expect!(x, back.is_ok(), "parses ({:?})", back.as_ref().err());
    let back = back.unwrap_or_default();
    expect!(x, back.record.name == b"Glider" && back.record.uuid == r.uuid, "identity");
    expect!(
        x,
        back.record.evidence.len() == r.evidence.len(),
        "every observation ({} vs {})",
        back.record.evidence.len(),
        r.evidence.len()
    );
    expect!(x, back.record.detections.len() == r.detections.len(), "every detection");
    expect!(x, (back.record.suspicion - r.suspicion).abs() < 1e-4, "confidence");
    expect!(x, back.note == s.note && back.nicked && back.likely_account == b"RealGlider", "note and identity summary");
    if let (Some(a), Some(b)) = (r.evidence.back(), back.record.evidence.last()) {
        expect!(
            x,
            a.check as usize == b.check
                && a.indicator as usize == b.indicator
                && a.summary == b.summary
                && a.values.len() == b.values.len(),
            "observation fields"
        );
        let eb = envelope_of(b);
        expect!(x, eb.valid() == a.envelope.valid() && eb.limit == a.envelope.limit, "envelope re-derived identically");
    }
    if let Some(speed) = w.engine.check_by_id("speed") {
        let i = speed.base.index as usize;
        let (ca, cb) = (&r.checks[i], &back.record.checks[i]);
        expect!(
            x,
            ca.evidence == cb.evidence && ca.detections == cb.detections && ca.indicator_counts == cb.indicator_counts,
            "per-check counts"
        );
    }
    x.finish();
}

/// A file from another version: unknown checks are dropped and listed, known ones remapped by id.
#[test]
fn snapshot_check_remapping() {
    let mut x = Expect::new("snapshot check remapping");
    let (w, cheat) = build_session();
    let Some(r) = w.rec(cheat) else {
        x.finish();
        return;
    };
    let cat = catalog_of(&w.engine);
    let text = profile::to_json(&capture(&w.engine, r, b"test"), &cat);
    // An older version with "speed" missing and the remaining checks in reverse order.
    let mut other: CheckCatalog = cat.clone();
    let si = other.checks.iter().position(|c| c == b"speed").expect("speed");
    other.checks.remove(si);
    other.indicators.remove(si);
    other.checks.reverse();
    other.indicators.reverse();
    let back = profile::from_json(&text, &other);
    expect!(x, back.is_ok(), "parses");
    let back = back.unwrap_or_default();
    expect!(x, back.dropped_checks.iter().any(|d| d == b"speed"), "unknown check listed");
    let mapped = back.record.evidence.iter().all(|e| other.checks[e.check] != b"speed");
    expect!(x, mapped, "evidence references the reader's own check indices");
    expect!(x, back.record.checks.len() == other.checks.len(), "check records sized for the reader");
    x.finish();
}

/// Hostile or broken files are refused cleanly.
#[test]
fn snapshot_rejects_bad_files() {
    let mut x = Expect::new("snapshot rejects bad files");
    let w = World::new();
    let cat = catalog_of(&w.engine);
    expect!(x, profile::from_json(b"{", &cat).is_err_and(|e| !e.is_empty()), "malformed JSON");
    expect!(x, profile::from_json(br#"{"kind":"something-else","format":1}"#, &cat).is_err(), "wrong kind");
    expect!(
        x,
        profile::from_json(br#"{"kind":"vigil-profile","format":99,"player":{"name":"X"}}"#, &cat)
            .is_err_and(|e| e.contains("newer")),
        "newer format explains itself"
    );
    expect!(x, profile::from_json(br#"{"kind":"vigil-profile","format":1,"player":{}}"#, &cat).is_err(), "no player");
    let big = vec![b' '; profile::MAX_FILE_BYTES + 1];
    expect!(x, profile::from_json(&big, &cat).is_err(), "oversized input");
    // Counts are capped no matter what the file claims.
    let mut many = String::from(r#"{"kind":"vigil-profile","format":1,"player":{"name":"Spam"},"evidence":["#);
    for i in 0..2000 {
        if i > 0 {
            many.push(',');
        }
        many.push_str(r#"{"check":"speed","indicator":"sustained","summary":"x","values":[]}"#);
    }
    many.push_str("]}");
    let s = profile::from_json(many.as_bytes(), &cat);
    expect!(
        x,
        s.as_ref().is_ok_and(|s| s.record.evidence.len() <= 600),
        "evidence capped ({})",
        s.as_ref().map_or(0, |s| s.record.evidence.len())
    );
    x.finish();
}

/// Save, list, note, export, import (file and pending folder), delete.
#[test]
fn snapshot_store() {
    let mut x = Expect::new("snapshot store");
    let (w, cheat) = build_session();
    let Some(r) = w.rec(cheat) else {
        x.finish();
        return;
    };
    let cat = catalog_of(&w.engine);
    let root = temp_dir("vigil_test_profiles");
    let mut store = SnapshotStore::new(Clock::system(), LocalTime::utc());
    store.set_directories(&root.join("profiles"), &root.join("exports"), &root.join("imports"));
    let saved = store.save(&capture(&w.engine, r, b"test"), &cat);
    expect!(x, saved.is_ok(), "save ({:?})", saved.as_ref().err());
    let id = saved.unwrap_or_default();
    expect!(x, store.list().len() == 1 && store.list()[0].name == b"Glider", "listed");
    expect!(
        x,
        store.set_note(&id, b"keep for the report", &cat) && store.list()[0].note == b"keep for the report",
        "note"
    );
    let exported = store.export_to(&id);
    expect!(x, exported.as_ref().is_ok_and(|p| p.exists()), "export ({:?})", exported);
    let exported = exported.unwrap_or_default();
    let name = exported.to_string_lossy().into_owned();
    expect!(
        x,
        name.len() > ".vigil-profile.json".len() && name.ends_with(".vigil-profile.json"),
        "export file name ({name})"
    );
    let imported = store.import_file(&exported, &cat);
    expect!(x, imported.as_ref().is_ok_and(|i| *i != id), "import a file ({:?})", imported.as_ref().err());
    let imported = imported.unwrap_or_default();
    expect!(x, store.list().len() == 2, "both listed");
    expect!(x, store.load(&imported, &cat).is_ok_and(|s| s.origin == b"imported"), "imported origin");
    let _ = std::fs::create_dir_all(root.join("imports"));
    let _ = std::fs::copy(&exported, root.join("imports").join("from-a-friend.json"));
    write_all(&root.join("imports").join("broken.json"), b"{");
    let mut errors = Vec::new();
    expect!(
        x,
        store.import_pending(&cat, &mut errors) == 1 && errors.len() == 1,
        "pending imports: one added, one refused"
    );
    expect!(x, !root.join("imports").join("from-a-friend.json").exists(), "imported files leave the imports folder");
    expect!(x, root.join("imports").join("broken.json").exists(), "refused files stay for the user to inspect");
    expect!(x, store.remove(&id) && store.list().len() == 2, "delete");
    let _ = std::fs::remove_dir_all(&root);
    x.finish();
}
