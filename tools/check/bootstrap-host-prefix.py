#!/usr/bin/env python3
"""Generate an exact Data prefix proof and a boxed differential wrapper.

The checked UMD wrapper and optionally the publication-following script are
installed in the differential executable. Provider methods remain runtime.
This tests the semantic transformation, not native Bootstrap admission.
"""

import argparse
import importlib.util
import json
from pathlib import Path
import re
import struct
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

# These are the unchanged provider-read programs measured before enabling
# mutation summaries. The new option changes analysis, never JavaScript.
PROVIDER_PROGRAM_SHA256 = {
    "commonjs": "cc6c3960099f201b3e6c17f556c4cc5e08a7a47190e08cfba5628be40ea0a181",
    "browser": "80a6fd87cbfdaafa6b2a3c6aab05bfc66ca36bf23f3827c29b4fe79b93ab3722",
    "browser_this_fallback": "a8dd4151142665d7aebd72bb7b099c4956ab9def0a8d304b134e057151293a00",
}

PROVIDER_MUTATION_TARGETS = [
    "fn$5", "fn$6", "fn$4", "fn$4", "fn$5", "fn$5", "fn$4", "fn$5", "fn$5", "fn$6", "fn$5",
]


def number_attribute(value):
    # CTJS NumberAttr prints the uint64 bit pattern, including signed zero.
    return f"#ctjs.number<{struct.unpack('<Q', struct.pack('<d', value))[0]}>"


PROVIDER_MUTATION_RESULTS = [
    "#ctjs.null", "#ctjs.undefined", "#ctjs.undefined", "#ctjs.undefined",
    number_attribute(42), number_attribute(21), "#ctjs.undefined", number_attribute(43),
    "#ctjs.null", "#ctjs.undefined", number_attribute(43),
]

PROVIDER_CALLBACK_TARGETS = [
    *PROVIDER_MUTATION_TARGETS, "fn$4", "fn$5", "fn$5", "fn$6", "fn$5", "fn$5", "fn$6",
]

PROVIDER_OBJECT_TARGETS = [*PROVIDER_CALLBACK_TARGETS, "fn$4", "fn$5", "fn$5", "fn$5", "fn$5"]


def check_provider_mutations(report, boundary="unsupported provider path at `ctjs.load_global` (console)"):
    summaries = report["provider_calls"]
    if (report["summarized_provider_calls"], report["runtime_provider_mutations"],
        report["runtime_nested_provider_allocations"]) != (11, 6, 2) or report["provider_reads"]:
        raise RuntimeError(f"private Map mutation summaries did not reach the conflict call: {report}")
    if report["provider_boundary"] != boundary:
        raise RuntimeError(f"provider mutation traversal did not stop at the console boundary: {report}")
    if ([entry["target"] for entry in summaries] != PROVIDER_MUTATION_TARGETS or
        [entry["factory_index"] for entry in summaries] != [0] * 11 or
        [entry["result"] for entry in summaries] != PROVIDER_MUTATION_RESULTS):
        raise RuntimeError("provider mutation summaries lost call order, captured factory or result identity")
    calls = [entry["call_operation"] for entry in summaries]
    if any(type(call) is not int or call < 0 for call in calls) or len(set(calls)) != 11:
        raise RuntimeError("provider mutation summaries lost their actual entry-call identities")
    if [len(entry["allocations"]) for entry in summaries] != [0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0]:
        raise RuntimeError("nested Maps were not allocated by the two first successful set invocations")
    allocations = [allocation for entry in summaries for allocation in entry["allocations"]]
    first, second = allocations
    if (first["map_id"] == second["map_id"] or
        first["allocation_operation"] != second["allocation_operation"] or
        first["invocation_operation"] != calls[2] or second["invocation_operation"] != calls[3]):
        raise RuntimeError("repeated execution of the nested ConstructOp conflated runtime Maps")

    provenance = {}
    for entry in summaries:
        for operation in [*entry["allocations"], *entry["operations"]]:
            map_id = operation["map_id"]
            origin = (operation["allocation_operation"], operation["invocation_operation"])
            if (type(map_id) is not int or map_id <= 0 or
                any(type(ordinal) is not int or ordinal < 0 for ordinal in origin)):
                raise RuntimeError("provider Map identity/provenance was not source-derived")
            if map_id in provenance and provenance[map_id] != origin:
                raise RuntimeError("one provider Map acquired inconsistent allocation provenance")
            provenance[map_id] = origin
    if len(provenance) != 3:
        raise RuntimeError("exact provider prefix did not retain one outer and two inner Maps")
    operations = [operation for entry in summaries for operation in entry["operations"]]
    if any(operation["member"] not in {"has", "get", "size", "set", "delete"} for operation in operations):
        raise RuntimeError("provider mutation mode summarized an unsupported builtin")
    reads = [operation for operation in operations if operation["member"] in {"has", "get", "size"}]
    sets = [operation for operation in operations if operation["member"] == "set"]
    deletes = [operation for operation in operations if operation["member"] == "delete"]
    if (report["runtime_provider_reads"], len(reads), len(sets), len(deletes)) != (31, 31, 5, 1):
        raise RuntimeError("provider operation counts disagree with their per-invocation reports")
    if any(operation["result"] is not None or operation["result_map_id"] != operation["map_id"] for operation in sets):
        raise RuntimeError("Map.set did not preserve its receiver-valued normal result")
    if deletes[0]["result"] != "#ctjs.boolean<false>" or deletes[0]["result_map_id"] != 0:
        raise RuntimeError("unsuccessful Map.delete was counted as successful removal")
    if deletes[0]["map_id"] != first["map_id"]:
        raise RuntimeError("wrong-key deletion lost the original element's nested Map")


def check_provider_callbacks(report, object_payloads=False):
    # These initial expectations follow the source's complete primitive prefix.
    # The compiler must measure them; source hashes and runtime observations are
    # independent gates, not evidence supplied by this forecast.
    summaries = report["provider_calls"][:18] if object_payloads else report["provider_calls"]
    expected_results = [
        *PROVIDER_MUTATION_RESULTS, "#ctjs.undefined", "#ctjs.null", number_attribute(43),
        "#ctjs.undefined", "#ctjs.null", number_attribute(21), "#ctjs.undefined",
    ]
    expected_counts = (23, 68, 10, 3, 1, 2) if object_payloads else (18, 52, 8, 2, 1, 2)
    if (report["summarized_provider_calls"], report["runtime_provider_reads"],
        report["runtime_provider_mutations"], report["runtime_nested_provider_allocations"],
        report["runtime_provider_callbacks"], report["runtime_provider_global_writes"]) != expected_counts:
        raise RuntimeError(f"provider callback proof did not reach the ordinary object payload: {report}")
    boundary = "" if object_payloads else "unsupported provider path at `ctjs.call`"
    if report["provider_reads"] or report["provider_boundary"] != boundary:
        raise RuntimeError(f"provider callback traversal crossed its object-payload boundary: {report}")
    if ([entry["target"] for entry in summaries] != PROVIDER_CALLBACK_TARGETS or
        [entry["factory_index"] for entry in summaries] != [0] * 18 or
        [entry["result"] for entry in summaries] != expected_results):
        raise RuntimeError("provider callback summaries lost their invocation order, factory or results")
    calls = [entry["call_operation"] for entry in summaries]
    if any(type(call) is not int or call < 0 for call in calls) or len(set(calls)) != 18:
        raise RuntimeError("provider callback summaries lost their actual entry-call identities")
    if [len(entry["allocations"]) for entry in summaries] != [0, 0, 1, 1] + [0] * 14:
        raise RuntimeError("failed ordinary object insertion published a tentative Map allocation")
    allocations = [allocation for entry in summaries for allocation in entry["allocations"]]
    first, second = allocations
    if (first["map_id"] == second["map_id"] or
        first["allocation_operation"] != second["allocation_operation"] or
        [entry["invocation_operation"] for entry in allocations] != [calls[2], calls[3]]):
        raise RuntimeError("callback traversal conflated the two retained inner Maps")
    provenance = {}
    operations = []
    for entry in summaries:
        operations.extend(entry["operations"])
        for operation in [*entry["allocations"], *entry["operations"]]:
            map_id = operation["map_id"]
            origin = (operation["allocation_operation"], operation["invocation_operation"])
            if (type(map_id) is not int or map_id <= 0 or
                any(type(ordinal) is not int or ordinal < 0 for ordinal in origin) or
                map_id in provenance and provenance[map_id] != origin):
                raise RuntimeError("callback traversal lost a Map's source allocation identity")
            provenance[map_id] = origin
    if len(provenance) != 3:
        raise RuntimeError("callback traversal did not retain one outer and two inner Maps")
    allowed = {"has", "get", "size", "set", "delete", "keys", "Array.from", "snapshot[index]"}
    if any(operation["member"] not in allowed for operation in operations):
        raise RuntimeError("provider callback mode summarized an unsupported builtin")
    sets = [operation for operation in operations if operation["member"] == "set"]
    deletes = [operation for operation in operations if operation["member"] == "delete"]
    reads = [operation for operation in operations if operation["member"] not in {"set", "delete"}]
    if (len(reads), len(sets), len(deletes)) != (52, 5, 3):
        raise RuntimeError("provider callback counts disagree with their per-invocation reports")
    if any(operation["result"] is not None or operation["result_map_id"] != operation["map_id"] for operation in sets):
        raise RuntimeError("callback traversal lost Map.set's receiver-valued normal result")
    if ([operation["result"] for operation in deletes] !=
            ["#ctjs.boolean<false>", "#ctjs.boolean<true>", "#ctjs.boolean<true>"] or
        any(operation["result_map_id"] != 0 for operation in deletes) or
        deletes[0]["map_id"] != first["map_id"] or deletes[1]["map_id"] != first["map_id"]):
        raise RuntimeError("callback traversal lost failed deletion and later successful removals")
    diagnostic_members = {"keys", "Array.from", "snapshot[index]"}
    snapshots = [operation for operation in operations if operation["member"] in diagnostic_members]
    conflict_snapshots = [operation for operation in summaries[11]["operations"]
                          if operation["member"] in diagnostic_members]
    if ([operation["member"] for operation in snapshots] != ["keys", "Array.from", "snapshot[index]"] or
        snapshots != conflict_snapshots or
        [operation["result"] for operation in snapshots] != [None, None, '#ctjs.string<"bs.alert">'] or
        any(operation["map_id"] != first["map_id"] or operation["result_map_id"] != 0 for operation in snapshots)):
        raise RuntimeError("diagnostic snapshot lost evaluation order, key value or source Map identity")
    if [len(entry["callbacks"]) for entry in summaries] != [0] * 11 + [1] + [0] * 6:
        raise RuntimeError("the actual conflict invocation did not own its recorder callback")
    callback = summaries[11]["callbacks"][0]
    callback_call = callback["call_operation"]
    if (callback["target"] != "fn$1" or callback["result"] != "#ctjs.undefined" or
        type(callback_call) is not int or callback_call < 0 or callback_call in calls):
        raise RuntimeError("callback proof lost its actual source closure and internal call identity")
    writes = callback["writes"]
    if ([write["binding"] for write in writes] != ["traceErrorCount", "traceErrorMessage"] or
        [write["value"] for write in writes] != [number_attribute(1)] * 2):
        raise RuntimeError("callback proof did not commit the exact recorder's two scalar writes")
    write_operations = [write["operation"] for write in writes]
    if (any(type(operation) is not int or operation < 0 for operation in write_operations) or
        len(set(write_operations)) != 2 or callback_call in write_operations):
        raise RuntimeError("callback writes lost their distinct source operation identities")


def check_provider_objects(report, entry_boundary=""):
    check_provider_callbacks(report, object_payloads=True)
    summaries = report["provider_calls"]
    if report["boundary"] != entry_boundary or [row["target"] for row in summaries] != PROVIDER_OBJECT_TARGETS:
        raise RuntimeError("object payload traversal did not complete the exact Data method sequence")
    if [row["factory_index"] for row in summaries] != [0] * 23:
        raise RuntimeError("object payload traversal lost its actual factory")
    calls = [row["call_operation"] for row in summaries]
    if len(set(calls)) != 23 or any(type(call) is not int or call < 0 for call in calls):
        raise RuntimeError("object payload traversal lost its distinct source invocations")
    tail = summaries[18:]
    if [row["result"] for row in tail] != ["#ctjs.undefined", None, None, "#ctjs.null", number_attribute(21)]:
        raise RuntimeError("object payload traversal lost primitive or identity-valued results")
    object_id = tail[1]["result_object_id"]
    if object_id <= 0 or [row["result_object_id"] for row in tail] != [0, object_id, object_id, 0, 0]:
        raise RuntimeError("repeated Data.get did not retain the original object identity")
    if any(row["result_object_id"] for row in summaries[:18]):
        raise RuntimeError("object following changed an earlier primitive result")
    if [len(row["allocations"]) for row in tail] != [1, 0, 0, 0, 0]:
        raise RuntimeError("object insertion lost its single nested Map allocation")
    allocations = [allocation for row in summaries for allocation in row["allocations"]]
    if (len({row["map_id"] for row in allocations}) != 3 or
        len({row["allocation_operation"] for row in allocations}) != 1 or
        [row["invocation_operation"] for row in allocations] != [calls[2], calls[3], calls[18]]):
        raise RuntimeError("object reinsertion reused an earlier nested Map identity")
    provenance = {}
    operations = [operation for row in summaries for operation in row["operations"]]
    for row in summaries:
        for operation in [*row["allocations"], *row["operations"]]:
            map_id = operation["map_id"]
            origin = operation["allocation_operation"], operation["invocation_operation"]
            if (map_id <= 0 or min(origin) < 0 or
                map_id in provenance and provenance[map_id] != origin):
                raise RuntimeError("object traversal changed a Map's allocation/invocation provenance")
            provenance[map_id] = origin
    if len(provenance) != 4:
        raise RuntimeError("object traversal did not retain one outer and three inner Map identities")
    members = [operation["member"] for operation in operations]
    if (members.count("set"), members.count("delete"), len(members) - members.count("set") - members.count("delete")) != (7, 3, 68):
        raise RuntimeError("object traversal counters disagree with its runtime effects")
    returned = [operation for operation in operations if operation["result_object_id"]]
    if (len(returned) != 2 or any(operation["member"] != "get" or
            operation["result_object_id"] != object_id or operation["result"] is not None or
            operation["map_id"] != allocations[2]["map_id"] for operation in returned)):
        raise RuntimeError("object payload was copied or read from the wrong nested Map")
    origins = set()
    for row in tail:
        for operation in row["object_operations"]:
            if operation["object_id"] != object_id:
                continue
            origin = operation["allocation_operation"], operation["invocation_operation"]
            if min(origin) < 0 or operation["action"] != "retain":
                raise RuntimeError("exact instance retention lost its live source proof")
            origins.add(origin)
    if len(origins) != 1 or any(row["callbacks"] for row in tail):
        raise RuntimeError("object following changed instance provenance or invoked another callback")


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
    parser.add_argument("--follow-provider-mutations", action="store_true")
    parser.add_argument("--follow-provider-diagnostics", action="store_true")
    parser.add_argument("--follow-provider-callbacks", action="store_true")
    parser.add_argument("--follow-provider-objects", action="store_true")
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
    if args.follow_provider_mutations and (not args.follow_provider_reads or not args.follow_publication):
        parser.error("--follow-provider-mutations requires --follow-provider-reads and --follow-publication")
    if args.follow_provider_diagnostics and not args.follow_provider_mutations:
        parser.error("--follow-provider-diagnostics requires --follow-provider-mutations")
    if args.follow_provider_callbacks and not args.follow_provider_diagnostics:
        parser.error("--follow-provider-callbacks requires --follow-provider-diagnostics")
    if args.follow_provider_objects and not args.follow_provider_callbacks:
        parser.error("exact object probes require --follow-provider-callbacks")
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
    if args.follow_provider_mutations and probe.sha256(program) != PROVIDER_PROGRAM_SHA256[args.mode]:
        raise RuntimeError("provider mutation mode changed the exact provider-read JavaScript")
    provenance.update({"mode": args.mode, "program_sha256": probe.sha256(program),
                       "source_functions": function_count, "declared_observations": sorted(expected),
                       "follow_publication": args.follow_publication, "replacement": args.replace,
                       "follow_provider_reads": args.follow_provider_reads,
                       "follow_provider_mutations": args.follow_provider_mutations,
                       "follow_provider_diagnostics": args.follow_provider_diagnostics,
                       "follow_provider_callbacks": args.follow_provider_callbacks,
                       "follow_provider_objects": args.follow_provider_objects,
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
                  f"--ctnative-specialize-host-prefix=manifest={manifest} output={report_file} report=true follow-publication={str(args.follow_publication).lower()} follow-provider-reads={str(args.follow_provider_reads).lower()} follow-provider-mutations={str(args.follow_provider_mutations).lower()} follow-provider-diagnostics={str(args.follow_provider_diagnostics).lower()} follow-provider-callbacks={str(args.follow_provider_callbacks).lower()} follow-provider-objects={str(args.follow_provider_objects).lower()}",
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
    if args.follow_provider_mutations:
        expected_targets = ["fn$3", *PROVIDER_MUTATION_TARGETS, "fn$4"]
    if args.follow_provider_callbacks:
        expected_targets = ["fn$3", *PROVIDER_CALLBACK_TARGETS, "fn$4"]
    if args.follow_provider_objects:
        expected_targets = ["fn$3", *PROVIDER_OBJECT_TARGETS]
    if not report["valid"] or report["selected_branches"] != expected_branches or report["targets"] != expected_targets:
        raise RuntimeError(f"exact wrapper proof did not advance as expected: {report}")
    if report["resolved_calls"] != len(expected_targets) or report["full_host_contract_claimed"]:
        raise RuntimeError("prefix target accounting or incomplete-host boundary changed")
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
        if args.follow_provider_objects:
            # The fallback appends realm-identity observers after every Data
            # call. Their comparison remains outside the ordinary-object proof.
            boundary = "unproved comparison behavior at `ctjs.compare`" if realm_fallback else ""
            check_provider_objects(report, boundary)
        elif args.follow_provider_callbacks:
            check_provider_callbacks(report)
        elif args.follow_provider_diagnostics:
            check_provider_mutations(report, "unsupported provider path at `ctjs.call`")
        elif args.follow_provider_mutations:
            check_provider_mutations(report)
        else:
            if (report["summarized_provider_calls"], report["runtime_provider_reads"]) != (2, 2):
                raise RuntimeError(f"initial empty-Map reads did not advance to the first mutation: {report}")
            summaries = report["provider_reads"]
            if ([entry["target"] for entry in summaries] != ["fn$5", "fn$6"] or
                [entry["factory_index"] for entry in summaries] != [0, 0] or
                [entry["result"] for entry in summaries] != ["#ctjs.null", "#ctjs.undefined"]):
                raise RuntimeError("read summaries lost actual captures or null/undefined identity")
        for name in ("_script_$0", "fn$1", "fn$3", "fn$4", "fn$5", "fn$6"):
            pattern = r"ctjs\.func (?:private )?@" + re.escape(name) + r"\(.*?(?=\n  ctjs\.func |\n})"
            original = re.search(pattern, before, re.S)[0]
            current = re.search(pattern, after, re.S)[0]
            if original.count("scf.if") != current.count("scf.if"):
                raise RuntimeError(f"provider traversal rewrote {name} observation/method branches")
            if name != "_script_$0" and original != current:
                raise RuntimeError(f"provider traversal specialized reusable Data method {name}")
    elif report["summarized_provider_calls"] or report["runtime_provider_reads"]:
        raise RuntimeError("provider-read traversal became implicit")
    if not args.follow_provider_mutations and (report["provider_calls"] or
            report["runtime_provider_mutations"] or report["runtime_nested_provider_allocations"]):
        raise RuntimeError("provider mutation traversal became implicit")
    if not args.follow_provider_callbacks and (report["runtime_provider_callbacks"] or
            report["runtime_provider_global_writes"] or
            any(entry["callbacks"] for entry in report["provider_calls"])):
        raise RuntimeError("provider callback traversal became implicit")
    if not args.follow_provider_diagnostics and any(
            operation["member"] in {"keys", "Array.from", "snapshot[index]"}
            for entry in report["provider_calls"] for operation in entry["operations"]):
        raise RuntimeError("provider diagnostic traversal became implicit")

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
    if args.follow_provider_objects and (claimed, refused, pruned) != (0, 7, 0):
        raise RuntimeError("provider object prefix evidence changed the exact native export boundary")
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
