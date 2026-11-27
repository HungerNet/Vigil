//! The C++ ↔ Rust format contract, checked against fixtures the C++ side generated
//! (tests/fixtures; P2_PLAN.md §2.3):
//!
//! * every committed fixture parses;
//! * for every input, Rust's canonical form equals the committed C++ canonical form byte for byte,
//!   or both refuse with the same reason (except the documented divergences);
//! * every canonical form is a fixed point;
//! * the number writer reproduces 4 000+ C++-written doubles and reads them back bit for bit;
//! * the math functions the engine uses return the same bits as the MSVC CRT;
//! * mutated inputs never make a reader panic.
//!
//! Committed files are compared without `\r` (a checkout may convert line endings).

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use vigil_formats::canon::canon;
use vigil_formats::catalog::CheckCatalog;
use vigil_formats::config::{EngineConfig, Schema};
use vigil_formats::json::{self, Value};
use vigil_formats::{outcome, show, trace};

fn fixtures() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../tests")
}

fn read(p: &Path) -> Vec<u8> {
    std::fs::read(p).unwrap_or_else(|e| panic!("cannot read {}: {e}", p.display()))
}

fn without_cr(b: &[u8]) -> Vec<u8> {
    b.iter().copied().filter(|&c| c != b'\r').collect()
}

fn schema() -> Schema {
    Schema::from_json(&read(&fixtures().join("fixtures/engine-schema.json"))).expect("engine schema")
}

const KINDS: [&str; 6] = ["json", "trace", "outcome", "marks", "profile", "config"];

fn inputs(kind: &str) -> Vec<PathBuf> {
    let dir = fixtures().join("fixtures/formats/inputs").join(kind);
    let mut v: Vec<PathBuf> = std::fs::read_dir(&dir)
        .unwrap_or_else(|e| panic!("{}: {e}", dir.display()))
        .map(|e| e.unwrap().path())
        .filter(|p| p.is_file())
        .collect();
    v.sort();
    v
}

/// kind/file → (C++ verdict, Rust verdict) for documented divergences.
fn divergences() -> BTreeMap<String, (String, String)> {
    let text = String::from_utf8(without_cr(&read(&fixtures().join("fixtures/formats/divergences.txt")))).unwrap();
    text.lines()
        .filter(|l| !l.starts_with('#') && !l.trim().is_empty())
        .map(|l| {
            let f: Vec<&str> = l.split('\t').collect();
            (f[0].to_string(), (f[1].to_string(), f[2].to_string()))
        })
        .collect()
}

#[test]
fn canonical_forms_match_cpp() {
    let schema = schema();
    let divergences = divergences();
    let (mut same, mut refused_alike, mut diverged) = (0, 0, 0);
    let mut failures = Vec::new();
    for kind in KINDS {
        for input in inputs(kind) {
            let name = input.file_name().unwrap().to_string_lossy().into_owned();
            let id = format!("{kind}/{name}");
            let canon_dir = fixtures().join("fixtures/formats/canon").join(kind);
            let accepted = canon_dir.join(&name);
            let refused = canon_dir.join(format!("{name}.refused"));
            let rust = canon(kind, &read(&input), Some(&schema));
            if let Some((cpp, rs)) = divergences.get(&id) {
                let cpp_ok = accepted.exists();
                let rust_ok = rust.is_ok();
                if (cpp == "accepts") == cpp_ok && (rs == "accepts") == rust_ok {
                    diverged += 1;
                } else {
                    failures.push(format!(
                        "{id}: documented divergence no longer holds (C++ accepts: {cpp_ok}, Rust accepts: {rust_ok})"
                    ));
                }
                continue;
            }
            match (accepted.exists(), rust) {
                (true, Ok(bytes)) => {
                    let expected = without_cr(&read(&accepted));
                    if bytes != expected {
                        let at = bytes
                            .iter()
                            .zip(&expected)
                            .position(|(a, b)| a != b)
                            .unwrap_or(bytes.len().min(expected.len()));
                        failures.push(format!("{id}: canonical form differs from C++ at byte {at}"));
                    } else if canon(kind, &bytes, Some(&schema)).as_deref() != Ok(&bytes[..]) {
                        failures.push(format!("{id}: Rust canonical form is not a fixed point"));
                    } else {
                        same += 1;
                    }
                }
                (true, Err(e)) => failures.push(format!("{id}: C++ accepts, Rust refuses ({e})")),
                (false, Ok(_)) => failures.push(format!("{id}: C++ refuses, Rust accepts")),
                (false, Err(e)) => {
                    let reason = String::from_utf8(without_cr(&read(&refused))).unwrap();
                    if reason.trim_end() == e {
                        refused_alike += 1;
                    } else {
                        failures.push(format!(
                            "{id}: refused for a different reason: C++ \"{}\", Rust \"{e}\"",
                            reason.trim_end()
                        ));
                    }
                }
            }
        }
    }
    println!("{same} identical canonical forms, {refused_alike} identical refusals, {diverged} documented divergences");
    assert!(failures.is_empty(), "{} problems:\n{}", failures.len(), failures.join("\n"));
    assert!(same >= 20 && refused_alike >= 30, "the fixture set looks incomplete");
}

#[test]
fn committed_fixtures_parse_and_are_canonical() {
    // The golden trace and its outcome (written by the C++ engine and VigilReplay).
    let golden = read(&fixtures().join("traces/golden-arena.vtrace"));
    let t = trace::read(&golden).expect("golden trace");
    assert_eq!(t.frames.len(), 600);
    assert_eq!(trace::write(&t), without_cr(&golden), "golden trace re-serializes byte for byte");
    let expect = read(&fixtures().join("traces/golden-arena.expect.json"));
    let o = outcome::from_json(&expect).expect("golden outcome");
    assert_eq!(o.frames, 600);
    let mut committed = without_cr(&expect);
    if committed.last() == Some(&b'\n') {
        committed.pop();
    }
    assert_eq!(outcome::to_json(&o), committed, "golden outcome re-serializes byte for byte");
    assert!(outcome::compare(&o, &o, 0.0).is_empty());

    // The engine declaration and default configuration (P1).
    let schema_bytes = read(&fixtures().join("fixtures/engine-schema.json"));
    let schema = Schema::from_json(&schema_bytes).unwrap();
    assert_eq!(schema.params.len(), 248);
    let mut sj = schema.to_json();
    sj.push(b'\n');
    assert_eq!(sj, without_cr(&schema_bytes), "engine schema re-serializes byte for byte");
    let default_bytes = read(&fixtures().join("fixtures/engine-config.default.json"));
    let (cfg, report) = EngineConfig::from_json(&default_bytes, &schema).unwrap();
    assert!(report.unknown.is_empty() && report.missing.is_empty());
    assert_eq!(cfg, EngineConfig::defaults(&schema), "the default document holds the declared defaults");
    let mut cj = cfg.to_json(&schema, 2);
    cj.push(b'\n');
    assert_eq!(cj, without_cr(&default_bytes));

    // The check catalog derived from the declaration is the one the C++ engine reports.
    let cat_json = json::parse(&read(&fixtures().join("fixtures/formats/check-catalog.json"))).unwrap();
    let from_cpp = CheckCatalog {
        checks: cat_json.get("checks").items().iter().map(|c| c.get("id").as_text()).collect(),
        indicators: cat_json
            .get("checks")
            .items()
            .iter()
            .map(|c| c.get("indicators").items().iter().map(Value::as_text).collect())
            .collect(),
    };
    assert_eq!(CheckCatalog::from_schema(&schema), from_cpp);
    assert_eq!(from_cpp.checks.len(), 11);
    assert_eq!(from_cpp.indicators.iter().map(Vec::len).sum::<usize>(), 24);
}

#[test]
fn numbers_match_cpp() {
    let text = without_cr(&read(&fixtures().join("fixtures/formats/numbers.txt")));
    let (mut n, mut bad) = (0, Vec::new());
    for line in text.split(|&b| b == b'\n').filter(|l| !l.is_empty() && l[0] != b'#') {
        let tab = line.iter().position(|&b| b == b'\t').unwrap();
        let bits = u64::from_str_radix(std::str::from_utf8(&line[..tab]).unwrap(), 16).unwrap();
        let expected = std::str::from_utf8(&line[tab + 1..]).unwrap();
        let d = f64::from_bits(bits);
        let written = json::format_number(d);
        if written != expected {
            bad.push(format!("{bits:016x}: C++ wrote {expected}, Rust writes {written}"));
        }
        if d.is_finite() {
            match json::parse(expected.as_bytes()) {
                Ok(Value::Number(back)) if back.to_bits() == bits => {}
                other => bad.push(format!("{bits:016x}: {expected} reads back as {other:?}")),
            }
        }
        n += 1;
    }
    assert!(n > 4000, "number table too small ({n})");
    assert!(
        bad.is_empty(),
        "{} of {n} numbers differ:\n{}",
        bad.len(),
        bad.iter().take(20).cloned().collect::<Vec<_>>().join("\n")
    );
}

#[test]
fn math_functions_match_the_msvc_crt() {
    let text = without_cr(&read(&fixtures().join("fixtures/formats/math.txt")));
    let mut per_fn: BTreeMap<String, (usize, usize)> = BTreeMap::new();
    let mut examples = Vec::new();
    for line in text.split(|&b| b == b'\n').filter(|l| !l.is_empty() && l[0] != b'#') {
        let f: Vec<&str> = std::str::from_utf8(line).unwrap().split('\t').collect();
        let x = |i: usize| f64::from_bits(u64::from_str_radix(f[i], 16).unwrap());
        let (a, b, expected) = (x(1), x(2), x(3));
        let got = match f[0] {
            "sin" => a.sin(),
            "cos" => a.cos(),
            "atan2" => a.atan2(b),
            "pow" => a.powf(b),
            "sqrt" => a.sqrt(),
            "fmod" => a % b,
            "floor" => a.floor(),
            "ceil" => a.ceil(),
            "round" => a.round(),
            "fmax" => a.max(b),
            other => panic!("unknown function {other}"),
        };
        let e = per_fn.entry(f[0].to_string()).or_default();
        e.0 += 1;
        if got.to_bits() != expected.to_bits() {
            e.1 += 1;
            if examples.len() < 10 {
                examples.push(format!("{}({a:e}, {b:e}): CRT {expected:e}, Rust {got:e}", f[0]));
            }
        }
    }
    let summary: Vec<String> = per_fn.iter().map(|(k, (n, d))| format!("{k} {d}/{n}")).collect();
    println!("mismatches per function: {}", summary.join(", "));
    assert!(per_fn.values().all(|&(_, d)| d == 0), "{}\n{}", summary.join(", "), examples.join("\n"));
}

// A tiny deterministic generator (xorshift64*), so the mutation test needs no dependency.
struct Rng(u64);
impl Rng {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 >> 12;
        self.0 ^= self.0 << 25;
        self.0 ^= self.0 >> 27;
        self.0.wrapping_mul(0x2545_F491_4F6C_DD1D)
    }
    fn below(&mut self, n: usize) -> usize {
        (self.next() % n.max(1) as u64) as usize
    }
}

#[test]
fn mutated_inputs_never_panic_and_accepted_ones_are_canonical() {
    let schema = schema();
    let mut rng = Rng(0x5EED_2026_1127);
    let (mut runs, mut accepted) = (0, 0);
    const SPICE: &[u8] = b"{}[]\",:\\0123456789eE+-.tfn \n\x00\xff";
    for kind in KINDS {
        for input in inputs(kind) {
            let original = read(&input);
            for _ in 0..60 {
                let mut m = original.clone();
                for _ in 0..1 + rng.below(4) {
                    match rng.below(5) {
                        0 if !m.is_empty() => {
                            let i = rng.below(m.len());
                            m[i] = SPICE[rng.below(SPICE.len())];
                        }
                        1 if !m.is_empty() => {
                            let i = rng.below(m.len());
                            m.truncate(i);
                        }
                        2 => {
                            let i = rng.below(m.len() + 1);
                            m.insert(i, SPICE[rng.below(SPICE.len())]);
                        }
                        3 if m.len() > 2 => {
                            let i = rng.below(m.len() - 1);
                            let j = i + 1 + rng.below((m.len() - i - 1).min(64));
                            let piece = m[i..j].to_vec();
                            m.splice(i..i, piece);
                        }
                        _ => {
                            let i = rng.below(m.len() + 1);
                            m.insert(i, rng.next() as u8);
                        }
                    }
                }
                runs += 1;
                if let Ok(out) = canon(kind, &m, Some(&schema)) {
                    accepted += 1;
                    assert_eq!(
                        canon(kind, &out, Some(&schema)).as_deref(),
                        Ok(&out[..]),
                        "{kind}: the canonical form of a mutated {} is not a fixed point",
                        show(input.file_name().unwrap().as_encoded_bytes())
                    );
                }
            }
        }
    }
    println!("{runs} mutated inputs, {accepted} accepted, none panicked");
}
