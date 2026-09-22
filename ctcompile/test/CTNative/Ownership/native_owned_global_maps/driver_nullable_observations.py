import hashlib
import os
import subprocess

from .driver_common import (
    LEAF_ABSENCE_LIFETIMES,
    LEAF_CLEAR_LIFETIMES,
    NULLABLE_OBSERVATIONS,
    NULLABLE_PAYLOAD_READBACKS,
    NUMERIC_ENTRY_LIFETIMES,
    boundary,
    check_call_preservation,
    check_prepared_result_calls,
    comparable_provenance,
    constant_global_cases,
    contract,
    forge_map_presence,
    host,
    leaf_absence_cases,
    leaf_absence_observer_source,
    leaf_clear_cases,
    leaf_clear_observer_source,
    methods,
    nullable_host_result_refusals,
    nullable_host_result_sources,
    nullable_key_sources,
    nullable_nested_result_refusals,
    nullable_nested_result_sources,
    nullable_observer_source,
    nullable_payload_sources,
    nullable_result_sources,
    numeric_entry_cases,
    numeric_entry_observer_source,
    owned,
    re,
    scalar_global_cases,
    scalar_global_output,
    shortcircuit_refusals,
    shortcircuit_sources,
    source_calls,
)
from .driver_fields import (
    check_string_field_observations,
)
from .driver_map_sizes import (
    check_delete_size_observations,
    check_join_size_observations,
    check_mutation_size_observations,
    check_one_size_observations,
    check_zero_size_observations,
)
from .driver_object_maps import (
    PRIMITIVE_ABSENCE_CARRIERS,
    check_comparison_identity_observations,
    check_leaf_object_observations,
    check_leaf_readback_observations,
    check_primitive_absence_observations,
)
from .harness_objects import instrument_leaf_objects


def check_numeric_entry_observations(args, node, reference):
    cases = {**numeric_entry_cases(), **scalar_global_cases(), **constant_global_cases()}
    for name in NUMERIC_ENTRY_LIFETIMES:
        source = cases[name]["source"]
        observed, value = numeric_entry_observer_source(source, name)
        js = args.work / f"{name}-numeric-future.js"
        js.write_text(observed)
        expected = f"trace={value}\n"
        if host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected or host.run(
            [str(reference), str(js)]
        ).stdout != scalar_global_output(name, value):
            raise RuntimeError(f"{name}: saved numeric field or future branch mismatch")
        mutations = [
            ("return saved.value;", "return 1;"),
            ("value: value", "value: 1"),
            ("state.clear();", "state.has(key);"),
        ]
        if name in {
            "local_numeric_branch_lifetime",
            "scalar_saved_branch_lifetime",
            "scalar_alias_branch_lifetime",
            "constant_branch_lifetime",
            "constant_boolean_branch_lifetime",
            "constant_string_branch_lifetime",
        }:
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
        (
            "local_numeric_sub",
            "host.slot.set('x') - host.slot.set('y')",
            "host.slot.set('y') - host.slot.set('x')",
        ),
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
            raise RuntimeError(
                f"{name}: numeric observation cannot distinguish reordered or reread results"
            )


def check_leaf_absence_observations(args, node, reference):
    cases = leaf_absence_cases()
    for name in LEAF_ABSENCE_LIFETIMES:
        source = cases[name]["source"]
        observed, value = leaf_absence_observer_source(source, name)
        js = args.work / f"{name}-absence-future.js"
        js.write_text(observed)
        expected = f"trace={value}\n"
        if (
            host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected
            or host.run([str(reference), str(js)]).stdout != expected
        ):
            raise RuntimeError(f"{name}: saved absence or future branch observation mismatch")
        mutations = [("state.delete(key);", "state.has(key);"), ("value: 1", "value: 2")]
        if name == "local_absence_saved_undefined":
            mutations += [
                ("return saved ===", "return state.get(key) ==="),
                ("state.set(key, item); return saved", "state.has(key); return saved"),
            ]
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
        if (
            host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected
            or host.run([str(reference), str(js)]).stdout != expected
        ):
            raise RuntimeError(f"{name}: saved value or future clear branch observation mismatch")
        mutations = [("state.clear();", "state.has(key);"), ("value: 1", "value: 2")]
        if name == "local_clear_saved_field":
            mutations.append(("return saved.value;", "return state.get(key).value;"))
        elif name == "local_clear_saved_undefined":
            mutations += [
                ("return saved ===", "return state.get(key) ==="),
                ("state.set(key, item); return saved", "state.has(key); return saved"),
            ]
        else:
            mutations.append(("if (flag) { state.clear(); }", "if (flag) { state.has(key); }"))
        for index, (old, replacement) in enumerate(mutations):
            if old not in source:
                raise RuntimeError(f"{name}: lost independent clear mutation {old}")
            blind, _ = leaf_clear_observer_source(source.replace(old, replacement), name)
            js = args.work / f"{name}-clear-blind-{index}.js"
            js.write_text(blind)
            throws = replacement == "return state.get(key).value;"
            if (
                host.run([node, "-e", boundary.NODE, str(js)], success=not throws).stdout
                == expected
            ):
                raise RuntimeError(f"{name}: clear observer cannot distinguish {replacement}")


def nullable_foreign_observer(source):
    return source + """
(function() {
    const get = host.slot.get, set = host.slot.set, size = host.slot.size;
    host = {};
    let ok = size() === 2;
    for (const key of [null, undefined, '', 'null', 'undefined']) {
        if (set(key) !== undefined) { ok = false; }
    }
    ok = ok && size() === 6;
    for (let i = 0; i < 128; ++i) {
        const before = size(), key = 'caller-' + i;
        const result = set(key);
        if (result !== undefined || set(result) !== undefined || size() !== before + 1 ||
            get(false) !== 'future' || get(true) !== null) { ok = false; }
    }
    trace = ok && size() === 134 ? 1 : 0;
})();
"""


def nullable_foreign_lifetime_cpp(cpp):
    return instrument_leaf_objects(cpp, allocations=0) + r"""
int main() {
    using Key = ctnative::nullable_string;
    using Result = ctnative::nullable_scalar;
    using Map = ctnative::map_storage<Key, std::variant<ctnative::js_boolean_t, Key>>;
    const auto undefined = [](Result result) { return result.tag == Result::kind::undefined; };
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 4) { return 285; }
    auto owner = g_host;
    auto table = owner->slot;
    auto get = table->m_get;
    auto set = table->m_set;
    auto size = table->m_size;
    static_assert(std::is_same_v<decltype(get), std::function<Key(ctnative::js_boolean_t)>>);
    static_assert(std::is_same_v<decltype(set), std::function<Result(Key)>>);
    static_assert(std::is_same_v<decltype(size), std::function<ctnative::js_num()>>);
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); owner.reset(); table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() || size().value() != 2) { return 286; }
    auto state = std::const_pointer_cast<Map>(
        std::static_pointer_cast<const Map>(ctn_test_maps[0].lock()));
    if (!state) { return 287; }
    Key null_key;
    null_key.tag = Key::kind::null_value;
    for (const auto & key : {null_key, Key{}, Key{std::string{}},
                            Key{std::string{"null"}}, Key{std::string{"undefined"}}}) {
        if (!undefined(set(key))) { return 288; }
        const auto stored = std::get<Key>(state->at(key));
        if (stored.tag != key.tag || stored.value != key.value) { return 289; }
    }
    if (size().value() != 6) { return 290; }
    for (int index = 0; index < 128; ++index) {
        const auto before = size().value();
        const auto allocations = ctn_test_maps.size();
        const std::string text = "caller-" + std::to_string(index);
        auto key = Key{text};
        const auto saved = set(key);
        key.value.assign(text.size(), 'x');
        if (!undefined(saved) || !undefined(set(ctnative::to_nullable_string(saved))) ||
            size().value() != before + 1 || std::get<Key>(state->at(Key{text})).value != text ||
            get(ctnative::js_boolean_t{false}).value != "future" || get(ctnative::js_boolean_t{true}).tag != Key::kind::null_value ||
            ctn_test_maps.size() != allocations + 2 || !ctn_test_maps.back().expired()) {
            return 291;
        }
    }
    if (size().value() != 134) { return 292; }
    for (std::size_t index = 1; index < ctn_test_maps.size(); ++index) {
        if (!ctn_test_maps[index].expired()) { return 293; }
    }
    const auto fresh = ctn_test_maps.size();
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != fresh + 4 ||
        ctn_test_maps[0].lock() == ctn_test_maps[fresh].lock() ||
        size().value() != 134 || g_host->slot->m_size().value() != 2) { return 294; }
    state.reset(); get = {}; set = {};
    if (ctn_test_maps[0].expired()) { return 295; }
    size = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[fresh].expired()) { return 296; }
    g_host.reset();
    for (const auto & map : ctn_test_maps) {
        if (!map.expired()) { return 297; }
    }
    return 0;
}
"""


def check_nullable_foreign_empty(args, output, source, node, reference, compilers, nm):
    name = "nullable_nested_foreign"
    if hashlib.sha256(source.encode()).hexdigest() != (
        "85aa6fa6356df6be5742dca004da0da8b5db5de4c816f8371d510f89974dc759"
    ):
        raise RuntimeError("changed the historical foreign-empty source")
    observed = args.work / f"{name}-future.js"
    observed.write_text(nullable_foreign_observer(source))
    result = host.run([str(reference), str(observed)])
    if (
        host.run([node, "-e", boundary.NODE, str(observed)]).stdout != "trace=1\n"
        or result.stdout != "trace=1\n"
        or "(1 number, 0 boolean, 0 string, 0 null, 0 undefined)" not in result.stderr
    ):
        raise RuntimeError("foreign empty Map: future Undefined result observation changed")
    for index, (old, replacement) in enumerate(
        (
            ("return new Map().get(key);", "return state.get(key);"),
            ("return new Map().get(key);", "return null;"),
            ("return new Map().get(key);", "return '';"),
            ("state.set(key, key);", "state.has(key);"),
        )
    ):
        assert source.count(old) == 1
        blind = args.work / f"{name}-future-blinded-{index}.js"
        blind.write_text(nullable_foreign_observer(source.replace(old, replacement)))
        if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == "trace=1\n":
            raise RuntimeError(f"foreign empty Map: observer cannot distinguish {replacement}")
    deduced = args.work / f"{name}.deduced.mlir"
    host.run([args.opt, str(output), "--ctnative-print-deduced", "-o", str(deduced)])
    for mode, ir in (("explicit", output), ("deduced", deduced)):
        cpp = host.run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
        if owned.VM.search(cpp):
            raise RuntimeError("foreign empty Map: generated a VM dependency")
        generated = args.work / f"{name}.{mode}.cpp"
        generated.write_text(cpp)
        native = args.work / f"{name}.{mode}.lifetime.cpp"
        native.write_text(nullable_foreign_lifetime_cpp(cpp))
        for index, compiler in enumerate(compilers):
            binary = native.with_suffix(f".{index}").resolve()
            host.run([compiler, *owned.FLAGS, str(native), "-o", str(binary)])
            if (
                owned.VM.search(host.run([nm, "-C", str(binary)]).stdout)
                or host.run([str(binary)]).stdout != "trace=2\n" * 2
            ):
                raise RuntimeError("foreign empty Map: standalone lifetime/result mismatch")
        binary = native.with_suffix(".sanitized").resolve()
        host.run(
            [
                compilers[1],
                *owned.FLAGS,
                "-O1",
                "-g",
                "-fno-omit-frame-pointer",
                "-fsanitize=address,undefined",
                "-fsanitize-address-use-after-scope",
                str(native),
                "-o",
                str(binary),
            ]
        )
        result = subprocess.run(
            [str(binary)],
            capture_output=True,
            text=True,
            timeout=60,
            env={
                **os.environ,
                "ASAN_OPTIONS": "detect_stack_use_after_return=1:detect_leaks=1",
                "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1",
            },
        )
        if result.returncode or result.stdout != "trace=2\n" * 2 or result.stderr:
            raise RuntimeError(
                f"foreign empty Map: sanitized lifetime failed\n"
                f"{result.returncode}: {result.stdout}{result.stderr}"
            )


def check_nullable_host_result_refusals(
    args, positives, node, reference, compilers, nm, *, names=None
):
    controls = {
        name: (
            *row[:4],
            "nullable_host_result_both" if name.endswith("deleted") else "nullable_host_result",
            16 if name.endswith(("deleted", "aliasing")) else 15,
        )
        for name, row in nullable_host_result_refusals().items()
    }
    controls.update(nullable_nested_result_refusals())
    controls = {
        name: row for name, row in controls.items() if name not in PRIMITIVE_ABSENCE_CARRIERS
    }
    if names is not None:
        controls = {name: row for name, row in controls.items() if name in names}
    for name, (source, value, old, replacement, repaired_name, expected_calls) in controls.items():
        js, rejected, count = boundary.prepare(args, name, source)
        if count != 6 or len(source_calls(rejected.read_text())) != expected_calls:
            raise RuntimeError(f"{name}: changed the host-result source census")
        expected = f"trace={value}\n"
        reference_expected = expected + ("unknownResult=null\n" if name.endswith("unknown") else "")
        if (
            host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected
            or host.run([str(reference), str(js)]).stdout != reference_expected
        ):
            raise RuntimeError(f"{name}: Node/interpreter host-result observation mismatch")
        restored_source = source.removeprefix("var unknownResult = null;\n").replace(
            old, replacement
        )
        restored_value = positives[repaired_name][2]
        if source.count(old) != 1 or restored_source != positives[repaired_name][0]:
            raise RuntimeError(f"{name}: repair no longer restores the independently gated source")
        restored_js, restored_ir, restored_count = boundary.prepare(
            args, name + "-restored", restored_source
        )
        if (
            restored_count != 6
            or value == restored_value
            or host.run([node, "-e", boundary.NODE, str(restored_js)]).stdout
            != f"trace={restored_value}\n"
            or host.run([str(reference), str(restored_js)]).stdout != f"trace={restored_value}\n"
        ):
            raise RuntimeError(f"{name}: missing the discriminating repaired observation")
        fresh = contract(args, rejected, name)
        restored_config = contract(args, restored_ir, name + "-restored")
        promoted = name == "nullable_nested_foreign"
        promoted_output = None
        for mode, options in (("default", ""), ("disabled", "optimize=false")):
            mode_name = name + "-" + mode

            def check_live(input_ir, label, current_config):
                if promoted:
                    # Its distinct fresh Map is empty, independently of the
                    # captured Map write. Keep the original Undefined result.
                    checked = owned.lower(args, input_ir, label, current_config, options=options)
                    text = methods.census(checked, 6, label, admitted=6)
                    if "ctnative.host_owner_proved = true" not in text:
                        raise RuntimeError(f"{label}: fresh empty Map lost ownership")
                    return checked
                if name != "nullable_nested_sibling":
                    failed = methods.refused(
                        args, input_ir, label, current_config, options=options, admitted=0
                    )
                    check_call_preservation(input_ir.read_text(), failed.read_text(), label)
                    return failed
                # The historical leaf-writing sibling now has a complete
                # owner. Its Object/String Map still has no native carrier.
                failed = owned.lower(
                    args, input_ir, label, current_config, options=options, cleanup=False
                )
                text = methods.census(failed, 6, label, admitted=0)
                if (
                    "ctnative.host_owner_proved = true" not in text
                    or "!ctnative.map<!ctnative.opt<!ctnative.str<utf8>>, !ctnative.boxed>"
                    not in text
                    or re.search(r"\bemitc\.func @main\(", text)
                ):
                    raise RuntimeError(
                        f"{label}: lost the complete owner or Object/String carrier refusal"
                    )
                check_prepared_result_calls(text, input_ir.read_text(), name)
                return failed

            failed = check_live(rejected, mode_name, fresh)
            if promoted:
                expected_cpp = comparable_provenance(
                    host.run([args.translate, "--mlir-to-cpp", str(failed)]).stdout, rejected
                )
                if promoted_output and failed.read_text() != promoted_output.read_text():
                    raise RuntimeError("foreign empty Map: proof depends on optimization policy")
                promoted_output = failed
            repaired = owned.lower(
                args, restored_ir, mode_name + "-restored", restored_config, options=options
            )
            repaired_text = methods.census(repaired, 6, mode_name + "-restored", admitted=6)
            if "ctnative.host_owner_proved = true" not in repaired_text:
                raise RuntimeError(f"{name}: repaired nullable host result lost ownership")
            for payload in ("bool", "string", "nullable_string"):
                forged_name = mode_name + "-forged-" + payload
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
                failed = check_live(forged, forged_name, forged_config)
                if "fingerprint mismatch" in failed.read_text():
                    raise RuntimeError(
                        f"{forged_name}: skipped independent host payload reanalysis"
                    )
                if promoted:
                    if (
                        comparable_provenance(
                            host.run([args.translate, "--mlir-to-cpp", str(failed)]).stdout, forged
                        )
                        != expected_cpp
                    ):
                        raise RuntimeError(f"{forged_name}: forged tag changed empty Map output")
                    rerun = owned.lower(
                        args,
                        failed,
                        forged_name + "-rerun",
                        forged_config,
                        options=options,
                        cleanup=False,
                    )
                    text = methods.census(rerun, 6, forged_name + "-rerun", admitted=6)
                    if (
                        "ctnative.host_owner_proved = false" not in text
                        or "fingerprint mismatch" not in text
                    ):
                        raise RuntimeError(f"{forged_name}: emitted result reused source authority")
                    continue
                rerun = methods.refused(
                    args, failed, forged_name + "-rerun", forged_config, options=options, admitted=0
                )
                check_call_preservation(
                    failed.read_text(), rerun.read_text(), forged_name + "-rerun"
                )
        if promoted:
            check_nullable_foreign_empty(
                args, promoted_output, source, node, reference, compilers, nm
            )


def check_shortcircuit_nullable_refusal(args, node, reference):
    # Host truthiness proves this historical result is String/Null. The wider
    # optional Bool/String temporary now has a scalar carrier, but its Map
    # write still lacks an independently proved owning alternative.
    name = "shortcircuit_nullable"
    source, value, old, replacement, restored_value = shortcircuit_refusals()[name]
    js, rejected, count = boundary.prepare(args, name, source)
    if count != 6 or len(source_calls(rejected.read_text())) != 17:
        raise RuntimeError(f"{name}: changed the original seventeen-call source census")
    if (
        host.run([node, "-e", boundary.NODE, str(js)]).stdout != f"trace={value}\n"
        or host.run([str(reference), str(js)]).stdout != f"trace={value}\n"
    ):
        raise RuntimeError(f"{name}: Node/interpreter original observation mismatch")
    restored_source = source.replace(old, replacement)
    if (
        source.count(old) != 1
        or restored_source != shortcircuit_sources()["shortcircuit_same_tag"][0]
    ):
        raise RuntimeError(f"{name}: repair no longer restores the independently gated source")
    restored_js, restored_ir, restored_count = boundary.prepare(
        args, name + "-restored", restored_source
    )
    if (
        restored_count != 6
        or value == restored_value
        or host.run([node, "-e", boundary.NODE, str(restored_js)]).stdout
        != f"trace={restored_value}\n"
        or host.run([str(reference), str(restored_js)]).stdout != f"trace={restored_value}\n"
    ):
        raise RuntimeError(f"{name}: lost the discriminating original/repaired observations")
    fresh = contract(args, rejected, name)
    restored_config = contract(args, restored_ir, name + "-restored")

    def check_prepared(output, original, label):
        text = methods.census(output, 6, label, admitted=0)
        diagnostic = (
            "nullable native Map write needs a proved scalar, String or absent "
            "alternative with an owning carrier"
        )
        if (
            "ctnative.host_owner_proved = true" not in text
            or diagnostic not in text
            or re.search(r"\bemitc\.func @main\(", text)
        ):
            raise RuntimeError(f"{label}: missing complete owner or nullable Map write refusal")
        calls = re.findall(
            r"^\s*(%[-\w.$]+) = ctjs\.call_direct @([-\w.$]+)\(([^\n]+)\) "
            r"\{ctnative\.stored_call = 1 : i32\}",
            text,
            re.M,
        )
        actuals = [arguments.split(", ") for _, _, arguments in calls]
        if (
            len(source_calls(text)) != len(source_calls(original))
            or [target for _, target, _ in calls] != ["fn$4", "fn$5", "fn$4", "fn$5", "fn$3"]
            or [len(arguments) for arguments in actuals] != [5, 5, 5, 5, 4]
            or actuals[1][-1] != calls[0][0]
            or actuals[3][-1] != calls[2][0]
            or f'ctjs.store_global "trace", {calls[-1][0]}' not in text
        ):
            raise RuntimeError(
                f"{label}: carrier refusal changed prepared producer/consumer operands"
            )
        return text

    for mode, options in (("default", ""), ("disabled", "optimize=false")):
        mode_name = name + "-" + mode
        output = owned.lower(args, rejected, mode_name, fresh, options=options, cleanup=False)
        check_prepared(output, rejected.read_text(), mode_name)
        restored = owned.lower(
            args, restored_ir, mode_name + "-restored", restored_config, options=options
        )
        restored_text = methods.census(restored, 6, mode_name + "-restored", admitted=6)
        if "ctnative.host_owner_proved = true" not in restored_text:
            raise RuntimeError(
                f"{mode_name}: repaired temporary did not restore complete ownership"
            )
        for payload in ("bool", "string", "nullable_string"):
            forged_name = mode_name + "-forged-" + payload
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
            check_call_preservation(forged.read_text(), stale.read_text(), forged_name + "-stale")
            forged_config = contract(args, forged, forged_name)
            checked = owned.lower(
                args, forged, forged_name, forged_config, options=options, cleanup=False
            )
            checked_text = check_prepared(checked, forged.read_text(), forged_name)
            rerun = methods.refused(
                args,
                checked,
                forged_name + "-rerun",
                forged_config,
                options=options,
                reason="fingerprint mismatch",
                admitted=0,
            )
            check_call_preservation(checked_text, rerun.read_text(), forged_name + "-rerun")
