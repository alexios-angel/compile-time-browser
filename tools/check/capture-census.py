#!/usr/bin/env python3
"""Count the capture operands of every ctjs.create_closure in a ctjs module.

Why this exists: CTJSOps.td and BytecodeImport.cpp cite "219 of 1,021
capture operands" as the reason a descriptor's index is an attribute rather
than a live ctjs.load_upvalue. This script reproduces that figure from a
module so those comments stay checkable (use --expect-indexed 219).

SSA names are scoped per ctjs.func, because `%0` exists in every function.
On the bootstrap corpus a flat table gives the same answer (--flat shows
this), because a capture operand is always defined earlier in the same
function, but per-function scoping is the correct reading.

What it counts, over the module `ctjs-translate --ctbrowser-js-to-ctjs` writes:

  closures     ctjs.create_closure operations
  with-list    those carrying a `captures` list at all
  operands     capture operands over all of them
  cells        operands defined by ctjs.create_cell - a from_parent_local slot,
               the binding boxed in this frame
  block-args   operands that are entry-block or block arguments - also
               from_parent_local, a binding that lives in a register
  placeholder  operands defined by `ctjs.constant #ctjs.undefined` - the slot
               the VM fills from the enclosing closure, whose descriptor index
               is on `enclosing_indices` beside it
  other        anything else, which the shapes above are supposed to exhaust

  indexed      placeholder slots whose `enclosing_indices` entry is >= 0. This
               is the number the two comments cite. Were the index an operand,
               each of these would cost compiled code an extra
               ct_aot_make_closure argument for a value known at compile time.

  load_upvalue ctjs.load_upvalue operations in the module. Writing the index
               as an operand instead raises this count by exactly `indexed`.

Usage:
  capture-census.py <module.mlir>            # or - for stdin
  capture-census.py --json out.json <module.mlir>
  capture-census.py --expect-indexed 219 <module.mlir>   # exact match required

The parse is textual on purpose: it reads a module a build already produced
instead of linking MLIR, so it works on a file from any commit. It relies on
ctjs's assembly format: one operation per line, `%name = ctjs.op ...`, and a
`captures %a, %b` clause before the attribute dictionary.
"""

import argparse
import json
import re
import sys

# `  ctjs.func private @name$3(%arg0: !ctjs.value, ...) -> ...` with two spaces
# of indent: a func is a top-level operation of the module and this dialect
# never nests one, so the indent marks where one function's SSA names end.
FUNC = re.compile(r"^  ctjs\.func\b")
# `%12 = ctjs.create_cell %11` / `%3 = ctjs.constant #ctjs.undefined`.
DEF = re.compile(r"^\s*(%[\w$.]+)\s*=\s*(\S+)")
UNDEF = re.compile(r"^\s*(%[\w$.]+)\s*=\s*ctjs\.constant\s+#ctjs\.undefined\b")
# Block arguments: the func's own entry list and every `^bb1(%7: !ctjs.value)`.
ARG = re.compile(r"(%[\w$.]+)\s*:\s*!ctjs\.value")
BLOCK = re.compile(r"^\s*\^\w+\s*\(")
CLOSURE = re.compile(r"ctjs\.create_closure\b")
CAPTURES = re.compile(r"\bcaptures\s+((?:%[\w$.]+)(?:\s*,\s*%[\w$.]+)*)")
INDICES = re.compile(r"enclosing_indices\s*=\s*array<i32\s*:?\s*([^>]*)>")
LOAD_UPVALUE = re.compile(r"\bctjs\.load_upvalue\b")


def census(lines, scoped=True):
    """Return (counts, disagreements) for the module text in `lines`.

    `disagreements` lists (line, operand, shape, index) for each indexed slot
    whose operand is not a placeholder. With `scoped` false, one SSA table is
    shared across all functions (--flat).
    """
    counts = dict(
        closures=0,
        with_list=0,
        operands=0,
        cells=0,
        block_args=0,
        placeholder=0,
        other=0,
        indexed=0,
        load_upvalue=0,
        functions=0,
    )
    disagreements = []
    kind = {}  # SSA name -> shape, reset at every ctjs.func.

    for number, line in enumerate(lines, 1):
        if FUNC.match(line):
            # --flat keeps the table, so a reader can check the docstring's
            # claim that both readings agree on bootstrap.
            if scoped:
                kind = {}
            counts["functions"] += 1
            for name in ARG.findall(line):
                kind[name] = "block_args"
        elif BLOCK.match(line):
            for name in ARG.findall(line):
                kind[name] = "block_args"

        if LOAD_UPVALUE.search(line):
            counts["load_upvalue"] += len(LOAD_UPVALUE.findall(line))

        if CLOSURE.search(line):
            counts["closures"] += 1
            captured = CAPTURES.search(line)
            if captured:
                counts["with_list"] += 1
                operands = [o.strip() for o in captured.group(1).split(",")]
                raw = INDICES.search(line)
                indices = [int(x) for x in raw.group(1).replace(",", " ").split()] if raw else []
                for i, operand in enumerate(operands):
                    counts["operands"] += 1
                    shape = kind.get(operand, "other")
                    counts[shape] += 1
                    k = indices[i] if i < len(indices) else -1
                    if k >= 0:
                        counts["indexed"] += 1
                        # A slot with an enclosing index must be a placeholder;
                        # the create_closure verifier rejects anything else, so
                        # hitting this means the module was written without
                        # running the verifier.
                        if shape != "placeholder":
                            disagreements.append((number, operand, shape, k))

        matched = DEF.match(line)
        if matched:
            kind[matched.group(1)] = (
                "placeholder"
                if UNDEF.match(line)
                else "cells"
                if matched.group(2) == "ctjs.create_cell"
                else "other"
            )
    return counts, disagreements


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("module")
    ap.add_argument("--json")
    ap.add_argument("--expect-indexed", type=int)
    ap.add_argument("--expect-operands", type=int)
    ap.add_argument(
        "--flat",
        action="store_true",
        help="share one SSA table across every function (the wrong reading)",
    )
    args = ap.parse_args()

    text = sys.stdin.read() if args.module == "-" else open(args.module).read()
    counts, disagreements = census(text.splitlines(), scoped=not args.flat)

    width = max(len(k) for k in counts)
    for key, value in counts.items():
        print(f"  {key:<{width}}  {value}")
    for number, operand, shape, k in disagreements:
        print(
            f"  line {number}: {operand} is a {shape} and enclosing_indices says {k}",
            file=sys.stderr,
        )

    if args.json:
        with open(args.json, "w") as out:
            json.dump(counts, out, indent=2, sort_keys=True)

    # An empty module is a failure, not a clean result: a stage that failed
    # upstream and left nothing on the pipe would otherwise print zeros and
    # satisfy any floor written as ">=".
    if counts["closures"] == 0:
        print(
            "capture-census: no ctjs.create_closure in the module - nothing was measured",
            file=sys.stderr,
        )
        return 2
    failed = False
    # Both expectations are exact, not floors: the cited figure measures one
    # corpus at one commit, and a count that drifts either way no longer
    # matches what the comment says.
    for name, expected in (("indexed", args.expect_indexed), ("operands", args.expect_operands)):
        if expected is not None and counts[name] != expected:
            print(f"capture-census: {name} is {counts[name]}, expected {expected}", file=sys.stderr)
            failed = True
    if disagreements:
        print(
            "capture-census: a slot names an enclosing upvalue AND carries a real "
            "operand - ctjs.create_closure's verifier refuses that shape",
            file=sys.stderr,
        )
        failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
