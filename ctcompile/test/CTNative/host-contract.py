#!/usr/bin/env python3
"""Positive and adversarial checks of the opt-in, driver-bound host proof."""

import argparse
import json
from pathlib import Path
import re
import subprocess


def run(command, *, success=True):
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    if (result.returncode == 0) != success:
        raise RuntimeError(f"unexpected tool status {result.returncode}: {command}\n{result.stdout}{result.stderr}")
    return result


def fingerprint(opt, ir):
    result = run([opt, str(ir), "--ctnative-host-contract=fingerprint=true", "-o", "/dev/null"])
    match = re.search(r"host-contract fingerprint: ([0-9a-f]{64})", result.stderr)
    if not match:
        raise RuntimeError(f"missing canonical fingerprint: {result.stderr}")
    return match[1]


def manifest(opt, ir, *, absent=(), undefined=()):
    return {
        "version": 1, "provider": "closed-source-v1",
        "module_sha256": fingerprint(opt, ir), "entry": "_script_$0",
        "roots": [{"binding": "host", "properties": ["slot"]}],
        "observations": ["trace"], "absent_bindings": list(absent),
        "undefined_bindings": list(undefined),
    }


def analyze(opt, ir, contract, prefix, *, strict=False, success=True, options=""):
    config = prefix.with_suffix(".json")
    report = prefix.with_suffix(".report.json")
    output = prefix.with_suffix(".out.mlir")
    config.write_text(json.dumps(contract, indent=2) + "\n")
    flags = f"manifest={config} output={report} report=true require-proof={'true' if strict else 'false'} {options}"
    result = run([opt, str(ir), "--ctnative-host-contract=" + flags, "-o", str(output)], success=success)
    return json.loads(report.read_text()) if report.exists() else None, output, result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    sources = {
        "ordinary": "var host = {}; host.slot = 42; var trace = host.slot;",
        "alias": "var host = {}; host.slot = 1; var alias = host; alias.slot = 42; var trace = host.slot;",
        "helper": "function publish(target) { target.slot = 42; } var host = {}; publish(host); var trace = host.slot;",
        "single_factory": "function make() { return {slot: 42}; } function once() { return make(); } var host = {}; host.slot = once().slot; var trace = host.slot;",
        "getter": "var host = {}; function make() { return {get() { return 42; }}; } host.slot = make(); var trace = host.slot.get();",
        "getter_alias": "var host = {}; function make() { return {get() { return 42; }}; } host.slot = make(); var alias = host; var trace = alias.slot.get();",
        "getter_string": "var host = {}; function make() { return {get() { return 'owned'; }}; } host.slot = make(); var trace = host.slot.get();",
        "getter_replaced": "var host = {}; function make() { return {get() { return 1; }}; } host.slot = make(); host.slot.get = function() { return 42; }; var trace = host.slot.get();",
        "before_write": "var host = {}; var trace = host.slot; host.slot = 42;",
        "replacement": "var host = {}; host.slot = 42; host = {}; var trace = host.slot;",
        "accessor": "var host = {get slot() { return 42; }}; var trace = host.slot;",
        "prototype": "var host = {}; host.slot = 42; host.__proto__ = {}; var trace = host.slot;",
        "unknown": "var host = {}; host.slot = 42; unknown(host); var trace = host.slot;",
        "missing_root": "var host = 42; var trace = host;",
        "absent_write": "var host = {}; host.slot = 42; var missing = 1; var trace = host.slot;",
        "absent_read": "var host = {}; host.slot = 42; var unseen = missing; var trace = host.slot;",
        "throwing": "function publish(target) { target.slot = 42; throw 7; } var host = {}; publish(host); var trace = host.slot;",
        "repeated_factory_escape": "function make() { return {}; } var host = {}; host.slot = 1; var a = make(); var b = make(); a.link = host; b.link.slot = 42; var trace = host.slot;",
        "repeated_factory": "function make() { return {}; } var host = {}; host.slot = 1; var a = make(); var b = make(); a.link = {slot: 42}; host.slot = b.link.slot; var trace = host.slot;",
        "repeated_wrapper": "function make() { return {}; } function again() { return make(); } var host = {}; host.slot = 1; var a = again(); var b = again(); a.link = {slot: 42}; host.slot = b.link.slot; var trace = host.slot;",
        "getter_capture": "var host = {}; function make() { const value = 42; return {get() { return value; }}; } host.slot = make(); var trace = host.slot.get();",
        "getter_this": "var host = {}; function make() { return {value: 42, get() { return this.value; }}; } host.slot = make(); var trace = host.slot.get();",
        "getter_effect": "var side = 0; var host = {}; function make() { return {get() { side = 1; return 42; }}; } host.slot = make(); var trace = host.slot.get();",
        "getter_arguments": "var host = {}; function make() { return {get() { return arguments.length; }}; } host.slot = make(); var trace = host.slot.get();",
        "getter_nonfunction": "var host = {}; function make() { return {get: 42}; } host.slot = make(); var trace = host.slot.get();",
        "getter_missing": "var host = {}; function make() { return {}; } host.slot = make(); var trace = host.slot.get();",
        "getter_extra_arg": "var host = {}; function make() { return {get() { return 42; }}; } host.slot = make(); var trace = host.slot.get(17);",
        "getter_detached": "var host = {}; function make() { return {get() { return 42; }}; } host.slot = make(); var detached = host.slot.get; var trace = detached();",
        "getter_generator": "var host = {}; function make() { return {get: function*() { return 42; }}; } host.slot = make(); var trace = host.slot.get();",
        "getter_async": "var host = {}; function make() { return {get: async function() { return 42; }}; } host.slot = make(); var trace = host.slot.get();",
    }
    prepared = {}
    for name, source in sources.items():
        js = args.work / f"{name}.js"
        raw = args.work / f"{name}.raw.mlir"
        ir = args.work / f"{name}.mlir"
        js.write_text(source + "\n")
        run([args.translate, "--ctbrowser-js-to-ctjs", str(js), "-o", str(raw)])
        run([args.opt, str(raw), "--ctjs-resolve-globals", "--ctjs-lift-to-scf", "-o", str(ir)])
        prepared[name] = ir
        contract = manifest(args.opt, ir, absent=("missing",) if name.startswith("absent_") else ())
        positive = name in {"ordinary", "alias", "helper", "single_factory", "getter", "getter_alias", "getter_string", "getter_replaced"}
        report, _, _ = analyze(args.opt, ir, contract, args.work / f"{name}-check")
        if report["proved"] != positive:
            raise RuntimeError(f"{name}: wrong proof result: {report}")
        if positive:
            slot = report["slots"][0]
            if slot["proved_edges"] != (2 if name == "getter_replaced" else 1) or slot["source_writes"] != (2 if name == "alias" else 1):
                raise RuntimeError(f"{name}: wrong publication flow: {slot}")
        elif any(slot["proved_edges"] for slot in report["slots"]):
            raise RuntimeError(f"{name}: a refusal exposed usable edges")

    ir = prepared["ordinary"]
    contract = manifest(args.opt, ir)
    report, output, _ = analyze(args.opt, ir, contract, args.work / "strict-positive", strict=True)
    repeated, _, _ = analyze(args.opt, output, contract, args.work / "repeated", strict=True)
    if report != repeated:
        raise RuntimeError("repeated analysis changed the canonical proof/report")

    unknown = prepared["unknown"]
    forged = args.work / "forged.mlir"
    forged.write_text(unknown.read_text().replace("module {", 'module attributes {ctnative.host_proved = true, ctnative.host_reason = "trusted"} {', 1))
    rejection, _, _ = analyze(args.opt, forged, manifest(args.opt, unknown), args.work / "forged-check", strict=True, success=False)
    if rejection["proved"] or not rejection["reason"]:
        raise RuntimeError("forged host metadata survived reanalysis")

    stale, _, result = analyze(args.opt, prepared["alias"], contract, args.work / "stale", strict=True, success=False)
    if not stale or "fingerprint mismatch" not in result.stderr:
        raise RuntimeError("stale manifest did not produce its exact diagnostic")
    bad_provider = dict(contract, provider="magic-bootstrap-host")
    _, _, result = analyze(args.opt, ir, bad_provider, args.work / "provider", success=False)
    if "unsupported host provider" not in result.stderr:
        raise RuntimeError("unknown provider was not rejected")
    extra_claim = dict(contract, nonthrowing=True)
    _, _, result = analyze(args.opt, ir, extra_claim, args.work / "claim", success=False)
    if "only supported fields" not in result.stderr:
        raise RuntimeError("manifest accepted a supplied effect proof")
    exhausted, _, _ = analyze(args.opt, ir, contract, args.work / "budget", options="max-steps=0")
    if exhausted["proved"] or "budget" not in exhausted["reason"]:
        raise RuntimeError("budget exhaustion did not withhold proof")
    print("host contract: 8 publication positives, 22 source refusals, stale/forged/provider/claim/budget controls passed")


if __name__ == "__main__":
    main()
