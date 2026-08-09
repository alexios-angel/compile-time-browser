#!/usr/bin/env python3
"""How much of WebGL 2 works, and what to write a probe for next.

tests/webgl2_api.cpp calls as much of WebGL 2's API as can be run headlessly and
reports which calls pass; tests/webgl2-api.txt records them; this drives the
loop.

    tools/webgl2-api.py                 build, run, show the failures
    tools/webgl2-api.py --advance       record what is passing now
    tools/webgl2-api.py --coverage      which WebGL 2 namespaces no probe mentions
    tools/webgl2-api.py --only textures run and report one module

The companion to webgl2-ratchet.py, and the distinction is the point: the
ratchet measures how FAR the bundle gets - one number up a ladder - and this
measures how WIDE the working surface is. WebGL 2's ratchet read 10/10 while
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

ROOT = Path(__file__).resolve().parent.parent
BUNDLE = ROOT / "vendor/phaser/phaser.js"
PROBES = ROOT / "tests/webgl2-api-probe.js"
RECORD = ROOT / "tests/webgl2-api.txt"
TEST = ROOT / "build/src/tests/ctbrowser-test-webgl2_api"

# The top-level namespaces WebGL 2 hangs off its export object, read from the
# bundle rather than written down here so the denominator tracks the library
# instead of drifting behind it. `    GameObjects: __webpack_require__(77856),`
#
# ANCHORED TO `var WebGL 2 = {`, and it has to be. The same line shape appears in
# every one of the 1763 webpack modules' export objects, so an unanchored match
# counted 1131 "namespaces" - event constants, XHR settings, anything - and
# reported 17/1131, a denominator that makes the work queue meaningless. The
# root object is the only one this question is about.
ROOT_EXPORT = re.compile(r"^var WebGL 2 = \{(.*?)^\};", re.M | re.S)
NAMESPACE = re.compile(r"^    ([A-Z][A-Za-z0-9_$]*): __webpack_require__\(\d+\),?$", re.M)


def build():
    r = subprocess.run(["cmake", "--build", "--preset", "default", "--target",
                        "ctbrowser-test-webgl2_api"],
                       cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit("webgl2-api: build failed")


def run():
    r = subprocess.run([str(TEST)], cwd=ROOT, capture_output=True, text=True)
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
    if "WebGL 2 API:" not in out:
        sys.exit("webgl2-api: the probes did not report:\n" + out)
    names = sorted(passing_from(out))
    if not names:
        sys.exit("webgl2-api: nothing is passing - refusing to record an empty surface")
    header = (
        "# Which WebGL 2 probes pass. tests/webgl2_api.cpp measures it; this file\n"
        "# records it. A probe that used to pass and now does not FAILS the test.\n"
        "#\n"
        "# Only tools/webgl2-api.py --advance writes this file. A test that edits its\n"
        "# own expectations cannot fail, so advancing is a deliberate act.\n"
        "#\n"
        "# The probes themselves are tests/webgl2-api-probe.js, one per line here as\n"
        "# `module/name`. A probe that is SKIPPED is not recorded - it is not a claim\n"
        "# about anything working.\n"
    )
    RECORD.write_text(header + "\n".join(names) + "\n")
    print(f"webgl2-api: recorded {len(names)} passing probes")


def do_coverage():
    """Which probe modules exist, and how many probes each one carries.

    NO BUNDLE TO COUNT AGAINST, unlike p5-api.py and phaser-api.py: WebGL 2 is a
    specification rather than a library, so there is no file to grep for the
    denominator. What is useful instead is the shape of the probe set itself -
    and in particular how much of it is the `unscoped` module, which is the part
    docs/webgl2-plan.md has deliberately NOT committed to and which Babylon.js
    calls in full.
    """
    text = PROBES.read_text(errors="replace")
    modules = {}
    for tag in re.findall(r"^  \['([a-z0-9]+)',", text, re.M):
        modules[tag] = modules.get(tag, 0) + 1
    total = sum(modules.values())
    print(f"\n  {total} probes across {len(modules)} modules\n")
    for tag in sorted(modules, key=lambda t: -modules[t]):
        note = "  <- deliberately not implemented; see docs/webgl2-plan.md" \
               if tag == "unscoped" else ""
        print(f"    {modules[tag]:>3}  {tag}{note}")
    print()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--advance", action="store_true",
                    help="record the passing probes in tests/webgl2-api.txt")
    ap.add_argument("--coverage", action="store_true",
                    help="list WebGL 2 namespaces no probe mentions")
    ap.add_argument("--only", metavar="MODULE",
                    help="report only probes in one module, e.g. textures")
    args = ap.parse_args()

    if not BUNDLE.exists() or not PROBES.exists():
        sys.exit("webgl2-api: the bundle or the probe file is missing")

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
            if args.only in line or "WebGL 2 API:" in line:
                print(line)
    else:
        print(out)
    if code != 0:
        print("The recorded surface is not satisfied. If this is progress, "
              "run tools/webgl2-api.py --advance")
    sys.exit(code)


if __name__ == "__main__":
    main()
