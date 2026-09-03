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

AND THE CEILING, WHICH THE THREE BUCKETS DO NOT SEE. Two closed-world rules
landed sound and far too coarse, and every global on every real corpus stopped
resolving: bootstrap 37 of 37 refused, p5 101 of 101, phaser 72 of 72, and not
one ctjs.call_direct emitted anywhere. THIS CHECK DID NOT NOTICE. The claimed
count did not move, because TypeInference::setToEntryState hands an entry
argument !ctnative.boxed unless a caller proves otherwise - so the functions
that stopped being reachable were the ones that take a parameter, which were
never claimed in the first place. A gate that measures only the outcome cannot
see the input to the outcome collapse.

So it measures the closed world too, from --ctjs-resolve-globals' own counters:

  globals   every name the census wrote a row for
  resolved  names bound exactly once, in the prologue, to one closure
  direct    ctjs.call sites rewritten to ctjs.call_direct

--min-resolved and --min-direct are floors on the last two, and they are the
teeth: a rule that closes the world by one clause too many fails HERE, in the
same commit, rather than in a survey six weeks later.

FOUR INVARIANTS ARE ASSERTED, not printed and admired:

  * every function that was imported and not claimed carries a reason. A
    `ctjs.func` with no `ctnative.not_native` is a function silently left
    behind - the failure part 24 SS2 exists to prevent - and it fails this
    check by name.
  * the total is not zero. A pipeline that produced nothing, or a corpus path
    that does not exist, would otherwise report "0 refused" and look perfect.
  * --ctjs-resolve-globals emitted its remark. Without it every closed-world
    count reads zero, and a corpus with no globals is indistinguishable from a
    pass whose `report` option somebody deleted.
  * the remark and the IR agree about the rewrite. A pass that counts a
    ctjs.call_direct it did not emit makes both floors below it worthless.

Usage:
  native-claims.py --translate <ctjs-translate> --opt <ctjs-opt>
                   --corpus <program.js> --name <label>
                   [--json <out.json>] [--top <n>] [--min-claimed <n>]
                   [--min-resolved <n>] [--min-direct <n>] [--timeout <seconds>]

NEGATIVE PROOFS, so both teeth stay in the suite rather than in a transcript:

  --mutate-drop-one-reason   delete one `ctnative.not_native` from the lowered
                             module before counting, which is what a function
                             silently left behind looks like
  --mutate-drop-call-direct  rename every ctjs.call_direct before counting,
                             which is what a closed world that stopped closing
                             looks like from here
  --expect-failure <text>    run this same check as a child WITHOUT this flag
                             and pass only if the child FAILED with <text> in
                             its output. Not ctest's WILL_FAIL: that passes when
                             the child crashes, segfaults or cannot find its
                             corpus, which is how a negative test rots.

Exit status is 0 only when all four invariants hold and every floor given is
met. A floor is a guard against silent narrowing: a change that refuses
functions - or globals - it used to claim fails here even though every other
gate stays green, because every other gate uses fixtures the compiler already
handles.
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
# --ctjs-resolve-globals=report=true, as a remark on the module. THE PASS'S OWN
# COUNTERS, not a count of text in the IR: `resolved` is incremented where the
# proof completes and `rewrittenCalls` where a call site is actually replaced,
# and a grep for `resolved = @` would instead count the rows of a diagnostic
# attribute. The remark exists because this LLVM package compiles pass
# statistics out - --mlir-pass-statistics prints an empty report.
_CLOSED_WORLD = re.compile(
    r"resolved (\d+) global\(s\), rewrote (\d+) call\(s\), closed (\d+) function\(s\) "
    r"over (\d+) name\(s\)"
)
# The rewrite the closed world produces, counted in the IR as a second opinion
# on the remark: the two must agree, and a disagreement is a pass that reports
# what it did not do.
_CALL_DIRECT = re.compile(r"\bctjs\.call_direct\b")
# --ctnative-lower-to-emitc=report=true, as a remark on the module. `rewrote N
# call(s)` is the closure lift's OWN count of the ctjs.call_direct it made -
# the second stage at which a callee gets named, and the one this check used to
# be blind to.
_LIFT_REPORT = re.compile(r"ctnative: lifted \d+ closure\(s\).*?rewrote (\d+) call\(s\)")
# `ctjs.globals = [{name = "x", reason = "...", resolved = ..., stores = N}]`,
# the per-name verdict the pass writes on the module. The REASONS are the
# roadmap for the ceiling in exactly the way `ctnative.not_native` is the
# roadmap for the claimed set: on 2026-09-02 every one of bootstrap's 37 said
# the same thing, and knowing WHICH thing is the difference between "the closed
# world is off" and "the program passes globalThis into a call".
_GLOBAL_REASON = re.compile(r'\{name = "(?:[^"\\]|\\.)*", reason = "((?:[^"\\]|\\.)*)"')


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
    # THE CLOSED WORLD ON ITS OWN, so what it produced can be counted before
    # the lowering consumes it. --ctnative-lower-to-emitc turns a
    # ctjs.call_direct into an emitc call, so counting them in the FINAL module
    # would report a number that falls as the backend improves.
    closed = subprocess.run(
        [argv.opt, "--ctjs-resolve-globals=report=true"],
        input=translate.stdout,
        capture_output=True,
        timeout=argv.timeout,
    )
    if closed.returncode != 0:
        sys.exit(
            f"native-claims ({argv.name}): --ctjs-resolve-globals failed on {argv.corpus}\n"
            + closed.stderr.decode("utf-8", "replace")[:4000]
        )
    resolved_ir = closed.stdout.decode("utf-8", "replace")
    if argv.mutate_drop_call_direct:
        resolved_ir = resolved_ir.replace("ctjs.call_direct", "ctjs.call_indirect_")
    remark = _CLOSED_WORLD.search(closed.stderr.decode("utf-8", "replace"))
    if not remark:
        # INVARIANT 3, and it is the one that would have caught the regression
        # quietly: no remark means the `report` option is gone, and every count
        # below would read zero and look like a corpus with no globals.
        sys.exit(
            f"native-claims ({argv.name}): --ctjs-resolve-globals=report=true emitted no "
            "remark, so the closed-world counts cannot be read. The pass's `report` option "
            "is what this check measures the ceiling with."
        )
    globals_resolved = int(remark.group(1))
    globals_rewritten = int(remark.group(2))
    globals_closed = int(remark.group(3))
    globals_total = int(remark.group(4))
    direct_in_ir = len(_CALL_DIRECT.findall(resolved_ir))
    global_reasons = collections.Counter(
        shape_of(m.group(1)) for m in _GLOBAL_REASON.finditer(resolved_ir)
    )

    lower = subprocess.run(
        [
            argv.opt,
            "--ctjs-lift-to-scf",
            "--ctnative-lower-to-emitc=report=true",
        ],
        input=closed.stdout,
        capture_output=True,
        timeout=argv.timeout,
    )
    if lower.returncode != 0:
        sys.exit(
            f"native-claims ({argv.name}): the lowering failed on {argv.corpus}\n"
            + lower.stderr.decode("utf-8", "replace")[:4000]
        )

    # THE SECOND STAGE AT WHICH A DIRECT CALL IS MADE, AND THE INSTRUMENT WAS
    # BLIND TO IT.
    #
    # `direct` above counts ctjs.call_direct after --ctjs-resolve-globals, and
    # that number was 0 on all three vendor bundles - which was read, in this
    # file and in the CMakeLists floors and in a survey written for a human, as
    # "no direct call is ever made inside a bundle". It is not what it means.
    # --ctnative-lower-to-emitc's own closure lift ALSO turns a call whose
    # callee is a ctjs.create_closure result into a ctjs.call_direct, later in
    # the pipeline, and it was doing so all along: measured on 2026-09-03 at 3
    # calls on bootstrap, 47 on p5 and 3 on phaser. Counting one stage and
    # reporting the total is how a capability that already exists gets built a
    # second time.
    #
    # So both stages are read, from each pass's own `report` remark, and both
    # are printed. They are DIFFERENT NUMBERS measuring different rewrites -
    # the resolver names a call and leaves the closure alone, the lift also
    # moves the captures into leading parameters - so they are not summed.
    lift_remark = _LIFT_REPORT.search(lower.stderr.decode("utf-8", "replace"))
    if not lift_remark:
        sys.exit(
            f"native-claims ({argv.name}): --ctnative-lower-to-emitc=report=true emitted no "
            "remark, so the direct calls the closure lift makes cannot be counted. Counting "
            "only the resolver's stage is what made three bundles read as 0 for a rewrite "
            "that was happening."
        )
    lifted_direct = int(lift_remark.group(1))

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

    # INVARIANT 4: the remark and the IR agree about the rewrite. A pass that
    # counts a rewrite it did not perform - or performs one it does not count -
    # makes every floor below it meaningless.
    if direct_in_ir != globals_rewritten:
        sys.exit(
            f"native-claims ({argv.name}): --ctjs-resolve-globals reported "
            f"{globals_rewritten} rewritten call(s) but the IR holds {direct_in_ir} "
            "ctjs.call_direct - the counter and the rewrite disagree"
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
        # THE CEILING, which the three buckets above cannot see.
        "globals": globals_total,
        "globals_resolved": globals_resolved,
        "call_direct": globals_rewritten,
        "lifted_call_direct": lifted_direct,
        "closed_functions": globals_closed,
        "global_reasons": global_reasons.most_common(),
    }


def report(result: dict, top: int) -> None:
    print(
        f"native claims ({result['name']}): {result['claimed']} of {result['total']} "
        f"functions claimed ({result['claimed_percent']}%), {result['refused']} refused "
        f"with a reason, {result['skipped']} never imported"
    )
    print(
        f"    closed world: {result['globals_resolved']} of {result['globals']} global(s) "
        f"resolved, {result['call_direct']} ctjs.call_direct, "
        f"{result['closed_functions']} function(s) closed"
    )
    print(
        f"    closure lift: {result['lifted_call_direct']} more ctjs.call_direct made inside "
        "--ctnative-lower-to-emitc (a stage this line did not used to measure)"
    )
    for reason, count in result["refusals"][:top]:
        print(f"    {count:6d}  {reason}")
    if len(result["refusals"]) > top:
        rest = sum(count for _, count in result["refusals"][top:])
        print(f"    {rest:6d}  ... and {len(result['refusals']) - top} rarer reasons")
    for reason, count in result["skips"][:top]:
        print(f"    {count:6d}  [not imported] {reason}")
    for reason, count in result["global_reasons"][:top]:
        print(f"    {count:6d}  [global] {reason}")


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
        "--min-resolved",
        type=int,
        help="fail if fewer globals resolve than this - the floor under the closed world",
    )
    parser.add_argument(
        "--min-direct",
        type=int,
        help="fail if fewer ctjs.call_direct are emitted than this",
    )
    parser.add_argument(
        "--min-lifted-direct",
        type=int,
        help="fail if the closure lift makes fewer ctjs.call_direct than this - the floor "
        "under the SECOND stage, which the resolver's count cannot see",
    )
    parser.add_argument(
        "--mutate-drop-one-reason",
        action="store_true",
        help="negative proof: delete one refusal reason, which the check must catch",
    )
    parser.add_argument(
        "--mutate-drop-call-direct",
        action="store_true",
        help="negative proof: rename every ctjs.call_direct, which the check must catch",
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
        if argv.min_resolved is not None:
            child_argv += ["--min-resolved", str(argv.min_resolved)]
        if argv.min_direct is not None:
            child_argv += ["--min-direct", str(argv.min_direct)]
        if argv.min_lifted_direct is not None:
            child_argv += ["--min-lifted-direct", str(argv.min_lifted_direct)]
        if argv.mutate_drop_one_reason:
            child_argv.append("--mutate-drop-one-reason")
        if argv.mutate_drop_call_direct:
            child_argv.append("--mutate-drop-call-direct")
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
    if argv.min_resolved is not None and result["globals_resolved"] < argv.min_resolved:
        sys.exit(
            f"native-claims ({result['name']}): resolved {result['globals_resolved']} "
            f"global(s), floor is {argv.min_resolved} - the CLOSED WORLD NARROWED. Every "
            "function that takes a parameter is unreachable until a caller proves it, so "
            "this floor is upstream of the claimed count and moves first."
        )
    if argv.min_direct is not None and result["call_direct"] < argv.min_direct:
        sys.exit(
            f"native-claims ({result['name']}): emitted {result['call_direct']} "
            f"ctjs.call_direct, floor is {argv.min_direct} - the CLOSED WORLD NARROWED."
        )
    if (
        argv.min_lifted_direct is not None
        and result["lifted_call_direct"] < argv.min_lifted_direct
    ):
        sys.exit(
            f"native-claims ({result['name']}): the closure lift made "
            f"{result['lifted_call_direct']} ctjs.call_direct, floor is "
            f"{argv.min_lifted_direct} - the CLOSURE LIFT NARROWED. This is a different "
            "number from the resolver's and a different rewrite; a regression here is "
            "invisible to --min-direct."
        )


if __name__ == "__main__":
    main()
