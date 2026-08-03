#!/usr/bin/env python3
"""How far a of the ES module system this engine has, and what is stopping it.

tests/module_ratchet.cpp measures a LEVEL and a BLOCKER; tests/module-ratchet.txt
records them; this drives the loop around both.

    tools/module-ratchet.py             build, measure, show the blocker
    tools/module-ratchet.py --advance   record what was just measured

--advance is the only thing that writes the recorded file. Deliberately: a test
that edits its own expectations cannot fail.

NO CORPUS BUNDLE BEHIND THIS ONE, unlike p5-ratchet.py and phaser-ratchet.py.
The measurement in docs/module-plan.md is why: p5 asks for `module` and falls
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
RECORD = ROOT / "tests/module-ratchet.txt"
TEST = ROOT / "build/src/tests/ctbrowser-test-module_ratchet"

RUNGS = ["nothing", "import/export parse", "one module runs in its own scope",
         "an importer sees an export", "imported bindings are live",
         "a cycle resolves", "<script type=module> runs on a page",
         "relative specifiers resolve", "dynamic import() works",
         "Babylon's ES build boots"]


def build():
    """Build just the ratchet, so the inner loop is seconds rather than a minute."""
    r = subprocess.run(
        ["cmake", "--build", "--preset", "default",
         "--target", "ctbrowser-test-module_ratchet"],
        cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit("module-ratchet: build failed")


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
        sys.exit("module-ratchet: could not read a level from the test:\n" + out)

    old = RECORD.read_text() if RECORD.exists() else "level=0\nblocker=\n"
    was = re.search(r"^level=(\d+)$", old, re.M)
    if was and int(was.group(1)) > level:
        sys.exit(f"module-ratchet: refusing to advance BACKWARDS, "
                 f"{was.group(1)} -> {level}.\nThe pawl only turns one way. "
                 f"Fix the regression.")

    text = re.sub(r"^level=.*$", f"level={level}", old, flags=re.M)
    text = re.sub(r"^blocker=.*$", f"blocker={blocker or ''}", text, flags=re.M)
    RECORD.write_text(text)
    name = RUNGS[level] if level < len(RUNGS) else "?"
    print(f"module-ratchet: recorded level={level} ({name})")
    if blocker:
        print(f"                blocker={blocker}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--advance", action="store_true",
                    help="record the measured level and blocker in tests/module-ratchet.txt")
    args = ap.parse_args()

    build()
    if args.advance:
        do_advance()
        return

    out, code = run()
    print(out)
    if code != 0:
        print("The ratchet is not satisfied. If this is progress, "
              "run tools/module-ratchet.py --advance")
    sys.exit(code)


if __name__ == "__main__":
    main()
