#!/usr/bin/env python3
"""Consume only actual closed entry-prefix proofs; keep unknown effects live."""

import argparse
import importlib.util
import json
from pathlib import Path
import re


spec = importlib.util.spec_from_file_location("host_contract", Path(__file__).with_name("host-contract.py"))
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)


def specialize(opt, ir, contract, prefix, *, success=True, options=""):
    manifest = prefix.with_suffix(".json")
    report = prefix.with_suffix(".report.json")
    output = prefix.with_suffix(".out.mlir")
    manifest.write_text(json.dumps(contract, indent=2) + "\n")
    result = host.run([opt, str(ir),
                       f"--ctnative-specialize-host-prefix=manifest={manifest} output={report} report=true {options}",
                       "-o", str(output)], success=success)
    return json.loads(report.read_text()) if report.exists() else None, output, result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    sources = {
        "callback": ("var trace = 0; var host = {slot: 42}; (function(f) { if (typeof host === 'object') { f(); } else { host.slot = 0; } })(function() { trace = 42; });", 1, 1),
        "replacement": ("var host = {slot: {}}; host.slot = 0; (function(f) { if (typeof host.slot === 'object') { f(); } else { host.slot = 42; } })(function() { throw 0; }); var trace = host.slot;", 1, 0),
        "mutating_helper": ("var trace = 0; var host = {slot: 1}; (function() { host = 0; })(); if (typeof host === 'object') { trace = 0; } else { trace = 42; }", 1, 0),
        "callee_replacement": ("function first() {} function second() {} var host = {slot: 42}; var callback = first; callback = second; (function(f) { if (typeof host === 'object') { f(); } })(callback); var trace = 42;", 1, 1),
        "unknown_before": ("var host = {slot: 42}; (function(f) { unknown(); if (typeof host === 'object') { f(); } })(function() {}); var trace = 42;", 0, 0),
        "throw_before": ("var host = {slot: 42}; (function() { throw 7; })(); if (typeof host === 'object') { host.slot = 0; } var trace = host.slot;", 0, 0),
        "terminating_arm": ("var host = {slot: 42}; (function(f) { if (typeof host === 'object') { f(); } else { throw 0; } })(function() {}); var trace = 42;", 0, 0),
        "receiver_guard": ("var host = {slot: 1}; (function() { if (typeof this === 'undefined') { host.slot = 0; } else { host.slot = 42; } })(); var trace = host.slot;", 0, 0),
        "accessor": ("var host = {get slot() { return 42; }}; if (typeof host.slot === 'number') { host.slot = 0; } var trace = host.slot;", 0, 0),
        "repeated": ("function route(f) { if (typeof host === 'object') { f(); } } var host = {slot: 42}; route(function() {}); host = 0; route(function() {}); var trace = 42;", 0, 0),
        "repeated_wrapper": ("function route() { (function(f) { if (typeof host === 'object') { f(); } })(function() {}); } var host = {slot: 42}; route(); host = 0; route(); var trace = 42;", 0, 0),
        "escape": ("function route(f) { if (typeof host === 'object') { f(); } } var host = {slot: 42}; route(function() {}); unknown(route); var trace = 42;", 0, 0),
        "prototype": ("var host = {slot: 42}; host.__proto__ = {}; if (typeof host === 'object') { host.slot = 0; } var trace = host.slot;", 0, 0),
        "global_reentry": ("function route() { if (typeof host === 'object') { trace = 1; } else { trace = 2; } } var host = {}; var trace = 0; route(); host = 0; this.route();", 0, 0),
        "self_reentry": ("var saved; var host = {}; var trace = 0; (function route() { if (typeof host === 'object') { trace = 1; } else { trace = 2; } saved = route; })(); host = 0; saved();", 0, 0),
        "caller_reflection": ("var saved; var host = {}; var trace = 0; function steal() { saved = steal.caller; } (function() { if (typeof host === 'object') { trace = 1; } else { trace = 2; } steal(); })(); host = 0; saved();", 0, 0),
        "late_host": ("var host = {slot: 42}; (function() { if (typeof host === 'object') { foreign(); } })(); foreign = 0; var trace = 42;", 0, 0),
        "late_host_factory": ("var host = {slot: 42}; (function(f) { if (typeof host === 'object') { f(); } })(function() { foreign(); }); foreign = 0; var trace = 42;", 0, 0),
        "opaque_realm_factory": ("var host = {slot: 42}; (function(f) { if (typeof host === 'object') { f(); } })(function() { this.foreign(); }); var trace = 42;", 0, 0),
        "opaque_slot_factory": ("var host = {slot: 42}; (function(f) { if (typeof host === 'object') { f(); } })(function() { var callback = this.foreign; callback(); }); var trace = 42;", 0, 0),
    }
    prepared = {}
    for name, (source, branches, calls) in sources.items():
        js = args.work / f"{name}.js"
        raw = args.work / f"{name}.raw.mlir"
        ir = args.work / f"{name}.mlir"
        js.write_text(source + "\n")
        host.run([args.translate, "--ctbrowser-js-to-ctjs", str(js), "-o", str(raw)])
        host.run([args.opt, str(raw), "--ctjs-resolve-globals", "--ctjs-lift-to-scf", "-o", str(ir)])
        prepared[name] = ir
        report, output, _ = specialize(args.opt, ir, host.manifest(args.opt, ir), args.work / f"{name}-check")
        if not report["valid"] or report["selected_branches"] != branches or report["resolved_calls"] != calls:
            raise RuntimeError(f"{name}: wrong prefix proof: {report}")
        if report["full_host_contract_claimed"]:
            raise RuntimeError("a prefix result claimed the complete host contract")
        before, after = ir.read_text(), output.read_text()
        if len(re.findall(r"\bctjs\.func\b", before)) != len(re.findall(r"\bctjs\.func\b", after)):
            raise RuntimeError(f"{name}: specialization changed the source function denominator")
        if name == "callback" and "resolved call body remains a runtime effect boundary" not in report["boundary"]:
            raise RuntimeError("factory invocation effects were assumed instead of retained")
        if name == "callee_replacement" and not all(target.startswith("second$") for target in report["targets"]):
            raise RuntimeError("a replaced source callee kept its old identity")
        if name.startswith("opaque_") and "continuation may expose" not in report["boundary"]:
            raise RuntimeError(f"{name}: did not exercise the continuation identity guard")
        if name == "receiver_guard" and "unproved unary conversion" not in report["boundary"]:
            raise RuntimeError("a raw call receiver was treated as effective JavaScript this")

    original = prepared["callback"]
    contract = host.manifest(args.opt, original)
    _, output, _ = specialize(args.opt, original, contract, args.work / "stale-source")
    stale, _, result = specialize(args.opt, output, contract, args.work / "stale-check", success=False)
    if stale["valid"] or "fingerprint mismatch" not in result.stderr:
        raise RuntimeError("changed semantic IR reused a prior invocation proof")
    budget, _, _ = specialize(args.opt, original, contract, args.work / "budget", success=False, options="max-steps=0")
    if budget["valid"] or budget["selected_branches"] or budget["resolved_calls"]:
        raise RuntimeError("exhausted discovery retained candidate rewrites")
    unknown = prepared["unknown_before"]
    forged = args.work / "forged.mlir"
    forged.write_text(unknown.read_text().replace("module attributes {", "module attributes {ctnative.host_prefix_proved = true, ", 1))
    report, _, _ = specialize(args.opt, forged, host.manifest(args.opt, unknown), args.work / "forged-check")
    if report["selected_branches"] or report["resolved_calls"]:
        raise RuntimeError("forged prefix metadata crossed an unknown effect")

    for name, changes, diagnostic in [
        ("unknown_intrinsic", {"initial_intrinsics": ["console"]}, "unsupported initial intrinsic identity"),
        ("realm_conflict", {"realm_global_this": True, "absent_bindings": ["globalThis"]}, "cannot be absent or undefined"),
        ("realm_type", {"realm_global_this": "yes"}, "must be a boolean"),
    ]:
        _, _, result = specialize(args.opt, original, dict(contract, **changes), args.work / name, success=False)
        if diagnostic not in result.stderr:
            raise RuntimeError(f"{name}: initial provider declaration was not validated")

    resource_sources = {
        "resource": "var host = {slot: 42}; (function(f) { if (typeof host === 'object') { f(); } })(function() { var trace = new Map; });",
        "replace_intrinsic": "var Map = function() {}; var host = {slot: 42}; var trace = host.slot;",
        "alias_intrinsic": "var alias = Map; var host = {slot: 42}; var trace = host.slot;",
        "realm_replace": "var realm = this; realm.Map = function() {}; var host = {slot: 42}; var trace = host.slot;",
        "array_method_replace": "Array.from = function() {}; var host = {slot: 42}; var trace = host.slot;",
    }
    for name, source in resource_sources.items():
        js = args.work / f"{name}.js"
        raw = args.work / f"{name}.raw.mlir"
        ir = args.work / f"{name}.mlir"
        js.write_text(source + "\n")
        host.run([args.translate, "--ctbrowser-js-to-ctjs", str(js), "-o", str(raw)])
        host.run([args.opt, str(raw), "--ctjs-resolve-globals", "--ctjs-lift-to-scf", "-o", str(ir)])
        resource_contract = dict(host.manifest(args.opt, ir), initial_intrinsics=["Map", "Array"], realm_global_this=True)
        report, _, _ = specialize(args.opt, ir, resource_contract, args.work / f"{name}-provider", success=name == "resource")
        if name == "resource":
            if report["selected_branches"] != 1 or report["resolved_calls"] != 1:
                raise RuntimeError("declared standard intrinsic did not close the identity boundary")
            withheld, _, _ = host.analyze(args.opt, ir, resource_contract, args.work / "resource-full-contract")
            if withheld["proved"] or any(slot["proved_edges"] for slot in withheld["slots"]):
                raise RuntimeError("intrinsic identity declaration became a complete effect proof")
            forged_resource = args.work / "forged-resource.mlir"
            forged_resource.write_text(ir.read_text().replace("module attributes {", 'module attributes {ctnative.host_intrinsics = ["Map", "Array"], ', 1))
            opaque, _, _ = specialize(args.opt, forged_resource, host.manifest(args.opt, ir), args.work / "forged-resource-check")
            if opaque["selected_branches"] or opaque["resolved_calls"]:
                raise RuntimeError("forged intrinsic metadata authorized an undeclared provider")
        elif report["valid"] or report["selected_branches"] or report["resolved_calls"]:
            raise RuntimeError(f"{name}: source replacement/escape accepted an intrinsic promise")
    print("host prefix: 4 useful consumers, 16 effect/context refusals, guarded Map factory and stale/forged/budget controls passed")


if __name__ == "__main__":
    main()
