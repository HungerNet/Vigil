#!/usr/bin/env python3
"""C++ <-> Rust engine parity (RUST_PLAN P3A, P3A_PLAN.md §2, docs/VALIDATION.md §9).

Runs the C++ engine (VigilParity) and the Rust engine (vigil-parity) on the same inputs and
requires byte-identical full-state results:

  corpus     the scenario corpus under every configuration VigilParity knows (416 x 7 runs):
             the whole VigilParity output, registry declaration and full default dumps included
  generated  N generated scenarios (tests/generator.h): one full-state digest and one summary
             line per scenario

On a difference it names the first differing run and writes the full state of that run from
both engines, so the first differing line is visible. It also reports what the generated set
exercised (indicators fired, filtered entities, stalls, pruning, binding, collisions...).

  python tools/ci/engine_parity.py --cpp build/Release/VigilParity.exe \
      --rust rust/target/release/vigil-parity.exe --work parity-work [--count 1000] [--seed S] [--skip-corpus]

Exit 0 identical, 1 different, 2 a tool failed.
"""

import argparse
import os
import shutil
import subprocess
import sys


def run(cmd):
    print("$ " + " ".join(str(c) for c in cmd), flush=True)
    r = subprocess.run([str(c) for c in cmd])
    if r.returncode != 0:
        print(f"command failed with exit code {r.returncode}")
        sys.exit(2)


def read(path):
    with open(path, "rb") as f:
        return f.read()


def first_difference(a, b):
    la, lb = a.split(b"\n"), b.split(b"\n")
    for i, (x, y) in enumerate(zip(la, lb)):
        if x != y:
            return i, x, y
    return min(len(la), len(lb)), b"<end>", b"<end>"


def explain(args, kind, plan_dir, cpp_out, rust_out, base_cmd):
    """Full dumps of the first differing run from both engines."""
    a, b = read(cpp_out), read(rust_out)
    line, x, y = first_difference(a, b)
    print(f"{kind}: outputs differ from line {line + 1}")
    print("  C++ : " + x.decode("utf-8", "replace")[:300])
    print("  Rust: " + y.decode("utf-8", "replace")[:300])
    words = x.decode("utf-8", "replace").split()
    if len(words) >= 3 and words[0] == "variant":
        variant, scenario = words[1], words[2]
        cpp_dump = os.path.join(args.work, f"{kind}-cpp-full.txt")
        rust_dump = os.path.join(args.work, f"{kind}-rust-full.txt")
        run(base_cmd + ["--full", variant, scenario, cpp_dump])
        run([args.rust, plan_dir, os.path.join(args.work, f"{kind}-rust-again.txt"), "--full", variant, scenario, rust_dump])
        l2, x2, y2 = first_difference(read(cpp_dump), read(rust_dump))
        print(f"  first difference in the full state of {variant} / {scenario}, line {l2 + 1}:")
        print("    C++ : " + x2.decode("utf-8", "replace")[:300])
        print("    Rust: " + y2.decode("utf-8", "replace")[:300])
        print(f"  full dumps: {cpp_dump} {rust_dump}")


def coverage(expected_path):
    """What the generated scenarios exercised, from the summary lines (identical in both engines)."""
    scen = 0
    counts = {k: 0 for k in ["evidence", "detections", "combat", "stalls", "filtered", "pruned", "suffixed", "exempt"]}
    multi_scene = 0
    indicators = None
    totals = None
    for line in read(expected_path).decode("utf-8", "replace").splitlines():
        if not line.startswith("summary "):
            continue
        scen += 1
        fields = dict(f.split("=", 1) for f in line.split()[2:] if "=" in f)
        for k in counts:
            if int(fields.get(k, "0")) > 0:
                counts[k] += 1
        if int(fields.get("scene", "0")) > 1:
            multi_scene += 1
        ind = [[int(n) for n in c.split(",")] if c else [] for c in fields["ind"].split(";")]
        if indicators is None:
            indicators = [[0] * len(c) for c in ind]
            totals = [[0] * len(c) for c in ind]
        for ci, c in enumerate(ind):
            for ii, n in enumerate(c):
                totals[ci][ii] += n
                if n > 0:
                    indicators[ci][ii] += 1
    print(f"coverage of {scen} generated scenarios (scenarios in which it happened):")
    for k, v in counts.items():
        print(f"  {k:<11} {v}")
    print(f"  >1 scene    {multi_scene}")
    fired = sum(1 for c in indicators or [] for n in c if n > 0)
    every = sum(len(c) for c in indicators or [])
    print(f"  indicators  {fired} of {every} fired; per check (scenarios / observations):")
    for ci, c in enumerate(indicators or []):
        print("    check %2d: %s" % (ci, "  ".join(f"{n}/{t}" for n, t in zip(c, totals[ci]))))
    return fired, every


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--cpp", required=True, help="VigilParity executable")
    p.add_argument("--rust", required=True, help="vigil-parity executable")
    p.add_argument("--work", required=True, help="working directory (traces are large)")
    p.add_argument("--count", type=int, default=1000, help="generated scenarios (RUST_PLAN P3: at least 1000)")
    p.add_argument("--seed", type=int, default=20261127)
    p.add_argument("--skip-corpus", action="store_true")
    args = p.parse_args()
    os.makedirs(args.work, exist_ok=True)
    problems = 0

    if not args.skip_corpus:
        plan = os.path.join(args.work, "corpus")
        cpp_out = os.path.join(args.work, "corpus-cpp.txt")
        rust_out = os.path.join(args.work, "corpus-rust.txt")
        base = [args.cpp, cpp_out, "--export", plan]
        run(base)
        run([args.rust, plan, rust_out])
        if read(cpp_out) == read(rust_out):
            runs = sum(1 for l in read(cpp_out).splitlines() if l.startswith(b"variant "))
            print(f"corpus: IDENTICAL ({runs} full-state digests, registry declaration and default dumps)")
        else:
            problems += 1
            explain(args, "corpus", plan, cpp_out, rust_out, base)

    plan = os.path.join(args.work, "generated")
    base = [args.cpp, "--generate", plan, "--count", str(args.count), "--seed", str(args.seed)]
    run(base)
    expected = os.path.join(plan, "expected.txt")
    # The generated set must be a function of its seed (a generator reading undefined memory would
    # hand the two engines different worlds on different runs).
    again = os.path.join(args.work, "generated-again")
    run([args.cpp, "--generate", again, "--count", str(args.count), "--seed", str(args.seed)])
    if read(expected) != read(os.path.join(again, "expected.txt")):
        problems += 1
        print("generated: NOT DETERMINISTIC - two generations of the same seed differ (generator bug)")
    else:
        print("generated: deterministic (two generations of the same seed are identical)")
    shutil.rmtree(again, ignore_errors=True)
    rust_out = os.path.join(args.work, "generated-rust.txt")
    run([args.rust, plan, rust_out])
    if read(expected) == read(rust_out):
        print(f"generated: IDENTICAL ({args.count} scenarios, seed {args.seed})")
    else:
        problems += 1
        explain(args, "generated", plan, expected, rust_out, base)
    coverage(expected)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
