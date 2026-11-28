//! replay-check — reads a trace and an outcome and checks them against each other (RUST_PLAN P2);
//! since P3A it can also replay the trace through the Rust engine (`--engine`).
//!
//!   replay-check <trace.vtrace> <outcome.json> [--strict] [--engine] [--config cfg.json] [--write out.json]
//!
//! Always: both files parse; the outcome covers as many frames as the trace holds; every player the
//! outcome names appears in the trace. `--strict`: both files are in canonical form (what the C++
//! writers produce), byte for byte. `--engine`: the Rust engine replays the trace (under the
//! default configuration, or `--config`) and its outcome must agree with the given one
//! (`compareOutcomes`); with `--strict` its outcome file must also be byte-identical to the given
//! one. `--write` saves the Rust engine's outcome (as `VigilReplay --json` does).
//! Exit 0 consistent · 1 inconsistent · 2 usage or unreadable.

use std::collections::BTreeSet;
use std::process::ExitCode;

use vigil_detection::engine::Engine;
use vigil_detection::replay::replay_frames;
use vigil_formats::config::EngineConfig;
use vigil_formats::json::Text;
use vigil_formats::{outcome, show, trace};

fn without_cr(b: &[u8]) -> Vec<u8> {
    b.iter().copied().filter(|&c| c != b'\r').collect()
}

fn option(args: &[String], name: &str) -> Option<String> {
    args.iter().position(|a| a == name).and_then(|i| args.get(i + 1).cloned())
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let strict = args.iter().any(|a| a == "--strict");
    let engine = args.iter().any(|a| a == "--engine");
    let config_path = option(&args, "--config");
    let write_path = option(&args, "--write");
    let mut files: Vec<&String> = Vec::new();
    let mut skip = false;
    for a in &args {
        if skip {
            skip = false;
            continue;
        }
        if a == "--config" || a == "--write" {
            skip = true;
        } else if !a.starts_with("--") {
            files.push(a);
        }
    }
    if files.len() != 2 {
        eprintln!(
            "usage: replay-check <trace.vtrace> <outcome.json> [--strict] [--engine] [--config cfg.json] [--write out.json]"
        );
        return ExitCode::from(2);
    }
    let (trace_bytes, outcome_bytes) = match (std::fs::read(files[0]), std::fs::read(files[1])) {
        (Ok(t), Ok(o)) => (t, o),
        (Err(e), _) | (_, Err(e)) => {
            eprintln!("cannot read: {e}");
            return ExitCode::from(2);
        }
    };
    let t = match trace::read(&trace_bytes) {
        Ok(t) => t,
        Err(e) => {
            eprintln!("cannot read trace: {e}");
            return ExitCode::from(2);
        }
    };
    let o = match outcome::from_json(&outcome_bytes) {
        Ok(o) => o,
        Err(e) => {
            eprintln!("cannot read outcome: {e}");
            return ExitCode::from(2);
        }
    };

    let names: BTreeSet<&Text> = t.frames.iter().flat_map(|f| f.players.iter().map(|p| &p.name)).collect();
    let chat: usize = t.frames.iter().map(|f| f.chat.len()).sum();
    let (first, last) = (t.frames.first().map_or(0, |f| f.tick), t.frames.last().map_or(0, |f| f.tick));
    let seconds = t.frames.last().map_or(0.0, |l| (l.time_ms - t.frames[0].time_ms) / 1000.0);
    println!(
        "trace: format {}, recorded by Vigil {} (build {}), {} frames, ticks {first}..{last} ({seconds:.1} s), {} players, {chat} chat lines",
        t.header.format,
        show(&t.header.vigil),
        show(&t.header.build),
        t.frames.len(),
        names.len()
    );
    println!("outcome: {} frames, {} players", o.frames, o.players.len());

    let mut problems = Vec::new();
    if o.frames as usize != t.frames.len() {
        problems.push(format!("the outcome covers {} frames, the trace holds {}", o.frames, t.frames.len()));
    }
    for (name, p) in &o.players {
        if !names.contains(name) {
            problems.push(format!("the outcome names {}, who never appears in the trace", show(name)));
        }
        let detections: Vec<String> = p.detections.iter().map(|(c, n)| format!("{}:{n}", show(c))).collect();
        println!(
            "  {:<18} suspicion {:5.1}%  peak {:5.1}%  {}",
            show(name),
            p.suspicion,
            p.peak,
            detections.join("  ")
        );
    }
    // VigilReplay writes outcomes without a final newline; an editor may have added one.
    let mut committed = without_cr(&outcome_bytes);
    if committed.last() == Some(&b'\n') {
        committed.pop();
    }
    if strict {
        if trace::write(&t) != without_cr(&trace_bytes) {
            problems.push("the trace is not in canonical form (not what the C++ writer produces)".into());
        }
        if outcome::to_json(&o) != committed {
            problems.push("the outcome is not in canonical form".into());
        }
    }
    if engine {
        let mut e = Engine::new();
        if let Some(path) = &config_path {
            let text = match std::fs::read(path) {
                Ok(t) => t,
                Err(err) => {
                    eprintln!("cannot read configuration {path}: {err}");
                    return ExitCode::from(2);
                }
            };
            match EngineConfig::from_json(&text, e.schema()) {
                Ok((cfg, report)) => {
                    e.set_config(&cfg);
                    println!(
                        "configuration {path} ({} unknown keys ignored, {} defaults kept)",
                        report.unknown.len(),
                        report.missing.len()
                    );
                }
                Err(err) => {
                    eprintln!("cannot use configuration {path}: {err}");
                    return ExitCode::from(2);
                }
            }
        }
        let actual = replay_frames(&mut e, &t.frames);
        let text = outcome::to_json(&actual);
        if let Some(path) = &write_path {
            if let Err(err) = std::fs::write(path, &text) {
                eprintln!("cannot write {path}: {err}");
                return ExitCode::from(2);
            }
        }
        let diffs = outcome::compare(&o, &actual, outcome::DEFAULT_TOLERANCE);
        for d in &diffs {
            println!("  DIFF {d}");
        }
        println!(
            "Rust engine: {} players, {}",
            actual.players.len(),
            if diffs.is_empty() { "outcome matches".to_string() } else { format!("{} differences", diffs.len()) }
        );
        problems.extend(diffs.into_iter().map(|d| format!("engine outcome differs: {d}")));
        if strict && text != committed {
            problems.push("the Rust engine's outcome file is not byte-identical to the given one".into());
        }
    }
    for p in &problems {
        println!("  PROBLEM {p}");
    }
    if problems.is_empty() {
        println!(
            "trace and outcome are consistent{}{}",
            if strict { " and canonical" } else { "" },
            if engine { "; the Rust engine reproduces the outcome" } else { "" }
        );
        ExitCode::SUCCESS
    } else {
        ExitCode::from(1)
    }
}
