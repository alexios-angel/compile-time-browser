#!/usr/bin/env python3
"""How much of a Babylon.js scene this engine renders, and what is stopping it.

tests/babylon_ratchet.cpp measures a LEVEL and a BLOCKER; tests/babylon-ratchet.txt
records them; this drives the loop around both.

    tools/babylon-ratchet.py             build, measure, show the blocker
    tools/babylon-ratchet.py --advance   record what was just measured

--advance is the only thing that writes the recorded file. Deliberately: a test
that edits its own expectations cannot fail.

THIS IS THE SECOND LADDER OVER THE SAME CORPUS. tools/webgl2-ratchet.py asks
whether Babylon draws AT ALL and reads 10/10; this asks what a scene can
contain. Two ladders over one bundle is not duplication - the first is about
WebGL 2 and stops at the first pixel, and this one is about Babylon.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RECORD = ROOT / "tests/babylon-ratchet.txt"
TEST = ROOT / "build/src/tests/ctbrowser-test-babylon_ratchet"

RUNGS = ["nothing", "a scene renders", "a texture samples",
         "two meshes with two materials", "an animation moves the picture",
         "a directional light shades", "specular highlights", "alpha blending",
         "a post-process runs", "a shadow lands", "a PBR material renders",
         "glTF import", "the GUI draws"]


def build():
    """Build just the ratchet, so the inner loop is seconds rather than a minute."""
    r = subprocess.run(
        ["cmake", "--build", "--preset", "default", "--target",
         "ctbrowser-test-babylon_ratchet"],
        cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit("build failed")


def measure():
    if not TEST.exists():
        sys.exit(f"{TEST} is missing - build first")
    r = subprocess.run([str(TEST)], cwd=ROOT, capture_output=True, text=True)
    out = r.stdout + r.stderr
    level = re.search(r"BABYLON LEVEL (\d+)/", out)
    blocker = re.search(r"blocked by: (.*)", out)
    if level is None:
        sys.stderr.write(out)
        sys.exit("could not read a level out of the test")
    return int(level.group(1)), (blocker.group(1).strip() if blocker else ""), out


def read_record():
    if not RECORD.exists():
        return 0, ""
    text = RECORD.read_text()
    level = re.search(r"^level=(\d+)$", text, re.M)
    blocker = re.search(r"^blocker=(.*)$", text, re.M)
    return (int(level.group(1)) if level else 0), (blocker.group(1) if blocker else "")


def advance(level, blocker):
    text = RECORD.read_text()
    text = re.sub(r"^level=\d+$", f"level={level}", text, count=1, flags=re.M)
    text = re.sub(r"^blocker=.*$", f"blocker={blocker}", text, count=1, flags=re.M)
    RECORD.write_text(text)
    print(f"babylon-ratchet: recorded level {level} ({RUNGS[level]})")
    if blocker:
        print(f"  blocked by: {blocker}")
    print("\nNow write DOWN what changed, in tests/babylon-ratchet.txt itself.")
    print("The number is the smaller half of the record; why it moved is the rest.")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--advance", action="store_true",
                    help="record the level just measured")
    ap.add_argument("--no-build", action="store_true", help="use the existing binary")
    args = ap.parse_args()

    if not args.no_build:
        build()
    level, blocker, out = measure()
    was, was_blocker = read_record()

    print(f"babylon: {level}/12 - {RUNGS[level]}")
    if blocker:
        print(f"  blocked by: {blocker}")
    print(f"  recorded:   {was}/12 - {RUNGS[was]}")

    if args.advance:
        if level < was:
            sys.exit(f"refusing to advance: {level} is BELOW the recorded {was}")
        if level == was and blocker == was_blocker:
            print("nothing to record - the level and the blocker are unchanged")
            return
        advance(level, blocker)
        return

    if level > was:
        print("\nAHEAD of the record. Run --advance to write it down.")
    elif level == was and blocker != was_blocker:
        print("\nSAME LEVEL, DIFFERENT BLOCKER - something else moved. "
              "Run --advance once you know why.")


if __name__ == "__main__":
    main()
