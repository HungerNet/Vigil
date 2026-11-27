"""Repository checks run by .github/workflows/validate.yml (docs/RELEASE_PROCESS.md).

    python tools/ci/validate_repo.py metadata [--github-output FILE]
        version.yaml is well formed, has a changelog entry for the version, and the release notes
        name the version. Writes version, run, tag and asset name to FILE (GitHub step outputs).
    python tools/ci/validate_repo.py links
        Every relative link in the repository's Markdown files points at an existing file.
    python tools/ci/validate_repo.py golden
        The golden trace and its expected outcome exist and are well formed.
    python tools/ci/validate_repo.py engine-globals
        The detection engine owns no global state (RUST_PLAN.md P1): nothing under src/detection
        except the registry binding (config_binding.*) includes the settings registry or names
        Settings/Setting, and no file declares a mutable static variable.

Standard library only. Exit code 1 with one line per problem when a check fails.
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
VERSION_RE = re.compile(r"^\d+\.\d+(?:\.\d+)?(?:-dev\.\d+)?$")


def fail(problems):
    for p in problems:
        print(f"::error::{p}")
    return 1 if problems else 0


def read_version():
    text = (ROOT / "version.yaml").read_text(encoding="utf-8")
    m = re.search(r"(?m)^version:\s*'?([^'\r\n]+)'?\s*$", text)
    run = re.search(r"(?m)^run:\s*(\w+)\s*$", text)
    return text, (m.group(1).strip() if m else ""), (run.group(1).strip().lower() if run else "")


def asset_name(version):
    # Windows- and URL-safe: GitHub turns spaces in asset names into dots (PLAN.md §10.2).
    return f"Vigil-{version}.exe"


def check_metadata(github_output):
    problems = []
    text, version, run = read_version()
    if not version:
        problems.append("version.yaml has no version")
    elif not VERSION_RE.match(version):
        problems.append(f"version '{version}' is not in the form 1.0-dev.N or 1.0[.N]")
    if run not in ("true", "false"):
        problems.append("version.yaml 'run' must be true or false")
    dev = re.search(r"-dev\.(\d+)$", version or "")
    if dev and not re.search(rf"(?m)^\s*Dev {dev.group(1)}\b", text) and not re.search(
            rf"(?m)^\s*Dev [\d-]*\b{dev.group(1)}:", text):
        problems.append(f"version.yaml description has no 'Dev {dev.group(1)}:' entry")
    notes = ROOT / "docs" / "RELEASE_NOTES.md"
    if not notes.exists():
        problems.append("docs/RELEASE_NOTES.md is missing (it becomes the release body)")
    elif version and version not in notes.read_text(encoding="utf-8")[:600]:
        problems.append(f"docs/RELEASE_NOTES.md does not mention {version} near the top (it becomes the release body)")
    if github_output:
        with open(github_output, "a", encoding="utf-8") as out:
            out.write(f"version={version}\nrun={run}\ntag={version}\nasset={asset_name(version)}\n")
    print(f"version {version}, run={run}, asset {asset_name(version)}")
    return problems


def check_links():
    problems = []
    link = re.compile(r"\]\(([^)\s]+)\)")
    for md in ROOT.rglob("*.md"):
        if any(part in (".git", "build", "node_modules") for part in md.parts):
            continue
        text = md.read_text(encoding="utf-8", errors="replace")
        # Ignore code blocks: links inside them are examples.
        text = re.sub(r"```.*?```", "", text, flags=re.S)
        for target in link.findall(text):
            if re.match(r"^[a-z]+:", target) or target.startswith("#") or target.startswith("mailto:"):
                continue
            path = target.split("#", 1)[0]
            if not path:
                continue
            resolved = (md.parent / path).resolve()
            if not resolved.exists():
                problems.append(f"{md.relative_to(ROOT)}: broken link '{target}'")
    print(f"checked links in {sum(1 for _ in ROOT.rglob('*.md'))} Markdown files")
    return problems


def check_golden():
    problems = []
    trace = ROOT / "tests" / "traces" / "golden-arena.vtrace"
    expect = ROOT / "tests" / "traces" / "golden-arena.expect.json"
    if not trace.exists() or not expect.exists():
        return ["golden trace or its expectation is missing (tests/traces)"]
    with open(trace, encoding="utf-8") as f:
        header = json.loads(f.readline())
        frames = sum(1 for line in f if line.strip())
    if header.get("kind") != "vigil-trace":
        problems.append("golden trace header is not a vigil-trace")
    outcome = json.loads(expect.read_text(encoding="utf-8"))
    if outcome.get("kind") != "vigil-outcome":
        problems.append("golden expectation is not a vigil-outcome")
    elif outcome.get("frames") != frames:
        problems.append(f"golden expectation covers {outcome.get('frames')} frames, the trace has {frames}")
    print(f"golden trace: {frames} frames")
    return problems


REGISTRY_RE = re.compile(r'core/settings\.h|\bSettings\s*::|\bSettings\s*&|\bSetting\s*[*&]|\bSettingRange\b')
STATIC_RE = re.compile(r"^\s*static\s+(?!const\b|constexpr\b|inline\b)([^=;{(]*)[=;{]")


def check_engine_globals():
    problems = []
    allowed = {"config_binding.h", "config_binding.cpp"}
    files = sorted(p for p in (ROOT / "src" / "detection").rglob("*") if p.suffix in (".h", ".cpp"))
    for path in files:
        if path.name in allowed:
            continue
        rel = path.relative_to(ROOT).as_posix()
        for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            code = line.split("//", 1)[0]
            if REGISTRY_RE.search(code):
                problems.append(f"{rel}:{n}: the engine reads the settings registry (use EngineConfig; only config_binding.* may)")
            if STATIC_RE.match(code):
                problems.append(f"{rel}:{n}: mutable static state in the engine")
    print(f"engine globals: checked {len(files)} files under src/detection")
    return problems


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "metadata":
        out = argv[argv.index("--github-output") + 1] if "--github-output" in argv else None
        return fail(check_metadata(out))
    if cmd == "links":
        return fail(check_links())
    if cmd == "golden":
        return fail(check_golden())
    if cmd == "engine-globals":
        return fail(check_engine_globals())
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
