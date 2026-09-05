#!/usr/bin/env python3
"""OF THE CALLEES A ctjs.call_direct NAMES, HOW MANY LIFT AND HOW MANY ARE CLAIMED.

The first report follows the fixed resolver-stage cohort across native lowering.
It is not the full call graph: native lowering also names callees. A second
report counts the remaining direct calls after lowering and traverses their
graph from the script entry. A static path does not prove runtime execution.
Each function in the resolver cohort is classified as:

  CLAIMED   an emitc.func            - proved and lowered
  LIFTED    a ctjs.func carrying `ctnative.captures` - the closure lift took its
            captures into leading parameters, whether or not the body proved
  REFUSED   a ctjs.func with `ctnative.not_native = "<reason>"`

A function can be LIFTED and still REFUSED: a lifted capture is a parameter that
still needs a native carrier, and most module-scope bindings in a bundle are
objects and functions. The two counts are reported separately for that reason.
"""
import argparse
import collections
import re
import subprocess
import sys

CALLEE = re.compile(r"ctjs\.call_direct @([A-Za-z0-9_$]+)")
FUNC = re.compile(r"(emitc\.func|ctjs\.func)[^\n]*?@([A-Za-z0-9_$]+)\b([^\n]*)")
REASON = re.compile(r'ctnative\.not_native = "((?:[^"\\]|\\.)*)"')
DIRECT = re.compile(r"\b(ctjs\.call_direct|(?:emitc\.)?call) @([A-Za-z0-9_$]+)")


def run(cmd, stdin=None):
    p = subprocess.run(cmd, input=stdin, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != 0:
        sys.exit("failed: " + " ".join(cmd) + "\n" + p.stderr.decode("utf-8", "replace")[:4000])
    return p.stdout


ap = argparse.ArgumentParser()
ap.add_argument("--translate", required=True)
ap.add_argument("--opt", required=True)
ap.add_argument("--corpus", required=True)
ap.add_argument("--require-entry-callee", action="append", default=[])
ap.add_argument("--expect-unreachable", action="append", default=[])
a = ap.parse_args()

ctjs = run([a.translate, "--ctbrowser-js-to-ctjs", a.corpus, "--mlir-print-debuginfo"])
named = run([a.opt, "--ctjs-resolve-globals"], ctjs).decode("utf-8", "replace")
sites = CALLEE.findall(named)
callees = sorted(set(sites))

low = run(
    [a.opt, "--ctjs-resolve-globals", "--ctjs-lift-to-scf", "--ctnative-lower-to-emitc"], ctjs
).decode("utf-8", "replace")
status = {}
for kind, name, rest in FUNC.findall(low):
    status[name] = (kind, rest)


def emitted_name(name):
    if name in status:
        return name
    return "main" if name == "_script_$0" else name.replace("$", "_")


claimed = lifted = refused = 0
by_reason = collections.Counter()
lifted_and_refused = collections.Counter()
for name in callees:
    actual = emitted_name(name)
    if actual not in status:
        sys.exit(f"named-callees: resolver callee {name} is absent after native lowering")
    kind, rest = status[actual]
    is_lift = "ctnative.captures" in rest
    why = REASON.search(rest)
    why = why.group(1) if why else ""
    if kind == "emitc.func":
        claimed += 1
        mark = "CLAIMED"
    else:
        refused += 1
        by_reason[why or "<no reason>"] += 1
        mark = "REFUSED"
        if is_lift:
            lifted_and_refused[why or "<no reason>"] += 1
    if is_lift:
        lifted += 1
    print(f"  {name:<12} {mark:<8} {'LIFTED' if is_lift else '      '}  {why}")

print(f"named callees: {len(callees)} distinct over {len(sites)} ctjs.call_direct site(s)")
print(f"  CLAIMED {claimed}   LIFTED {lifted}   REFUSED {refused}")
print("  refusal reasons:")
for why, n in by_reason.most_common():
    print(f"    {n:>3}  {why}")
if lifted_and_refused:
    print("  of the REFUSED, those the lift DID take (the next lever):")
    for why, n in lifted_and_refused.most_common():
        print(f"    {n:>3}  {why}")

graph = collections.defaultdict(set)
direct_counts = collections.Counter()
caller = None
for line in low.splitlines():
    header = FUNC.search(line)
    if header:
        caller = header.group(2)
    direct = DIRECT.search(line)
    if caller and direct:
        kind = "ctjs.call_direct" if direct.group(1) == "ctjs.call_direct" else "emitc.call"
        direct_counts[kind] += 1
        graph[caller].add(direct.group(2))

entry = "main" if "main" in status else "_script_$0"
reachable = set()
pending = [entry] if entry in status else []
while pending:
    current = pending.pop()
    if current in reachable:
        continue
    reachable.add(current)
    pending.extend(graph[current])

targets = {target for callees in graph.values() for target in callees}
print(
    f"post-native graph: {len(targets)} distinct targets over "
    f"{direct_counts['ctjs.call_direct']} ctjs.call_direct and "
    f"{direct_counts['emitc.call']} emitc.call site(s)"
)
print(f"  entry {entry}: {len(reachable)} function(s) on static direct-call paths")
print("  reachable: " + ", ".join(sorted(reachable)))

for required in a.require_entry_callee:
    if emitted_name(required) not in reachable:
        sys.exit(f"named-callees: {required} is not reachable by direct calls from {entry}")
for absent in a.expect_unreachable:
    target = emitted_name(absent)
    if target not in status:
        sys.exit(f"named-callees: unreachable witness {absent} does not exist")
    if target in reachable:
        sys.exit(f"named-callees: expected {absent} to be unreachable from {entry}")
