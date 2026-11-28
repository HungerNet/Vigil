#!/usr/bin/env python3
"""Test parity matrix: every C++ engine test has a Rust counterpart (RUST_PLAN P3B, P3B_PLAN.md §3.2).

For the three engine-level C++ suites (detection, validation, records) it runs the C++ executable
with --counts and the Rust suite of the same name (rust/crates/vigil-detection/tests/<suite>.rs)
with --nocapture, and requires for every C++ test:

  * a Rust test that prints the same name (`Expect::new("<C++ name>")`) and passes;
  * the same number of expectations — minus the host-only expectations listed in
    tests/test-parity.json for tests that exercise the C++ settings registry;
  * the same frames: the digest of every frame the test fed to engines (tests/sim.h `World::frame`,
    tests/common/sim.rs) is identical, so the Rust test drove the engine with exactly the input
    the C++ test did.

  python tools/ci/test_parity.py --cpp-dir build/Release --rust-dir rust [--markdown matrix.md]

Exit 0 complete and green, 1 a difference, 2 a tool failed.
"""

import argparse
import json
import os
import re
import subprocess
import sys

# A count line, at the start of a line or (cargo --nocapture) after the harness's "test <fn> ... ".
LINE = re.compile(r"(?:^|\.\.\. )(expectations|frames|failed) (.*)$")


def parse(output):
    """{name: {"expectations": n, "frames": (digest, count), "failed": n}} from --counts output."""
    tests = {}
    for raw in output.splitlines():
        line = raw.strip("\r")
        m = LINE.search(line)
        if not m:
            continue
        kind, rest = m.group(1), m.group(2)
        if kind == "frames":
            digest, count, name = rest.split(" ", 2)
            tests.setdefault(name, {})["frames"] = (digest, int(count))
        else:
            n, name = rest.split(" ", 1)
            tests.setdefault(name, {})[kind] = int(n)
    return tests


def run(cmd, cwd=None, env=None):
    r = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return r.returncode, r.stdout.decode("utf-8", "replace")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--cpp-dir", required=True, help="directory with detection_tests.exe, validation_tests.exe, records_tests.exe")
    p.add_argument("--rust-dir", required=True, help="the Cargo workspace (rust/)")
    p.add_argument("--config", default=os.path.join(os.path.dirname(__file__), "..", "..", "tests", "test-parity.json"))
    p.add_argument("--cargo", default="cargo")
    p.add_argument("--markdown", help="also write the matrix as a Markdown table")
    args = p.parse_args()

    with open(args.config, encoding="utf-8") as f:
        config = json.load(f)
    special = {(t["suite"], t["name"]): t for t in config["tests"]}
    problems, rows = [], []
    totals = {"cpp_tests": 0, "rust_tests": 0, "cpp_exp": 0, "rust_exp": 0, "host_exp": 0}

    for suite, names in config["suites"].items():
        exe = os.path.join(args.cpp_dir, names["cpp"] + (".exe" if os.name == "nt" else ""))
        code, out = run([exe, "--counts"])
        if code not in (0, 1):
            print(f"{exe} failed to run (exit {code})")
            return 2
        cpp = parse(out)
        if code != 0:
            problems.append(f"{suite}: the C++ suite itself fails")
        code, out = run([args.cargo, "test", "--release", "-p", "vigil-detection", "--test", names["rust"], "--",
                         "--nocapture", "--test-threads", "1"], cwd=args.rust_dir)
        rust = parse(out)
        if code != 0:
            problems.append(f"{suite}: cargo test --test {names['rust']} failed (exit {code})")
            if not rust:
                print(out[-4000:])
        for name, c in cpp.items():
            totals["cpp_tests"] += 1
            totals["cpp_exp"] += c.get("expectations", 0)
            s = special.get((suite, name), {"class": "full", "host": 0})
            host = int(s.get("host", 0))
            totals["host_exp"] += host
            r = rust.get(name)
            status = "ok"
            if s["class"] == "host":
                if r is not None:
                    problems.append(f"{suite} / {name}: listed as host-only but a Rust test exists")
                    status = "unexpected Rust test"
                rows.append((suite, name, s["class"], c.get("expectations", 0), "-", host, "-", status))
                continue
            if r is None:
                problems.append(f"{suite} / {name}: no Rust counterpart")
                rows.append((suite, name, s["class"], c.get("expectations", 0), "missing", host, "-", "MISSING"))
                continue
            totals["rust_tests"] += 1
            totals["rust_exp"] += r.get("expectations", 0)
            if r.get("failed"):
                problems.append(f"{suite} / {name}: {r['failed']} Rust expectations failed")
                status = "FAILED"
            if c.get("failed"):
                problems.append(f"{suite} / {name}: the C++ test fails")
                status = "C++ FAILED"
            if r.get("expectations", -1) + host != c.get("expectations", 0):
                problems.append(f"{suite} / {name}: {r.get('expectations')} Rust + {host} host-only expectations, C++ has {c.get('expectations')}")
                status = "COUNT"
            same_frames = r.get("frames") == c.get("frames")
            if not same_frames:
                problems.append(f"{suite} / {name}: frames differ (C++ {c.get('frames')}, Rust {r.get('frames')})")
                status = "FRAMES"
            frames = c.get("frames", ("", 0))
            rows.append((suite, name, s["class"], c.get("expectations", 0), r.get("expectations", 0), host,
                         f"{frames[1]} {'=' if same_frames else '!='}", status))
        for name in rust:
            if name not in cpp:
                problems.append(f"{suite} / {name}: a Rust test with no C++ test of that name")

    width = max(len(r[1]) for r in rows)
    print(f"{'suite':<11} {'test':<{width}} {'class':<9} {'C++':>4} {'Rust':>4} {'host':>4} {'frames':>9}  status")
    for r in rows:
        print(f"{r[0]:<11} {r[1]:<{width}} {r[2]:<9} {r[3]:>4} {str(r[4]):>4} {r[5]:>4} {r[6]:>9}  {r[7]}")
    print(f"\n{totals['cpp_tests']} C++ tests, {totals['rust_tests']} mirrored; expectations: C++ {totals['cpp_exp']}, "
          f"Rust {totals['rust_exp']}, host-only {totals['host_exp']}")
    if args.markdown:
        with open(args.markdown, "w", encoding="utf-8") as f:
            f.write("| Suite | Test | Class | C++ | Rust | Host-only | Frames | Status |\n|---|---|---|---|---|---|---|---|\n")
            for r in rows:
                f.write("| " + " | ".join(str(x) for x in r) + " |\n")
    if problems:
        print(f"\nTEST PARITY: {len(problems)} problem(s)")
        for pr in problems:
            print("  " + pr)
        return 1
    print("TEST PARITY: complete (every C++ engine test mirrored by name, same expectations, same frames)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
