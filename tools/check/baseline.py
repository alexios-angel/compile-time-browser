#!/usr/bin/env python3
"""What a packaged application pays before it draws anything, recorded.

    tools/check/baseline.py            # measure on the devbox, print the JSON
    tools/check/baseline.py --record   # and write it to ctcompile/docs/baseline/

Phase 0's performance baseline. ctcompile's whole claim is that the parsing and
compiling below moves from startup to BUILD time, so this is the number that
claim will be measured against - and a claim with no before is not a claim.

A BASELINE WITHOUT ITS CONFIGURATION IS UNUSABLE SIX MONTHS LATER, which is why
this script exists at all rather than a note saying "run ctbaseline": it records
the machine, the compiler, the build flags and the commit beside every timing.
The numbers are not comparable across machines and are not meant to be.

MEASURED ON THE DEVBOX, like every other build here. The small WSL box has 7.5
GiB and a different CPU, so a number from it would be a different baseline
wearing this one's name.
"""
import argparse
import json
import shlex
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
REMOTE_DIR = "$HOME/projects/compile-time-browser"
OUT = ROOT / "ctcompile" / "docs" / "baseline" / "startup.json"

# name=path, relative to the repository root. The four corpora plus the
# Bootstrap fixture: one stylesheet, one document, three bundles.
CORPORA = [
    ("bootstrap-css", "ctbrowser/vendor/bootstrap/bootstrap.css"),
    ("bootstrap-kitchen", "ctbrowser/examples/pages/bootstrap-kitchen.html"),
    ("p5", "ctbrowser/vendor/p5/p5.js"),
    ("phaser", "ctbrowser/vendor/phaser/phaser.js"),
    ("babylon", "ctbrowser/vendor/babylon/babylon.js"),
]


def on_box(command: str, host: str) -> str:
    done = subprocess.run(["ssh", host, command], capture_output=True, text=True)
    if done.returncode != 0:
        sys.exit(f"baseline.py: {command!r} failed on {host}:\n{done.stderr.strip()}")
    return done.stdout


def machine(host: str) -> dict:
    """The configuration the numbers are only meaningful against."""
    probe = (
        "grep -m1 '^model name' /proc/cpuinfo | cut -d: -f2- ; "
        "nproc ; "
        "free -m | awk '/^Mem:/ {print $2}' ; "
        f"{REMOTE_DIR}/tools/clang-std-embed/bin/clang++ --version | head -1 ; "
        ". /etc/os-release && echo \"$PRETTY_NAME\""
    )
    cpu, cores, ram_mb, compiler, os_name = [
        line.strip() for line in on_box(probe, host).splitlines()[:5]
    ]
    return {
        "cpu": cpu,
        "cores": int(cores),
        "ram_mb": int(ram_mb),
        "os": os_name,
        "compiler": compiler,
        # The engine's flags, from ctbrowser/lib/CMakeLists.txt's
        # CTBROWSER_STRICT_WARNINGS plus the Release preset's own.
        "build": "cmake --preset default (Release, -O3 -DNDEBUG -O2)",
        "host": host,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", default="devbox", help="the build box (default: devbox)")
    ap.add_argument("--record", action="store_true",
                    help=f"write {OUT.relative_to(ROOT)} as well as printing it")
    args = ap.parse_args()

    # Built EXCLUDE_FROM_ALL, so it has to be asked for by name.
    on_box(f"cd {REMOTE_DIR}/ctbrowser && cmake --build --preset default "
           "--target ctcompile-tool-ctbaseline", args.host)

    corpora = " ".join(shlex.quote(f"{name}={path}") for name, path in CORPORA)
    measured = json.loads(on_box(
        f"cd {REMOTE_DIR} && build/ctcompile/tools/ctbaseline/ctbaseline {corpora}", args.host))

    commit = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"],
                            capture_output=True, text=True).stdout.strip()
    report = {
        "what": "startup cost per stage, before any of it is compiled ahead of time",
        "measured_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "commit": commit,
        "machine": machine(args.host),
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
    sys.exit(main())
