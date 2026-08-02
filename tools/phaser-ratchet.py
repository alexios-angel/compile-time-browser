#!/usr/bin/env python3
"""How far Phaser 4 gets through the engine, and what is stopping it.

tests/phaser_ratchet.cpp measures a LEVEL and a BLOCKER; tests/phaser-ratchet.txt
records them; this drives the loop around both.

    tools/phaser-ratchet.py             build, measure, show the blocker in context
    tools/phaser-ratchet.py --advance   record what was just measured

--advance is the only thing that writes the recorded file. Deliberately: a test
that edits its own expectations cannot fail.

WHY THE BLOCKER IS RECORDED AS WELL AS THE LEVEL. The four bugs that took this
from 7/10 to 9/10 were found one behind another, each hidden by the one in
front - a data: URL that would not decode, then a PNG that would not decode
without SDL, then unary plus, then `a.length = 0`. A fix that trades one wall
for another leaves the number alone, and without comparing the blocker that
reads as no change at all.

NO --bisect HERE, unlike tools/p5-ratchet.py, and that is a deliberate absence
rather than an omission. p5's carving exists because that bundle failed at the
LANGUAGE rungs and a 4.5 MB IIFE makes a parse error a needle in a haystack.
Phaser clears lexing, parsing and compiling outright, so every failure it has
had is a runtime one that a fragment cannot reproduce. Its webpack modules are
delimited (`/***/ <id>` ... `/***/ },`, 1763 of them) if that ever changes -
but building the machinery before there is a failure to point it at would be
building it for nobody.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUNDLE = ROOT / "examples/assets/phaser/phaser.js"
RECORD = ROOT / "tests/phaser-ratchet.txt"
TEST = ROOT / "build/src/tests/ctbrowser-test-phaser_ratchet"

RUNGS = ["unread", "read", "lexed", "parsed", "compiled", "runs as a page",
         "defines Phaser", "constructs a Game", "runs create()", "runs update()",
         "paints what the scene drew"]


def build():
    """Build just the ratchet, so the inner loop is seconds rather than a minute."""
    r = subprocess.run(
        ["cmake", "--build", "--preset", "default",
         "--target", "ctbrowser-test-phaser_ratchet"],
        cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit("phaser-ratchet: build failed")


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


def show_context(blocker, span=10):
    """Print the bundle around a `file:LINE:COL` mentioned in the blocker.

    A blocker that names a position but makes you go and find it is half a
    diagnostic.
    """
    hit = re.search(r"([\w.$-]+):(\d+):(\d+)", blocker or "")
    if not hit:
        return
    line_no, col = int(hit.group(2)), int(hit.group(3))
    try:
        lines = BUNDLE.read_text(errors="replace").splitlines()
    except OSError:
        return
    if line_no > len(lines):
        return
    lo, hi = max(1, line_no - span), min(len(lines), line_no + span)
    print(f"\n  {BUNDLE.name}:{line_no}:{col}")
    for n in range(lo, hi + 1):
        mark = "->" if n == line_no else "  "
        print(f"  {mark} {n:>6}  {lines[n - 1]}")
        if n == line_no:
            print(f"       {'':>6}  {' ' * (col - 1)}^")
    print()


def do_advance():
    out, _ = run()
    level, blocker = parse_output(out)
    if level is None:
        sys.exit("phaser-ratchet: could not read a level from the test:\n" + out)

    old = RECORD.read_text() if RECORD.exists() else "level=0\nblocker=\n"

    was = re.search(r"^level=(\d+)$", old, re.M)
    if was and int(was.group(1)) > level:
        sys.exit(f"phaser-ratchet: refusing to advance BACKWARDS, "
                 f"{was.group(1)} -> {level}.\nThe pawl only turns one way. "
                 f"Fix the regression.")

    text = re.sub(r"^level=.*$", f"level={level}", old, flags=re.M)
    text = re.sub(r"^blocker=.*$", f"blocker={blocker or ''}", text, flags=re.M)
    RECORD.write_text(text)
    name = RUNGS[level] if level < len(RUNGS) else "?"
    print(f"phaser-ratchet: recorded level={level} ({name})")
    if blocker:
        print(f"                blocker={blocker}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--advance", action="store_true",
                    help="record the measured level and blocker in tests/phaser-ratchet.txt")
    args = ap.parse_args()

    if not BUNDLE.exists():
        sys.exit(f"phaser-ratchet: {BUNDLE.relative_to(ROOT)} is missing")

    build()
    if args.advance:
        do_advance()
        return

    out, code = run()
    print(out)
    _, blocker = parse_output(out)
    show_context(blocker)
    if code != 0:
        print("The ratchet is not satisfied. If this is progress, "
              "run tools/phaser-ratchet.py --advance")
    sys.exit(code)


if __name__ == "__main__":
    main()
