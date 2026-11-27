//! vigil-fmt — the Rust twin of `VigilFormats` (tools/formats).
//!
//!   vigil-fmt canon <kind> <in> [<out>] [--schema <engine-schema.json>]
//!       Writes the canonical form (parse + write) of <in>, read as <kind>, to <out> or stdout.
//!       Kinds: json, trace, outcome, marks, profile, config, schema. `profile` and `config` need
//!       the engine schema. Exit 0 written · 3 refused (the reason on stderr) · 2 usage or I/O.

use std::io::Write;
use std::process::ExitCode;

use vigil_formats::canon::{KINDS, canon};
use vigil_formats::config::Schema;

fn usage() -> ExitCode {
    eprintln!("usage: vigil-fmt canon <kind> <in> [<out>] [--schema <engine-schema.json>]");
    eprintln!("       kinds: {}", KINDS.join(", "));
    ExitCode::from(2)
}

fn main() -> ExitCode {
    let mut args: Vec<String> = std::env::args().skip(1).collect();
    let mut schema_path = None;
    if let Some(i) = args.iter().position(|a| a == "--schema") {
        if i + 1 >= args.len() {
            return usage();
        }
        schema_path = Some(args.remove(i + 1));
        args.remove(i);
    }
    if args.len() < 3 || args[0] != "canon" {
        return usage();
    }
    let schema = match schema_path {
        Some(p) => match std::fs::read(&p).map_err(|e| e.to_string()).and_then(|b| Schema::from_json(&b)) {
            Ok(s) => Some(s),
            Err(e) => {
                eprintln!("cannot use schema {p}: {e}");
                return ExitCode::from(2);
            }
        },
        None => None,
    };
    let input = match std::fs::read(&args[2]) {
        Ok(b) => b,
        Err(e) => {
            eprintln!("cannot read {}: {e}", args[2]);
            return ExitCode::from(2);
        }
    };
    match canon(&args[1], &input, schema.as_ref()) {
        Ok(bytes) => {
            let written = match args.get(3) {
                Some(out) => std::fs::write(out, &bytes),
                None => std::io::stdout().write_all(&bytes),
            };
            if let Err(e) = written {
                eprintln!("cannot write: {e}");
                return ExitCode::from(2);
            }
            ExitCode::SUCCESS
        }
        Err(reason) => {
            eprintln!("refused: {reason}");
            ExitCode::from(3)
        }
    }
}
