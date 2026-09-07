#!/usr/bin/env python3
"""Exercise live direct-callee payload verification and its finite bounds."""

import argparse
import json
from pathlib import Path
import re
import subprocess


MISMATCH = "requires each escaping callee throw to match the homogeneous catch payload"
UNDEFINED = "requires a defined EmitC callee for each protected direct call"
RECURSION = "cannot prove the payload of a recursive protected direct call"
DEPTH = "protected direct-call payload depth limit exhausted"
WORK = "protected direct-call payload work budget exhausted"


def check(args, name, source, reason=None):
    path = args.work / f"{name}.mlir"
    output = args.work / f"{name}.verified.mlir"
    path.write_text(source)
    result = subprocess.run([args.opt, str(path), "-o", str(output)], text=True,
                            capture_output=True, timeout=30)
    if reason is None:
        if result.returncode:
            raise RuntimeError(f"{name}: unexpectedly refused\n{result.stdout}{result.stderr}")
    elif result.returncode == 0 or reason not in result.stderr:
        raise RuntimeError(f"{name}: expected {reason!r}\n{result.stdout}{result.stderr}")
    return {"name": name, "accepted": reason is None, "reason": reason}


def helper(name, body, *, attributes=""):
    return (f"  emitc.func @{name}(){attributes} {{\n" + body +
            "    emitc.return\n  }\n")


def thrown(kind="f64"):
    value = "true" if kind == "i1" else "42.0"
    return (f'    %value = emitc.literal "{value}" : {kind}\n'
            f"    ctnative.cpp_throw %value : {kind}\n")


def guard(name="leaf", kind="f64", *, repeats=1):
    calls = "".join(f"      emitc.call @{name}() : () -> ()\n" for _ in range(repeats))
    return ("  emitc.func @guard() {\n    ctnative.cpp_try {\n" + calls +
            "      ctnative.cpp_try_end\n    } catch {\n" +
            f"    ^bb0(%caught: {kind}):\n" +
            "      ctnative.cpp_try_end\n    }\n    emitc.return\n  }\n")


def module(*pieces):
    return "module {\n" + "".join(pieces) + "}\n"


def chain(size):
    pieces = [helper(f"helper{index}",
                     f"    emitc.call @helper{index + 1}() : () -> ()\n")
              for index in range(size - 1)]
    pieces.append(helper(f"helper{size - 1}", thrown()))
    pieces.append(guard("helper0"))
    return module(*pieces)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--opt", required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    reports = []

    def run(name, source, reason=None):
        reports.append(check(args, name, source, reason))

    direct = module(helper("leaf", thrown()), guard())
    run("direct", direct)
    run("direct-mismatch", module(helper("leaf", thrown("i1")), guard()), MISMATCH)
    middle = helper("middle", "    emitc.call @leaf() : () -> ()\n")
    run("transitive", module(helper("leaf", thrown()), middle, guard("middle")))
    run("transitive-mismatch", module(helper("leaf", thrown("i1")), middle,
                                      guard("middle")), MISMATCH)
    run("unresolved", module(guard("missing")), UNDEFINED)
    run("external", module("  emitc.func @leaf()\n", guard()), UNDEFINED)
    run("transitive-unresolved", module(middle, guard("middle")), UNDEFINED)
    run("recursive", module(helper("leaf", "    emitc.call @leaf() : () -> ()\n"),
                             guard()), RECURSION)
    run("mutual-recursion", module(helper("leaf", "    emitc.call @middle() : () -> ()\n"),
                                    middle, guard()), RECURSION)
    run("depth-32", chain(32))
    run("depth-33", chain(33), DEPTH)
    work = "".join(f'    %value{index} = emitc.literal "0.0" : f64\n'
                   for index in range(4096))
    run("work-exhausted", module(helper("leaf", work), guard()), WORK)
    # One live query can reuse a completed callee; repeated calls do not
    # repeatedly consume the helper body's operations. Proof never survives
    # a verifier call or a change to a different handler's payload type.
    reused = "".join(f'    %value{index} = emitc.literal "0.0" : f64\n'
                     for index in range(1024))
    run("repeated-callee", module(helper("leaf", reused), guard(repeats=100)))
    other_guard = guard(kind="i1").replace("@guard", "@other_guard")
    run("distinct-handlers", module(helper("leaf", thrown()), guard(), other_guard), MISMATCH)

    # Reverify the successful source with an annotation a downstream pass
    # might leave, then mutate the actual throw while retaining that marker.
    marked = module(helper("leaf", thrown(),
                           attributes=" attributes {ctnative.exception_payload = f64}"), guard())
    run("marked", marked)
    marker_output = (args.work / "marked.verified.mlir").read_text()
    changed, literal_count = re.subn(r'((?:emitc\.)?literal) "42\.0" : f64',
                                     r'\1 "true" : i1', marker_output)
    changed, throw_count = re.subn(r'(ctnative\.cpp_throw %[-\w.$]+) : f64',
                                   r'\1 : i1', changed)
    if literal_count != 1 or throw_count != 1:
        raise RuntimeError("late mutation did not change exactly its literal and throw")
    run("late-mutation", changed, MISMATCH)
    run("late-mutation-rerun", changed, MISMATCH)

    fixture = args.fixture.read_text()
    # The fixture's helper catches boolean locally and rethrows string from
    # its catch. Mutating that outgoing throw must fail the caller's handler.
    needle = "      ctnative.cpp_throw %payload : !emitc.opaque<\"std::string\">"
    changed = fixture.replace(needle, "      ctnative.cpp_throw %boolean : i1")
    if changed == fixture:
        raise RuntimeError("rethrow mutation control lost its source operation")
    run("catch-rethrow-mismatch", changed, MISMATCH)

    # Executed by the lit driver: taking state at try entry instead of the
    # call site produces 32 instead of 42 and must fail the same observation.
    needle = "emitc.assign %before : f64 to %state : !emitc.lvalue<f64>"
    wrong = fixture.replace(needle, "emitc.assign %zero : f64 to %state : !emitc.lvalue<f64>")
    if wrong == fixture:
        raise RuntimeError("wrong-state control lost its pre-call snapshot")
    (args.work / "wrong-state.mlir").write_text(wrong)
    (args.work / "report.json").write_text(json.dumps(reports, indent=2) + "\n")
    accepted = sum(row["accepted"] for row in reports)
    print(f"native call exception verifier: {accepted} accepted, "
          f"{len(reports) - accepted} refused controls")


if __name__ == "__main__":
    main()
