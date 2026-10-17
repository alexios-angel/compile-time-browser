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
CLASS_REASON = re.compile(r'ctnative\.class_reason = "((?:[^"\\]|\\.)*)"')
RESULT = re.compile(r"^\s*(%[\w#]+) = ")
STRING = re.compile(r'ctjs\.constant #ctjs\.string<"([^"]*)">')
SET = re.compile(r"ctjs\.set_property (%[\w#]+)\[(%[\w#]+)\], (%[\w#]+)")
ACCESSOR = re.compile(r"ctjs\.define_accessor \S+ on (%[\w#]+) get (%[\w#]+) set (%[\w#]+)")
INDEX = re.compile(r"[$_](\d+)$")

ENCLOSING = "capture N is filled from the enclosing closure, which did not lift: "
VALUE = "a closure used as a value: "


def normalize(reason):
    """A reason with names and numbers replaced, so that one bucket holds
    every function refused for the same thing."""
    reason = re.sub(r"`[^`]*`", "`X`", reason)
    return re.sub(r"\b\d+\b", "N", reason)


def parse(text):
    """Functions, call edges, closures and refused classes of a lowered module.

    Returns (funcs, callers, closures, classes): funcs maps a name to its
    not_native reason (None when it lowered); callers maps a name to the
    functions with a direct call of it; closures lists (creator, target,
    reason or None, class target or None), where the class is the refused
    class whose constructor or prototype the closure is stored on, or which
    the closure constructs; classes maps a constructor's target to its
    ctnative.class_reason.
    """
    funcs, callers, closures, classes = {}, collections.defaultdict(set), [], {}
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
            # Per function: SSA name -> pending closure, string, or class.
            made_at, strings, class_of = {}, {}, {}
            continue
        if current is None:
            continue
        if line.startswith("  }"):
            current = None
            continue
        for callee in DIRECT.findall(line):
            callers[callee].add(current)
        result = RESULT.match(line)
        constant = STRING.search(line)
        if result and constant:
            strings[result.group(1)] = constant.group(1)
        made = CLOSURE.search(line)
        if made:
            why = CLOSURE_REASON.search(line)
            entry = [current, int(made.group(1)), why.group(1) if why else None, None]
            cls = CLASS_REASON.search(line)
            if cls:
                classes[int(made.group(1))] = cls.group(1)
                entry[3] = int(made.group(1))
                if result:
                    class_of[result.group(1)] = int(made.group(1))
            pending.append(entry)
            if result:
                made_at[result.group(1)] = entry
            continue
        stored = SET.search(line)
        if stored:
            owner, key, value = stored.groups()
            if owner in class_of and strings.get(key) == "prototype":
                class_of[value] = class_of[owner]
            elif owner in class_of and value in made_at and made_at[value][3] is None:
                made_at[value][3] = class_of[owner]
        accessor = ACCESSOR.search(line)
        if accessor and accessor.group(1) in class_of:
            for value in accessor.group(2, 3):
                if value in made_at and made_at[value][3] is None:
                    made_at[value][3] = class_of[accessor.group(1)]
    for creator, index, why, cls in pending:
        if index in by_index:
            closures.append((creator, by_index[index], why, by_index.get(cls)))
    classes = {by_index[i]: why for i, why in classes.items() if i in by_index}
    return funcs, callers, closures, classes


def blame(funcs, callers, closures, classes):
    """The dependency graph: node -> (reason, set of nodes it depends on).

    A node is ("fn", name) for a refused function, ("closure", target) for a
    refused ctjs.create_closure of `target`, or ("class", target) for a class
    the class rule refused, whose constructor's target is `target`."""
    nodes = {("class", c): ("class: " + normalize(why), set()) for c, why in classes.items()}
    maker = {target: (creator, why) for creator, target, why, _ in closures}
    for creator, target, why, cls in closures:
        if not why:
            continue
        deps = set()
        tail = normalize(why)
        if tail.startswith(ENCLOSING) and maker.get(creator, (None, None))[1]:
            deps.add(("closure", creator))
        while tail.startswith(ENCLOSING):
            tail = tail[len(ENCLOSING) :]
        # A class member, and the constructor itself, are only values because
        # the class rule refused the class: a proved class lowers them all.
        if cls and ("class", cls) in nodes:
            deps.add(("class", cls))
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
            candidates += [("closure", t) for c, t, w, _ in closures if c == name and w]
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


def self_test():
    """Run the checks in test_root_reasons.py; exit if any fails."""
    import selftest

    if selftest.run(__file__):
        sys.exit("root-reasons self-test FAILED")


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
    funcs, callers, closures, classes = parse(text)
    rows = report(blame(funcs, callers, closures, classes))
    print_rows(rows, sum(1 for why in funcs.values() if why is not None), a.width)


if __name__ == "__main__":
    main()
