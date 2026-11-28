//! `std::sort` exactly as the MSVC STL implements it (P3B_PLAN.md §2.2).
//!
//! `std::sort` is not stable: above 32 elements the order of elements that compare equal depends
//! on the algorithm itself. Vigil sorts with it where ties are possible — `Marks::all()` (marks
//! updated in the same millisecond, files from older versions) and the saved-profile list
//! (`SnapshotStore::refresh`: an imported copy keeps its original save time) — so a Rust port that
//! sorted any other way would list the same records in a different order.
//!
//! This is MSVC's introsort, function for function: insertion sort for ranges of at most 32
//! elements; otherwise a "fat" three-way partition around a median guess (median of three, or
//! Tukey's ninther above 40 elements), recursing into the smaller side, with a heap sort once
//! `1.5 log2(n)` divisions are used up. Moves of a held value ("holes") are written as swaps, which
//! leave every element where the C++ moves leave it. Proven against `std::sort` itself by
//! `tests/fixtures/formats/sort-order.txt` (random ties, every size around the cut-offs, and
//! McIlroy adversarial inputs that reach the heap sort).

/// The insertion-sort cut-off (`_ISORT_MAX`).
const ISORT_MAX: usize = 32;

/// Which parts of the algorithm a sort used (for the fixture test's coverage claim).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct SortPaths {
    pub insertion: u64,
    pub partitions: u64,
    pub heap: u64,
}

/// `std::sort(v.begin(), v.end(), less)`.
pub fn sort_by<T>(v: &mut [T], less: impl FnMut(&T, &T) -> bool) {
    let mut paths = SortPaths::default();
    sort_by_traced(v, less, &mut paths);
}

/// [`sort_by`], also counting which paths of the algorithm ran.
pub fn sort_by_traced<T>(v: &mut [T], mut less: impl FnMut(&T, &T) -> bool, paths: &mut SortPaths) {
    let n = v.len();
    sort_unchecked(v, 0, n, n as isize, &mut less, paths);
}

fn sort_unchecked<T>(
    v: &mut [T],
    mut first: usize,
    mut last: usize,
    mut ideal: isize,
    less: &mut impl FnMut(&T, &T) -> bool,
    paths: &mut SortPaths,
) {
    loop {
        if last - first <= ISORT_MAX {
            paths.insertion += 1;
            insertion_sort(v, first, last, less);
            return;
        }
        if ideal <= 0 {
            paths.heap += 1;
            make_heap(v, first, last, less);
            sort_heap(v, first, last, less);
            return;
        }
        paths.partitions += 1;
        let (mid_first, mid_last) = partition_by_median_guess(v, first, last, less);
        ideal = (ideal >> 1) + (ideal >> 2);
        if mid_first - first < last - mid_last {
            sort_unchecked(v, first, mid_first, ideal, less, paths);
            first = mid_last;
        } else {
            sort_unchecked(v, mid_last, last, ideal, less, paths);
            last = mid_first;
        }
    }
}

/// `_Insertion_sort_unchecked`.
fn insertion_sort<T>(v: &mut [T], first: usize, last: usize, less: &mut impl FnMut(&T, &T) -> bool) {
    if first == last {
        return;
    }
    let mut mid = first + 1;
    while mid != last {
        if less(&v[mid], &v[first]) {
            // A new earliest element: everything before it moves up by one.
            v[first..=mid].rotate_right(1);
        } else {
            // Look for the insertion point after `first` (the C++ loop relies on `*first` not
            // being greater; the bound only matters for an inconsistent comparator).
            let mut hole = mid;
            while hole > first + 1 && less(&v[mid], &v[hole - 1]) {
                hole -= 1;
            }
            v[hole..=mid].rotate_right(1);
        }
        mid += 1;
    }
}

/// `_Med3_unchecked`: sorts the median of three elements to the middle.
fn med3<T>(v: &mut [T], first: usize, mid: usize, last: usize, less: &mut impl FnMut(&T, &T) -> bool) {
    if less(&v[mid], &v[first]) {
        v.swap(mid, first);
    }
    if less(&v[last], &v[mid]) {
        v.swap(last, mid);
        if less(&v[mid], &v[first]) {
            v.swap(mid, first);
        }
    }
}

/// `_Guess_median_unchecked` (`last` is inclusive here, as in the caller).
fn guess_median<T>(v: &mut [T], first: usize, mid: usize, last: usize, less: &mut impl FnMut(&T, &T) -> bool) {
    let count = last - first;
    if 40 < count {
        // Tukey's ninther.
        let step = (count + 1) >> 3;
        let two_step = step << 1;
        med3(v, first, first + step, first + two_step, less);
        med3(v, mid - step, mid, mid + step, less);
        med3(v, last - two_step, last - step, last, less);
        med3(v, first + step, mid, last - step, less);
    } else {
        med3(v, first, mid, last, less);
    }
}

/// `_Partition_by_median_guess_unchecked`: a three-way partition; returns the range of elements
/// equal to the pivot.
fn partition_by_median_guess<T>(
    v: &mut [T],
    first: usize,
    last: usize,
    less: &mut impl FnMut(&T, &T) -> bool,
) -> (usize, usize) {
    let mid = first + ((last - first) >> 1);
    guess_median(v, first, mid, last - 1, less);
    let mut pfirst = mid;
    let mut plast = pfirst + 1;

    while first < pfirst && !less(&v[pfirst - 1], &v[pfirst]) && !less(&v[pfirst], &v[pfirst - 1]) {
        pfirst -= 1;
    }
    while plast < last && !less(&v[plast], &v[pfirst]) && !less(&v[pfirst], &v[plast]) {
        plast += 1;
    }

    let mut gfirst = plast;
    let mut glast = pfirst;

    loop {
        while gfirst < last {
            if less(&v[pfirst], &v[gfirst]) {
                // greater than the pivot: stays
            } else if less(&v[gfirst], &v[pfirst]) {
                break;
            } else if plast != gfirst {
                v.swap(plast, gfirst);
                plast += 1;
            } else {
                plast += 1;
            }
            gfirst += 1;
        }

        while first < glast {
            let glast_prev = glast - 1;
            if less(&v[glast_prev], &v[pfirst]) {
                // smaller than the pivot: stays
            } else if less(&v[pfirst], &v[glast_prev]) {
                break;
            } else {
                pfirst -= 1;
                if pfirst != glast_prev {
                    v.swap(pfirst, glast_prev);
                }
            }
            glast -= 1;
        }

        if glast == first && gfirst == last {
            return (pfirst, plast);
        }

        if glast == first {
            // No room at the bottom: rotate the pivot upward.
            if plast != gfirst {
                v.swap(pfirst, plast);
            }
            plast += 1;
            v.swap(pfirst, gfirst);
            pfirst += 1;
            gfirst += 1;
        } else if gfirst == last {
            // No room at the top: rotate the pivot downward.
            glast -= 1;
            pfirst -= 1;
            if glast != pfirst {
                v.swap(glast, pfirst);
            }
            plast -= 1;
            v.swap(pfirst, plast);
        } else {
            glast -= 1;
            v.swap(gfirst, glast);
            gfirst += 1;
        }
    }
}

/// `_Push_heap_by_index`: the held value (at `hole`) percolates up towards `top`.
fn push_heap_by_index<T>(v: &mut [T], base: usize, mut hole: usize, top: usize, less: &mut impl FnMut(&T, &T) -> bool) {
    let mut idx = hole.wrapping_sub(1) >> 1;
    while top < hole && less(&v[base + idx], &v[base + hole]) {
        v.swap(base + hole, base + idx);
        hole = idx;
        idx = hole.wrapping_sub(1) >> 1;
    }
}

/// `_Pop_heap_hole_by_index`: the hole at `hole` moves down to the larger child until the bottom,
/// then the held value (kept at the hole) is pushed back up.
fn pop_heap_hole_by_index<T>(
    v: &mut [T],
    base: usize,
    mut hole: usize,
    bottom: usize,
    less: &mut impl FnMut(&T, &T) -> bool,
) {
    let top = hole;
    let mut idx = hole;
    let max_non_leaf = (bottom - 1) >> 1;
    while idx < max_non_leaf {
        idx = 2 * idx + 2;
        if less(&v[base + idx], &v[base + idx - 1]) {
            idx -= 1;
        }
        v.swap(base + hole, base + idx);
        hole = idx;
    }
    if idx == max_non_leaf && bottom % 2 == 0 {
        // Only child at the bottom.
        v.swap(base + hole, base + bottom - 1);
        hole = bottom - 1;
    }
    push_heap_by_index(v, base, hole, top, less);
}

/// `_Make_heap_unchecked`.
fn make_heap<T>(v: &mut [T], first: usize, last: usize, less: &mut impl FnMut(&T, &T) -> bool) {
    let bottom = last - first;
    let mut hole = bottom >> 1;
    while hole > 0 {
        hole -= 1;
        pop_heap_hole_by_index(v, first, hole, bottom, less);
    }
}

/// `_Sort_heap_unchecked`: repeated `_Pop_heap_unchecked`.
fn sort_heap<T>(v: &mut [T], first: usize, mut last: usize, less: &mut impl FnMut(&T, &T) -> bool) {
    while last - first >= 2 {
        last -= 1;
        // *dest = *first, and the old *last is the held value now at the hole (first).
        v.swap(first, last);
        pop_heap_hole_by_index(v, first, 0, last - first, less);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sorts() {
        let mut v: Vec<i32> = (0..500).map(|i| (i * 7919) % 501).collect();
        sort_by(&mut v, |a, b| a < b);
        assert!(v.windows(2).all(|w| w[0] <= w[1]));
        let mut e: Vec<i32> = Vec::new();
        sort_by(&mut e, |a, b| a < b);
        let mut one = vec![3];
        sort_by(&mut one, |a, b| a < b);
        assert_eq!(one, vec![3]);
    }
}
