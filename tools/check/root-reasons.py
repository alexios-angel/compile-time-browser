#!/usr/bin/env python3
"""Rank native-lowering refusal reasons by the refused functions each one blocks.

Native admission is first past the post: a function records one
`ctnative.not_native` reason, and many reasons only say that something else
was refused first. This tool follows those reasons back to the refusals that
stand on their own (the roots), and counts the refused functions behind each
root reason. Fixing a reason that is not a root gains nothing until its root
is fixed.

A refused function F depends on:
  - its direct callers, when its reason is a parameter or capture that no
    caller proves (a callee cannot be typed while its callers are refused);
  - the ctjs.create_closure that makes it, when that closure was refused and
    F's reason is a parameter, `this`, new.target or its own closure (an
    unlifted closure has no call site that passes them);
  - a refused ctjs.create_closure inside F, when F's reason is that closure's
    refusal ("a closure used as a value: ...", "uses `this` - ...").
A refused create_closure depends on its enclosing closure when its reason is
"capture N is filled from the enclosing closure"; otherwise it is a root.
Every other refused function is a root. A dependent with no refused
dependency is a root too, under its own reason.

For each root reason (names and numbers normalized) the report prints the
roots, every refused function at or behind them ("blocks"), and the ones whose
every root has that reason ("solely"): fixing the reason can free at most the
"solely" functions, and only if nothing behind them refuses next.
"""

import argparse
import collections
import re
import subprocess
import sys

# The same IR patterns tools/check/named-callees.py matches.
FUNC = re.compile(r"^  (emitc\.func|ctjs\.func) (?:private )?@([A-Za-z0-9_$]+)\(([^\n]*)")
REASON = re.compile(r'ctnative\.not_native = "((?:[^"\\]|\\.)*)"')
DIRECT = re.compile(r"\b(?:ctjs\.call_direct|emitc\.call) @([A-Za-z0-9_$]+)")
CLOSURE = re.compile(r"ctjs\.create_closure %arg2\[(\d+)\]")
CLOSURE_REASON = re.compile(r'ctnative\.closure_reason = "((?:[^"\\]|\\.)*)"')
INDEX = re.compile(r"[$_](\d+)$")

ENCLOSING = "capture N is filled from the enclosing closure, which did not lift: "
VALUE = "a closure used as a value: "


def normalize(reason):
    """A reason with names and numbers replaced, so that one bucket holds
    every function refused for the same thing."""
    reason = re.sub(r"`[^`]*`", "`X`", reason)
    return re.sub(r"\b\d+\b", "N", reason)


def parse(text):
    """Functions, call edges and closures of a lowered module.

    Returns (funcs, callers, closures): funcs maps a name to its not_native
    reason (None when it lowered); callers maps a name to the functions with
    a direct call of it; closures lists (creator, target name, reason or None).
    """
    funcs, callers, closures = {}, collections.defaultdict(set), []
    by_index, pending = {}, []
    current = None
    for line in text.splitlines():
        header = FUNC.match(line)
        if header:
            current = header.group(2)
            why = REASON.search(header.group(3))
            funcs[current] = why.group(1) if why and header.group(1) == "ctjs.func" else None
            index = INDEX.search(current)
            if index:
                by_index[int(index.group(1))] = current
            continue
        if current is None:
            continue
        if line.startswith("  }"):
            current = None
            continue
        for callee in DIRECT.findall(line):
            callers[callee].add(current)
        made = CLOSURE.search(line)
        if made:
            why = CLOSURE_REASON.search(line)
            pending.append((current, int(made.group(1)), why.group(1) if why else None))
    for creator, index, why in pending:
        if index in by_index:
            closures.append((creator, by_index[index], why))
    return funcs, callers, closures


def blame(funcs, callers, closures):
    """The dependency graph: node -> (reason, set of nodes it depends on).

    A node is ("fn", name) for a refused function or ("closure", target) for a
    refused ctjs.create_closure of `target`."""
    nodes = {}
    maker = {target: (creator, why) for creator, target, why in closures}
    for creator, target, why in closures:
        if not why:
            continue
        deps = set()
        tail = normalize(why)
        if tail.startswith(ENCLOSING) and maker.get(creator, (None, None))[1]:
            deps.add(("closure", creator))
        while tail.startswith(ENCLOSING):
            tail = tail[len(ENCLOSING) :]
        nodes[("closure", target)] = (tail, deps)
    for name, why in funcs.items():
        if why is None:
            continue
        reason = normalize(why)
        deps = set()
        made = ("closure", name) if ("closure", name) in nodes else None
        if re.match(r"(shared )?(parameter|capture) N is \S+ - no caller proves it", reason):
            deps |= {("fn", c) for c in callers.get(name, ()) if funcs.get(c) is not None}
            if made:
                deps.add(made)
        elif reason in ("uses `X`", "uses new.target", "uses its own closure"):
            if made:
                deps.add(made)
        elif reason.startswith((VALUE, "uses `X` - " + VALUE)):
            inner = reason.split(VALUE, 1)[1]
            while inner.startswith(ENCLOSING):
                inner = inner[len(ENCLOSING) :]
            # "uses `this` - ..." names the receiver lift's refusal of F's own
            # closure; the bare form names a refused closure made inside F.
            candidates = [made] if reason.startswith("uses") and made else []
            candidates += [("closure", t) for c, t, w in closures if c == name and w]
            for candidate in candidates:
                if nodes[candidate][0] == inner:
                    deps.add(candidate)
                    break
        nodes[("fn", name)] = (reason, deps)
    return nodes


def roots_of(nodes):
    """Each node's set of root nodes, following dependencies. A cycle (mutual
    recursion) contributes nothing from its back edge; a result computed
    through one is not cached, so every node is answered from its own start."""
    memo = {}

    def visit(node, stack):
        if node in memo:
            return memo[node], False
        if node in stack:
            return set(), True
        stack.add(node)
        found, cyclic = set(), False
        for dep in nodes[node][1]:
            if dep in nodes:
                more, hit = visit(dep, stack)
                found |= more
                cyclic |= hit
        stack.discard(node)
        if not found:
            found = {node}
        if not cyclic:
            memo[node] = found
        return found, cyclic

    return {node: visit(node, set())[0] for node in nodes}


def report(nodes):
    """Rows (reason, roots, blocks, solely) sorted by solely, then blocks."""
    rooted = roots_of(nodes)
    roots = collections.Counter()
    blocks = collections.Counter()
    solely = collections.Counter()
    for node, found in rooted.items():
        if found == {node}:
            roots[nodes[node][0]] += 1
        if node[0] != "fn":
            continue
        reasons = {nodes[r][0] for r in found}
        for reason in reasons:
            blocks[reason] += 1
        if len(reasons) == 1:
            solely[next(iter(reasons))] += 1
    rows = [(reason, roots[reason], blocks[reason], solely[reason]) for reason in roots]
    rows.sort(key=lambda row: (-row[3], -row[2], row[0]))
    return rows


def print_rows(rows, refused, width):
    print(f"refused functions: {refused}")
    print(f"{'solely':>6} {'blocks':>6} {'roots':>5}  root reason")
    for reason, nroots, nblocks, nsolely in rows:
        print(f"{nsolely:>6} {nblocks:>6} {nroots:>5}  {reason[:width]}")


PLANTED = """\
  ctjs.func @_script_$0(%arg0) -> !ctjs.value {
    %1 = ctjs.create_closure %arg2[1] this %0 {ctnative.lifted}
    %2 = ctjs.call_direct @a$1(%x)
    %3 = ctjs.create_closure %arg2[5] this %0 {ctnative.closure_reason = "it is passed as an argument"}
    %4 = ctjs.call_direct @f$6(%x)
    %5 = ctjs.create_closure %arg2[9] this %0 {ctnative.closure_reason = "it is stored into an object or an array"}
  }
  ctjs.func @a$1(%arg0) -> !ctjs.value attributes {ctnative.not_native = "a regular expression literal"} {
    %1 = ctjs.call_direct @b$2(%x)
    %2 = ctjs.call_direct @c$3(%x)
  }
  ctjs.func private @b$2(%arg0) -> !ctjs.value attributes {ctnative.not_native = "parameter 0 is !ctnative.boxed - no caller proves it (x)"} {
    %1 = ctjs.call_direct @c$3(%x)
  }
  ctjs.func private @c$3(%arg0) -> !ctjs.value attributes {ctnative.not_native = "parameter 1 is !ctnative.boxed - no caller proves it (x)"} {
    %1 = ctjs.call_direct @d$4(%x)
  }
  emitc.func @d_4(%arg0) {
  }
  ctjs.func @e$5(%arg0) -> !ctjs.value attributes {ctnative.not_native = "uses its own closure"} {
    %1 = ctjs.create_closure %arg2[7] this %arg0 {ctnative.closure_reason = "capture 0 is filled from the enclosing closure, which did not lift: it is passed as an argument"}
  }
  ctjs.func @f$6(%arg0) -> !ctjs.value attributes {ctnative.not_native = "uses `this` - a closure used as a value: it is stored into an object or an array"} {
    %1 = ctjs.create_closure %arg2[8] this %arg0 {ctnative.closure_reason = "it is stored into an object or an array"}
    %2 = ctjs.call_direct @c$3(%x)
  }
  ctjs.func @g$7(%arg0) -> !ctjs.value attributes {ctnative.not_native = "parameter 0 is !ctnative.boxed - no caller proves it (x)"} {
  }
  ctjs.func @h$8(%arg0) -> !ctjs.value attributes {ctnative.not_native = "uses `this`"} {
  }
  ctjs.func @i$9(%arg0) -> !ctjs.value attributes {ctnative.not_native = "uses `this` - a closure used as a value: it is stored into an object or an array"} {
  }
"""

# Expected rows for PLANTED: (reason, roots, blocks, solely).
#  - a$1 is a root (regex); b$2 is behind a$1 only; c$3 is behind a$1 (through
#    a$1 and b$2) and f$6's closure, so it is blocked by both and solely by none.
#  - The closure of e$5 is a root (passed as an argument); e$5 and g$7 (made by
#    a closure filled from e$5's) are behind it. d$4 lowered and is no node.
#  - f$6 and h$8 are behind the closure of h$8 (stored into an object), and
#    i$9 behind its own closure, refused for the same reason: two roots.
EXPECTED = [
    ("it is stored into an object or an array", 2, 4, 3),
    ("a regular expression literal", 1, 3, 2),
    ("it is passed as an argument", 1, 2, 2),
]


def self_test():
    funcs, callers, closures = parse(PLANTED)
    rows = report(blame(funcs, callers, closures))
    if rows != EXPECTED:
        sys.exit(f"root-reasons self-test FAILED\n  got      {rows}\n  expected {EXPECTED}")
    print("root-reasons self-test passed")


def run(cmd, stdin=None):
    p = subprocess.run(cmd, input=stdin, capture_output=True, check=False)
    if p.returncode != 0:
        sys.exit("failed: " + " ".join(cmd) + "\n" + p.stderr.decode("utf-8", "replace")[:4000])
    return p.stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--ir", help="an already lowered module (ctjs-opt output)")
    ap.add_argument("--translate")
    ap.add_argument("--opt")
    ap.add_argument("--corpus")
    ap.add_argument("--width", type=int, default=160, help="reason column width")
    a = ap.parse_args()
    if a.self_test:
        self_test()
        return
    if a.ir:
        with open(a.ir, encoding="utf-8", errors="replace") as f:
            text = f.read()
    elif a.translate and a.opt and a.corpus:
        ctjs = run([a.translate, "--ctbrowser-js-to-ctjs", a.corpus])
        pipeline = ["--ctjs-resolve-globals", "--ctjs-lift-to-scf", "--ctnative-lower-to-emitc"]
        text = run([a.opt, *pipeline], ctjs).decode("utf-8", "replace")
    else:
        ap.error("give --self-test, --ir, or --translate, --opt and --corpus")
    funcs, callers, closures = parse(text)
    rows = report(blame(funcs, callers, closures))
    print_rows(rows, sum(1 for why in funcs.values() if why is not None), a.width)


if __name__ == "__main__":
    main()
