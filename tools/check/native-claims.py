#!/usr/bin/env python3
"""THE CLAIMED SET - ctcompile part 24 Phase 63 Steps 2 and 3.

Step 2 asks for per-function selection WITH A REASON, and says of the refusals:
"That list is the roadmap." Step 3 asks how many functions each backend claims.
This is both, over one JavaScript program, as a table and as JSON.

It runs the native pipeline - the importer, the closed world, the lift and the
lowering - and sorts every function of the program into exactly one of three
buckets:

  claimed   an `emitc.func`: proved, lowered, no interpreter and no collector
  refused   a `ctjs.func` still carrying `ctnative.not_native = "<reason>"`
  skipped   never imported at all, because the bytecode used an opcode the
            importer does not lower; ctjs-translate warns about each one

THE THIRD BUCKET IS THE POINT. A percentage over (claimed + refused) alone
flatters the compiler by exactly the functions it never looked at, and those
are not free: they are the ones with the constructs it likes least. The
denominator here is all three.

TWO INVARIANTS ARE ASSERTED, not printed and admired:

  * every function that was imported and not claimed carries a reason. A
    `ctjs.func` with no `ctnative.not_native` is a function silently left
    behind - the failure part 24 SS2 exists to prevent - and it fails this
    check by name.
  * the total is not zero. A pipeline that produced nothing, or a corpus path
    that does not exist, would otherwise report "0 refused" and look perfect.

Usage:
  native-claims.py --translate <ctjs-translate> --opt <ctjs-opt>
                   --corpus <program.js> --name <label>
                   [--json <out.json>] [--top <n>] [--min-claimed <n>]
                   [--timeout <seconds>]

NEGATIVE PROOFS, so both teeth stay in the suite rather than in a transcript:

  --mutate-drop-one-reason   delete one `ctnative.not_native` from the lowered
                             module before counting, which is what a function
                             silently left behind looks like
  --expect-failure <text>    run this same check as a child WITHOUT this flag
                             and pass only if the child FAILED with <text> in
                             its output. Not ctest's WILL_FAIL: that passes when
                             the child crashes, segfaults or cannot find its
                             corpus, which is how a negative test rots.

Exit status is 0 only when both invariants hold and --min-claimed, if given, is
met. --min-claimed is a FLOOR against silent narrowing: a change that refuses
functions it used to claim fails here even though every other gate stays green,
because every other gate uses fixtures the compiler already handles.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import re
import subprocess
import sys

# Set in the child of an --expect-failure run, so it can never become a parent.
_CHILD_MARK = "CTCOMPILE_NATIVE_CLAIMS_CHILD"

# A refusal names the value or site that failed, so the raw strings are almost
# all distinct. Grouping needs the SHAPE of the reason: identifiers in
# backticks, numbers, and quoted MLIR types are the parts that vary.
_IDENTIFIER = re.compile(r"`[^`]*`")
_NUMBER = re.compile(r"\d+")
_TYPE = re.compile(r"!ctnative\.\w+|!ctjs\.\w+")

# `ctjs.func @name(...) attributes {ctnative.not_native = "...", ...}`
_REFUSAL = re.compile(r'ctnative\.not_native = "((?:[^"\\]|\\.)*)"')
_CTJS_FUNC = re.compile(r"^\s*ctjs\.func\b", re.MULTILINE)
_EMITC_FUNC = re.compile(r"^\s*emitc\.func\b", re.MULTILINE)
# ctjs-translate: `function 685 is not compiled: <reason> (<where>)`
_SKIPPED = re.compile(r"function (\d+) is not compiled: ([^(\n]*)")


def shape_of(reason: str) -> str:
    """The reason with its varying parts replaced, so refusals group."""
    shaped = _IDENTIFIER.sub("`X`", reason)
    shaped = _TYPE.sub("T", shaped)
    shaped = _NUMBER.sub("N", shaped)
    return shaped.strip()


def run(argv: argparse.Namespace) -> dict:
    translate = subprocess.run(
        [argv.translate, "--ctbrowser-js-to-ctjs", argv.corpus, "--mlir-print-debuginfo"],
        capture_output=True,
        timeout=argv.timeout,
    )
    if translate.returncode != 0:
        sys.exit(
            f"native-claims ({argv.name}): the importer failed on {argv.corpus}\n"
            + translate.stderr.decode("utf-8", "replace")[:4000]
        )
    lower = subprocess.run(
        [
            argv.opt,
            "--ctjs-resolve-globals",
            "--ctjs-lift-to-scf",
            "--ctnative-lower-to-emitc",
        ],
        input=translate.stdout,
        capture_output=True,
        timeout=argv.timeout,
    )
    if lower.returncode != 0:
        sys.exit(
            f"native-claims ({argv.name}): the lowering failed on {argv.corpus}\n"
            + lower.stderr.decode("utf-8", "replace")[:4000]
        )

    module = lower.stdout.decode("utf-8", "replace")
    if argv.mutate_drop_one_reason:
        module = re.sub(r', ctnative\.not_native = "(?:[^"\\]|\\.)*"', "", module, count=1)
    claimed = len(_EMITC_FUNC.findall(module))
    left = len(_CTJS_FUNC.findall(module))
    reasons = [m.group(1) for m in _REFUSAL.finditer(module)]
    skipped = [
        (int(m.group(1)), m.group(2).strip())
        for m in _SKIPPED.finditer(translate.stderr.decode("utf-8", "replace"))
    ]

    # INVARIANT 1: nothing is left behind without a reason.
    if left != len(reasons):
        sys.exit(
            f"native-claims ({argv.name}): {left} function(s) were not lowered but only "
            f"{len(reasons)} carry `ctnative.not_native`; {left - len(reasons)} were "
            "dropped silently, which part 24 SS2 forbids"
        )
    # INVARIANT 2: the run is not vacuous.
    total = claimed + left + len(skipped)
    if total == 0:
        sys.exit(
            f"native-claims ({argv.name}): no functions at all from {argv.corpus} - "
            "an empty or unreadable corpus would report a perfect score"
        )

    by_reason = collections.Counter(shape_of(r) for r in reasons)
    by_skip = collections.Counter(reason for _, reason in skipped)
    return {
        "name": argv.name,
        "corpus": argv.corpus,
        "total": total,
        "claimed": claimed,
        "refused": left,
        "skipped": len(skipped),
        "claimed_percent": round(100.0 * claimed / total, 2),
        "refusals": by_reason.most_common(),
        "skips": by_skip.most_common(),
    }


def report(result: dict, top: int) -> None:
    print(
        f"native claims ({result['name']}): {result['claimed']} of {result['total']} "
        f"functions claimed ({result['claimed_percent']}%), {result['refused']} refused "
        f"with a reason, {result['skipped']} never imported"
    )
    for reason, count in result["refusals"][:top]:
        print(f"    {count:6d}  {reason}")
    if len(result["refusals"]) > top:
        rest = sum(count for _, count in result["refusals"][top:])
        print(f"    {rest:6d}  ... and {len(result['refusals']) - top} rarer reasons")
    for reason, count in result["skips"][:top]:
        print(f"    {count:6d}  [not imported] {reason}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--corpus", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--json")
    parser.add_argument("--top", type=int, default=8)
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument(
        "--min-claimed",
        type=int,
        help="fail if fewer functions are claimed than this - the floor against silent narrowing",
    )
    parser.add_argument(
        "--mutate-drop-one-reason",
        action="store_true",
        help="negative proof: delete one refusal reason, which the check must catch",
    )
    parser.add_argument(
        "--expect-failure",
        help="negative proof: re-run without this flag and pass only if that run failed with this text",
    )
    argv = parser.parse_args()

    if argv.expect_failure:
        # THE CHILD ARGV IS REBUILT FROM THE PARSED VALUES, never filtered out
        # of sys.argv. Filtering looked right and was catastrophically wrong:
        # `--expect-failure=TEXT` arrives as ONE token that equals neither the
        # flag nor the value, so it survived into the child, which re-ran
        # itself, forever. Each generation was killed by the OOM killer, and
        # its parent then found its own expected text quoted in the failure
        # message it printed - so the test PASSED while the machine died.
        # Belt as well as braces: the child is marked in the environment and
        # refuses to be a parent.
        if os.environ.get(_CHILD_MARK):
            sys.exit(
                "native-claims: --expect-failure reached a child process; the recursion "
                "guard fired. The parent must strip it."
            )
        child_argv = [
            sys.executable, __file__,
            "--translate", argv.translate,
            "--opt", argv.opt,
            "--corpus", argv.corpus,
            "--name", argv.name,
            "--top", str(argv.top),
            "--timeout", str(argv.timeout),
        ]
        if argv.json:
            child_argv += ["--json", argv.json]
        if argv.min_claimed is not None:
            child_argv += ["--min-claimed", str(argv.min_claimed)]
        if argv.mutate_drop_one_reason:
            child_argv.append("--mutate-drop-one-reason")
        assert "--expect-failure" not in " ".join(child_argv)
        child = subprocess.run(
            child_argv,
            capture_output=True,
            text=True,
            timeout=argv.timeout,
            env=dict(os.environ, **{_CHILD_MARK: "1"}),
        )
        output = child.stdout + child.stderr
        if child.returncode == 0:
            sys.exit(
                f"native-claims ({argv.name}): the child PASSED; it had to fail with "
                f"'{argv.expect_failure}'\n{output}"
            )
        if argv.expect_failure not in output:
            sys.exit(
                f"native-claims ({argv.name}): the child failed, but not for the reason "
                f"this test exists to prove:\n{output}"
            )
        print(
            f"native claims ({argv.name}): the child failed as it had to: {argv.expect_failure}"
        )
        return

    result = run(argv)
    report(result, argv.top)
    if argv.json:
        with open(argv.json, "w", encoding="utf-8") as out:
            json.dump(result, out, indent=2, sort_keys=True)
    if argv.min_claimed is not None and result["claimed"] < argv.min_claimed:
        sys.exit(
            f"native-claims ({result['name']}): claimed {result['claimed']}, floor is "
            f"{argv.min_claimed} - the native tier NARROWED. If that is intended, move "
            "the floor in the same commit and say why."
        )


if __name__ == "__main__":
    main()
