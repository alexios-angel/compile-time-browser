#!/usr/bin/env python3
"""TWO IMPLEMENTATIONS OF AN ORACLE'S CHECKER, COMPARED - Phase 54B, and 55O
for the escape half.

`Oracle.cpp` walks the recorder in memory and runs the verdict table over stub
inferences; tools/check/{type,escape}-oracle.py parses the recording FILE and
runs the same table. Neither is a transcription of the other and they share no
arithmetic. This runs both over the same fixture and makes the C++ side's
counters the Python side's EXPECTATIONS, so a disagreement is a failure rather
than two numbers nobody compared. THE FILE FORMAT IS WHAT SITS BETWEEN THEM: a
recorder bug shows up in both and would agree with itself; a writer or parser
bug shows up in exactly one.

Both halves guard the vacuous pass: the deliberately wrong inference must have
been caught, in a recording that observed something and left something
unobserved; the trivial inference must be sound and useless; and the checker
must NAME a site, because a count that cannot say where is a number, not a bug
report.
"""

import argparse
import re
import subprocess
import sys

TYPE_TALLY = (
    r"all-i32 observed ([0-9]+) unobserved ([0-9]+) violations ([0-9]+) beat-boxed ([0-9]+)"
)
ESCAPE_FIELDS = (
    "claimed observed unobserved violations sound partial pending imprecise exact unclaimed "
    "inconclusive mismatch"
).split()


def run(command):
    result = subprocess.run(command, capture_output=True, text=True)
    print(result.stdout + result.stderr)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, help="ctcompile-test-type-oracle")
    parser.add_argument("--script", required=True, help="tools/check/*-oracle.py")
    parser.add_argument("--recording", required=True)
    parser.add_argument("--escape", action="store_true")
    args = parser.parse_args()

    selftest = run([args.exe, *(["--escape"] if args.escape else []), "--out", args.recording])
    if selftest.returncode != 0:
        sys.exit(f"the oracle's own self-test failed (exit {selftest.returncode})")
    out = selftest.stdout
    checker = [sys.executable, args.script, "--recording", args.recording, "--name", "selftest"]

    if not args.escape:
        # Anchored, because this file exists because two numbers that were
        # never compared looked like agreement.
        tally = re.search(TYPE_TALLY, out)
        if not tally:
            sys.exit(f"the executable did not report an all-i32 tally:\n{out}")
        observed, unobserved, violations, beat = map(int, tally.groups())
        if violations <= 0:
            sys.exit(f"all-i32 produced {violations} violations - the checker caught nothing")
        if observed <= 0:
            sys.exit(f"the recording observed {observed} registers - nothing ran")
        if unobserved <= 0:
            sys.exit(
                f"the recording has {unobserved} unobserved registers - the fixture's "
                "never_called() should have left some"
            )
        wrong = run(
            checker
            + ["--infer", "all-i32", "--expect-violations", str(violations)]
            + ["--expect-observed", str(observed), "--expect-unobserved", str(unobserved)]
            + ["--expect-precision", str(beat)]
        )
        if wrong.returncode != 0:
            sys.exit("the Python checker disagrees with the C++ one on all-i32")
        if not re.search(
            r"VIOLATION program [0-9a-f]+ function [0-9]+ \([A-Za-z_-]+\) register [0-9]+",
            wrong.stdout,
        ):
            sys.exit(f"the checker counted violations without naming one:\n{wrong.stdout}")
        trivial = run(
            checker
            + ["--infer", "all-boxed", "--expect-violations", "0"]
            + ["--expect-observed", str(observed), "--expect-unobserved", str(unobserved)]
            + ["--expect-precision", "0"]
        )
        if trivial.returncode != 0:
            sys.exit("all-boxed is sound by construction and the checker says otherwise")
        print(
            f"type oracle: {observed} observed registers, {unobserved} unobserved, all-i32 "
            f"{violations} soundness violations, all-boxed 0"
        )
        return

    def tally(stub):
        pattern = f"{stub} " + " ".join(f"{field} ([0-9]+)" for field in ESCAPE_FIELDS)
        found = re.search(pattern, out)
        if not found:
            sys.exit(f"the executable did not report a {stub} tally:\n{out}")
        return dict(zip(ESCAPE_FIELDS, map(int, found.groups())))

    confined, escapes = tally("all-confined"), tally("all-escapes")
    if confined["violations"] <= 0:
        sys.exit(
            f"all-confined produced {confined['violations']} violations - the checker caught nothing"
        )
    if confined["observed"] <= 0:
        sys.exit(f"the recording observed {confined['observed']} sites - nothing ran")
    if confined["unobserved"] <= 0:
        sys.exit(
            f"the recording has {confined['unobserved']} unobserved claims - the probe's "
            "never_called() should have left one"
        )
    if confined["mismatch"] != 0:
        sys.exit(
            "the recorder's coordinates disagree with its own inventory: "
            f"{confined['mismatch']} kind mismatch(es)"
        )
    # THE BOUNDED WALK IS THE ONE THAT MAKES "CONFINED" SAYABLE: unbounded must
    # have reported strictly more.
    walk = re.search(r"escaped bounded ([0-9]+) unbounded ([0-9]+)", out)
    if not walk:
        sys.exit(f"the executable did not report the bounded/unbounded A/B:\n{out}")
    if not int(walk.group(2)) > int(walk.group(1)):
        sys.exit(
            f"--unbounded reported {walk.group(2)} escapes against bounded {walk.group(1)}; "
            "the dead-window exclusion changed nothing, so it is not doing its job"
        )

    def expectations(counts, **overrides):
        counts = {**counts, **overrides}
        flags = []
        for field in ESCAPE_FIELDS[:-1]:  # `mismatch` is the recorder's, not a verdict
            flags += [f"--expect-{field}", str(counts[field])]
        return flags

    wrong = run(checker + ["--infer", "all-confined", "--max-report", "0"] + expectations(confined))
    if wrong.returncode != 0:
        sys.exit("the Python checker disagrees with the C++ one on all-confined")
    # Every violation is printed (--max-report 0) because the first ten are
    # the top level's closures, named `<script>`; and one hand-computed
    # violation is required BY NAME - `ret`'s object literal, the in-flight
    # return value - so "names a site" means the right one.
    if not re.search(
        r"VIOLATION program [0-9a-f]+ function [0-9]+ \([A-Za-z_-]+\) pc [0-9]+", wrong.stdout
    ):
        sys.exit(f"the checker counted violations without naming one:\n{wrong.stdout}")
    if not re.search(
        r"VIOLATION program [0-9a-f]+ function [0-9]+ \(ret\) pc [0-9]+ kind obj: claimed "
        r"confined, observed escaped 1/made 1 via temporaries:1",
        wrong.stdout,
    ):
        sys.exit(f"the checker did not name `ret`'s site with its route:\n{wrong.stdout}")
    trivial = run(
        checker + ["--infer", "all-escapes"] + expectations(escapes, violations=0, sound=0)
    )
    if trivial.returncode != 0:
        sys.exit("all-escapes is sound by construction and the checker says otherwise")
    if "PRECISION confined 0/" not in trivial.stdout:
        sys.exit(f"all-escapes must have zero precision:\n{trivial.stdout}")
    print(
        f"escape oracle: {confined['observed']} observed sites, {confined['unobserved']} "
        f"unobserved claims, all-confined {confined['violations']} violations, all-escapes 0, "
        f"{confined['unclaimed']} unclaimed"
    )


if __name__ == "__main__":
    main()
