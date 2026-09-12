"""Execute checked published Map methods and live primitive results without the VM."""

from concurrent.futures import ThreadPoolExecutor
import os

from .driver_common import *
from .driver_object_maps import *
from .driver_globals import *
from .driver_observations import *
from .driver_fields import *
from .driver_map_sizes import *
from .driver_object_keys import *
from .driver_nested_maps import check_nested_maps

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--node")
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--group", choices=("all", "object-keys", "nested-maps"), default="all")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1,
                        help="parallel positive programs (default: available CPUs)")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be a positive integer")
    args.work.mkdir(parents=True, exist_ok=True)
    node = boundary.node_executable(args)
    reference = boundary.reference_tool(args.opt)
    compilers = [next((shutil.which(c) for c in choices if shutil.which(c)), None)
                 for choices in (("g++-13", "g++"), ("clang++-18", "clang++"))]
    nm = shutil.which("nm") or shutil.which("llvm-nm")
    if not all(compilers) or not nm or not owned.VM.search(
            host.run([nm, "-C", str(reference)]).stdout):
        raise RuntimeError("need both host compilers and a working VM-symbol control")
    if args.group in {"all", "nested-maps"}:
        check_nested_maps(args, node, reference, compilers, nm)
        if args.group == "nested-maps":
            return
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
        **string_field_sources(), **zero_size_sources(), **one_size_sources(), **delete_size_sources(),
        **join_size_sources(), **mutation_size_sources(), **object_argument_sources(),
        # Keep the original refusal source byte-for-byte. Its method-local
        # empty payload now has the same independently proved leaf owner.
        "object_payload": (refusal_sources()["object_payload"], "host", 1),
    }
    saved = {}
    if args.group == "object-keys":
        positives = object_argument_sources()
        check_object_argument_observations(args, node, reference)
    else:
        check_source_observations(args, node, reference, positives)

    def check_positive(item):
        name, (source, binding, value) = item
        js, ir, count = boundary.prepare(args, name, source)
        functions = (object_argument_cases()[name]['functions'] if name in object_argument_sources()
                     else LEAF_OBJECT_FUNCTIONS[name] if name in LEAF_OBJECT_FUNCTIONS
                     else 5 if name in LEAF_READBACK_CALLS or name in leaf_absence_sources()
                     or name in leaf_clear_sources() or name in numeric_entry_sources() or name in scalar_global_sources()
                     or name in constant_global_sources()
                     or name in string_field_sources() or name in zero_size_sources() or name in one_size_sources()
                     or name in delete_size_sources() or name in join_size_sources() or name in mutation_size_sources()
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
        check_string_field_census(args, ir, name)
        check_zero_size_census(args, ir, name)
        check_one_size_census(args, ir, name)
        check_delete_size_census(args, ir, name)
        check_join_size_census(args, ir, name)
        check_mutation_size_census(args, ir, name)
        check_object_argument_census(args, ir, name)
        if name in primitive_absence_sources() and (
                len(source_calls((args.work / f"{name}.raw.mlir").read_text())) != 10
                or len(source_calls(ir.read_text())) != 10):
            raise RuntimeError(f"{name}: changed the exact ten-call primitive absence source")
        if name == "already_resolved":
            ir = resolve_getter(args, ir)
        if name.startswith("legacy_"):
            ir = methods.legacy_marker(args, ir, name)
        expected = f"trace={str(value).lower() if isinstance(value, bool) else value}\n"
        if isinstance(value, StringValue):
            expected = scalar_global_output(name, value)
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
                    **scalar_global_sources(), **constant_global_sources(), **string_field_sources(),
                    **join_size_sources(), **mutation_size_sources(), **object_argument_sources()}:
            disabled = owned.lower(args, ir, name + "-disabled", config, options="optimize=false")
            if disabled.read_text() != output.read_text():
                raise RuntimeError(f"{name}: saved scalar proof depends on optimization policy")
        standalone(args, output, name, value, compilers, nm)
        check_scalar_global_emission(args, ir, name)
        return name, (ir, config, output)

    # Each case owns its files; publish completed results in source order.
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        for name, result in executor.map(check_positive, positives.items()):
            saved[name] = result

    check_object_argument_controls(args, saved)
    if args.group == "object-keys":
        print(f"object keys: {len(positives)} native programs and their controls")
        return
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
    check_leaf_object_forgeries(args, saved,
        ("leaf_object_string_field", "field_string_bytes", "field_string_saved", "field_string_lifetime"))
    check_string_field_tag_mutations(args, compilers, nm)

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
        if name in object_argument_sources():
            continue
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
        if name in PRIMITIVE_ABSENCE_CARRIERS | HISTORICAL_STRING_FIELD_CARRIERS:
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
    check_nullable_host_result_refusals(args, positives, node, reference, compilers, nm)
    check_leaf_object_refusals(args, positives, node, reference)
    check_leaf_object_refusals(args, positives, node, reference,
        {name: row for name, row in leaf_readback_refusals().items()
         if name not in LEAF_ABSENCE_PROMOTED_REFUSALS | NUMERIC_ENTRY_PROMOTED | HISTORICAL_STRING_FIELD_CARRIERS})
    check_leaf_object_refusals(args, positives, node, reference,
        {name: row for name, row in leaf_absence_refusals().items() if name not in LEAF_CLEAR_PROMOTED})
    check_leaf_object_refusals(args, positives, node, reference, leaf_clear_refusals())
    check_leaf_object_refusals(args, positives, node, reference, numeric_entry_refusals())
    check_leaf_object_refusals(args, positives, node, reference, scalar_global_refusals())
    check_scalar_global_carriers(args, positives, node, reference)
    check_constant_global_observations(args, node, reference)
    check_constant_global_refusals(args, positives, node, reference)
    check_leaf_readback_carriers(args, positives, node, reference, leaf_field_result_refusals())
    check_string_field_refusals(args, positives)
    check_zero_size_refusals(args, positives)
    check_leaf_object_forgeries(args, saved, ("local_clear_zero_size_key", "zero_size_saved_lifetime"))
    check_one_size_refusals(args, positives)
    check_delete_size_refusals(args, positives)
    check_join_size_refusals(args, positives)
    check_mutation_size_refusals(args, positives)
    check_leaf_object_forgeries(args, saved,
        ("joined_disjoint_set_false", "joined_absent_delete_false", "joined_mutation_saved_lifetime"))
    check_leaf_object_forgeries(args, saved, ("joined_delete_disjoint_false", "joined_size_saved_lifetime"))
    check_leaf_object_forgeries(args, saved, ("size_deleted_literal_last", "size_deleted_saved_lifetime"))
    check_leaf_object_forgeries(args, saved, ("zero_size_read_after_write", "size_one_saved_lifetime"))
    check_historical_string_field_refusals(args, positives, node, reference)
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
                 | constant_global_sources().keys() | string_field_sources().keys()
                 | zero_size_sources().keys() | one_size_sources().keys() | delete_size_sources().keys()
                 | join_size_sources().keys() | mutation_size_sources().keys()):
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
                 "constant_string", "constant_string_alias_chain",
                 "field_string_read", "field_string_lifetime",
                 "local_clear_zero_size_key", "zero_size_saved_lifetime",
                 "zero_size_read_after_write", "size_one_saved_lifetime",
                 "size_deleted_literal_last", "size_deleted_saved_lifetime",
                 "joined_delete_disjoint_false", "joined_size_saved_lifetime",
                 "joined_disjoint_set_false", "joined_mutation_saved_lifetime"):
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
          "the unchanged historical foreign-empty-Map source now preserves Undefined results "
          "through nested/future calls and owning lifetimes; "
          f"{len(nullable_nested_result_refusals()) - 2} unknown/unseeded/later-actual "
          "host refusals and exact repairs pass both modes and fresh/stale proof controls; "
          "the historical leaf-writing sibling retains complete ownership and a separate "
          "Object/String carrier refusal with nested prepared operands intact; "
          f"{len(leaf_object_sources())} method-local leaf programs and the historical object payload "
          "preserve runtime allocation, Map writes, fixed scalar fields and numeric public signatures; "
          "future distinct objects and saved size/set/erase callables pass overwrite/deletion, reentry "
          "and final Map/object lifetime checks; "
          f"{len(leaf_object_refusals()) - 3 - len(STRING_FIELD_PROMOTED)} object graph/field/argument/result/read refusals "
          "restore exact gated sources and reject fresh/stale forged leaf reports; "
          f"{len(leaf_readback_sources()) - len(LEAF_READBACK_UNOWNED)} local leaf readback programs "
          "preserve definite object origins, strict identities and fixed scalar field reads; "
          "saved aliases observe later field writes across replacement/deletion and saved numeric "
          "callables pass future-argument, reentry and final-owner sanitizer lifetime checks; "
          f"{len(leaf_readback_refusals().keys() - LEAF_ABSENCE_PROMOTED_REFUSALS - NUMERIC_ENTRY_PROMOTED - HISTORICAL_STRING_FIELD_CARRIERS)} "
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
          f"{len(zero_size_sources())} exact zero-size programs preserve all thirteen historical sources, "
          "live reads/calls, SameValueZero and saved facts through growth; "
          f"{len(zero_size_cases()) - len(zero_size_sources())} unproved size/branch refusals keep calls "
          "under stale/fresh forgeries and exact repairs; one saved-zero lifetime runs 128 future "
          "calls/both flags through owner/table release, reentry and final Map/leaf destruction; "
          f"{len(one_size_sources())} exact finite-size programs preserve all eight continuation sources; "
          "saved one/two, aliases, repeated keys and both structural arms keep runtime reads/calls; "
          f"{len(one_size_cases()) - len(one_size_sources())} independent cardinality refusals retain "
          "fresh/stale forgeries, reruns and exact admitted repairs; a saved-one lifetime runs "
          "128 future calls/both flags, owner/table release, reentry and final Map/leaf destruction; "
          f"{len(delete_size_sources())} delete-size programs preserve all ten continuation sources; "
          "current object fields, aliases, structural arms and immutable sizes retain runtime operations; "
          f"{len(delete_size_cases()) - len(delete_size_sources())} independent deletion refusals retain "
          "fresh/stale forgeries and exact repairs; a saved deletion lifetime runs 128 future calls, "
          "both flags, owner/table release, reentry and final Map/leaf destruction; "
          f"{len(join_size_sources())} equal-cardinality join programs preserve twelve continuation sources; "
          "disjoint deleting/writing arms retain real saved-size and object-field reads; "
          f"{len(join_size_cases()) - len(join_size_sources())} unequal-size, missing-common-key and "
          "post-join mutation refusals retain exact repairs and fresh/stale evidence checks; "
          "one saved join lifetime runs 128 future calls/both flags through final Map/leaf release; "
          f"{len(mutation_size_sources())} known after-join mutation programs preserve twelve continuation sources; "
          "definite insertion, overwrite and deletion keep real size/Map/field operations; "
          f"{len(mutation_size_cases()) - len(mutation_size_sources())} uncertain mutation refusals retain "
          "exact repairs, fresh/stale forgeries and future key/flag observations; "
          "one saved-two mutation lifetime runs 128 future calls through final Map/leaf release; "
          f"{len(object_argument_sources())} fresh empty-object argument programs preserve live Map calls; "
          f"{len(object_argument_cases()) - len(object_argument_sources())} argument/effect/carrier refusals remain; "
          "saved object-key getters distinguish same/alias/distinct keys over 128 future rounds, "
          "release borrowed arguments; saved siblings retain keys across caller release, overwrite, delete, "
          "clear, reentry and final Map/key release; "
          f"{len(string_field_sources())} owning String-field programs preserve exact tags, bytes and live accesses; "
          "six field refusal/repair families and six emitted field-tag controls remain independent; "
          "two historical String-field sources retain complete ownership and exact equality/mixed-Map refusals; "
          "saved String callables and snapshots survive 128 future calls, both flags and final Map/leaf release; "
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
