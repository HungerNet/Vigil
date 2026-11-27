//! replay-check — reads a trace and an outcome and checks them against each other
//! (RUST_PLAN.md P2). In P3 it gains the Rust engine and replays the trace itself; today it proves
//! that the Rust side reads both files exactly as the C++ side does.
//!
//!   replay-check <trace.vtrace> <outcome.json> [--strict]
//!
//! Checks: both files parse; the outcome covers as many frames as the trace holds; every player
//! the outcome names appears in the trace. With --strict, both files must also be in canonical
//! form (what the C++ writers produce), byte for byte.
//! Exit 0 consistent · 1 inconsistent · 2 usage or unreadable.

use std::collections::BTreeSet;
use std::process::ExitCode;

use vigil_formats::json::Text;
use vigil_formats::{outcome, show, trace};

fn without_cr(b: &[u8]) -> Vec<u8> {
    b.iter().copied().filter(|&c| c != b'\r').collect()
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let strict = args.iter().any(|a| a == "--strict");
    let files: Vec<&String> = args.iter().filter(|a| !a.starts_with("--")).collect();
    if files.len() != 2 {
        eprintln!("usage: replay-check <trace.vtrace> <outcome.json> [--strict]");
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
    if strict {
        if trace::write(&t) != without_cr(&trace_bytes) {
            problems.push("the trace is not in canonical form (not what the C++ writer produces)".into());
        }
        // VigilReplay writes outcomes without a final newline; an editor may have added one.
        let mut committed = without_cr(&outcome_bytes);
        if committed.last() == Some(&b'\n') {
            committed.pop();
        }
        if outcome::to_json(&o) != committed {
            problems.push("the outcome is not in canonical form".into());
        }
    }
    for p in &problems {
        println!("  PROBLEM {p}");
    }
    if problems.is_empty() {
        println!("trace and outcome are consistent{}", if strict { " and canonical" } else { "" });
        ExitCode::SUCCESS
    } else {
        ExitCode::from(1)
    }
}
