#!/usr/bin/env python3
"""Generate an exact Data prefix proof and a boxed differential wrapper.

Only the checked UMD wrapper is installed in the differential executable.
This tests the semantic transformation, not native Bootstrap admission.
"""

import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess


def run(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise RuntimeError(f"failed: {command}\n{result.stdout}{result.stderr}")
    return result


REALM_OBSERVATIONS = {
    "traceRealmPublished": "1", "traceRealmStable": "1", "traceRealmDistinct": "1",
    "traceSelfUntouched": "1", "traceAliasUndefined": "1",
}


def fallback_program(probe, fragment):
    # Reuse the unchanged Data observations while keeping the exact vendor
    # fragment outside all host-prelude/API substitutions.
    browser = probe.generate(fragment, "browser")
    before, found, after = browser.partition(fragment)
    prelude, api = probe.ENVIRONMENTS["browser"]
    if not found or not before.startswith(prelude):
        raise RuntimeError("Data generator no longer has the expected exact-fragment boundary")
    return ("var scriptThis = this; var globalThis = undefined; var self = {};\n" +
            before[len(prelude):] + fragment + after.replace(api, "scriptThis.bootstrap") +
            "var traceRealmPublished = typeof scriptThis.bootstrap === 'object' ? 1 : 0;\n"
            "var traceRealmStable = scriptThis === this ? 1 : 0;\n"
            "var traceRealmDistinct = scriptThis !== self ? 1 : 0;\n"
            "var traceSelfUntouched = self.bootstrap === undefined ? 1 : 0;\n"
            "var traceAliasUndefined = globalThis === undefined ? 1 : 0;\n")


def replace_publication(program, fragment, api, replacement):
    before, found, after = program.partition(fragment)
    if not found:
        raise RuntimeError("publication replacement lost the exact vendor fragment")
    setup = f"originalGet = {api}.get;\n"
    method = "function replacement(element, key) { return originalGet(element, key); }"
    if replacement == "method":
        setup += f"{api}.get = {method};\n"
    else:
        setup += f"originalApi = {api};\n{api} = {{set: originalApi.set, get: {method}, remove: originalApi.remove}};\n"
    return "var originalGet; var originalApi;\n" + before + fragment + setup + after


NODE_DRIVER = r"""const fs = require('node:fs');
const vm = require('node:vm');
const specification = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const sandbox = {};
for (const key of specification.realm_own_data_properties) {
    Object.defineProperty(sandbox, key, {value: undefined, writable: true, configurable: true});
}
const context = vm.createContext(sandbox);
vm.runInContext(fs.readFileSync(specification.source, 'utf8'), context, {timeout: 10000});
const observations = {};
for (const key of specification.observations) {
    const value = vm.runInContext(key, context);
    if (typeof value !== 'number') throw new Error('non-numeric observation ' + key);
    observations[key] = String(value);
}
process.stdout.write(JSON.stringify({node: process.version, observations}, null, 2) + '\n');
"""


def node_oracle(node, work, js, expected, realm_properties):
    driver = work / "node-driver.cjs"
    specification = work / "node-input.json"
    driver.write_text(NODE_DRIVER)
    specification.write_text(json.dumps({"source": str(js.resolve()), "observations": sorted(expected),
                                         "realm_own_data_properties": realm_properties}, indent=2) + "\n")
    observed = json.loads(run([node, str(driver), str(specification)]).stdout)
    for name, value in expected.items():
        if observed["observations"].get(name) != value:
            raise RuntimeError(f"Node observation {name}: expected {value}, got {observed['observations'].get(name)}")
    (work / "node.json").write_text(json.dumps(observed, indent=2) + "\n")
    return observed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bootstrap", type=Path, required=True)
    parser.add_argument("--translate")
    parser.add_argument("--opt")
    parser.add_argument("--node", help="also check every declared observation in a fresh Node realm")
    parser.add_argument("--oracle-only", action="store_true", help="generate source and run Node without compiler tools")
    parser.add_argument("--follow-publication", action="store_true")
    parser.add_argument("--follow-provider-reads", action="store_true")
    parser.add_argument("--replace", choices=("method", "table"), help="replace the published callable/table before observation")
    parser.add_argument("--mode", choices=("commonjs", "browser", "browser_this_fallback", "global_reentry", "self_reentry", "resource_instances"), required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    if args.oracle_only and not args.node:
        parser.error("--oracle-only requires --node")
    if not args.oracle_only and (not args.translate or not args.opt):
        parser.error("compiler evidence requires --translate and --opt")
    if args.replace and (not args.follow_publication or args.mode not in {"commonjs", "browser", "browser_this_fallback"}):
        parser.error("--replace requires a followed exact publication mode")
    if args.mode == "resource_instances" and not args.follow_publication:
        parser.error("resource_instances requires --follow-publication")
    if args.follow_provider_reads and (not args.follow_publication or args.replace or
                                        args.mode not in {"commonjs", "browser", "browser_this_fallback"}):
        parser.error("--follow-provider-reads requires an unchanged exact publication mode")
    args.work.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location("bootstrap_probe", Path(__file__).with_name("bootstrap-data-probe.py"))
    probe = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(probe)
    fragment, provenance = probe.extract(args.bootstrap.read_bytes().decode("utf-8"))
    adversarial = args.mode in {"global_reentry", "self_reentry"}
    instances = args.mode == "resource_instances"
    realm_fallback = args.mode == "browser_this_fallback"
    realm_properties = ["bootstrap"] if realm_fallback else []
    if instances:
        program = """var host = {}; var first;
(function(factory) { first = factory(); host.slot = factory(); })(function() {
    const resource = new Map;
    return {get: () => resource.size, set: function(value) { resource.set('key', value); return value; }};
});
var traceSet = first.set(42);
var traceFirst = first.get();
var traceSecond = host.slot.get();
var traceDistinct = first !== host.slot ? 1 : 0;
"""
        provenance = {"fixture": "two factory invocations retain distinct resource instances"}
    elif adversarial:
        program = "function route() { if (typeof host === 'object') { trace = 1; } else { trace = 2; } } var host = {}; var trace = 0; route(); host = 0; this.route();\n"
        provenance = {"fixture": "global publication permits a later realm-property invocation"}
        if args.mode == "self_reentry":
            program = "var saved; var host = {}; var trace = 0; (function route() { if (typeof host === 'object') { trace = 1; } else { trace = 2; } saved = route; })(); host = 0; saved();\n"
            provenance = {"fixture": "implicit callee publication permits a later invocation"}
    elif realm_fallback:
        program = fallback_program(probe, fragment)
    else:
        program = probe.generate(fragment, args.mode)
    expected = {"trace": "2"} if adversarial else dict(probe.EXPECTED)
    if realm_fallback:
        expected.update(REALM_OBSERVATIONS)
    if instances:
        expected = {"traceSet": "42", "traceFirst": "1", "traceSecond": "0", "traceDistinct": "1"}
    if args.replace:
        api = "module.exports" if args.mode == "commonjs" else "scriptThis.bootstrap" if realm_fallback else "globalThis.bootstrap"
        program = replace_publication(program, fragment, api, args.replace)
    function_count = 2 if adversarial else 5 if instances else 8 if args.replace else 7
    js = args.work / "program.js"
    raw = args.work / "raw.mlir"
    prepared = args.work / "prepared.mlir"
    specialized = args.work / "specialized.mlir"
    manifest = args.work / "contract.json"
    report_file = args.work / "prefix.json"
    js.write_bytes(program.encode("utf-8"))
    provenance.update({"mode": args.mode, "program_sha256": probe.sha256(program),
                       "source_functions": function_count, "declared_observations": sorted(expected),
                       "follow_publication": args.follow_publication, "replacement": args.replace,
                       "follow_provider_reads": args.follow_provider_reads,
                       "native_execution_claimed": False})
    if args.node:
        provenance["node_oracle"] = node_oracle(args.node, args.work, js, expected, realm_properties)
        if realm_fallback:
            # The oracle must reject an executed receiver-identity mutation,
            # not merely compare a self-authored expected-output file.
            control = args.work / "node-negative-control"
            control.mkdir(exist_ok=True)
            changed = control / "program.js"
            changed.write_text(program + "traceRealmDistinct = 0;\n")
            try:
                node_oracle(args.node, control, changed, expected, realm_properties)
            except RuntimeError as failure:
                if str(failure) != "Node observation traceRealmDistinct: expected 1, got 0":
                    raise
                provenance["node_receiver_negative_control"] = str(failure)
            else:
                raise RuntimeError("Node receiver negative control was accepted")
    if args.oracle_only:
        (args.work / "oracle-evidence.json").write_text(json.dumps(provenance, indent=2) + "\n")
        print(f"host prefix {args.mode}: Node agrees with {len(expected)} observations; native execution not claimed")
        return
    run([args.translate, "--ctbrowser-js-to-ctjs", str(js), "-o", str(raw)])
    run([args.opt, str(raw), "--ctjs-resolve-globals", "--ctjs-lift-to-scf", "-o", str(prepared)])
    before = prepared.read_text()
    if len(re.findall(r"\bctjs\.func\b", before)) != function_count or "ctjs.skipped" in before:
        raise RuntimeError("exact Data source accounting changed")
    fingerprint = run([args.opt, str(prepared), "--ctnative-host-contract=fingerprint=true", "-o", "/dev/null"])
    digest = re.search(r"host-contract fingerprint: ([0-9a-f]{64})", fingerprint.stderr)
    if not digest:
        raise RuntimeError("missing canonical source fingerprint")
    binding, key = "globalThis", "bootstrap"
    present = {"globalThis"}
    if adversarial or instances:
        binding, key = "host", "slot"
    elif realm_fallback:
        binding, key = "scriptThis", "bootstrap"
        present.add("self")
    elif args.mode == "commonjs":
        binding, key = "module", "exports"
        present = {"module", "exports"}
    contract = {
        "version": 1, "provider": "closed-source-v1", "module_sha256": digest[1],
        "entry": "_script_$0", "roots": [{"binding": binding, "properties": [key]}],
        "observations": sorted(expected),
        "absent_bindings": [] if adversarial or instances else sorted({"module", "exports", "define", "self"} - present),
        "undefined_bindings": ["undefined"],
        "initial_intrinsics": [] if adversarial else ["Map"] if instances else ["Map", "Array"],
        "realm_global_this": True,
    }
    if realm_fallback:
        contract["entry_receiver"] = {"kind": "classic-script-realm", "own_data_properties": realm_properties}
    manifest.write_text(json.dumps(contract, indent=2) + "\n")
    result = run([args.opt, str(prepared),
                  f"--ctnative-specialize-host-prefix=manifest={manifest} output={report_file} report=true follow-publication={str(args.follow_publication).lower()} follow-provider-reads={str(args.follow_provider_reads).lower()}",
                  "-o", str(specialized)])
    (args.work / "prefix.log").write_text(result.stderr)
    report = json.loads(report_file.read_text())
    expected_branches = {"commonjs": 2, "browser": 5, "browser_this_fallback": 6,
                         "global_reentry": 0, "self_reentry": 0, "resource_instances": 0}[args.mode]
    expected_targets = [] if adversarial else ["fn$2", "fn$2", "fn$4"] if instances else ["fn$3"]
    if args.follow_publication and not instances and not adversarial:
        expected_targets.append("replacement$7" if args.replace else "fn$5")
    if args.follow_provider_reads:
        expected_targets.extend(["fn$6", "fn$4"])
    if not report["valid"] or report["selected_branches"] != expected_branches or report["targets"] != expected_targets:
        raise RuntimeError(f"exact wrapper proof did not advance as expected: {report}")
    after = specialized.read_text()
    if len(re.findall(r"\bctjs\.func\b", after)) != function_count:
        raise RuntimeError("prefix specialization lost a source function")
    wrapper = re.search(r"ctjs\.func (?:private )?@fn\$2\(.*?(?=\n  ctjs\.func |\n})", after, re.S)
    if not adversarial and not instances and (not wrapper or "scf.if" in wrapper[0] or len(re.findall(r"ctjs.call_direct @fn\$3\(", wrapper[0])) != 1):
        raise RuntimeError("selected wrapper still has open UMD alternatives or lost its factory call")
    # The retained value is the fifth wrapper operand; no replacement closure
    # or substituted script receiver may stand in for it.
    if not adversarial and not instances and not re.search(r"ctjs.call_direct @fn\$3\([^,]+, [^,]+, %arg4\)", wrapper[0]):
        raise RuntimeError("factory call did not preserve its actual callee operand")
    if args.follow_publication:
        count = 2 if instances else 1
        edges = 4 if instances else 3
        if (report["summarized_factories"], report["runtime_provider_allocations"], report["capture_edges"], report["publication_writes"]) != (count, count, edges, 1):
            raise RuntimeError(f"factory retention/publication proof changed: {report}")
        for operation in ("ctjs.construct", "ctjs.create_cell", "ctjs.create_closure"):
            if before.count(operation) != after.count(operation):
                raise RuntimeError(f"runtime {operation} was removed by factory following")
        if not instances and (not wrapper or wrapper[0].count("ctjs.set_property") != 1):
            raise RuntimeError("selected publication was removed by factory following")
        script = re.search(r"ctjs\.func (?:private )?@_script_\$0\(.*?(?=\n  ctjs\.func |\n})", after, re.S)
        last = re.escape(expected_targets[-1])
        if not script or not re.search(r"ctjs.call_direct @" + last + r"\(", script[0]):
            raise RuntimeError("published target was not resolved in the script entry")
    if args.follow_provider_reads:
        if (report["summarized_provider_calls"], report["runtime_provider_reads"]) != (2, 2):
            raise RuntimeError(f"initial empty-Map reads did not advance to the first mutation: {report}")
        summaries = report["provider_reads"]
        if ([entry["target"] for entry in summaries] != ["fn$5", "fn$6"] or
            [entry["factory_index"] for entry in summaries] != [0, 0] or
            [entry["result"] for entry in summaries] != ["#ctjs.null", "#ctjs.undefined"]):
            raise RuntimeError("read summaries lost actual captures or null/undefined identity")
        for name in ("_script_$0", "fn$4", "fn$5", "fn$6"):
            pattern = r"ctjs\.func (?:private )?@" + re.escape(name) + r"\(.*?(?=\n  ctjs\.func |\n})"
            original = re.search(pattern, before, re.S)[0]
            current = re.search(pattern, after, re.S)[0]
            if original.count("scf.if") != current.count("scf.if"):
                raise RuntimeError(f"provider traversal rewrote {name} observation/method branches")
            if name != "_script_$0" and original != current:
                raise RuntimeError(f"provider traversal specialized reusable Data method {name}")
    elif report["summarized_provider_calls"] or report["runtime_provider_reads"]:
        raise RuntimeError("provider-read traversal became implicit")

    # Native accounting remains an independent four-bucket census. Keep the
    # imported denominator even if ordinary default pruning becomes applicable.
    native_file = args.work / "native.mlir"
    native = run([args.opt, str(specialized), "--ctnative-lower-to-emitc=report=true", "-o", str(native_file)])
    native_text = native_file.read_text()
    claimed = len(re.findall(r"\bemitc\.func\b", native_text))
    refused = len(re.findall(r"\bctjs\.func\b", native_text))
    reasons = re.findall(r'ctnative.not_native = "((?:[^"\\]|\\.)*)"', native_text)
    reachability = re.search(r"ctnative\.reachability_summary = \{removed = (\d+) : i64,", native_text)
    pruned = int(reachability[1]) if reachability else -1
    if refused != len(reasons) or claimed + refused + pruned != function_count:
        raise RuntimeError("native refusal/source accounting is incomplete")
    provenance.update({"prefix": report,
                       "native_census": {"claimed": claimed, "refused": refused,
                                         "pruned": pruned, "reasons": reasons}})
    (args.work / "evidence.json").write_text(json.dumps(provenance, indent=2) + "\n")
    (args.work / "native.log").write_text(native.stderr)

    boxed = args.work / "boxed.mlir"
    # SCF lifting leaves dead arithmetic markers. CSE removes those without
    # canonicalization forming arithmetic selects over boxed CTJS values.
    lowered = run([args.opt, str(specialized), "--convert-scf-to-cf", "--cse", "--ctjs-lower-to-emitc",
                   "--ctjs-drop-uncompiled", "--emitc-eliminate-block-arguments", "-o", str(boxed)])
    (args.work / "boxed.log").write_text(lowered.stderr)
    cpp = run([args.translate, "--mlir-to-cpp", "--declare-variables-at-top", str(boxed)]).stdout
    entry_name = "route_1" if adversarial else "fn_1" if instances else "fn_2"
    if not re.search(r"\b" + entry_name + r"\(", cpp):
        raise RuntimeError("boxed differential wrapper was refused")
    cpp = re.sub(r"\b" + entry_name + r"\b", "ctcompile_host_prefix_wrapper", cpp)
    if args.follow_publication:
        if not re.search(r"\b_script__0\(", cpp):
            raise RuntimeError("boxed differential script entry was refused")
        cpp = re.sub(r"\b_script__0\b", "ctcompile_host_prefix_script", cpp)
    (args.work / "wrapper.cpp").write_text(cpp)
    (args.work / "expected.inc").write_text("".join(
        "{" + json.dumps(name) + ", " + value + ".0},\n" for name, value in sorted(expected.items())))
    print(f"host prefix {args.mode}: {expected_branches} selected branches, {len(expected_targets)} resolved call target(s); "
          f"native {claimed}/{function_count}, {refused} refused; boxed differential wrapper generated")


if __name__ == "__main__":
    main()
