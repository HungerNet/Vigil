//! The C++ semantics the port stands on, checked against tables the C++ side generated
//! (tests/fixtures/formats; P3A_PLAN.md V6, V7):
//!
//! * `hash-order.txt` — `std::unordered_map` iteration order after random insert/erase/clear
//!   scripts over int and string keys (H1, H2);
//! * `printf.txt` — `snprintf` output for every conversion the engine and the parity dump use (H3).

use std::path::{Path, PathBuf};

use vigil_detection::hashorder::HashOrder;
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
