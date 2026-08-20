#!/usr/bin/env python3
"""How much of p5.js works, and what to write a probe for next.

ctbrowser/test/corpus/p5/p5_api.cpp calls as much of p5's API as can be run headlessly and reports
which calls pass; ctbrowser/test/corpus/p5/p5-api.txt records them; this drives the loop.

    tools/corpus/p5-api.py                 build, run, show the failures
    tools/corpus/p5-api.py --advance       record what is passing now
    tools/corpus/p5-api.py --coverage      which of p5's functions no probe mentions
    tools/corpus/p5-api.py --only shape    run and report one module

The companion to p5-ratchet.py, and the distinction is the point: the ratchet
measures how FAR the bundle gets - one number up a ladder - and this measures
how WIDE the working surface is. The ratchet read 12/12 for days while
colorMode(HSB) was broken, because nothing on the ladder called it.

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
BUNDLE = ROOT / "ctbrowser/vendor/p5/p5.js"
PROBES = ROOT / "ctbrowser/test/corpus/p5/p5-api-probe.js"
RECORD = ROOT / "ctbrowser/test/corpus/p5/p5-api.txt"
TEST = ROOT / "build/test/ctbrowser-test-p5_api"

# `    fn.background = function (...)` - p5's public surface, as the bundle
# itself declares it. Read from the bundle rather than written down here, so the
# denominator tracks the library instead of drifting behind it.
PUBLIC = re.compile(r"^\s+fn\.([a-zA-Z][A-Za-z0-9_$]*)\s*=", re.M)


def build():
    r = subprocess.run(["cmake", "--build", "--preset", "default", "--target",
                        "ctbrowser-test-p5_api"], cwd=ENGINE, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit("p5-api: build failed")


def run():
    r = subprocess.run([str(TEST)], cwd=ENGINE, capture_output=True, text=True)
    return r.stdout + r.stderr, r.returncode


def parse(out):
    """(passing, failing, advancing) from the test's own report."""
    passing, failing, advancing = [], [], []
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("!! "):
            failing.append(line[3:])
        elif line.startswith("+ "):
            advancing.append(line[2:])
    for line in out.splitlines():
        if "probes -" in line:
            passing = [line.strip()]
    return passing, failing, advancing


def do_advance():
    out, _ = run()
    _, failing, advancing = parse(out)
    if not advancing:
        print("p5-api: nothing new is passing.")
        if failing:
            print(f"        {len(failing)} still failing:")
            for f in failing:
                print(f"          {f}")
        return

    # The test prints what is newly passing; the recorded file is the union of
    # what it had and that. Rewriting from the test's full pass list would lose
    # a name whose probe is temporarily commented out, which is a different
    # thing from a regression and should not look like one.
    header, names = [], set()
    if RECORD.exists():
        for line in RECORD.read_text().splitlines():
            if line.startswith("#") or not line.strip():
                header.append(line)
            else:
                names.add(line.strip())
    names.update(advancing)
    RECORD.write_text("\n".join(header + sorted(names)) + "\n")
    print(f"p5-api: recorded {len(advancing)} newly passing, {len(names)} total")
    for name in advancing:
        print(f"        + {name}")


def do_coverage():
    """Which of p5's public functions no probe mentions.

    This is the work queue. A function nobody has written a probe for is not
    passing and not failing - it is UNMEASURED, which is the state every bug
    found so far was hiding in.
    """
    declared = sorted(set(PUBLIC.findall(BUNDLE.read_text(errors="replace"))))
    # Internals are not the public surface and are not worth probing directly.
    public = [n for n in declared if not n.startswith("_")]
    probe_text = PROBES.read_text()
    covered = [n for n in public if re.search(rf"\b{re.escape(n)}\b", probe_text)]
    missing = [n for n in public if n not in covered]

    print(f"\n  p5 declares {len(public)} public functions; probes mention {len(covered)}.\n")
    print(f"  {len(missing)} with no probe:\n")
    for i in range(0, len(missing), 6):
        print("    " + "  ".join(f"{n:<22}" for n in missing[i:i + 6]).rstrip())
    print()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--advance", action="store_true",
                    help="record what is passing now in ctbrowser/test/corpus/p5/p5-api.txt")
    ap.add_argument("--coverage", action="store_true",
                    help="list p5 functions no probe mentions - the work queue")
    ap.add_argument("--only", metavar="MODULE", help="show only one module's results")
    args = ap.parse_args()

    if not BUNDLE.exists():
        sys.exit(f"p5-api: {BUNDLE.relative_to(ROOT)} is missing")
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
            if args.only in line or "probes -" in line:
                print(line)
    else:
        print(out)
    sys.exit(code)


if __name__ == "__main__":
    main()
