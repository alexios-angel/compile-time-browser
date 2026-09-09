"""Execute checked published Map methods and live primitive results without the VM."""

# Split out of native-owned-global-maps.py on 2026-09-08: this is its main(),
# verbatim. The docstring above is the one argparse prints, so it stays here.

import argparse
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

from .sources import (
    methods, owned, boundary, host, SOURCE, SHARED, parameter_sources, result_sources,
    seeded_result_sources, key_fact_sources, joined_result_sources, seeded_carrier_refusals,
    RESULT_SIGNATURES, refusal_sources, parameter_refusals, result_refusals,
    seeded_result_refusals, size_result_sources, size_result_refusals,
    payload_result_sources, payload_result_refusals, mixed_result_sources, mixed_result_refusals,
    saved_read_sources, saved_read_refusals, saved_join_sources, saved_join_refusals,
    guarded_saved_sources, guarded_saved_refusals, shortcircuit_sources, shortcircuit_refusals,
    nullable_result_sources, nullable_result_refusals, mixed_nullable_payload_refusals,
    nullable_key_sources, nullable_key_refusals, NULLABLE_OBSERVATIONS, NULLABLE_KEY_CALLS,
    nullable_payload_sources, nullable_payload_refusals, NULLABLE_PAYLOAD_CALLS,
    NULLABLE_PAYLOAD_READBACKS, nullable_host_result_sources, nullable_host_result_refusals,
    NULLABLE_HOST_RESULT_CALLS,
    nullable_nested_result_sources, nullable_nested_result_refusals, NULLABLE_NESTED_RESULT_CALLS,
    leaf_object_sources, leaf_object_refusals, LEAF_OBJECT_CALLS, LEAF_OBJECT_FUNCTIONS,
    leaf_readback_sources, leaf_readback_refusals, LEAF_READBACK_CALLS,
    LEAF_COMPARISON_REPAIRS, LEAF_COMPARISON_CASES,
    LEAF_READBACK_UNOWNED, LEAF_FIELD_RESULTS, leaf_field_result_refusals,
    leaf_absence_cases, leaf_absence_sources, leaf_absence_refusals,
    LEAF_ABSENCE_UNOWNED, LEAF_ABSENCE_PROMOTED_REFUSALS,
    primitive_absence_sources,
    leaf_clear_cases, leaf_clear_sources, leaf_clear_refusals,
    LEAF_CLEAR_UNOWNED, LEAF_CLEAR_PROMOTED,
    numeric_entry_cases, numeric_entry_sources, numeric_entry_refusals, NUMERIC_ENTRY_PROMOTED,
    NUMERIC_ENTRY_CARRIERS, NUMERIC_ENTRY_SAVED_GLOBALS,
    scalar_global_cases, scalar_global_sources, scalar_global_refusals, scalar_global_output,
    SCALAR_GLOBAL_CARRIERS, SCALAR_GLOBAL_INITIALIZED,
    constant_global_cases, constant_global_sources, normalized_scalar_output,
    CONSTANT_GLOBAL_UNOWNED, CONSTANT_GLOBAL_CARRIERS, CONSTANT_GLOBAL_EXISTING,
    StringValue,
)
from .harness import (
    source_calls, contract, resolve_getter, lifetime, standalone, check_call_preservation,
    forge_map_presence, check_budgets, check_prepared_result_calls,
    nullable_observer_source,
    leaf_object_observer_source, forge_leaf_evidence, comparison_identity_observer_source,
    LEAF_ABSENCE_LIFETIMES, leaf_absence_observer_source,
    primitive_absence_observer_source,
    LEAF_CLEAR_LIFETIMES, leaf_clear_observer_source,
    NUMERIC_ENTRY_LIFETIMES, numeric_entry_observer_source,
)



PRIMITIVE_ABSENCE_CARRIERS = {
    "seeded_cleared", "seeded_deleted", "seeded_deleted_earlier", "result_seeded_false_deleted",
    "result_seeded_mixed_false_deleted", "result_seeded_mixed_string_deleted",
    "saved_read_write_deleted", "saved_read_write_missing_source", "saved_join_deleted_true",
    "guarded_saved_mutated_arm", "shortcircuit_mutated_arm", "nullable_host_result_deleted",
}


def primitive_absence_carrier_cases():
    cases = {}
    mixed_string = "!ctnative.opt<!ctnative.variant<!ctnative.bool, !ctnative.str<utf8>>>"

    def add(name, source, value, old, replacement, repaired_value, calls, functions=6,
            carrier=mixed_string, *, repeated=False, size_argument=False):
        if source.count(old) != 1:
            raise RuntimeError(f"{name}: lost its exact absence repair")
        sequence = [("fn$3", 4), ("fn$4", 5), ("fn$3", 4)] if functions == 5 else (
            [("fn$4", 5 if repeated else 4), ("fn$5", 5)] * (2 if repeated else 1)
            + [("fn$3", 5 if size_argument else 4)])
        dependencies = [(1, 0)] + ([(3, 2)] if repeated else [])
        if size_argument:
            dependencies.append((4, 3))
        cases[name] = dict(source=source, value=value, repair=source.replace(old, replacement),
            old=old, replacement=replacement, repaired_value=repaired_value, calls=calls,
            functions=functions, carrier=carrier, sequence=sequence, dependencies=dependencies)

    for name, calls in (("seeded_deleted", 9), ("seeded_deleted_earlier", 10)):
        add(name, seeded_result_refusals()[name], "undefined", "state.delete(0); ", "", 1,
            calls, 5, "!ctnative.map<!ctnative.opt<!ctnative.num<i32>>, !ctnative.num<i32>>")
    add("seeded_cleared", seeded_result_refusals()["seeded_cleared"], "undefined",
        "state.clear();", "state.has(0);", 1, 9, 5,
        "!ctnative.map<!ctnative.opt<!ctnative.num<i32>>, !ctnative.num<i32>>")
    # The repair changes only the mutation; the evaluated method call remains.
    cases["seeded_cleared"]["repair_calls"] = 9
    source, value = payload_result_refusals()["result_seeded_false_deleted"]
    add("result_seeded_false_deleted", source, value, "state.delete(false); ", "", 1, 10,
        carrier="!ctnative.map<!ctnative.opt<!ctnative.bool>, !ctnative.bool>")
    for name, key, carrier in (
        ("result_seeded_mixed_false_deleted", "0",
         "!ctnative.map<!ctnative.opt<!ctnative.variant<!ctnative.bool, !ctnative.num<i32>>>, "
         "!ctnative.variant<!ctnative.bool, !ctnative.num<i32>>>"),
        ("result_seeded_mixed_string_deleted", "''", mixed_string),
    ):
        source, value = mixed_result_refusals()[name]
        add(name, source, value, f"state.delete({key}); ", "", 3, 10, carrier=carrier)
    source, value, _, _ = saved_read_refusals()["saved_read_write_deleted"]
    add("saved_read_write_deleted", source, value,
        "state.delete(false); const result = state.get(false);",
        "const result = state.get(false); state.delete(false);", 1, 12)
    source, value, old, replacement = saved_read_refusals()["saved_read_write_missing_source"]
    add("saved_read_write_missing_source", source, value, old, replacement, 1, 14)
    for name, rows, calls in (
        ("saved_join_deleted_true", saved_join_refusals(), 19),
        ("guarded_saved_mutated_arm", guarded_saved_refusals(), 20),
        ("shortcircuit_mutated_arm", shortcircuit_refusals(), 19),
        ("nullable_host_result_deleted", nullable_host_result_refusals(), 16),
    ):
        source, value, old, replacement, repaired_value = rows[name]
        add(name, source, value, old, replacement, repaired_value, calls, repeated=True,
            size_argument=name == "nullable_host_result_deleted")
    if cases.keys() != PRIMITIVE_ABSENCE_CARRIERS:
        raise RuntimeError("changed the measured primitive absence carrier inventory")
    return cases


def check_primitive_absence_preparation(text, original, case, name):
    calls = re.findall(r"^\s*(%[-\w.$]+) = ctjs\.call_direct @([-\w.$]+)\(([^\n]+)\) "
                       r"\{ctnative\.stored_call = 1 : i32\}", text, re.M)
    actuals = [arguments.split(", ") for _, _, arguments in calls]
    if (len(source_calls(text)) != case["calls"]
            or len(source_calls(original)) != case["calls"]
            or [(target, len(arguments)) for (_, target, _), arguments in zip(calls, actuals)]
            != case["sequence"]
            or any(actuals[consumer][-1] != calls[producer][0]
                   for consumer, producer in case["dependencies"])
            or f'ctjs.store_global "trace", {calls[-1][0]}' not in text):
        raise RuntimeError(f"{name}: absence carrier changed prepared result operands/order")
    entry = text.split("\n  }", 1)[0]
    receivers = dict(re.findall(r"(%[-\w.$]+) = ctjs\.get_property (%[-\w.$]+)\[", entry))
    captures = dict(re.findall(r"(%[-\w.$]+) = ctjs\.load_upvalue (%[-\w.$]+)\[0\]", entry))
    for arguments in actuals:
        if receivers.get(arguments[2]) != arguments[0] or captures.get(arguments[3]) != arguments[2]:
            raise RuntimeError(f"{name}: changed a current receiver/callee/capture operand")
    for action in ("set", "get", "has", "delete", "clear"):
        source_count = len(re.findall(rf"\bstate\.{action}\(", case["source"]))
        prepared_count = len(re.findall(rf'ctjs\.call [^\n]*ctnative\.map_action = "{action}"', text))
        if source_count != prepared_count:
            raise RuntimeError(f"{name}: changed {source_count} live Map.{action} calls")
    for operation in ("scf.if", "scf.yield", "ctjs.create_object", "ctjs.construct"):
        if text.count(operation) != original.count(operation):
            raise RuntimeError(f"{name}: changed live {operation} census")


def check_primitive_absence_carriers(args, node, reference):
    undefined_node = r"""const fs = require('node:fs'), vm = require('node:vm');
const context = vm.createContext({});
vm.runInContext(fs.readFileSync(process.argv[1], 'utf8'), context);
if (vm.runInContext('trace', context) !== undefined) throw new Error('lost Undefined trace');
process.stdout.write('trace=undefined\n');
"""
    for name, case in primitive_absence_carrier_cases().items():
        js, ir, count = boundary.prepare(args, name, case["source"])
        raw = args.work / f"{name}.raw.mlir"
        if (count != case["functions"] or len(source_calls(raw.read_text())) != case["calls"]
                or len(source_calls(ir.read_text())) != case["calls"]):
            raise RuntimeError(f"{name}: changed the exact source function/call census")
        observer = undefined_node if case["value"] == "undefined" else boundary.NODE
        expected = f'trace={case["value"]}\n'
        if (host.run([node, "-e", observer, str(js)]).stdout != expected
                or host.run([str(reference), str(js)]).stdout != expected):
            raise RuntimeError(f"{name}: Node/interpreter absent result mismatch")
        repaired_js, repaired_ir, repaired_count = boundary.prepare(args, name + "-restored", case["repair"])
        repaired_expected = f'trace={case["repaired_value"]}\n'
        if (repaired_count != count
                or host.run([node, "-e", boundary.NODE, str(repaired_js)]).stdout != repaired_expected
                or host.run([str(reference), str(repaired_js)]).stdout != repaired_expected):
            raise RuntimeError(f"{name}: exact repair lost its independent observation")
        if "repair_calls" in case:
            repaired_raw = args.work / f"{name}-restored.raw.mlir"
            if (len(source_calls(repaired_raw.read_text())) != case["repair_calls"]
                    or len(source_calls(repaired_ir.read_text())) != case["repair_calls"]):
                raise RuntimeError(f"{name}: exact repair dropped an evaluated source call")
        config = contract(args, ir, name)
        repaired_config = contract(args, repaired_ir, name + "-restored")
        for mode, options in (("default", ""), ("disabled", "optimize=false")):
            def reject(input_ir, label, current_config):
                failed = owned.lower(args, input_ir, label, current_config, options=options, cleanup=False)
                text = methods.census(failed, count, label, admitted=0)
                origin = "ctjs.load_upvalue" if case["carrier"].startswith("!ctnative.map<") else "ctjs.call_direct"
                expected_reason = f'a value of type {case["carrier"]} from `{origin}`'
                if ("ctnative.host_owner_proved = true" not in text
                        or expected_reason not in boundary.REFUSAL.findall(text)):
                    raise RuntimeError(f"{label}: lost exact complete-owner carrier diagnostic")
                if origin == "ctjs.load_upvalue":
                    reason = ("native Map needs supported keys and numeric, boolean, closed mixed, "
                        "owning-string, object-identity union or acyclic Map values; inferred " + case["carrier"])
                else:
                    reason = "stored callable result has no supported concrete signature"
                if reason not in boundary.REFUSAL.findall(text):
                    raise RuntimeError(f"{label}: lost independent Map/result carrier refusal")
                check_primitive_absence_preparation(text, input_ir.read_text(), case, label)
                return failed

            label = name + "-" + mode
            reject(ir, label, config)
            repaired = owned.lower(args, repaired_ir, label + "-restored", repaired_config, options=options)
            repaired_text = methods.census(repaired, count, label + "-restored", admitted=count)
            if "ctnative.host_owner_proved = true" not in repaired_text:
                raise RuntimeError(f"{label}: exact absence repair lost native ownership")
            for payload in ("bool", "string", "nullable_string"):
                forged_name = label + "-forged-" + payload
                forged = args.work / f"{forged_name}.mlir"
                forged.write_text(forge_map_presence(ir.read_text(), payload))
                stale = methods.refused(args, forged, forged_name + "-stale", config,
                    options=options, reason="fingerprint mismatch", admitted=0)
                check_call_preservation(forged.read_text(), stale.read_text(), forged_name + "-stale")
                fresh = contract(args, forged, forged_name)
                checked = reject(forged, forged_name, fresh)
                rerun = methods.refused(args, checked, forged_name + "-rerun", fresh,
                    options=options, reason="fingerprint mismatch", admitted=0)
                check_call_preservation(checked.read_text(), rerun.read_text(), forged_name + "-rerun")


def check_primitive_absence_observations(args, node, reference):
    source = primitive_absence_sources()["result_seeded_empty_deleted"][0]
    observed = primitive_absence_observer_source(source)
    js = args.work / "primitive-absence-observed.js"
    js.write_text(observed)
    if (host.run([node, "-e", boundary.NODE, str(js)]).stdout != "trace=255\n"
            or host.run([str(reference), str(js)]).stdout != "trace=255\n"):
        raise RuntimeError("empty String deletion lost independent Undefined/empty/future key observations")
    for index, (old, replacement) in enumerate((
        ("state.delete('');", "state.has('');"),
        ("state.set('', 'stored'); ", ""),
        ("return state.get('');", "return '';"),
    )):
        if source.count(old) != 1:
            raise RuntimeError("primitive absence mutation lost its exact source")
        js = args.work / f"primitive-absence-blind-{index}.js"
        js.write_text(primitive_absence_observer_source(source.replace(old, replacement)))
        if host.run([node, "-e", boundary.NODE, str(js)]).stdout == "trace=255\n":
            raise RuntimeError("primitive absence observer cannot distinguish a missing mutation/read")



def check_primitive_absence_forgeries(args, saved, node, reference):
    name = "result_seeded_empty_deleted"
    ir, config, output = saved[name]
    expected_cpp = comparable_provenance(
        host.run([args.translate, "--mlir-to-cpp", str(output)]).stdout, ir)
    source = primitive_absence_sources()[name][0]
    old = "state.delete('');"
    if source.count(old) != 1:
        raise RuntimeError("empty String absence lost its exact deletion repair")
    js, repaired_ir, count = boundary.prepare(args, name + "-restored", source.replace(old, "state.has('');"))
    if (count != 6 or len(source_calls(repaired_ir.read_text())) != 10
            or host.run([node, "-e", boundary.NODE, str(js)]).stdout != "trace=1\n"
            or host.run([str(reference), str(js)]).stdout != "trace=1\n"):
        raise RuntimeError("empty String absence repair lost its independent trace/calls")
    repaired_config = contract(args, repaired_ir, name + "-restored")
    for mode, options in (("default", ""), ("disabled", "optimize=false")):
        repaired = owned.lower(args, repaired_ir, name + "-" + mode + "-restored", repaired_config,
                               options=options)
        methods.census(repaired, 6, name + "-restored", admitted=6)
        for payload in ("bool", "string", "nullable_string"):
            label = name + "-" + mode + "-forged-" + payload
            forged = args.work / f"{label}.mlir"
            forged.write_text(forge_map_presence(ir.read_text(), payload))
            stale = methods.refused(args, forged, label + "-stale", config,
                options=options, reason="fingerprint mismatch", admitted=0)
            check_call_preservation(forged.read_text(), stale.read_text(), label + "-stale")
            fresh = contract(args, forged, label)
            checked = owned.lower(args, forged, label, fresh, options=options)
            text = methods.census(checked, 6, label, admitted=6)
            cpp = host.run([args.translate, "--mlir-to-cpp", str(checked)]).stdout
            if ("ctnative.host_owner_proved = true" not in text
                    or comparable_provenance(cpp, forged) != expected_cpp):
                raise RuntimeError(f"{label}: forged scalar facts changed native Undefined/key semantics")
    rerun = owned.lower(args, output, name + "-rerun", config, cleanup=False)
    text = methods.census(rerun, 6, name + "-rerun", admitted=6)
    if "ctnative.host_owner_proved = false" not in text or "fingerprint mismatch" not in text:
        raise RuntimeError("prepared primitive absence source reused stale owner authority")


def comparable_provenance(cpp, input_ir):
    # Reparsed forged input has a different filename but the same source
    # locations. Keep every line/column, other comment byte and emitted token.
    filename = re.compile(r"(?<!\S)" + re.escape(str(input_ir)) + r"(?=:\d+:\d+(?:\D|$))")
    return "".join(filename.sub("<input>", line) if line.startswith("// ctcompile:") else line
                   for line in cpp.splitlines(keepends=True))


def check_leaf_object_observations(args, node, reference):
    for name, (source, _, _) in leaf_object_sources().items():
        if name in {"leaf_object_number_repair", "leaf_object_string_repair"}:
            continue
        observed, value = leaf_object_observer_source(source, name)
        observed_js = args.work / f"{name}-object-observer.js"
        observed_js.write_text(observed)
        if (host.run([node, "-e", boundary.NODE, str(observed_js)]).stdout != f"trace={value}\n"
                or host.run([str(reference), str(observed_js)]).stdout != f"trace={value}\n"):
            raise RuntimeError(f"{name}: independent future object identity/field mismatch")
        mutations = [("const item = {};", "const item = host;")] if name == "leaf_object_plain" else []
        if name in {"leaf_object_number_field", "leaf_object_identity_repair"}:
            mutations = [("const item = {value: 1};", "const item = host;"), ("{value: 1}", "{value: 0}")]
        if name in {"leaf_object_scalar_writes", "leaf_object_alias", "leaf_object_lifetime"}:
            mutations = [("item.value = state.size;", "item.value = 1;"),
                         ("item.flag = false;", "item.flag = true;"),
                         ("empty: null", "empty: 0"), ("absent: void 0", "absent: null")]
        for index, (old, replacement) in enumerate(mutations):
            if source.count(old) != 1:
                raise RuntimeError(f"{name}: leaf observer mutation lost its unique source origin")
            mutated, _ = leaf_object_observer_source(source.replace(old, replacement), name)
            blind = args.work / f"{name}-object-observer-blind-{index}.js"
            blind.write_text(mutated)
            if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == f"trace={value}\n":
                raise RuntimeError(f"{name}: object identity/field observer cannot distinguish {replacement}")


def check_leaf_object_forgeries(args, saved, names=None):
    for name in names or ("leaf_object_plain", "leaf_object_scalar_writes", "leaf_object_lifetime"):
        ir, config, output = saved[name]
        functions = LEAF_OBJECT_FUNCTIONS.get(name, 5)
        expected_cpp = comparable_provenance(
            host.run([args.translate, "--mlir-to-cpp", str(output)]).stdout, ir)
        for mode, options in (("default", ""), ("disabled", "optimize=false")):
            for payload in ("bool", "string", "nullable_string"):
                forged_name = name + "-" + mode + "-forged-" + payload
                forged = args.work / f"{forged_name}.mlir"
                forged.write_text(forge_leaf_evidence(ir.read_text(), payload))
                failed = methods.refused(args, forged, forged_name + "-stale", config,
                    options=options, reason="fingerprint mismatch", admitted=0)
                check_call_preservation(forged.read_text(), failed.read_text(), forged_name + "-stale")
                if name in scalar_global_cases() or name in constant_global_cases():
                    check_scalar_global_preparation(failed.read_text(), forged.read_text(), name)
                fresh = contract(args, forged, forged_name)
                checked = owned.lower(args, forged, forged_name, fresh, options=options)
                text = methods.census(checked, functions, forged_name, admitted=functions)
                if ("ctnative.host_owner_proved = true" not in text
                        or comparable_provenance(
                            host.run([args.translate, "--mlir-to-cpp", str(checked)]).stdout,
                            forged) != expected_cpp):
                    raise RuntimeError(f"{forged_name}: forged leaf facts changed native owners or fields")


def check_leaf_object_refusals(args, positives, node, reference, controls=None):
    if controls is None:
        controls = {name: row for name, row in leaf_object_refusals().items()
                    if name not in {"leaf_object_saved_identity", "leaf_object_distinct_identity",
                                    *LEAF_ABSENCE_PROMOTED_REFUSALS}}
    for name, (source, value, old, replacement, repaired_name, calls) in controls.items():
        def preserved(before, after, label):
            check_call_preservation(before, after, label)
            if name in scalar_global_cases() or name in constant_global_cases():
                check_scalar_global_preparation(after, before, name)

        js, rejected, count = boundary.prepare(args, name, source)
        check_leaf_absence_census(args, rejected, name)
        if count != 5 or len(source_calls(rejected.read_text())) != calls:
            raise RuntimeError(f"{name}: changed the exact leaf-object source census")
        if (host.run([node, "-e", numeric_node_observer(value), str(js)]).stdout != f"trace={value}\n"
                or host.run([str(reference), str(js)]).stdout != numeric_reference_output(name, value)):
            raise RuntimeError(f"{name}: Node/interpreter leaf-object refusal mismatch")
        if source.count(old) != 1 or source.replace(old, replacement) != positives[repaired_name][0]:
            raise RuntimeError(f"{name}: repair no longer restores its independently gated source")
        _, restored, restored_count = boundary.prepare(args, name + "-restored", source.replace(old, replacement))
        if restored_count != LEAF_OBJECT_FUNCTIONS.get(repaired_name, 5):
            raise RuntimeError(f"{name}: changed repaired source function census")
        config = contract(args, rejected, name)
        restored_config = contract(args, restored, name + "-restored")
        for mode, options in (("default", ""), ("disabled", "optimize=false")):
            mode_name = name + "-" + mode
            failed = methods.refused(args, rejected, mode_name, config, options=options, admitted=0)
            preserved(rejected.read_text(), failed.read_text(), mode_name)
            postdelete = (name in LEAF_ABSENCE_UNOWNED or name in LEAF_CLEAR_UNOWNED
                          or name in numeric_entry_refusals() or name in scalar_global_refusals())
            if postdelete and "ctnative.host_owner_proved = false" not in failed.read_text():
                raise RuntimeError(f"{name}: possible absence manufactured a complete host owner")
            repaired = owned.lower(args, restored, mode_name + "-restored", restored_config, options=options)
            text = methods.census(repaired, restored_count, mode_name + "-restored", admitted=restored_count)
            if "ctnative.host_owner_proved = true" not in text:
                raise RuntimeError(f"{name}: exact leaf-object repair did not restore ownership")
            for payload in ("bool", "string", "nullable_string"):
                forged_name = mode_name + "-forged-" + payload
                forged = args.work / f"{forged_name}.mlir"
                forged.write_text(forge_leaf_evidence(rejected.read_text(), payload))
                stale = methods.refused(args, forged, forged_name + "-stale", config,
                    options=options, reason="fingerprint mismatch", admitted=0)
                preserved(forged.read_text(), stale.read_text(), forged_name + "-stale")
                fresh = contract(args, forged, forged_name)
                failed = methods.refused(args, forged, forged_name, fresh, options=options, admitted=0)
                if "fingerprint mismatch" in failed.read_text():
                    raise RuntimeError(f"{forged_name}: skipped independent leaf/use reanalysis")
                preserved(forged.read_text(), failed.read_text(), forged_name)
                if postdelete and "ctnative.host_owner_proved = false" not in failed.read_text():
                    raise RuntimeError(f"{forged_name}: forged absence manufactured a host owner")
                rerun = methods.refused(args, failed, forged_name + "-rerun", fresh,
                                        options=options, admitted=0)
                preserved(forged.read_text(), rerun.read_text(), forged_name + "-rerun")


def check_leaf_readback_carriers(args, positives, node, reference, controls):
    for name, (source, value, old, replacement, repair, source_call_count) in controls.items():
        js, ir, count = boundary.prepare(args, name, source)
        if count != 5 or len(source_calls(ir.read_text())) != source_call_count:
            raise RuntimeError(f"{name}: changed exact complete-owner carrier source")
        expected = f"trace={value}\n"
        if (host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected
                or host.run([str(reference), str(js)]).stdout != expected):
            raise RuntimeError(f"{name}: complete-owner carrier observation mismatch")
        if source.count(old) != 1 or source.replace(old, replacement) != positives[repair][0]:
            raise RuntimeError(f"{name}: carrier repair changed its exact source")
        config = contract(args, ir, name)

        def check_prepared(output, label):
            text = methods.census(output, 5, label, admitted=0)
            calls = re.findall(r"^\s*(%[-\w.$]+) = ctjs\.call_direct @([-\w.$]+)\(([^\n]+)\) "
                               r"\{ctnative\.stored_call = 1 : i32\}", text, re.M)
            if ("ctnative.host_owner_proved = true" not in text
                    or len(source_calls(text)) != source_call_count
                    or [callee for _, callee, _ in calls] != ["fn$3", "fn$4"]
                    or [len(actuals.split(", ")) for _, _, actuals in calls]
                    != [4, 5]
                    or f'ctjs.store_global "trace", {calls[-1][0]}' not in text):
                raise RuntimeError(f"{label}: lost complete local origins or prepared published calls")

        for mode, options in (("default", ""), ("disabled", "optimize=false")):
            output = owned.lower(args, ir, name + "-" + mode, config, options=options, cleanup=False)
            check_prepared(output, name + "-" + mode)
            for payload in ("bool", "string", "nullable_string"):
                label = name + "-" + mode + "-forged-" + payload
                forged = args.work / f"{label}.mlir"
                forged.write_text(forge_leaf_evidence(ir.read_text(), payload))
                stale = methods.refused(args, forged, label + "-stale", config,
                    options=options, reason="fingerprint mismatch", admitted=0)
                check_call_preservation(forged.read_text(), stale.read_text(), label + "-stale")
                fresh = contract(args, forged, label)
                checked = owned.lower(args, forged, label, fresh, options=options, cleanup=False)
                check_prepared(checked, label)
                rerun = methods.refused(args, checked, label + "-rerun", fresh,
                    options=options, reason="fingerprint mismatch", admitted=0)
                check_call_preservation(checked.read_text(), rerun.read_text(), label + "-rerun")


def check_leaf_readback_observations(args, node, reference):
    positives = leaf_readback_sources()
    for name, old, replacement in (
        ("local_identity_saved", "saved === item", "saved !== item"),
        ("local_identity_distinct_stored", "saved === replacement", "saved === item"),
        ("historical_object_saved_identity", "saved === item", "state.get(key) === item"),
        ("local_field_get", "return saved.value;", "return 0;"),
        ("local_field_saved_overwrite", "return saved === item ? saved.value : 0;",
         "return state.get(key).value;"),
        ("local_field_saved_alias_write", "item.value = 2;", "item.value = 1;"),
        ("local_field_boolean", "value: false", "value: true"),
        ("local_field_null", "value: null", "value: void 0"),
        ("local_field_undefined", "value: void 0", "value: null"),
        ("local_identity_repeated_keys", "return saved === item ? 1 : 0;", "return state.size;"),
        ("local_field_readback_lifetime", "item.value = value;", "item.value = 3;"),
    ):
        source, _, value = positives[name]
        if source.count(old) != 1:
            raise RuntimeError(f"{name}: missing the independent readback observation")
        blind = args.work / f"{name}-readback-blinded.js"
        blind.write_text(source.replace(old, replacement))
        if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == f"trace={value}\n":
            raise RuntimeError(f"{name}: readback observation cannot distinguish {replacement}")
    # The future caller supplies values absent from the startup script and
    # retains the callable independently of the published owner.
    future = """
(function() {
    const setter = host.slot.set, size = host.slot.size;
    host = {};
    trace = 0;
    if (setter('future', 17) === 17) { trace += 1; }
    if (setter('future', -3) === -3) { trace += 2; }
    if (setter('other', 0) === 0) { trace += 4; }
    if (setter('other', 1.5) === 1.5) { trace += 8; }
    if (size() === 0) { trace += 16; }
})();
"""
    for name in ("local_field_readback_lifetime_checked", "local_field_readback_lifetime"):
        source = positives[name][0] + future
        observed = args.work / f"{name}-future.js"
        observed.write_text(source)
        expected = "trace=31\n"
        if (host.run([node, "-e", boundary.NODE, str(observed)]).stdout != expected
                or host.run([str(reference), str(observed)]).stdout != expected):
            raise RuntimeError(f"{name}: future callable field/identity observation mismatch")
        if name.endswith("_checked"):
            continue
        for index, (old, replacement) in enumerate((
            ("item.value = value;", "item.value = 3;"),
            ("state.delete(key);", "state.has(key);"),
            ("return saved === item ? saved.value : 0;", "return saved === item ? 1 : 0;"),
        )):
            if source.count(old) != 1:
                raise RuntimeError(f"{name}: lost a unique future field observation")
            blind = args.work / f"{name}-future-blinded-{index}.js"
            blind.write_text(source.replace(old, replacement))
            if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == expected:
                raise RuntimeError(f"{name}: future field observation cannot distinguish {replacement}")


def check_comparison_identity_observations(args, node, reference):
    positives = leaf_readback_sources()
    for name, repair in LEAF_COMPARISON_REPAIRS.items():
        source, _, value = positives[name]
        expression = "saved === {value: 1}" if name.startswith("historical_") else "saved === {}"
        if (source.count(expression) != 1
                or source.replace(expression, "saved === item") != positives[repair][0]
                or value == positives[repair][2]):
            raise RuntimeError(f"{name}: distinct identity lost its exact saved-object repair")
    for name in LEAF_COMPARISON_CASES:
        source = positives[name][0]
        observed, expected = comparison_identity_observer_source(source, name)
        js = args.work / f"{name}-comparison-future.js"
        js.write_text(observed)
        if (host.run([node, "-e", boundary.NODE, str(js)]).stdout != f"trace={expected}\n"
                or host.run([str(reference), str(js)]).stdout != f"trace={expected}\n"):
            raise RuntimeError(f"{name}: future strict identity or saved object observation mismatch")
        mutations = [("saved ===", "saved !=="), ("const item =", "const item = host; const unused =")]
        if name.startswith("historical_"):
            mutations.extend([("value: 1", "value: 2"), ("state.delete(key);", "state.has(key);")])
        for index, (old, replacement) in enumerate(mutations):
            if old not in source:
                raise RuntimeError(f"{name}: lost independent identity mutation {old}")
            blind, _ = comparison_identity_observer_source(source.replace(old, replacement), name)
            js = args.work / f"{name}-comparison-blind-{index}.js"
            js.write_text(blind)
            if host.run([node, "-e", boundary.NODE, str(js)]).stdout == f"trace={expected}\n":
                raise RuntimeError(f"{name}: identity observer cannot distinguish {replacement}")


def check_leaf_absence_census(args, ir, name):
    case = {**leaf_absence_cases(), **leaf_clear_cases(), **numeric_entry_cases(), **scalar_global_cases(), **constant_global_cases()}.get(name)
    if case is None:
        return
    check_scalar_global_source(ir, name)
    raw = args.work / f"{name}.raw.mlir"
    if (len(source_calls(raw.read_text())), len(source_calls(ir.read_text()))) != (
            case["raw_calls"], case["prepared_calls"]):
        raise RuntimeError(f"{name}: changed the independent raw/prepared call census")
    if ("distinct_branches_" in name or name.startswith("local_clear_both_branches_")) \
            and ir.read_text().count("scf.if") < 2:
        raise RuntimeError(f"{name}: lost the nonidentical two-arm absence join")


def numeric_reference_output(name, value):
    return scalar_global_output(name, value)


def numeric_node_observer(value):
    if not isinstance(value, bool) and value not in {"NaN", "-Infinity"}:
        return boundary.NODE
    predicate = "typeof trace !== 'number' || !Number.isFinite(trace)"
    if boundary.NODE.count(predicate) != 1:
        raise RuntimeError("exact NaN observer lost the shared finite-number control")
    condition = ("typeof trace !== 'boolean'" if isinstance(value, bool)
                 else "typeof trace !== 'number' || !Number.isNaN(trace)" if value == "NaN"
                 else "typeof trace !== 'number' || trace !== -Infinity")
    return boundary.NODE.replace(predicate, condition)


def check_numeric_global_preparation(text, original, name):
    entry = text.split("\n  }", 1)[0]
    calls = re.findall(r"^\s*(%[-\w.$]+) = ctjs\.call_direct @([-\w.$]+)\(([^\n]+)\) "
                       r"\{ctnative\.stored_call = 1 : i32\}", entry, re.M)
    actuals = [arguments.split(", ") for _, _, arguments in calls]
    if (len(source_calls(text)) != 8 or len(source_calls(original)) != 8
            or [(target, len(arguments)) for (_, target, _), arguments in zip(calls, actuals)]
            != [("fn$3", 4), ("fn$4", 5), ("fn$4", 5), ("fn$4", 5)]):
        raise RuntimeError(f"{name}: saved-global refusal changed the eight evaluated calls")
    receivers = dict(re.findall(r"(%[-\w.$]+) = ctjs\.get_property (%[-\w.$]+)\[", entry))
    captures = dict(re.findall(r"(%[-\w.$]+) = ctjs\.load_upvalue (%[-\w.$]+)\[0\]", entry))
    for arguments in actuals:
        if receivers.get(arguments[2]) != arguments[0] or captures.get(arguments[3]) != arguments[2]:
            raise RuntimeError(f"{name}: saved-global refusal changed receiver/callee/capture provenance")
    snapshot = "saved_snapshot" in name
    names = ("first", "second") if snapshot else ("first", "second", "third")
    for index, binding in enumerate(names, 1):
        if re.findall(rf'ctjs\.store_global "{binding}", (%[-\w.$]+)', entry) != [calls[index][0]]:
            raise RuntimeError(f"{name}: changed the live {binding} result store")
    values, binaries = {}, []
    for line in entry.splitlines():
        if match := re.search(r'(%[-\w.$]+) = ctjs\.load_global "([^\"]+)"', line):
            values[match[1]] = "global:" + match[2]
        elif match := re.search(r"(%[-\w.$]+) = ctjs\.constant #ctjs\.number<(\d+)>", line):
            values[match[1]] = struct.unpack("d", struct.pack("Q", int(match[2])))[0]
        elif match := re.search(r"(%[-\w.$]+) = ctjs\.binary (\w+) (%[-\w.$]+), (%[-\w.$]+)", line):
            binaries.append((match[2], values.get(match[3]), values.get(match[4])))
            values[match[1]] = "binary:" + str(len(binaries) - 1)
    expected = ([("mul", "global:first", 10.0), ("add", "binary:0", "global:second")]
                if snapshot else [("add", "global:first", "global:second"),
                                  ("add", "binary:0", "global:third")])
    trace = re.findall(r'ctjs\.store_global "trace", (%[-\w.$]+)', entry)
    if binaries != expected or len(trace) != 1 or values.get(trace[0]) != "binary:1":
        raise RuntimeError(f"{name}: changed saved-global arithmetic operands: {binaries}")
    for operation in ("scf.if", "scf.yield", "ctjs.create_object", "ctjs.construct"):
        if text.count(operation) != original.count(operation):
            raise RuntimeError(f"{name}: saved-global refusal changed live {operation}")


def scalar_string_literal(text, mlir=False):
    """Compare literal bytes despite MLIR hex and C++ octal spellings."""
    if text.startswith('R"('):
        return text[3:-2]
    out = bytearray()
    content = text[1:-1]
    while content:
        if content[0] != "\\":
            out.extend(content[0].encode("utf-8", "surrogatepass"))
            content = content[1:]
            continue
        pattern = r"\\([0-9A-Fa-f]{2})" if mlir else r"\\([0-7]{1,3})"
        match = re.match(pattern, content)
        if match:
            out.append(int(match[1], 16 if mlir else 8))
            content = content[match.end():]
        else:
            out.extend({"n": b"\n", "t": b"\t", "r": b"\r"}.get(
                content[1], content[1].encode()))
            content = content[2:]
    return out.decode("utf-8", "surrogatepass")


def scalar_global_graph(text, name):
    """Compare current scalar dataflow before and after callable preparation."""
    entry = text.split("\n  }", 1)[0]
    values, properties, captures = {}, {}, {}
    events, calls, binaries = [], 0, 0
    for line in entry.splitlines():
        if match := re.search(r'(%[-\w.$]+) = ctjs\.load_global "([^\"]+)"', line):
            values[match[1]] = "global:" + match[2]
            if match[2] != "host":
                events.append(("load", match[2]))
        elif match := re.search(r'ctjs\.store_global "([^\"]+)", (%[-\w.$]+)', line):
            if match[1] != "host":
                events.append(("store", match[1], values.get(match[2], "unknown")))
        elif match := re.search(r'(%[-\w.$]+) = ctjs\.constant #ctjs\.string<("(?:[^"\\]|\\.)*")>', line):
            values[match[1]] = ("string", scalar_string_literal(match[2], mlir=True))
        elif match := re.search(r"(%[-\w.$]+) = ctjs\.constant #ctjs\.number<(\d+)>", line):
            values[match[1]] = struct.unpack("d", struct.pack("Q", int(match[2])))[0]
        elif match := re.search(r"(%[-\w.$]+) = ctjs\.constant #ctjs\.(?:boolean|bool)<(true|false)>", line):
            values[match[1]] = ("boolean", match[2])
        elif match := re.search(r"(%[-\w.$]+) = ctjs\.get_property (%[-\w.$]+)\[(%[-\w.$]+)\]", line):
            properties[match[1]] = (match[2], values.get(match[3]))
        elif match := re.search(r"(%[-\w.$]+) = ctjs\.load_upvalue (%[-\w.$]+)\[0\]", line):
            captures[match[1]] = match[2]
        elif match := re.search(r"(%[-\w.$]+) = ctjs\.binary (\w+) (%[-\w.$]+), (%[-\w.$]+)", line):
            events.append(("binary", match[2], values.get(match[3], "unknown"),
                           values.get(match[4], "unknown")))
            values[match[1]] = "binary:" + str(binaries)
            binaries += 1
        else:
            direct = re.search(r"(%[-\w.$]+) = ctjs\.call_direct @([-\w.$]+)\(([^\n]+)\).*ctnative\.stored_call", line)
            ordinary = re.search(r"(%[-\w.$]+) = ctjs\.call (%[-\w.$]+)\(([^\n)]+)\)", line)
            if direct:
                result, target, arguments = direct.groups()
                actuals = arguments.split(", ")
                receiver, callee = actuals[0], actuals[2]
                if captures.get(actuals[3]) != callee:
                    raise RuntimeError(f"{name}: changed current published callable capture")
                arguments = actuals[4:]
            elif ordinary:
                result, callee, arguments = ordinary.groups()
                actuals = arguments.split(", ")
                receiver, arguments = actuals[0], actuals[1:]
                target = None
            else:
                continue
            method = properties.get(callee)
            if not method or method[1] not in {("string", "size"), ("string", "set")}:
                continue
            table = properties.get(receiver)
            if (method[0] != receiver or not table or table[1] != ("string", "slot")
                    or values.get(table[0]) != "global:host"):
                raise RuntimeError(f"{name}: changed current published receiver/callee")
            method = method[1][1]
            if target and target != ("fn$3" if method == "size" else "fn$4"):
                raise RuntimeError(f"{name}: changed current published callee target")
            events.append(("call", method, tuple(values.get(argument, "unknown") for argument in arguments)))
            values[result] = "call:" + str(calls)
            calls += 1
    return events


def check_scalar_global_preparation(text, original, name):
    before, after = scalar_global_graph(original, name), scalar_global_graph(text, name)
    if before != after or not before:
        raise RuntimeError(f"{name}: preparation changed live scalar stores/loads/calls/arithmetic\n{before}\n{after}")
    for operation in ("scf.if", "scf.yield", "ctjs.create_object", "ctjs.construct", "ctjs.store_property"):
        if text.count(operation) != original.count(operation):
            raise RuntimeError(f"{name}: preparation changed live {operation}")


def check_scalar_global_source(ir, name):
    if name not in NUMERIC_ENTRY_SAVED_GLOBALS and name not in scalar_global_cases() and name not in constant_global_cases():
        return
    graph = scalar_global_graph(ir.read_text(), name)
    calls = [event for event in graph if event[0] == "call"]
    stores = [event for event in graph if event[0] == "store"]
    loads = [event for event in graph if event[0] == "load"]
    if not calls or calls[0] != ("call", "size", ()):
        raise RuntimeError(f"{name}: lost the first evaluated size observation")
    cases = {**numeric_entry_cases(), **scalar_global_cases(), **constant_global_cases()}
    source = cases[name]["source"]
    if len(calls) != len(re.findall(r"host\.slot\.(?:size|set)\(", source)):
        raise RuntimeError(f"{name}: lost an evaluated published call")
    if name in NUMERIC_ENTRY_SAVED_GLOBALS:
        snapshot = name == "local_numeric_saved_snapshot"
        names = ("first", "second") if snapshot else ("first", "second", "third")
        if [event for event in stores if event[1] != "trace"] != [
                ("store", binding, "call:" + str(index)) for index, binding in enumerate(names, 1)]:
            raise RuntimeError(f"{name}: lost the exact original call-result stores")
        if loads != [("load", binding) for binding in names]:
            raise RuntimeError(f"{name}: lost the exact original scalar loads")
        expected = ([("binary", "mul", "global:first", 10.0),
                     ("binary", "add", "binary:0", "global:second")]
                    if snapshot else [("binary", "add", "global:first", "global:second"),
                                      ("binary", "add", "binary:0", "global:third")])
        if [event for event in graph if event[0] == "binary"] != expected:
            raise RuntimeError(f"{name}: lost the original two arithmetic dependencies")
    written = set()
    early = []
    for event in graph:
        if event[0] == "store":
            written.add(event[1])
        elif event[0] == "load" and event[1] not in written:
            early.append(event[1])
    expected_early = {"scalar_read_before_write": ["first"],
                      "constant_read_before_write": ["fixed"],
                      "constant_dynamic_global": ["globalThis"],
                      "constant_boolean_read_before_write": ["fixed"],
                      "constant_boolean_dynamic_global": ["globalThis"],
                      "constant_string_read_before_write": ["fixed"]}.get(name, [])
    if early != expected_early:
        raise RuntimeError(f"{name}: changed source store/load order: {early}")
    first_writes = sum(event[:2] == ("store", "first") for event in stores)
    if name.startswith("scalar_duplicate_") and first_writes != 2:
        raise RuntimeError(f"{name}: erased a second live scalar write")
    aliases = {
        "scalar_alias": [("alias", "first")],
        "scalar_single_write_repair": [("trace", "first")],
        "scalar_alias_chain": [("saved", "first"), ("alias", "saved")],
        "scalar_alias_arithmetic": [("alias", "total")],
        "scalar_alias_branch_lifetime": [("left", "first"), ("middle", "second"),
                                          ("right", "third")],
        "scalar_constant_only": [("copy", "fixed")],
        "constant_alias_chain": [("offset", "fixed"), ("copy", "offset")],
        "constant_alias_arithmetic": [("copy", "offset")],
        "constant_branch_lifetime": [("left", "first"), ("middle", "second"),
                                     ("right", "third"), ("offset", "fixed"), ("copy", "offset")],
    }.get(name, [])
    if name in {"constant_duplicate_write", "constant_boolean_duplicate_write",
                "constant_boolean_mixed_write", "constant_string_duplicate_write",
                "constant_string_mixed_write"} and sum(
            event[:2] == ("store", "fixed") for event in stores) != 2:
        raise RuntimeError(f"{name}: erased the second constant-only global write")
    constant = constant_global_cases().get(name)
    if constant:
        source = constant["source"]
        aliases += [(destination, origin) for destination, origin in re.findall(
            r"(?:const|var) (\w+) = (\w+);", source.rsplit("});\n", 1)[1])
            if origin in constant["saved"]]
    for destination, origin in aliases:
        expected = ("store", destination, "global:" + origin)
        writes = [event for event in stores if event[1] == destination]
        if writes != [expected] or ("load", origin) not in loads:
            raise RuntimeError(f"{name}: lost the exact {origin} to {destination} alias edge")


def check_scalar_global_emission(args, ir, name):
    if name not in NUMERIC_ENTRY_SAVED_GLOBALS and name not in scalar_global_sources() and name not in constant_global_sources():
        return
    expected = scalar_global_graph(ir.read_text(), name)
    for mode in ("explicit", "deduced"):
        cpp = (args.work / f"{name}.{mode}.cpp").read_text()
        entry = re.search(r"\bmain\(\)\s*\{(.*?)^\}", cpp, re.M | re.S)
        if not entry:
            raise RuntimeError(f"{name}/{mode}: lost the scalar entry")
        observed = set(re.findall(r"ctnative::global_(?:number|boolean|string)\((\w+)\)", entry[1]))
        methods_by_value = dict(re.findall(
            r"(\w+)\s*=\s*ctnative::method_get<&[^>\n]+::m_(\w+)>\(", entry[1]))
        values, actual, calls, binaries = {}, [], 0, 0
        for line in entry[1].splitlines():
            assignment = re.search(r"\b(\w+)\s*=\s*(.*);$", line)
            result, expression = assignment.groups() if assignment else (None, "")
            if call := re.search(r"ctnative::invoke_callable\((\w+)([^;\n]*)\);", line):
                arguments = [arg.strip() for arg in call[2].split(",")[1:]]
                actual.append(("call", methods_by_value.get(call[1]),
                               tuple(values.get(arg, "unknown") for arg in arguments)))
                if result:
                    values[result] = "call:" + str(calls)
                calls += 1
            elif not result:
                continue
            elif result.startswith("g_"):
                if result != "g_host":
                    actual.append(("store", result[2:], values.get(expression, "unknown")))
            elif expression.startswith("g_"):
                values[result] = "global:" + expression[2:]
                if expression != "g_host" and result not in observed:
                    actual.append(("load", expression[2:]))
            elif match := re.fullmatch(r"ctnative::(?:to_number|to_nullable|to_nullable_string|string_text|scalar_truthy)\((\w+)\)", expression):
                values[result] = values.get(match[1], "unknown")
            elif match := re.fullmatch(r'(?:ctnative::js_string|std::string)\((R"\(.*\)"|"(?:[^"\\]|\\.)*")(?:, (\d+))?\)', expression):
                literal = scalar_string_literal(match[1])
                if match[2] and len(literal.encode("utf-8", "surrogatepass")) != int(match[2]):
                    raise RuntimeError(f"{name}/{mode}: emitted String lost its exact byte length")
                values[result] = ("string", literal)
            elif expression in {"true", "false"}:
                values[result] = ("boolean", expression)
            elif re.fullmatch(r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?", expression):
                values[result] = float(expression)
            elif match := re.fullmatch(r"(\w+) ([+*/-]) (\w+)", expression):
                operation = {"+": "add", "-": "sub", "*": "mul", "/": "div"}[match[2]]
                actual.append(("binary", operation, values.get(match[1], "unknown"),
                               values.get(match[3], "unknown")))
                values[result] = "binary:" + str(binaries)
                binaries += 1
            elif expression in values:
                values[result] = values[expression]
        if actual != expected:
            raise RuntimeError(f"{name}/{mode}: emitted scalar dataflow changed\n{expected}\n{actual}")
        case = constant_global_cases().get(name)
        if case:
            requested = {**case["saved"], "trace": case["expected_trace"]}
            for binding, value in requested.items():
                tag = ("string" if isinstance(value, StringValue) or value == "owned scalar"
                       else "boolean" if isinstance(value, bool) else "number")
                carrier = "nullable_string" if tag == "string" else "nullable_scalar"
                if not re.search(rf"\bctnative::{carrier}\s+g_{binding}\s*;", cpp):
                    raise RuntimeError(f"{name}/{mode}: {binding} lost its independently typed owning storage")
                loaded = re.findall(rf"\b(\w+)\s*=\s*g_{binding};", entry[1])
                checked = [temporary for temporary in loaded if re.search(
                    rf"ctnative::global_{tag}\({temporary}\)", entry[1])]
                if len(checked) != 1:
                    raise RuntimeError(f"{name}/{mode}: {binding} lost its exact {tag} observation check")


def check_scalar_global_carriers(args, positives, node, reference):
    cases = scalar_global_cases()
    for name in sorted(SCALAR_GLOBAL_CARRIERS):
        case = cases[name]
        source, value = case["source"], case["expected_trace"]
        js, ir, count = boundary.prepare(args, name, source)
        check_leaf_absence_census(args, ir, name)
        if count != 5 or (host.run([node, "-e", boundary.NODE, str(js)]).stdout != f"trace={value}\n"
                or host.run([str(reference), str(js)]).stdout != numeric_reference_output(name, value)):
            raise RuntimeError(f"{name}: saved-global source census or observation changed")
        repaired_source = source.replace(case["removed_text"], case["replacement_text"])
        if source.count(case["removed_text"]) != 1 or repaired_source != positives[case["repair"]][0]:
            raise RuntimeError(f"{name}: scalar repair no longer restores its exact source")
        _, repaired, repaired_count = boundary.prepare(args, name + "-restored", repaired_source)
        if repaired_count != count or len(source_calls(repaired.read_text())) != case["prepared_calls"]:
            raise RuntimeError(f"{name}: scalar repair lost an evaluated method call")
        config = contract(args, ir, name)
        repaired_config = contract(args, repaired, name + "-restored")
        for mode, options in (("default", ""), ("disabled", "optimize=false")):
            def reject(input_ir, label, current_config):
                failed = owned.lower(args, input_ir, label, current_config, options=options, cleanup=False)
                text = methods.census(failed, count, label, admitted=0)
                reason = "standard Map identity is unproved with other host/global value reads"
                if "ctnative.host_owner_proved = true" not in text or reason not in text:
                    raise RuntimeError(f"{label}: lost independent scalar ownership/carrier boundary")
                check_scalar_global_preparation(text, input_ir.read_text(), name)
                return failed

            label = name + "-" + mode
            reject(ir, label, config)
            output = owned.lower(args, repaired, label + "-restored", repaired_config, options=options)
            checked = methods.census(output, count, label + "-restored", admitted=count)
            if "ctnative.host_owner_proved = true" not in checked:
                raise RuntimeError(f"{label}: scalar repair lost complete ownership")
            for payload in ("bool", "string", "nullable_string"):
                forged_name = label + "-forged-" + payload
                forged = args.work / f"{forged_name}.mlir"
                forged.write_text(forge_leaf_evidence(ir.read_text(), payload))
                stale = methods.refused(args, forged, forged_name + "-stale", config,
                    options=options, reason="fingerprint mismatch", admitted=0)
                check_call_preservation(forged.read_text(), stale.read_text(), forged_name + "-stale")
                check_scalar_global_preparation(stale.read_text(), forged.read_text(), name)
                fresh = contract(args, forged, forged_name)
                failed = reject(forged, forged_name, fresh)
                rerun = methods.refused(args, failed, forged_name + "-rerun", fresh,
                    options=options, reason="fingerprint mismatch", admitted=0)
                check_call_preservation(failed.read_text(), rerun.read_text(), forged_name + "-rerun")
                check_scalar_global_preparation(rerun.read_text(), failed.read_text(), name)



CONSTANT_GLOBAL_NODE = r"""const fs = require('node:fs');
const vm = require('node:vm');
const context = vm.createContext({});
vm.runInContext(fs.readFileSync(process.argv[1], 'utf8'), context);
for (const name of JSON.parse(process.argv[2])) {
    const value = vm.runInContext(name, context);
    let text;
    if (typeof value === 'number') {
        text = Number.isNaN(value) ? 'nan' : Object.is(value, -0) ? '-0' :
            value === -Infinity ? '-inf' : String(value);
    } else if (typeof value === 'string') {
        text = '"' + encodeURIComponent(value).replace(/[!'()*]/g,
            c => '%' + c.charCodeAt(0).toString(16).toUpperCase()) + '"';
    } else if (typeof value === 'boolean' || value === undefined || value === null) {
        text = String(value);
    } else {
        throw new Error(name + ': unexpected scalar tag');
    }
    process.stdout.write(name + '=' + text + '\n');
}
"""


def check_constant_global_observations(args, node, reference):
    for name, row in constant_global_cases().items():
        js = args.work / f"{name}-observed.js"
        js.write_text(row["source"])
        names = json.dumps(sorted(["trace", *row["saved"]]))
        expected = scalar_global_output(name, row["expected_trace"])
        node_output = host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), names]).stdout
        result = host.run([str(reference), str(js)])
        if node_output != expected or normalized_scalar_output(result.stdout) != expected:
            raise RuntimeError(f"{name}: exact typed scalar observations changed\n{expected}\n"
                               f"{node_output}\n{result.stdout}")
        counts = [0, 0, 0, 0, 0]
        for value in [row["expected_trace"], *row["saved"].values()]:
            index = 1 if isinstance(value, bool) else 2 if isinstance(value, StringValue) or value == "owned scalar" else (
                4 if value == "undefined" else 0)
            counts[index] += 1
        types = re.search(r"\((\d+) number, (\d+) boolean, (\d+) string, (\d+) null, (\d+) undefined\)",
                          result.stderr)
        if not types or list(map(int, types.groups())) != counts:
            raise RuntimeError(f"{name}: reference lost its independently observed scalar tags")
    # A trace-only observer would miss substitutions in the historical copy.
    # Every mutation must instead change the complete value-and-tag observation.
    for name, old, replacement in (
        ("constant_exact_historical", "const copy = fixed;", "const copy = 0;"),
        ("constant_feeds_trace", "const copy = fixed;", "const copy = first;"),
        ("constant_negative_zero", "0 / (0 - 1)", "0"),
        ("constant_nan", "0 / 0", "0"),
        ("constant_nan", "0 / 0", "void 0"),
        ("constant_nan", "0 / 0", "'NaN'"),
        ("constant_boolean", "const fixed = false;", "const fixed = 0;"),
        ("constant_boolean", "const copy = fixed;", "const copy = true;"),
        ("constant_boolean_true", "const copy = fixed;", "const copy = 1;"),
        ("constant_boolean_alias_chain", "const copy = offset;", "const copy = enabled;"),
        ("constant_boolean_trace_false", "var trace = copy;", "var trace = 0;"),
        ("constant_boolean_trace_true", "var trace = copy;", "var trace = 1;"),
        ("constant_boolean_saved_result", "const copy = first;", "const copy = fixed;"),
        ("constant_boolean_branch_lifetime", "const copy_flag = fixed_flag;", "const copy_flag = enabled;"),
        ("constant_string", "'owned scalar'", "7"),
        ("constant_string_empty", "const copy = fixed;", "const copy = void 0;"),
        ("constant_string_empty", "const copy = fixed;", "const copy = null;"),
        ("constant_string_bytes", "const copy = fixed;", "const copy = 'tail';"),
        ("constant_string_long", "const copy = fixed;", "const copy = '';"),
        ("constant_string_alias_chain", "const copy = offset;", "const copy = false;"),
        ("constant_undefined", "void 0", "0 / 0"),
        ("constant_alias_chain", "const copy = offset;", "const copy = first;"),
        ("constant_alias_arithmetic", "const copy = offset;", "const copy = fixed;"),
        ("constant_branch_lifetime", "const copy = offset;", "const copy = first;"),
    ):
        row = constant_global_cases()[name]
        assert row["source"].count(old) == 1, name
        js = args.work / f"{name}-constant-blind.js"
        js.write_text(row["source"].replace(old, replacement))
        names = json.dumps(sorted(["trace", *row["saved"]]))
        if host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), names]).stdout == scalar_global_output(
                name, row["expected_trace"]):
            raise RuntimeError(f"{name}: typed observation cannot distinguish {replacement}")


def check_boolean_observation_mutations(args, compilers, nm):
    # Mutate only an emitted store after all source proofs and successful native
    # executions. A missing store or a numerically equal value of another tag
    # must fail the final observation instead of printing a plausible Boolean.
    check_scalar_observation_mutations(args, compilers, nm, (
        ("constant_boolean", "copy", "ctnative::nullable_scalar(0.0)"),
        ("constant_boolean_true", "copy", "ctnative::nullable_scalar(1.0)"),
        ("constant_boolean", "copy", "ctnative::nullable_scalar::null()"),
        ("constant_boolean", "copy", None),
        ("constant_boolean_true", "copy", None),
        ("constant_boolean", "first", "ctnative::nullable_scalar(true)"),
        ("constant_boolean", "first", None),
    ))


def check_string_observation_mutations(args, compilers, nm):
    check_scalar_observation_mutations(args, compilers, nm, (
        ("constant_string", "copy", "ctnative::to_nullable_string(ctnative::nullable_scalar::null())"),
        ("constant_string", "copy", "ctnative::nullable_string{}"),
        ("constant_string", "copy", None),
        ("constant_string_empty", "copy", None),
        ("constant_string_trace_empty", "trace", None),
    ))


def check_scalar_observation_mutations(args, compilers, nm, mutations):
    for name, binding, replacement in mutations:
        kind = "missing" if replacement is None else (
            "null" if "::null" in replacement else "wrong-tag")
        row = constant_global_cases()[name]
        output = scalar_global_output(name, row["expected_trace"])
        preceding = output.split(binding + "=", 1)[0]
        for mode in ("explicit", "deduced"):
            original = (args.work / f"{name}.{mode}.cpp").read_text()
            pattern = rf"(?m)^(\s*)g_{binding} = ([^;\n]+);$"
            matches = list(re.finditer(pattern, original))
            if len(matches) != 1:
                raise RuntimeError(f"{name}/{mode}: lost unique {binding} store mutation")
            match = matches[0]
            assignment = (f"{match[1]}(void){match[2]};" if replacement is None else
                          f"{match[1]}g_{binding} = {replacement};\n{match[1]}(void){match[2]};")
            changed = original[:match.start()] + assignment + original[match.end():]
            changed, count = re.subn(r"(\bmain\(\)\s*\{)",
                r"\1\n    std::set_terminate([] { std::fflush(stdout); std::_Exit(211); });", changed)
            if count != 1:
                raise RuntimeError(f"{name}/{mode}: lost exact observation termination witness")
            source = args.work / f"{name}.{mode}.{binding}-{kind}.cpp"
            source.write_text("#include <cstdio>\n#include <cstdlib>\n#include <exception>\n" + changed)
            binary = source.with_suffix(".mutated").resolve()
            host.run([compilers[1], *owned.FLAGS, str(source), "-o", str(binary)])
            if owned.VM.search(host.run([nm, "-C", str(binary)]).stdout):
                raise RuntimeError(f"{name}/{mode}: observation control linked a VM symbol")
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            if result.returncode != 211 or result.stdout != preceding or result.stderr:
                raise RuntimeError(f"{name}/{mode}: {binding} {kind} bypassed exact observation tag check "
                                   f"(exit {result.returncode})\n{result.stdout}{result.stderr}")


def check_constant_global_refusals(args, positives, node, reference):
    cases = constant_global_cases()
    for name in sorted(CONSTANT_GLOBAL_UNOWNED | CONSTANT_GLOBAL_CARRIERS):
        row = cases[name]
        source = row["source"]
        repair = row.get("candidate")
        if repair in constant_global_sources():
            old, replacement = row["removed_text"], row["replacement_text"]
        else:
            # Literal substitution alone does not repair an unsupported global.
            # Restore both declarations to the exact independently gated source.
            old = source.rsplit("\n", 2)[-2]
            replacement = "const fixed = 7; const copy = fixed;"
            repair = "scalar_constant_only"
        if source.count(old) != 1 or source.replace(old, replacement) != positives[repair][0]:
            raise RuntimeError(f"{name}: lost its exact independently gated repair")
        _, ir, count = boundary.prepare(args, name, source)
        check_leaf_absence_census(args, ir, name)
        _, restored, repair_count = boundary.prepare(args, repair + "-restored-for-" + name,
                                                      positives[repair][0])
        if count != 5 or repair_count != count or len(source_calls(restored.read_text())) != row["prepared_calls"]:
            raise RuntimeError(f"{name}: constant-global repair changed the five-function/eight-call source")
        config = contract(args, ir, name)
        restored_config = contract(args, restored, repair + "-restored-for-" + name)
        for mode, options in (("default", ""), ("disabled", "optimize=false")):
            def reject(input_ir, label, current_config):
                failed = owned.lower(args, input_ir, label, current_config, options=options, cleanup=False)
                text = methods.census(failed, count, label, admitted=0)
                owner = name not in CONSTANT_GLOBAL_UNOWNED
                if f"ctnative.host_owner_proved = {str(owner).lower()}" not in text:
                    raise RuntimeError(f"{label}: constant-global refusal changed its independent owner boundary")
                if owner:
                    attribute = "ctnative.not_native"
                    reason = {
                        "constant_undefined_candidate":
                            "store to global `fixed` may be null or undefined; "
                            "native global observations require a definite Number, Boolean or String",
                    }.get(name, "standard Map identity is unproved with other host/global value reads")
                else:
                    attribute = "ctnative.host_owner_reason"
                    reason = {
                        "constant_read_before_write": "global read lacks definite source initialization",
                        "constant_boolean_read_before_write": "global read lacks definite source initialization",
                        "constant_string_read_before_write": "global read lacks definite source initialization",
                        "constant_dynamic_global": "unproved host binding `globalThis`",
                        "constant_boolean_dynamic_global": "unproved host binding `globalThis`",
                        "constant_future_method_write": "property call lacks a current source getter proof",
                        "constant_boolean_future_method_write": "property call lacks a current source getter proof",
                        "constant_string_future_method_write": "property call lacks a current source getter proof",
                        "constant_boolean_optional":
                            "owned global method table requires unconditional straight-line operations",
                        "constant_boolean_mixed":
                            "owned global method table requires unconditional straight-line operations",
                        "constant_string_optional":
                            "owned global method table requires unconditional straight-line operations",
                        "constant_string_mixed":
                            "owned global method table requires unconditional straight-line operations",
                    }[name]
                if f'{attribute} = "{reason}"' not in text:
                    raise RuntimeError(f"{label}: lost its independent ownership/Map identity/global carrier boundary")
                check_scalar_global_preparation(text, input_ir.read_text(), name)
                if not owner:
                    check_call_preservation(input_ir.read_text(), text, label)
                return failed

            label = name + "-" + mode
            reject(ir, label, config)
            output = owned.lower(args, restored, label + "-restored", restored_config, options=options)
            if "ctnative.host_owner_proved = true" not in methods.census(output, count, label, admitted=count):
                raise RuntimeError(f"{label}: exact constant-global repair lost ownership")
            forged = args.work / f"{label}-forged.mlir"
            forged.write_text(forge_leaf_evidence(ir.read_text(), "string"))
            stale = methods.refused(args, forged, label + "-stale", config,
                options=options, reason="fingerprint mismatch", admitted=0)
            check_call_preservation(forged.read_text(), stale.read_text(), label + "-stale")
            check_scalar_global_preparation(stale.read_text(), forged.read_text(), name)
            fresh = contract(args, forged, label + "-forged")
            failed = reject(forged, label + "-fresh", fresh)
            rerun = methods.refused(args, failed, label + "-rerun", fresh,
                options=options, reason="fingerprint mismatch", admitted=0)
            check_call_preservation(failed.read_text(), rerun.read_text(), label + "-rerun")
            check_scalar_global_preparation(rerun.read_text(), failed.read_text(), name)

def check_numeric_entry_observations(args, node, reference):
    cases = {**numeric_entry_cases(), **scalar_global_cases(), **constant_global_cases()}
    for name in NUMERIC_ENTRY_LIFETIMES:
        source = cases[name]["source"]
        observed, value = numeric_entry_observer_source(source, name)
        js = args.work / f"{name}-numeric-future.js"
        js.write_text(observed)
        expected = f"trace={value}\n"
        if (host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected
                or host.run([str(reference), str(js)]).stdout != scalar_global_output(name, value)):
            raise RuntimeError(f"{name}: saved numeric field or future branch mismatch")
        mutations = [("return saved.value;", "return 1;"),
                     ("value: value", "value: 1"),
                     ("state.clear();", "state.has(key);")]
        if name in {"local_numeric_branch_lifetime", "scalar_saved_branch_lifetime",
                    "scalar_alias_branch_lifetime", "constant_branch_lifetime", "constant_boolean_branch_lifetime",
                    "constant_string_branch_lifetime"}:
            mutations.append(("state.delete(key);", "state.has(key);"))
        for index, (old, replacement) in enumerate(mutations):
            if old not in source:
                raise RuntimeError(f"{name}: lost numeric mutation control {old}")
            blind, _ = numeric_entry_observer_source(source.replace(old, replacement), name)
            js = args.work / f"{name}-numeric-blind-{index}.js"
            js.write_text(blind)
            if host.run([node, "-e", boundary.NODE, str(js)]).stdout == expected:
                raise RuntimeError(f"{name}: numeric observer cannot distinguish {replacement}")
    for name, old, replacement in (
        ("local_numeric_sub", "host.slot.set('x') - host.slot.set('y')",
         "host.slot.set('y') - host.slot.set('x')"),
        ("local_numeric_saved_snapshot", "first * 10 + second", "host.slot.size() * 10 + second"),
        ("scalar_alias", "const alias = first;", "const alias = second;"),
        ("scalar_single_write_repair", "var trace = first;", "var trace = host.slot.size();"),
        ("scalar_alias_chain", "const saved = first;", "const saved = second;"),
        ("scalar_alias_arithmetic", "const alias = total;", "const alias = first;"),
        ("scalar_alias_branch_lifetime", "const left = first;", "const left = third;"),
    ):
        source, value = cases[name]["source"], cases[name]["expected_trace"]
        if name == "local_numeric_sub":
            # Reverse evaluation while keeping each original result in its
            # operand position: swapping only key spellings would be invisible.
            replacement = "(host.slot.set('y'), host.slot.set('x')) - 1"
        js = args.work / f"{name}-numeric-blind.js"
        js.write_text(source.replace(old, replacement))
        if host.run([node, "-e", boundary.NODE, str(js)]).stdout == f"trace={value}\n":
            raise RuntimeError(f"{name}: numeric observation cannot distinguish reordered or reread results")


def check_leaf_absence_observations(args, node, reference):
    cases = leaf_absence_cases()
    for name in LEAF_ABSENCE_LIFETIMES:
        source = cases[name]["source"]
        observed, value = leaf_absence_observer_source(source, name)
        js = args.work / f"{name}-absence-future.js"
        js.write_text(observed)
        expected = f"trace={value}\n"
        if (host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected
                or host.run([str(reference), str(js)]).stdout != expected):
            raise RuntimeError(f"{name}: saved absence or future branch observation mismatch")
        mutations = [("state.delete(key);", "state.has(key);"),
                     ("value: 1", "value: 2")]
        if name == "local_absence_saved_undefined":
            mutations += [("return saved ===", "return state.get(key) ==="),
                          ("state.set(key, item); return saved", "state.has(key); return saved")]
        else:
            mutations += [("if (flag) { state.delete(key); }", "if (flag) { state.has(key); }")]
        for index, (old, replacement) in enumerate(mutations):
            if old not in source:
                raise RuntimeError(f"{name}: lost independent absence mutation {old}")
            blind, _ = leaf_absence_observer_source(source.replace(old, replacement), name)
            js = args.work / f"{name}-absence-blind-{index}.js"
            js.write_text(blind)
            if host.run([node, "-e", boundary.NODE, str(js)]).stdout == expected:
                raise RuntimeError(f"{name}: absence observer cannot distinguish {replacement}")


def check_leaf_clear_observations(args, node, reference):
    cases = leaf_clear_cases()
    for name in LEAF_CLEAR_LIFETIMES:
        source = cases[name]["source"]
        observed, value = leaf_clear_observer_source(source, name)
        js = args.work / f"{name}-clear-future.js"
        js.write_text(observed)
        expected = f"trace={value}\n"
        if (host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected
                or host.run([str(reference), str(js)]).stdout != expected):
            raise RuntimeError(f"{name}: saved value or future clear branch observation mismatch")
        mutations = [("state.clear();", "state.has(key);"), ("value: 1", "value: 2")]
        if name == "local_clear_saved_field":
            mutations.append(("return saved.value;", "return state.get(key).value;"))
        elif name == "local_clear_saved_undefined":
            mutations += [("return saved ===", "return state.get(key) ==="),
                          ("state.set(key, item); return saved", "state.has(key); return saved")]
        else:
            mutations.append(("if (flag) { state.clear(); }", "if (flag) { state.has(key); }"))
        for index, (old, replacement) in enumerate(mutations):
            if old not in source:
                raise RuntimeError(f"{name}: lost independent clear mutation {old}")
            blind, _ = leaf_clear_observer_source(source.replace(old, replacement), name)
            js = args.work / f"{name}-clear-blind-{index}.js"
            js.write_text(blind)
            throws = replacement == "return state.get(key).value;"
            if host.run([node, "-e", boundary.NODE, str(js)], success=not throws).stdout == expected:
                raise RuntimeError(f"{name}: clear observer cannot distinguish {replacement}")


def check_nullable_host_result_refusals(args, positives, node, reference, *, names=None):
    controls = {name: (*row[:4], "nullable_host_result_both" if name.endswith("deleted")
                      else "nullable_host_result", 16 if name.endswith(("deleted", "aliasing")) else 15)
                for name, row in nullable_host_result_refusals().items()}
    controls.update(nullable_nested_result_refusals())
    controls = {name: row for name, row in controls.items() if name not in PRIMITIVE_ABSENCE_CARRIERS}
    if names is not None:
        controls = {name: row for name, row in controls.items() if name in names}
    for name, (source, value, old, replacement, repaired_name, expected_calls) in controls.items():
        js, rejected, count = boundary.prepare(args, name, source)
        if count != 6 or len(source_calls(rejected.read_text())) != expected_calls:
            raise RuntimeError(f"{name}: changed the host-result source census")
        expected = f"trace={value}\n"
        reference_expected = expected + ("unknownResult=null\n" if name.endswith("unknown") else "")
        if (host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected
                or host.run([str(reference), str(js)]).stdout != reference_expected):
            raise RuntimeError(f"{name}: Node/interpreter host-result observation mismatch")
        restored_source = source.removeprefix("var unknownResult = null;\n").replace(old, replacement)
        restored_value = positives[repaired_name][2]
        if source.count(old) != 1 or restored_source != positives[repaired_name][0]:
            raise RuntimeError(f"{name}: repair no longer restores the independently gated source")
        restored_js, restored_ir, restored_count = boundary.prepare(args, name + "-restored", restored_source)
        if (restored_count != 6 or value == restored_value
                or host.run([node, "-e", boundary.NODE, str(restored_js)]).stdout
                != f"trace={restored_value}\n"
                or host.run([str(reference), str(restored_js)]).stdout != f"trace={restored_value}\n"):
            raise RuntimeError(f"{name}: missing the discriminating repaired observation")
        fresh = contract(args, rejected, name)
        restored_config = contract(args, restored_ir, name + "-restored")
        for mode, options in (("default", ""), ("disabled", "optimize=false")):
            mode_name = name + "-" + mode

            def reject_live(input_ir, label, current_config):
                if name != "nullable_nested_sibling":
                    failed = methods.refused(args, input_ir, label, current_config,
                                             options=options, admitted=0)
                    check_call_preservation(input_ir.read_text(), failed.read_text(), label)
                    return failed
                # The historical leaf-writing sibling now has a complete
                # owner. Its Object/String Map still has no native carrier.
                failed = owned.lower(args, input_ir, label, current_config, options=options, cleanup=False)
                text = methods.census(failed, 6, label, admitted=0)
                if ("ctnative.host_owner_proved = true" not in text
                        or "!ctnative.map<!ctnative.opt<!ctnative.str<utf8>>, !ctnative.boxed>" not in text
                        or re.search(r"\bemitc\.func @main\(", text)):
                    raise RuntimeError(f"{label}: lost the complete owner or Object/String carrier refusal")
                check_prepared_result_calls(text, input_ir.read_text(), name)
                return failed

            failed = reject_live(rejected, mode_name, fresh)
            repaired = owned.lower(args, restored_ir, mode_name + "-restored", restored_config,
                                   options=options)
            repaired_text = methods.census(repaired, 6, mode_name + "-restored", admitted=6)
            if "ctnative.host_owner_proved = true" not in repaired_text:
                raise RuntimeError(f"{name}: repaired nullable host result lost ownership")
            for payload in ("bool", "string", "nullable_string"):
                forged_name = mode_name + "-forged-" + payload
                forged = args.work / f"{forged_name}.mlir"
                forged.write_text(forge_map_presence(rejected.read_text(), payload))
                stale = methods.refused(args, forged, forged_name + "-stale", fresh,
                    options=options, reason="fingerprint mismatch", admitted=0)
                check_call_preservation(forged.read_text(), stale.read_text(), forged_name + "-stale")
                forged_config = contract(args, forged, forged_name)
                failed = reject_live(forged, forged_name, forged_config)
                if "fingerprint mismatch" in failed.read_text():
                    raise RuntimeError(f"{forged_name}: skipped independent host payload reanalysis")
                rerun = methods.refused(args, failed, forged_name + "-rerun", forged_config,
                                        options=options, admitted=0)
                check_call_preservation(failed.read_text(), rerun.read_text(), forged_name + "-rerun")


def check_shortcircuit_nullable_refusal(args, node, reference):
    # Host truthiness now proves this historical result is String/Null. Its
    # earlier &&/|| temporary still needs an unsupported optional Bool/String
    # carrier, independently of the complete owner and published result proof.
    name = "shortcircuit_nullable"
    source, value, old, replacement, restored_value = shortcircuit_refusals()[name]
    js, rejected, count = boundary.prepare(args, name, source)
    if count != 6 or len(source_calls(rejected.read_text())) != 17:
        raise RuntimeError(f"{name}: changed the original seventeen-call source census")
    if (host.run([node, "-e", boundary.NODE, str(js)]).stdout != f"trace={value}\n"
            or host.run([str(reference), str(js)]).stdout != f"trace={value}\n"):
        raise RuntimeError(f"{name}: Node/interpreter original observation mismatch")
    restored_source = source.replace(old, replacement)
    if source.count(old) != 1 or restored_source != shortcircuit_sources()["shortcircuit_same_tag"][0]:
        raise RuntimeError(f"{name}: repair no longer restores the independently gated source")
    restored_js, restored_ir, restored_count = boundary.prepare(args, name + "-restored", restored_source)
    if (restored_count != 6 or value == restored_value
            or host.run([node, "-e", boundary.NODE, str(restored_js)]).stdout
            != f"trace={restored_value}\n"
            or host.run([str(reference), str(restored_js)]).stdout != f"trace={restored_value}\n"):
        raise RuntimeError(f"{name}: lost the discriminating original/repaired observations")
    fresh = contract(args, rejected, name)
    restored_config = contract(args, restored_ir, name + "-restored")

    def check_prepared(output, original, label):
        text = methods.census(output, 6, label, admitted=0)
        diagnostic = ("a value of type !ctnative.opt<!ctnative.variant<!ctnative.bool, "
                      "!ctnative.str<utf8>>> from `scf.if`")
        if ("ctnative.host_owner_proved = true" not in text or diagnostic not in text
                or re.search(r"\bemitc\.func @main\(", text)):
            raise RuntimeError(f"{label}: missing complete owner or optional intermediate refusal")
        calls = re.findall(
            r"^\s*(%[-\w.$]+) = ctjs\.call_direct @([-\w.$]+)\(([^\n]+)\) "
            r"\{ctnative\.stored_call = 1 : i32\}", text, re.M)
        actuals = [arguments.split(", ") for _, _, arguments in calls]
        if (len(source_calls(text)) != len(source_calls(original))
                or [target for _, target, _ in calls] != ["fn$4", "fn$5", "fn$4", "fn$5", "fn$3"]
                or [len(arguments) for arguments in actuals] != [5, 5, 5, 5, 4]
                or actuals[1][-1] != calls[0][0] or actuals[3][-1] != calls[2][0]
                or f'ctjs.store_global "trace", {calls[-1][0]}' not in text):
            raise RuntimeError(f"{label}: carrier refusal changed prepared producer/consumer operands")
        return text

    for mode, options in (("default", ""), ("disabled", "optimize=false")):
        mode_name = name + "-" + mode
        output = owned.lower(args, rejected, mode_name, fresh, options=options, cleanup=False)
        check_prepared(output, rejected.read_text(), mode_name)
        restored = owned.lower(args, restored_ir, mode_name + "-restored", restored_config,
                               options=options)
        restored_text = methods.census(restored, 6, mode_name + "-restored", admitted=6)
        if "ctnative.host_owner_proved = true" not in restored_text:
            raise RuntimeError(f"{mode_name}: repaired temporary did not restore complete ownership")
        for payload in ("bool", "string", "nullable_string"):
            forged_name = mode_name + "-forged-" + payload
            forged = args.work / f"{forged_name}.mlir"
            forged.write_text(forge_map_presence(rejected.read_text(), payload))
            stale = methods.refused(args, forged, forged_name + "-stale", fresh,
                options=options, reason="fingerprint mismatch", admitted=0)
            check_call_preservation(forged.read_text(), stale.read_text(), forged_name + "-stale")
            forged_config = contract(args, forged, forged_name)
            checked = owned.lower(args, forged, forged_name, forged_config, options=options, cleanup=False)
            checked_text = check_prepared(checked, forged.read_text(), forged_name)
            rerun = methods.refused(args, checked, forged_name + "-rerun", forged_config,
                options=options, reason="fingerprint mismatch", admitted=0)
            check_call_preservation(checked_text, rerun.read_text(), forged_name + "-rerun")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--node")
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    node = boundary.node_executable(args)
    reference = boundary.reference_tool(args.opt)
    compilers = [next((shutil.which(c) for c in choices if shutil.which(c)), None)
                 for choices in (("g++-13", "g++"), ("clang++-18", "clang++"))]
    nm = shutil.which("nm") or shutil.which("llvm-nm")
    if not all(compilers) or not nm or not owned.VM.search(
            host.run([nm, "-C", str(reference)]).stdout):
        raise RuntimeError("need both host compilers and a working VM-symbol control")
    mutated = SOURCE.replace("return state.size;", "state.set('x', 1); return state.size;")
    growing = SOURCE.replace("return state.size;", "state.set(state.size, 1); return state.size;")
    positives = {
        "ordinary": (SOURCE, "host", 0),
        "already_resolved": (SOURCE, "host", 0),
        "legacy_store_marker": (SOURCE, "host", 0),
        "legacy_field_marker": (SOURCE, "host", 0),
        "repeated": (SOURCE + "\ntrace = host.slot.get();", "host", 0),
        "ordinary_window": (SOURCE.replace("host", "window"), "window", 0),
        "mutate_map": (mutated, "host", 1),
        "boolean_result": (SOURCE.replace("return state.size;",
            "state.set('x', 1); return state.has('x');"), "host", True),
        "growing": (growing, "host", 1),
        "growing_repeated": (growing + "\ntrace = host.slot.get();" * 2, "host", 3),
        "primitive_actions": (SOURCE.replace("return state.size;",
            "state.set('x', 1); state.has('x'); state.get('x'); "
            "state.delete('missing'); return state.size;"), "host", 1),
        "replace_delete": (SOURCE.replace("return state.size;",
            "state.set('x', 1); state.set('x', 2); state.delete('x'); return state.size;"), "host", 0),
        "fluent": (SOURCE.replace("return state.size;",
            "state.set('x', 1).set('y', 2); return state.size;"), "host", 2),
        "has_result": (SOURCE.replace("return state.size;",
            "state.set(false, 1); state.set(state.has(false), 2); return state.size;"), "host", 2),
        "delete_result": (SOURCE.replace("return state.size;",
            "state.set(false, 0); state.set(true, 1); state.set(state.delete(true), 2); "
            "return state.size;"), "host", 2),
        "seeded_local_key": (SOURCE.replace("return state.size;",
            "state.set(1, 2); state.set(1, 3); state.set(state.get(1), 4); "
            "state.delete(3); return state.size;"), "host", 1),
        "shared": (SHARED, "host", 1),
        "shared_growing": (SHARED.replace("state.set('x', 1)", "state.set(state.size, 1)"),
                           "host", 1),
        "shared_repeated": (SHARED.replace("state.set('x', 1)", "state.set(state.size, 1)")
                            + "\nhost.slot.set(); trace = host.slot.get();", "host", 2),
        "shared_early_read": (SHARED.replace("host.slot.set();", "host.slot.get(); host.slot.set();"),
                              "host", 1),
        "shared_three": (SHARED.replace("get() { return state.size; },",
                         "size() { return state.size; }, get() { return state.size; },")
                         + "\ntrace = host.slot.size();", "host", 1),
        **parameter_sources(),
        **result_sources(),
        **seeded_result_sources(),
        **key_fact_sources(),
        **joined_result_sources(),
        **size_result_sources(),
        **payload_result_sources(),
        **mixed_result_sources(),
        **saved_read_sources(),
        **saved_join_sources(),
        **guarded_saved_sources(),
        **shortcircuit_sources(),
        **nullable_result_sources(),
        **nullable_key_sources(),
        **nullable_payload_sources(),
        **nullable_host_result_sources(), **nullable_nested_result_sources(),
        **leaf_object_sources(), **leaf_absence_sources(), **primitive_absence_sources(),
        **leaf_clear_sources(),
        **{name: row for name, row in leaf_readback_sources().items()
           if name not in LEAF_READBACK_UNOWNED},
        **numeric_entry_sources(),
        **scalar_global_sources(),
        **constant_global_sources(),
        # Keep the original refusal source byte-for-byte. Its method-local
        # empty payload now has the same independently proved leaf owner.
        "object_payload": (refusal_sources()["object_payload"], "host", 1),
    }
    saved = {}
    check_leaf_object_observations(args, node, reference)
    check_leaf_readback_observations(args, node, reference)
    check_comparison_identity_observations(args, node, reference)
    check_leaf_absence_observations(args, node, reference)
    check_primitive_absence_observations(args, node, reference)
    check_leaf_clear_observations(args, node, reference)
    check_numeric_entry_observations(args, node, reference)
    overwrite_source, _, overwrite_value = positives["seeded_dynamic_overwrite"]
    blind = args.work / "seeded-dynamic-overwrite-blinded.js"
    blind.write_text(overwrite_source.replace("return state.get(1);", "return 1;"))
    if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == f"trace={overwrite_value}\n":
        raise RuntimeError("dynamic overwrite witness cannot distinguish retaining the old payload")
    for name in ("seeded_size_saved", "seeded_size_two_saved", "seeded_size_two_saved_empty"):
        saved_source, _, saved_value = positives[name]
        blind = args.work / f"{name}-snapshot-blinded.js"
        blind.write_text(saved_source.replace("state.delete(saved)", "state.delete(state.size)"))
        if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == f"trace={saved_value}\n":
            raise RuntimeError(f"{name}: saved size witness cannot distinguish a current-size read")
    for name in ("seeded_size_two_saved", "seeded_size_after_delete"):
        live_source, _, live_value = positives[name]
        blind = args.work / f"{name}-delete-blinded.js"
        blind.write_text(live_source.replace("state.delete(", "state.has("))
        if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == f"trace={live_value}\n":
            raise RuntimeError(f"{name}: size witness cannot distinguish a real deletion")
    for name, old, replacements in (
        ("result_seeded_false", "return state.get(false);", ("return undefined;",)),
        ("result_seeded_empty_string", "return state.get('');", ("return undefined;",)),
        ("result_seeded_false_overwrite", "return state.get(false);", ("return true;",)),
        ("result_seeded_string_saved", "return saved;",
         ("return state.get('seed');", "return 'changed';")),
        ("result_seeded_mixed_false_zero", "state.set(false, 1);", ("state.set(0, 1);",)),
        ("result_seeded_mixed_false", "return state.get(true);",
         ("return 0;", "return undefined;")),
        ("result_seeded_mixed_empty_key", "state.set(false, false);", ("state.set('', false);",)),
        ("result_seeded_mixed_string_saved", "return saved;",
         ("return state.get('seed');", "return false;")),
        ("saved_read_write", "state.set(false, saved);",
         ("state.has(false);", "state.set(false, true);")),
        ("saved_read_write", "state.delete(false);", ("state.has(false);",)),
        ("saved_read_write_false", "state.set('', saved);",
         ("state.has('');", "state.set('', state.get(false));")),
        ("saved_read_write_number", "state.set(false, saved);",
         ("state.has(false);", "state.set(false, state.get(1));")),
        ("saved_read_write_repeated", "state.set(false, saved);", ("state.has(false);",)),
        ("saved_read_write_string_saved", "state.set(false, saved);", ("state.has(false);",)),
        ("saved_read_write_string_saved", "return result;", ("return state.get(false);",)),
        ("saved_read_write_string_saved", "state.delete(false);", ("state.has(false);",)),
        ("saved_read_write_wrong_tag", "state.set(false, true); const result",
         ("state.set(false, saved); const result",)),
        ("saved_read_write_overwritten", "state.set(false, true); const result",
         ("const result",)),
        ("saved_join", "flag ? state.get('other') : state.get('')", ("state.get('')",)),
        ("saved_join_distinct", "flag ? state.get('other') : state.get('')",
         ("state.get('')", "state.get('other')")),
        ("saved_join_bool", "flag ? state.get('other') : state.get('')",
         ("state.get('')", "state.get('other')")),
        ("saved_join_bool", "state.set('temp', saved);",
         ("state.has('temp');", "state.set('temp', state.get(''));")),
        ("saved_join_number", "flag ? state.get(1) : state.get(0)",
         ("state.get(0)", "state.get(1)")),
        ("saved_join_number", "state.set(false, saved);",
         ("state.has(false);", "state.set(false, state.get(0));")),
        ("saved_join_string_saved", "state.set(false, saved);", ("state.has(false);",)),
        ("saved_join_string_saved", "return result;", ("return state.get(false);",)),
        ("saved_join_string_saved", "state.delete(false);", ("state.has(false);",)),
        ("saved_join_string_saved", "state.delete('');", ("state.has('');",)),
        ("guarded_saved_read", "if (flag) { state.delete('other'); }",
         ("state.has('other');",)),
        ("guarded_saved_read", "state.has('other') ? state.get('other') : state.get('')",
         ("state.get('')",)),
        ("guarded_saved_read", "state.delete(false);", ("state.has(false);",)),
        ("guarded_saved_bool", "if (flag) { state.delete('other'); }",
         ("state.has('other');",)),
        ("guarded_saved_bool", "state.has('other') ? state.get('other') : state.get('')",
         ("state.get('')",)),
        ("guarded_saved_bool", "state.set('temp', saved);",
         ("state.has('temp');", "state.set('temp', state.get(''));")),
        ("guarded_saved_number", "if (flag) { state.delete(1); }", ("state.has(1);",)),
        ("guarded_saved_number", "state.has(1) ? state.get(1) : state.get(0)",
         ("state.get(0)",)),
        ("guarded_saved_number", "state.set(false, saved);",
         ("state.has(false);", "state.set(false, state.get(0));")),
        ("guarded_saved_string_saved", "state.set(false, saved);", ("state.has(false);",)),
        ("guarded_saved_string_saved", "return result;", ("return state.get(false);",)),
        ("guarded_saved_string_saved", "state.delete(false);", ("state.has(false);",)),
        ("guarded_saved_string_saved", "state.delete('');", ("state.has('');",)),
        ("guarded_saved_string_saved", "state.set('other', false); state.delete('other');",
         ("state.set('other', false); state.has('other');",)),
        ("shortcircuit_same_tag", "if (flag) { state.delete('other'); }",
         ("state.has('other');",)),
        ("shortcircuit_same_tag", "state.delete(false);", ("state.has(false);",)),
        ("shortcircuit_distinct", "(state.has('other') && state.get('other')) || state.get('')",
         ("state.get('')", "'future'")),
        ("shortcircuit_distinct", "state.set(false, saved);", ("state.has(false);",)),
        ("shortcircuit_empty_string", "(state.has('other') && state.get('other')) || state.get('')",
         ("state.has('other') ? state.get('other') : state.get('')", "state.get('other')")),
        ("shortcircuit_bool", "(state.has('other') && state.get('other')) || state.get('')",
         ("state.get('')", "true")),
        ("shortcircuit_bool", "state.set('temp', saved);",
         ("state.has('temp');", "state.set('temp', state.get(''));")),
        ("shortcircuit_false", "(state.has('other') && state.get('other')) || state.get('')",
         ("state.has('other') ? state.get('other') : state.get('')",
          "state.has('other') && state.get('other')")),
        ("shortcircuit_number", "(state.has(1) && state.get(1)) || state.get(0)",
         ("state.get(0)", "3")),
        ("shortcircuit_number", "state.set(false, saved);",
         ("state.has(false);", "state.set(false, state.get(0));")),
        ("shortcircuit_zero", "(state.has(1) && state.get(1)) || state.get(0)",
         ("state.has(1) ? state.get(1) : state.get(0)", "state.get(1)")),
        ("shortcircuit_string_saved", "state.set(false, saved);", ("state.has(false);",)),
        ("shortcircuit_string_saved", "return result;", ("return state.get(false);",)),
        ("shortcircuit_string_saved", "state.delete(false);", ("state.has(false);",)),
        ("shortcircuit_string_saved", "state.delete('');", ("state.has('');",)),
        ("shortcircuit_string_saved", "state.set('other', false); state.delete('other');",
         ("state.set('other', false); state.has('other');",)),
        ("nullable_key_identity", "state.set(key, true);",
         ("state.set(key || 'missing', true);",)),
        ("nullable_key_identity", "host.slot.set(void 0);", ("host.slot.set(null);",)),
        ("nullable_key_identity", "host.slot.set('');", ("host.slot.set(null);",)),
        ("nullable_key_identity", "return result || null;", ("return result || (void 0);",)),
        ("nullable_key_second_use", "state.set(key, true);", ("state.has(key);",)),
        ("nullable_key_mixed", "state.delete(false);", ("state.has(false);",)),
        ("nullable_original_key", "return result || null;", ("return result;",)),
        ("nullable_second_key_use", "state.set(key, true);", ("state.has(key);",)),
        ("nullable_key_string_saved", "state.delete('seed');", ("state.has('seed');",)),
        ("nullable_payload_saved", "state.delete(key);", ("state.has(key);",)),
        ("nullable_payload_deleted", "state.delete(key);", ("state.has(key);",)),
        ("nullable_payload_mixed_readback", "state.delete('extra');", ("state.has('extra');",)),
        ("nullable_payload_mixed_identity", "state.delete('extra');", ("state.has('extra');",)),
        ("nullable_mixed_payload_readback", "state.delete(false);", ("state.has(false);",)),
        ("nullable_payload_mixed_saved", "state.delete(key);", ("state.has(key);",)),
        ("nullable_host_result", "return state.get(key);",
         ("return null;", "return void 0;", "return '';", "return true;")),
        ("nullable_host_result_identity", "host.slot.set(void 0);", ("host.slot.set(null);",)),
        ("nullable_host_result_identity", "host.slot.set('');", ("host.slot.set(null);",)),
        ("nullable_host_result_conditional", "if (key) { state.set(key, 'selected'); }",
         ("state.has(key);",)),
        ("nullable_host_result_saved", "state.delete('seed');", ("state.has('seed');",)),
        ("nullable_nested_result_same", "return state.get(key);",
         ("return null;", "return void 0;", "return '';", "return true;")),
        ("nullable_nested_result_identity", "host.slot.set(void 0);", ("host.slot.set(null);",)),
        ("nullable_nested_result_identity", "host.slot.set('');", ("host.slot.set(null);",)),
        ("nullable_nested_result_identity", "host.slot.set('later');", ("host.slot.set('future');",)),
        ("nullable_nested_result_saved", "state.delete(key);", ("state.has(key);",)),
    ):
        live_source, _, live_value = positives[name]
        if live_source.count(old) != 1:
            raise RuntimeError(f"{name}: lost the payload observation")
        for index, replacement in enumerate(replacements):
            blind = args.work / f"{name}-payload-blinded-{index}.js"
            blind.write_text(live_source.replace(old, replacement))
            if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == f"trace={live_value}\n":
                raise RuntimeError(f"{name}: payload witness cannot distinguish {replacement}")
    for name, (source, _, _) in {
        **nullable_result_sources(), **nullable_key_sources(), **nullable_payload_sources(),
        **nullable_host_result_sources(), **nullable_nested_result_sources(),
    }.items():
        identity = args.work / f"{name}-identity.js"
        identity.write_text(nullable_observer_source(source, name))
        observations = len(NULLABLE_OBSERVATIONS[name]) + len(NULLABLE_PAYLOAD_READBACKS.get(name, ()))
        expected = f"trace={(1 << observations) - 1}\n"
        if (host.run([node, "-e", boundary.NODE, str(identity)]).stdout != expected
                or host.run([str(reference), str(identity)]).stdout != expected):
            raise RuntimeError(f"{name}: Node/interpreter String/null/undefined identity mismatch")
        original_return = ("return result ? result : null;" if name == "nullable_ternary" else
                           "return result || (void 0);" if name == "nullable_undefined" else
                           "return result || (nullish ? null : (void 0));" if name == "nullable_threeway"
                           else "return result || null;")
        replacements = ("return result;", "return null;", "return undefined;")
        if name == "nullable_empty":
            replacements = ("return result;", "return undefined;")
        if name == "nullable_string_saved":
            replacements += ("return state.get(false) || null;",)
        if name == "nullable_key_string_saved":
            replacements += ("return state.get('seed') || null;",)
        if name in {"nullable_payload_saved", "nullable_payload_mixed_saved", "nullable_host_result_saved",
                    "nullable_nested_result_saved"}:
            replacements += ("return state.get('seed') || null;",)
        for index, replacement in enumerate(replacements):
            if source.count(original_return) != 1:
                raise RuntimeError(f"{name}: lost the nullable return observation")
            blind = args.work / f"{name}-identity-blinded-{index}.js"
            blind.write_text(nullable_observer_source(source.replace(original_return, replacement), name))
            if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == expected:
                raise RuntimeError(f"{name}: nullable identity cannot distinguish {replacement}")
    for name, old, replacements in (
        ("nullable_payload_identity", "state.set(key, key);",
         ("state.set(key, null);", "state.set(key, void 0);", "state.set(key, '');")),
        ("nullable_payload_saved", "return saved;",
         ("return state.get(key);", "return 'overwritten';")),
        ("nullable_payload_deleted", "state.delete(key);", ("state.has(key);",)),
        ("nullable_payload_mixed_readback", "state.set(key, key);",
         ("state.set(key, null);", "state.set(key, void 0);", "state.set(key, '');")),
        ("nullable_mixed_payload_readback", "state.set(key, key);",
         ("state.set(key, null);", "state.set(key, void 0);", "state.set(key, '');")),
        ("nullable_payload_mixed_identity", "state.set(key, key);",
         ("state.set(key, null);", "state.set(key, void 0);", "state.set(key, '');")),
        ("nullable_payload_mixed_saved", "return saved;",
         ("return state.get(key);", "return true;")),
        ("nullable_payload_mixed_saved", "const saved = state.get(key); state.set(key, true);",
         ("state.set(key, true); const saved = state.get(key);",)),
        ("nullable_host_result", "return state.get(key);",
         ("return null;", "return void 0;", "return '';", "return true;")),
        ("nullable_host_result_conditional", "if (key) { state.set(key, 'selected'); }",
         ("state.has(key);", "state.set(key, 'selected');")),
        ("nullable_host_result_saved", "return saved;", ("return state.get(key);", "return true;")),
        ("nullable_host_result_saved", "const saved = state.get(key); state.set(key, true);",
         ("state.set(key, true); const saved = state.get(key);",)),
        ("nullable_nested_result", "return state.get(key);",
         ("return null;", "return void 0;", "return '';", "return true;")),
        ("nullable_nested_result_saved", "return saved;", ("return state.get(key);", "return true;")),
        ("nullable_nested_result_saved", "const saved = state.get(key); state.set(key, true);",
         ("state.set(key, true); const saved = state.get(key);",)),
    ):
        source = positives[name][0]
        observations = len(NULLABLE_OBSERVATIONS[name]) + len(NULLABLE_PAYLOAD_READBACKS[name])
        expected = f"trace={(1 << observations) - 1}\n"
        if source.count(old) != 1:
            raise RuntimeError(f"{name}: lost the stored nullable payload observation")
        for index, replacement in enumerate(replacements):
            blind = args.work / f"{name}-stored-payload-blinded-{index}.js"
            blind.write_text(nullable_observer_source(source.replace(old, replacement), name))
            if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == expected:
                raise RuntimeError(f"{name}: stored payload cannot distinguish {replacement}")
    saved_source = positives["nullable_host_result_saved"][0]
    saved_source += "\nhost.slot.set(host.slot.get(false)); trace = host.slot.size('anchor');\n"
    deletion = args.work / "nullable-host-result-saved-deletion.js"
    deletion.write_text(saved_source)
    if (host.run([node, "-e", boundary.NODE, str(deletion)]).stdout != "trace=1\n"
            or host.run([str(reference), str(deletion)]).stdout != "trace=1\n"):
        raise RuntimeError("nullable host result: saved payload deletion observation changed")
    deletion.write_text(saved_source.replace("state.delete(key);", "state.has(key);"))
    if host.run([node, "-e", boundary.NODE, str(deletion)]).stdout != "trace=2\n":
        raise RuntimeError("nullable host result: saved payload lifetime cannot distinguish deletion")
    for name, (source, binding, value) in positives.items():
        js, ir, count = boundary.prepare(args, name, source)
        functions = (LEAF_OBJECT_FUNCTIONS[name] if name in LEAF_OBJECT_FUNCTIONS
                     else 5 if name in LEAF_READBACK_CALLS or name in leaf_absence_sources()
                     or name in leaf_clear_sources() or name in numeric_entry_sources() or name in scalar_global_sources()
                     or name in constant_global_sources()
                     else RESULT_SIGNATURES[name][2] if name in RESULT_SIGNATURES
                     else 6 if name == "shared_three" else 5 if name.startswith("shared") else 4)
        if count != functions:
            raise RuntimeError(f"{name}: lost the {functions}-function source chain")
        if name == "boolean_result" and (
                len(source_calls((args.work / f"{name}.raw.mlir").read_text())) != 5
                or len(source_calls(ir.read_text())) != 5):
            raise RuntimeError("boolean_result: changed the historical five-call source")
        if name == "saved_read_write" and len(source_calls(ir.read_text())) != 12:
            raise RuntimeError("saved_read_write: changed the exact 12-call boundary")
        if name == "saved_join" and len(source_calls(ir.read_text())) != 16:
            raise RuntimeError("saved_join: changed the exact 16-call boundary")
        if name in {"guarded_saved_read", "shortcircuit_same_tag", "nullable_normalized"} \
                and len(source_calls(ir.read_text())) != 18:
            raise RuntimeError(f"{name}: changed the exact 18-call boundary")
        if name == "nullable_homogeneous_key" and len(source_calls(ir.read_text())) != 11:
            raise RuntimeError("nullable_homogeneous_key: changed the exact 11-call control")
        if name in NULLABLE_KEY_CALLS and len(source_calls(ir.read_text())) != NULLABLE_KEY_CALLS[name]:
            raise RuntimeError(f"{name}: changed the exact {NULLABLE_KEY_CALLS[name]}-call boundary")
        if name in NULLABLE_PAYLOAD_CALLS \
                and len(source_calls(ir.read_text())) != NULLABLE_PAYLOAD_CALLS[name]:
            raise RuntimeError(f"{name}: changed the exact {NULLABLE_PAYLOAD_CALLS[name]}-call boundary")
        if name in NULLABLE_HOST_RESULT_CALLS \
                and len(source_calls(ir.read_text())) != NULLABLE_HOST_RESULT_CALLS[name]:
            raise RuntimeError(f"{name}: changed the exact {NULLABLE_HOST_RESULT_CALLS[name]}-call boundary")
        if name in NULLABLE_NESTED_RESULT_CALLS \
                and len(source_calls(ir.read_text())) != NULLABLE_NESTED_RESULT_CALLS[name]:
            raise RuntimeError(f"{name}: changed the exact {NULLABLE_NESTED_RESULT_CALLS[name]}-call boundary")
        if name in LEAF_OBJECT_CALLS and len(source_calls(ir.read_text())) != LEAF_OBJECT_CALLS[name]:
            raise RuntimeError(f"{name}: changed the exact {LEAF_OBJECT_CALLS[name]}-call leaf boundary")
        if name in LEAF_READBACK_CALLS and len(source_calls(ir.read_text())) != LEAF_READBACK_CALLS[name]:
            raise RuntimeError(f"{name}: changed the exact {LEAF_READBACK_CALLS[name]}-call readback boundary")
        check_leaf_absence_census(args, ir, name)
        if name in primitive_absence_sources() and (
                len(source_calls((args.work / f"{name}.raw.mlir").read_text())) != 10
                or len(source_calls(ir.read_text())) != 10):
            raise RuntimeError(f"{name}: changed the exact ten-call primitive absence source")
        if name == "already_resolved":
            ir = resolve_getter(args, ir)
        if name.startswith("legacy_"):
            ir = methods.legacy_marker(args, ir, name)
        expected = f"trace={str(value).lower() if isinstance(value, bool) else value}\n"
        node_command = [node, "-e", numeric_node_observer(value), str(js)]
        if name in constant_global_cases():
            names = json.dumps(sorted(["trace", *constant_global_cases()[name]["saved"]]))
            node_command = [node, "-e", CONSTANT_GLOBAL_NODE, str(js), names]
            expected = numeric_reference_output(name, value)
        reference_result = host.run([str(reference), str(js)])
        if (host.run(node_command).stdout != expected
                or normalized_scalar_output(reference_result.stdout)
                != numeric_reference_output(name, value)):
            raise RuntimeError(f"{name}: Node/interpreter source observation mismatch")
        if name == "boolean_result" and (
                "(0 number, 1 boolean, 0 string, 0 null, 0 undefined)" not in reference_result.stderr):
            raise RuntimeError("boolean_result: reference lost its independently observed Boolean tag")
        config = contract(args, ir, name, binding)
        original, manifest = ir.read_text(), config.read_text()
        output = owned.lower(args, ir, name, config)
        text = methods.census(output, functions, name, admitted=functions)
        if "ctnative.host_owner_proved = true" not in text:
            raise RuntimeError(f"{name}: lost live owning proof")
        if ir.read_text() != original or config.read_text() != manifest:
            raise RuntimeError(f"{name}: changed supplied source or manifest")
        if name == "boolean_result" or name in {**saved_read_sources(), **saved_join_sources(), **guarded_saved_sources(),
                    **shortcircuit_sources(), **nullable_result_sources(), **nullable_key_sources(),
                    **nullable_payload_sources(), **nullable_host_result_sources(), **nullable_nested_result_sources(),
                    **leaf_object_sources(), **leaf_readback_sources(), **leaf_absence_sources(),
                    **primitive_absence_sources(), **leaf_clear_sources(), **numeric_entry_sources(),
                    **scalar_global_sources(), **constant_global_sources()}:
            disabled = owned.lower(args, ir, name + "-disabled", config, options="optimize=false")
            if disabled.read_text() != output.read_text():
                raise RuntimeError(f"{name}: saved scalar proof depends on optimization policy")
        standalone(args, output, name, value, compilers, nm)
        check_scalar_global_emission(args, ir, name)
        saved[name] = ir, config, output

    check_primitive_absence_forgeries(args, saved, node, reference)
    check_leaf_object_forgeries(args, saved)
    check_leaf_object_forgeries(args, saved,
        ("local_field_get_guarded_checked", "historical_object_saved_identity", "local_field_readback_lifetime_checked"))
    check_leaf_object_forgeries(args, saved,
        ("local_field_direct", "local_field_saved_overwrite", "local_field_readback_lifetime"))
    check_leaf_object_forgeries(args, saved, LEAF_COMPARISON_REPAIRS)
    check_leaf_object_forgeries(args, saved,
        ("local_absence_delete_undefined", "local_absence_saved_undefined",
         "local_absence_distinct_branches_false"))
    check_leaf_object_forgeries(args, saved,
        ("local_absence_clear_saved_identity", "local_clear_saved_undefined",
         "local_clear_both_branches_false"))
    check_leaf_object_forgeries(args, saved,
        ("local_identity_repeated_keys", "local_numeric_nested_key", "local_numeric_branch_lifetime"))
    check_leaf_object_forgeries(args, saved,
        ("local_add_saved_results", "local_numeric_saved_snapshot", "scalar_result_key",
         "scalar_saved_branch_lifetime"))
    check_leaf_object_forgeries(args, saved,
        (*sorted(SCALAR_GLOBAL_INITIALIZED), "scalar_alias_chain", "scalar_alias_branch_lifetime"))
    check_leaf_object_forgeries(args, saved, ("scalar_constant_only", "constant_alias_arithmetic"))
    check_leaf_object_forgeries(args, saved,
        ("constant_boolean", "constant_boolean_true", "constant_boolean_alias_chain",
         "constant_boolean_saved_result", "constant_boolean_branch_lifetime"))
    check_boolean_observation_mutations(args, compilers, nm)
    check_leaf_object_forgeries(args, saved,
        ("constant_string", "constant_string_bytes", "constant_string_alias_chain",
         "constant_string_trace_empty", "constant_string_branch_lifetime"))
    check_string_observation_mutations(args, compilers, nm)

    # Valid-looking scalar markers cannot normalize real null keys or narrow
    # the second use of a nullable formal. Fresh proof must emit the same C++.
    for name in ("nullable_key_identity", "nullable_second_key_use",
                 "nullable_payload_readback", "nullable_payload_mixed", "nullable_payload_deleted",
                 "nullable_payload_mixed_readback", "nullable_mixed_payload_readback",
                 "nullable_payload_mixed_identity", "nullable_payload_mixed_saved",
                 *nullable_host_result_sources(), *nullable_nested_result_sources()):
        ir, config, output = saved[name]
        expected_cpp = comparable_provenance(
            host.run([args.translate, "--mlir-to-cpp", str(output)]).stdout, ir)
        for mode, options in (("default", ""), ("disabled", "optimize=false")):
            for payload in ("bool", "string", "nullable_string"):
                forged_name = name + "-" + mode + "-forged-" + payload
                forged = args.work / f"{forged_name}.mlir"
                forged.write_text(forge_map_presence(ir.read_text(), payload))
                failed = methods.refused(args, forged, forged_name + "-stale", config,
                    options=options, reason="fingerprint mismatch", admitted=0)
                check_call_preservation(forged.read_text(), failed.read_text(), forged_name + "-stale")
                fresh = contract(args, forged, forged_name)
                checked = owned.lower(args, forged, forged_name, fresh, options=options)
                text = methods.census(checked, 6, forged_name, admitted=6)
                if ("ctnative.host_owner_proved = true" not in text
                        or comparable_provenance(
                            host.run([args.translate, "--mlir-to-cpp", str(checked)]).stdout,
                            forged) != expected_cpp):
                    raise RuntimeError(f"{forged_name}: forged scalar tags changed nullable storage")

    ir, config, output = saved["ordinary"]
    boundary.native(args, ir, "no-manifest", 4)
    absent = owned.contract(args, ir, "no-intrinsic")
    methods.refused(args, ir, "no-intrinsic", absent, admitted=0)
    for budget in (0, 32):
        methods.refused(args, ir, f"budget-{budget}", config,
                        options=f"host-max-steps={budget}", reason="budget", admitted=0)
    disabled = owned.lower(args, ir, "disabled", config, options="optimize=false")
    if disabled.read_text() != output.read_text():
        raise RuntimeError("explicit host preparation depends on default optimization policy")
    changed = args.work / "changed.mlir"
    text, count = re.subn(r'#ctjs\.string<"size">', '#ctjs.string<"other">', ir.read_text())
    if count != 1:
        raise RuntimeError("stale fingerprint control lost its size read")
    changed.write_text(text)
    methods.refused(args, changed, "stale", config, reason="fingerprint mismatch", admitted=0)
    forged = args.work / "forged.mlir"
    forged.write_text(methods.forge_reports(changed.read_text()))
    boundary.native(args, forged, "forged-no-manifest", 4)
    methods.refused(args, forged, "forged-stale", config, reason="fingerprint mismatch", admitted=0)
    rerun = owned.lower(args, output, "admitted-rerun", config, cleanup=False)
    text = methods.census(rerun, 4, "admitted-rerun", admitted=4)
    if "ctnative.host_owner_proved = false" not in text or "fingerprint mismatch" not in text:
        raise RuntimeError("already-lowered output reused the original source proof")

    rollback = check_budgets(args, ir, config, "ordinary")
    _, repeated, count = boundary.prepare(args, "budget-repeated",
        SOURCE + "\ntrace = host.slot.get();" * 15)
    if count != 4:
        raise RuntimeError("repeated budget witness changed source function count")
    fresh = contract(args, repeated, "budget-repeated")
    rollback += check_budgets(args, repeated, fresh, "repeated")
    growing_ir, growing_config, _ = saved["growing"]
    rollback += check_budgets(args, growing_ir, growing_config, "growing")
    shared_ir, shared_config, _ = saved["shared_growing"]
    rollback += check_budgets(args, shared_ir, shared_config, "shared_growing", functions=5)
    parameter_ir, parameter_config, parameter_output = saved["shared_parameter"]
    rollback += check_budgets(args, parameter_ir, parameter_config, "shared_parameter", functions=5)
    result_ir, result_config, result_output = saved["parameter_call_result"]
    rollback += check_budgets(args, result_ir, result_config, "parameter_call_result", functions=5)
    for name in ("seeded_earlier_key", "seeded_other_delete", "seeded_dynamic_write",
                 "seeded_dynamic_formal", "seeded_dynamic_delete", "seeded_size_saved",
                 "seeded_size_two_entries", "seeded_size_two_saved_empty",
                 "result_seeded_bool", "result_seeded_string", "result_seeded_string_saved",
                 "result_seeded_mixed_contents", "result_seeded_join_reseed",
                 "result_seeded_bool_string_contents", "result_seeded_mixed_string_saved",
                 "saved_read_write", "saved_read_write_false", "saved_read_write_number",
                 "saved_read_write_string_saved", "saved_join", "saved_join_bool",
                 "saved_join_number", "saved_join_string_saved", "guarded_saved_read",
                 "guarded_saved_bool", "guarded_saved_number", "guarded_saved_string_saved",
                 "shortcircuit_same_tag", "shortcircuit_false", "shortcircuit_zero",
                 "shortcircuit_string_saved", "nullable_normalized", "nullable_threeway",
                 "nullable_string_saved", "nullable_key_homogeneous", "nullable_key_identity",
                 "nullable_original_key", "nullable_key_string_saved", "nullable_payload_readback",
                 "nullable_payload_mixed", "nullable_payload_saved",
                 "nullable_payload_mixed_readback", "nullable_mixed_payload_readback",
                 "nullable_payload_mixed_identity", "nullable_payload_mixed_saved",
                 "nullable_host_result", "nullable_host_result_conditional", "nullable_host_result_saved",
                 "nullable_nested_result", "nullable_nested_result_identity", "nullable_nested_result_saved"):
        key_ir, key_config, _ = saved[name]
        rollback += check_budgets(args, key_ir, key_config, name,
                                  functions=RESULT_SIGNATURES[name][2])
    seeded_ir, seeded_config, seeded_output = saved["result_seeded_map_get"]
    rollback += check_budgets(args, seeded_ir, seeded_config, "result_seeded_map_get", functions=5)

    for name, source in refusal_sources().items():
        if name == "object_payload":
            continue
        _, rejected, _ = boundary.prepare(args, name, source)
        fresh = contract(args, rejected, name)
        methods.refused(args, rejected, name, fresh)
    # Primitive ownership does not make a nullable result a definite observation.
    # The historical Boolean result is executed above with its actual Bool type.
    for name, body in {
        "nullable_result": "state.set('x', 1); return state.get('missing');",
    }.items():
        js, rejected, _ = boundary.prepare(args, name, SOURCE.replace("return state.size;", body))
        fresh = contract(args, rejected, name)
        result = owned.lower(args, rejected, name, fresh, cleanup=False)
        text = methods.census(result, 4, name)
        if ("ctnative.host_owner_proved = true" not in text
                or re.search(r"\bemitc\.func @main\(", text)
                or not boundary.REFUSAL.search(text)):
            raise RuntimeError(f"{name}: ownership supplied an unsupported native carrier\n{text}")

    shared_refusals = {
        "shared_uncalled": SHARED.replace("host.slot.set(); ", ""),
        "shared_effect": SHARED.replace("state.set('x', 1)", "inspect(state)"),
        "shared_return_map": SHARED.replace("get() { return state.size; }", "get() { return state; }"),
        "shared_rewrite": SHARED + "\nhost.slot.set = function() { return 1; };",
        "shared_detached": SHARED + "\nvar saved = host.slot.set;",
    }
    for name, source in shared_refusals.items():
        _, rejected, _ = boundary.prepare(args, name, source)
        fresh = contract(args, rejected, name)
        methods.refused(args, rejected, name, fresh)
    for name, source in parameter_refusals().items():
        _, rejected, count = boundary.prepare(args, name, source)
        fresh = contract(args, rejected, name)
        failed = methods.refused(args, rejected, name, fresh, admitted=0 if count == 5 else None)
        check_call_preservation(rejected.read_text(), failed.read_text(), name)
        if name == "parameter_heterogeneous":
            forged = args.work / "parameter-forged.mlir"
            forged.write_text(forge_map_presence(rejected.read_text()))
            forged_config = contract(args, forged, "parameter-forged")
            failed = methods.refused(args, forged, "parameter-forged", forged_config, admitted=0)
            check_call_preservation(forged.read_text(), failed.read_text(), "parameter-forged")
    for name, source in result_refusals().items():
        _, rejected, count = boundary.prepare(args, name, source)
        if count != 5:
            raise RuntimeError(f"{name}: changed result-refusal source denominator")
        fresh = contract(args, rejected, name)
        failed = methods.refused(args, rejected, name, fresh, admitted=0)
        check_call_preservation(rejected.read_text(), failed.read_text(), name)
        if name == "result_unknown_map_get":
            # A fresh fingerprint authenticates the unsupported source, not
            # a forged conclusion about its Map contents or method result.
            forged = args.work / "result-forged.mlir"
            forged.write_text(forge_map_presence(rejected.read_text()))
            forged_config = contract(args, forged, "result-forged")
            failed = methods.refused(args, forged, "result-forged", forged_config, admitted=0)
            check_call_preservation(forged.read_text(), failed.read_text(), "result-forged")
    for name, source in seeded_result_refusals().items():
        if name in PRIMITIVE_ABSENCE_CARRIERS:
            continue
        _, rejected, count = boundary.prepare(args, name, source)
        if count != 5:
            raise RuntimeError(f"{name}: changed seeded-refusal source denominator")
        fresh = contract(args, rejected, name)
        failed = methods.refused(args, rejected, name, fresh, admitted=0)
        check_call_preservation(rejected.read_text(), failed.read_text(), name)
        if name == "seeded_dynamic_bool_join":
            forged_name = name + "-forged"
            forged = args.work / f"{forged_name}.mlir"
            forged.write_text(forge_map_presence(rejected.read_text()))
            forged_config = contract(args, forged, forged_name)
            failed = methods.refused(args, forged, forged_name, forged_config, admitted=0)
            check_call_preservation(forged.read_text(), failed.read_text(), forged_name)
    check_primitive_absence_carriers(args, node, reference)
    for name, (source, value) in size_result_refusals().items():
        js, rejected, count = boundary.prepare(args, name, source)
        if count != 5:
            raise RuntimeError(f"{name}: changed size-refusal source denominator")
        expected = f"trace={value}\n"
        if (host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected
                or host.run([str(reference), str(js)]).stdout != expected):
            raise RuntimeError(f"{name}: Node/interpreter observation mismatch")
        blind = args.work / f"{name}-blinded.js"
        # Earlier removals may establish the current cardinality. Suppress
        # only the final, result-determining deletion in this control.
        parts = source.rsplit("state.delete(", 1)
        if len(parts) != 2:
            raise RuntimeError(f"{name}: size-key refusal lost its deleting operation")
        blind.write_text("state.has(".join(parts))
        if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == expected:
            raise RuntimeError(f"{name}: size-key refusal cannot distinguish a real deletion")
        fresh = contract(args, rejected, name)
        failed = methods.refused(args, rejected, name, fresh, admitted=0)
        check_call_preservation(rejected.read_text(), failed.read_text(), name)
        forged_name = name + "-forged"
        forged = args.work / f"{forged_name}.mlir"
        forged.write_text(forge_map_presence(rejected.read_text()))
        failed = methods.refused(args, forged, forged_name + "-stale", fresh,
                                 reason="fingerprint mismatch", admitted=0)
        check_call_preservation(forged.read_text(), failed.read_text(), forged_name + "-stale")
        forged_config = contract(args, forged, forged_name)
        failed = methods.refused(args, forged, forged_name, forged_config, admitted=0)
        if "fingerprint mismatch" in failed.read_text():
            raise RuntimeError(f"{forged_name}: fresh forgery skipped live size reanalysis")
        check_call_preservation(forged.read_text(), failed.read_text(), forged_name)
        rerun = methods.refused(args, failed, forged_name + "-rerun", forged_config, admitted=0)
        check_call_preservation(forged.read_text(), rerun.read_text(), forged_name + "-rerun")
    for name, (source, value) in {
        **payload_result_refusals(), **mixed_result_refusals(),
    }.items():
        if name in PRIMITIVE_ABSENCE_CARRIERS or name in primitive_absence_sources():
            continue
        js, rejected, count = boundary.prepare(args, name, source)
        if count != 6:
            raise RuntimeError(f"{name}: changed missing-payload source denominator")
        expected = f"trace={value}\n"
        if (host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected
                or host.run([str(reference), str(js)]).stdout != expected):
            raise RuntimeError(f"{name}: Node/interpreter observation mismatch")
        blind = args.work / f"{name}-blinded.js"
        blind.write_text(source.replace("state.delete(", "state.has("))
        if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == expected:
            raise RuntimeError(f"{name}: missing payload cannot be distinguished from its live value")
        fresh = contract(args, rejected, name)
        failed = methods.refused(args, rejected, name, fresh, admitted=0)
        check_call_preservation(rejected.read_text(), failed.read_text(), name)
        forged_name = name + "-forged"
        forged = args.work / f"{forged_name}.mlir"
        forged.write_text(forge_map_presence(rejected.read_text()))
        failed = methods.refused(args, forged, forged_name + "-stale", fresh,
                                 reason="fingerprint mismatch", admitted=0)
        check_call_preservation(forged.read_text(), failed.read_text(), forged_name + "-stale")
        forged_config = contract(args, forged, forged_name)
        failed = methods.refused(args, forged, forged_name, forged_config, admitted=0)
        if "fingerprint mismatch" in failed.read_text():
            raise RuntimeError(f"{forged_name}: fresh forgery skipped live payload reanalysis")
        check_call_preservation(forged.read_text(), failed.read_text(), forged_name)
        rerun = methods.refused(args, failed, forged_name + "-rerun", forged_config, admitted=0)
        check_call_preservation(forged.read_text(), rerun.read_text(), forged_name + "-rerun")
    for name, (source, value, old, replacement, restored_value) in {
        **{name: (*row, 1) for name, row in saved_read_refusals().items()},
        **saved_join_refusals(),
        **guarded_saved_refusals(),
        **{name: row for name, row in shortcircuit_refusals().items() if name != "shortcircuit_nullable"},
        **nullable_result_refusals(),
        **nullable_key_refusals(),
        **nullable_payload_refusals(),
    }.items():
        if name in PRIMITIVE_ABSENCE_CARRIERS:
            continue
        js, rejected, count = boundary.prepare(args, name, source)
        if count != 6:
            raise RuntimeError(f"{name}: changed saved-read refusal source denominator")
        expected = f"trace={value}\n"
        # This refusal deliberately reads an additional source global. The
        # reference prints that unchanged null binding after the trace too.
        reference_expected = expected + (
            "unknownResult=null\n" if name == "nullable_unknown_result" else "")
        if (host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected
                or host.run([str(reference), str(js)]).stdout != reference_expected):
            raise RuntimeError(f"{name}: Node/interpreter observation mismatch")
        if source.count(old) != 1:
            raise RuntimeError(f"{name}: lost the saved-read refusal observation")
        blind = args.work / f"{name}-blinded.js"
        blind.write_text(source.replace(old, replacement))
        if host.run([node, "-e", boundary.NODE, str(blind)]).stdout != f"trace={restored_value}\n":
            raise RuntimeError(f"{name}: saved scalar cannot distinguish the missing read")
        fresh = contract(args, rejected, name)
        for mode, options in (("default", ""), ("disabled", "optimize=false")):
            mode_name = name + "-" + mode
            failed = methods.refused(args, rejected, mode_name, fresh, options=options, admitted=0)
            check_call_preservation(rejected.read_text(), failed.read_text(), mode_name)
            # Valid scalar read markers cannot manufacture a missing get or
            # justify extracting an unproved value before set.
            for payload in ("bool", "string"):
                forged_name = mode_name + "-forged-" + payload
                forged = args.work / f"{forged_name}.mlir"
                forged.write_text(forge_map_presence(rejected.read_text(), payload))
                failed = methods.refused(args, forged, forged_name + "-stale", fresh,
                    options=options, reason="fingerprint mismatch", admitted=0)
                check_call_preservation(forged.read_text(), failed.read_text(), forged_name + "-stale")
                forged_config = contract(args, forged, forged_name)
                failed = methods.refused(args, forged, forged_name, forged_config,
                                         options=options, admitted=0)
                if "fingerprint mismatch" in failed.read_text():
                    raise RuntimeError(f"{forged_name}: forgery skipped live saved-value reanalysis")
                check_call_preservation(forged.read_text(), failed.read_text(), forged_name)
                rerun = methods.refused(args, failed, forged_name + "-rerun", forged_config,
                                        options=options, admitted=0)
                check_call_preservation(forged.read_text(), rerun.read_text(), forged_name + "-rerun")
    check_shortcircuit_nullable_refusal(args, node, reference)
    check_nullable_host_result_refusals(args, positives, node, reference)
    check_leaf_object_refusals(args, positives, node, reference)
    check_leaf_object_refusals(args, positives, node, reference,
        {name: row for name, row in leaf_readback_refusals().items()
         if name not in LEAF_ABSENCE_PROMOTED_REFUSALS | NUMERIC_ENTRY_PROMOTED})
    check_leaf_object_refusals(args, positives, node, reference,
        {name: row for name, row in leaf_absence_refusals().items() if name not in LEAF_CLEAR_PROMOTED})
    check_leaf_object_refusals(args, positives, node, reference, leaf_clear_refusals())
    check_leaf_object_refusals(args, positives, node, reference, numeric_entry_refusals())
    check_leaf_object_refusals(args, positives, node, reference, scalar_global_refusals())
    check_scalar_global_carriers(args, positives, node, reference)
    check_constant_global_observations(args, node, reference)
    check_constant_global_refusals(args, positives, node, reference)
    check_leaf_readback_carriers(args, positives, node, reference, leaf_field_result_refusals())
    # A result contract does not narrow Map storage or supply an implemented
    # callable signature. Preserve the prepared producer/consumer operands.
    mixed_read_refusals = mixed_nullable_payload_refusals()
    for name, (source, value) in {
        **seeded_carrier_refusals(),
        **{name: (row[0], row[1]) for name, row in mixed_read_refusals.items()},
    }.items():
        js, rejected, count = boundary.prepare(args, name, source)
        if count != 6:
            raise RuntimeError(f"{name}: changed seeded carrier source denominator")
        if name in mixed_read_refusals:
            expected_calls = 14 if name == "nullable_mixed_read_missing" else 15
            if len(source_calls(rejected.read_text())) != expected_calls:
                raise RuntimeError(f"{name}: changed the exact {expected_calls}-call refusal")
        if (host.run([node, "-e", boundary.NODE, str(js)]).stdout != f"trace={value}\n"
                or host.run([str(reference), str(js)]).stdout != f"trace={value}\n"):
            raise RuntimeError(f"{name}: Node/interpreter observation mismatch")
        fresh = contract(args, rejected, name)
        if name in mixed_read_refusals:
            _, _, old, replacement, restored_value = mixed_read_refusals[name]
            if (source.count(old) != 1 or source.replace(old, replacement)
                    != positives["nullable_payload_mixed_readback"][0]):
                raise RuntimeError(f"{name}: repair no longer restores the independently gated source")
            restored_js, restored_ir, restored_count = boundary.prepare(
                args, name + "-restored", source.replace(old, replacement))
            if (restored_count != 6
                    or host.run([node, "-e", boundary.NODE, str(restored_js)]).stdout
                    != f"trace={restored_value}\n"
                    or host.run([str(reference), str(restored_js)]).stdout
                    != f"trace={restored_value}\n"):
                raise RuntimeError(f"{name}: repaired read lost its discriminating observation")
            restored_config = contract(args, restored_ir, name + "-restored")
        modes = (("default", ""), ("disabled", "optimize=false")) if name.startswith("nullable") \
            else (("default", ""),)
        for mode, options in modes:
            mode_name = name + "-" + mode
            output = owned.lower(args, rejected, mode_name, fresh, options=options, cleanup=False)
            text = methods.census(output, count, mode_name, admitted=0)
            if "ctnative.host_owner_proved = true" not in text:
                raise RuntimeError(f"{mode_name}: did not independently prove the result owner")
            check_prepared_result_calls(text, rejected.read_text(), mode_name)
            if name in mixed_read_refusals:
                restored = owned.lower(args, restored_ir, mode_name + "-restored", restored_config,
                                       options=options)
                restored_text = methods.census(restored, 6, mode_name + "-restored", admitted=6)
                if "ctnative.host_owner_proved = true" not in restored_text:
                    raise RuntimeError(f"{mode_name}: restoring the live read did not restore ownership")
            payloads = ("bool", "string", "nullable_string") \
                if name in mixed_read_refusals else ("bool", "string")
            for payload in payloads:
                forged_name = mode_name + "-forged-" + payload
                forged = args.work / f"{forged_name}.mlir"
                forged.write_text(forge_map_presence(rejected.read_text(), payload))
                failed = methods.refused(args, forged, forged_name + "-stale", fresh,
                    options=options, reason="fingerprint mismatch", admitted=0)
                check_call_preservation(forged.read_text(), failed.read_text(), forged_name + "-stale")
                forged_config = contract(args, forged, forged_name)
                failed = owned.lower(args, forged, forged_name, forged_config,
                                     options=options, cleanup=False)
                forged_text = methods.census(failed, count, forged_name, admitted=0)
                if "ctnative.host_owner_proved = true" not in forged_text:
                    raise RuntimeError(f"{forged_name}: forged tags changed the carrier proof")
                check_prepared_result_calls(forged_text, forged.read_text(), forged_name)
                rerun = methods.refused(args, failed, forged_name + "-rerun", forged_config,
                    options=options, reason="fingerprint mismatch", admitted=0)
                check_call_preservation(forged_text, rerun.read_text(), forged_name + "-rerun")
    # An implicit undefined return has an exact primitive tag, but that alone
    # does not supply an implemented native Map key. The separate size method
    # keeps the observation numeric so that it cannot cause this refusal.
    js, missing, count = boundary.prepare(args, "result_missing_return",
        result_sources()["result_bool"][0].replace("get() { return state.has(false); }",
                                                   "get() { state.size; }"))
    if count != 6:
        raise RuntimeError("result_missing_return: changed source denominator")
    if (host.run([node, "-e", boundary.NODE, str(js)]).stdout != "trace=1\n"
            or host.run([str(reference), str(js)]).stdout != "trace=1\n"):
        raise RuntimeError("result_missing_return: Node/interpreter observation mismatch")
    missing_config = contract(args, missing, "result_missing_return")
    missing_output = owned.lower(args, missing, "result_missing_return", missing_config, cleanup=False)
    text = methods.census(missing_output, 6, "result_missing_return", admitted=0)
    if ("ctnative.host_owner_proved = true" not in text
            or "native Map needs supported keys" not in text
            or "!ctnative.map<!ctnative.opt<!ctnative.bottom>" not in text):
        raise RuntimeError("result_missing_return: missing unsupported-result diagnostic")
    # Ownership succeeded, so the established preparation may resolve calls
    # and insert their environment arguments before carrier admission refuses.
    # Check those runtime calls and result edges, not the old source spelling.
    prepared_calls = re.findall(
        r"^\s*(%[-\w.$]+) = ctjs\.call_direct @([-\w.$]+)\(([^\n]+)\) "
        r"\{ctnative\.stored_call = 1 : i32\}", text, re.M)
    actuals = [arguments.split(", ") for _, _, arguments in prepared_calls]
    if (len(source_calls(text)) != len(source_calls(missing.read_text()))
            or [target for _, target, _ in prepared_calls]
            != ["fn$4", "fn$5", "fn$4", "fn$5", "fn$3"]
            or [len(arguments) for arguments in actuals] != [4, 5, 4, 5, 4]
            or actuals[1][-1] != prepared_calls[0][0]
            or actuals[3][-1] != prepared_calls[2][0]
            or f'ctjs.store_global "trace", {prepared_calls[4][0]}' not in text):
        raise RuntimeError("result_missing_return: prepared calls lost live result order/operands")
    boundary.native(args, shared_ir, "shared-no-manifest", 5)
    methods.refused(args, shared_ir, "shared-no-intrinsic",
                    owned.contract(args, shared_ir, "shared-no-intrinsic"), admitted=0)
    boundary.native(args, parameter_ir, "parameter-no-manifest", 5)
    methods.refused(args, parameter_ir, "parameter-no-intrinsic",
                    owned.contract(args, parameter_ir, "parameter-no-intrinsic"), admitted=0)
    boundary.native(args, result_ir, "result-no-manifest", 5)
    methods.refused(args, result_ir, "result-no-intrinsic",
                    owned.contract(args, result_ir, "result-no-intrinsic"), admitted=0)
    stale_parameter = args.work / "parameter-stale.mlir"
    text, count = re.subn(r'#ctjs\.string<"x">', '#ctjs.string<"y">', parameter_ir.read_text())
    if count != 1:
        raise RuntimeError("parameter-stale: lost the live string actual")
    stale_parameter.write_text(text)
    failed = methods.refused(args, stale_parameter, "parameter-stale", parameter_config,
                             reason="fingerprint mismatch", admitted=0)
    check_call_preservation(text, failed.read_text(), "parameter-stale")
    rerun = owned.lower(args, parameter_output, "parameter-rerun", parameter_config, cleanup=False)
    text = methods.census(rerun, 5, "parameter-rerun", admitted=5)
    if "ctnative.host_owner_proved = false" not in text or "fingerprint mismatch" not in text:
        raise RuntimeError("parameter-rerun: prepared argument signature reused source authority")
    stale_result = args.work / "result-stale.mlir"
    text, count = re.subn(r'#ctjs\.string<"size">', '#ctjs.string<"other">', result_ir.read_text())
    if count != 2:
        raise RuntimeError("result-stale: lost a source Map size read")
    stale_result.write_text(text)
    failed = methods.refused(args, stale_result, "result-stale", result_config,
                             reason="fingerprint mismatch", admitted=0)
    check_call_preservation(text, failed.read_text(), "result-stale")
    rerun = owned.lower(args, result_output, "result-rerun", result_config, cleanup=False)
    text = methods.census(rerun, 5, "result-rerun", admitted=5)
    if "ctnative.host_owner_proved = false" not in text or "fingerprint mismatch" not in text:
        raise RuntimeError("result-rerun: prepared result signature reused source authority")
    boundary.native(args, seeded_ir, "seeded-no-manifest", 5)
    methods.refused(args, seeded_ir, "seeded-no-intrinsic",
                    owned.contract(args, seeded_ir, "seeded-no-intrinsic"), admitted=0)
    stale_seeded = args.work / "seeded-stale.mlir"
    text, count = re.subn(r'#ctjs\.number<0>', '#ctjs.number<4611686018427387904>',
                         seeded_ir.read_text())
    if count == 0:
        raise RuntimeError("seeded-stale: lost the live seed and lookup key")
    stale_seeded.write_text(text)
    failed = methods.refused(args, stale_seeded, "seeded-stale", seeded_config,
                             reason="fingerprint mismatch", admitted=0)
    check_call_preservation(text, failed.read_text(), "seeded-stale")
    rerun = owned.lower(args, seeded_output, "seeded-rerun", seeded_config, cleanup=False)
    text = methods.census(rerun, 5, "seeded-rerun", admitted=5)
    if "ctnative.host_owner_proved = false" not in text or "fingerprint mismatch" not in text:
        raise RuntimeError("seeded-rerun: prepared presence reused the original source authority")
    for name in ("seeded_size_two_entries", "seeded_size_two_saved_empty",
                 "result_seeded_bool", "result_seeded_string", "result_seeded_string_saved",
                 "result_seeded_mixed_contents", "result_seeded_join_reseed",
                 "result_seeded_bool_string_contents", "result_seeded_mixed_string_saved",
                 *saved_read_sources(), *saved_join_sources(), *guarded_saved_sources(),
                 *shortcircuit_sources(), *nullable_result_sources(), *nullable_key_sources(),
                 *nullable_payload_sources(), *nullable_host_result_sources(), *nullable_nested_result_sources()):
        _, config, output = saved[name]
        functions = RESULT_SIGNATURES[name][2]
        rerun = owned.lower(args, output, name + "-rerun", config,
                            cleanup=False)
        text = methods.census(rerun, functions, name + "-rerun", admitted=functions)
        if "ctnative.host_owner_proved = false" not in text or "fingerprint mismatch" not in text:
            raise RuntimeError(f"{name}: prepared Map reused the original source authority")
    for name in leaf_object_sources():
        _, config, output = saved[name]
        functions = LEAF_OBJECT_FUNCTIONS[name]
        rerun = owned.lower(args, output, name + "-rerun", config, cleanup=False)
        text = methods.census(rerun, functions, name + "-rerun", admitted=functions)
        if "ctnative.host_owner_proved = false" not in text or "fingerprint mismatch" not in text:
            raise RuntimeError(f"{name}: prepared leaf owner reused the original source authority")
    for name in ((leaf_readback_sources().keys() - LEAF_READBACK_UNOWNED)
                 | leaf_absence_sources().keys() | leaf_clear_sources().keys()
                 | numeric_entry_sources().keys() | scalar_global_sources().keys()
                 | constant_global_sources().keys()):
        _, config, output = saved[name]
        rerun = owned.lower(args, output, name + "-rerun", config, cleanup=False)
        text = methods.census(rerun, 5, name + "-rerun", admitted=5)
        if "ctnative.host_owner_proved = false" not in text or "fingerprint mismatch" not in text:
            raise RuntimeError(f"{name}: prepared readback reused the original source authority")
    for name in ("leaf_object_plain", "leaf_object_scalar_writes", "leaf_object_lifetime"):
        ir, config, _ = saved[name]
        rollback += check_budgets(args, ir, config, name, functions=LEAF_OBJECT_FUNCTIONS[name])
    for name in ("local_field_get_guarded_checked", "historical_object_saved_identity", "local_field_readback_lifetime_checked"):
        ir, config, _ = saved[name]
        rollback += check_budgets(args, ir, config, name, functions=5)
    for name in ("local_field_direct", "local_field_get_guarded", "local_field_readback_lifetime"):
        ir, config, _ = saved[name]
        rollback += check_budgets(args, ir, config, name, functions=5)
    for name in LEAF_COMPARISON_REPAIRS:
        ir, config, _ = saved[name]
        rollback += check_budgets(args, ir, config, name, functions=5)
    for name in ("local_absence_delete_undefined", "local_absence_distinct_branches_false"):
        ir, config, _ = saved[name]
        rollback += check_budgets(args, ir, config, name, functions=5)
    for name in ("local_absence_clear_saved_identity", "local_clear_both_branches_false"):
        ir, config, _ = saved[name]
        rollback += check_budgets(args, ir, config, name, functions=5)
    for name in ("local_identity_repeated_keys", "local_numeric_nested_key",
                 "local_add_saved_results", "scalar_result_key", "scalar_alias",
                 "scalar_alias_chain", "scalar_constant_only", "constant_alias_arithmetic",
                 "constant_boolean", "constant_boolean_alias_chain",
                 "constant_string", "constant_string_alias_chain"):
        ir, config, _ = saved[name]
        rollback += check_budgets(args, ir, config, name, functions=5)
    print(f"native captured Map ownership: {len(positives)} complete programs (4/4, 5/5, 6/6); "
          "Node/interpreter/GCC/Clang explicit+deduced and Map/table/callable lifetime pass; "
          f"{len(refusal_sources()) - 1} source refusals and contract/rerun/budget controls pass; "
          f"one nullable carrier refusal and {len(shared_refusals)} shared-method refusals; "
          "the historical five-call Boolean result retains its bool() callable and exact Boolean output; "
          f"{len(parameter_refusals())} argument refusals preserve current call operands; "
          "typed parameterized setters 5/5 with changing source and saved-callable keys; "
          f"{len(result_sources())} live result programs preserve call order and operands; "
          f"{len(result_refusals())} result-proof refusals and missing-return carrier refusal; "
          f"{len(seeded_result_sources())} seeded result programs and growing lifetime pass; "
          f"{len(key_fact_sources())} per-key result programs; "
          f"{len(joined_result_sources())} type-joined result programs; "
          f"{len(size_result_sources())} bounded-size programs and "
          f"{len(size_result_refusals())} size-key refusals with discriminating observations; "
          f"{len(payload_result_sources())} Bool/String payload programs and saved-string lifetime; "
          f"{len(payload_result_refusals()) - 1} missing-payload carrier refusal distinguishes false; "
          f"{len(mixed_result_sources())} closed mixed Map programs and saved-string lifetime; "
          f"{len(mixed_result_refusals())} mixed deleted-result refusals preserve calls; "
          f"{len(saved_read_sources())} saved-read/write programs in both optimization modes; "
          f"{len(saved_read_refusals())} saved-read missing/deleted refusals and saved-string lifetime; "
          f"{len(saved_join_sources())} conditional saved-value programs in both modes; "
          f"{len(saved_join_refusals())} conditional missing/deleted/mixed-tag refusals; "
          "both future getter flags and independent owning strings survive final Map release; "
          f"{len(guarded_saved_sources())} live has-guarded scalar programs in both modes; "
          f"{len(guarded_saved_refusals())} absent/stale/wrong-guard/payload refusals; "
          "guarded future reads own both selected Strings after final Map release; "
          f"{len(shortcircuit_sources())} short-circuit scalar programs in both modes; "
          f"{len(shortcircuit_refusals())} short-circuit guard/effect/tag refusals; "
          "the original nullable short-circuit result retains complete host ownership but refuses "
          "its optional Bool/String intermediate with prepared calls intact; "
          "present empty/false/zero select fallback, future Strings survive final Map release; "
          f"{len(nullable_result_sources())} nullable result programs preserve String/null/undefined; "
          "future nullable getter and owning strings survive reentry and final Map release; "
          f"{len(nullable_result_refusals())} unknown/mixed/object/effect nullable refusals; "
          f"{len(nullable_key_sources())} nullable Map-key programs preserve all four key identities; "
          f"{len(nullable_key_refusals())} mixed snapshot refusals and fresh/stale key forgeries; "
          "saved owning keys survive caller mutation, deletion, reentry and final Map release; "
          f"{len(nullable_payload_sources())} nullable payload write/readback programs and "
          f"{len(nullable_payload_refusals())} missing/object/snapshot refusals; "
          "stored String/null/undefined/empty tags and saved payload ownership survive "
          "overwrite/delete, caller mutation, reentry and final Map release; "
          "deleted nullable reads return Undefined independently of their former payload tag; "
          "mixed nullable reads retain the exact 14/19-call witnesses and their finite payloads; "
          f"{len(mixed_nullable_payload_refusals())} mixed missing/deleted/aliasing-result refusals "
          "retain host ownership and prepared calls under fresh/stale scalar/nullable read forgeries; "
          "restoring each exact live read restores 6/6 native in both modes; "
          f"{len(nullable_host_result_sources())} acyclic nullable host-result programs retain "
          "the exact 15-call chain, live conditional writes and owning saved results; "
          f"{len(nullable_host_result_refusals())} independent unknown/missing/deleted/aliasing "
          "host-result refusals and exact repairs pass both modes and fresh/stale proof controls; "
          f"{len(nullable_nested_result_sources())} same-method result programs retain the exact "
          "15-call nested chain, later nullable/String actuals and saved owning nested results; "
          f"{len(nullable_nested_result_refusals()) - 1} unknown/foreign/unseeded/later-actual "
          "host refusals and exact repairs pass both modes and fresh/stale proof controls; "
          "the historical leaf-writing sibling retains complete ownership and a separate "
          "Object/String carrier refusal with nested prepared operands intact; "
          f"{len(leaf_object_sources())} method-local leaf programs and the historical object payload "
          "preserve runtime allocation, Map writes, fixed scalar fields and numeric public signatures; "
          "future distinct objects and saved size/set/erase callables pass overwrite/deletion, reentry "
          "and final Map/object lifetime checks; "
          f"{len(leaf_object_refusals()) - 3} object graph/field/argument/result/read refusals "
          "restore exact gated sources and reject fresh/stale forged leaf reports; "
          f"{len(leaf_readback_sources()) - len(LEAF_READBACK_UNOWNED)} local leaf readback programs "
          "preserve definite object origins, strict identities and fixed scalar field reads; "
          "saved aliases observe later field writes across replacement/deletion and saved numeric "
          "callables pass future-argument, reentry and final-owner sanitizer lifetime checks; "
          f"{len(leaf_readback_refusals().keys() - LEAF_ABSENCE_PROMOTED_REFUSALS - NUMERIC_ENTRY_PROMOTED)} "
          "unknown/missing/export/field refusals restore exact sources; "
          f"{len(LEAF_FIELD_RESULTS)} exact raw field results remove only independently proved absence; "
          "saved raw numeric results and callable lifetimes pass future-argument and field mutations; "
          f"{len(LEAF_COMPARISON_REPAIRS)} exact comparison-only fresh allocations retain distinct "
          "identities, field writes and saved-callable lifetimes with their exact saved-object repairs; "
          f"{len(leaf_absence_sources())} absence and historical delete programs preserve exact "
          "raw/prepared calls, definite Undefined and saved-object repairs; "
          "nonidentical two-arm joins survive source preparation and both future flags; "
          f"{len(leaf_absence_refusals()) - len(LEAF_CLEAR_PROMOTED)} possible-alias and conditional "
          "absence refusals reject fresh/stale forgeries and restore exact admitted sources; "
          "saved Undefined across reseed and branch deletion pass final Map/leaf lifetime checks; "
          f"{len(leaf_clear_sources())} exact clear programs retain fresh arbitrary-key absence, "
          "saved object/Undefined reads, aliases, reseeding and surviving nonidentical branches; "
          f"{len(leaf_clear_refusals())} possible-alias, one-arm and evaluated-argument clear refusals "
          "retain every source operation under fresh/stale reports and exact admitted repairs; "
          "three clear lifetime families retain saved callables over 128 future calls, release "
          "both Maps after reentry and preserve an observed leaf until its final owner releases; "
          f"{len(numeric_entry_sources())} numeric entry programs retain evaluated arithmetic and call order; "
          f"{len(numeric_entry_refusals())} non-Number/future-input refusals retain their exact repairs; "
          f"{len(NUMERIC_ENTRY_SAVED_GLOBALS)} historical saved-global sources and "
          f"{len(scalar_global_sources())} new scalar programs retain live global stores, loads and arithmetic; "
          f"{len(scalar_global_refusals())} source-order/write/future-family refusals and "
          f"{len(SCALAR_GLOBAL_CARRIERS)} scalar Map identity refusals retain exact repairs; "
          f"{len(SCALAR_GLOBAL_INITIALIZED)} original alias/direct observations retain definite stored-value types; "
          f"{len(constant_global_cases())} constant-global probes/candidate edits preserve exact scalar observations; "
          f"{len(CONSTANT_GLOBAL_UNOWNED)} unowned/{len(CONSTANT_GLOBAL_CARRIERS)} complete-owner refusals, "
          "constant-only alias/arithmetic edges and mixed saved-Map lifetimes pass; "
          "Boolean aliases/results retain true/false output with independent Number/Boolean tags; "
          "14 wrong-tag/null/missing-store observation mutations reach the exact termination check; "
          f"{len(NUMERIC_ENTRY_LIFETIMES)} numeric lifetime families retain 128 future results across both branches, reentry, "
          "final Map release and independent leaf release; "
          f"{len(leaf_field_result_refusals())} complete-schema field results "
          "retain complete host ownership and separate native carrier refusals; "
          f"{len(PRIMITIVE_ABSENCE_CARRIERS)} exact absent-result carrier refusals preserve complete owners, "
          "concrete diagnostics and prepared producer/consumer/capture operands; "
          "the unchanged ten-call empty-String deletion source and exact repair preserve nullable "
          "key signatures and future Undefined/empty/distinct-key observations; "
          f"{len(seeded_result_refusals().keys() - PRIMITIVE_ABSENCE_CARRIERS)} seeded proof and "
          f"{len(seeded_carrier_refusals())} seeded carrier refusals; "
          f"{len(rollback)} speculative rollback cutoffs")
