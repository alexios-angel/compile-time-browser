"""Helpers that check the C++ emitted for the global-Map ownership tests.

check_result_calls verifies the order of Map method calls in the native entry;
the nullable_* helpers build programs that probe each result; lifetime and
shared_lifetime wrap the generated `main` with weak-pointer probes that prove
each Map is freed exactly when its last owner is.
"""

import json
import os
import re
import subprocess

from CTNative.harness import inline_runtime, run_sanitized

from ..sources import *


def comparable_provenance(cpp, input_ir):
    """Return `cpp` with the input filename in `// ctcompile:` location comments
    replaced by `<input>`.

    A forged, reparsed input has a different filename but the same source
    locations, so after this every line, column and emitted token must match.
    """
    filename = re.compile(r"(?<!\S)" + re.escape(str(input_ir)) + r"(?=:\d+:\d+(?:\D|$))")
    return "".join(
        filename.sub("<input>", line) if line.startswith("// ctcompile:") else line
        for line in cpp.splitlines(keepends=True)
    )


def instrument_maps(cpp, *, declarations=""):
    """Rename `main` to ctnative_test_entry and record every Map allocation as a
    weak pointer in ctn_test_maps, so an appended `main` can check lifetimes."""
    changed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", inline_runtime(cpp))
    if count != 1:
        raise RuntimeError("Map lifetime observer needs exactly one entry")
    changed = (
        "#include <memory>\n#include <type_traits>\n#include <vector>\n"
        "static std::vector<std::weak_ptr<const void>> ctn_test_maps;\n" + declarations + changed
    )
    changed, count = re.subn(
        r"return std::make_shared<(map_storage<K, V>|number_map<K>)>\(\);",
        lambda match: (
            "auto made = std::make_shared<" + match[1] + ">(); "
            "ctn_test_maps.emplace_back(made); return made;"
        ),
        changed,
    )
    if count != 2:
        raise RuntimeError("Map lifetime observer lost its allocation helpers")
    return changed


def run_sanitized_lifetime(source, binary, compiler):
    return run_sanitized(compiler, source, binary, owned.FLAGS)


def check_result_calls(cpp, name, mode):
    """Check that the native `main` of case `name` calls the Map's get/set/size
    methods in the expected order, passing each getter result straight to the
    next setter, and keeps the live map_get/map_delete calls instead of a
    precomputed summary."""
    entry = re.search(r"\bmain\(\)\s*\{(.*?)^\}", cpp, re.M | re.S)
    if not entry:
        raise RuntimeError(f"{name}/{mode}: missing native entry for result-call census")
    methods_by_value = dict(
        re.findall(r"(\w+)\s*=\s*ctnative::method_get<&[^>\n]+::m_(\w+)>\(", entry[1])
    )
    calls = re.findall(r"(?:\b(\w+)\s*=\s*)?ctnative::invoke_callable\((\w+)([^;\n]*)\);", entry[1])
    sequence, pending = [], []
    setter_result = None
    for call_index, (result, callee, arguments) in enumerate(calls):
        method = methods_by_value.get(callee)
        if not method:
            raise RuntimeError(f"{name}/{mode}: result call lost its current method binding")
        sequence.append(method)
        if method == "get":
            if not result:
                raise RuntimeError(f"{name}/{mode}: dropped the producing call's result")
            pending.append(result)
        elif method == "set":
            actuals = [argument.strip() for argument in arguments.split(",")[1:]]
            actuals = [re.sub(r"^std::move\((\w+)\)$", r"\1", argument) for argument in actuals]
            literal_key = (
                name
                in {
                    "nullable_key_identity",
                    "nullable_key_identity_normalized",
                    "nullable_key_string_saved",
                    "nullable_payload_identity",
                    "nullable_payload_saved",
                    "nullable_payload_mixed_identity",
                    "nullable_payload_mixed_saved",
                    "nullable_host_result_identity",
                    "nullable_nested_result_identity",
                    "nullable_nested_result_saved",
                }
                and not pending
                and len(actuals) == 1
            )
            if name in nullable_nested_result_sources() and call_index == 2:
                if not setter_result or actuals != [setter_result]:
                    raise RuntimeError(f"{name}/{mode}: outer setter lost the inner setter result")
            elif not literal_key and (not pending or actuals != pending):
                raise RuntimeError(f"{name}/{mode}: setter lost live producing-call operands/order")
            pending.clear()
            setter_result = result
        elif method == "size" and name in nullable_host_result_sources():
            actuals = [
                re.sub(r"^std::move\((\w+)\)$", r"\1", argument.strip())
                for argument in arguments.split(",")[1:]
            ]
            if not setter_result or actuals != [setter_result]:
                raise RuntimeError(f"{name}/{mode}: size lost the live nullable setter result")
    expected = {
        "parameter_call_result": ["get", "set", "get"],
        "result_reverse_members": ["get", "set", "get"],
        "result_repeated": ["get", "set", "get", "set", "get"],
        "result_alias": ["get", "set", "get"],
        "result_argument_order": ["get", "get", "set", "get"],
        "result_bool": ["get", "set", "get", "set", "size"],
        "result_missing_return": ["get", "set", "get", "set", "size"],
        "result_delete": ["get", "set", "get", "set", "get", "set", "size"],
        "result_string": ["get", "set", "size"],
        "result_formal": ["get", "set", "size"],
        "result_seeded_map_get": ["get", "set", "get"],
        "result_seeded_empty_deleted": ["get", "set", "size"],
        "result_seeded_repeated": ["get", "set", "get", "set", "get"],
        "result_seeded_overwrite": ["get", "set", "get"],
        "result_seeded_growing": ["get", "set", "get"],
        "result_seeded_formal": ["get", "set", "get", "set", "size"],
        **{name: ["get", "set", "get"] for name in key_fact_sources()},
        **{name: ["get", "set", "get"] for name in joined_result_sources()},
        **{name: ["get", "set", "get"] for name in size_result_sources()},
        **{name: ["get", "set", "size"] for name in payload_result_sources()},
        **{name: ["get", "set", "size"] for name in mixed_result_sources()},
        **{name: ["get", "set", "size"] for name in saved_read_sources()},
        **{name: ["get", "set", "get", "set", "size"] for name in saved_join_sources()},
        **{name: ["get", "set", "get", "set", "size"] for name in guarded_saved_sources()},
        **{name: ["get", "set", "get", "set", "size"] for name in shortcircuit_sources()},
        **{name: ["get", "set", "get", "set", "size"] for name in nullable_result_sources()},
        **{name: ["get", "set", "get", "set", "size"] for name in nullable_key_sources()},
        **{name: ["get", "set", "get", "set", "size"] for name in nullable_payload_sources()},
        **{name: ["get", "set", "get", "set", "size"] for name in nullable_host_result_sources()},
        **{
            name: ["get", "set", "set", "get", "set", "size"]
            for name in nullable_nested_result_sources()
        },
        "nullable_nested_result_identity": [
            "get",
            "set",
            "set",
            "get",
            "set",
            "set",
            "set",
            "set",
            "size",
        ],
        "nullable_nested_result_saved": ["get", "set", "set", "set", "size"],
        "nullable_host_result_identity": [
            "get",
            "set",
            "get",
            "set",
            "set",
            "set",
            "get",
            "set",
            "size",
        ],
        "saved_join_string_saved": ["get", "set", "size"],
        "guarded_saved_string_saved": ["get", "set", "size"],
        "shortcircuit_empty_string": ["get", "set", "size"],
        "shortcircuit_zero": ["get", "set", "size"],
        "shortcircuit_string_saved": ["get", "set", "size"],
        "nullable_empty": ["get", "set", "size"],
        "nullable_string_saved": ["get", "set", "size"],
        "nullable_threeway": ["get", "set", "get", "set", "get", "set", "size"],
        "nullable_key_identity": ["get", "set", "get", "set", "set", "set", "size"],
        "nullable_key_identity_normalized": ["get", "set", "get", "set", "set", "set", "size"],
        "nullable_key_string_saved": ["get", "set", "set", "size"],
        "nullable_payload_identity": ["get", "set", "get", "set", "set", "set", "size"],
        "nullable_payload_saved": ["get", "set", "set", "size"],
        "nullable_payload_mixed_identity": ["get", "set", "get", "set", "set", "set", "size"],
        "nullable_payload_mixed_saved": ["get", "set", "set", "size"],
        "saved_read_write_repeated": ["get", "set", "get", "set", "size"],
        "seeded_dynamic_overwrite": ["get", "set", "get", "set"],
        "seeded_dynamic_repeated": ["get", "set", "get", "set", "get"],
        "seeded_dynamic_formal": ["get", "set", "get", "set", "size"],
        "seeded_size_saved": ["get", "set", "get", "set"],
        "seeded_size_two_saved": ["get", "set", "get", "set"],
        "seeded_size_two_saved_empty": ["get", "set", "get", "set"],
        "seeded_size_after_delete": ["get", "set", "get", "set"],
    }[name]
    if sequence != expected or "ctnative::map_set(" not in cpp:
        raise RuntimeError(f"{name}/{mode}: lost runtime getter/mutation/final observation calls")
    if name == "result_missing_return":
        observed = re.search(
            r"\b(\w+)\s*=\s*ctnative::to_nullable\(" + re.escape(calls[-1][0]) + r"\);",
            entry[1],
        )
        if not observed or f"g_trace = {observed[1]};" not in entry[1]:
            raise RuntimeError(f"{name}/{mode}: trace lost the final size result")
    seeded = {
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
        **nullable_host_result_sources(),
        **nullable_nested_result_sources(),
        **primitive_absence_sources(),
    }
    if name in seeded and not re.search(r"ctnative::map_get(?:_\w+)?(?:<[^>]+>)?\(", cpp):
        raise RuntimeError(f"{name}/{mode}: replaced the live seeded Map lookup with a summary")
    if name in size_result_sources() and "ctnative::map_delete(" not in cpp:
        raise RuntimeError(f"{name}/{mode}: dropped the live size-keyed deletion")
    if (
        name
        in {
            "result_seeded_string_saved",
            "result_seeded_mixed_string_saved",
            "saved_read_write_string_saved",
            "saved_join_string_saved",
            "guarded_saved_string_saved",
            "shortcircuit_string_saved",
            "nullable_string_saved",
            "nullable_key_string_saved",
            "nullable_payload_saved",
            "nullable_payload_mixed_saved",
            "nullable_host_result_saved",
            "nullable_nested_result_saved",
        }
        and "ctnative::map_delete(" not in cpp
    ):
        raise RuntimeError(f"{name}/{mode}: dropped the saved string's source deletion")
    mixed = {
        **mixed_result_sources(),
        **saved_read_sources(),
        **saved_join_sources(),
        **guarded_saved_sources(),
        **shortcircuit_sources(),
        **nullable_result_sources(),
        **nullable_key_sources(),
        **nullable_payload_sources(),
        **nullable_host_result_sources(),
        **nullable_nested_result_sources(),
        **primitive_absence_sources(),
    }
    if name in mixed:
        source = mixed[name][0]
        for method in ("set", "get", "has", "delete"):
            source_count = len(re.findall(rf"\bstate\.{method}\(", source))
            native_count = len(re.findall(rf"\bctnative::map_{method}(?:_\w+)?(?:<[^>]+>)?\(", cpp))
            if native_count != source_count:
                raise RuntimeError(
                    f"{name}/{mode}: changed the {source_count} live Map.{method} calls"
                )
    if name in {
        **shortcircuit_sources(),
        **nullable_result_sources(),
        **nullable_key_sources(),
        **nullable_payload_sources(),
        **nullable_host_result_sources(),
        **nullable_nested_result_sources(),
    }:
        getter = re.search(r"^[^\n;]+\bfn_4\([^\n]*\) \{(.*?)^\}", cpp, re.M | re.S)
        branches = (
            5 if name == "nullable_threeway" else 4 if name in nullable_result_sources() else 3
        )
        if name == "nullable_homogeneous_key":
            branches = 2
        if name in nullable_key_sources():
            branches = 4 if name in {"nullable_original_key", "nullable_second_key_use"} else 2
        if name in nullable_payload_sources():
            branches = (
                4 if name in {"nullable_payload_mixed", "nullable_mixed_payload_readback"} else 2
            )
        if name in {**nullable_host_result_sources(), **nullable_nested_result_sources()}:
            branches = 2
        # fn_4 is the emitted C++ for the Map getter method; count its if/?:
        # selections, one per distinct result the getter can return.
        # Canonicalization may turn the pure null/undefined choice into ?:, and
        # nullable_identity_cpp checks both results at run time.
        if not getter or len(re.findall(r"\bif\s*\(|\?", getter[1])) != branches:
            raise RuntimeError(f"{name}/{mode}: lost the {branches} live getter selections")


def nullable_observer_source(source, name):
    """Append a snippet that runs after the original program and calls
    host.slot.get/set with each recorded case.

    Bit `index` of `trace` is set when case `index` returns its expected value,
    so one number reports every case when run under Node and the reference
    interpreter.
    """
    observed = source + "\n(function() { trace = 0;\n"
    for index, (arguments, tag, value) in enumerate(NULLABLE_OBSERVATIONS[name]):
        expected = (
            json.dumps(value) if tag == "string" else "null" if tag == "null_value" else "undefined"
        )
        observed += (
            f"var nullableObserved{index} = host.slot.get({arguments});\n"
            f"if (nullableObserved{index} === {expected}) {{ trace = trace + {1 << index}; }}\n"
        )
    for index, (argument, _, _, tag, value) in enumerate(
        NULLABLE_PAYLOAD_READBACKS.get(name, ()), len(NULLABLE_OBSERVATIONS[name])
    ):
        expected = (
            json.dumps(value) if tag == "string" else "null" if tag == "null_value" else "undefined"
        )
        invocation = f"host.slot.set({argument})"
        if name in nullable_nested_result_sources():
            invocation = f"host.slot.set({invocation})"
        observed += (
            f"var nullableObserved{index} = {invocation};\n"
            f"if (nullableObserved{index} === {expected}) {{ trace = trace + {1 << index}; }}\n"
        )
    return observed + "})();\n"


def nullable_identity_cpp(cpp, name):
    """C++ counterpart of nullable_observer_source: call each case on g_host and
    return 91 + index for the first result whose tag or value is wrong."""
    changed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", inline_runtime(cpp))
    if count != 1:
        raise RuntimeError("nullable identity observer needs exactly one entry")
    changed += "\nint main() {\n    if (ctnative_test_entry() != 0) { return 90; }\n"
    for index, (arguments, tag, value) in enumerate(NULLABLE_OBSERVATIONS[name]):
        arguments = ", ".join(
            f"ctnative::js_boolean_t{{{argument}}}" for argument in arguments.split(", ")
        )
        changed += (
            f"    const auto observed_{index} = g_host->slot->m_get({arguments});\n"
            f"    if (observed_{index}.tag != ctnative::nullable_string::kind::{tag} ||\n"
            f"        observed_{index}.value != {json.dumps(value)}) {{ return {91 + index}; }}\n"
        )
    for index, (_, input_tag, input_value, tag, value) in enumerate(
        NULLABLE_PAYLOAD_READBACKS.get(name, ()), len(NULLABLE_OBSERVATIONS[name])
    ):
        invocation = f"g_host->slot->m_set(input_{index})"
        if name in nullable_nested_result_sources():
            invocation = f"g_host->slot->m_set({invocation})"
        changed += (
            f"    ctnative::nullable_string input_{index};\n"
            f"    input_{index}.tag = ctnative::nullable_string::kind::{input_tag};\n"
            f"    input_{index}.value = {json.dumps(input_value)};\n"
            f"    const auto observed_{index} = {invocation};\n"
            f"    if (observed_{index}.tag != ctnative::nullable_string::kind::{tag} ||\n"
            f"        observed_{index}.value != {json.dumps(value)}) {{ return {91 + index}; }}\n"
        )
    return changed + "    return 0;\n}\n"


def source_calls(text):
    return [
        call.strip()
        for call in re.findall(r"^\s*(?:%[-\w.$]+ = )?ctjs\.(call(?:_direct)? [^\n{]+)", text, re.M)
    ]


def contract(args, ir, name, binding="host"):
    """Write the owned-binding contract for `name` with Map as an intrinsic and,
    for scalar-global cases, the saved globals listed as observations."""
    config = owned.contract(args, ir, name, binding)
    value = json.loads(config.read_text())
    value["initial_intrinsics"] = ["Map"]
    candidates = (
        NUMERIC_ENTRY_SAVED_GLOBALS | scalar_global_cases().keys() | constant_global_cases().keys()
    )
    case = next(
        (
            candidate
            for candidate in sorted(candidates, key=len, reverse=True)
            if name == candidate or name.startswith(candidate + "-")
        ),
        None,
    )
    if case:
        # Listing the saved globals as observations only asks the compiler to
        # expose them; it tells the ownership and type analyses nothing, and
        # they still prove each value from the program.
        value["observations"] = sorted(["trace", *scalar_global_values(case)])
    config.write_text(json.dumps(value, indent=2) + "\n")
    return config


def resolve_getter(args, ir):
    """Write a copy of `ir` whose one `ctjs.call` of the `get` property is
    already a ctjs.call_direct to the fourth function, as a control input."""
    text = ir.read_text()
    targets = re.findall(r"^\s*ctjs\.func (?:private )?@([^\s(]+)", text, re.M)
    end = text.index("\n  }\n")
    entry = text[:end]
    undefined = re.search(r"(%[-\w.$]+) = ctjs\.constant #ctjs\.undefined", entry)
    key = re.search(r'(%[-\w.$]+) = ctjs\.constant #ctjs\.string<"get">', entry)
    if len(targets) != 4 or not undefined or not key:
        raise RuntimeError("direct getter lost its source function chain")
    getter = re.search(
        r"(%[-\w.$]+) = ctjs\.get_property (%[-\w.$]+)\[" + re.escape(key[1]) + r"\]", entry
    )
    if not getter:
        raise RuntimeError("direct getter lost its property read")
    entry, count = re.subn(
        r"ctjs\.call " + re.escape(getter[1]) + r"\(" + re.escape(getter[2]) + r"\)",
        lambda _: f"ctjs.call_direct @{targets[3]}({getter[2]}, {undefined[1]}, {getter[1]})",
        entry,
    )
    if count != 1:
        raise RuntimeError("direct getter control did not resolve exactly one call")
    output = args.work / "already_resolved.direct.mlir"
    output.write_text(entry + text[end:])
    return output


def lifetime(args, cpp, name, mode, value, compiler):
    changed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", inline_runtime(cpp))
    if count != 1:
        raise RuntimeError("lifetime harness needs exactly one entry")
    # Record each runtime Map allocation as a weak pointer, which adds no owner.
    # A constant getter or a leaked environment fails these checks.
    changed = (
        "#include <memory>\n#include <vector>\n"
        "static std::vector<std::weak_ptr<const void>> ctn_test_maps;\n" + changed
    )
    changed, count = re.subn(
        r"return std::make_shared<(map_storage<K, V>|number_map<K>)>\(\);",
        lambda match: (
            "auto made = std::make_shared<" + match[1] + ">(); "
            "ctn_test_maps.emplace_back(made); return made;"
        ),
        changed,
    )
    if count != 2:
        raise RuntimeError("Map allocation lifetime observer no longer matches both helpers")
    # Each nonzero exit code (90-97) names the first lifetime check that failed,
    # so a failing run can be read off its status alone.
    changed += r"""
int main() {
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1) { return 90; }
    auto first = g_host;
    auto table = first->slot;
    auto callable = table->m_get;
    std::weak_ptr owner_lifetime = first;
    std::weak_ptr table_lifetime = table;
    g_host.reset();
    for (int index = 0; index < 4096; ++index) {
        auto churn = std::make_shared<ctn_slot>();
        churn->slot = std::make_shared<typename decltype(table)::element_type>();
    }
    if (owner_lifetime.expired() || table_lifetime.expired() || ctn_test_maps[0].expired()) {
        return 91;
    }
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock() || first == g_host ||
        table == g_host->slot) {
        return 92;
    }
    first.reset();
    if (!owner_lifetime.expired() || table_lifetime.expired() || table->m_get().value() != SAVED_FIRST) {
        return 93;
    }
    table.reset();
    if (!table_lifetime.expired() || ctn_test_maps[0].expired()) { return 94; }
    for (int index = 0; index < 1024; ++index) {
        if (callable().value() != SAVED_NEXT || g_host->slot->m_get().value() != FRESH_NEXT) { return 95; }
    }
    callable = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired()) { return 96; }
    std::weak_ptr second_owner = g_host;
    std::weak_ptr second_table = g_host->slot;
    g_host.reset();
    if (!second_owner.expired() || !second_table.expired() || !ctn_test_maps[1].expired()) {
        return 97;
    }
    return 0;
}
"""
    # The growing-key method changes its Map on every call. The saved
    # environment has had one extra call, so sharing the second entry's
    # allocation or replacing later calls with a startup summary fails here.
    growing = name in {"growing", "result_seeded_growing"}
    changed = changed.replace("SAVED_FIRST", "2" if growing else str(value))
    changed = changed.replace("SAVED_NEXT", "index + 3" if growing else str(value))
    changed = changed.replace("FRESH_NEXT", "index + 2" if growing else str(value))
    owned.check_lifetime(
        args, name, mode, compiler, changed, f"trace={value}\n", "captured Map lifetime"
    )


def shared_lifetime(args, cpp, name, mode, compiler):
    """Check that a getter/setter pair sharing one Map keeps it alive only while
    either callable does; exit codes 90-100 name the first failed check."""
    changed = instrument_maps(cpp)
    changed += r"""
int main() {
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1) { return 90; }
    auto owner = g_host;
    auto table = owner->slot;
    auto setter = table->m_set;
    auto getter = table->m_get;
    CHECK_SIGNATURE
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset();
    owner.reset();
    table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired() || getter().value() != 1) { return 91; }
    for (int index = 0; index < 4096; ++index) {
        auto churn = std::make_shared<ctn_slot>();
        churn->slot = std::make_shared<typename decltype(g_host->slot)::element_type>();
    }
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock()) { return 92; }
    BEFORE_FIRST
    if (SAVED_FIRST != 2 || getter().value() != 2 || g_host->slot->m_get().value() != 1) { return 93; }
    AFTER_FIRST
    for (int index = 0; index < 1024; ++index) {
        if (SAVED_NEXT != index + 3 || getter().value() != index + 3 ||
            g_host->slot->m_get().value() != index + 1 ||
            FRESH_NEXT != index + 2) { return 94; }
    }
    setter = {};
    if (ctn_test_maps[0].expired() || getter().value() != 1026) { return 95; }
    auto copied = getter;
    getter = {};
    if (ctn_test_maps[0].expired() || copied().value() != 1026) { return 96; }
    copied = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired()) { return 97; }
    auto fresh_getter = g_host->slot->m_get;
    std::weak_ptr fresh_owner = g_host;
    std::weak_ptr fresh_table = g_host->slot;
    g_host.reset();
    if (!fresh_owner.expired() || !fresh_table.expired() ||
        ctn_test_maps[1].expired() || fresh_getter().value() != 1025) { return 98; }
    fresh_getter = {};
    if (!ctn_test_maps[1].expired()) { return 99; }
    return 0;
}
"""
    if name == "shared_parameter":
        # A typed C++ caller, not a general JS export. New keys after startup
        # must flow through the real parameter, and the Map must own its own
        # copy of the string after the caller overwrites its buffer.
        changed = changed.replace(
            "CHECK_SIGNATURE",
            """
    static_assert(std::is_same_v<decltype(setter), std::function<ctnative::js_num(ctnative::js_string)>>);
    static_assert(!std::is_invocable_v<decltype(setter)>);
    static_assert(!std::is_invocable_v<decltype(setter), int>);
    static_assert(!std::is_invocable_v<decltype(setter), std::string, int>);
""",
        )
        changed = changed.replace("BEFORE_FIRST", "std::string saved_key(96, 's');")
        changed = changed.replace("SAVED_FIRST", "setter(ctnative::js_string{saved_key}).value()")
        changed = changed.replace(
            "AFTER_FIRST",
            """
    saved_key.assign(96, 't');
    if (setter(ctnative::js_string{std::string(96, 's')}).value() != 2 || getter().value() != 2) { return 100; }
""",
        )
        changed = changed.replace(
            "SAVED_NEXT", 'setter(ctnative::js_string{"saved-" + std::to_string(index)}).value()'
        )
        changed = changed.replace(
            "FRESH_NEXT",
            'g_host->slot->m_set(ctnative::js_string{"fresh-" + std::to_string(index)}).value()',
        )
    else:
        for marker in ("CHECK_SIGNATURE", "BEFORE_FIRST", "AFTER_FIRST"):
            changed = changed.replace(marker, "")
        changed = changed.replace("SAVED_FIRST", "setter().value()")
        changed = changed.replace("SAVED_NEXT", "setter().value()")
        changed = changed.replace("FRESH_NEXT", "g_host->slot->m_set().value()")
    owned.check_lifetime(args, name, mode, compiler, changed, "trace=1\n", "shared Map lifetime")
