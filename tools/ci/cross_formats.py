"""C++ <-> Rust format round trips with both toolchains (P2_PLAN.md §2.3, docs/VALIDATION.md §8).

    python tools/ci/cross_formats.py --cpp <VigilFormats.exe> --rust <vigil-fmt.exe>
                                     --fixtures tests/fixtures --work <scratch dir>

For every accepted fixture input and every scenario-corpus trace (generated fresh by the C++ side):

    C++ canon(f) == Rust canon(f)              both directions agree on the canonical bytes
    C++ canon(Rust canon(f)) == Rust canon(f)  what Rust writes, C++ reads back unchanged
    Rust canon(C++ canon(f)) == C++ canon(f)   and vice versa

Standard library only. Exit 1 with one line per problem.
"""
import argparse
import pathlib
import shutil
import subprocess
import sys

KINDS = ["json", "trace", "outcome", "marks", "profile", "config"]


def canon(tool, kind, src, dst, schema=None):
    cmd = [tool, "canon", kind, str(src), str(dst)]
    if schema is not None:
        cmd += ["--schema", str(schema)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode == 0, (r.stderr or r.stdout).strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cpp", required=True)
    ap.add_argument("--rust", required=True)
    ap.add_argument("--fixtures", required=True)
    ap.add_argument("--work", required=True)
    a = ap.parse_args()
    fixtures = pathlib.Path(a.fixtures)
    schema = fixtures / "engine-schema.json"
    work = pathlib.Path(a.work)
    shutil.rmtree(work, ignore_errors=True)
    (work / "corpus").mkdir(parents=True)
    subprocess.run([a.cpp, "corpus", str(work / "corpus")], check=True, capture_output=True)

    divergent = set()
    for line in (fixtures / "formats" / "divergences.txt").read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            divergent.add(line.split("\t")[0])

    cases = []
    for kind in KINDS:
        for f in sorted((fixtures / "formats" / "inputs" / kind).iterdir()):
            if f.is_file() and f"{kind}/{f.name}" not in divergent:
                cases.append((kind, f))
    cases += [("trace", f) for f in sorted((work / "corpus").glob("*.vtrace"))]
    cases.append(("trace", fixtures.parent / "traces" / "golden-arena.vtrace"))
    cases.append(("outcome", fixtures.parent / "traces" / "golden-arena.expect.json"))

    problems, agreed, refused = [], 0, 0
    for n, (kind, f) in enumerate(cases):
        c1, r1, c2, r2 = (work / f"{n}.{s}" for s in ("cpp", "rust", "cpp-of-rust", "rust-of-cpp"))
        ok_c, why_c = canon(a.cpp, kind, f, c1)
        ok_r, why_r = canon(a.rust, kind, f, r1, schema)
        label = f"{kind}/{f.name}"
        if not ok_c and not ok_r:
            refused += 1
            if why_c != why_r:
                problems.append(f"{label}: refused for different reasons: C++ '{why_c}', Rust '{why_r}'")
            continue
        if ok_c != ok_r:
            problems.append(f"{label}: C++ {'accepts' if ok_c else 'refuses'} ({why_c}), Rust {'accepts' if ok_r else 'refuses'} ({why_r})")
            continue
        if c1.read_bytes() != r1.read_bytes():
            problems.append(f"{label}: canonical forms differ")
            continue
        ok1, why1 = canon(a.cpp, kind, r1, c2)
        ok2, why2 = canon(a.rust, kind, c1, r2, schema)
        if not (ok1 and ok2 and c2.read_bytes() == r1.read_bytes() and r2.read_bytes() == c1.read_bytes()):
            problems.append(f"{label}: cross round trip not identical ({why1} {why2})")
            continue
        agreed += 1
    for p in problems:
        print(f"::error::{p}")
    print(f"{len(cases)} inputs: {agreed} identical round trips in both directions, {refused} refused alike, "
          f"{len(divergent)} documented divergences skipped, {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
