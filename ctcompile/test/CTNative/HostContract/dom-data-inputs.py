#!/usr/bin/env python3
"""Check the host-contract proof for a Bootstrap-style `Data` registry.

The registry is a closure-private Map keyed by DOM elements that `dataEntry`
receives as parameters. The proof must accept SOURCE as written and lower it to
native storage, refuse every variant in `sources` that lets the elements or
the table escape, and refuse stale fingerprints, exhausted budgets and forged
report attributes.
"""

import json
import re

from CTNative.Exports import boundary
from CTNative.HostContract import contract as host
from CTNative.harness import FUNCTION, native_args

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
    """Compile `source` to IR and build its DOM Data manifest.

    Checks that preparation kept the source text and the function count, and
    that exactly one `dataEntry` function exists. Returns the JS path, the IR
    path, the manifest and the function count.
    """
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
    """Run the host-contract pass and check the report matches `proved`.

    A proof must count both element parameters as outer-key inputs and every
    observation store; a refusal must give a reason and expose no edges.
    Either way the pass must leave the source fingerprint unchanged.
    """
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


def native_storage(args, ir, requested, name, *, proved=True):
    """Lower `ir` to EmitC with the manifest, with and without optimization.

    When `proved`, every source function must become an emitc.func; otherwise
    lowering must fail with the DOM Data diagnostic and write no output.
    """
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
            success=proved,
        )
        if proved:
            text = output.read_text()
            if len(re.findall(r"\bemitc.func @", text)) != 6 or FUNCTION.search(text):
                raise RuntimeError(f"{name}: incomplete native Data source family")
        elif "native DOM Data source:" not in result.stderr:
            raise RuntimeError(f"{name}: missing native source diagnostic")
        elif output.exists() and output.read_text():
            raise RuntimeError(f"{name}: native refusal emitted a partial module")


def main():
    args = native_args(__doc__)
    js, ir, requested, count = prepare(args, "inputs", SOURCE)
    # The script body, dataEntry, the IIFE, the factory and its three methods.
    if count != 7:
        raise RuntimeError(f"expected seven complete source functions, got {count}")
    report, annotated = check_report(args, ir, requested, "proved", True)
    native_storage(args, ir, requested, "native-storage")

    # Run SOURCE plus OBSERVER under node and the reference interpreter from a
    # separate file, so the compiler's input stays exactly SOURCE (where
    # dataEntry is declared but never called).
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
    native_storage(args, ir, stale, "stale-native", proved=False)
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
    native_storage(args, forged, fresh, "forged-native-storage")

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
        native_storage(args, subject, fresh, name + "-native", proved=False)
    print(
        "DOM Data inputs: seven imported functions, two external keys, zero synthetic objects; "
        "Node/VM alias and source-allocation reset observations; eleven source refusals, "
        "stale/budget/forged controls; private native storage revalidated under both policies"
    )


if __name__ == "__main__":
    main()
