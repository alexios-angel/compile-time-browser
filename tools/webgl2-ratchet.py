#!/usr/bin/env python3
"""How far a WebGL 2 page gets through the engine, and what is stopping it.

tests/webgl2_ratchet.cpp measures a LEVEL and a BLOCKER; tests/corpus/webgl2/webgl2-ratchet.txt
records them; this drives the loop around both.

    tools/webgl2-ratchet.py             build, measure, show the blocker
    tools/webgl2-ratchet.py --advance   record what was just measured

--advance is the only thing that writes the recorded file. Deliberately: a test
that edits its own expectations cannot fail.

NO CORPUS BUNDLE BEHIND THIS ONE, unlike p5-ratchet.py and phaser-ratchet.py.
The measurement in docs/webgl2-plan.md is why: p5 asks for `webgl2` and falls
back silently, Phaser never asks at all, and Babylon - which does use nearly the
whole specification - is not vendored here. So the ladder drives the API
directly and only its last rung hands the result to a real renderer.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RECORD = ROOT / "tests/corpus/webgl2/webgl2-ratchet.txt"
TEST = ROOT / "build/src/tests/ctbrowser-test-webgl2_ratchet"

RUNGS = ["nothing", "makes a webgl2 context", "has the WebGL 2 constants",
         "compiles #version 300 es", "vertex array objects work",
         "accepts instanced drawing", "an instanced draw reaches the pixels",
         "the WebGL 1 extensions expose the same thing",
         "Phaser's WebGL renderer paints on it"]


def build():
    """Build just the ratchet, so the inner loop is seconds rather than a minute."""
    r = subprocess.run(
        ["cmake", "--build", "--preset", "default",
         "--target", "ctbrowser-test-webgl2_ratchet"],
        cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit("webgl2-ratchet: build failed")


def run():
    """Run the measurement. Its exit code is the pawl's verdict, not an error here."""
    r = subprocess.run([str(TEST)], cwd=ROOT, capture_output=True, text=True)
    return r.stdout + r.stderr, r.returncode


def parse_output(out):
    level, blocker = None, None
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("LEVEL "):
            level = int(line.split()[1].split("/")[0])
        elif line.startswith("BLOCKER "):
            blocker = line[len("BLOCKER "):]
        elif line == "BLOCKER":
            blocker = ""
    return level, blocker


def do_advance():
    out, _ = run()
    level, blocker = parse_output(out)
    if level is None:
        sys.exit("webgl2-ratchet: could not read a level from the test:\n" + out)

    old = RECORD.read_text() if RECORD.exists() else "level=0\nblocker=\n"
    was = re.search(r"^level=(\d+)$", old, re.M)
    if was and int(was.group(1)) > level:
        sys.exit(f"webgl2-ratchet: refusing to advance BACKWARDS, "
                 f"{was.group(1)} -> {level}.\nThe pawl only turns one way. "
                 f"Fix the regression.")

    text = re.sub(r"^level=.*$", f"level={level}", old, flags=re.M)
    text = re.sub(r"^blocker=.*$", f"blocker={blocker or ''}", text, flags=re.M)
    RECORD.write_text(text)
    name = RUNGS[level] if level < len(RUNGS) else "?"
    print(f"webgl2-ratchet: recorded level={level} ({name})")
    if blocker:
        print(f"                blocker={blocker}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--advance", action="store_true",
                    help="record the measured level and blocker in tests/corpus/webgl2/webgl2-ratchet.txt")
    args = ap.parse_args()

    build()
    if args.advance:
        do_advance()
        return

    out, code = run()
    print(out)
    if code != 0:
        print("The ratchet is not satisfied. If this is progress, "
              "run tools/webgl2-ratchet.py --advance")
    sys.exit(code)


if __name__ == "__main__":
    main()
