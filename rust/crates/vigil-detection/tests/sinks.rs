//! The persistence sinks and the simulator, checked against committed C++ files and on their own
//! (P3B_PLAN.md V4, V5, V7, V11). Differential parity of the sinks with the C++ engine over
//! generated sessions is `tools/ci/engine_parity.py` (the `sinks` lines).

mod common;

use std::path::Path;

use common::scenarios::golden_arena;
use common::{TempFile, read_all, repo, temp_dir, without_cr, write_all};
use vigil_detection::clock::{Clock, LocalTime};
use vigil_detection::engine::Engine;
use vigil_detection::marks::Marks;
use vigil_detection::printf::Arg;
use vigil_detection::replay::replay_frames;
use vigil_detection::sinks::{LogFile, LogLines, LogSink, MAX_LOG_BYTES, writef_message};
use vigil_detection::snapshot::{capture, catalog_of};
use vigil_formats::json;
use vigil_formats::profile;
use vigil_formats::trace::{self, WorldFrame};

fn golden() -> Vec<WorldFrame> {
    trace::read(&read_all(&repo().join("tests/traces/golden-arena.vtrace"))).unwrap().frames
}

/// The simulator port, frame for frame: the Rust golden arena is the committed C++ trace.
#[test]
fn golden_arena_equals_the_committed_trace_frame_for_frame() {
    let committed = golden();
    let mine = golden_arena();
    assert_eq!(mine.len(), committed.len());
    for (i, (a, b)) in mine.iter().zip(&committed).enumerate() {
        assert_eq!(trace::frame_line(a), trace::frame_line(b), "frame {i}");
    }
}

/// Profile capture, byte for byte: `inputs/profile/session-hopper.json` is what the C++ engine
/// captured from the golden arena (tools/formats `sessionProfile`); the Rust capture of the same
/// session with the same identity fields writes the same file.
#[test]
fn profile_capture_reproduces_the_committed_fixture() {
    let mut e = Engine::new();
    replay_frames(&mut e, &golden_arena());
    let r = e.session().players().iter().find(|r| r.name == b"LowHopper").expect("LowHopper");
    let mut s = capture(&e, r, b"fixture");
    s.saved_at_epoch_ms = 1_764_201_600_000.0;
    s.note = b"Saved for the format fixtures \xE2\x80\x94 \"quotes\", a tab\t and a newline\n.".to_vec();
    s.nicked = true;
    s.likely_account = b"Likely_Main".to_vec();
    s.candidates = vec![b"Likely_Main".to_vec(), b"Other".to_vec()];
    s.reasons = vec![b"KillAura".to_vec(), b"Reach".to_vec()];
    s.watched = true;
    s.confirmed = true;
    let committed = without_cr(&read_all(&repo().join("tests/fixtures/formats/inputs/profile/session-hopper.json")));
    assert!(!committed.is_empty());
    assert_eq!(String::from_utf8_lossy(&profile::to_json(&s, &catalog_of(&e))), String::from_utf8_lossy(&committed));
}

/// The journal: one line per observation and per detection, in the session's file
/// (`session-<start epoch>.jsonl`, named from the injected clock), with the C++ fields in order.
#[test]
fn journal_lines_and_file_name() {
    let dir = temp_dir("journal");
    let mut e = Engine::new();
    e.set_clock(Clock::fixed(1_700_000_000_123.0));
    e.set_journal_directory(&dir);
    let frames = golden();
    replay_frames(&mut e, &frames);
    let files: Vec<_> =
        std::fs::read_dir(&dir).unwrap().flatten().map(|f| f.file_name().to_string_lossy().into_owned()).collect();
    assert_eq!(files, vec!["session-1700000000123.jsonl".to_string()]);
    let text = read_all(&dir.join(&files[0]));
    let lines: Vec<&[u8]> = text.split(|&b| b == b'\n').filter(|l| !l.is_empty()).collect();
    let evidence: usize =
        e.session().players().iter().map(|r| r.checks.iter().map(|c| c.evidence as usize).sum::<usize>()).sum();
    let detections = e.session().total_detections() as usize;
    assert_eq!(lines.len(), evidence + detections);
    let first = json::parse(lines[0]).unwrap();
    let keys: Vec<String> = first.members().iter().map(|(k, _)| String::from_utf8_lossy(k).into_owned()).collect();
    assert_eq!(
        keys,
        ["type", "time", "player", "uuid", "check", "indicator", "severity", "confidence", "summary", "values"]
    );
    let det = lines.iter().map(|l| json::parse(l).unwrap()).find(|j| j.get("type").as_bytes() == b"detection").unwrap();
    let keys: Vec<String> = det.members().iter().map(|(k, _)| String::from_utf8_lossy(k).into_owned()).collect();
    assert_eq!(keys, ["type", "time", "player", "uuid", "check", "confidence", "evidence"]);
    // data.journal off: no journal.
    let off = temp_dir("journal-off");
    let mut e = Engine::new();
    let mut c = e.config().clone();
    c.values[e.schema().find(b"data.journal").unwrap()] = 0.0;
    e.set_config(&c);
    e.set_journal_directory(&off);
    replay_frames(&mut e, &frames);
    assert_eq!(std::fs::read_dir(&off).unwrap().count(), 0);
    let _ = std::fs::remove_dir_all(dir);
    let _ = std::fs::remove_dir_all(off);
}

/// The history: loaded entries reach new records; a flush merges this session's players with
/// evidence (sessions + 1, detections added, peak = max, lastSeen = now) and keeps the others.
#[test]
fn history_load_apply_and_merge() {
    let f = TempFile::new("history.json");
    let frames = golden();
    let uuid = |name: &str| {
        frames.iter().flat_map(|fr| &fr.players).find(|p| p.name == name.as_bytes()).map(|p| p.uuid.clone()).unwrap()
    };
    let hopper = String::from_utf8(uuid("LowHopper")).unwrap();
    let walker = String::from_utf8(uuid("Walker")).unwrap();
    write_all(
        &f.path,
        format!(
            r#"{{"format":1,"players":{{"{hopper}":{{"name":"Old","sessions":4,"detections":2,"peak":99.75,"lastSeen":1600000000000}},"{walker}":{{"name":"W","sessions":1e300}},"stranger":{{"name":"S","sessions":2}}}}}}"#
        )
        .as_bytes(),
    );
    let mut e = Engine::new();
    e.set_clock(Clock::fixed(1_800_000_000_000.0));
    e.set_history_path(&f.path);
    replay_frames(&mut e, &frames);
    let rec = |e: &Engine, n: &str| e.session().players().iter().find(|r| r.name == n.as_bytes()).cloned().unwrap();
    let h = rec(&e, "LowHopper");
    assert_eq!(
        (h.prior_sessions, h.prior_detections, h.prior_peak, h.last_seen_epoch_ms),
        (4, 2, 99.75, 1_600_000_000_000.0)
    );
    assert_eq!(rec(&e, "Walker").prior_sessions, i32::MIN, "1e300 converts as MSVC's cvttsd2si");
    e.flush_history();
    let saved = json::parse(&read_all(&f.path)).unwrap();
    let p = saved.get("players");
    let hj = p.get(&hopper);
    assert_eq!(hj.get("sessions").as_number(), 5.0);
    assert_eq!(hj.get("detections").as_number(), 2.0 + h.detections.len() as f64);
    assert_eq!(hj.get("peak").as_number(), f64::from(99.75f32.max(h.peak_suspicion)));
    assert_eq!(hj.get("lastSeen").as_number(), 1_800_000_000_000.0);
    assert_eq!(p.get("stranger").get("sessions").as_number(), 2.0, "entries of players not seen are kept");
    assert!(
        p.find("Observer").is_none() && p.members().iter().all(|(_, v)| v.get("name").as_bytes() != b"Observer"),
        "the local player is never recorded"
    );
    // std::map order.
    let keys: Vec<_> = p.members().iter().map(|(k, _)| k.clone()).collect();
    let mut sorted = keys.clone();
    sorted.sort();
    assert_eq!(keys, sorted);
}

/// The log channel: one "DETECTION …" line per detection (in C++ text) and the message cut that
/// `writef`'s 2 048-byte buffer makes.
#[test]
fn detections_log_channel() {
    let lines = LogLines::default();
    let mut e = Engine::new();
    e.set_log(Some(Box::new(lines.clone())));
    replay_frames(&mut e, &golden());
    let got = lines.lines();
    assert_eq!(got.len() as i32, e.session().total_detections());
    assert!(got.iter().all(|(c, _)| c == "detections"));
    let first = String::from_utf8(got[0].1.clone()).unwrap();
    assert!(
        first.starts_with("DETECTION LowHopper ")
            && first.contains("% (")
            && first.contains(" evidence, ")
            && first.contains(" \u{2014} "),
        "{first}"
    );
    let long = vec![b'x'; 5000];
    assert_eq!(writef_message("[%s] %s: %s", &[Arg::S(b"bhop"), Arg::S(b"P"), Arg::S(&long)]).len(), 2047);
}

/// `core/log`'s file format and rotation.
#[test]
fn log_file_format_and_rotation() {
    let dir = temp_dir("logfile");
    let mut f = LogFile::new(&dir, Clock::fixed(1_764_201_600_000.0), LocalTime::offset(-6 * 3600));
    f.write("detections", b"first");
    assert_eq!(read_all(&dir.join("detections.log")), b"2025-11-26 18:00:00  first\n");
    std::fs::write(dir.join("detections.log"), vec![b'.'; MAX_LOG_BYTES as usize + 1]).unwrap();
    f.write("detections", b"after rotation");
    assert_eq!(std::fs::metadata(dir.join("detections.log.old")).unwrap().len(), MAX_LOG_BYTES + 1);
    assert_eq!(read_all(&dir.join("detections.log")), b"2025-11-26 18:00:00  after rotation\n");
    let _ = std::fs::remove_dir_all(dir);
}

/// Hostile sink inputs never panic: random bytes as history and marks files, a journal
/// "directory" that is a file, and paths that cannot be written.
#[test]
fn hostile_sink_inputs_never_panic() {
    let frames = golden();
    let mut seed = 0x5EED_5155_u64;
    let mut next = || {
        seed ^= seed >> 12;
        seed ^= seed << 25;
        seed ^= seed >> 27;
        seed.wrapping_mul(0x2545_F491_4F6C_DD1D)
    };
    let base = br#"{"format":1,"players":{"00000000-0000-4000-8000-100000000003":{"name":"N","sessions":3,"detections":1,"peak":50,"lastSeen":1}}}"#;
    for case in 0..30 {
        let h = TempFile::new(&format!("hostile-history-{case}.json"));
        let m = TempFile::new(&format!("hostile-marks-{case}.json"));
        let mut bytes = base.to_vec();
        for _ in 0..next() % 12 {
            let i = (next() % bytes.len() as u64) as usize;
            bytes[i] = next() as u8;
        }
        if case % 5 == 0 {
            bytes = (0..next() % 300).map(|_| next() as u8).collect();
        }
        write_all(&h.path, &bytes);
        write_all(&m.path, &bytes);
        let blocker = TempFile::new(&format!("hostile-journal-{case}"));
        write_all(&blocker.path, b"not a directory");
        let mut e = Engine::new();
        e.set_history_path(&h.path);
        e.set_marks_path(&m.path);
        e.set_journal_directory(&blocker.path);
        e.set_log(Some(Box::new(LogFile::new(&blocker.path.join("logs"), Clock::system(), LocalTime::utc()))));
        replay_frames(&mut e, &frames[..200]);
        e.flush_history();
        let _ = Marks::load(&vigil_formats::marks::MarksFile::parse(&bytes));
    }
    // A history path inside a file: nothing written, nothing breaks.
    let blocker = TempFile::new("hostile-blocker");
    write_all(&blocker.path, b"x");
    let mut e = Engine::new();
    e.set_history_path(&blocker.path.join("players.json"));
    replay_frames(&mut e, &frames[..100]);
    e.flush_history();
    assert!(Path::new(&blocker.path).is_file());
}
