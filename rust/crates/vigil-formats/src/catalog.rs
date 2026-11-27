//! Check and indicator ids in index order — the C++ `CheckCatalog`, which profiles are resolved
//! against.
//!
//! The catalog of an engine build is derived from its parameter declaration ([`Schema`]): every
//! check declares `checks.<id>.enabled` first, then its parameters in order, among them
//! `checks.<id>.ind.<indicator>.enabled` once per indicator in index order (check.cpp).

use crate::config::Schema;
use crate::json::Text;

#[derive(Clone, Debug, PartialEq, Default)]
pub struct CheckCatalog {
    pub checks: Vec<Text>,
    pub indicators: Vec<Vec<Text>>,
}

impl CheckCatalog {
    pub fn from_schema(schema: &Schema) -> CheckCatalog {
        let mut cat = CheckCatalog::default();
        for p in &schema.params {
            let Some(rest) = p.key.strip_prefix(b"checks.") else { continue };
            let Some(dot) = rest.iter().position(|&b| b == b'.') else { continue };
            let (id, tail) = (&rest[..dot], &rest[dot + 1..]);
            if tail == b"enabled" {
                cat.checks.push(id.to_vec());
                cat.indicators.push(Vec::new());
            } else if let Some(ind) = tail.strip_prefix(b"ind.").and_then(|t| t.strip_suffix(b".enabled"))
                && cat.checks.last().is_some_and(|c| c.as_slice() == id)
            {
                cat.indicators.last_mut().expect("a check precedes its indicators").push(ind.to_vec());
            }
        }
        cat
    }

    /// Index of a check id.
    pub fn check_index(&self, id: &[u8]) -> Option<usize> {
        self.checks.iter().position(|c| c == id)
    }

    /// Index of an indicator id within a check.
    pub fn indicator_index(&self, check: usize, id: &[u8]) -> Option<usize> {
        self.indicators.get(check)?.iter().position(|i| i == id)
    }
}
