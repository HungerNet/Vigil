//! The wall clock and local time, injectable (P3B_PLAN.md §2.2, §3.7).
//!
//! The C++ engine reads the wall clock in four places, none of which influences a decision
//! (ARCHITECTURE §6, P3_PLAN H10): the journal file name (`Session::startEpochMs`), history
//! `lastSeen`, mark times (`epochNowMs`) and a profile's `savedAt`. It reads local time
//! (`localtime_s` + `strftime`) for profile ids and log lines. Here both are values a host passes
//! in: [`Clock`] (default: the system clock, as C++) and [`LocalTime`] (default: UTC — `std` has no
//! time zones, and the workspace allows neither `unsafe` nor dependencies; the host supplies its
//! local-time conversion when the engine moves into the DLL in P4). Tests and the differential
//! harness inject deterministic clocks.

use std::fmt;
use std::sync::Arc;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

/// `duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()` as a double
/// (truncated toward zero, negative before 1970).
pub fn system_epoch_ms() -> f64 {
    match SystemTime::now().duration_since(UNIX_EPOCH) {
        Ok(d) => d.as_millis() as f64,
        Err(e) => -(e.duration().as_millis() as f64),
    }
}

/// A source of epoch milliseconds.
#[derive(Clone)]
pub struct Clock(Arc<dyn Fn() -> f64 + Send + Sync>);

impl Clock {
    /// The system clock (what the C++ code reads).
    pub fn system() -> Clock {
        Clock(Arc::new(system_epoch_ms))
    }
    pub fn new(f: impl Fn() -> f64 + Send + Sync + 'static) -> Clock {
        Clock(Arc::new(f))
    }
    /// Always `ms`.
    pub fn fixed(ms: f64) -> Clock {
        Clock::new(move || ms)
    }
    /// `start`, `start + step`, `start + 2 step`, … one value per read.
    pub fn counter(start: f64, step: f64) -> Clock {
        let n = Arc::new(AtomicU64::new(0));
        Clock::new(move || start + step * n.fetch_add(1, Ordering::Relaxed) as f64)
    }
    pub fn now_ms(&self) -> f64 {
        (self.0)()
    }
}

impl Default for Clock {
    fn default() -> Self {
        Clock::system()
    }
}

impl fmt::Debug for Clock {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("Clock")
    }
}

/// A broken-down local time (`std::tm` with the year and month as written).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Civil {
    pub year: i64,
    pub month: u32,
    pub day: u32,
    pub hour: u32,
    pub minute: u32,
    pub second: u32,
}

/// The largest `time_t` MSVC's `localtime_s` accepts (probed: 3001-01-19 07:59:59 UTC; with the
/// default invalid-parameter handler anything outside `0..=` this terminates the process).
pub const LOCALTIME_MAX: i64 = 32_536_799_999;

/// UTC civil time of a `time_t` (proleptic Gregorian; days-from-civil inverse).
pub fn civil_utc(t: i64) -> Civil {
    let days = t.div_euclid(86_400);
    let secs = t.rem_euclid(86_400);
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z.rem_euclid(146_097);
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let day = (doy - (153 * mp + 2) / 5 + 1) as u32;
    let month = if mp < 10 { mp + 3 } else { mp - 9 } as u32;
    let year = yoe + era * 400 + i64::from(month <= 2);
    Civil {
        year,
        month,
        day,
        hour: (secs / 3600) as u32,
        minute: (secs % 3600 / 60) as u32,
        second: (secs % 60) as u32,
    }
}

/// `localtime_s`: the host's conversion of a `time_t` to local civil time; `None` where MSVC's
/// would fail (outside `0..=LOCALTIME_MAX`).
#[derive(Clone)]
pub struct LocalTime(Arc<dyn Fn(i64) -> Civil + Send + Sync>);

impl LocalTime {
    /// UTC (the default: no time zone data without the host).
    pub fn utc() -> LocalTime {
        LocalTime(Arc::new(civil_utc))
    }
    /// A fixed offset from UTC in seconds (tests; a host passes its own conversion).
    pub fn offset(seconds: i64) -> LocalTime {
        LocalTime(Arc::new(move |t| civil_utc(t.wrapping_add(seconds))))
    }
    pub fn new(f: impl Fn(i64) -> Civil + Send + Sync + 'static) -> LocalTime {
        LocalTime(Arc::new(f))
    }
    pub fn at(&self, t: i64) -> Option<Civil> {
        if (0..=LOCALTIME_MAX).contains(&t) { Some((self.0)(t)) } else { None }
    }
}

impl Default for LocalTime {
    fn default() -> Self {
        LocalTime::utc()
    }
}

impl fmt::Debug for LocalTime {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("LocalTime")
    }
}

/// `strftime(…, "%Y%m%d-%H%M%S", …)`.
pub fn stamp_compact(c: &Civil) -> String {
    format!("{:04}{:02}{:02}-{:02}{:02}{:02}", c.year, c.month, c.day, c.hour, c.minute, c.second)
}

/// `strftime(…, "%Y-%m-%d %H:%M:%S", …)`.
pub fn stamp_log(c: &Civil) -> String {
    format!("{:04}-{:02}-{:02} {:02}:{:02}:{:02}", c.year, c.month, c.day, c.hour, c.minute, c.second)
}

/// `static_cast<std::time_t>(double)` on MSVC x64 (`cvttsd2si`, 64-bit): truncation toward zero;
/// NaN and values outside `int64` give `INT64_MIN`.
pub fn to_time_t(d: f64) -> i64 {
    if d.is_nan() || d >= 9_223_372_036_854_775_808.0 || d < -9_223_372_036_854_775_808.0 { i64::MIN } else { d as i64 }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn civil() {
        assert_eq!(stamp_compact(&civil_utc(0)), "19700101-000000");
        assert_eq!(stamp_compact(&civil_utc(1_764_201_600)), "20251127-000000");
        assert_eq!(stamp_log(&civil_utc(951_782_400)), "2000-02-29 00:00:00");
        // The probe of snapshot.cpp's stampOf on this machine (UTC-6): 1764201600 → 20251126-180000.
        assert_eq!(stamp_compact(&LocalTime::offset(-6 * 3600).at(1_764_201_600).unwrap()), "20251126-180000");
        assert_eq!(stamp_compact(&LocalTime::offset(-6 * 3600).at(LOCALTIME_MAX).unwrap()), "30010119-015959");
        assert!(LocalTime::utc().at(-1).is_none() && LocalTime::utc().at(LOCALTIME_MAX + 1).is_none());
        assert_eq!(to_time_t(f64::INFINITY), i64::MIN);
        assert_eq!(to_time_t(-0.9), 0);
        let c = Clock::counter(10.0, 2.0);
        assert_eq!((c.now_ms(), c.now_ms(), c.now_ms()), (10.0, 12.0, 14.0));
    }
}
