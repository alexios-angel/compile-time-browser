#!/usr/bin/env python3
"""OF THE CALLEES A ctjs.call_direct NAMES, HOW MANY LIFT AND HOW MANY ARE CLAIMED.

Two different questions asked of the same set. `--ctjs-resolve-globals` names a
call site; the set below is every DISTINCT symbol those sites name - the only
functions in a bundle a native call reaches. Then the whole native pipeline runs
and each of those functions is one of:

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


def run(cmd, stdin=None):
    p = subprocess.run(cmd, input=stdin, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != 0:
        sys.exit("failed: " + " ".join(cmd) + "\n" + p.stderr.decode("utf-8", "replace")[:4000])
    return p.stdout


ap = argparse.ArgumentParser()
ap.add_argument("--translate", required=True)
ap.add_argument("--opt", required=True)
ap.add_argument("--corpus", required=True)
a = ap.parse_args()

ctjs = run([a.translate, "--ctbrowser-js-to-ctjs", a.corpus])
named = run([a.opt, "--ctjs-resolve-globals"], ctjs).decode("utf-8", "replace")
sites = CALLEE.findall(named)
callees = sorted(set(sites))

low = run(
    [a.opt, "--ctjs-resolve-globals", "--ctjs-lift-to-scf", "--ctnative-lower-to-emitc"], ctjs
).decode("utf-8", "replace")
status = {}
for kind, name, rest in FUNC.findall(low):
    status[name] = (kind, rest)

claimed = lifted = refused = 0
by_reason = collections.Counter()
lifted_and_refused = collections.Counter()
for name in callees:
    kind, rest = status.get(name, ("<absent>", ""))
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
