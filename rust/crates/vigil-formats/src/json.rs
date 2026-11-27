//! A byte-exact mirror of `src/core/json.{h,cpp}`.
//!
//! Every Vigil file is written by the C++ JSON layer, so a Rust reader and writer must agree with it
//! byte for byte, not merely "parse JSON":
//!
//! * **Strings are bytes.** The C++ layer neither requires nor checks UTF-8 (chat lines arrive as
//!   JNI *modified* UTF-8; profiles clip strings at a byte count). Strings are [`Text`] (`Vec<u8>`).
//! * **Objects keep insertion order**; a repeated key overwrites the earlier value in place.
//! * **Numbers** are written as `%.0f` (integers below 1e15), else `%.15g`, else `%.17g` when
//!   `%.15g` does not read back exactly, and non-finite values as `0` ([`format_number`]).
//! * The parser takes every decision the C++ one takes (escapes, surrogate arithmetic with unsigned
//!   wrap-around, a nesting limit of 64, `strtod`-style decimal numbers including `inf`/`nan`).
//!   One deliberate difference: hexadecimal floats (`0x1p3`), which `strtod` accepts and no Vigil
//!   writer produces, are refused (P2_PLAN.md §2.1).

use std::fmt;

/// A JSON string: raw bytes, as in the C++ layer.
pub type Text = Vec<u8>;

/// A JSON value. Objects are insertion-ordered `(key, value)` pairs.
#[derive(Clone, Debug, PartialEq, Default)]
pub enum Value {
    #[default]
    Null,
    Bool(bool),
    Number(f64),
    String(Text),
    Array(Vec<Value>),
    Object(Vec<(Text, Value)>),
}

static NULL: Value = Value::Null;

impl Value {
    pub fn object() -> Value {
        Value::Object(Vec::new())
    }
    pub fn array() -> Value {
        Value::Array(Vec::new())
    }
    pub fn str(s: impl AsRef<[u8]>) -> Value {
        Value::String(s.as_ref().to_vec())
    }
    pub fn num(n: impl Into<f64>) -> Value {
        Value::Number(n.into())
    }

    pub fn is_null(&self) -> bool {
        matches!(self, Value::Null)
    }
    pub fn is_bool(&self) -> bool {
        matches!(self, Value::Bool(_))
    }
    pub fn is_number(&self) -> bool {
        matches!(self, Value::Number(_))
    }
    pub fn is_string(&self) -> bool {
        matches!(self, Value::String(_))
    }
    pub fn is_array(&self) -> bool {
        matches!(self, Value::Array(_))
    }
    pub fn is_object(&self) -> bool {
        matches!(self, Value::Object(_))
    }

    /// `Value::asBool(fallback)`.
    pub fn as_bool_or(&self, fallback: bool) -> bool {
        match self {
            Value::Bool(b) => *b,
            _ => fallback,
        }
    }
    pub fn as_bool(&self) -> bool {
        self.as_bool_or(false)
    }
    /// `Value::asNumber(fallback)`.
    pub fn as_number_or(&self, fallback: f64) -> f64 {
        match self {
            Value::Number(n) => *n,
            _ => fallback,
        }
    }
    pub fn as_number(&self) -> f64 {
        self.as_number_or(0.0)
    }
    /// `Value::asString()`: the bytes, or empty when not a string.
    pub fn as_bytes(&self) -> &[u8] {
        match self {
            Value::String(s) => s,
            _ => &[],
        }
    }
    pub fn as_text(&self) -> Text {
        self.as_bytes().to_vec()
    }

    /// `Value::size()`: element count of an array or object, else 0.
    pub fn size(&self) -> usize {
        match self {
            Value::Array(a) => a.len(),
            Value::Object(o) => o.len(),
            _ => 0,
        }
    }
    /// `Value::items()`: the elements of an array, else none.
    pub fn items(&self) -> &[Value] {
        match self {
            Value::Array(a) => a,
            _ => &[],
        }
    }
    /// `Value::members()`: the members of an object, else none.
    pub fn members(&self) -> &[(Text, Value)] {
        match self {
            Value::Object(o) => o,
            _ => &[],
        }
    }
    /// `Value::at(i)`: an array element, or Null.
    pub fn at(&self, i: usize) -> &Value {
        self.items().get(i).unwrap_or(&NULL)
    }
    /// `Value::find(key)`.
    pub fn find(&self, key: &str) -> Option<&Value> {
        self.members().iter().find(|(k, _)| k.as_slice() == key.as_bytes()).map(|(_, v)| v)
    }
    /// `Value::get(key)`: the member, or Null.
    pub fn get(&self, key: &str) -> &Value {
        self.find(key).unwrap_or(&NULL)
    }

    /// `operator[]` assignment: overwrites an existing key in place, else appends. A non-object
    /// becomes an object first (as in C++).
    pub fn set(&mut self, key: impl AsRef<[u8]>, v: Value) {
        if !self.is_object() {
            *self = Value::object();
        }
        if let Value::Object(members) = self {
            let key = key.as_ref();
            if let Some(slot) = members.iter_mut().find(|(k, _)| k.as_slice() == key) {
                slot.1 = v;
            } else {
                members.push((key.to_vec(), v));
            }
        }
    }
    /// `Value::push`: a non-array becomes an array first (as in C++).
    pub fn push(&mut self, v: Value) {
        if !self.is_array() {
            *self = Value::array();
        }
        if let Value::Array(items) = self {
            items.push(v);
        }
    }
}

impl From<bool> for Value {
    fn from(b: bool) -> Self {
        Value::Bool(b)
    }
}
impl From<f64> for Value {
    fn from(n: f64) -> Self {
        Value::Number(n)
    }
}
impl From<i32> for Value {
    fn from(n: i32) -> Self {
        Value::Number(f64::from(n))
    }
}
impl From<&str> for Value {
    fn from(s: &str) -> Self {
        Value::str(s)
    }
}
impl From<&[u8]> for Value {
    fn from(s: &[u8]) -> Self {
        Value::str(s)
    }
}
impl From<Text> for Value {
    fn from(s: Text) -> Self {
        Value::String(s)
    }
}
impl From<&Text> for Value {
    fn from(s: &Text) -> Self {
        Value::String(s.clone())
    }
}

/// A parse failure with the C++ parser's message.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ParseError(pub &'static str);

impl fmt::Display for ParseError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.0)
    }
}

impl std::error::Error for ParseError {}

/// `json::parse`: the whole input must be one value (surrounding whitespace allowed).
pub fn parse(text: &[u8]) -> Result<Value, ParseError> {
    let mut p = Parser { s: text, i: 0, err: None, depth: 0 };
    match p.value() {
        Some(v) => {
            p.ws();
            if p.i != text.len() { Err(ParseError("trailing characters")) } else { Ok(v) }
        }
        None => Err(ParseError(p.err.unwrap_or("parse error"))),
    }
}

struct Parser<'a> {
    s: &'a [u8],
    i: usize,
    err: Option<&'static str>,
    depth: i32,
}

impl Parser<'_> {
    fn ws(&mut self) {
        while self.i < self.s.len() && matches!(self.s[self.i], b' ' | b'\t' | b'\n' | b'\r') {
            self.i += 1;
        }
    }

    // The first failure wins, as in C++.
    fn fail<T>(&mut self, m: &'static str) -> Option<T> {
        if self.err.is_none() {
            self.err = Some(m);
        }
        None
    }

    fn hex4(&mut self) -> Option<u32> {
        if self.i + 4 > self.s.len() {
            return self.fail("truncated \\u escape");
        }
        let mut out: u32 = 0;
        for _ in 0..4 {
            let c = self.s[self.i];
            self.i += 1;
            out <<= 4;
            match c {
                b'0'..=b'9' => out |= u32::from(c - b'0'),
                b'a'..=b'f' => out |= u32::from(c - b'a' + 10),
                b'A'..=b'F' => out |= u32::from(c - b'A' + 10),
                _ => return self.fail("bad hex digit"),
            }
        }
        Some(out)
    }

    fn str(&mut self) -> Option<Text> {
        if self.s[self.i] != b'"' {
            return self.fail("expected string");
        }
        self.i += 1;
        let mut out = Vec::new();
        while self.i < self.s.len() {
            let c = self.s[self.i];
            self.i += 1;
            if c == b'"' {
                return Some(out);
            }
            if c != b'\\' {
                out.push(c);
                continue;
            }
            if self.i >= self.s.len() {
                return self.fail("bad escape");
            }
            let e = self.s[self.i];
            self.i += 1;
            match e {
                b'"' => out.push(b'"'),
                b'\\' => out.push(b'\\'),
                b'/' => out.push(b'/'),
                b'b' => out.push(0x08),
                b'f' => out.push(0x0C),
                b'n' => out.push(b'\n'),
                b'r' => out.push(b'\r'),
                b't' => out.push(b'\t'),
                b'u' => {
                    let mut cp = self.hex4()?;
                    if (0xD800..=0xDBFF).contains(&cp)
                        && self.i + 6 <= self.s.len()
                        && self.s[self.i] == b'\\'
                        && self.s[self.i + 1] == b'u'
                    {
                        self.i += 2;
                        let lo = self.hex4()?;
                        // Unsigned 32-bit arithmetic, unvalidated, exactly as in C++.
                        cp = 0x10000u32
                            .wrapping_add((cp.wrapping_sub(0xD800)) << 10)
                            .wrapping_add(lo.wrapping_sub(0xDC00));
                    }
                    append_utf8(&mut out, cp);
                }
                _ => return self.fail("bad escape"),
            }
        }
        self.fail("unterminated string")
    }

    fn value(&mut self) -> Option<Value> {
        self.depth += 1;
        if self.depth > 64 {
            return self.fail("nesting too deep");
        }
        self.ws();
        if self.i >= self.s.len() {
            return self.fail("unexpected end");
        }
        let c = self.s[self.i];
        let out = if c == b'{' {
            self.i += 1;
            let mut obj = Value::object();
            self.ws();
            if self.i < self.s.len() && self.s[self.i] == b'}' {
                self.i += 1;
            } else {
                loop {
                    self.ws();
                    if self.i >= self.s.len() {
                        return self.fail("expected key");
                    }
                    let Some(key) = self.str() else { return self.fail("expected key") };
                    self.ws();
                    if self.i >= self.s.len() || self.s[self.i] != b':' {
                        return self.fail("expected ':'");
                    }
                    self.i += 1;
                    let v = self.value()?;
                    obj.set(key, v);
                    self.ws();
                    if self.i < self.s.len() && self.s[self.i] == b',' {
                        self.i += 1;
                        continue;
                    }
                    if self.i < self.s.len() && self.s[self.i] == b'}' {
                        self.i += 1;
                        break;
                    }
                    return self.fail("expected ',' or '}'");
                }
            }
            obj
        } else if c == b'[' {
            self.i += 1;
            let mut arr = Vec::new();
            self.ws();
            if self.i < self.s.len() && self.s[self.i] == b']' {
                self.i += 1;
            } else {
                loop {
                    let v = self.value()?;
                    arr.push(v);
                    self.ws();
                    if self.i < self.s.len() && self.s[self.i] == b',' {
                        self.i += 1;
                        continue;
                    }
                    if self.i < self.s.len() && self.s[self.i] == b']' {
                        self.i += 1;
                        break;
                    }
                    return self.fail("expected ',' or ']'");
                }
            }
            Value::Array(arr)
        } else if c == b'"' {
            Value::String(self.str()?)
        } else if self.s[self.i..].starts_with(b"true") {
            self.i += 4;
            Value::Bool(true)
        } else if self.s[self.i..].starts_with(b"false") {
            self.i += 5;
            Value::Bool(false)
        } else if self.s[self.i..].starts_with(b"null") {
            self.i += 4;
            Value::Null
        } else {
            match strtod_prefix(&self.s[self.i..]) {
                Some((d, used)) => {
                    self.i += used;
                    Value::Number(d)
                }
                None => return self.fail("unexpected character"),
            }
        };
        self.depth -= 1;
        Some(out)
    }
}

// `appendUtf8` with the C++ byte truncation for out-of-range code points.
fn append_utf8(out: &mut Vec<u8>, cp: u32) {
    if cp < 0x80 {
        out.push(cp as u8);
    } else if cp < 0x800 {
        out.push((0xC0 | (cp >> 6)) as u8);
        out.push((0x80 | (cp & 0x3F)) as u8);
    } else if cp < 0x10000 {
        out.push((0xE0 | (cp >> 12)) as u8);
        out.push((0x80 | ((cp >> 6) & 0x3F)) as u8);
        out.push((0x80 | (cp & 0x3F)) as u8);
    } else {
        out.push((0xF0 | (cp >> 18)) as u8);
        out.push((0x80 | ((cp >> 12) & 0x3F)) as u8);
        out.push((0x80 | ((cp >> 6) & 0x3F)) as u8);
        out.push((0x80 | (cp & 0x3F)) as u8);
    }
}

fn is_c_space(c: u8) -> bool {
    matches!(c, b' ' | b'\t' | b'\n' | 0x0B | 0x0C | b'\r')
}

fn starts_with_ci(s: &[u8], word: &[u8]) -> bool {
    s.len() >= word.len() && s[..word.len()].eq_ignore_ascii_case(word)
}

/// The prefix `strtod` would consume: its value and length, or `None` when it consumes nothing.
/// Decimal numbers in C's grammar (`[+-]` · digits · `.` · digits · exponent), `inf`, `infinity`
/// and `nan[(…)]` in any case, after C whitespace. Hexadecimal floats are not supported (module
/// documentation): their `0` is read and the `x` is left over, so the document is refused.
fn strtod_prefix(s: &[u8]) -> Option<(f64, usize)> {
    let mut j = 0;
    while j < s.len() && is_c_space(s[j]) {
        j += 1;
    }
    let start = j;
    let negative = j < s.len() && s[j] == b'-';
    if j < s.len() && (s[j] == b'+' || s[j] == b'-') {
        j += 1;
    }
    let rest = &s[j..];
    if starts_with_ci(rest, b"inf") {
        let len = if starts_with_ci(rest, b"infinity") { 8 } else { 3 };
        let v = if negative { f64::NEG_INFINITY } else { f64::INFINITY };
        return Some((v, j + len));
    }
    if starts_with_ci(rest, b"nan") {
        let mut len = 3;
        if rest.get(3) == Some(&b'(') {
            let mut k = 4;
            while k < rest.len() && (rest[k].is_ascii_alphanumeric() || rest[k] == b'_') {
                k += 1;
            }
            if rest.get(k) == Some(&b')') {
                len = k + 1;
            }
        }
        let v = if negative { -f64::NAN } else { f64::NAN };
        return Some((v, j + len));
    }
    let mut k = j;
    let mut digits = 0;
    while k < s.len() && s[k].is_ascii_digit() {
        k += 1;
        digits += 1;
    }
    if k < s.len() && s[k] == b'.' {
        let mut m = k + 1;
        let mut frac = 0;
        while m < s.len() && s[m].is_ascii_digit() {
            m += 1;
            frac += 1;
        }
        if digits + frac > 0 {
            k = m;
            digits += frac;
        }
    }
    if digits == 0 {
        return None;
    }
    if k < s.len() && (s[k] == b'e' || s[k] == b'E') {
        let mut m = k + 1;
        if m < s.len() && (s[m] == b'+' || s[m] == b'-') {
            m += 1;
        }
        let exp_start = m;
        while m < s.len() && s[m].is_ascii_digit() {
            m += 1;
        }
        if m > exp_start {
            k = m;
        }
    }
    // The token is ASCII by construction and in Rust's float grammar (which is correctly rounded).
    let token = std::str::from_utf8(&s[start..k]).ok()?;
    let v: f64 = token.parse().ok()?;
    Some((v, k))
}

/// `json::write`: `indent < 0` is compact (`"k":v`), otherwise one member per line, indented by
/// `indent` spaces per level, with `": "` after keys.
pub fn write(v: &Value, indent: i32) -> Vec<u8> {
    let mut out = Vec::new();
    write_impl(v, &mut out, indent, 0);
    out
}

/// [`write`] as a `String` (lossy only if the document holds non-UTF-8 string bytes).
pub fn write_string(v: &Value, indent: i32) -> String {
    String::from_utf8_lossy(&write(v, indent)).into_owned()
}

fn newline(out: &mut Vec<u8>, indent: i32, level: i32) {
    if indent < 0 {
        return;
    }
    out.push(b'\n');
    out.extend(std::iter::repeat_n(b' ', (indent * level) as usize));
}

fn write_impl(v: &Value, out: &mut Vec<u8>, indent: i32, level: i32) {
    match v {
        Value::Null => out.extend_from_slice(b"null"),
        Value::Bool(b) => out.extend_from_slice(if *b { b"true" } else { b"false" }),
        Value::Number(d) => out.extend_from_slice(format_number(*d).as_bytes()),
        Value::String(s) => {
            out.push(b'"');
            escape_into(s, out);
            out.push(b'"');
        }
        Value::Array(items) => {
            out.push(b'[');
            for (k, item) in items.iter().enumerate() {
                newline(out, indent, level + 1);
                write_impl(item, out, indent, level + 1);
                if k + 1 < items.len() {
                    out.push(b',');
                }
            }
            if !items.is_empty() {
                newline(out, indent, level);
            }
            out.push(b']');
        }
        Value::Object(members) => {
            out.push(b'{');
            for (k, (key, value)) in members.iter().enumerate() {
                newline(out, indent, level + 1);
                out.push(b'"');
                escape_into(key, out);
                out.extend_from_slice(if indent < 0 { b"\":" } else { b"\": " });
                write_impl(value, out, indent, level + 1);
                if k + 1 < members.len() {
                    out.push(b',');
                }
            }
            if !members.is_empty() {
                newline(out, indent, level);
            }
            out.push(b'}');
        }
    }
}

/// `json::escape`: `"` `\` `\n` `\r` `\t` escaped, other control bytes as `\u00xx`, everything else
/// (including bytes ≥ 0x80) copied.
pub fn escape(s: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(s.len() + 8);
    escape_into(s, &mut out);
    out
}

fn escape_into(s: &[u8], out: &mut Vec<u8>) {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    for &c in s {
        match c {
            b'"' => out.extend_from_slice(b"\\\""),
            b'\\' => out.extend_from_slice(b"\\\\"),
            b'\n' => out.extend_from_slice(b"\\n"),
            b'\r' => out.extend_from_slice(b"\\r"),
            b'\t' => out.extend_from_slice(b"\\t"),
            c if c < 0x20 => {
                out.extend_from_slice(b"\\u00");
                out.push(HEX[usize::from(c >> 4)]);
                out.push(HEX[usize::from(c & 0x0F)]);
            }
            c => out.push(c),
        }
    }
}

/// The C++ writer's number text: `%.0f` for integral values below 1e15 in magnitude, else `%.15g`
/// if it reads back as the same double, else `%.17g`; non-finite values as `0`.
pub fn format_number(d: f64) -> String {
    if !d.is_finite() {
        return "0".to_string();
    }
    if d.floor() == d && d.abs() < 1e15 {
        return format!("{d:.0}");
    }
    let short = format_g(d, 15);
    if short.parse::<f64>().ok() == Some(d) { short } else { format_g(d, 17) }
}

/// C's `%.{precision}g` for a finite value: `precision` significant digits, scientific notation
/// when the decimal exponent is below -4 or at least `precision`, trailing zeros removed, and an
/// exponent of at least two digits (`1e+20`).
pub fn format_g(d: f64, precision: usize) -> String {
    let p = precision.max(1);
    // Correctly rounded digits and the exponent after rounding, as %e would produce them.
    let sci = format!("{:.*e}", p - 1, d);
    let (mantissa, exp) = sci.split_once('e').expect("{:e} always has an exponent");
    let exp: i32 = exp.parse().expect("{:e} exponent is an integer");
    let (negative, mantissa) = match mantissa.strip_prefix('-') {
        Some(m) => (true, m),
        None => (false, mantissa),
    };
    let digits: String = mantissa.chars().filter(char::is_ascii_digit).collect();
    let mut out = String::new();
    if negative {
        out.push('-');
    }
    if exp < -4 || exp >= p as i32 {
        let (first, rest) = digits.split_at(1);
        let rest = rest.trim_end_matches('0');
        out.push_str(first);
        if !rest.is_empty() {
            out.push('.');
            out.push_str(rest);
        }
        out.push('e');
        out.push(if exp < 0 { '-' } else { '+' });
        let e = exp.unsigned_abs();
        if e < 10 {
            out.push('0');
        }
        out.push_str(&e.to_string());
    } else if exp >= 0 {
        let point = (exp + 1) as usize;
        let (int_part, frac) = digits.split_at(point.min(digits.len()));
        out.push_str(int_part);
        let frac = frac.trim_end_matches('0');
        if !frac.is_empty() {
            out.push('.');
            out.push_str(frac);
        }
    } else {
        out.push_str("0.");
        out.extend(std::iter::repeat_n('0', (-exp - 1) as usize));
        out.push_str(digits.trim_end_matches('0'));
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn w(v: f64) -> String {
        format_number(v)
    }

    #[test]
    fn numbers_match_printf() {
        assert_eq!(w(0.4), "0.4");
        assert_eq!(w(64.03125), "64.03125");
        assert_eq!(w(1.0 / 3.0), "0.33333333333333331");
        assert_eq!(w(-1e-9), "-1e-09");
        assert_eq!(w(0.1 + 0.2), "0.30000000000000004");
        assert_eq!(w(1e15), "1e+15");
        assert_eq!(w(123456.789012345), "123456.789012345");
        assert_eq!(w(-0.0), "-0");
        assert_eq!(w(42.0), "42");
        assert_eq!(w(f64::NAN), "0");
        assert_eq!(w(f64::INFINITY), "0");
        assert_eq!(format_g(0.0001, 15), "0.0001");
        assert_eq!(format_g(0.00001, 15), "1e-05");
        assert_eq!(format_g(1e100, 15), "1e+100");
    }

    #[test]
    fn writer_layout() {
        let mut v = Value::object();
        v.set("a", Value::from(1));
        v.set("b", Value::array());
        let mut arr = Value::array();
        arr.push(Value::from(true));
        arr.push(Value::Null);
        v.set("c", arr);
        v.set("a", Value::from("x\ty\u{1}"));
        assert_eq!(write_string(&v, -1), r#"{"a":"x\ty\u0001","b":[],"c":[true,null]}"#);
        assert_eq!(write_string(&v, 1), "{\n \"a\": \"x\\ty\\u0001\",\n \"b\": [],\n \"c\": [\n  true,\n  null\n ]\n}");
    }

    #[test]
    fn parser_decisions() {
        assert_eq!(parse(b" [1, 2.5e0 , \"\\u00e9\"] ").unwrap().at(2).as_bytes(), "é".as_bytes());
        assert_eq!(parse(b"{\"a\":1,\"a\":2}").unwrap().members().len(), 1);
        assert_eq!(parse(b"{\"a\": [1, 2,}").unwrap_err().0, "unexpected character");
        assert_eq!(parse(b"\"\\ud83d\\ude00\"").unwrap().as_bytes(), "😀".as_bytes());
        assert_eq!(parse(b"\"\\ud800\"").unwrap().as_bytes(), &[0xED, 0xA0, 0x80]);
        assert!(parse(b"[+1, .5, 1., 01, inf, nan, -infinity]").is_ok());
        assert!(parse(b"0x10").is_err());
        assert!(parse(b"truex").is_err());
        let deep = "[".repeat(64) + &"]".repeat(64);
        assert!(parse(deep.as_bytes()).is_ok());
        let deeper = "[".repeat(65) + &"]".repeat(65);
        assert_eq!(parse(deeper.as_bytes()).unwrap_err().0, "nesting too deep");
    }
}
