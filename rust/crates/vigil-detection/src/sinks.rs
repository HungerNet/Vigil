//! The "detections" log channel (P3B_PLAN.md §3.5).
//!
//! The C++ engine writes one line per detection and one per debug line with
//! `log::writef("detections", …)`: `vsnprintf` into a 2 048-byte buffer (so a message is cut at
//! 2 047 bytes), then `log::write`, which appends `YYYY-MM-DD HH:MM:SS  <message>\n` (local time) to
//! `<log dir>/detections.log`, moving the file to `detections.log.old` first once it is larger than
//! 4 MiB. The engine here hands each message to an optional [`LogSink`]; [`LogFile`] writes it like
//! `core/log`, and [`LogLines`] keeps it in memory (tests, the differential harness).

use std::fmt;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

use crate::clock::{Clock, LocalTime, stamp_log, to_time_t};
use crate::evidence::Text;

/// `writef`'s buffer.
pub const WRITEF_BUFFER: usize = 2048;
/// `kMaxLogBytes`.
pub const MAX_LOG_BYTES: u64 = 4 * 1024 * 1024;

/// `log::writef`'s message: `vsnprintf` into its 2 048-byte buffer (at most 2 047 bytes).
pub fn writef_message(f: &str, args: &[crate::printf::Arg<'_>]) -> Text {
    crate::printf::format_into(WRITEF_BUFFER, f, args)
}

/// Where the engine's log lines go.
pub trait LogSink: Send {
    fn write(&mut self, channel: &str, message: &[u8]);
}

/// A shareable in-memory sink: every `(channel, message)` in order.
#[derive(Clone, Debug, Default)]
pub struct LogLines(pub Arc<Mutex<Vec<(String, Text)>>>);

impl LogLines {
    pub fn lines(&self) -> Vec<(String, Text)> {
        self.0.lock().map(|l| l.clone()).unwrap_or_default()
    }
}

impl LogSink for LogLines {
    fn write(&mut self, channel: &str, message: &[u8]) {
        if let Ok(mut l) = self.0.lock() {
            l.push((channel.to_string(), message.to_vec()));
        }
    }
}

/// `core/log`'s file half: `<dir>/<channel>.log`, one timestamped line per message, rotated to
/// `.old` above 4 MiB. Timestamps are the injected clock in the injected local time
/// (`std::time(nullptr)` + `localtime_s` in C++).
#[derive(Clone, Debug)]
pub struct LogFile {
    dir: PathBuf,
    clock: Clock,
    local: LocalTime,
}

impl LogFile {
    /// `log::init(dir)`: creates the directory.
    pub fn new(dir: &Path, clock: Clock, local: LocalTime) -> LogFile {
        let _ = std::fs::create_dir_all(dir);
        LogFile { dir: dir.to_path_buf(), clock, local }
    }

    fn timestamp(&self) -> String {
        let t = to_time_t((self.clock.now_ms() / 1000.0).floor());
        // time(nullptr) is always in range; a host clock outside it prints the epoch.
        let c = self.local.at(t).or_else(|| self.local.at(0)).unwrap_or_default();
        stamp_log(&c)
    }
}

impl LogSink for LogFile {
    fn write(&mut self, channel: &str, message: &[u8]) {
        let path = self.dir.join(format!("{channel}.log"));
        if std::fs::metadata(&path).is_ok_and(|m| m.len() > MAX_LOG_BYTES) {
            let mut old = path.as_os_str().to_owned();
            old.push(".old");
            let _ = std::fs::remove_file(&old);
            let _ = std::fs::rename(&path, &old);
        }
        use std::io::Write;
        let Ok(mut out) = std::fs::OpenOptions::new().create(true).append(true).open(&path) else { return };
        let mut line = self.timestamp().into_bytes();
        line.extend_from_slice(b"  ");
        line.extend_from_slice(message);
        line.push(b'\n');
        let _ = out.write_all(&line);
    }
}

/// The engine's optional sink (an `Option<Box<dyn LogSink>>` with a `Debug` the engine can derive).
#[derive(Default)]
pub struct LogSlot(pub Option<Box<dyn LogSink>>);

impl fmt::Debug for LogSlot {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(if self.0.is_some() { "LogSlot(set)" } else { "LogSlot(none)" })
    }
}
