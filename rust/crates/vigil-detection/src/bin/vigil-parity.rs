//! vigil-parity — the Rust half of the differential harness (RUST_PLAN P3A, P3A_PLAN.md §2).
//!
//!   vigil-parity <plan-dir> <out.txt> [--full <variant> <scenario> <dump.txt>]
//!   vigil-parity --bench <corpus-plan-dir> <reps>      (tick cost; twin of `VigilParity --bench`)
//!
//! Reads `<plan-dir>/plan.jsonl` (written by `VigilParity --export` or `VigilParity --generate`),
//! replays every run through the Rust engine — the same trace, marks and configuration snapshots
//! at the same frames — and writes the result in the `VigilParity` format. The C++ and Rust
//! engines agree when the two outputs are byte-identical:
//!
//! * corpus plans: the whole `VigilParity` output (registry declaration, corpus shapes, one
//!   digest per run, full default dumps);
//! * generated plans: one digest line, one summary line and (P3B) one `sinks` line per scenario
//!   (`expected.txt`): the journal, the history file after every flush, the "detections" log, the
//!   marks file, captured profiles and the records' history fields, written by the Rust sinks.
//!
//! Exit 0 written · 2 usage or unreadable input.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use vigil_detection::clock::{Clock, LocalTime};
use vigil_detection::engine::Engine;
use vigil_detection::marks::Marks;
use vigil_detection::parity::{
    Out, corpus_line, dump_engine, dump_registry, fnv, mask_times, sinks_text, summary_line,
};
use vigil_detection::printf::Arg;
use vigil_detection::sinks::LogFile;
use vigil_formats::config::EngineConfig;
use vigil_formats::json::{self, Text, Value};
use vigil_formats::marks::MarksFile;
use vigil_formats::trace::{self, WorldFrame};

/// The wall clock of generated runs: 2100-01-01, one millisecond per read. Values from it are
/// masked like the C++ tool masks its real wall-clock window.
const CLOCK_BASE: f64 = 4_102_444_800_000.0;
const CLOCK_SPAN: f64 = 1e9;

fn read(p: &Path) -> Result<Vec<u8>, String> {
    std::fs::read(p).map_err(|e| format!("cannot read {}: {e}", p.display()))
}

fn path_of(dir: &Path, rel: &[u8]) -> PathBuf {
    dir.join(String::from_utf8_lossy(rel).as_ref())
}

struct Run {
    variant: Text,
    scenario: Text,
    trace: Text,
    marks: Text,
    history: Text,
    full: bool,
    configs: Vec<(i64, EngineConfig)>,
}

/// Where a generated run's sinks write (`<plan>/rust-work`, apart from the C++ tool's `work`).
struct Sinks {
    marks: PathBuf,
    history: PathBuf,
    journal: PathBuf,
    log: PathBuf,
}

impl Sinks {
    fn for_run(dir: &Path, name: &[u8]) -> Sinks {
        let work = dir.join("rust-work");
        let n = String::from_utf8_lossy(name).into_owned();
        Sinks {
            marks: work.join(format!("{n}.marks.json")),
            history: work.join(format!("{n}.history.json")),
            journal: work.join(format!("{n}-journal")),
            log: work.join(format!("{n}-log")),
        }
    }
    fn clear(&self) {
        let _ = std::fs::remove_file(&self.marks);
        let _ = std::fs::remove_file(&self.history);
        let _ = std::fs::remove_dir_all(&self.journal);
        let _ = std::fs::remove_dir_all(&self.log);
    }
}

fn parse_run(v: &Value, schema: &vigil_formats::config::Schema) -> Result<Run, String> {
    let mut configs = Vec::new();
    for c in v.get("configs").items() {
        let mut doc = Value::object();
        doc.set("kind", Value::from("vigil-engine-config"));
        doc.set("format", Value::from(1));
        doc.set("values", c.get("values").clone());
        let (cfg, _) = EngineConfig::from_json(&json::write(&doc, -1), schema)?;
        configs.push((c.get("frame").as_number() as i64, cfg));
    }
    Ok(Run {
        variant: v.get("variant").as_text(),
        scenario: v.get("scenario").as_text(),
        trace: v.get("trace").as_text(),
        marks: v.get("marks").as_text(),
        history: v.get("history").as_text(),
        full: v.get("full").as_bool(),
        configs,
    })
}

/// Replays one run. With `sinks` (generated plans) the engine also writes its marks file, history,
/// journal and log as the C++ tool's engine does; returns the history file after every flush.
fn replay(
    dir: &Path,
    run: &Run,
    frames: &[WorldFrame],
    sinks: Option<&Sinks>,
) -> Result<(Engine, Vec<Vec<u8>>), String> {
    let mut e = Engine::new();
    let mut flushes = Vec::new();
    match sinks {
        None => {
            if !run.marks.is_empty() {
                e.set_marks(Marks::load(&MarksFile::parse(&read(&path_of(dir, &run.marks))?)));
            }
        }
        Some(s) => {
            s.clear();
            let _ = std::fs::create_dir_all(s.marks.parent().expect("work dir"));
            e.set_clock(Clock::counter(CLOCK_BASE, 1.0));
            if !run.marks.is_empty() {
                // The engine may rewrite its marks file (binding): it works on a copy.
                std::fs::write(&s.marks, read(&path_of(dir, &run.marks))?).map_err(|e| e.to_string())?;
                e.set_marks_path(&s.marks);
            }
            if !run.history.is_empty() {
                std::fs::write(&s.history, read(&path_of(dir, &run.history))?).map_err(|e| e.to_string())?;
            }
            e.set_history_path(&s.history);
            e.set_journal_directory(&s.journal);
            e.set_log(Some(Box::new(LogFile::new(&s.log, Clock::system(), LocalTime::utc()))));
        }
    }
    let mut next = 0;
    while next < run.configs.len() && run.configs[next].0 < 0 {
        if !e.set_config(&run.configs[next].1) {
            return Err("configuration does not fit the engine's schema".into());
        }
        next += 1;
    }
    if let Some(first) = frames.first() {
        e.reset_session(first.time_ms);
    }
    let snapshot = |s: Option<&Sinks>| s.map(|s| std::fs::read(&s.history).unwrap_or_default());
    flushes.extend(snapshot(sinks));
    let mut last_flush = 0.0; // Engine::lastHistoryFlushMs_, mirrored
    for (i, f) in frames.iter().enumerate() {
        while next < run.configs.len() && run.configs[next].0 == i as i64 {
            e.set_config(&run.configs[next].1);
            next += 1;
        }
        e.on_frame(f);
        if sinks.is_some() && e.recording() && f.time_ms - last_flush > 60000.0 {
            last_flush = f.time_ms;
            flushes.extend(snapshot(sinks));
        }
    }
    if sinks.is_some() {
        e.flush_history(); // what the host does when it stops
        flushes.extend(snapshot(sinks));
    }
    Ok((e, flushes))
}

// ── Marks scripts (parity.cpp `marksScripts`, P3B_PLAN.md §3.6) ──────────────────────────────

/// Mark times as `@<action>` (each action of a script runs at its own injected time) or literally.
struct TimeMap {
    windows: usize,
}

impl TimeMap {
    fn of(&self, t: f64) -> Vec<u8> {
        if t >= CLOCK_BASE {
            let k = t - CLOCK_BASE;
            if k.fract() == 0.0 && (k as usize) < self.windows {
                return format!("@{}", k as usize).into_bytes();
            }
            return b"@?".to_vec();
        }
        vigil_detection::printf::format("%.17g", &[Arg::F(t)])
    }
    fn file(&self, text: &[u8]) -> Vec<u8> {
        const KEYS: [&[u8]; 4] = [b"\"added\":", b"\"confirmedAt\":", b"\"exemptAt\":", b"\"updated\":"];
        let mut out = Vec::with_capacity(text.len());
        let mut i = 0;
        'scan: while i < text.len() {
            for k in KEYS {
                if !text[i..].starts_with(k) {
                    continue;
                }
                let mut j = i + k.len();
                while j < text.len() && text[j] == b' ' {
                    j += 1;
                }
                let mut e = j;
                while e < text.len()
                    && (text[e].is_ascii_digit() || matches!(text[e], b'-' | b'+' | b'.' | b'e' | b'E'))
                {
                    e += 1;
                }
                out.extend_from_slice(&text[i..j]);
                let v = std::str::from_utf8(&text[j..e]).ok().and_then(|s| s.parse::<f64>().ok()).unwrap_or(0.0);
                out.extend(self.of(v));
                i = e;
                continue 'scan;
            }
            out.push(text[i]);
            i += 1;
        }
        out
    }
}

fn marks_state(m: &Marks, tm: &TimeMap, path: &Path, names: &[Text]) -> Vec<u8> {
    let mut o = Out::default();
    for k in m.all() {
        let mut rs = Vec::new();
        for x in &k.reasons {
            rs.extend_from_slice(x);
            rs.push(b';');
        }
        let (a, c, x, u) = (
            tm.of(k.added_epoch_ms),
            tm.of(k.confirmed_at_epoch_ms),
            tm.of(k.exempt_at_epoch_ms),
            tm.of(k.updated_epoch_ms),
        );
        o.line(
            "m %s|%s|%s|%d%d%d|%s|%s|%s|%s|%.9g|%s|%s|%s",
            &[
                Arg::S(&k.key),
                Arg::S(&k.name),
                Arg::S(&k.uuid),
                Arg::from(k.watch),
                Arg::from(k.confirmed),
                Arg::from(k.exempt),
                Arg::S(&a),
                Arg::S(&c),
                Arg::S(&x),
                Arg::S(&u),
                Arg::from(k.confidence_at_confirm),
                Arg::S(&k.origin),
                Arg::S(&k.note),
                Arg::S(&rs),
            ],
        );
    }
    o.line(
        "counts c=%d w=%d e=%d ro=%d rev=%llu note=%s",
        &[
            Arg::from(m.confirmed_count()),
            Arg::from(m.watched_count()),
            Arg::from(m.exempt_count()),
            Arg::from(m.read_only()),
            Arg::U(m.revision()),
            Arg::S(m.load_note()),
        ],
    );
    for q in ["", "kes", "e", "duo", "reach"] {
        for kind in -1..=2 {
            let mut keys = Vec::new();
            for k in m.search(q.as_bytes(), kind) {
                keys.extend_from_slice(&k.key);
                keys.push(b',');
            }
            o.line("search %s %d %s", &[Arg::S(q.as_bytes()), Arg::from(kind), Arg::S(&keys)]);
        }
    }
    for n in names {
        let key = m.find_by_name(n).map(|f| f.key.clone()).unwrap_or_else(|| b"-".to_vec());
        o.line("byname %s %s", &[Arg::S(n), Arg::S(&key)]);
    }
    let file = tm.file(&std::fs::read(path).unwrap_or_default());
    o.line("file %zu %016llx", &[Arg::U(file.len() as u64), Arg::U(fnv(&file))]);
    o.s
}

fn run_marks<'a>(
    dir: &Path,
    lines: impl Iterator<Item = &'a [u8]>,
    out_path: &Path,
    full: Option<(&str, &str, &Path)>,
) -> Result<(usize, usize), String> {
    use vigil_detection::marks::MarkKind;
    let names: Vec<Text> = [
        "Kestrel",
        "kestrel",
        "KESTREL",
        "Vale",
        "Notch",
        "Quill_9",
        "not a name!",
        "",
        "seventeen_chars_x",
        "Kessler",
    ]
    .iter()
    .map(|s| s.as_bytes().to_vec())
    .collect();
    let mut o = Out::default();
    o.line("# VigilParity marks format 1", &[]);
    let (mut scripts, mut actions) = (0, 0);
    for line in lines {
        let run = json::parse(line).map_err(|e| e.0.to_string())?;
        let name = run.get("name").as_text();
        let path = dir.join("marks-rust-work").join(format!("{}.json", String::from_utf8_lossy(&name)));
        let _ = std::fs::create_dir_all(path.parent().expect("dir"));
        let _ = std::fs::remove_file(&path);
        let initial = run.get("initial").as_text();
        if !initial.is_empty() {
            std::fs::write(&path, read(&path_of(dir, &initial))?).map_err(|e| e.to_string())?;
        }
        let mut tm = TimeMap { windows: 0 };
        let mut m = Marks::default();
        m.set_path(&path);
        let mut dump = b"== initial\n".to_vec();
        dump.extend(marks_state(&m, &tm, &path, &names));
        let acts = run.get("actions").items();
        for (i, a) in acts.iter().enumerate() {
            m.set_clock(Clock::fixed(CLOCK_BASE + i as f64));
            let (key, nm) = (a.get("key").as_bytes(), a.get("name").as_bytes());
            let on = a.get("on").as_bool();
            let reasons: Vec<Text> = a.get("reasons").items().iter().map(Value::as_text).collect();
            let mut ret = b"-".to_vec();
            let op = a.get("op").as_bytes();
            match op {
                b"watch" => m.set_watch(key, nm, on),
                b"confirm" => m.confirm(key, nm, reasons, a.get("confidence").as_number() as f32),
                b"unconfirm" => m.unconfirm(key),
                b"exempt" => m.set_exempt(key, nm, on),
                b"note" => m.set_note(key, a.get("note").as_bytes()),
                b"reasons" => m.set_reasons(key, reasons),
                b"remove" => m.remove(key),
                b"remember" => m.remember_name(key, nm),
                b"add" => {
                    let kind = match a.get("kind").as_number() as i32 {
                        0 => MarkKind::Watch,
                        1 => MarkKind::Confirm,
                        _ => MarkKind::Exempt,
                    };
                    ret = m.add_by_name(nm, kind, a.get("knownKey").as_bytes());
                }
                b"bind" => {
                    ret =
                        if m.bind_name(key, nm, a.get("uuid").as_bytes()) { b"bound".to_vec() } else { b"no".to_vec() };
                }
                b"reload" => m.set_path(&path),
                other => return Err(format!("unknown marks action {}", String::from_utf8_lossy(other))),
            }
            tm.windows = i + 1;
            dump.extend(format!("== {i} {} -> ", String::from_utf8_lossy(op)).into_bytes());
            dump.extend(&ret);
            dump.push(b'\n');
            dump.extend(marks_state(&m, &tm, &path, &names));
        }
        actions += acts.len();
        o.line(
            "marks %s digest=%016llx actions=%zu marks=%zu ro=%d",
            &[
                Arg::S(&name),
                Arg::U(fnv(&dump)),
                Arg::U(acts.len() as u64),
                Arg::U(m.all().len() as u64),
                Arg::from(m.read_only()),
            ],
        );
        if let Some((_, fs, fp)) = full {
            if name == fs.as_bytes() {
                std::fs::write(fp, &dump).map_err(|e| format!("cannot write {}: {e}", fp.display()))?;
            }
        }
        scripts += 1;
    }
    std::fs::write(out_path, &o.s).map_err(|e| format!("cannot write {}: {e}", out_path.display()))?;
    Ok((scripts, actions))
}

fn run_plan(dir: &Path, out_path: &Path, full: Option<(&str, &str, &Path)>) -> Result<(usize, usize), String> {
    let plan = read(&dir.join("plan.jsonl"))?;
    let mut lines = plan.split(|&b| b == b'\n').map(|l| l.strip_suffix(b"\r").unwrap_or(l)).filter(|l| !l.is_empty());
    let header = json::parse(lines.next().ok_or("empty plan")?).map_err(|e| e.0.to_string())?;
    if header.get("kind").as_bytes() == b"vigil-parity-marks" {
        return run_marks(dir, lines, out_path, full);
    }
    if header.get("kind").as_bytes() != b"vigil-parity-plan" {
        return Err("not a parity plan".into());
    }
    let corpus = header.get("mode").as_bytes() == b"corpus";
    let probe = Engine::new();
    let schema = probe.schema().clone();
    let load = |rel: &[u8]| -> Result<Vec<WorldFrame>, String> { Ok(trace::read(&read(&path_of(dir, rel))?)?.frames) };
    // Corpus traces are replayed under every configuration: read each once.
    let mut corpus_traces: HashMap<Text, Vec<WorldFrame>> = HashMap::new();

    let mut o = Out::default();
    if corpus {
        o.line("# VigilParity format 1", &[]);
        dump_registry(&probe, &mut o);
        for s in header.get("scenarios").items() {
            let rel = s.get("trace").as_text();
            let frames = load(&rel)?;
            corpus_line(&mut o, s.get("name").as_bytes(), &frames);
            corpus_traces.insert(rel, frames);
        }
    } else {
        o.line("# VigilParity generated format 1", &[]);
    }
    let mut full_defaults = Vec::new();
    let mut runs = 0;
    let mut frames_total = 0;
    for line in lines {
        let v = json::parse(line).map_err(|e| e.0.to_string())?;
        let run = parse_run(&v, &schema)?;
        let loaded;
        let frames: &[WorldFrame] = match corpus_traces.get(&run.trace) {
            Some(f) => f,
            None => {
                loaded = load(&run.trace)?;
                &loaded
            }
        };
        frames_total += frames.len();
        let sinks = if corpus { None } else { Some(Sinks::for_run(dir, &run.scenario)) };
        let (e, flushes) = replay(dir, &run, frames, sinks.as_ref())?;
        let mut d = Out::default();
        dump_engine(&e, &mut d);
        o.line(
            "variant %s %s digest=%016llx evidence=%d detections=%d alerts=%llu",
            &[
                Arg::S(&run.variant),
                Arg::S(&run.scenario),
                Arg::U(fnv(&d.s)),
                Arg::from(e.session().total_evidence()),
                Arg::from(e.session().total_detections()),
                Arg::U(e.alert_seq()),
            ],
        );
        let mut sinks_full = None;
        if !corpus {
            o.s.extend(summary_line(&e, &run.scenario));
            let s = sinks.as_ref().expect("generated runs have sinks");
            let (lo, hi) = (CLOCK_BASE, CLOCK_BASE + CLOCK_SPAN);
            let mut history = Vec::new();
            for h in &flushes {
                history.extend_from_slice(b"== flush\n");
                history.extend(mask_times(h, &["lastSeen"], lo, hi));
                history.push(b'\n');
            }
            let (line, text) =
                sinks_text(&e, &s.journal, &s.log, &s.marks, &history, flushes.len() as i32, lo, hi, &run.scenario);
            o.s.extend(line);
            sinks_full = Some(text);
            s.clear();
        }
        if run.full {
            full_defaults.extend_from_slice(b"== ");
            full_defaults.extend_from_slice(&run.scenario);
            full_defaults.push(b'\n');
            full_defaults.extend_from_slice(&d.s);
        }
        if let Some((fv, fs, fp)) = full {
            if run.variant == fv.as_bytes() && run.scenario == fs.as_bytes() {
                std::fs::write(fp, &d.s).map_err(|e| format!("cannot write {}: {e}", fp.display()))?;
                if let Some(text) = &sinks_full {
                    let mut p = fp.as_os_str().to_owned();
                    p.push(".sinks.txt");
                    std::fs::write(PathBuf::from(p), text).map_err(|e| format!("cannot write sinks dump: {e}"))?;
                }
            }
        }
        runs += 1;
        if runs % 500 == 0 {
            println!("  {runs} runs");
        }
    }
    if corpus {
        o.s.extend_from_slice(b"# full state, defaults\n");
        o.s.extend(full_defaults);
    }
    std::fs::write(out_path, &o.s).map_err(|e| format!("cannot write {}: {e}", out_path.display()))?;
    Ok((runs, frames_total))
}

/// `--bench <plan-dir> <reps>`: the twin of `VigilParity --bench` — each corpus scenario under the
/// defaults, `reps` times, timing only `on_frame`.
fn bench(dir: &Path, reps: usize) -> Result<(), String> {
    let plan = read(&dir.join("plan.jsonl"))?;
    let first = plan.split(|&b| b == b'\n').next().ok_or("empty plan")?;
    let header = json::parse(first.strip_suffix(b"\r").unwrap_or(first)).map_err(|e| e.0.to_string())?;
    let (mut total_micros, mut total_frames) = (0.0, 0usize);
    for s in header.get("scenarios").items() {
        let frames = trace::read(&read(&path_of(dir, &s.get("trace").as_text()))?)?.frames;
        let mut micros = 0.0;
        for _ in 0..reps {
            let mut e = Engine::new();
            e.reset_session(frames[0].time_ms);
            let t0 = std::time::Instant::now();
            for f in &frames {
                e.on_frame(f);
            }
            micros += t0.elapsed().as_secs_f64() * 1e6;
        }
        total_micros += micros;
        total_frames += frames.len() * reps;
        println!(
            "bench {:<20} frames={:5} reps={reps} us_per_frame={:.3}",
            String::from_utf8_lossy(s.get("name").as_bytes()),
            frames.len(),
            micros / (frames.len() * reps) as f64
        );
    }
    println!(
        "bench {:<20} frames={:5} reps={reps} us_per_frame={:.3}",
        "all",
        total_frames / reps,
        total_micros / total_frames as f64
    );
    Ok(())
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.len() == 3 && args[0] == "--bench" {
        let reps = args[2].parse().unwrap_or(1).max(1);
        return match bench(Path::new(&args[1]), reps) {
            Ok(()) => ExitCode::SUCCESS,
            Err(e) => {
                eprintln!("vigil-parity: {e}");
                ExitCode::from(2)
            }
        };
    }
    let mut positional = Vec::new();
    let mut full = None;
    let mut i = 0;
    while i < args.len() {
        if args[i] == "--full" && i + 3 < args.len() {
            full = Some((args[i + 1].clone(), args[i + 2].clone(), PathBuf::from(&args[i + 3])));
            i += 4;
        } else {
            positional.push(args[i].clone());
            i += 1;
        }
    }
    if positional.len() != 2 {
        eprintln!("usage: vigil-parity <plan-dir> <out.txt> [--full <variant> <scenario> <dump.txt>]");
        return ExitCode::from(2);
    }
    let full_ref = full.as_ref().map(|(v, s, p)| (v.as_str(), s.as_str(), p.as_path()));
    let started = std::time::Instant::now();
    match run_plan(Path::new(&positional[0]), Path::new(&positional[1]), full_ref) {
        Ok((runs, frames)) => {
            println!(
                "{runs} runs, {frames} frames replayed by the Rust engine in {:.1} s -> {}",
                started.elapsed().as_secs_f64(),
                positional[1]
            );
            ExitCode::SUCCESS
        }
        Err(e) => {
            eprintln!("vigil-parity: {e}");
            ExitCode::from(2)
        }
    }
}
