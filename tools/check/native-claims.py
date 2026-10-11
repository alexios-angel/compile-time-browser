#!/usr/bin/env python3
"""Report which functions of a JavaScript program the native backend compiles.

For every function it reports either that the native (EmitC) backend claimed
it or the reason it was refused; the list of refusal reasons is the to-do list
for widening the backend. Output is a table and, with --json, a JSON file.

It runs the native pipeline (importer, closed world, closure lift, lowering)
and sorts every function of the program into exactly one of five buckets:

  claimed   an `emitc.func`: proved, lowered, no interpreter and no collector
  refused   a `ctjs.func` still carrying `ctnative.not_native = "<reason>"`
  skipped   never imported, because the bytecode used an opcode the importer
            does not lower; ctjs-translate warns about each one
  pruned    an imported private function removed as provably unreachable by
            the default optimizations; counted in the denominator, never as
            claimed
  removed   a refused function the lowering removed after admission because
            nothing that can run creates or calls it (a row in
            `ctnative.unreachable_refusals`); counted in the denominator,
            never as claimed

--no-default-optimizations measures admission without those optimizations
(no precomputation, no reachability pruning).

The skipped bucket matters most: a percentage over claimed + refused alone
would flatter the compiler by exactly the functions it never looked at, which
are the ones with the constructs it handles worst. The denominator is all five.

The buckets cannot see a collapse of the closed world: if global resolution
stops working, functions that take parameters become unreachable, but those
were never claimed anyway (TypeInference::setToEntryState boxes an entry
argument unless a caller proves otherwise), so the claimed count does not
move. So this also measures the closed world, from --ctjs-resolve-globals' own
counters:

  globals   every name the census wrote a row for
  resolved  names bound exactly once, in the prologue, to one closure
  direct    ctjs.call sites rewritten to ctjs.call_direct

--min-resolved and --min-direct are floors on the last two, so a rule that
closes the world too far fails here, in the same commit that introduces it.

Five invariants are asserted, numbered as in the code below:

  1. every function that was imported and not claimed carries a reason. A
     `ctjs.func` with no `ctnative.not_native` is a function silently left
     behind, and it fails this check by name.
  2. the total is not zero. A pipeline that produced nothing, or a corpus path
     that does not exist, would otherwise report "0 refused" and look perfect.
  3. --ctjs-resolve-globals emitted its remark. Without it every closed-world
     count reads zero, and a corpus with no globals is indistinguishable from a
     pass whose `report` option somebody deleted.
  4. the remark and the IR agree about the rewrite. A pass that counts a
     ctjs.call_direct it did not emit makes both floors below it worthless.
  5. every imported function is accounted for as native, refused, pruned or
     removed.
     Removing unreachable functions must not inflate the native percentage by
     silently shrinking the source denominator.

Usage:
  native-claims.py --translate <ctjs-translate> --opt <ctjs-opt>
                   --corpus <program.js> --name <label>
                   [--json <out.json>] [--top <n>] [--min-claimed <n>]
                   [--min-resolved <n>] [--min-direct <n>]
                   [--min-lifted-direct <n>] [--no-default-optimizations]
                   [--lower-options <options>] [--timeout <seconds>]

Negative proofs, which show the check can fail (combine one with
--expect-failure; ctcompile/test/cmake/Native.cmake does so):

  --mutate-drop-one-reason   delete one `ctnative.not_native` from the lowered
                             module before counting, which is what a function
                             silently left behind looks like
  --mutate-drop-call-direct  rename every ctjs.call_direct before counting,
                             which is what a closed world that stopped closing
                             looks like from here
  --mutate-pruned-count      count one more pruned function than the module
                             reports, which source conservation must catch
  --expect-failure <text>    run this same check as a child without this flag
                             and pass only if the child failed with <text> in
                             its output. Not ctest's WILL_FAIL: that passes when
                             the child crashes, segfaults or cannot find its
                             corpus, which is how a negative test rots.

Exit status is 0 only when all five invariants hold and every floor given is
met. A floor is a guard against silent narrowing: a change that refuses
functions (or globals) it used to claim fails here even when every other check
passes, because the other checks use fixtures the compiler already handles.
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
# all distinct. Grouping needs the shape of the reason: identifiers in
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
# --ctjs-resolve-globals=report=true, as a remark on the module. These are the
# pass's own counters, not a count of text in the IR: `resolved` is incremented where the
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
# call(s)` is the closure lift's own count of the ctjs.call_direct it made: the
# second stage at which a callee gets named.
_LIFT_REPORT = re.compile(r"ctnative: lifted \d+ closure\(s\).*?rewrote (\d+) call\(s\)")
# `ctnative.reachability_summary = {removed = N : i64, ...}`: how many imported
# functions the default reachability pruning removed.
_REACHABILITY = re.compile(r"ctnative\.reachability_summary = \{removed = (\d+) : i64,")
# `ctnative.unreachable_refusals = [{function = "f$3", reason = "removed:
# unreachable (...)"}]`: the refused functions the lowering removed after
# admission, one row each.
_UNREACHABLE = re.compile(r'\{function = "(?:[^"\\]|\\.)*", reason = "removed: unreachable ')
# `ctjs.globals = [{name = "x", reason = "...", resolved = ..., stores = N}]`,
# the per-name verdict the pass writes on the module. Grouping the reasons
# shows why globals failed to resolve, the same way `ctnative.not_native`
# shows why functions were refused.
_GLOBAL_REASON = re.compile(r'\{name = "(?:[^"\\]|\\.)*", reason = "((?:[^"\\]|\\.)*)"')


def shape_of(reason: str) -> str:
    """The reason with its varying parts replaced, so refusals group."""
    shaped = _IDENTIFIER.sub("`X`", reason)
    shaped = _TYPE.sub("T", shaped)
    shaped = _NUMBER.sub("N", shaped)
    return shaped.strip()


def run(argv: argparse.Namespace) -> dict:
    """Run the pipeline on argv.corpus, assert the five invariants (exiting on a
    violation) and return the counts as a dict."""
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
    # Run the closed world on its own, so what it produced can be counted before
    # the lowering consumes it. --ctnative-lower-to-emitc turns a
    # ctjs.call_direct into an emitc call, so counting them in the final module
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
        # Invariant 3: no remark means the `report` option is gone, and every
        # count below would read zero and look like a corpus with no globals.
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
            "--ctnative-lower-to-emitc=report=true"
            + (" optimize=false" if argv.no_default_optimizations else "")
            + (" " + argv.lower_options if argv.lower_options else ""),
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

    # Direct calls are made at two stages. `direct` above counts the
    # ctjs.call_direct that --ctjs-resolve-globals makes; the closure lift inside
    # --ctnative-lower-to-emitc also rewrites a call whose callee is a
    # ctjs.create_closure result into ctjs.call_direct (and moves the captures
    # into leading parameters). Both counts come from each pass's own `report`
    # remark and are printed separately; they measure different rewrites, so they
    # are not summed.
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
    reachability = _REACHABILITY.search(module)
    if not argv.no_default_optimizations and not reachability:
        sys.exit(f"native-claims ({argv.name}): default reachability summary is missing")
    pruned = int(reachability.group(1)) if reachability else 0
    if argv.mutate_pruned_count:
        pruned += 1
    imported = len(_CTJS_FUNC.findall(resolved_ir))
    if argv.mutate_drop_one_reason:
        module = re.sub(r', ctnative\.not_native = "(?:[^"\\]|\\.)*"', "", module, count=1)
    claimed = len(_EMITC_FUNC.findall(module))
    removed = len(_UNREACHABLE.findall(module))
    left = len(_CTJS_FUNC.findall(module))
    reasons = [m.group(1) for m in _REFUSAL.finditer(module)]
    skipped = [
        (int(m.group(1)), m.group(2).strip())
        for m in _SKIPPED.finditer(translate.stderr.decode("utf-8", "replace"))
    ]

    # Invariant 1: nothing is left behind without a reason.
    if left != len(reasons):
        sys.exit(
            f"native-claims ({argv.name}): {left} function(s) were not lowered but only "
            f"{len(reasons)} carry `ctnative.not_native`; {left - len(reasons)} were "
            "dropped silently, which part 24 SS2 forbids"
        )
    # Invariant 5: every imported function is claimed, refused, pruned or
    # removed.
    if imported != claimed + left + pruned + removed:
        sys.exit(
            f"native-claims ({argv.name}): source function conservation failed: "
            f"{imported} imported, {claimed} claimed + {left} refused + {pruned} pruned + "
            f"{removed} removed"
        )
    total = imported + len(skipped)
    # Invariant 2: the run is not vacuous.
    if total == 0:
        sys.exit(
            f"native-claims ({argv.name}): no functions at all from {argv.corpus} - "
            "an empty or unreadable corpus would report a perfect score"
        )

    # Invariant 4: the remark and the IR agree about the rewrite. A pass that
    # counts a rewrite it did not perform, or performs one it does not count,
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
        "pruned": pruned,
        "removed": removed,
        "optimization_defaults": not argv.no_default_optimizations,
        "claimed_percent": round(100.0 * claimed / total, 2),
        "refusals": by_reason.most_common(),
        "skips": by_skip.most_common(),
        # The closed world, which the five buckets above cannot see.
        "globals": globals_total,
        "globals_resolved": globals_resolved,
        "call_direct": globals_rewritten,
        "lifted_call_direct": lifted_direct,
        "closed_functions": globals_closed,
        "global_reasons": global_reasons.most_common(),
    }


def report(result: dict, top: int) -> None:
    """Print the summary and the `top` most common refusal, skip and global reasons."""
    print(
        f"native claims ({result['name']}): {result['claimed']} of {result['total']} "
        f"functions claimed ({result['claimed_percent']}%), {result['refused']} refused "
        f"with a reason, {result['skipped']} never imported, {result['pruned']} pruned, "
        f"{result['removed']} removed as unreachable after admission"
    )
    print(f"    optimization defaults: {'on' if result['optimization_defaults'] else 'off'}")
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
        "--no-default-optimizations",
        action="store_true",
        help="measure admission without default precomputation and reachability pruning",
    )
    parser.add_argument(
        "--lower-options",
        help="more --ctnative-lower-to-emitc options, space-separated, such as "
        "string-entries=NAME for a function the embedder calls with String arguments",
    )
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
        "--mutate-pruned-count",
        action="store_true",
        help="negative proof: overcount pruning, which source conservation must catch",
    )
    parser.add_argument(
        "--expect-failure",
        help="negative proof: re-run without this flag and pass only if that run failed with this text",
    )
    argv = parser.parse_args()

    if argv.expect_failure:
        # Rebuild the child's argv from the parsed values rather than filtering
        # sys.argv: `--expect-failure=TEXT` arrives as one token that a filter can
        # miss, and a child that inherits the flag re-runs itself forever. The
        # environment marker is a second guard: a marked child refuses to be a
        # parent.
        if os.environ.get(_CHILD_MARK):
            sys.exit(
                "native-claims: --expect-failure reached a child process; the recursion "
                "guard fired. The parent must strip it."
            )
        child_argv = [
            sys.executable,
            __file__,
            "--translate",
            argv.translate,
            "--opt",
            argv.opt,
            "--corpus",
            argv.corpus,
            "--name",
            argv.name,
            "--top",
            str(argv.top),
            "--timeout",
            str(argv.timeout),
        ]
        if argv.json:
            child_argv += ["--json", argv.json]
        if argv.no_default_optimizations:
            child_argv.append("--no-default-optimizations")
        if argv.lower_options:
            child_argv.append(f"--lower-options={argv.lower_options}")
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
        if argv.mutate_pruned_count:
            child_argv.append("--mutate-pruned-count")
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
        print(f"native claims ({argv.name}): the child failed as it had to: {argv.expect_failure}")
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
    if argv.min_lifted_direct is not None and result["lifted_call_direct"] < argv.min_lifted_direct:
        sys.exit(
            f"native-claims ({result['name']}): the closure lift made "
            f"{result['lifted_call_direct']} ctjs.call_direct, floor is "
            f"{argv.min_lifted_direct} - the CLOSURE LIFT NARROWED. This is a different "
            "number from the resolver's and a different rewrite; a regression here is "
            "invisible to --min-direct."
        )


if __name__ == "__main__":
    main()
