#!/usr/bin/env python3
"""What a packaged application pays before it draws anything, recorded.

    flock /tmp/ctbrowser-devbox-build.lock tools/check/baseline.py
    flock /tmp/ctbrowser-devbox-build.lock tools/check/baseline.py --record

Phase 0's performance baseline. ctcompile's whole claim is that the parsing and
compiling below moves from startup to BUILD time, so this is the number that
claim will be measured against - and a claim with no before is not a claim.

A BASELINE WITHOUT ITS CONFIGURATION IS UNUSABLE SIX MONTHS LATER, which is why
this script exists at all rather than a note saying "run ctbaseline": it records
the machine, configured commands and hashes of the measured artifact and corpora.
Git metadata is supplementary: artifact hashes do not prove source provenance.
The numbers are not comparable across machines and are not meant to be.

MEASURED ON THE DEVBOX, like every other build here. The small WSL box has 7.5
GiB and a different CPU, so a number from it would be a different baseline
wearing this one's name.
"""

import argparse
import hashlib
import json
import os
import platform
import shlex
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
REMOTE_DIR = "~/projects/compile-time-browser"
OUT = ROOT / "ctcompile" / "docs" / "baseline" / "startup.json"
TARGET = "ctcompile-tool-ctbaseline"
BINARY = Path("build/ctcompile/tools/ctbaseline/ctbaseline")

# name=path, relative to the repository root. The four corpora plus the
# Bootstrap fixture: one stylesheet, one document, three bundles.
CORPORA = [
    ("bootstrap-css", "ctbrowser/vendor/bootstrap/bootstrap.css"),
    ("bootstrap-kitchen", "ctbrowser/examples/pages/bootstrap-kitchen.html"),
    ("p5", "ctbrowser/vendor/p5/p5.js"),
    ("phaser", "ctbrowser/vendor/phaser/phaser.js"),
    ("babylon", "ctbrowser/vendor/babylon/babylon.js"),
]


def run_command(command: list[str], cwd: Path | None = None) -> str:
    return subprocess.run(
        command, cwd=cwd, capture_output=True, text=True, check=True, timeout=1800
    ).stdout


def sha256(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def source_metadata(root: Path) -> dict:
    """A synced worktree's .git file may point to an unavailable local path."""
    try:
        top = run_command(["git", "-C", str(root), "rev-parse", "--show-toplevel"]).strip()
        if Path(top).resolve() != root:
            return {"revision": None, "dirty": None}
        revision = run_command(["git", "-C", str(root), "rev-parse", "HEAD"]).strip()
        status = run_command(["git", "-C", str(root), "status", "--porcelain"])
        return {"revision": revision, "dirty": bool(status)}
    except (OSError, subprocess.SubprocessError):
        return {"revision": None, "dirty": None}


def snapshot(root: Path) -> dict:
    build = root / "build"
    cache_path = build / "CMakeCache.txt"
    cache = {}
    for line in cache_path.read_text().splitlines():
        if line.startswith(("#", "//")) or "=" not in line:
            continue
        key, value = line.split("=", 1)
        cache[key.split(":", 1)[0]] = value
    if cache.get("CMAKE_GENERATOR") != "Ninja" or not cache.get("CMAKE_CXX_COMPILER"):
        raise ValueError("the configured Ninja build and C++ compiler are required")
    compiler = cache["CMAKE_CXX_COMPILER"]
    version = run_command([compiler, "--version"], root).splitlines()
    commands = run_command(["ninja", "-C", str(build), "-t", "commands", TARGET], root).splitlines()
    if not any(line.strip() for line in version) or not any(line.strip() for line in commands):
        raise ValueError("compiler version and configured build commands must not be empty")
    return {
        "kind": "built-artifact-and-corpora; source-to-binary provenance is not verified",
        "binary": {"path": str(BINARY), "sha256": sha256(root / BINARY)},
        "corpora": [
            {"name": name, "path": path, "sha256": sha256(root / path)} for name, path in CORPORA
        ],
        "source": source_metadata(root),
        "configuration": {
            "cmake_cache_sha256": sha256(cache_path),
            "build_ninja_sha256": sha256(build / "build.ninja"),
            "build_type": cache.get("CMAKE_BUILD_TYPE", ""),
            "compiler_path": compiler,
            "compiler_version": version[0],
            "commands": commands,
        },
    }


def machine() -> dict:
    cpu = next(
        (
            line.split(":", 1)[1].strip()
            for line in Path("/proc/cpuinfo").read_text().splitlines()
            if line.startswith("model name")
        ),
        platform.machine(),
    )
    memory = next(
        line
        for line in Path("/proc/meminfo").read_text().splitlines()
        if line.startswith("MemTotal:")
    )
    return {
        "cpu": cpu,
        "cores": len(os.sched_getaffinity(0)),
        "ram_mb": int(memory.split()[1]) // 1024,
        "os": platform.freedesktop_os_release().get("PRETTY_NAME", platform.platform()),
    }


def collect(root: Path) -> dict:
    root = root.expanduser().resolve()
    # The configured build, not a guessed preset/compiler description.
    run_command(["cmake", "--build", str(root / "build"), "--target", TARGET], root)
    before = snapshot(root)
    measured = json.loads(
        run_command([str(root / BINARY), *(f"{name}={path}" for name, path in CORPORA)], root)
    )
    if snapshot(root) != before:
        raise ValueError("remote artifact, corpus or configuration changed during measurement")
    return {"identity": before, "machine": machine(), "runs": measured["runs"]}


def on_box(host: str, remote_dir: str) -> dict:
    # Ship this collector over stdin; a stale remote copy must not label the run.
    command = shlex.join(["python3", "-", "--collect", remote_dir])
    done = subprocess.run(
        ["ssh", host, command],
        input=Path(__file__).read_text(),
        capture_output=True,
        text=True,
        check=True,
        timeout=3600,
    )
    return json.loads(done.stdout)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", default="devbox", help="the build box (default: devbox)")
    ap.add_argument(
        "--remote-dir", default=REMOTE_DIR, help="repository directory on the build box"
    )
    ap.add_argument("--collect", type=Path, help=argparse.SUPPRESS)
    ap.add_argument(
        "--record",
        action="store_true",
        help=f"write {OUT.relative_to(ROOT)} as well as printing it",
    )
    args = ap.parse_args()
    if args.collect is not None:
        print(json.dumps(collect(args.collect)))
        return 0
    measured = on_box(args.host, args.remote_dir)
    measured["machine"]["host"] = args.host
    report = {
        "what": "startup cost per stage, before any of it is compiled ahead of time",
        "measured_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        # SAID OUT LOUD, because a baseline that quietly omits half the pipeline
        # invites someone to compare against it as though it were the whole.
        "not_measured": [
            "style resolution, layout, paint, raster and first frame - these stay "
            "runtime work by Principle 6 and a compiler must not freeze them; "
            "ctbrowser/benchmarks/ measures them",
            "js_run_top_level runs in a BARE script::context with no browser globals, "
            "so every bundle stops early - see each stage's stopped_because. Measuring "
            "real top-level execution needs the shell, and is a later measurement",
        ],
        **measured,
    }

    text = json.dumps(report, indent=2) + "\n"
    print(text, end="")
    if args.record:
        OUT.parent.mkdir(parents=True, exist_ok=True)
        OUT.write_text(text)
        print(f"\nrecorded {OUT.relative_to(ROOT)}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        detail = (
            (error.stderr or "").strip() if isinstance(error, subprocess.CalledProcessError) else ""
        )
        sys.exit(f"baseline.py: {error}\n{detail}".rstrip())
