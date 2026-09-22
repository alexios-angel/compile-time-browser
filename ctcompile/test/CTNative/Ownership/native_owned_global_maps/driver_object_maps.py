from .driver_common import (
    LEAF_ABSENCE_PROMOTED_REFUSALS,
    LEAF_ABSENCE_UNOWNED,
    LEAF_CLEAR_UNOWNED,
    LEAF_COMPARISON_CASES,
    LEAF_COMPARISON_REPAIRS,
    LEAF_OBJECT_FUNCTIONS,
    STRING_FIELD_PROMOTED,
    boundary,
    check_call_preservation,
    comparable_provenance,
    comparison_identity_observer_source,
    constant_global_cases,
    contract,
    forge_leaf_evidence,
    forge_map_presence,
    guarded_saved_refusals,
    host,
    leaf_object_observer_source,
    leaf_object_refusals,
    leaf_object_sources,
    leaf_readback_sources,
    methods,
    mixed_result_refusals,
    nullable_host_result_refusals,
    numeric_entry_refusals,
    numeric_node_observer,
    numeric_reference_output,
    object_argument_cases,
    object_argument_sources,
    owned,
    payload_result_refusals,
    primitive_absence_observer_source,
    primitive_absence_sources,
    re,
    saved_join_refusals,
    saved_read_refusals,
    scalar_global_cases,
    scalar_global_refusals,
    seeded_result_refusals,
    shortcircuit_refusals,
    source_calls,
)
from .driver_globals import (
    check_leaf_absence_census,
    check_scalar_global_preparation,
)
from .driver_fields import string_field_method_graph

PRIMITIVE_ABSENCE_CARRIERS = {
    "seeded_cleared",
    "seeded_deleted",
    "seeded_deleted_earlier",
    "result_seeded_false_deleted",
    "result_seeded_mixed_false_deleted",
    "result_seeded_mixed_string_deleted",
    "saved_read_write_deleted",
    "saved_read_write_missing_source",
    "saved_join_deleted_true",
    "guarded_saved_mutated_arm",
    "shortcircuit_mutated_arm",
    "nullable_host_result_deleted",
}


def primitive_absence_carrier_cases():
    cases = {}
    mixed_string = "!ctnative.opt<!ctnative.variant<!ctnative.bool, !ctnative.str<utf8>>>"

    def add(
        name,
        source,
        value,
        old,
        replacement,
        repaired_value,
        calls,
        functions=6,
        carrier=mixed_string,
        *,
        repeated=False,
        size_argument=False,
    ):
        if source.count(old) != 1:
            raise RuntimeError(f"{name}: lost its exact absence repair")
        sequence = (
            [("fn$3", 4), ("fn$4", 5), ("fn$3", 4)]
            if functions == 5
            else (
                [("fn$4", 5 if repeated else 4), ("fn$5", 5)] * (2 if repeated else 1)
                + [("fn$3", 5 if size_argument else 4)]
            )
        )
        dependencies = [(1, 0)] + ([(3, 2)] if repeated else [])
        if size_argument:
            dependencies.append((4, 3))
        cases[name] = dict(
            source=source,
            value=value,
            repair=source.replace(old, replacement),
            old=old,
            replacement=replacement,
            repaired_value=repaired_value,
            calls=calls,
            functions=functions,
            carrier=carrier,
            sequence=sequence,
            dependencies=dependencies,
        )

    for name, calls in (("seeded_deleted", 9), ("seeded_deleted_earlier", 10)):
        add(
            name,
            seeded_result_refusals()[name],
            "undefined",
            "state.delete(0); ",
            "",
            1,
            calls,
            5,
            "!ctnative.map<!ctnative.opt<!ctnative.num<i32>>, !ctnative.num<i32>>",
        )
    add(
        "seeded_cleared",
        seeded_result_refusals()["seeded_cleared"],
        "undefined",
        "state.clear();",
        "state.has(0);",
        1,
        9,
        5,
        "!ctnative.map<!ctnative.opt<!ctnative.num<i32>>, !ctnative.num<i32>>",
    )
    # The repair changes only the mutation; the evaluated method call remains.
    cases["seeded_cleared"]["repair_calls"] = 9
    source, value = payload_result_refusals()["result_seeded_false_deleted"]
    add(
        "result_seeded_false_deleted",
        source,
        value,
        "state.delete(false); ",
        "",
        1,
        10,
        carrier="!ctnative.map<!ctnative.opt<!ctnative.bool>, !ctnative.bool>",
    )
    for name, key, carrier in (
        (
            "result_seeded_mixed_false_deleted",
            "0",
            "!ctnative.map<!ctnative.opt<!ctnative.variant<!ctnative.bool, !ctnative.num<i32>>>, "
            "!ctnative.variant<!ctnative.bool, !ctnative.num<i32>>>",
        ),
        ("result_seeded_mixed_string_deleted", "''", mixed_string),
    ):
        source, value = mixed_result_refusals()[name]
        add(name, source, value, f"state.delete({key}); ", "", 3, 10, carrier=carrier)
    source, value, _, _ = saved_read_refusals()["saved_read_write_deleted"]
    add(
        "saved_read_write_deleted",
        source,
        value,
        "state.delete(false); const result = state.get(false);",
        "const result = state.get(false); state.delete(false);",
        1,
        12,
    )
    source, value, old, replacement = saved_read_refusals()["saved_read_write_missing_source"]
    add("saved_read_write_missing_source", source, value, old, replacement, 1, 14)
    for name, rows, calls in (
        ("saved_join_deleted_true", saved_join_refusals(), 19),
        ("guarded_saved_mutated_arm", guarded_saved_refusals(), 20),
        ("shortcircuit_mutated_arm", shortcircuit_refusals(), 19),
        ("nullable_host_result_deleted", nullable_host_result_refusals(), 16),
    ):
        source, value, old, replacement, repaired_value = rows[name]
        add(
            name,
            source,
            value,
            old,
            replacement,
            repaired_value,
            calls,
            repeated=True,
            size_argument=name == "nullable_host_result_deleted",
        )
    if cases.keys() != PRIMITIVE_ABSENCE_CARRIERS:
        raise RuntimeError("changed the measured primitive absence carrier inventory")
    return cases


def check_primitive_absence_preparation(text, original, case, name):
    calls = re.findall(
        r"^\s*(%[-\w.$]+) = ctjs\.call_direct @([-\w.$]+)\(([^\n]+)\) "
        r"\{ctnative\.stored_call = 1 : i32\}",
        text,
        re.M,
    )
    actuals = [arguments.split(", ") for _, _, arguments in calls]
    if (
        len(source_calls(text)) != case["calls"]
        or len(source_calls(original)) != case["calls"]
        or [(target, len(arguments)) for (_, target, _), arguments in zip(calls, actuals)]
        != case["sequence"]
        or any(
            actuals[consumer][-1] != calls[producer][0]
            for consumer, producer in case["dependencies"]
        )
        or f'ctjs.store_global "trace", {calls[-1][0]}' not in text
    ):
        raise RuntimeError(f"{name}: absence carrier changed prepared result operands/order")
    entry = text.split("\n  }", 1)[0]
    receivers = dict(re.findall(r"(%[-\w.$]+) = ctjs\.get_property (%[-\w.$]+)\[", entry))
    captures = dict(re.findall(r"(%[-\w.$]+) = ctjs\.load_upvalue (%[-\w.$]+)\[0\]", entry))
    for arguments in actuals:
        if (
            receivers.get(arguments[2]) != arguments[0]
            or captures.get(arguments[3]) != arguments[2]
        ):
            raise RuntimeError(f"{name}: changed a current receiver/callee/capture operand")
    for action in ("set", "get", "has", "delete", "clear"):
        source_count = len(re.findall(rf"\bstate\.{action}\(", case["source"]))
        prepared_count = len(
            re.findall(rf'ctjs\.call [^\n]*ctnative\.map_action = "{action}"', text)
        )
        if source_count != prepared_count:
            raise RuntimeError(f"{name}: changed {source_count} live Map.{action} calls")
    for operation in ("scf.if", "scf.yield", "ctjs.create_object", "ctjs.construct"):
        if text.count(operation) != original.count(operation):
            raise RuntimeError(f"{name}: changed live {operation} census")


def check_primitive_absence_carriers(args, node, reference, compilers, nm):
    promoted = {
        "seeded_cleared",
        "seeded_deleted",
        "seeded_deleted_earlier",
        "result_seeded_false_deleted",
    }
    undefined_node = r"""const fs = require('node:fs'), vm = require('node:vm');
const context = vm.createContext({});
vm.runInContext(fs.readFileSync(process.argv[1], 'utf8'), context);
if (vm.runInContext('trace', context) !== undefined) throw new Error('lost Undefined trace');
process.stdout.write('trace=undefined\n');
"""
    for name, case in primitive_absence_carrier_cases().items():
        js, ir, count = boundary.prepare(args, name, case["source"])
        raw = args.work / f"{name}.raw.mlir"
        if (
            count != case["functions"]
            or len(source_calls(raw.read_text())) != case["calls"]
            or len(source_calls(ir.read_text())) != case["calls"]
        ):
            raise RuntimeError(f"{name}: changed the exact source function/call census")
        observer = undefined_node if case["value"] == "undefined" else boundary.NODE
        expected = f'trace={case["value"]}\n'
        if (
            host.run([node, "-e", observer, str(js)]).stdout != expected
            or host.run([str(reference), str(js)]).stdout != expected
        ):
            raise RuntimeError(f"{name}: Node/interpreter absent result mismatch")
        repaired_js, repaired_ir, repaired_count = boundary.prepare(
            args, name + "-restored", case["repair"]
        )
        repaired_expected = f'trace={case["repaired_value"]}\n'
        if (
            repaired_count != count
            or repaired_expected == expected
            or host.run([node, "-e", boundary.NODE, str(repaired_js)]).stdout != repaired_expected
            or host.run([str(reference), str(repaired_js)]).stdout != repaired_expected
        ):
            raise RuntimeError(f"{name}: exact repair lost its independent observation")
        if "repair_calls" in case:
            repaired_raw = args.work / f"{name}-restored.raw.mlir"
            if (
                len(source_calls(repaired_raw.read_text())) != case["repair_calls"]
                or len(source_calls(repaired_ir.read_text())) != case["repair_calls"]
            ):
                raise RuntimeError(f"{name}: exact repair dropped an evaluated source call")
        config = contract(args, ir, name)
        repaired_config = contract(args, repaired_ir, name + "-restored")
        for mode, options in (("default", ""), ("disabled", "optimize=false")):

            def check(input_ir, label, current_config):
                admitted = name in promoted
                output = owned.lower(
                    args, input_ir, label, current_config, options=options, cleanup=admitted
                )
                text = methods.census(output, count, label, admitted=count if admitted else 0)
                if "ctnative.host_owner_proved = true" not in text:
                    raise RuntimeError(f"{label}: lost complete-owner proof")
                if admitted:
                    cpp = host.run([args.translate, "--mlir-to-cpp", str(output)]).stdout
                    storage = (
                        "ctnative::map_storage<ctnative::nullable_scalar, ctnative::js_boolean_t>"
                        if name == "result_seeded_false_deleted"
                        else "ctnative::number_map<ctnative::nullable_scalar>"
                    )
                    if (
                        f"std::shared_ptr<{storage}>" not in cpp
                        or "std::function<ctnative::nullable_scalar()>" not in cpp
                        or "std::function<ctnative::js_num(ctnative::nullable_scalar)>" not in cpp
                    ):
                        raise RuntimeError(f"{label}: lost nullable key/result callable carriers")
                    for action in ("set", "get", "has", "delete", "clear"):
                        if cpp.count(f"ctnative::map_{action}(") != len(
                            re.findall(rf"\bstate\.{action}\(", case["source"])
                        ):
                            raise RuntimeError(f"{label}: changed live Map.{action} calls")
                    return output, comparable_provenance(cpp, input_ir)
                reason = (
                    "mixed native Map read needs independent present payload type evidence"
                    if name == "result_seeded_mixed_false_deleted"
                    else "stored callable result has no supported concrete signature"
                )
                if reason not in boundary.REFUSAL.findall(text):
                    raise RuntimeError(f"{label}: lost independent Map/result carrier refusal")
                check_primitive_absence_preparation(text, input_ir.read_text(), case, label)
                return output, None

            label = name + "-" + mode
            original, expected_cpp = check(ir, label, config)
            repaired = owned.lower(
                args, repaired_ir, label + "-restored", repaired_config, options=options
            )
            repaired_text = methods.census(repaired, count, label + "-restored", admitted=count)
            if "ctnative.host_owner_proved = true" not in repaired_text:
                raise RuntimeError(f"{label}: exact absence repair lost native ownership")
            if name in promoted:
                owned.standalone(args, original, label, expected, compilers, nm)
                owned.standalone(
                    args, repaired, label + "-restored", repaired_expected, compilers, nm
                )
            for payload in ("bool", "string", "nullable_string"):
                forged_name = label + "-forged-" + payload
                forged = args.work / f"{forged_name}.mlir"
                forged.write_text(forge_map_presence(ir.read_text(), payload))
                stale = methods.refused(
                    args,
                    forged,
                    forged_name + "-stale",
                    config,
                    options=options,
                    reason="fingerprint mismatch",
                    admitted=0,
                )
                check_call_preservation(
                    forged.read_text(), stale.read_text(), forged_name + "-stale"
                )
                fresh = contract(args, forged, forged_name)
                checked, checked_cpp = check(forged, forged_name, fresh)
                if checked_cpp != expected_cpp:
                    raise RuntimeError(f"{forged_name}: forged facts changed native absence")
                if name in promoted:
                    rerun = owned.lower(args, checked, forged_name + "-rerun", fresh, cleanup=False)
                    text = methods.census(rerun, count, forged_name + "-rerun", admitted=count)
                    if (
                        "ctnative.host_owner_proved = false" not in text
                        or "fingerprint mismatch" not in text
                    ):
                        raise RuntimeError(f"{forged_name}: emitted source reused owner authority")
                    continue
                rerun = methods.refused(
                    args,
                    checked,
                    forged_name + "-rerun",
                    fresh,
                    options=options,
                    reason="fingerprint mismatch",
                    admitted=0,
                )
                check_call_preservation(
                    checked.read_text(), rerun.read_text(), forged_name + "-rerun"
                )


def check_primitive_absence_observations(args, node, reference):
    source = primitive_absence_sources()["result_seeded_empty_deleted"][0]
    observed = primitive_absence_observer_source(source)
    js = args.work / "primitive-absence-observed.js"
    js.write_text(observed)
    if (
        host.run([node, "-e", boundary.NODE, str(js)]).stdout != "trace=255\n"
        or host.run([str(reference), str(js)]).stdout != "trace=255\n"
    ):
        raise RuntimeError(
            "empty String deletion lost independent Undefined/empty/future key observations"
        )
    for index, (old, replacement) in enumerate(
        (
            ("state.delete('');", "state.has('');"),
            ("state.set('', 'stored'); ", ""),
            ("return state.get('');", "return '';"),
        )
    ):
        if source.count(old) != 1:
            raise RuntimeError("primitive absence mutation lost its exact source")
        js = args.work / f"primitive-absence-blind-{index}.js"
        js.write_text(primitive_absence_observer_source(source.replace(old, replacement)))
        if host.run([node, "-e", boundary.NODE, str(js)]).stdout == "trace=255\n":
            raise RuntimeError(
                "primitive absence observer cannot distinguish a missing mutation/read"
            )


def check_primitive_absence_forgeries(args, saved, node, reference):
    name = "result_seeded_empty_deleted"
    ir, config, output = saved[name]
    expected_cpp = comparable_provenance(
        host.run([args.translate, "--mlir-to-cpp", str(output)]).stdout, ir
    )
    source = primitive_absence_sources()[name][0]
    old = "state.delete('');"
    if source.count(old) != 1:
        raise RuntimeError("empty String absence lost its exact deletion repair")
    js, repaired_ir, count = boundary.prepare(
        args, name + "-restored", source.replace(old, "state.has('');")
    )
    if (
        count != 6
        or len(source_calls(repaired_ir.read_text())) != 10
        or host.run([node, "-e", boundary.NODE, str(js)]).stdout != "trace=1\n"
        or host.run([str(reference), str(js)]).stdout != "trace=1\n"
    ):
        raise RuntimeError("empty String absence repair lost its independent trace/calls")
    repaired_config = contract(args, repaired_ir, name + "-restored")
    for mode, options in (("default", ""), ("disabled", "optimize=false")):
        repaired = owned.lower(
            args, repaired_ir, name + "-" + mode + "-restored", repaired_config, options=options
        )
        methods.census(repaired, 6, name + "-restored", admitted=6)
        for payload in ("bool", "string", "nullable_string"):
            label = name + "-" + mode + "-forged-" + payload
            forged = args.work / f"{label}.mlir"
            forged.write_text(forge_map_presence(ir.read_text(), payload))
            stale = methods.refused(
                args,
                forged,
                label + "-stale",
                config,
                options=options,
                reason="fingerprint mismatch",
                admitted=0,
            )
            check_call_preservation(forged.read_text(), stale.read_text(), label + "-stale")
            fresh = contract(args, forged, label)
            checked = owned.lower(args, forged, label, fresh, options=options)
            text = methods.census(checked, 6, label, admitted=6)
            cpp = host.run([args.translate, "--mlir-to-cpp", str(checked)]).stdout
            if (
                "ctnative.host_owner_proved = true" not in text
                or comparable_provenance(cpp, forged) != expected_cpp
            ):
                raise RuntimeError(
                    f"{label}: forged scalar facts changed native Undefined/key semantics"
                )
    rerun = owned.lower(args, output, name + "-rerun", config, cleanup=False)
    text = methods.census(rerun, 6, name + "-rerun", admitted=6)
    if "ctnative.host_owner_proved = false" not in text or "fingerprint mismatch" not in text:
        raise RuntimeError("prepared primitive absence source reused stale owner authority")


def check_leaf_object_observations(args, node, reference):
    for name, (source, _, _) in leaf_object_sources().items():
        if name in {"leaf_object_number_repair", "leaf_object_string_repair"}:
            continue
        observed, value = leaf_object_observer_source(source, name)
        observed_js = args.work / f"{name}-object-observer.js"
        observed_js.write_text(observed)
        if (
            host.run([node, "-e", boundary.NODE, str(observed_js)]).stdout != f"trace={value}\n"
            or host.run([str(reference), str(observed_js)]).stdout != f"trace={value}\n"
        ):
            raise RuntimeError(f"{name}: independent future object identity/field mismatch")
        mutations = (
            [("const item = {};", "const item = host;")] if name == "leaf_object_plain" else []
        )
        if name in {"leaf_object_number_field", "leaf_object_identity_repair"}:
            mutations = [
                ("const item = {value: 1};", "const item = host;"),
                ("{value: 1}", "{value: 0}"),
            ]
        if name in {"leaf_object_scalar_writes", "leaf_object_alias", "leaf_object_lifetime"}:
            mutations = [
                ("item.value = state.size;", "item.value = 1;"),
                ("item.flag = false;", "item.flag = true;"),
                ("empty: null", "empty: 0"),
                ("absent: void 0", "absent: null"),
            ]
        for index, (old, replacement) in enumerate(mutations):
            if source.count(old) != 1:
                raise RuntimeError(f"{name}: leaf observer mutation lost its unique source origin")
            mutated, _ = leaf_object_observer_source(source.replace(old, replacement), name)
            blind = args.work / f"{name}-object-observer-blind-{index}.js"
            blind.write_text(mutated)
            if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == f"trace={value}\n":
                raise RuntimeError(
                    f"{name}: object identity/field observer cannot distinguish {replacement}"
                )


def check_leaf_object_forgeries(args, saved, names=None):
    for name in names or ("leaf_object_plain", "leaf_object_scalar_writes", "leaf_object_lifetime"):
        ir, config, output = saved[name]
        functions = (
            object_argument_cases()[name]["functions"]
            if name in object_argument_sources()
            else LEAF_OBJECT_FUNCTIONS.get(name, 5)
        )
        expected_cpp = comparable_provenance(
            host.run([args.translate, "--mlir-to-cpp", str(output)]).stdout, ir
        )
        for mode, options in (("default", ""), ("disabled", "optimize=false")):
            for payload in ("bool", "string", "nullable_string"):
                forged_name = name + "-" + mode + "-forged-" + payload
                forged = args.work / f"{forged_name}.mlir"
                forged.write_text(forge_leaf_evidence(ir.read_text(), payload))
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
                if name in scalar_global_cases() or name in constant_global_cases():
                    check_scalar_global_preparation(failed.read_text(), forged.read_text(), name)
                fresh = contract(args, forged, forged_name)
                checked = owned.lower(args, forged, forged_name, fresh, options=options)
                text = methods.census(checked, functions, forged_name, admitted=functions)
                if (
                    "ctnative.host_owner_proved = true" not in text
                    or comparable_provenance(
                        host.run([args.translate, "--mlir-to-cpp", str(checked)]).stdout, forged
                    )
                    != expected_cpp
                ):
                    raise RuntimeError(
                        f"{forged_name}: forged leaf facts changed native owners or fields"
                    )


def check_leaf_object_mixed_key_preparation(text, original, name):
    if len(source_calls(original)) != 8 or len(source_calls(text)) != 8:
        raise RuntimeError(f"{name}: changed the eight-call mixed-key source")
    for operation in ("ctjs.create_object", "ctjs.construct"):
        if text.count(operation) != original.count(operation):
            raise RuntimeError(f"{name}: changed the mixed-key allocation census")
    for function in ("fn$3", "fn$4"):
        if string_field_method_graph(original, function, False) != string_field_method_graph(
            text, function, True
        ):
            raise RuntimeError(f"{name}: changed the source field/Map method body")
    calls = re.findall(
        r"^\s*(%[-\w.$]+) = ctjs\.call_direct @([-\w.$]+)\(([^\n]+)\) "
        r"\{ctnative\.stored_call = 1 : i32\}",
        text,
        re.M,
    )
    actuals = [arguments.split(", ") for _, _, arguments in calls]
    entry = text.split("\n  }", 1)[0]
    if (
        [target for _, target, _ in calls] != ["fn$4"] * 4 + ["fn$3"]
        or list(map(len, actuals)) != [5] * 4 + [4]
        or re.findall(r'ctjs\.store_global "trace", (%[-\w.$]+)', entry) != [calls[-1][0]]
    ):
        raise RuntimeError(f"{name}: changed the mixed-key call order or final size result")
    receivers = dict(re.findall(r"(%[-\w.$]+) = ctjs\.get_property (%[-\w.$]+)\[", entry))
    captures = dict(re.findall(r"(%[-\w.$]+) = ctjs\.load_upvalue (%[-\w.$]+)\[0\]", entry))
    for arguments in actuals:
        if (
            receivers.get(arguments[2]) != arguments[0]
            or captures.get(arguments[3]) != arguments[2]
        ):
            raise RuntimeError(f"{name}: lost the current receiver/callee/Map capture")
    for arguments, key in zip(actuals, ("x", "x", "y")):
        if f'{arguments[-1]} = ctjs.constant #ctjs.string<"{key}">' not in entry:
            raise RuntimeError(f"{name}: changed an earlier String key actual")
    objects = re.findall(r"(%[-\w.$]+) = ctjs\.create_object", entry)
    if len(objects) != 2 or actuals[3][-1] != objects[1]:
        raise RuntimeError(f"{name}: lost the later fresh object key actual")


def check_leaf_object_refusals(args, positives, node, reference, controls=None):
    if controls is None:
        controls = {
            name: row
            for name, row in leaf_object_refusals().items()
            if name
            not in {
                "leaf_object_saved_identity",
                "leaf_object_distinct_identity",
                *LEAF_ABSENCE_PROMOTED_REFUSALS,
                *STRING_FIELD_PROMOTED,
            }
        }
    for name, (source, value, old, replacement, repaired_name, calls) in controls.items():

        def preserved(before, after, label):
            check_call_preservation(before, after, label)
            if name in scalar_global_cases() or name in constant_global_cases():
                check_scalar_global_preparation(after, before, name)

        js, rejected, count = boundary.prepare(args, name, source)
        check_leaf_absence_census(args, rejected, name)
        if count != 5 or len(source_calls(rejected.read_text())) != calls:
            raise RuntimeError(f"{name}: changed the exact leaf-object source census")
        if host.run(
            [node, "-e", numeric_node_observer(value), str(js)]
        ).stdout != f"trace={value}\n" or host.run(
            [str(reference), str(js)]
        ).stdout != numeric_reference_output(
            name, value
        ):
            raise RuntimeError(f"{name}: Node/interpreter leaf-object refusal mismatch")
        if (
            source.count(old) != 1
            or source.replace(old, replacement) != positives[repaired_name][0]
        ):
            raise RuntimeError(f"{name}: repair no longer restores its independently gated source")
        _, restored, restored_count = boundary.prepare(
            args, name + "-restored", source.replace(old, replacement)
        )
        if restored_count != LEAF_OBJECT_FUNCTIONS.get(repaired_name, 5):
            raise RuntimeError(f"{name}: changed repaired source function census")
        config = contract(args, rejected, name)
        restored_config = contract(args, restored, name + "-restored")
        for mode, options in (("default", ""), ("disabled", "optimize=false")):
            mode_name = name + "-" + mode

            def reject(input_ir, label, current_config):
                if name != "leaf_object_later_actual":
                    failed = methods.refused(
                        args, input_ir, label, current_config, options=options, admitted=0
                    )
                    preserved(input_ir.read_text(), failed.read_text(), label)
                    return failed
                # The complete caller owner still has no mixed String/object
                # key carrier. Keep all earlier calls and the later allocation.
                failed = owned.lower(
                    args, input_ir, label, current_config, options=options, cleanup=False
                )
                text = methods.census(failed, count, label, admitted=0)
                carrier = "!ctnative.map<!ctnative.boxed, !ctnative.object_identity>"
                reason = (
                    "native Map needs supported keys and numeric, boolean, closed mixed, "
                    "owning-string, object-identity union or acyclic Map values; inferred "
                    + carrier
                )
                if (
                    "ctnative.host_owner_proved = true" not in text
                    or reason not in boundary.REFUSAL.findall(text)
                ):
                    raise RuntimeError(
                        f"{label}: lost the complete owner or mixed-key carrier refusal"
                    )
                check_leaf_object_mixed_key_preparation(text, input_ir.read_text(), label)
                return failed

            failed = reject(rejected, mode_name, config)
            postdelete = (
                name in LEAF_ABSENCE_UNOWNED
                or name in LEAF_CLEAR_UNOWNED
                or name in numeric_entry_refusals()
                or name in scalar_global_refusals()
            )
            if postdelete and "ctnative.host_owner_proved = false" not in failed.read_text():
                raise RuntimeError(f"{name}: possible absence manufactured a complete host owner")
            repaired = owned.lower(
                args, restored, mode_name + "-restored", restored_config, options=options
            )
            text = methods.census(
                repaired, restored_count, mode_name + "-restored", admitted=restored_count
            )
            if "ctnative.host_owner_proved = true" not in text:
                raise RuntimeError(f"{name}: exact leaf-object repair did not restore ownership")
            for payload in ("bool", "string", "nullable_string"):
                forged_name = mode_name + "-forged-" + payload
                forged = args.work / f"{forged_name}.mlir"
                forged.write_text(forge_leaf_evidence(rejected.read_text(), payload))
                stale = methods.refused(
                    args,
                    forged,
                    forged_name + "-stale",
                    config,
                    options=options,
                    reason="fingerprint mismatch",
                    admitted=0,
                )
                preserved(forged.read_text(), stale.read_text(), forged_name + "-stale")
                fresh = contract(args, forged, forged_name)
                failed = reject(forged, forged_name, fresh)
                if "fingerprint mismatch" in failed.read_text():
                    raise RuntimeError(f"{forged_name}: skipped independent leaf/use reanalysis")
                if postdelete and "ctnative.host_owner_proved = false" not in failed.read_text():
                    raise RuntimeError(f"{forged_name}: forged absence manufactured a host owner")
                rerun = methods.refused(
                    args, failed, forged_name + "-rerun", fresh, options=options, admitted=0
                )
                preserved(failed.read_text(), rerun.read_text(), forged_name + "-rerun")


def check_leaf_readback_carriers(args, positives, node, reference, controls, compilers, nm):
    for name, (source, value, old, replacement, repair, source_call_count) in controls.items():
        js, ir, count = boundary.prepare(args, name, source)
        if count != 5 or len(source_calls(ir.read_text())) != source_call_count:
            raise RuntimeError(f"{name}: changed exact complete-owner carrier source")
        expected = f"trace={value}\n"
        reference_result = host.run([str(reference), str(js)])
        if (
            host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected
            or reference_result.stdout != expected
            or "(1 number, 0 boolean, 0 string, 0 null, 0 undefined)" not in reference_result.stderr
        ):
            raise RuntimeError(f"{name}: complete-owner carrier observation mismatch")
        if source.count(old) != 1 or source.replace(old, replacement) != positives[repair][0]:
            raise RuntimeError(f"{name}: carrier repair changed its exact source")
        config = contract(args, ir, name)

        def check_native(output, input_ir, label):
            text = methods.census(output, 5, label, admitted=5)
            cpp = host.run([args.translate, "--mlir-to-cpp", str(output)]).stdout
            entry = re.search(r"\bmain\(\)\s*\{(.*?)^\}", cpp, re.M | re.S)
            if (
                "ctnative.host_owner_proved = true" not in text
                or "std::function<ctnative::nullable_scalar(ctnative::js_string)>" not in cpp
                or not entry
                or entry[1].count("ctnative::invoke_callable(") != 2
            ):
                raise RuntimeError(
                    f"{label}: lost complete local origins or published nullable calls"
                )
            return comparable_provenance(cpp, input_ir)

        for mode, options in (("default", ""), ("disabled", "optimize=false")):
            mode_name = name + "-" + mode
            output = owned.lower(args, ir, mode_name, config, options=options)
            expected_cpp = check_native(output, ir, mode_name)
            owned.standalone(args, output, mode_name, expected, compilers, nm)
            for payload in ("bool", "string", "nullable_string"):
                label = mode_name + "-forged-" + payload
                forged = args.work / f"{label}.mlir"
                forged.write_text(forge_leaf_evidence(ir.read_text(), payload))
                stale = methods.refused(
                    args,
                    forged,
                    label + "-stale",
                    config,
                    options=options,
                    reason="fingerprint mismatch",
                    admitted=0,
                )
                check_call_preservation(forged.read_text(), stale.read_text(), label + "-stale")
                fresh = contract(args, forged, label)
                checked = owned.lower(args, forged, label, fresh, options=options)
                if check_native(checked, forged, label) != expected_cpp:
                    raise RuntimeError(f"{label}: forged schema facts changed native field output")
                rerun = owned.lower(
                    args, checked, label + "-rerun", fresh, options=options, cleanup=False
                )
                text = methods.census(rerun, count, label + "-rerun", admitted=count)
                if (
                    "ctnative.host_owner_proved = false" not in text
                    or "fingerprint mismatch" not in text
                ):
                    raise RuntimeError(f"{label}: native field rerun reused source authority")


def check_leaf_readback_observations(args, node, reference):
    positives = leaf_readback_sources()
    for name, old, replacement in (
        ("local_identity_saved", "saved === item", "saved !== item"),
        ("local_identity_distinct_stored", "saved === replacement", "saved === item"),
        ("historical_object_saved_identity", "saved === item", "state.get(key) === item"),
        ("local_field_get", "return saved.value;", "return 0;"),
        (
            "local_field_saved_overwrite",
            "return saved === item ? saved.value : 0;",
            "return state.get(key).value;",
        ),
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
        if (
            host.run([node, "-e", boundary.NODE, str(observed)]).stdout != expected
            or host.run([str(reference), str(observed)]).stdout != expected
        ):
            raise RuntimeError(f"{name}: future callable field/identity observation mismatch")
        if name.endswith("_checked"):
            continue
        for index, (old, replacement) in enumerate(
            (
                ("item.value = value;", "item.value = 3;"),
                ("state.delete(key);", "state.has(key);"),
                ("return saved === item ? saved.value : 0;", "return saved === item ? 1 : 0;"),
            )
        ):
            if source.count(old) != 1:
                raise RuntimeError(f"{name}: lost a unique future field observation")
            blind = args.work / f"{name}-future-blinded-{index}.js"
            blind.write_text(source.replace(old, replacement))
            if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == expected:
                raise RuntimeError(
                    f"{name}: future field observation cannot distinguish {replacement}"
                )


def check_comparison_identity_observations(args, node, reference):
    positives = leaf_readback_sources()
    for name, repair in LEAF_COMPARISON_REPAIRS.items():
        source, _, value = positives[name]
        expression = "saved === {value: 1}" if name.startswith("historical_") else "saved === {}"
        if (
            source.count(expression) != 1
            or source.replace(expression, "saved === item") != positives[repair][0]
            or value == positives[repair][2]
        ):
            raise RuntimeError(f"{name}: distinct identity lost its exact saved-object repair")
    for name in LEAF_COMPARISON_CASES:
        source = positives[name][0]
        observed, expected = comparison_identity_observer_source(source, name)
        js = args.work / f"{name}-comparison-future.js"
        js.write_text(observed)
        if (
            host.run([node, "-e", boundary.NODE, str(js)]).stdout != f"trace={expected}\n"
            or host.run([str(reference), str(js)]).stdout != f"trace={expected}\n"
        ):
            raise RuntimeError(
                f"{name}: future strict identity or saved object observation mismatch"
            )
        mutations = [
            ("saved ===", "saved !=="),
            ("const item =", "const item = host; const unused ="),
        ]
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
