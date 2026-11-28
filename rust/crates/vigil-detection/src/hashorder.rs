//! The iteration order of MSVC's `std::unordered_map` (P3_PLAN.md H1, H2).
//!
//! The C++ engine visits its entities in `std::unordered_map<int, Tracked>` order, which decides
//! the order records are created, evidence and detection ids are assigned, alerts are queued and
//! combat candidates are tie-broken. This container reproduces that order exactly:
//!
//! * hash: FNV-1a (64-bit) over the key's bytes (`std::hash<int>` hashes the 4 little-endian bytes,
//!   `std::hash<std::string>` the characters);
//! * one doubly linked list of all elements plus, per bucket, the first and last node of the
//!   bucket's contiguous range; bucket = hash & (buckets - 1);
//! * a new key goes in front of its bucket's range, or at the end of the list when the bucket is
//!   empty;
//! * before an insert that would make `size / buckets` exceed 1.0, the table grows (8 buckets at
//!   first, ×8 below 512 buckets, ×2 above) and is rehashed by walking the list: the first node of
//!   each bucket stays where it is, every later one is spliced in front of its bucket's range (so
//!   buckets appear in the order of their first element, each with its elements reversed);
//! * erase and clear never shrink the table.
//!
//! Verified against `std::unordered_map` itself: tests/fixtures/formats/hash-order.txt (random
//! insert/erase/clear scripts, int and string keys, through every growth step).

use std::collections::HashMap;
use std::hash::{BuildHasherDefault, Hash, Hasher};

/// A fast, deterministic, non-cryptographic hasher (the multiply-rotate scheme of rustc's
/// FxHasher) for lookup indexes. Only lookups go through it — iteration order never does — so it
/// cannot influence behavior; it replaces std's SipHash, which dominated entity lookups.
#[derive(Clone, Copy, Debug, Default)]
pub struct FastHasher(u64);

impl FastHasher {
    #[inline]
    fn add(&mut self, w: u64) {
        self.0 = (self.0.rotate_left(5) ^ w).wrapping_mul(0x517c_c1b7_2722_0a95);
    }
}

impl Hasher for FastHasher {
    fn finish(&self) -> u64 {
        self.0
    }
    fn write(&mut self, bytes: &[u8]) {
        for chunk in bytes.chunks(8) {
            let mut w = [0u8; 8];
            w[..chunk.len()].copy_from_slice(chunk);
            self.add(u64::from_le_bytes(w));
        }
    }
    fn write_u8(&mut self, i: u8) {
        self.add(u64::from(i));
    }
    fn write_u32(&mut self, i: u32) {
        self.add(u64::from(i));
    }
    fn write_i32(&mut self, i: i32) {
        self.add(u64::from(i as u32));
    }
    fn write_u64(&mut self, i: u64) {
        self.add(i);
    }
    fn write_usize(&mut self, i: usize) {
        self.add(i as u64);
    }
}

/// A `HashMap` for lookups only (never iterated where order matters).
pub type FastMap<K, V> = HashMap<K, V, BuildHasherDefault<FastHasher>>;

const END: usize = usize::MAX;
const MIN_BUCKETS: usize = 8;

/// The bytes MSVC's `std::hash` feeds to FNV-1a.
pub trait MsvcHash {
    fn msvc_hash(&self) -> u64;
}

/// FNV-1a, 64-bit (`_Fnv1a_append_bytes`).
pub fn fnv1a(bytes: &[u8]) -> u64 {
    let mut h: u64 = 14_695_981_039_346_656_037;
    for &b in bytes {
        h ^= u64::from(b);
        h = h.wrapping_mul(1_099_511_628_211);
    }
    h
}

impl MsvcHash for i32 {
    fn msvc_hash(&self) -> u64 {
        fnv1a(&self.to_le_bytes())
    }
}

impl MsvcHash for Vec<u8> {
    fn msvc_hash(&self) -> u64 {
        fnv1a(self)
    }
}

#[derive(Clone, Debug)]
struct Node<K, V> {
    key: K,
    value: V,
    hash: u64,
    prev: usize,
    next: usize,
}

/// A map that iterates exactly like MSVC's `std::unordered_map<K, V>`.
#[derive(Clone, Debug)]
pub struct HashOrder<K, V> {
    nodes: Vec<Option<Node<K, V>>>,
    free: Vec<usize>,
    first: usize,
    last: usize,
    /// (lo, hi) node of each bucket, `END` when empty.
    buckets: Vec<(usize, usize)>,
    len: usize,
    index: FastMap<K, usize>,
}

impl<K: MsvcHash + Hash + Eq + Clone, V> Default for HashOrder<K, V> {
    fn default() -> Self {
        Self::new()
    }
}

impl<K: MsvcHash + Hash + Eq + Clone, V> HashOrder<K, V> {
    pub fn new() -> Self {
        HashOrder {
            nodes: Vec::new(),
            free: Vec::new(),
            first: END,
            last: END,
            buckets: vec![(END, END); MIN_BUCKETS],
            len: 0,
            index: FastMap::default(),
        }
    }

    pub fn len(&self) -> usize {
        self.len
    }

    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    pub fn bucket_count(&self) -> usize {
        self.buckets.len()
    }

    fn node(&self, i: usize) -> &Node<K, V> {
        self.nodes[i].as_ref().expect("live node")
    }

    fn node_mut(&mut self, i: usize) -> &mut Node<K, V> {
        self.nodes[i].as_mut().expect("live node")
    }

    pub fn contains_key(&self, k: &K) -> bool {
        self.index.contains_key(k)
    }

    pub fn get(&self, k: &K) -> Option<&V> {
        self.index.get(k).map(|&i| &self.node(i).value)
    }

    pub fn get_mut(&mut self, k: &K) -> Option<&mut V> {
        let i = *self.index.get(k)?;
        Some(&mut self.node_mut(i).value)
    }

    /// `operator[]` / `try_emplace`: the value for `k`, inserted with `make` when absent.
    pub fn entry_or_insert_with(&mut self, k: K, make: impl FnOnce() -> V) -> &mut V {
        if let Some(&i) = self.index.get(&k) {
            return &mut self.node_mut(i).value;
        }
        let hash = k.msvc_hash();
        // _Check_rehash_required_1: max_load_factor (1.0f) < float(size + 1) / float(buckets).
        if 1.0f32 < (self.len + 1) as f32 / self.buckets.len() as f32 {
            let old = self.buckets.len();
            let required = MIN_BUCKETS.max(self.len + 1);
            let grown = if old >= required {
                old
            } else if old < 512 {
                old * 8
            } else {
                old * 2
            };
            self.rehash(grown.next_power_of_two());
        }
        // _Find_last: an empty bucket inserts before the list end; otherwise before the bucket's
        // first node (the key is not present).
        let b = (hash as usize) & (self.buckets.len() - 1);
        let (lo, hi) = self.buckets[b];
        let before = if lo == END { END } else { lo };
        let after = if before == END { self.last } else { self.node(before).prev };
        let node = Node { key: k.clone(), value: make(), hash, prev: after, next: before };
        let i = match self.free.pop() {
            Some(i) => {
                self.nodes[i] = Some(node);
                i
            }
            None => {
                self.nodes.push(Some(node));
                self.nodes.len() - 1
            }
        };
        if after == END {
            self.first = i;
        } else {
            self.node_mut(after).next = i;
        }
        if before == END {
            self.last = i;
        } else {
            self.node_mut(before).prev = i;
        }
        // _Insert_new_node_before: bucket bounds.
        if lo == END {
            self.buckets[b] = (i, i);
        } else if lo == before {
            self.buckets[b].0 = i;
        } else if hi == after {
            self.buckets[b].1 = i;
        }
        self.len += 1;
        self.index.insert(k, i);
        &mut self.node_mut(i).value
    }

    fn rehash(&mut self, buckets: usize) {
        self.buckets = vec![(END, END); buckets];
        let mask = buckets - 1;
        // Walk the list in order; each node joins the end of its bucket's range. The resulting
        // order: buckets in order of their first node, each in its previous relative order.
        let order: Vec<usize> = self.order_indices();
        let mut groups: Vec<Vec<usize>> = Vec::new();
        let mut group_of: FastMap<usize, usize> = FastMap::default();
        for &i in &order {
            let b = (self.node(i).hash as usize) & mask;
            let g = *group_of.entry(b).or_insert_with(|| {
                groups.push(Vec::new());
                groups.len() - 1
            });
            groups[g].push(i);
        }
        // Each node after the first of its bucket is spliced in front of the bucket's range, so
        // every group ends up reversed.
        for g in &mut groups {
            g.reverse();
        }
        let flat: Vec<usize> = groups.iter().flatten().copied().collect();
        self.first = flat.first().copied().unwrap_or(END);
        self.last = flat.last().copied().unwrap_or(END);
        for (n, &i) in flat.iter().enumerate() {
            let prev = if n == 0 { END } else { flat[n - 1] };
            let next = flat.get(n + 1).copied().unwrap_or(END);
            let node = self.node_mut(i);
            node.prev = prev;
            node.next = next;
        }
        for g in &groups {
            let b = (self.node(g[0]).hash as usize) & mask;
            self.buckets[b] = (g[0], *g.last().expect("non-empty group"));
        }
    }

    /// `erase(key)`.
    pub fn remove(&mut self, k: &K) -> Option<V> {
        let i = self.index.remove(k)?;
        let (hash, prev, next) = {
            let n = self.node(i);
            (n.hash, n.prev, n.next)
        };
        let b = (hash as usize) & (self.buckets.len() - 1);
        let (lo, hi) = self.buckets[b];
        // _Erase_bucket.
        if hi == i {
            if lo == i {
                self.buckets[b] = (END, END);
            } else {
                self.buckets[b].1 = prev;
            }
        } else if lo == i {
            self.buckets[b].0 = next;
        }
        if prev == END {
            self.first = next;
        } else {
            self.node_mut(prev).next = next;
        }
        if next == END {
            self.last = prev;
        } else {
            self.node_mut(next).prev = prev;
        }
        self.len -= 1;
        let node = self.nodes[i].take().expect("live node");
        self.free.push(i);
        Some(node.value)
    }

    /// `clear()`: empties the map; the bucket count stays.
    pub fn clear(&mut self) {
        self.nodes.clear();
        self.free.clear();
        self.index.clear();
        self.first = END;
        self.last = END;
        self.len = 0;
        self.buckets.fill((END, END));
    }

    fn order_indices(&self) -> Vec<usize> {
        let mut out = Vec::with_capacity(self.len);
        let mut i = self.first;
        while i != END {
            out.push(i);
            i = self.node(i).next;
        }
        out
    }

    /// The keys in iteration order.
    pub fn keys(&self) -> Vec<K> {
        self.order_indices().into_iter().map(|i| self.node(i).key.clone()).collect()
    }

    /// `(key, value)` in iteration order.
    pub fn iter(&self) -> impl Iterator<Item = (&K, &V)> {
        let mut i = self.first;
        std::iter::from_fn(move || {
            if i == END {
                return None;
            }
            let n = self.node(i);
            i = n.next;
            Some((&n.key, &n.value))
        })
    }

    /// Values in iteration order.
    pub fn values(&self) -> impl Iterator<Item = &V> {
        self.iter().map(|(_, v)| v)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn grows_like_msvc() {
        let mut m: HashOrder<i32, ()> = HashOrder::new();
        assert_eq!(m.bucket_count(), 8);
        for k in 0..8 {
            m.entry_or_insert_with(k, || ());
        }
        assert_eq!(m.bucket_count(), 8);
        m.entry_or_insert_with(8, || ());
        assert_eq!(m.bucket_count(), 64);
        for k in 9..65 {
            m.entry_or_insert_with(k, || ());
        }
        assert_eq!(m.bucket_count(), 512);
        for k in 65..513 {
            m.entry_or_insert_with(k, || ());
        }
        assert_eq!(m.bucket_count(), 1024);
        m.clear();
        assert_eq!(m.bucket_count(), 1024);
        assert!(m.is_empty());
    }
}
