"""Execute checked published Map methods and live primitive results without the VM."""

from .driver_execution import *


def main(group=None):
    args, node, reference, compilers, nm = setup()
    if group is not None:
        group(args, node, reference, compilers, nm)
        return
    positives = positive_cases()
    check_source_observations(args, node, reference, positives)
    saved = {
        name: check_positive(args, node, reference, compilers, nm, name, spec)
        for name, spec in positives.items()
    }
    check_primitive_absence_forgeries(args, saved, node, reference)
    check_leaf_object_forgeries(args, saved)
    check_leaf_object_forgeries(
        args,
        saved,
        (
            "local_field_get_guarded_checked",
            "historical_object_saved_identity",
            "local_field_readback_lifetime_checked",
        ),
    )
    check_leaf_object_forgeries(
        args,
        saved,
        ("local_field_direct", "local_field_saved_overwrite", "local_field_readback_lifetime"),
    )
    check_leaf_object_forgeries(args, saved, LEAF_COMPARISON_REPAIRS)
    check_leaf_object_forgeries(
        args,
        saved,
        (
            "local_absence_delete_undefined",
            "local_absence_saved_undefined",
            "local_absence_distinct_branches_false",
        ),
    )
    check_leaf_object_forgeries(
        args,
        saved,
        (
            "local_absence_clear_saved_identity",
            "local_clear_saved_undefined",
            "local_clear_both_branches_false",
        ),
    )
    check_leaf_object_forgeries(
        args,
        saved,
        (
            "local_identity_repeated_keys",
            "local_numeric_nested_key",
            "local_numeric_branch_lifetime",
            "local_numeric_bool",
        ),
    )
    check_leaf_object_forgeries(
        args,
        saved,
        (
            "local_add_saved_results",
            "local_numeric_saved_snapshot",
            "scalar_result_key",
            "scalar_saved_branch_lifetime",
        ),
    )
    check_leaf_object_forgeries(
        args,
        saved,
        (*sorted(SCALAR_GLOBAL_INITIALIZED), "scalar_alias_chain", "scalar_alias_branch_lifetime"),
    )
    check_leaf_object_forgeries(args, saved, ("scalar_constant_only", "constant_alias_arithmetic"))
    check_leaf_object_forgeries(
        args,
        saved,
        (
            "constant_boolean",
            "constant_boolean_true",
            "constant_boolean_alias_chain",
            "constant_boolean_saved_result",
            "constant_boolean_branch_lifetime",
        ),
    )
    check_boolean_observation_mutations(args, compilers, nm)
    check_leaf_object_forgeries(
        args,
        saved,
        (
            "constant_string",
            "constant_string_bytes",
            "constant_string_alias_chain",
            "constant_string_trace_empty",
            "constant_string_branch_lifetime",
        ),
    )
    check_string_observation_mutations(args, compilers, nm)
    check_leaf_object_forgeries(
        args,
        saved,
        (
            "leaf_object_string_field",
            "field_string_bytes",
            "field_string_saved",
            "field_string_lifetime",
        ),
    )
    check_string_field_tag_mutations(args, compilers, nm)

    # Valid-looking scalar markers cannot normalize real null keys or narrow
    # the second use of a nullable formal. Fresh proof must emit the same C++.
    for name in (
        "nullable_key_identity",
        "nullable_second_key_use",
        "nullable_payload_readback",
        "nullable_payload_mixed",
        "nullable_payload_deleted",
        "nullable_payload_mixed_readback",
        "nullable_mixed_payload_readback",
        "nullable_payload_mixed_identity",
        "nullable_payload_mixed_saved",
        *nullable_host_result_sources(),
        *nullable_nested_result_sources(),
    ):
        ir, config, output = saved[name]
        expected_cpp = comparable_provenance(
            host.run([args.translate, "--mlir-to-cpp", str(output)]).stdout, ir
        )
        for mode, options in (("default", ""), ("disabled", "optimize=false")):
            for payload in ("bool", "string", "nullable_string"):
                forged_name = name + "-" + mode + "-forged-" + payload
                forged = args.work / f"{forged_name}.mlir"
                forged.write_text(forge_map_presence(ir.read_text(), payload))
                failed = methods.refused(
                    args,
                    forged,
                    forged_name + "-stale",
                    config,
                    options=options,
                    reason="fingerprint mismatch",
                    admitted=0,
                )
                check_call_preservation(
                    forged.read_text(), failed.read_text(), forged_name + "-stale"
                )
                fresh = contract(args, forged, forged_name)
                checked = owned.lower(args, forged, forged_name, fresh, options=options)
                text = methods.census(checked, 6, forged_name, admitted=6)
                if (
                    "ctnative.host_owner_proved = true" not in text
                    or comparable_provenance(
                        host.run([args.translate, "--mlir-to-cpp", str(checked)]).stdout, forged
                    )
                    != expected_cpp
                ):
                    raise RuntimeError(
                        f"{forged_name}: forged scalar tags changed nullable storage"
                    )

    ir, config, output = saved["ordinary"]
    boundary.native(args, ir, "no-manifest", 4)
    absent = owned.contract(args, ir, "no-intrinsic")
    methods.refused(args, ir, "no-intrinsic", absent, admitted=0)
    for budget in (0, 32):
        methods.refused(
            args,
            ir,
            f"budget-{budget}",
            config,
            options=f"host-max-steps={budget}",
            reason="budget",
            admitted=0,
        )
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
    _, repeated, count = boundary.prepare(
        args, "budget-repeated", SOURCE + "\ntrace = host.slot.get();" * 15
    )
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
    for name in (
        "seeded_earlier_key",
        "seeded_other_delete",
        "seeded_dynamic_write",
        "seeded_dynamic_formal",
        "seeded_dynamic_delete",
        "seeded_size_saved",
        "seeded_size_two_entries",
        "seeded_size_two_saved_empty",
        "result_seeded_bool",
        "result_seeded_string",
        "result_seeded_string_saved",
        "result_seeded_mixed_contents",
        "result_seeded_join_reseed",
        "result_seeded_bool_string_contents",
        "result_seeded_mixed_string_saved",
        "saved_read_write",
        "saved_read_write_false",
        "saved_read_write_number",
        "saved_read_write_string_saved",
        "saved_join",
        "saved_join_bool",
        "saved_join_number",
        "saved_join_string_saved",
        "guarded_saved_read",
        "guarded_saved_bool",
        "guarded_saved_number",
        "guarded_saved_string_saved",
        "shortcircuit_same_tag",
        "shortcircuit_false",
        "shortcircuit_zero",
        "shortcircuit_string_saved",
        "nullable_normalized",
        "nullable_threeway",
        "nullable_string_saved",
        "nullable_key_homogeneous",
        "nullable_key_identity",
        "nullable_original_key",
        "nullable_key_string_saved",
        "nullable_payload_readback",
        "nullable_payload_mixed",
        "nullable_payload_saved",
        "nullable_payload_mixed_readback",
        "nullable_mixed_payload_readback",
        "nullable_payload_mixed_identity",
        "nullable_payload_mixed_saved",
        "nullable_host_result",
        "nullable_host_result_conditional",
        "nullable_host_result_saved",
        "nullable_nested_result",
        "nullable_nested_result_identity",
        "nullable_nested_result_saved",
    ):
        key_ir, key_config, _ = saved[name]
        rollback += check_budgets(
            args, key_ir, key_config, name, functions=RESULT_SIGNATURES[name][2]
        )
    seeded_ir, seeded_config, seeded_output = saved["result_seeded_map_get"]
    rollback += check_budgets(args, seeded_ir, seeded_config, "result_seeded_map_get", functions=5)

    for name, source in refusal_sources().items():
        if name == "object_payload":
            continue
        _, rejected, _ = boundary.prepare(args, name, source)
        fresh = contract(args, rejected, name)
        methods.refused(args, rejected, name, fresh)
    # Preserve the historical source and observe its actual Undefined tag.
    for name, body in {
        "nullable_result": "state.set('x', 1); return state.get('missing');",
    }.items():
        js, rejected, count = boundary.prepare(
            args, name, SOURCE.replace("return state.size;", body)
        )
        expected = "trace=undefined\n"
        reference_result = host.run([str(reference), str(js)])
        if (
            count != 4
            or host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), '["trace"]']).stdout != expected
            or reference_result.stdout != expected
            or "(0 number, 0 boolean, 0 string, 0 null, 1 undefined)" not in reference_result.stderr
        ):
            raise RuntimeError(f"{name}: lost the exact nullable source observation")
        fresh = contract(args, rejected, name)
        for mode, options in (("default", ""), ("disabled", "optimize=false")):
            label = name + "-" + mode
            result = owned.lower(args, rejected, label, fresh, options=options)
            text = methods.census(result, count, label, admitted=count)
            if "ctnative.host_owner_proved = true" not in text:
                raise RuntimeError(f"{label}: nullable output lost its independent owner proof")
            owned.standalone(args, result, label, expected, compilers, nm)
            expected_cpp = comparable_provenance(
                host.run([args.translate, "--mlir-to-cpp", str(result)]).stdout, rejected
            )
            for payload in ("bool", "string", "nullable_string"):
                forged_name = label + "-forged-" + payload
                forged = args.work / f"{forged_name}.mlir"
                forged.write_text(forge_map_presence(rejected.read_text(), payload))
                stale = methods.refused(
                    args,
                    forged,
                    forged_name + "-stale",
                    fresh,
                    options=options,
                    reason="fingerprint mismatch",
                    admitted=0,
                )
                check_call_preservation(
                    forged.read_text(), stale.read_text(), forged_name + "-stale"
                )
                forged_config = contract(args, forged, forged_name)
                checked = owned.lower(args, forged, forged_name, forged_config, options=options)
                forged_text = methods.census(checked, count, forged_name, admitted=count)
                if (
                    "ctnative.host_owner_proved = true" not in forged_text
                    or comparable_provenance(
                        host.run([args.translate, "--mlir-to-cpp", str(checked)]).stdout, forged
                    )
                    != expected_cpp
                ):
                    raise RuntimeError(
                        f"{forged_name}: forged result facts changed nullable output"
                    )

    shared_refusals = {
        "shared_uncalled": SHARED.replace("host.slot.set(); ", ""),
        "shared_effect": SHARED.replace("state.set('x', 1)", "inspect(state)"),
        "shared_return_map": SHARED.replace(
            "get() { return state.size; }", "get() { return state; }"
        ),
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
    check_primitive_absence_carriers(args, node, reference, compilers, nm)
    for name, (source, value) in size_result_refusals().items():
        js, rejected, count = boundary.prepare(args, name, source)
        if count != 5:
            raise RuntimeError(f"{name}: changed size-refusal source denominator")
        expected = f"trace={value}\n"
        if (
            host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected
            or host.run([str(reference), str(js)]).stdout != expected
        ):
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
        failed = methods.refused(
            args, forged, forged_name + "-stale", fresh, reason="fingerprint mismatch", admitted=0
        )
        check_call_preservation(forged.read_text(), failed.read_text(), forged_name + "-stale")
        forged_config = contract(args, forged, forged_name)
        failed = methods.refused(args, forged, forged_name, forged_config, admitted=0)
        if "fingerprint mismatch" in failed.read_text():
            raise RuntimeError(f"{forged_name}: fresh forgery skipped live size reanalysis")
        check_call_preservation(forged.read_text(), failed.read_text(), forged_name)
        rerun = methods.refused(args, failed, forged_name + "-rerun", forged_config, admitted=0)
        check_call_preservation(forged.read_text(), rerun.read_text(), forged_name + "-rerun")
    for name, (source, value) in {
        **payload_result_refusals(),
        **mixed_result_refusals(),
    }.items():
        if name in PRIMITIVE_ABSENCE_CARRIERS or name in primitive_absence_sources():
            continue
        js, rejected, count = boundary.prepare(args, name, source)
        if count != 6:
            raise RuntimeError(f"{name}: changed missing-payload source denominator")
        expected = f"trace={value}\n"
        if (
            host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected
            or host.run([str(reference), str(js)]).stdout != expected
        ):
            raise RuntimeError(f"{name}: Node/interpreter observation mismatch")
        blind = args.work / f"{name}-blinded.js"
        blind.write_text(source.replace("state.delete(", "state.has("))
        if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == expected:
            raise RuntimeError(
                f"{name}: missing payload cannot be distinguished from its live value"
            )
        fresh = contract(args, rejected, name)
        failed = methods.refused(args, rejected, name, fresh, admitted=0)
        check_call_preservation(rejected.read_text(), failed.read_text(), name)
        forged_name = name + "-forged"
        forged = args.work / f"{forged_name}.mlir"
        forged.write_text(forge_map_presence(rejected.read_text()))
        failed = methods.refused(
            args, forged, forged_name + "-stale", fresh, reason="fingerprint mismatch", admitted=0
        )
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
        **{
            name: row
            for name, row in shortcircuit_refusals().items()
            if name != "shortcircuit_nullable"
        },
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
            "unknownResult=null\n" if name == "nullable_unknown_result" else ""
        )
        if (
            host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected
            or host.run([str(reference), str(js)]).stdout != reference_expected
        ):
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
                failed = methods.refused(
                    args,
                    forged,
                    forged_name + "-stale",
                    fresh,
                    options=options,
                    reason="fingerprint mismatch",
                    admitted=0,
                )
                check_call_preservation(
                    forged.read_text(), failed.read_text(), forged_name + "-stale"
                )
                forged_config = contract(args, forged, forged_name)
                failed = methods.refused(
                    args, forged, forged_name, forged_config, options=options, admitted=0
                )
                if "fingerprint mismatch" in failed.read_text():
                    raise RuntimeError(
                        f"{forged_name}: forgery skipped live saved-value reanalysis"
                    )
                check_call_preservation(forged.read_text(), failed.read_text(), forged_name)
                rerun = methods.refused(
                    args, failed, forged_name + "-rerun", forged_config, options=options, admitted=0
                )
                check_call_preservation(
                    forged.read_text(), rerun.read_text(), forged_name + "-rerun"
                )
    check_shortcircuit_nullable_refusal(args, node, reference)
    check_nullable_host_result_refusals(args, positives, node, reference, compilers, nm)
    check_leaf_object_refusals(args, positives, node, reference)
    check_leaf_object_refusals(
        args,
        positives,
        node,
        reference,
        {
            name: row
            for name, row in leaf_readback_refusals().items()
            if name
            not in LEAF_ABSENCE_PROMOTED_REFUSALS
            | NUMERIC_ENTRY_PROMOTED
            | HISTORICAL_STRING_FIELD_CARRIERS
        },
    )
    check_leaf_object_refusals(
        args,
        positives,
        node,
        reference,
        {
            name: row
            for name, row in leaf_absence_refusals().items()
            if name not in LEAF_CLEAR_PROMOTED
        },
    )
    check_leaf_object_refusals(args, positives, node, reference, leaf_clear_refusals())
    check_leaf_object_refusals(args, positives, node, reference, numeric_entry_refusals())
    check_leaf_object_refusals(args, positives, node, reference, scalar_global_refusals())
    check_scalar_global_carriers(args, positives, node, reference)
    check_constant_global_observations(args, node, reference)
    check_constant_global_refusals(args, positives, node, reference)
    check_leaf_readback_carriers(
        args, positives, node, reference, leaf_field_result_refusals(), compilers, nm
    )
    check_string_field_refusals(args, positives)
    check_zero_size_refusals(args, positives)
    check_leaf_object_forgeries(
        args, saved, ("local_clear_zero_size_key", "zero_size_saved_lifetime")
    )
    check_one_size_refusals(args, positives)
    check_delete_size_refusals(args, positives)
    check_join_size_refusals(args, positives)
    check_mutation_size_refusals(args, positives)
    check_leaf_object_forgeries(
        args,
        saved,
        (
            "joined_disjoint_set_false",
            "joined_absent_delete_false",
            "joined_mutation_saved_lifetime",
        ),
    )
    check_leaf_object_forgeries(
        args, saved, ("joined_delete_disjoint_false", "joined_size_saved_lifetime")
    )
    check_leaf_object_forgeries(
        args, saved, ("size_deleted_literal_last", "size_deleted_saved_lifetime")
    )
    check_leaf_object_forgeries(
        args, saved, ("zero_size_read_after_write", "size_one_saved_lifetime")
    )
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
        if (
            host.run([node, "-e", boundary.NODE, str(js)]).stdout != f"trace={value}\n"
            or host.run([str(reference), str(js)]).stdout != f"trace={value}\n"
        ):
            raise RuntimeError(f"{name}: Node/interpreter observation mismatch")
        fresh = contract(args, rejected, name)
        if name in mixed_read_refusals:
            _, _, old, replacement, restored_value = mixed_read_refusals[name]
            if (
                source.count(old) != 1
                or source.replace(old, replacement)
                != positives["nullable_payload_mixed_readback"][0]
            ):
                raise RuntimeError(
                    f"{name}: repair no longer restores the independently gated source"
                )
            restored_js, restored_ir, restored_count = boundary.prepare(
                args, name + "-restored", source.replace(old, replacement)
            )
            if (
                restored_count != 6
                or host.run([node, "-e", boundary.NODE, str(restored_js)]).stdout
                != f"trace={restored_value}\n"
                or host.run([str(reference), str(restored_js)]).stdout
                != f"trace={restored_value}\n"
            ):
                raise RuntimeError(f"{name}: repaired read lost its discriminating observation")
            restored_config = contract(args, restored_ir, name + "-restored")
        modes = (
            (("default", ""), ("disabled", "optimize=false"))
            if name.startswith("nullable")
            else (("default", ""),)
        )
        for mode, options in modes:
            mode_name = name + "-" + mode
            output = owned.lower(args, rejected, mode_name, fresh, options=options, cleanup=False)
            text = methods.census(output, count, mode_name, admitted=0)
            if "ctnative.host_owner_proved = true" not in text:
                raise RuntimeError(f"{mode_name}: did not independently prove the result owner")
            check_prepared_result_calls(text, rejected.read_text(), mode_name)
            if name in mixed_read_refusals:
                restored = owned.lower(
                    args, restored_ir, mode_name + "-restored", restored_config, options=options
                )
                restored_text = methods.census(restored, 6, mode_name + "-restored", admitted=6)
                if "ctnative.host_owner_proved = true" not in restored_text:
                    raise RuntimeError(
                        f"{mode_name}: restoring the live read did not restore ownership"
                    )
            payloads = (
                ("bool", "string", "nullable_string")
                if name in mixed_read_refusals
                else ("bool", "string")
            )
            for payload in payloads:
                forged_name = mode_name + "-forged-" + payload
                forged = args.work / f"{forged_name}.mlir"
                forged.write_text(forge_map_presence(rejected.read_text(), payload))
                failed = methods.refused(
                    args,
                    forged,
                    forged_name + "-stale",
                    fresh,
                    options=options,
                    reason="fingerprint mismatch",
                    admitted=0,
                )
                check_call_preservation(
                    forged.read_text(), failed.read_text(), forged_name + "-stale"
                )
                forged_config = contract(args, forged, forged_name)
                failed = owned.lower(
                    args, forged, forged_name, forged_config, options=options, cleanup=False
                )
                forged_text = methods.census(failed, count, forged_name, admitted=0)
                if "ctnative.host_owner_proved = true" not in forged_text:
                    raise RuntimeError(f"{forged_name}: forged tags changed the carrier proof")
                check_prepared_result_calls(forged_text, forged.read_text(), forged_name)
                rerun = methods.refused(
                    args,
                    failed,
                    forged_name + "-rerun",
                    forged_config,
                    options=options,
                    reason="fingerprint mismatch",
                    admitted=0,
                )
                check_call_preservation(forged_text, rerun.read_text(), forged_name + "-rerun")
    # An implicit undefined return has an exact primitive tag, but that alone
    # does not supply an implemented native Map key. The separate size method
    # keeps the observation numeric so that it cannot cause this refusal.
    js, missing, count = boundary.prepare(
        args,
        "result_missing_return",
        result_sources()["result_bool"][0].replace(
            "get() { return state.has(false); }", "get() { state.size; }"
        ),
    )
    if count != 6:
        raise RuntimeError("result_missing_return: changed source denominator")
    if (
        host.run([node, "-e", boundary.NODE, str(js)]).stdout != "trace=1\n"
        or host.run([str(reference), str(js)]).stdout != "trace=1\n"
    ):
        raise RuntimeError("result_missing_return: Node/interpreter observation mismatch")
    missing_config = contract(args, missing, "result_missing_return")
    missing_output = owned.lower(
        args, missing, "result_missing_return", missing_config, cleanup=False
    )
    text = methods.census(missing_output, 6, "result_missing_return", admitted=0)
    if (
        "ctnative.host_owner_proved = true" not in text
        or "native Map needs supported keys" not in text
        or "!ctnative.map<!ctnative.opt<!ctnative.bottom>" not in text
    ):
        raise RuntimeError("result_missing_return: missing unsupported-result diagnostic")
    # Ownership succeeded, so the established preparation may resolve calls
    # and insert their environment arguments before carrier admission refuses.
    # Check those runtime calls and result edges, not the old source spelling.
    prepared_calls = re.findall(
        r"^\s*(%[-\w.$]+) = ctjs\.call_direct @([-\w.$]+)\(([^\n]+)\) "
        r"\{ctnative\.stored_call = 1 : i32\}",
        text,
        re.M,
    )
    actuals = [arguments.split(", ") for _, _, arguments in prepared_calls]
    if (
        len(source_calls(text)) != len(source_calls(missing.read_text()))
        or [target for _, target, _ in prepared_calls] != ["fn$4", "fn$5", "fn$4", "fn$5", "fn$3"]
        or [len(arguments) for arguments in actuals] != [4, 5, 4, 5, 4]
        or actuals[1][-1] != prepared_calls[0][0]
        or actuals[3][-1] != prepared_calls[2][0]
        or f'ctjs.store_global "trace", {prepared_calls[4][0]}' not in text
    ):
        raise RuntimeError("result_missing_return: prepared calls lost live result order/operands")
    boundary.native(args, shared_ir, "shared-no-manifest", 5)
    methods.refused(
        args,
        shared_ir,
        "shared-no-intrinsic",
        owned.contract(args, shared_ir, "shared-no-intrinsic"),
        admitted=0,
    )
    boundary.native(args, parameter_ir, "parameter-no-manifest", 5)
    methods.refused(
        args,
        parameter_ir,
        "parameter-no-intrinsic",
        owned.contract(args, parameter_ir, "parameter-no-intrinsic"),
        admitted=0,
    )
    boundary.native(args, result_ir, "result-no-manifest", 5)
    methods.refused(
        args,
        result_ir,
        "result-no-intrinsic",
        owned.contract(args, result_ir, "result-no-intrinsic"),
        admitted=0,
    )
    stale_parameter = args.work / "parameter-stale.mlir"
    text, count = re.subn(r'#ctjs\.string<"x">', '#ctjs.string<"y">', parameter_ir.read_text())
    if count != 1:
        raise RuntimeError("parameter-stale: lost the live string actual")
    stale_parameter.write_text(text)
    failed = methods.refused(
        args,
        stale_parameter,
        "parameter-stale",
        parameter_config,
        reason="fingerprint mismatch",
        admitted=0,
    )
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
    failed = methods.refused(
        args, stale_result, "result-stale", result_config, reason="fingerprint mismatch", admitted=0
    )
    check_call_preservation(text, failed.read_text(), "result-stale")
    rerun = owned.lower(args, result_output, "result-rerun", result_config, cleanup=False)
    text = methods.census(rerun, 5, "result-rerun", admitted=5)
    if "ctnative.host_owner_proved = false" not in text or "fingerprint mismatch" not in text:
        raise RuntimeError("result-rerun: prepared result signature reused source authority")
    boundary.native(args, seeded_ir, "seeded-no-manifest", 5)
    methods.refused(
        args,
        seeded_ir,
        "seeded-no-intrinsic",
        owned.contract(args, seeded_ir, "seeded-no-intrinsic"),
        admitted=0,
    )
    stale_seeded = args.work / "seeded-stale.mlir"
    text, count = re.subn(
        r"#ctjs\.number<0>", "#ctjs.number<4611686018427387904>", seeded_ir.read_text()
    )
    if count == 0:
        raise RuntimeError("seeded-stale: lost the live seed and lookup key")
    stale_seeded.write_text(text)
    failed = methods.refused(
        args, stale_seeded, "seeded-stale", seeded_config, reason="fingerprint mismatch", admitted=0
    )
    check_call_preservation(text, failed.read_text(), "seeded-stale")
    rerun = owned.lower(args, seeded_output, "seeded-rerun", seeded_config, cleanup=False)
    text = methods.census(rerun, 5, "seeded-rerun", admitted=5)
    if "ctnative.host_owner_proved = false" not in text or "fingerprint mismatch" not in text:
        raise RuntimeError("seeded-rerun: prepared presence reused the original source authority")
    for name in (
        "seeded_size_two_entries",
        "seeded_size_two_saved_empty",
        "result_seeded_bool",
        "result_seeded_string",
        "result_seeded_string_saved",
        "result_seeded_mixed_contents",
        "result_seeded_join_reseed",
        "result_seeded_bool_string_contents",
        "result_seeded_mixed_string_saved",
        *saved_read_sources(),
        *saved_join_sources(),
        *guarded_saved_sources(),
        *shortcircuit_sources(),
        *nullable_result_sources(),
        *nullable_key_sources(),
        *nullable_payload_sources(),
        *nullable_host_result_sources(),
        *nullable_nested_result_sources(),
    ):
        _, config, output = saved[name]
        functions = RESULT_SIGNATURES[name][2]
        rerun = owned.lower(args, output, name + "-rerun", config, cleanup=False)
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
    for name in (
        (leaf_readback_sources().keys() - LEAF_READBACK_UNOWNED)
        | leaf_absence_sources().keys()
        | leaf_clear_sources().keys()
        | numeric_entry_sources().keys()
        | scalar_global_sources().keys()
        | constant_global_sources().keys()
        | string_field_sources().keys()
        | zero_size_sources().keys()
        | one_size_sources().keys()
        | delete_size_sources().keys()
        | join_size_sources().keys()
        | mutation_size_sources().keys()
    ):
        _, config, output = saved[name]
        rerun = owned.lower(args, output, name + "-rerun", config, cleanup=False)
        text = methods.census(rerun, 5, name + "-rerun", admitted=5)
        if "ctnative.host_owner_proved = false" not in text or "fingerprint mismatch" not in text:
            raise RuntimeError(f"{name}: prepared readback reused the original source authority")
    for name in ("leaf_object_plain", "leaf_object_scalar_writes", "leaf_object_lifetime"):
        ir, config, _ = saved[name]
        rollback += check_budgets(args, ir, config, name, functions=LEAF_OBJECT_FUNCTIONS[name])
    for name in (
        "local_field_get_guarded_checked",
        "historical_object_saved_identity",
        "local_field_readback_lifetime_checked",
    ):
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
    for name in (
        "local_identity_repeated_keys",
        "local_numeric_nested_key",
        "local_add_saved_results",
        "scalar_result_key",
        "scalar_alias",
        "scalar_alias_chain",
        "scalar_constant_only",
        "constant_alias_arithmetic",
        "constant_boolean",
        "constant_boolean_alias_chain",
        "constant_string",
        "constant_string_alias_chain",
        "field_string_read",
        "field_string_lifetime",
        "local_clear_zero_size_key",
        "zero_size_saved_lifetime",
        "zero_size_read_after_write",
        "size_one_saved_lifetime",
        "size_deleted_literal_last",
        "size_deleted_saved_lifetime",
        "joined_delete_disjoint_false",
        "joined_size_saved_lifetime",
        "joined_disjoint_set_false",
        "joined_mutation_saved_lifetime",
    ):
        ir, config, _ = saved[name]
        rollback += check_budgets(args, ir, config, name, functions=5)
    report(positives, shared_refusals, rollback)
