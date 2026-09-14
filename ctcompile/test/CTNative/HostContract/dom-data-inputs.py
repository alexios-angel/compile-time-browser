#!/usr/bin/env python3
"""Prove imported DOM Data inputs while native storage remains explicitly refused."""

import argparse
import json
from pathlib import Path
import re

from CTNative.Exports import boundary
from CTNative.HostContract import contract as host

SOURCE = """function dataEntry(element, other) {
  host = {};
  (function(factory) {
    host.slot = factory();
  })(function() {
    const state = new Map();
    return {
      set(key, value) { state.set(key, value); return state.size; },
      get(key) { return state.has(key); },
      remove(key) { state.delete(key); return state.size; }
    };
  });
  traceEntered = 1;
  traceBefore = host.slot.get(element);
  host.slot.set(element, 42);
  traceOther = host.slot.get(other);
  host.slot.remove(other);
  traceAfter = host.slot.get(element);
}
"""
FUNCTION = re.compile(r"\bctjs\.func (?:private )?@([^ (]+)\(")
OBSERVATIONS = ["traceAfter", "traceBefore", "traceEntered", "traceOther"]
OBSERVER = """
var first = {}, second = {};
dataEntry(first, first);
var traceEqual = !traceBefore && traceOther && !traceAfter ? 1 : 0;
dataEntry(first, second);
var traceDistinct = !traceBefore && !traceOther && traceAfter ? 1 : 0;
dataEntry(first, second);
var traceReset = !traceBefore && !traceOther && traceAfter ? 1 : 0;
"""
EXPECTED = """traceAfter=true
traceBefore=false
traceDistinct=1
traceEntered=1
traceEqual=1
traceOther=false
traceReset=1
"""
NODE = r"""const fs = require('node:fs');
const vm = require('node:vm');
const context = vm.createContext({});
vm.runInContext(fs.readFileSync(process.argv[1], 'utf8'), context);
for (const key of Object.keys(context).filter(key => key.startsWith('trace')).sort()) {
  const value = context[key];
  if (typeof value !== 'boolean' && typeof value !== 'number') throw new Error(key);
  process.stdout.write(key + '=' + String(value) + '\n');
}
"""


def prepare(args, name, source):
    js, ir, count = boundary.prepare(args, name, source.removesuffix("\n"))
    if js.read_text() != source:
        raise RuntimeError(f"{name}: preparation changed the original source")
    entries = [name for name in FUNCTION.findall(ir.read_text()) if name.startswith("dataEntry")]
    if len(entries) != 1 or len(FUNCTION.findall(ir.read_text())) != count:
        raise RuntimeError(f"{name}: imported entry/function census changed")
    requested = host.manifest(args.opt, ir)
    requested.update(
        provider="ctbrowser-dom-data-session-v1",
        entry=entries[0],
        element_parameters=[0, 1],
        observations=OBSERVATIONS,
        initial_intrinsics=["Map", "Array"],
    )
    return js, ir, requested, count


def check_report(args, ir, requested, name, proved, *, options=""):
    report, output, _ = host.analyze(args.opt, ir, requested, args.work / name, options=options)
    if report["proved"] != proved or report["provider"] != "ctbrowser-dom-data-session-v1":
        raise RuntimeError(f"{name}: wrong imported input proof: {report}")
    if report["outer_key_objects"] != 0 or report["outer_key_inputs"] != (2 if proved else 0):
        raise RuntimeError(f"{name}: DOM inputs became allocations or partial input evidence")
    if proved:
        if (
            report["observation_stores"] != len(OBSERVATIONS)
            or len(report["slots"]) != 1
            or report["slots"][0]["proved_edges"] != 5
        ):
            raise RuntimeError(f"{name}: incomplete root/observation proof: {report}")
    elif not report["reason"] or any(slot["proved_edges"] for slot in report["slots"]):
        raise RuntimeError(f"{name}: failed proof exposed usable publication edges")
    if host.fingerprint(args.opt, output) != host.fingerprint(args.opt, ir):
        raise RuntimeError(f"{name}: input proof changed the original source")
    return report, output


def native_refusal(args, ir, requested, name):
    config = args.work / f"{name}.json"
    config.write_text(json.dumps(requested, indent=2) + "\n")
    for optimize in (False, True):
        output = args.work / f"{name}-{optimize}.native.mlir"
        result = host.run(
            [
                args.opt,
                str(ir),
                "--ctnative-lower-to-emitc="
                f"host-manifest={config} optimize={str(optimize).lower()}",
                "-o",
                str(output),
            ],
            success=False,
        )
        if "requires storage confined to its document owner" not in result.stderr:
            raise RuntimeError(f"{name}: missing native lifetime diagnostic")
        if output.exists() and output.read_text():
            raise RuntimeError(f"{name}: native refusal emitted a partial module")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt", "node", "reference"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    js, ir, requested, count = prepare(args, "inputs", SOURCE)
    if count != 7:
        raise RuntimeError(f"expected seven complete source functions, got {count}")
    report, annotated = check_report(args, ir, requested, "proved", True)
    native_refusal(args, ir, requested, "native-refused")

    # Observe a separate harness. The compiler's input retains the uninvoked
    # declaration, its complete method family and every source allocation.
    observed = args.work / "observed.js"
    observed.write_text(js.read_text() + OBSERVER)
    for command in ([args.node, "-e", NODE, str(observed)], [args.reference, str(observed)]):
        result = host.run(command)
        if result.stdout != EXPECTED:
            raise RuntimeError(f"source alias/reentry observations changed: {result.stdout}")

    stale = dict(requested, module_sha256="0" * 64)
    refused, _ = check_report(args, ir, stale, "stale", False)
    if "fingerprint mismatch" not in refused["reason"]:
        raise RuntimeError("stale input proof lost its fingerprint diagnostic")
    refused, _ = check_report(args, ir, requested, "budget", False, options="max-steps=0")
    if "budget" not in refused["reason"]:
        raise RuntimeError("an incomplete input census exposed proof")
    forged = args.work / "forged.mlir"
    text, changed = re.subn(
        r"ctnative.host_outer_key_inputs = 2 : i64",
        "ctnative.host_outer_key_inputs = 99 : i64",
        annotated.read_text(),
    )
    if changed != 1:
        raise RuntimeError("forged input control lost its actual report attribute")
    forged.write_text(text)
    fresh = dict(requested, module_sha256=host.fingerprint(args.opt, forged))
    rechecked, _ = check_report(args, forged, fresh, "forged-fresh", True)
    if rechecked != report:
        raise RuntimeError("forged reports changed the live source proof")
    native_refusal(args, forged, fresh, "forged-native-refused")

    sources = {
        "wrapper-effect": "traceOutside = 1;\n" + SOURCE,
        "entry-call": SOURCE + "dataEntry({}, {});\n",
        "entry-alias": SOURCE + "var savedEntry = dataEntry;\n",
        "entry-replaced": SOURCE + "dataEntry = 0;\n",
        "entry-read-inactive": SOURCE.replace(
            "traceEntered = 1;", "if (false) { var observedEntry = dataEntry; } traceEntered = 1;"
        ),
        "table-extracted": SOURCE.replace(
            "traceEntered = 1;", "escapedTable = host.slot; traceEntered = 1;"
        ),
        "input-stored": SOURCE.replace("traceEntered = 1;", "escaped = element; traceEntered = 1;"),
        "input-payload": SOURCE.replace(
            "host.slot.set(element, 42)", "host.slot.set(element, element)"
        ),
        "input-returned": SOURCE.removesuffix("}\n") + "  return element;\n}\n",
        "method-extracted": SOURCE.replace(
            "traceEntered = 1;", "escaped = host.slot.get; traceEntered = 1;"
        ),
        "outer-snapshot": SOURCE.replace(
            "return state.has(key);", "Array.from(state.keys()); return state.has(key);"
        ),
    }
    for name, source in sources.items():
        _, subject, fresh, _ = prepare(args, name, source)
        check_report(args, subject, fresh, name + "-proof", False)
    print(
        "DOM Data inputs: seven imported functions, two external keys, zero synthetic objects; "
        "Node/VM alias and source-allocation reset observations; eleven source refusals, "
        "stale/budget/forged controls; native storage refused under both policies"
    )


if __name__ == "__main__":
    main()
