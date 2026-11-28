//! The C++ semantics the port stands on, checked against tables the C++ side generated
//! (tests/fixtures/formats; P3A_PLAN.md V6, V7):
//!
//! * `hash-order.txt` — `std::unordered_map` iteration order after random insert/erase/clear
//!   scripts over int and string keys (H1, H2);
//! * `printf.txt` — `snprintf` output for every conversion the engine and the parity dump use (H3);
//! * `sort-order.txt` — MSVC `std::sort` on ties, including the heap-sort fallback (P3B);
//! * `random.txt` — `std::mt19937` and the MSVC distributions the C++ tests draw from (P3B).

mod common;

use std::path::{Path, PathBuf};

use common::rng::{Discrete, Mt19937, UniformReal};
use vigil_detection::hashorder::HashOrder;
use vigil_detection::msvcsort::{SortPaths, sort_by_traced};
use vigil_detection::printf::{Arg, format};

fn fixtures() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../tests/fixtures/formats")
}

fn lines(name: &str) -> Vec<Vec<u8>> {
    let bytes = std::fs::read(fixtures().join(name)).unwrap_or_else(|e| panic!("{name}: {e}"));
    bytes
        .split(|&b| b == b'\n')
        .map(|l| l.strip_suffix(b"\r").unwrap_or(l).to_vec())
        .filter(|l| !l.is_empty() && l[0] != b'#')
        .collect()
}

enum Map {
    Int(HashOrder<i32, ()>),
    Text(HashOrder<Vec<u8>, ()>),
}

impl Map {
    fn order(&self) -> Vec<u8> {
        let mut s = Vec::new();
        match self {
            Map::Int(m) => {
                for k in m.keys() {
                    s.extend(k.to_string().bytes());
                    s.push(b',');
                }
            }
            Map::Text(m) => {
                for k in m.keys() {
                    s.extend(k);
                    s.push(b',');
                }
            }
        }
        s
    }
    fn len(&self) -> usize {
        match self {
            Map::Int(m) => m.len(),
            Map::Text(m) => m.len(),
        }
    }
}

#[test]
fn unordered_map_iteration_order_matches_msvc() {
    let mut map: Option<Map> = None;
    let (mut scripts, mut ops, mut mismatches) = (0, 0, Vec::new());
    let mut script = String::new();
    for line in lines("hash-order.txt") {
        let text = String::from_utf8(line.clone()).unwrap();
        if let Some(rest) = text.strip_prefix("script ") {
            scripts += 1;
            script = rest.to_string();
            map =
                Some(if rest.contains(" string ") { Map::Text(HashOrder::new()) } else { Map::Int(HashOrder::new()) });
            continue;
        }
        let f: Vec<&str> = text.split('\t').collect();
        let m = map.as_mut().expect("a script header first");
        if f[0] == "end" {
            let expected = f.get(1).copied().unwrap_or("").as_bytes();
            if m.order() != expected {
                mismatches.push(format!("script {script}: final order differs"));
            }
            continue;
        }
        ops += 1;
        match (f[0], &mut *m) {
            ("i", Map::Int(x)) => {
                x.entry_or_insert_with(f[1].parse().unwrap(), || ());
            }
            ("e", Map::Int(x)) => {
                x.remove(&f[1].parse().unwrap());
            }
            ("i", Map::Text(x)) => {
                x.entry_or_insert_with(f[1].as_bytes().to_vec(), || ());
            }
            ("e", Map::Text(x)) => {
                x.remove(&f[1].as_bytes().to_vec());
            }
            ("c", Map::Int(x)) => x.clear(),
            ("c", Map::Text(x)) => x.clear(),
            (op, _) => panic!("unknown op {op}"),
        }
        let size: usize = f[2].parse().unwrap();
        let hash = format!("{:016x}", digest(&m.order()));
        if m.len() != size || hash != f[3] {
            mismatches.push(format!("script {script}, op {ops} ({} {}): size {} vs {size}", f[0], f[1], m.len()));
        }
    }
    assert!(scripts >= 12 && ops > 10_000, "{scripts} scripts, {ops} operations");
    assert!(
        mismatches.is_empty(),
        "{} of {ops} operations differ, first: {:?}",
        mismatches.len(),
        &mismatches[..mismatches.len().min(5)]
    );
}

/// The order digest the fixture uses: VigilParity's FNV variant (offset 1469598103934665603, one
/// digit shorter than the FNV basis that `std::hash` itself uses).
fn digest(bytes: &[u8]) -> u64 {
    let mut h: u64 = 1_469_598_103_934_665_603;
    for &b in bytes {
        h ^= u64::from(b);
        h = h.wrapping_mul(1_099_511_628_211);
    }
    h
}

fn hex_bits(s: &str) -> f64 {
    f64::from_bits(u64::from_str_radix(s, 16).unwrap())
}

#[test]
fn printf_matches_the_ucrt() {
    let (mut rows, mut diffs) = (0, Vec::new());
    for line in lines("printf.txt") {
        let mut f = line.splitn(3, |&b| b == b'\t');
        let fmt = String::from_utf8(f.next().unwrap().to_vec()).unwrap();
        let arg = f.next().unwrap().to_vec();
        let expected = f.next().unwrap_or(b"").to_vec();
        let text = String::from_utf8_lossy(&arg[1..]).into_owned();
        let out = match arg[0] {
            b'd' => format(&fmt, &[Arg::F(hex_bits(&text))]),
            b'i' => format(&fmt, &[Arg::I(text.parse().unwrap())]),
            b'u' => format(&fmt, &[Arg::U(text.parse().unwrap())]),
            b's' => format(&fmt, &[Arg::S(&arg[1..])]),
            other => panic!("unknown argument kind {other}"),
        };
        rows += 1;
        if out != expected {
            diffs.push(format!(
                "{fmt} {text}: C++ {:?}, Rust {:?}",
                String::from_utf8_lossy(&expected),
                String::from_utf8_lossy(&out)
            ));
        }
    }
    assert!(rows > 9_000, "{rows} rows");
    assert!(diffs.is_empty(), "{} of {rows} differ: {:#?}", diffs.len(), &diffs[..diffs.len().min(12)]);
}

fn ints(s: &str) -> Vec<i64> {
    if s.is_empty() { Vec::new() } else { s.split(',').map(|x| x.parse().unwrap()).collect() }
}

#[test]
fn std_sort_order_matches_msvc() {
    let (mut cases, mut heap_cases, mut heap_with_ties, mut diffs) = (0, 0, 0, Vec::new());
    for line in lines("sort-order.txt") {
        let text = String::from_utf8(line).unwrap();
        let f: Vec<&str> = text.split('\t').collect();
        let keys = ints(f.get(2).copied().unwrap_or(""));
        let expected = ints(f.get(3).copied().unwrap_or(""));
        let mut v: Vec<(i64, i64)> = keys.iter().enumerate().map(|(i, &k)| (k, i as i64)).collect();
        let mut paths = SortPaths::default();
        sort_by_traced(&mut v, |a, b| a.0 < b.0, &mut paths);
        let order: Vec<i64> = v.iter().map(|p| p.1).collect();
        cases += 1;
        if paths.heap > 0 {
            heap_cases += 1;
            let mut k = keys.clone();
            k.sort_unstable();
            if k.windows(2).any(|w| w[0] == w[1]) {
                heap_with_ties += 1;
            }
        }
        if order != expected {
            diffs.push(format!("case {} ({}, n={})", f[0], f[1], keys.len()));
        }
    }
    assert!(cases > 200, "{cases} cases");
    assert!(diffs.is_empty(), "{} of {cases} cases differ: {:?}", diffs.len(), &diffs[..diffs.len().min(10)]);
    // The adversarial inputs must actually reach the heap sort, and with equal keys in the heap
    // (where the order of ties depends on the heap operations themselves).
    assert!(
        heap_cases > 0 && heap_with_ties > 0,
        "heap sort reached in {heap_cases} cases ({heap_with_ties} with ties)"
    );
}

#[test]
fn test_random_generators_match_msvc() {
    let mut rows = 0;
    for line in lines("random.txt") {
        let text = String::from_utf8(line).unwrap();
        let f: Vec<&str> = text.split('\t').collect();
        let seed: u32 = f[1].parse().unwrap();
        let mut g = Mt19937::new(seed);
        match f[0] {
            "mt19937" => {
                let got: Vec<i64> = (0..64).map(|_| i64::from(g.next_u32())).collect();
                assert_eq!(got, ints(f[3]), "mt19937 seed {seed}");
            }
            "mt19937-10000" => {
                g.discard(9999);
                assert_eq!(i64::from(g.next_u32()), f[3].parse::<i64>().unwrap(), "10000th output");
            }
            "uniform_real" => {
                let p: Vec<f64> = f[2].split(' ').map(|x| x.parse().unwrap()).collect();
                let d = UniformReal::new(p[0], p[1]);
                for (i, bits) in f[3].split(',').enumerate() {
                    let v = d.sample(&mut g);
                    assert_eq!(v.to_bits(), hex_bits(bits).to_bits(), "uniform_real seed {seed} value {i}: {v}");
                }
            }
            "discrete" => {
                let w: Vec<f64> = f[2].split(',').map(hex_bits).collect();
                let d = Discrete::new(&w);
                let got: Vec<i64> = (0..400).map(|_| i64::from(d.sample(&mut g))).collect();
                assert_eq!(got, ints(f[3]), "discrete seed {seed}");
            }
            other => panic!("unknown generator {other}"),
        }
        rows += 1;
    }
    assert_eq!(rows, 15, "rows");
}
