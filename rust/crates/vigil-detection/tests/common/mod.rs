//! Shared by the mirrored C++ suites (detection, validation, records), the property tests and the
//! fixture tests (P3B_PLAN.md §3.1):
//!
//! * [`Expect`] / `expect!` — `EXPECT` of tests/test_framework.h: every expectation is counted, a
//!   failing one is reported with its message and the test continues; [`Expect::finish`] prints
//!   `expectations <n> <C++ test name>` (read by tools/ci/test_parity.py) and fails the test if any
//!   expectation failed;
//! * `rng` — the C++ tests' random number generators; `sim` — tests/sim.h; `scenarios` —
//!   tests/scenarios.h; `registry` — a stand-in for the C++ settings registry;
//! * [`TempFile`] / [`temp_dir`] — files under `%TEMP%` removed afterwards.
#![allow(dead_code, unused_macros)]

pub mod registry;
pub mod rng;
pub mod scenarios;
pub mod sim;

use std::cell::Cell;
use std::path::{Path, PathBuf};

const FNV_OFFSET: u64 = 1_469_598_103_934_665_603;

thread_local! {
    /// The frames the running test fed to engines (`frameDigest()` / `framesFed()` of
    /// tests/test_framework.h): FNV-1a over their trace lines. Each test runs on its own thread.
    static FRAMES: Cell<(u64, i64)> = const { Cell::new((FNV_OFFSET, 0)) };
}

/// `feedFrameDigest`: called by `World::frame` with the frame's trace line.
pub fn feed_frame(line: &[u8]) {
    FRAMES.with(|f| {
        let (mut h, n) = f.get();
        for &c in line.iter().chain(b"\n") {
            h ^= u64::from(c);
            h = h.wrapping_mul(1_099_511_628_211);
        }
        f.set((h, n + 1));
    });
}

/// `EXPECT(cond, fmt, ...)`.
macro_rules! expect {
    ($x:expr, $cond:expr, $($fmt:tt)+) => {
        $x.check($cond, || format!($($fmt)+))
    };
}

/// The expectations of one mirrored test.
#[derive(Debug)]
pub struct Expect {
    name: &'static str,
    count: usize,
    failures: Vec<String>,
}

impl Expect {
    /// `name` is the C++ test's name, exactly.
    pub fn new(name: &'static str) -> Expect {
        FRAMES.with(|f| f.set((FNV_OFFSET, 0)));
        Expect { name, count: 0, failures: Vec::new() }
    }

    #[track_caller]
    pub fn check(&mut self, cond: bool, message: impl FnOnce() -> String) {
        self.count += 1;
        if !cond {
            let at = std::panic::Location::caller();
            let line = format!("{}:{}  - {}", at.file(), at.line(), message());
            println!("    FAIL {line}");
            self.failures.push(line);
        }
    }

    pub fn count(&self) -> usize {
        self.count
    }
    pub fn failed(&self) -> usize {
        self.failures.len()
    }

    /// Prints the count and the frame digest for the test parity matrix and fails the test on any
    /// failed expectation.
    pub fn finish(self) {
        let (h, n) = FRAMES.with(Cell::get);
        println!("expectations {} {}", self.count, self.name);
        println!("frames {h:016x} {n} {}", self.name);
        if !self.failures.is_empty() {
            println!("failed {} {}", self.failures.len(), self.name);
        }
        assert!(
            self.failures.is_empty(),
            "{}: {} of {} expectations failed",
            self.name,
            self.failures.len(),
            self.count
        );
    }
}

pub fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../..")
}

pub fn temp_root() -> PathBuf {
    std::env::var_os("TEMP").map(PathBuf::from).unwrap_or_else(std::env::temp_dir)
}

/// `TempFile`: a path under %TEMP% (with the process id, so parallel test binaries never share
/// one), removed before use and when dropped.
pub struct TempFile {
    pub path: PathBuf,
}

impl TempFile {
    pub fn new(name: &str) -> TempFile {
        let path = temp_root().join(format!("vigil-rs-{}-{name}", std::process::id()));
        let _ = std::fs::remove_file(&path);
        TempFile { path }
    }
}

impl Drop for TempFile {
    fn drop(&mut self) {
        let _ = std::fs::remove_file(&self.path);
        let mut tmp = self.path.as_os_str().to_owned();
        tmp.push(".tmp");
        let _ = std::fs::remove_file(PathBuf::from(tmp));
    }
}

/// `tempDir`: an empty directory under %TEMP%.
pub fn temp_dir(name: &str) -> PathBuf {
    let p = temp_root().join(format!("vigil-rs-{}-{name}", std::process::id()));
    let _ = std::fs::remove_dir_all(&p);
    let _ = std::fs::create_dir_all(&p);
    p
}

pub fn read_all(p: &Path) -> Vec<u8> {
    std::fs::read(p).unwrap_or_default()
}

pub fn write_all(p: &Path, bytes: &[u8]) {
    std::fs::write(p, bytes).expect("write");
}

/// Line endings of committed text fixtures depend on the checkout; compare content.
pub fn without_cr(b: &[u8]) -> Vec<u8> {
    b.iter().copied().filter(|&c| c != b'\r').collect()
}
