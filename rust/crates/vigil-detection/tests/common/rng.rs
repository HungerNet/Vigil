//! The random number generators of the C++ test suites, as MSVC implements them: `std::mt19937`,
//! `std::uniform_real_distribution<double>` (through `std::generate_canonical`) and
//! `std::discrete_distribution<int>`. Proven value for value by `tests/fixtures/formats/random.txt`
//! (`foundations.rs`), so a mirrored test draws exactly the numbers its C++ original draws.

/// `std::mt19937`.
#[derive(Clone, Debug)]
pub struct Mt19937 {
    mt: [u32; 624],
    index: usize,
}

impl Mt19937 {
    pub fn new(seed: u32) -> Mt19937 {
        let mut mt = [0u32; 624];
        mt[0] = seed;
        for i in 1..624 {
            mt[i] = 1_812_433_253u32.wrapping_mul(mt[i - 1] ^ (mt[i - 1] >> 30)).wrapping_add(i as u32);
        }
        Mt19937 { mt, index: 624 }
    }

    fn twist(&mut self) {
        for i in 0..624 {
            let y = (self.mt[i] & 0x8000_0000) | (self.mt[(i + 1) % 624] & 0x7fff_ffff);
            let mut v = self.mt[(i + 397) % 624] ^ (y >> 1);
            if y & 1 != 0 {
                v ^= 0x9908_b0df;
            }
            self.mt[i] = v;
        }
        self.index = 0;
    }

    pub fn next_u32(&mut self) -> u32 {
        if self.index >= 624 {
            self.twist();
        }
        let mut y = self.mt[self.index];
        self.index += 1;
        y ^= y >> 11;
        y ^= (y << 7) & 0x9d2c_5680;
        y ^= (y << 15) & 0xefc6_0000;
        y ^= y >> 18;
        y
    }

    pub fn discard(&mut self, n: u64) {
        for _ in 0..n {
            self.next_u32();
        }
    }

    /// `std::generate_canonical<double, 53>` as current MSVC implements it (the P0952R2
    /// specification): the integer `g0 + g1 * 2^32` of two draws, truncated to its top 53 bits,
    /// over `2^53` — exact, and never 1.0. (The older "sum in double, divide by 2^64" reading
    /// rounds instead of truncating and matches only about a third of the fixture's values.)
    pub fn canonical(&mut self) -> f64 {
        let g0 = u64::from(self.next_u32());
        let g1 = u64::from(self.next_u32());
        ((g1 << 21) | (g0 >> 11)) as f64 / 9_007_199_254_740_992.0
    }
}

/// `std::uniform_real_distribution<double>(lo, hi)`.
#[derive(Clone, Copy, Debug)]
pub struct UniformReal {
    lo: f64,
    hi: f64,
}

impl UniformReal {
    pub fn new(lo: f64, hi: f64) -> UniformReal {
        UniformReal { lo, hi }
    }
    pub fn sample(&self, g: &mut Mt19937) -> f64 {
        g.canonical() * (self.hi - self.lo) + self.lo
    }
}

/// `std::discrete_distribution<int>(weights)`.
#[derive(Clone, Debug)]
pub struct Discrete {
    cdf: Vec<f64>,
}

impl Discrete {
    pub fn new(weights: &[f64]) -> Discrete {
        let mut p: Vec<f64> = if weights.is_empty() { vec![1.0] } else { weights.to_vec() };
        let sum: f64 = p.iter().fold(0.0, |a, b| a + b);
        if sum != 1.0 {
            for x in &mut p {
                *x /= sum;
            }
        }
        let mut cdf = Vec::with_capacity(p.len());
        cdf.push(p[0]);
        for i in 1..p.len() {
            let prev = cdf[i - 1];
            cdf.push(p[i] + prev);
        }
        Discrete { cdf }
    }
    // `std::lower_bound` compares `c < x`; the negation is that comparison, kept as written.
    #[allow(clippy::neg_cmp_op_on_partial_ord)]
    pub fn sample(&self, g: &mut Mt19937) -> i32 {
        let x = g.canonical();
        // lower_bound over all but the last cumulative value.
        let last = self.cdf.len() - 1;
        self.cdf[..last].iter().position(|&c| !(c < x)).unwrap_or(last) as i32
    }
}
