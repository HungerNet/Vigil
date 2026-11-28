//! The C++ standard library and MSVC semantics the engine's arithmetic depends on
//! (P3_PLAN.md §2.1, H4–H9). Every helper mirrors one C++ construct exactly, including the cases
//! (NaN, signed zeros, out-of-range casts) where Rust's own functions behave differently.

/// `std::max(a, b)`: `(a < b) ? b : a`.
#[inline]
pub fn max<T: PartialOrd>(a: T, b: T) -> T {
    if a < b { b } else { a }
}

/// `std::min(a, b)`: `(b < a) ? b : a`.
#[inline]
pub fn min<T: PartialOrd>(a: T, b: T) -> T {
    if b < a { b } else { a }
}

/// `std::clamp(v, lo, hi)` as MSVC implements it: `hi < v ? hi : v < lo ? lo : v` (NaN passes).
#[inline]
pub fn clamp<T: PartialOrd>(v: T, lo: T, hi: T) -> T {
    if hi < v {
        hi
    } else if v < lo {
        lo
    } else {
        v
    }
}

/// C `fmax`: the other argument when one is NaN.
#[inline]
pub fn fmax(a: f64, b: f64) -> f64 {
    if a.is_nan() {
        b
    } else if b.is_nan() || a > b {
        a
    } else {
        b
    }
}

/// `static_cast<int>(double)` on MSVC x64 (`cvttsd2si`): truncation toward zero; NaN and values
/// outside `int` give `INT_MIN` (C++ leaves them undefined; this is what the compiled code does).
#[inline]
pub fn to_int(d: f64) -> i32 {
    if d.is_nan() || d >= 2_147_483_648.0 || d <= -2_147_483_649.0 { i32::MIN } else { d as i32 }
}

/// `static_cast<int>(float)` on MSVC x64 (`cvttss2si`).
#[inline]
pub fn to_int_f32(f: f32) -> i32 {
    to_int(f64::from(f))
}

/// `static_cast<float>(double)`.
#[inline]
pub fn to_f32(d: f64) -> f32 {
    d as f32
}

/// `std::tolower` in the "C" locale, byte by byte.
pub fn lower(s: &[u8]) -> Vec<u8> {
    s.iter().map(u8::to_ascii_lowercase).collect()
}

/// `std::to_string(int)`.
pub fn int_text(v: i64) -> Vec<u8> {
    v.to_string().into_bytes()
}

/// `a + b` on byte strings.
pub fn cat(parts: &[&[u8]]) -> Vec<u8> {
    let mut out = Vec::with_capacity(parts.iter().map(|p| p.len()).sum());
    for p in parts {
        out.extend_from_slice(p);
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stl_semantics() {
        // std::max returns the first argument unless it is smaller: NaN first stays NaN.
        assert!(max(f64::NAN, 1.0).is_nan());
        assert_eq!(max(1.0, f64::NAN), 1.0);
        assert_eq!(min(f64::NAN, 1.0).to_bits(), f64::NAN.to_bits());
        assert!(clamp(f64::NAN, 0.0, 1.0).is_nan());
        assert_eq!(clamp(-0.0f64, 0.0, 1.0).to_bits(), (-0.0f64).to_bits());
        assert_eq!(fmax(f64::NAN, 2.0), 2.0);
        assert_eq!(to_int(f64::NAN), i32::MIN);
        assert_eq!(to_int(3e9), i32::MIN);
        assert_eq!(to_int(-2_147_483_648.9), i32::MIN);
        assert_eq!(to_int(-2.9), -2);
        assert_eq!(lower(b"AbC\xC4"), b"abc\xC4");
    }
}
