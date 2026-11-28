//! A byte-exact mirror of the `printf` subset the engine and the parity dump use
//! (P3_PLAN.md H3): `vsnprintf` as the MSVC universal CRT implements it.
//!
//! Conversions `d i u x X f F e E g G s c %`; flags `- + space 0 #`; width; precision; the length
//! modifiers `hh h l ll z j t L` are accepted and ignored (the caller passes the value already
//! converted to the type C would read). Non-finite numbers print as the UCRT does: `inf`, `-inf`,
//! `nan`, `-nan(ind)` (the default NaN of x64 arithmetic). Checked against a C++-generated table
//! (tests/fixtures/formats/printf.txt).

use vigil_formats::json::format_g;

/// One `printf` argument.
#[derive(Clone, Copy, Debug)]
pub enum Arg<'a> {
    /// `double` (a C `float` argument is promoted by the caller with `f64::from`).
    F(f64),
    /// Any signed integer, already converted to the C type the conversion reads.
    I(i64),
    /// Any unsigned integer.
    U(u64),
    /// `%s`.
    S(&'a [u8]),
}

impl From<f64> for Arg<'_> {
    fn from(v: f64) -> Self {
        Arg::F(v)
    }
}
impl From<f32> for Arg<'_> {
    fn from(v: f32) -> Self {
        Arg::F(f64::from(v))
    }
}
impl From<i32> for Arg<'_> {
    fn from(v: i32) -> Self {
        Arg::I(i64::from(v))
    }
}
impl From<i64> for Arg<'_> {
    fn from(v: i64) -> Self {
        Arg::I(v)
    }
}
impl From<u64> for Arg<'_> {
    fn from(v: u64) -> Self {
        Arg::U(v)
    }
}
impl From<usize> for Arg<'_> {
    fn from(v: usize) -> Self {
        Arg::U(v as u64)
    }
}
impl From<bool> for Arg<'_> {
    /// A `bool` passed to `%d` is promoted to `int`.
    fn from(v: bool) -> Self {
        Arg::I(i64::from(v))
    }
}
impl<'a> From<&'a [u8]> for Arg<'a> {
    fn from(v: &'a [u8]) -> Self {
        Arg::S(v)
    }
}
impl<'a> From<&'a Vec<u8>> for Arg<'a> {
    fn from(v: &'a Vec<u8>) -> Self {
        Arg::S(v)
    }
}
impl<'a> From<&'a str> for Arg<'a> {
    fn from(v: &'a str) -> Self {
        Arg::S(v.as_bytes())
    }
}

#[derive(Default)]
struct Spec {
    left: bool,
    plus: bool,
    space: bool,
    zero: bool,
    alt: bool,
    width: usize,
    precision: Option<usize>,
}

/// The full output of `printf(format, args…)`.
pub fn format(fmt: &str, args: &[Arg<'_>]) -> Vec<u8> {
    let f = fmt.as_bytes();
    let mut out = Vec::with_capacity(f.len() + 16);
    let mut next = 0usize;
    let mut i = 0usize;
    while i < f.len() {
        let c = f[i];
        if c != b'%' {
            out.push(c);
            i += 1;
            continue;
        }
        i += 1;
        let mut s = Spec::default();
        while i < f.len() {
            match f[i] {
                b'-' => s.left = true,
                b'+' => s.plus = true,
                b' ' => s.space = true,
                b'0' => s.zero = true,
                b'#' => s.alt = true,
                _ => break,
            }
            i += 1;
        }
        while i < f.len() && f[i].is_ascii_digit() {
            s.width = s.width * 10 + usize::from(f[i] - b'0');
            i += 1;
        }
        if i < f.len() && f[i] == b'.' {
            i += 1;
            let mut p = 0usize;
            while i < f.len() && f[i].is_ascii_digit() {
                p = p * 10 + usize::from(f[i] - b'0');
                i += 1;
            }
            s.precision = Some(p);
        }
        while i < f.len() && matches!(f[i], b'h' | b'l' | b'z' | b'j' | b't' | b'L') {
            i += 1;
        }
        let Some(&conv) = f.get(i) else { break };
        i += 1;
        if conv == b'%' {
            out.push(b'%');
            continue;
        }
        let arg = args.get(next).copied().unwrap_or(Arg::I(0));
        next += 1;
        match conv {
            b'd' | b'i' => {
                let v = int_of(arg);
                let (neg, mag) = (v < 0, v.unsigned_abs());
                integer(&mut out, &s, neg, mag.to_string().into_bytes(), true);
            }
            b'u' => integer(&mut out, &s, false, uint_of(arg).to_string().into_bytes(), false),
            b'x' => integer(&mut out, &s, false, format!("{:x}", uint_of(arg)).into_bytes(), false),
            b'X' => integer(&mut out, &s, false, format!("{:X}", uint_of(arg)).into_bytes(), false),
            b'f' | b'F' | b'e' | b'E' | b'g' | b'G' => floating(&mut out, &s, conv, float_of(arg)),
            b's' => {
                let text: &[u8] = match arg {
                    // A C string ends at its first NUL (`std::string::c_str()`).
                    Arg::S(t) => t.iter().position(|&b| b == 0).map_or(t, |n| &t[..n]),
                    _ => b"",
                };
                let text = match s.precision {
                    Some(p) => &text[..text.len().min(p)],
                    None => text,
                };
                pad(&mut out, &s, b"", text, false);
            }
            b'c' => {
                let ch = [int_of(arg) as u8];
                pad(&mut out, &s, b"", &ch, false);
            }
            other => {
                // Not a conversion this mirror knows; C would print nothing useful either.
                out.push(b'%');
                out.push(other);
            }
        }
    }
    out
}

/// `vsnprintf` into a buffer of `capacity` bytes: at most `capacity - 1` bytes of output.
pub fn format_into(capacity: usize, fmt: &str, args: &[Arg<'_>]) -> Vec<u8> {
    let mut out = format(fmt, args);
    out.truncate(capacity.saturating_sub(1));
    out
}

fn int_of(a: Arg<'_>) -> i64 {
    match a {
        Arg::I(v) => v,
        Arg::U(v) => v as i64,
        Arg::F(v) => v as i64,
        Arg::S(_) => 0,
    }
}

fn uint_of(a: Arg<'_>) -> u64 {
    match a {
        Arg::I(v) => v as u64,
        Arg::U(v) => v,
        Arg::F(v) => v as u64,
        Arg::S(_) => 0,
    }
}

fn float_of(a: Arg<'_>) -> f64 {
    match a {
        Arg::F(v) => v,
        Arg::I(v) => v as f64,
        Arg::U(v) => v as f64,
        Arg::S(_) => 0.0,
    }
}

fn sign_prefix(s: &Spec, negative: bool) -> &'static [u8] {
    if negative {
        b"-"
    } else if s.plus {
        b"+"
    } else if s.space {
        b" "
    } else {
        b""
    }
}

/// Writes `prefix` + `body` padded to the field width (zeros go between the sign and the digits).
fn pad(out: &mut Vec<u8>, s: &Spec, prefix: &[u8], body: &[u8], zero_ok: bool) {
    let len = prefix.len() + body.len();
    let fill = s.width.saturating_sub(len);
    if s.left {
        out.extend_from_slice(prefix);
        out.extend_from_slice(body);
        out.extend(std::iter::repeat_n(b' ', fill));
    } else if s.zero && zero_ok {
        out.extend_from_slice(prefix);
        out.extend(std::iter::repeat_n(b'0', fill));
        out.extend_from_slice(body);
    } else {
        out.extend(std::iter::repeat_n(b' ', fill));
        out.extend_from_slice(prefix);
        out.extend_from_slice(body);
    }
}

fn integer(out: &mut Vec<u8>, s: &Spec, negative: bool, mut digits: Vec<u8>, signed: bool) {
    // A precision is the minimum number of digits (and disables the 0 flag); ".0" with 0 prints nothing.
    if let Some(p) = s.precision {
        if p == 0 && digits == b"0" {
            digits.clear();
        }
        if digits.len() < p {
            let mut d = vec![b'0'; p - digits.len()];
            d.extend_from_slice(&digits);
            digits = d;
        }
    }
    let prefix = if signed { sign_prefix(s, negative) } else { b"" };
    pad(out, s, prefix, &digits, s.precision.is_none());
}

fn floating(out: &mut Vec<u8>, s: &Spec, conv: u8, v: f64) {
    let negative = v.is_sign_negative();
    if !v.is_finite() {
        let upper = conv.is_ascii_uppercase();
        let body: &[u8] = if v.is_infinite() {
            if upper { b"INF" } else { b"inf" }
        } else if v.to_bits() == 0xFFF8_0000_0000_0000 {
            // The x64 default NaN (sign set, quiet, no payload): "-nan(ind)".
            if upper { b"NAN(IND)" } else { b"nan(ind)" }
        } else if v.to_bits() & 0x0008_0000_0000_0000 == 0 {
            if upper { b"NAN(SNAN)" } else { b"nan(snan)" }
        } else if upper {
            b"NAN"
        } else {
            b"nan"
        };
        pad(out, s, sign_prefix(s, negative), body, false);
        return;
    }
    let a = v.abs();
    let p = s.precision.unwrap_or(6);
    let mut body = match conv {
        b'f' | b'F' => format!("{a:.p$}"),
        b'e' | b'E' => exponent_form(a, p),
        // `%#g` (trailing zeros kept) is not used by Vigil.
        _ => format_g(a, p),
    }
    .into_bytes();
    if s.alt && matches!(conv, b'f' | b'F') && p == 0 {
        body.push(b'.');
    }
    if conv.is_ascii_uppercase() {
        body.make_ascii_uppercase();
    }
    pad(out, s, sign_prefix(s, negative), &body, true);
}

/// `%.{p}e` of a non-negative finite value: `d.ddde+XX`.
fn exponent_form(a: f64, p: usize) -> String {
    let sci = format!("{a:.p$e}");
    let (mantissa, exp) = sci.split_once('e').expect("{:e} has an exponent");
    let exp: i32 = exp.parse().expect("integral exponent");
    format!("{mantissa}e{}{:02}", if exp < 0 { '-' } else { '+' }, exp.unsigned_abs())
}

/// `printf`-style formatting into a `Vec<u8>`: `cfmt!("%.2f blocks", x)` (unbounded).
#[macro_export]
macro_rules! cfmt {
    ($fmt:expr) => { $crate::printf::format($fmt, &[]) };
    ($fmt:expr, $($arg:expr),+ $(,)?) => {
        $crate::printf::format($fmt, &[$($crate::printf::Arg::from($arg)),+])
    };
}

/// The engine's `fmt()` (check.cpp): `vsnprintf` into a 512-byte buffer.
#[macro_export]
macro_rules! fmt {
    ($fmt:expr) => { $crate::printf::format_into(512, $fmt, &[]) };
    ($fmt:expr, $($arg:expr),+ $(,)?) => {
        $crate::printf::format_into(512, $fmt, &[$($crate::printf::Arg::from($arg)),+])
    };
}

#[cfg(test)]
mod tests {
    use super::*;

    fn p(f: &str, args: &[Arg<'_>]) -> String {
        String::from_utf8(format(f, args)).unwrap()
    }

    #[test]
    fn common_conversions() {
        assert_eq!(p("%.2f", &[Arg::F(3.30751)]), "3.31");
        assert_eq!(p("%+.2f", &[Arg::F(0.25)]), "+0.25");
        assert_eq!(p("%.0f%%", &[Arg::F(70.5)]), "70%");
        assert_eq!(p("%02x", &[Arg::U(5)]), "05");
        assert_eq!(p("%016llx", &[Arg::U(0xABC)]), "0000000000000abc");
        assert_eq!(p("%-6s|", &[Arg::S(b"ab")]), "ab    |");
        assert_eq!(p("%5.1f", &[Arg::F(-2.25)]), " -2.2");
        assert_eq!(p("%.9g", &[Arg::F(f64::from(0.1f32))]), "0.100000001");
        assert_eq!(p("%d tick%s", &[Arg::I(1), Arg::S(b"")]), "1 tick");
        assert_eq!(p("%.1f", &[Arg::F(f64::INFINITY)]), "inf");
        assert_eq!(p("%.1f", &[Arg::F(-f64::NAN)]), "-nan(ind)");
        assert_eq!(format_into(4, "%s", &[Arg::S(b"abcdef")]), b"abc");
    }
}
