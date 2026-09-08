"""Execute checked published Map methods and live primitive results without the VM."""

# Split out of native-owned-global-maps.py on 2026-09-08: this is its main(),
# verbatim. The docstring above is the one argparse prints, so it stays here.

import argparse
from pathlib import Path
import re
import shutil

from .sources import (
    methods, owned, boundary, host, SOURCE, SHARED, parameter_sources, result_sources,
    seeded_result_sources, key_fact_sources, joined_result_sources, seeded_carrier_refusals,
    RESULT_SIGNATURES, refusal_sources, parameter_refusals, result_refusals,
    seeded_result_refusals, size_result_sources, size_result_refusals,
    payload_result_sources, payload_result_refusals, mixed_result_sources, mixed_result_refusals,
)
from .harness import (
    source_calls, contract, resolve_getter, lifetime, standalone, check_call_preservation,
    forge_map_presence, check_budgets, check_prepared_result_calls,
)


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
    }
    saved = {}
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
    ):
        live_source, _, live_value = positives[name]
        if live_source.count(old) != 1:
            raise RuntimeError(f"{name}: lost the payload observation")
        for index, replacement in enumerate(replacements):
            blind = args.work / f"{name}-payload-blinded-{index}.js"
            blind.write_text(live_source.replace(old, replacement))
            if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == f"trace={live_value}\n":
                raise RuntimeError(f"{name}: payload witness cannot distinguish {replacement}")
    for name, (source, binding, value) in positives.items():
        js, ir, count = boundary.prepare(args, name, source)
        functions = (RESULT_SIGNATURES[name][2] if name in RESULT_SIGNATURES
                     else 6 if name == "shared_three" else 5 if name.startswith("shared") else 4)
        if count != functions:
            raise RuntimeError(f"{name}: lost the {functions}-function source chain")
        if name == "already_resolved":
            ir = resolve_getter(args, ir)
        if name.startswith("legacy_"):
            ir = methods.legacy_marker(args, ir, name)
        expected = f"trace={value}\n"
        if (host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected
                or host.run([str(reference), str(js)]).stdout != expected):
            raise RuntimeError(f"{name}: Node/interpreter source observation mismatch")
        config = contract(args, ir, name, binding)
        original, manifest = ir.read_text(), config.read_text()
        output = owned.lower(args, ir, name, config)
        text = methods.census(output, functions, name, admitted=functions)
        if "ctnative.host_owner_proved = true" not in text:
            raise RuntimeError(f"{name}: lost live owning proof")
        if ir.read_text() != original or config.read_text() != manifest:
            raise RuntimeError(f"{name}: changed supplied source or manifest")
        standalone(args, output, name, value, compilers, nm)
        saved[name] = ir, config, output

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
                 "result_seeded_bool_string_contents", "result_seeded_mixed_string_saved"):
        key_ir, key_config, _ = saved[name]
        rollback += check_budgets(args, key_ir, key_config, name,
                                  functions=RESULT_SIGNATURES[name][2])
    seeded_ir, seeded_config, seeded_output = saved["result_seeded_map_get"]
    rollback += check_budgets(args, seeded_ir, seeded_config, "result_seeded_map_get", functions=5)

    for name, source in refusal_sources().items():
        _, rejected, _ = boundary.prepare(args, name, source)
        fresh = contract(args, rejected, name)
        methods.refused(args, rejected, name, fresh)
    # Primitive ownership does not promise an implemented Map carrier or make
    # a nullable/boolean result a numeric export. These bodies have complete
    # live ownership but still need independent type and carrier proofs.
    for name, body in {
        "nullable_result": "state.set('x', 1); return state.get('missing');",
        "boolean_result": "state.set('x', 1); return state.has('x');",
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
        _, rejected, count = boundary.prepare(args, name, source)
        if count != 5:
            raise RuntimeError(f"{name}: changed seeded-refusal source denominator")
        fresh = contract(args, rejected, name)
        failed = methods.refused(args, rejected, name, fresh, admitted=0)
        check_call_preservation(rejected.read_text(), failed.read_text(), name)
        if name in {"seeded_deleted", "seeded_dynamic_bool_join"}:
            forged_name = name + "-forged"
            forged = args.work / f"{forged_name}.mlir"
            forged.write_text(forge_map_presence(rejected.read_text()))
            forged_config = contract(args, forged, forged_name)
            failed = methods.refused(args, forged, forged_name, forged_config, admitted=0)
            check_call_preservation(forged.read_text(), failed.read_text(), forged_name)
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
    # A definite get tag does not narrow the whole Map's storage schema. The
    # Number/String refusal keeps its complete owner proof and runtime edge.
    for name, (source, value) in seeded_carrier_refusals().items():
        js, rejected, count = boundary.prepare(args, name, source)
        if count != 6:
            raise RuntimeError(f"{name}: changed seeded carrier source denominator")
        if (host.run([node, "-e", boundary.NODE, str(js)]).stdout != f"trace={value}\n"
                or host.run([str(reference), str(js)]).stdout != f"trace={value}\n"):
            raise RuntimeError(f"{name}: Node/interpreter observation mismatch")
        fresh = contract(args, rejected, name)
        output = owned.lower(args, rejected, name, fresh, cleanup=False)
        text = methods.census(output, count, name, admitted=0)
        if "ctnative.host_owner_proved = true" not in text:
            raise RuntimeError(f"{name}: did not independently prove the seeded result owner")
        check_prepared_result_calls(text, rejected.read_text(), name)
        forged_name = name + "-forged"
        forged = args.work / f"{forged_name}.mlir"
        forged.write_text(forge_map_presence(rejected.read_text()))
        failed = methods.refused(args, forged, forged_name + "-stale", fresh,
                                 reason="fingerprint mismatch", admitted=0)
        check_call_preservation(forged.read_text(), failed.read_text(), forged_name + "-stale")
        forged_config = contract(args, forged, forged_name)
        failed = owned.lower(args, forged, forged_name, forged_config, cleanup=False)
        forged_text = methods.census(failed, count, forged_name, admitted=0)
        if "ctnative.host_owner_proved = true" not in forged_text:
            raise RuntimeError(f"{forged_name}: forged presence changed the mixed carrier proof")
        check_prepared_result_calls(forged_text, forged.read_text(), forged_name)
        rerun = methods.refused(args, failed, forged_name + "-rerun", forged_config,
                                reason="fingerprint mismatch", admitted=0)
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
                 "result_seeded_bool_string_contents", "result_seeded_mixed_string_saved"):
        _, config, output = saved[name]
        functions = RESULT_SIGNATURES[name][2]
        rerun = owned.lower(args, output, name + "-rerun", config,
                            cleanup=False)
        text = methods.census(rerun, functions, name + "-rerun", admitted=functions)
        if "ctnative.host_owner_proved = false" not in text or "fingerprint mismatch" not in text:
            raise RuntimeError(f"{name}: prepared Map reused the original source authority")
    print(f"native captured Map ownership: {len(positives)} complete programs (4/4, 5/5, 6/6); "
          "Node/interpreter/GCC/Clang explicit+deduced and Map/table/callable lifetime pass; "
          f"{len(refusal_sources())} source refusals and contract/rerun/budget controls pass; "
          f"two carrier refusals and {len(shared_refusals)} shared-method refusals; "
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
          f"{len(payload_result_refusals())} missing-payload refusals distinguish false/empty; "
          f"{len(mixed_result_sources())} closed mixed Map programs and saved-string lifetime; "
          f"{len(mixed_result_refusals())} mixed deleted-result refusals preserve calls; "
          f"{len(seeded_result_refusals())} seeded proof and "
          f"{len(seeded_carrier_refusals())} seeded carrier refusals; "
          f"{len(rollback)} speculative rollback cutoffs")
