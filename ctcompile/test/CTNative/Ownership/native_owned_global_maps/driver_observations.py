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
    using Map = ctnative::map_storage<Key, std::variant<bool, Key>>;
    const auto undefined = [](Result result) { return result.tag == Result::kind::undefined; };
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 4) { return 285; }
    auto owner = g_host;
    auto table = owner->slot;
    auto get = table->m_get;
    auto set = table->m_set;
    auto size = table->m_size;
    static_assert(std::is_same_v<decltype(get), std::function<Key(bool)>>);
    static_assert(std::is_same_v<decltype(set), std::function<Result(Key)>>);
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset(); owner.reset(); table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() || size() != 2) { return 286; }
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
    if (size() != 6) { return 290; }
    for (int index = 0; index < 128; ++index) {
        const auto before = size();
        const auto allocations = ctn_test_maps.size();
        const std::string text = "caller-" + std::to_string(index);
        auto key = Key{text};
        const auto saved = set(key);
        key.value.assign(text.size(), 'x');
        if (!undefined(saved) || !undefined(set(ctnative::to_nullable_string(saved))) ||
            size() != before + 1 || std::get<Key>(state->at(Key{text})).value != text ||
            get(false).value != "future" || get(true).tag != Key::kind::null_value ||
            ctn_test_maps.size() != allocations + 2 || !ctn_test_maps.back().expired()) {
            return 291;
        }
    }
    if (size() != 134) { return 292; }
    for (std::size_t index = 1; index < ctn_test_maps.size(); ++index) {
        if (!ctn_test_maps[index].expired()) { return 293; }
    }
    const auto fresh = ctn_test_maps.size();
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != fresh + 4 ||
        ctn_test_maps[0].lock() == ctn_test_maps[fresh].lock() ||
        size() != 134 || g_host->slot->m_size() != 2) { return 294; }
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
    # Host truthiness now proves this historical result is String/Null. Its
    # earlier &&/|| temporary still needs an unsupported optional Bool/String
    # carrier, independently of the complete owner and published result proof.
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
            "a value of type !ctnative.opt<!ctnative.variant<!ctnative.bool, "
            "!ctnative.str<utf8>>> from `scf.if`"
        )
        if (
            "ctnative.host_owner_proved = true" not in text
            or diagnostic not in text
            or re.search(r"\bemitc\.func @main\(", text)
        ):
            raise RuntimeError(f"{label}: missing complete owner or optional intermediate refusal")
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


def check_source_observations(args, node, reference, positives):
    check_leaf_object_observations(args, node, reference)
    check_leaf_readback_observations(args, node, reference)
    check_comparison_identity_observations(args, node, reference)
    check_leaf_absence_observations(args, node, reference)
    check_primitive_absence_observations(args, node, reference)
    check_leaf_clear_observations(args, node, reference)
    check_numeric_entry_observations(args, node, reference)
    check_string_field_observations(args, node, reference)
    check_zero_size_observations(args, node, reference)
    check_one_size_observations(args, node, reference)
    check_delete_size_observations(args, node, reference)
    check_join_size_observations(args, node, reference)
    check_mutation_size_observations(args, node, reference)
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
        (
            "result_seeded_string_saved",
            "return saved;",
            ("return state.get('seed');", "return 'changed';"),
        ),
        ("result_seeded_mixed_false_zero", "state.set(false, 1);", ("state.set(0, 1);",)),
        (
            "result_seeded_mixed_false",
            "return state.get(true);",
            ("return 0;", "return undefined;"),
        ),
        ("result_seeded_mixed_empty_key", "state.set(false, false);", ("state.set('', false);",)),
        (
            "result_seeded_mixed_string_saved",
            "return saved;",
            ("return state.get('seed');", "return false;"),
        ),
        (
            "saved_read_write",
            "state.set(false, saved);",
            ("state.has(false);", "state.set(false, true);"),
        ),
        ("saved_read_write", "state.delete(false);", ("state.has(false);",)),
        (
            "saved_read_write_false",
            "state.set('', saved);",
            ("state.has('');", "state.set('', state.get(false));"),
        ),
        (
            "saved_read_write_number",
            "state.set(false, saved);",
            ("state.has(false);", "state.set(false, state.get(1));"),
        ),
        ("saved_read_write_repeated", "state.set(false, saved);", ("state.has(false);",)),
        ("saved_read_write_string_saved", "state.set(false, saved);", ("state.has(false);",)),
        ("saved_read_write_string_saved", "return result;", ("return state.get(false);",)),
        ("saved_read_write_string_saved", "state.delete(false);", ("state.has(false);",)),
        (
            "saved_read_write_wrong_tag",
            "state.set(false, true); const result",
            ("state.set(false, saved); const result",),
        ),
        ("saved_read_write_overwritten", "state.set(false, true); const result", ("const result",)),
        ("saved_join", "flag ? state.get('other') : state.get('')", ("state.get('')",)),
        (
            "saved_join_distinct",
            "flag ? state.get('other') : state.get('')",
            ("state.get('')", "state.get('other')"),
        ),
        (
            "saved_join_bool",
            "flag ? state.get('other') : state.get('')",
            ("state.get('')", "state.get('other')"),
        ),
        (
            "saved_join_bool",
            "state.set('temp', saved);",
            ("state.has('temp');", "state.set('temp', state.get(''));"),
        ),
        (
            "saved_join_number",
            "flag ? state.get(1) : state.get(0)",
            ("state.get(0)", "state.get(1)"),
        ),
        (
            "saved_join_number",
            "state.set(false, saved);",
            ("state.has(false);", "state.set(false, state.get(0));"),
        ),
        ("saved_join_string_saved", "state.set(false, saved);", ("state.has(false);",)),
        ("saved_join_string_saved", "return result;", ("return state.get(false);",)),
        ("saved_join_string_saved", "state.delete(false);", ("state.has(false);",)),
        ("saved_join_string_saved", "state.delete('');", ("state.has('');",)),
        ("guarded_saved_read", "if (flag) { state.delete('other'); }", ("state.has('other');",)),
        (
            "guarded_saved_read",
            "state.has('other') ? state.get('other') : state.get('')",
            ("state.get('')",),
        ),
        ("guarded_saved_read", "state.delete(false);", ("state.has(false);",)),
        ("guarded_saved_bool", "if (flag) { state.delete('other'); }", ("state.has('other');",)),
        (
            "guarded_saved_bool",
            "state.has('other') ? state.get('other') : state.get('')",
            ("state.get('')",),
        ),
        (
            "guarded_saved_bool",
            "state.set('temp', saved);",
            ("state.has('temp');", "state.set('temp', state.get(''));"),
        ),
        ("guarded_saved_number", "if (flag) { state.delete(1); }", ("state.has(1);",)),
        ("guarded_saved_number", "state.has(1) ? state.get(1) : state.get(0)", ("state.get(0)",)),
        (
            "guarded_saved_number",
            "state.set(false, saved);",
            ("state.has(false);", "state.set(false, state.get(0));"),
        ),
        ("guarded_saved_string_saved", "state.set(false, saved);", ("state.has(false);",)),
        ("guarded_saved_string_saved", "return result;", ("return state.get(false);",)),
        ("guarded_saved_string_saved", "state.delete(false);", ("state.has(false);",)),
        ("guarded_saved_string_saved", "state.delete('');", ("state.has('');",)),
        (
            "guarded_saved_string_saved",
            "state.set('other', false); state.delete('other');",
            ("state.set('other', false); state.has('other');",),
        ),
        ("shortcircuit_same_tag", "if (flag) { state.delete('other'); }", ("state.has('other');",)),
        ("shortcircuit_same_tag", "state.delete(false);", ("state.has(false);",)),
        (
            "shortcircuit_distinct",
            "(state.has('other') && state.get('other')) || state.get('')",
            ("state.get('')", "'future'"),
        ),
        ("shortcircuit_distinct", "state.set(false, saved);", ("state.has(false);",)),
        (
            "shortcircuit_empty_string",
            "(state.has('other') && state.get('other')) || state.get('')",
            ("state.has('other') ? state.get('other') : state.get('')", "state.get('other')"),
        ),
        (
            "shortcircuit_bool",
            "(state.has('other') && state.get('other')) || state.get('')",
            ("state.get('')", "true"),
        ),
        (
            "shortcircuit_bool",
            "state.set('temp', saved);",
            ("state.has('temp');", "state.set('temp', state.get(''));"),
        ),
        (
            "shortcircuit_false",
            "(state.has('other') && state.get('other')) || state.get('')",
            (
                "state.has('other') ? state.get('other') : state.get('')",
                "state.has('other') && state.get('other')",
            ),
        ),
        (
            "shortcircuit_number",
            "(state.has(1) && state.get(1)) || state.get(0)",
            ("state.get(0)", "3"),
        ),
        (
            "shortcircuit_number",
            "state.set(false, saved);",
            ("state.has(false);", "state.set(false, state.get(0));"),
        ),
        (
            "shortcircuit_zero",
            "(state.has(1) && state.get(1)) || state.get(0)",
            ("state.has(1) ? state.get(1) : state.get(0)", "state.get(1)"),
        ),
        ("shortcircuit_string_saved", "state.set(false, saved);", ("state.has(false);",)),
        ("shortcircuit_string_saved", "return result;", ("return state.get(false);",)),
        ("shortcircuit_string_saved", "state.delete(false);", ("state.has(false);",)),
        ("shortcircuit_string_saved", "state.delete('');", ("state.has('');",)),
        (
            "shortcircuit_string_saved",
            "state.set('other', false); state.delete('other');",
            ("state.set('other', false); state.has('other');",),
        ),
        ("nullable_key_identity", "state.set(key, true);", ("state.set(key || 'missing', true);",)),
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
        (
            "nullable_host_result",
            "return state.get(key);",
            ("return null;", "return void 0;", "return '';", "return true;"),
        ),
        ("nullable_host_result_identity", "host.slot.set(void 0);", ("host.slot.set(null);",)),
        ("nullable_host_result_identity", "host.slot.set('');", ("host.slot.set(null);",)),
        (
            "nullable_host_result_conditional",
            "if (key) { state.set(key, 'selected'); }",
            ("state.has(key);",),
        ),
        ("nullable_host_result_saved", "state.delete('seed');", ("state.has('seed');",)),
        (
            "nullable_nested_result_same",
            "return state.get(key);",
            ("return null;", "return void 0;", "return '';", "return true;"),
        ),
        ("nullable_nested_result_identity", "host.slot.set(void 0);", ("host.slot.set(null);",)),
        ("nullable_nested_result_identity", "host.slot.set('');", ("host.slot.set(null);",)),
        (
            "nullable_nested_result_identity",
            "host.slot.set('later');",
            ("host.slot.set('future');",),
        ),
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
        **nullable_result_sources(),
        **nullable_key_sources(),
        **nullable_payload_sources(),
        **nullable_host_result_sources(),
        **nullable_nested_result_sources(),
    }.items():
        identity = args.work / f"{name}-identity.js"
        identity.write_text(nullable_observer_source(source, name))
        observations = len(NULLABLE_OBSERVATIONS[name]) + len(
            NULLABLE_PAYLOAD_READBACKS.get(name, ())
        )
        expected = f"trace={(1 << observations) - 1}\n"
        if (
            host.run([node, "-e", boundary.NODE, str(identity)]).stdout != expected
            or host.run([str(reference), str(identity)]).stdout != expected
        ):
            raise RuntimeError(f"{name}: Node/interpreter String/null/undefined identity mismatch")
        original_return = (
            "return result ? result : null;"
            if name == "nullable_ternary"
            else (
                "return result || (void 0);"
                if name == "nullable_undefined"
                else (
                    "return result || (nullish ? null : (void 0));"
                    if name == "nullable_threeway"
                    else "return result || null;"
                )
            )
        )
        replacements = ("return result;", "return null;", "return undefined;")
        if name == "nullable_empty":
            replacements = ("return result;", "return undefined;")
        if name == "nullable_string_saved":
            replacements += ("return state.get(false) || null;",)
        if name == "nullable_key_string_saved":
            replacements += ("return state.get('seed') || null;",)
        if name in {
            "nullable_payload_saved",
            "nullable_payload_mixed_saved",
            "nullable_host_result_saved",
            "nullable_nested_result_saved",
        }:
            replacements += ("return state.get('seed') || null;",)
        for index, replacement in enumerate(replacements):
            if source.count(original_return) != 1:
                raise RuntimeError(f"{name}: lost the nullable return observation")
            blind = args.work / f"{name}-identity-blinded-{index}.js"
            blind.write_text(
                nullable_observer_source(source.replace(original_return, replacement), name)
            )
            if host.run([node, "-e", boundary.NODE, str(blind)]).stdout == expected:
                raise RuntimeError(f"{name}: nullable identity cannot distinguish {replacement}")
    for name, old, replacements in (
        (
            "nullable_payload_identity",
            "state.set(key, key);",
            ("state.set(key, null);", "state.set(key, void 0);", "state.set(key, '');"),
        ),
        (
            "nullable_payload_saved",
            "return saved;",
            ("return state.get(key);", "return 'overwritten';"),
        ),
        ("nullable_payload_deleted", "state.delete(key);", ("state.has(key);",)),
        (
            "nullable_payload_mixed_readback",
            "state.set(key, key);",
            ("state.set(key, null);", "state.set(key, void 0);", "state.set(key, '');"),
        ),
        (
            "nullable_mixed_payload_readback",
            "state.set(key, key);",
            ("state.set(key, null);", "state.set(key, void 0);", "state.set(key, '');"),
        ),
        (
            "nullable_payload_mixed_identity",
            "state.set(key, key);",
            ("state.set(key, null);", "state.set(key, void 0);", "state.set(key, '');"),
        ),
        (
            "nullable_payload_mixed_saved",
            "return saved;",
            ("return state.get(key);", "return true;"),
        ),
        (
            "nullable_payload_mixed_saved",
            "const saved = state.get(key); state.set(key, true);",
            ("state.set(key, true); const saved = state.get(key);",),
        ),
        (
            "nullable_host_result",
            "return state.get(key);",
            ("return null;", "return void 0;", "return '';", "return true;"),
        ),
        (
            "nullable_host_result_conditional",
            "if (key) { state.set(key, 'selected'); }",
            ("state.has(key);", "state.set(key, 'selected');"),
        ),
        ("nullable_host_result_saved", "return saved;", ("return state.get(key);", "return true;")),
        (
            "nullable_host_result_saved",
            "const saved = state.get(key); state.set(key, true);",
            ("state.set(key, true); const saved = state.get(key);",),
        ),
        (
            "nullable_nested_result",
            "return state.get(key);",
            ("return null;", "return void 0;", "return '';", "return true;"),
        ),
        (
            "nullable_nested_result_saved",
            "return saved;",
            ("return state.get(key);", "return true;"),
        ),
        (
            "nullable_nested_result_saved",
            "const saved = state.get(key); state.set(key, true);",
            ("state.set(key, true); const saved = state.get(key);",),
        ),
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
    if (
        host.run([node, "-e", boundary.NODE, str(deletion)]).stdout != "trace=1\n"
        or host.run([str(reference), str(deletion)]).stdout != "trace=1\n"
    ):
        raise RuntimeError("nullable host result: saved payload deletion observation changed")
    deletion.write_text(saved_source.replace("state.delete(key);", "state.has(key);"))
    if host.run([node, "-e", boundary.NODE, str(deletion)]).stdout != "trace=2\n":
        raise RuntimeError(
            "nullable host result: saved payload lifetime cannot distinguish deletion"
        )
