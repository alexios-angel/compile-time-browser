#!/usr/bin/env python3
"""Audit host semantics around the exact source-derived Bootstrap Data wrapper.

This is a JavaScript host-boundary oracle, not a native coverage test. The UMD
wrapper and Data declaration use bootstrap-data-probe.py's checked extraction.
Every scenario preserves that fragment; none substitutes an isolated factory.
Node checks ECMAScript observations. An optional ctbrowser reference run records
differences explicitly, and --require-reference-match turns them into failures.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys


_SPEC = importlib.util.spec_from_file_location(
    "bootstrap_data_probe", Path(__file__).with_name("bootstrap-data-probe.py")
)
_SOURCE = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_SOURCE)
ProbeError = _SOURCE.ProbeError

BROWSER = "var globalThis = {};\n"
MAP_COUNTER = """var NativeMap = Map;
var traceMapAllocations = 0;
var Map = function() {
    traceMapAllocations = traceMapAllocations + 1;
    return new NativeMap();
};
"""

# Host setup and observations surround the unchanged extracted wrapper. The
# boolean in each row encloses its execution in try/catch, without editing it.
# Object-valued exception tokens test identity, not engine-specific error text.
SCENARIOS = {
    "commonjs-precedence": (
        BROWSER + """var module = {exports: {}};
var exports = module.exports;
var originalExports = exports;
var traceDefineReads = 0;
var traceDefineCalls = 0;
function define(factory) { traceDefineCalls = traceDefineCalls + 1; }
Object.defineProperty(define, "amd", {get: function() {
    traceDefineReads = traceDefineReads + 1;
    return 1;
}});
""" + MAP_COUNTER,
        """var tracePublished = typeof module.exports.set === "function" ? 1 : 0;
var traceOldExports = exports === originalExports && exports !== module.exports ? 1 : 0;
var traceNoBrowser = globalThis.bootstrap === undefined ? 1 : 0;
""",
        {"traceDefineReads": "0", "traceDefineCalls": "0", "traceMapAllocations": "1",
         "tracePublished": "1", "traceOldExports": "1", "traceNoBrowser": "1"}, False,
    ),
    "browser-target": (
        BROWSER + "var scriptThis = this; var self = {};\n",
        """var traceChosenGlobal = typeof globalThis.bootstrap.get === "function" ? 1 : 0;
var traceNoFallback = scriptThis.bootstrap === undefined && self.bootstrap === undefined ? 1 : 0;
""",
        {"traceChosenGlobal": "1", "traceNoFallback": "1"}, False,
    ),
    "browser-this-fallback": (
        "var globalThis = undefined; var scriptThis = this; var self = {};\n",
        """var traceChosenThis = typeof scriptThis.bootstrap.get === "function" ? 1 : 0;
var traceNoSelf = self.bootstrap === undefined ? 1 : 0;
""",
        {"traceChosenThis": "1", "traceNoSelf": "1"}, False,
    ),
    "amd-retained-factory": (
        BROWSER + MAP_COUNTER + """var pending;
var traceDefineCalls = 0;
var traceDefineReceiver = 0;
function define(factory) {
    "use strict";
    traceDefineCalls = traceDefineCalls + 1;
    traceDefineReceiver = this === undefined ? 1 : 0;
    pending = factory;
    return 99;
}
define.amd = 1;
""",
        """var traceDelayed = traceMapAllocations === 0 && typeof pending === "function" ? 1 : 0;
var first = pending(); var second = pending(); var element = {};
first.set(element, "bs.alert", 42);
var traceIndependent = first !== second && second.get(element, "bs.alert") === null ? 1 : 0;
var traceRetained = first.get(element, "bs.alert");
var traceNoBrowser = globalThis.bootstrap === undefined ? 1 : 0;
""",
        {"traceMapAllocations": "3", "traceDefineCalls": "1", "traceDefineReceiver": "1",
         "traceDelayed": "1", "traceIndependent": "1", "traceRetained": "42",
         "traceNoBrowser": "1"}, False,
    ),
    "mutable-export-and-methods": (
        BROWSER,
        """var data = globalThis.bootstrap; var alias = data; var element = {};
var descriptor = Object.getOwnPropertyDescriptor(data, "get");
var traceMutableDescriptor = descriptor.writable && descriptor.enumerable && descriptor.configurable ? 1 : 0;
var detachedSet = data.set; var originalGet = data.get; var detachedRemove = data.remove;
detachedSet(element, "bs.alert", 42);
var traceDetached = originalGet(element, "bs.alert");
var traceArrowReceiver = originalGet.call({}, element, "bs.alert");
var traceReplacementReceiver = 0;
data.get = function() {
    traceReplacementReceiver = this === alias ? 1 : 0;
    return 77;
};
var traceFieldReplacement = alias.get(element, "bs.alert");
globalThis.bootstrap = {get: function() { return 88; }};
var traceExportReplacement = globalThis.bootstrap.get(element, "bs.alert");
var traceOldMethod = originalGet(element, "bs.alert");
detachedRemove(element, "bs.alert");
var traceDetachedRemove = originalGet(element, "bs.alert") === null ? 1 : 0;
""",
        {"traceMutableDescriptor": "1", "traceDetached": "42", "traceArrowReceiver": "42",
         "traceReplacementReceiver": "1", "traceFieldReplacement": "77",
         "traceExportReplacement": "88", "traceOldMethod": "42", "traceDetachedRemove": "1"},
        False,
    ),
    "console-replacement-and-throw": (
        BROWSER + "var console = {error: function() {}}; var marker = {};\n",
        """var data = globalThis.bootstrap; var element = {};
data.set(element, "bs.alert", 42);
var traceCaught = 0; var traceErrorReceiver = 0; var traceErrorMessage = 0;
console.error = function(message) {
    traceErrorReceiver = this === console ? 1 : 0;
    traceErrorMessage = message === "Bootstrap doesn't allow more than one instance per element. Bound instance: bs.alert." ? 1 : 0;
    throw marker;
};
try { data.set(element, "bs.collapse", 99); } catch (error) {
    traceCaught = error === marker ? 1 : 0;
}
var traceOriginal = data.get(element, "bs.alert");
var traceRejected = data.get(element, "bs.collapse") === null ? 1 : 0;
var traceReplacementCalls = 0;
console = {error: function() { traceReplacementCalls = traceReplacementCalls + 1; return 123; }};
var traceUndefinedReturn = data.set(element, "bs.collapse", 99) === undefined ? 1 : 0;
""",
        {"traceCaught": "1", "traceErrorReceiver": "1", "traceErrorMessage": "1",
         "traceOriginal": "42", "traceRejected": "1", "traceReplacementCalls": "1",
         "traceUndefinedReturn": "1"}, False,
    ),
    "console-reentry": (
        BROWSER + "var console = {};\n",
        """var data = globalThis.bootstrap; var element = {}; var traceCalls = 0;
data.set(element, "bs.alert", 42);
console.error = function() {
    traceCalls = traceCalls + 1;
    data.remove(element, "bs.alert");
    data.set(element, "bs.collapse", 64);
};
var traceOuterReturn = data.set(element, "bs.collapse", 99) === undefined ? 1 : 0;
var traceInnerValue = data.get(element, "bs.collapse");
var traceOldRemoved = data.get(element, "bs.alert") === null ? 1 : 0;
""",
        {"traceCalls": "1", "traceOuterReturn": "1", "traceInnerValue": "64",
         "traceOldRemoved": "1"}, False,
    ),
    "callee-read-order": (
        BROWSER + """var traceOrder = 0;
var oldFrom = Array.from;
Object.defineProperty(Array, "from", {get: function() {
    traceOrder = traceOrder * 10 + 2;
    return oldFrom;
}});
var oldKeys = Map.prototype.keys;
Map.prototype.keys = function() {
    traceOrder = traceOrder * 10 + 3;
    return oldKeys.call(this);
};
var console = {};
Object.defineProperty(console, "error", {get: function() {
    traceOrder = traceOrder * 10 + 1;
    return function(message) { traceOrder = traceOrder * 10 + 4; };
}});
""",
        """var element = {};
globalThis.bootstrap.set(element, "bs.alert", 42);
globalThis.bootstrap.set(element, "bs.collapse", 99);
""",
        {"traceOrder": "1234"}, False,
    ),
    "publication-setter-throw": (
        BROWSER + """var marker = {}; var retained; var traceCaught = 0; var traceSetterCalls = 0;
Object.defineProperty(globalThis, "bootstrap", {set: function(value) {
    traceSetterCalls = traceSetterCalls + 1;
    retained = value;
    throw marker;
}});
""",
        """var element = {}; retained.set(element, "bs.alert", 42);
var traceRetained = retained.get(element, "bs.alert");
""",
        {"traceCaught": "1", "traceSetterCalls": "1", "traceRetained": "42"}, True,
    ),
    "factory-throw-before-publication": (
        BROWSER + """var marker = {}; var traceCaught = 0; var traceSetterCalls = 0;
Object.defineProperty(globalThis, "bootstrap", {set: function(value) {
    traceSetterCalls = traceSetterCalls + 1;
}});
var Map = function() { throw marker; };
""",
        "",
        {"traceCaught": "1", "traceSetterCalls": "0"}, True,
    ),
    "amd-registration-throw": (
        BROWSER + MAP_COUNTER + """var marker = {}; var pending; var traceCaught = 0;
function define(factory) { pending = factory; throw marker; }
define.amd = 1;
""",
        """var traceDelayed = traceMapAllocations === 0 && typeof pending === "function" ? 1 : 0;
var retained = pending(); var element = {}; retained.set(element, "bs.alert", 42);
var traceRetained = retained.get(element, "bs.alert");
""",
        {"traceMapAllocations": "2", "traceCaught": "1", "traceDelayed": "1",
         "traceRetained": "42"}, True,
    ),
}

NODE_DRIVER = r"""const fs = require('node:fs');
const vm = require('node:vm');
const manifest = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const observations = {};
for (const row of manifest) {
    const context = vm.createContext({});
    const source = fs.readFileSync(row.program, 'utf8');
    new vm.Script(source, {filename: row.program}).runInContext(context, {timeout: 2000});
    observations[row.name] = Object.fromEntries(Object.keys(context)
        .filter(name => name.startsWith('trace')).sort().map(name => [name, String(context[name])]));
}
process.stdout.write(JSON.stringify({node: process.version, observations}, null, 2) + '\n');
"""


def run(command: list[str]) -> subprocess.CompletedProcess:
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise ProbeError(f"command failed ({result.returncode}): {command[0]}\n{result.stdout}{result.stderr}")
    return result


def check(args: argparse.Namespace) -> None:
    vendor = args.bootstrap.read_bytes().decode("utf-8")
    fragment, provenance = _SOURCE.extract(vendor)
    args.work.mkdir(parents=True, exist_ok=True)
    rows = []
    for name, (before, after, expected, caught) in SCENARIOS.items():
        wrapped = ("try {\n" + fragment + "} catch (error) {\n"
                   "    traceCaught = error === marker ? 1 : 0;\n}\n") if caught else fragment
        source = before + wrapped + after
        if args.negative_control and name == "callee-read-order":
            source += "traceOrder = traceOrder + 1;\n"
        path = (args.work / f"{name}.js").resolve()
        path.write_bytes(source.encode("utf-8"))
        rows.append({"name": name, "program": str(path), "program_sha256": _SOURCE.sha256(source),
                     "expected": expected, "variant": "exact-wrapper-and-data"})
    manifest = (args.work / "manifest.json").resolve()
    manifest.write_text(json.dumps(rows, indent=2) + "\n")
    driver = args.work / "node-driver.cjs"
    driver.write_text(NODE_DRIVER)
    report = {**provenance, "vendor": str(args.bootstrap),
              "scope": "host semantic audit; no native compile or execution claim", "scenarios": rows}
    if args.generate_only:
        (args.work / "audit.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"bootstrap host contract: generated {len(rows)} exact-fragment scenarios in {args.work}")
        return

    oracle = run([args.node, str(driver), str(manifest)])
    (args.work / "node.json").write_text(oracle.stdout)
    observed = json.loads(oracle.stdout)
    report["node"] = observed["node"]
    mismatches = []
    for row in rows:
        name = row["name"]
        actual = observed["observations"].get(name)
        if actual != row["expected"]:
            raise ProbeError(f"Node observation {name}: expected {row['expected']}, got {actual}")
        row["node_observations"] = actual
        if args.reference:
            reference = run([args.reference, row["program"]])
            (args.work / f"{name}.reference.txt").write_text(reference.stdout)
            (args.work / f"{name}.reference.log").write_text(reference.stderr)
            values = {}
            for line in reference.stdout.splitlines():
                key, separator, value = line.partition("=")
                if not separator:
                    raise ProbeError(f"invalid reference observation for {name}: {line!r}")
                # The reference also prints non-trace primitive globals, e.g.
                # its undefined script receiver. Keep the full raw output but
                # compare the same explicit observation roots as Node.
                if not key.startswith("trace"):
                    continue
                if key in values:
                    raise ProbeError(f"duplicate reference observation for {name}: {key}")
                values[key] = value
            row["reference_observations"] = values
            row["reference_matches_node"] = values == actual
            if values != actual:
                mismatches.append(name)
    report["reference_mismatches"] = mismatches if args.reference else None
    (args.work / "audit.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"bootstrap host contract: Node agrees with {len(rows)} exact-fragment host scenarios")
    if args.reference:
        print(f"ctbrowser reference: {len(rows) - len(mismatches)}/{len(rows)} agree; "
              f"boundary mismatches: {', '.join(mismatches) or 'none'}")
        if mismatches and args.require_reference_match:
            raise ProbeError("ctbrowser host boundary differs from Node: " + ", ".join(mismatches))
    print(f"audit report: {args.work / 'audit.json'}; no native coverage asserted")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bootstrap", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--node", default="node")
    parser.add_argument("--reference")
    parser.add_argument("--require-reference-match", action="store_true")
    parser.add_argument("--generate-only", action="store_true")
    parser.add_argument("--negative-control", action="store_true")
    args = parser.parse_args()
    if args.require_reference_match and not args.reference:
        parser.error("--require-reference-match requires --reference")
    if args.generate_only and (args.reference or args.negative_control):
        parser.error("--generate-only cannot run a reference or negative control")
    # Compare the complete expected diagnostic; a tool failure cannot pass.
    expected_error = ("Node observation callee-read-order: expected {'traceOrder': '1234'}, "
                      "got {'traceOrder': '1235'}") if args.negative_control else None
    try:
        check(args)
    except ProbeError as error:
        if expected_error and str(error) == expected_error:
            print(f"bootstrap host contract negative control caught: {error}")
            return
        sys.exit(f"bootstrap host contract: {error}")
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        sys.exit(f"bootstrap host contract: {error}")
    if expected_error:
        sys.exit(f"bootstrap host contract: negative control did not fail with {expected_error!r}")


if __name__ == "__main__":
    main()
