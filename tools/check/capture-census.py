#!/usr/bin/env python3
"""THE CENSUS OF ctjs.create_closure's CAPTURE OPERANDS - part 24 Phase 59.

WHY THIS EXISTS. Two comments in the compiler state a measured figure - "219 of
1,021 capture operands" (CTJSOps.td, BytecodeImport.cpp) - as the reason the
descriptor's index rides on an attribute instead of a live ctjs.load_upvalue.
A comment in this repository is read as specification, so a number in one that
no command reproduces is a liability: the script that produced the original
figure lived in a devbox directory and was deleted with it.

AND THE FIRST FIGURE WAS WRONG FOR A REASON THIS SCRIPT IS BUILT AROUND. An
earlier count said 427, from a flat table of SSA names over the whole module.
SSA names RESET AT EVERY ctjs.func - `%0` exists in all 574 of bootstrap's - so
a flat table has one entry per name and the last function to define `%0` decides
what every other function's `%0` was. The definitions here are scoped to the
function that contains them, which is what makes 219 reproducible.

WHAT IT COUNTS, over the module `ctjs-translate --ctbrowser-js-to-ctjs` writes:

  closures     ctjs.create_closure operations
  with-list    those carrying a `captures` list at all
  operands     capture operands over all of them
  cells        operands defined by ctjs.create_cell - a from_parent_local slot,
               the binding boxed in THIS frame
  block-args   operands that are entry-block or block arguments - also
               from_parent_local, a binding that lives in a register
  placeholder  operands defined by `ctjs.constant #ctjs.undefined` - the slot
               the VM fills from the ENCLOSING closure, whose descriptor index
               is on `enclosing_indices` beside it
  other        anything else, which the shapes above are supposed to exhaust

  indexed      placeholder slots whose `enclosing_indices` entry is >= 0. This
               is the number the two comments cite, and the one that turned
               into a runtime upvalue read per slot when the index was an
               operand: every one of them cost the BOXED tier a parked
               ct_aot_make_closure argument for a native-tier fact.

  load_upvalue ctjs.load_upvalue operations in the module. The corroboration:
               when the index was written as an operand, this count rose by
               exactly `indexed`.

USAGE
  capture-census.py <module.mlir>            # or - for stdin
  capture-census.py --json out.json <module.mlir>
  capture-census.py --expect-indexed 219 <module.mlir>   # a floor AND a ceiling

THE PARSE IS TEXTUAL, and deliberately so: this measures the module a build
already produced rather than linking MLIR, so it can be pointed at a file from
any commit - which is the whole use, since the figure is a comparison between
two of them. The shapes it reads are the ones ctjs's own assembly format prints:
one operation per line, `%name = ctjs.op ...`, and a `captures %a, %b` clause
before the attribute dictionary.
"""

import argparse
import json
import re
import sys

# `  ctjs.func private @name$3(%arg0: !ctjs.value, ...) -> ...` - two spaces of
# indent, because a func is a top-level operation of the module and nothing in
# this dialect nests one. That indent is what separates one function's SSA
# names from the next one's.
FUNC = re.compile(r"^  ctjs\.func\b")
# `%12 = ctjs.create_cell %11` / `%3 = ctjs.constant #ctjs.undefined`
DEF = re.compile(r"^\s*(%[\w$.]+)\s*=\s*(\S+)")
UNDEF = re.compile(r"^\s*(%[\w$.]+)\s*=\s*ctjs\.constant\s+#ctjs\.undefined\b")
# Block arguments: the func's own entry list and every `^bb1(%7: !ctjs.value)`.
ARG = re.compile(r"(%[\w$.]+)\s*:\s*!ctjs\.value")
BLOCK = re.compile(r"^\s*\^\w+\s*\(")
CLOSURE = re.compile(r"ctjs\.create_closure\b")
CAPTURES = re.compile(r"\bcaptures\s+((?:%[\w$.]+)(?:\s*,\s*%[\w$.]+)*)")
INDICES = re.compile(r"enclosing_indices\s*=\s*array<i32\s*:?\s*([^>]*)>")
LOAD_UPVALUE = re.compile(r"\bctjs\.load_upvalue\b")


def census(lines):
    counts = dict(closures=0, with_list=0, operands=0, cells=0, block_args=0,
                  placeholder=0, other=0, indexed=0, load_upvalue=0, functions=0)
    disagreements = []
    kind = {}  # SSA name -> shape, RESET at every ctjs.func

    for number, line in enumerate(lines, 1):
        if FUNC.match(line):
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
                indices = ([int(x) for x in raw.group(1).replace(",", " ").split()]
                           if raw else [])
                for i, operand in enumerate(operands):
                    counts["operands"] += 1
                    shape = kind.get(operand, "other")
                    counts[shape] += 1
                    k = indices[i] if i < len(indices) else -1
                    if k >= 0:
                        counts["indexed"] += 1
                        # THE SLOT SAYS TWO THINGS: ctjs.create_closure's
                        # verifier rejects exactly this, so a module that
                        # reaches here holding one was written by something
                        # that did not run it.
                        if shape != "placeholder":
                            disagreements.append((number, operand, shape, k))

        matched = DEF.match(line)
        if matched:
            kind[matched.group(1)] = ("placeholder" if UNDEF.match(line)
                                      else "cells" if matched.group(2) == "ctjs.create_cell"
                                      else "other")
    return counts, disagreements


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("module")
    ap.add_argument("--json")
    ap.add_argument("--expect-indexed", type=int)
    ap.add_argument("--expect-operands", type=int)
    args = ap.parse_args()

    text = sys.stdin.read() if args.module == "-" else open(args.module).read()
    counts, disagreements = census(text.splitlines())

    width = max(len(k) for k in counts)
    for key, value in counts.items():
        print(f"  {key:<{width}}  {value}")
    for number, operand, shape, k in disagreements:
        print(f"  line {number}: {operand} is a {shape} and enclosing_indices says {k}",
              file=sys.stderr)

    if args.json:
        with open(args.json, "w") as out:
            json.dump(counts, out, indent=2, sort_keys=True)

    # AN EMPTY MODULE IS NOT A CLEAN SHEET. A path that does not exist, or a
    # stage that failed upstream and left nothing on the pipe, would otherwise
    # print zeros and satisfy any floor written as ">=".
    if counts["closures"] == 0:
        print("capture-census: no ctjs.create_closure in the module - "
              "nothing was measured", file=sys.stderr)
        return 2
    failed = False
    # EXACT, NOT A FLOOR, on both: the figure the comments cite is a
    # measurement of one corpus at one commit, and a census that drifts either
    # way is a census that no longer says what the comment says.
    for name, expected in (("indexed", args.expect_indexed),
                           ("operands", args.expect_operands)):
        if expected is not None and counts[name] != expected:
            print(f"capture-census: {name} is {counts[name]}, expected {expected}",
                  file=sys.stderr)
            failed = True
    if disagreements:
        print("capture-census: a slot names an enclosing upvalue AND carries a real "
              "operand - ctjs.create_closure's verifier refuses that shape", file=sys.stderr)
        failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
