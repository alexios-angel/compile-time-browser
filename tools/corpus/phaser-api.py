#!/usr/bin/env python3
"""How much of Phaser 4 works, and what to write a probe for next.

ctbrowser/test/corpus/phaser/phaser_api.cpp calls as much of Phaser's API as can be run headlessly and
reports which calls pass; ctbrowser/test/corpus/phaser/phaser-api.txt records them; this drives the
loop.

    tools/corpus/phaser-api.py                 build, run, show the failures
    tools/corpus/phaser-api.py --advance       record what is passing now
    tools/corpus/phaser-api.py --coverage      which Phaser namespaces no probe mentions
    tools/corpus/phaser-api.py --only textures run and report one module

The companion to phaser-ratchet.py, and the distinction is the point: the
ratchet measures how FAR the bundle gets - one number up a ladder - and this
measures how WIDE the working surface is. Phaser's ratchet read 10/10 while
`(5).hasOwnProperty` was undefined, because nothing on the ladder asked a
number for a property.

--advance is the only thing that writes the txt. Deliberately: a test that
edits its own expectations cannot fail.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
# The test binary resolves its inputs against the ctbrowser PROJECT
# directory, which is where ctest runs it from too. ROOT is the
# repository root and is one level above that since the monorepo split.
ENGINE = ROOT / "ctbrowser"
BUNDLE = ROOT / "ctbrowser/vendor/phaser/phaser.js"
PROBES = ROOT / "ctbrowser/test/corpus/phaser/phaser-api-probe.js"
RECORD = ROOT / "ctbrowser/test/corpus/phaser/phaser-api.txt"
TEST = ROOT / "build/test/ctbrowser-test-phaser_api"

# The top-level namespaces Phaser hangs off its export object, read from the
# bundle rather than written down here so the denominator tracks the library
# instead of drifting behind it. `    GameObjects: __webpack_require__(77856),`
#
# ANCHORED TO `var Phaser = {`, and it has to be. The same line shape appears in
# every one of the 1763 webpack modules' export objects, so an unanchored match
# counted 1131 "namespaces" - event constants, XHR settings, anything - and
# reported 17/1131, a denominator that makes the work queue meaningless. The
# root object is the only one this question is about.
ROOT_EXPORT = re.compile(r"^var Phaser = \{(.*?)^\};", re.M | re.S)
NAMESPACE = re.compile(r"^    ([A-Z][A-Za-z0-9_$]*): __webpack_require__\(\d+\),?$", re.M)


def build():
    r = subprocess.run(["cmake", "--build", "--preset", "default", "--target",
                        "ctbrowser-test-phaser_api"],
                       cwd=ENGINE, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit("phaser-api: build failed")


def run():
    r = subprocess.run([str(TEST)], cwd=ENGINE, capture_output=True, text=True)
    return r.stdout + r.stderr, r.returncode


def passing_from(out):
    """The probes the test reported as newly passing plus those already recorded.

    The test prints failures and newly-passing names; the recorded file holds
    the rest. Reading both is what lets --advance write the full set without the
    test having to print hundreds of lines it already agrees with.
    """
    # `(.+)`, NOT `(\S+)`: a probe name has spaces in it - `textures/boot
    # textures exist`, `loader/image from a data URL`. The first version of this
    # stopped at the first space and silently recorded 69 of 76, which is the
    # harness losing data about the harness.
    gained = re.findall(r"^\s+\+ (.+)$", out, re.M)
    lost = re.findall(r"^\s+- (.+)$", out, re.M)
    recorded = set()
    if RECORD.exists():
        for line in RECORD.read_text().splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                recorded.add(line)
    return (recorded | set(gained)) - set(lost)


def do_advance():
    out, _ = run()
    if "Phaser API:" not in out:
        sys.exit("phaser-api: the probes did not report:\n" + out)
    names = sorted(passing_from(out))
    if not names:
        sys.exit("phaser-api: nothing is passing - refusing to record an empty surface")
    header = (
        "# Which Phaser 4 probes pass. ctbrowser/test/corpus/phaser/phaser_api.cpp measures it; this file\n"
        "# records it. A probe that used to pass and now does not FAILS the test.\n"
        "#\n"
        "# Only tools/corpus/phaser-api.py --advance writes this file. A test that edits its\n"
        "# own expectations cannot fail, so advancing is a deliberate act.\n"
        "#\n"
        "# The probes themselves are ctbrowser/test/corpus/phaser/phaser-api-probe.js, one per line here as\n"
        "# `module/name`. A probe that is SKIPPED is not recorded - it is not a claim\n"
        "# about anything working.\n"
    )
    RECORD.write_text(header + "\n".join(names) + "\n")
    print(f"phaser-api: recorded {len(names)} passing probes")


def do_coverage():
    """Which of Phaser's namespaces no probe mentions - the work queue."""
    text = BUNDLE.read_text(errors="replace")
    root = ROOT_EXPORT.search(text)
    if not root:
        sys.exit("phaser-api: no `var Phaser = {` in the bundle - has its shape changed?")
    namespaces = sorted(set(NAMESPACE.findall(root.group(1))))
    if not namespaces:
        sys.exit("phaser-api: found no namespaces in the bundle - has its shape changed?")
    probes = PROBES.read_text(errors="replace")
    # WHERE A PROBE'S MODULE TAG DOES NOT MATCH PHASER'S NAMESPACE NAME. Written
    # out rather than matched fuzzily: a prefix rule that turned `Animations`
    # into `anims` would also turn something else into a false positive, and a
    # coverage tool that overstates itself is worse than one that understates.
    # Anything not listed here is matched on its own name.
    aliases = {
        "Animations": ["anims"],
        "GameObjects": ["add", "gameobject", "displaylist"],
        "Scenes": ["scene"],
    }
    covered, missing = [], []
    for name in namespaces:
        tags = aliases.get(name, [name.lower()])
        if f"Phaser.{name}" in probes or any(f"['{tag}'," in probes for tag in tags):
            covered.append(name)
        else:
            missing.append(name)
    print(f"\n  {len(covered)}/{len(namespaces)} Phaser namespaces have at least one probe\n")
    for name in missing:
        print(f"    no probe mentions  Phaser.{name}")
    print("\n  A namespace with one probe is not a namespace that works - this is a\n"
          "  list of what has NOTHING pointed at it, not a coverage percentage.\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--advance", action="store_true",
                    help="record the passing probes in ctbrowser/test/corpus/phaser/phaser-api.txt")
    ap.add_argument("--coverage", action="store_true",
                    help="list Phaser namespaces no probe mentions")
    ap.add_argument("--only", metavar="MODULE",
                    help="report only probes in one module, e.g. textures")
    args = ap.parse_args()

    if not BUNDLE.exists() or not PROBES.exists():
        sys.exit("phaser-api: the bundle or the probe file is missing")

    if args.coverage:
        do_coverage()
        return

    build()
    if args.advance:
        do_advance()
        return

    out, code = run()
    if args.only:
        for line in out.splitlines():
            if args.only in line or "Phaser API:" in line:
                print(line)
    else:
        print(out)
    if code != 0:
        print("The recorded surface is not satisfied. If this is progress, "
              "run tools/corpus/phaser-api.py --advance")
    sys.exit(code)


if __name__ == "__main__":
    main()
