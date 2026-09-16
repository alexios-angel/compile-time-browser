#!/usr/bin/env python3
"""Positive and adversarial checks of the opt-in, driver-bound host proof."""

import argparse
import json
from pathlib import Path
import re

from CTNative.harness import run


def fingerprint(opt, ir):
    result = run([opt, str(ir), "--ctnative-host-contract=fingerprint=true", "-o", "/dev/null"])
    match = re.search(r"host-contract fingerprint: ([0-9a-f]{64})", result.stderr)
    if not match:
        raise RuntimeError(f"missing canonical fingerprint: {result.stderr}")
    return match[1]


def manifest(opt, ir, *, absent=(), undefined=()):
    return {
        "version": 1,
        "provider": "closed-source-v1",
        "module_sha256": fingerprint(opt, ir),
        "entry": "_script_$0",
        "roots": [{"binding": "host", "properties": ["slot"]}],
        "observations": ["trace"],
        "absent_bindings": list(absent),
        "undefined_bindings": list(undefined),
    }


def analyze(opt, ir, contract, prefix, *, strict=False, success=True, options=""):
    config = prefix.with_suffix(".json")
    report = prefix.with_suffix(".report.json")
    output = prefix.with_suffix(".out.mlir")
    config.write_text(json.dumps(contract, indent=2) + "\n")
    flags = f"manifest={config} output={report} report=true require-proof={'true' if strict else 'false'} {options}"
    result = run(
        [opt, str(ir), "--ctnative-host-contract=" + flags, "-o", str(output)], success=success
    )
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
        "absent_typeof": "var host = {}; host.slot = 42; typeof missing; var trace = host.slot;",
        "absent_parens": "var host = {}; host.slot = 42; typeof (missing); var trace = host.slot;",
        "absent_comma": "var host = {}; host.slot = 42; typeof (0, missing); var trace = host.slot;",
        "absent_alias": "var host = {}; host.slot = 42; var saved = missing; typeof saved; var trace = host.slot;",
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
        positive = name in {
            "ordinary",
            "alias",
            "helper",
            "single_factory",
            "getter",
            "getter_alias",
            "getter_string",
            "getter_replaced",
            "absent_typeof",
            "absent_parens",
        }
        report, _, _ = analyze(args.opt, ir, contract, args.work / f"{name}-check")
        if report["proved"] != positive:
            raise RuntimeError(f"{name}: wrong proof result: {report}")
        if positive:
            slot = report["slots"][0]
            if slot["proved_edges"] != (2 if name == "getter_replaced" else 1) or slot[
                "source_writes"
            ] != (2 if name == "alias" else 1):
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
    text, count = re.subn(
        r"\bmodule( attributes)? \{",
        lambda match: 'module attributes {ctnative.host_proved = true, ctnative.host_reason = "trusted"'
        + (", " if match[1] else "} {"),
        unknown.read_text(),
        count=1,
    )
    if count != 1:
        raise RuntimeError("forged host metadata was not inserted")
    forged.write_text(text)
    rejection, _, _ = analyze(
        args.opt,
        forged,
        manifest(args.opt, unknown),
        args.work / "forged-check",
        strict=True,
        success=False,
    )
    if rejection["proved"] or not rejection["reason"]:
        raise RuntimeError("forged host metadata survived reanalysis")

    stale, _, result = analyze(
        args.opt, prepared["alias"], contract, args.work / "stale", strict=True, success=False
    )
    if not stale or "fingerprint mismatch" not in result.stderr:
        raise RuntimeError("stale manifest did not produce its exact diagnostic")
    bad_provider = dict(contract, provider="magic-bootstrap-host")
    _, _, result = analyze(args.opt, ir, bad_provider, args.work / "provider", success=False)
    if "unsupported host provider" not in result.stderr:
        raise RuntimeError("unknown provider was not rejected")
    for provider in ("closed-source-v1", "closed-source-session-v1"):
        requested = dict(contract, provider=provider)
        report, _, _ = analyze(args.opt, ir, requested, args.work / provider)
        if report["provider"] != provider or report["outer_key_inputs"] != 0:
            raise RuntimeError("host report changed its provider or invented DOM inputs")
        _, _, result = analyze(
            args.opt,
            ir,
            dict(requested, element_parameters=[0]),
            args.work / (provider + "-inputs"),
            success=False,
        )
        if "only supported fields" not in result.stderr:
            raise RuntimeError("source-only provider accepted DOM input declarations")
    dom_data = dict(contract, provider="ctbrowser-dom-data-session-v1", element_parameters=[0])
    report, _, _ = analyze(args.opt, ir, dom_data, args.work / "dom-data")
    if report["proved"] or report["provider"] != dom_data["provider"]:
        raise RuntimeError("DOM Data report must preserve its provider and reject missing inputs")
    result = run(
        [
            args.opt,
            str(ir),
            "--ctnative-specialize-host-prefix=" f"manifest={args.work / 'dom-data.json'}",
            "-o",
            "/dev/null",
        ],
        success=False,
    )
    if "host prefix does not support DOM Data input contracts" not in result.stderr:
        raise RuntimeError("source prefix analysis accepted a DOM Data input contract")
    for optimize in (True, False):
        emitted = args.work / f"dom-data-{optimize}.mlir"
        result = run(
            [
                args.opt,
                str(ir),
                "--ctnative-lower-to-emitc="
                f"host-manifest={args.work / 'dom-data.json'} optimize={str(optimize).lower()}",
                "-o",
                str(emitted),
            ],
            success=False,
        )
        if "native DOM Data source:" not in result.stderr or report["reason"] not in result.stderr:
            raise RuntimeError("DOM Data lowering did not explain the missing source input proof")
        if emitted.exists() and emitted.read_text():
            raise RuntimeError("DOM Data storage refusal emitted a partial native module")
    extra_claim = dict(contract, nonthrowing=True)
    _, _, result = analyze(args.opt, ir, extra_claim, args.work / "claim", success=False)
    if "only supported fields" not in result.stderr:
        raise RuntimeError("manifest accepted a supplied effect proof")
    exhausted, _, _ = analyze(args.opt, ir, contract, args.work / "budget", options="max-steps=0")
    if exhausted["proved"] or "budget" not in exhausted["reason"]:
        raise RuntimeError("budget exhaustion did not withhold proof")
    print(
        "host contract: 10 publication positives, 24 source refusals, stale/forged/provider/claim/budget controls passed"
    )


if __name__ == "__main__":
    main()
