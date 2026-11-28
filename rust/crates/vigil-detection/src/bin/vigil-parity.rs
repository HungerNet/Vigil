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
//! * generated plans: one digest line and one summary line per scenario (`expected.txt`).
//!
//! Exit 0 written · 2 usage or unreadable input.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use vigil_detection::engine::Engine;
use vigil_detection::marks::Marks;
use vigil_detection::parity::{Out, corpus_line, dump_engine, dump_registry, fnv, summary_line};
use vigil_detection::printf::Arg;
use vigil_formats::config::EngineConfig;
use vigil_formats::json::{self, Text, Value};
use vigil_formats::marks::MarksFile;
use vigil_formats::trace::{self, WorldFrame};

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
    full: bool,
    configs: Vec<(i64, EngineConfig)>,
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
        full: v.get("full").as_bool(),
        configs,
    })
}

fn replay(dir: &Path, run: &Run, frames: &[WorldFrame]) -> Result<Engine, String> {
    let mut e = Engine::new();
    if !run.marks.is_empty() {
        e.set_marks(Marks::load(&MarksFile::parse(&read(&path_of(dir, &run.marks))?)));
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
    for (i, f) in frames.iter().enumerate() {
        while next < run.configs.len() && run.configs[next].0 == i as i64 {
            e.set_config(&run.configs[next].1);
            next += 1;
        }
        e.on_frame(f);
    }
    Ok(e)
}

fn run_plan(dir: &Path, out_path: &Path, full: Option<(&str, &str, &Path)>) -> Result<(usize, usize), String> {
    let plan = read(&dir.join("plan.jsonl"))?;
    let mut lines = plan.split(|&b| b == b'\n').map(|l| l.strip_suffix(b"\r").unwrap_or(l)).filter(|l| !l.is_empty());
    let header = json::parse(lines.next().ok_or("empty plan")?).map_err(|e| e.0.to_string())?;
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
        let e = replay(dir, &run, frames)?;
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
        if !corpus {
            o.s.extend(summary_line(&e, &run.scenario));
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
