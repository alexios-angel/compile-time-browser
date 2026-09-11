#!/usr/bin/env python3
"""Check runtime factory retention and current publication identities."""

import argparse
import importlib.util
import json
from pathlib import Path
import re


spec = importlib.util.spec_from_file_location("prefix", Path(__file__).with_name("host-prefix.py"))
prefix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prefix)
host = prefix.host

FACTORY = "function() { const resource = new Map; return {get: () => resource.size}; }"


def source(factory=FACTORY, middle="", *, twice=False):
    calls = "first = factory(); host.slot = factory();" if twice else "host.slot = factory();"
    mutate_first = "first.get = function firstOnly() { return 7; };" if twice else ""
    return ("var host = {}; var alias; var first; " +
            "(function(factory) { " + calls + " })(" + factory + "); " +
            mutate_first + middle + " var trace = host.slot.get();\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    sources = {
        "ordinary": source(),
        "method_replaced": source(middle="host.slot.get = function replacement() { return 42; };"),
        "alias_replaced": source(middle="alias = host.slot; alias.get = function replacement() { return 42; };"),
        "table_replaced": source(middle="host.slot = {get: function replacement() { return 42; }};"),
        "two_factories": source(twice=True),
        "unknown_after": source(middle="globalThis.foreign();"),
        "accessor_after": source(middle="Object.defineProperty(host.slot, 'get', {get: function() { return function() { return 0; }; }});"),
        "unknown_factory": source("function() { const resource = new Map; globalThis.foreign(); return {get: () => resource.size}; }"),
        "writable_capture": source("function() { let resource = new Map; return {get: () => { resource = new Map; return 42; }}; }"),
        "resource_escape": source("function() { const resource = new Map; return {resource: resource, get: () => resource.size}; }"),
        "forwarded_capture": source("function() { const resource = new Map; return {get: () => () => resource.size}; }"),
        "post_capture_overwrite": source("function() { let resource = new Map; const table = {get: () => resource.size}; resource = 0; return table; }"),
        "provider_replaced": "Map = function() {}; " + source(),
    }
    prepared = {}
    for name, text in sources.items():
        js = args.work / f"{name}.js"
        raw = args.work / f"{name}.raw.mlir"
        ir = args.work / f"{name}.mlir"
        js.write_text(text)
        host.run([args.translate, "--ctbrowser-js-to-ctjs", str(js), "-o", str(raw)])
        host.run([args.opt, str(raw), "--ctjs-resolve-globals", "--ctjs-lift-to-scf", "-o", str(ir)])
        prepared[name] = ir

    # Normalize the resource initialization into create_cell itself. The
    # later primitive overwrite is then the cell's sole explicit cell_set.
    # This distinguishes a final-state proof from counting at most one store.
    text = prepared["post_capture_overwrite"].read_text()
    initialized = re.search(r"^\s*ctjs.cell_set (%\w+), (%\w+)\s*$", text, re.M)
    if not initialized:
        raise RuntimeError("missing captured resource initialization")
    cell, resource = initialized.groups()
    allocation = re.search(r"^\s*" + re.escape(cell) + r" = ctjs.create_cell %\w+\s*$", text, re.M)
    if not allocation or not re.search(re.escape(resource) + r" = ctjs.construct", text):
        raise RuntimeError("captured resource initialization shape changed")
    normalized = text[:allocation.start()] + text[allocation.end():]
    normalized = normalized.replace(initialized[0], f"\n    {cell} = ctjs.create_cell {resource}\n", 1)
    ir = args.work / "normalized_capture_overwrite.mlir"
    ir.write_text(normalized)
    prepared["normalized_capture_overwrite"] = ir

    positive = {"ordinary", "method_replaced", "alias_replaced", "table_replaced", "two_factories"}
    for name, ir in prepared.items():
        contract = dict(host.manifest(args.opt, ir), initial_intrinsics=["Map"], realm_global_this=True)
        report, output, result = prefix.specialize(args.opt, ir, contract, args.work / f"{name}-follow",
                                                   options="follow-publication=true", success=name != "provider_replaced")
        if name == "provider_replaced":
            if report["valid"] or "declared intrinsic binding is replaced" not in result.stderr:
                raise RuntimeError("source provider replacement retained a summary")
            continue
        if not report["valid"] or report["full_host_contract_claimed"]:
            raise RuntimeError(f"{name}: wrong contract boundary: {report}")
        if name in positive:
            count = 2 if name == "two_factories" else 1
            if (report["summarized_factories"], report["runtime_provider_allocations"], report["capture_edges"], report["publication_writes"], report["resolved_calls"]) != (count, count, count, 1, count + 1):
                raise RuntimeError(f"{name}: missing owning publication facts: {report}")
            if name.endswith("replaced") and not report["targets"][-1].startswith("replacement$"):
                raise RuntimeError(f"{name}: current method value was replaced by an old symbol")
            if name == "two_factories" and (len(report["factories"]) != 2 or
                                            report["targets"][-1].startswith("firstOnly$")):
                raise RuntimeError("two invocations collapsed their resource identities")
            before, after = ir.read_text(), output.read_text()
            for operation in ("ctjs.construct", "ctjs.create_cell", "ctjs.create_closure", "ctjs.set_property"):
                if before.count(operation) != after.count(operation):
                    raise RuntimeError(f"{name}: {operation} was executed early or removed")
            if "resolved call body remains a runtime effect boundary" not in report["boundary"]:
                raise RuntimeError("method body effects were interpreted")
        elif name == "unknown_after":
            if report["summarized_factories"] != 1 or report["resolved_calls"] != 1:
                raise RuntimeError("unknown intervening effect did not stop current-value discovery")
        elif report["summarized_factories"] or report["capture_edges"] or report["publication_writes"]:
            raise RuntimeError(f"{name}: unsupported factory or capture escaped refusal: {report}")

    original = prepared["ordinary"]
    contract = dict(host.manifest(args.opt, original), initial_intrinsics=["Map"], realm_global_this=True)
    legacy, _, _ = prefix.specialize(args.opt, original, contract, args.work / "legacy")
    if legacy["summarized_factories"] or legacy["resolved_calls"] != 1:
        raise RuntimeError("factory following became implicit")
    report, rewritten, _ = prefix.specialize(args.opt, original, contract, args.work / "once", options="follow-publication=true")
    stale, _, _ = prefix.specialize(args.opt, rewritten, contract, args.work / "stale", options="follow-publication=true", success=False)
    if stale["valid"] or stale["summarized_factories"] or stale["capture_edges"]:
        raise RuntimeError("stale source kept usable retention facts")
    for limit in (0, 200):
        limited, _, _ = prefix.specialize(args.opt, original, contract, args.work / f"budget-{limit}",
                                          options=f"follow-publication=true max-steps={limit}", success=False)
        if limited["valid"] or limited["resolved_calls"] or limited["summarized_factories"] or limited["capture_edges"]:
            raise RuntimeError("work exhaustion retained partial factory/capture facts")
    unsafe = prepared["writable_capture"]
    forged = args.work / "forged.mlir"
    forged.write_text(unsafe.read_text().replace("module attributes {", 'module attributes {ctnative.host_factory_proved = true, ctnative.host_capture_edges = 1 : i64, ', 1))
    forged_contract = dict(host.manifest(args.opt, unsafe), initial_intrinsics=["Map"], realm_global_this=True)
    report, _, _ = prefix.specialize(args.opt, forged, forged_contract, args.work / "forged-check", options="follow-publication=true")
    if report["summarized_factories"] or report["capture_edges"]:
        raise RuntimeError("forged retention metadata replaced the independent cell proof")
    uncontracted, _, _ = prefix.specialize(args.opt, original, host.manifest(args.opt, original), args.work / "no-provider", options="follow-publication=true")
    if uncontracted["summarized_factories"] or uncontracted["capture_edges"]:
        raise RuntimeError("an undeclared Map provider supplied a resource identity")
    print("host publication: exact factories, current method/table replacements, distinct invocations and ownership/effect guards passed")


if __name__ == "__main__":
    main()
