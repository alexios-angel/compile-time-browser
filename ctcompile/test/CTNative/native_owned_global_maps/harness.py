"""The checks: emitted-C++ censuses, the lifetime harnesses, standalone execution
and the budget bisection.

Split out of native-owned-global-maps.py on 2026-09-08, verbatim.
"""

import json
import os
import re
import subprocess

from .sources import (
    methods, owned, boundary, host, parameter_sources, seeded_result_sources, key_fact_sources,
    joined_result_sources, size_result_sources, payload_result_sources, STRING_RESULT,
    RESULT_SIGNATURES, mixed_result_sources, MIXED_RESULT_TYPES, saved_read_sources,
    saved_join_sources, OTHER_STRING_RESULT, guarded_saved_sources, shortcircuit_sources,
    nullable_result_sources, nullable_key_sources, NULLABLE_OBSERVATIONS,
    nullable_payload_sources, NULLABLE_PAYLOAD_READBACKS, nullable_host_result_sources,
    nullable_nested_result_sources,
)


def check_result_calls(cpp, name, mode):
    entry = re.search(r"\bmain\(\)\s*\{(.*?)^\}", cpp, re.M | re.S)
    if not entry:
        raise RuntimeError(f"{name}/{mode}: missing native entry for result-call census")
    methods_by_value = dict(re.findall(
        r"(\w+)\s*=\s*ctnative::method_get<&[^>\n]+::m_(\w+)>\(", entry[1]))
    calls = re.findall(
        r"(?:\b(\w+)\s*=\s*)?ctnative::invoke_callable\((\w+)([^;\n]*)\);", entry[1])
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
            actuals = [re.sub(r"^std::move\((\w+)\)$", r"\1", argument)
                       for argument in actuals]
            literal_key = name in {"nullable_key_identity", "nullable_key_identity_normalized",
                                   "nullable_key_string_saved", "nullable_payload_identity",
                                   "nullable_payload_saved", "nullable_payload_mixed_identity",
                                   "nullable_payload_mixed_saved", "nullable_host_result_identity",
                                   "nullable_nested_result_identity", "nullable_nested_result_saved"} \
                and not pending and len(actuals) == 1
            if name in nullable_nested_result_sources() and call_index == 2:
                if not setter_result or actuals != [setter_result]:
                    raise RuntimeError(f"{name}/{mode}: outer setter lost the inner setter result")
            elif not literal_key and (not pending or actuals != pending):
                raise RuntimeError(f"{name}/{mode}: setter lost live producing-call operands/order")
            pending.clear()
            setter_result = result
        elif method == "size" and name in nullable_host_result_sources():
            actuals = [re.sub(r"^std::move\((\w+)\)$", r"\1", argument.strip())
                       for argument in arguments.split(",")[1:]]
            if not setter_result or actuals != [setter_result]:
                raise RuntimeError(f"{name}/{mode}: size lost the live nullable setter result")
    expected = {
        "parameter_call_result": ["get", "set", "get"],
        "result_reverse_members": ["get", "set", "get"],
        "result_repeated": ["get", "set", "get", "set", "get"],
        "result_alias": ["get", "set", "get"],
        "result_argument_order": ["get", "get", "set", "get"],
        "result_bool": ["get", "set", "get", "set", "size"],
        "result_delete": ["get", "set", "get", "set", "get", "set", "size"],
        "result_string": ["get", "set", "size"],
        "result_formal": ["get", "set", "size"],
        "result_seeded_map_get": ["get", "set", "get"],
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
        **{name: ["get", "set", "set", "get", "set", "size"]
           for name in nullable_nested_result_sources()},
        "nullable_nested_result_identity": ["get", "set", "set", "get", "set", "set", "set",
                                            "set", "size"],
        "nullable_nested_result_saved": ["get", "set", "set", "set", "size"],
        "nullable_host_result_identity": ["get", "set", "get", "set", "set", "set",
                                          "get", "set", "size"],
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
    seeded = {**seeded_result_sources(), **key_fact_sources(), **joined_result_sources(),
              **size_result_sources(), **payload_result_sources(), **mixed_result_sources(),
              **saved_read_sources(), **saved_join_sources(), **guarded_saved_sources(),
              **shortcircuit_sources(), **nullable_result_sources(), **nullable_key_sources(),
              **nullable_payload_sources(), **nullable_host_result_sources(), **nullable_nested_result_sources()}
    if name in seeded and not re.search(r"ctnative::map_get(?:_\w+)?(?:<[^>]+>)?\(", cpp):
        raise RuntimeError(f"{name}/{mode}: replaced the live seeded Map lookup with a summary")
    if name in size_result_sources() and "ctnative::map_delete(" not in cpp:
        raise RuntimeError(f"{name}/{mode}: dropped the live size-keyed deletion")
    if name in {"result_seeded_string_saved", "result_seeded_mixed_string_saved",
                "saved_read_write_string_saved", "saved_join_string_saved",
                "guarded_saved_string_saved", "shortcircuit_string_saved", "nullable_string_saved",
                "nullable_key_string_saved", "nullable_payload_saved", "nullable_payload_mixed_saved",
                "nullable_host_result_saved", "nullable_nested_result_saved"} \
            and "ctnative::map_delete(" not in cpp:
        raise RuntimeError(f"{name}/{mode}: dropped the saved string's source deletion")
    mixed = {**mixed_result_sources(), **saved_read_sources(), **saved_join_sources(),
             **guarded_saved_sources(), **shortcircuit_sources(), **nullable_result_sources(),
             **nullable_key_sources(), **nullable_payload_sources(), **nullable_host_result_sources(), **nullable_nested_result_sources()}
    if name in mixed:
        source = mixed[name][0]
        for method in ("set", "get", "has", "delete"):
            source_count = len(re.findall(rf"\bstate\.{method}\(", source))
            native_count = len(re.findall(rf"\bctnative::map_{method}(?:_\w+)?(?:<[^>]+>)?\(", cpp))
            if native_count != source_count:
                raise RuntimeError(f"{name}/{mode}: changed the {source_count} live Map.{method} calls")
    if name in {**shortcircuit_sources(), **nullable_result_sources(), **nullable_key_sources(),
                **nullable_payload_sources(), **nullable_host_result_sources(), **nullable_nested_result_sources()}:
        getter = re.search(r"^[^\n;]+\bfn_4\([^\n]*\) \{(.*?)^\}", cpp, re.M | re.S)
        branches = 5 if name == "nullable_threeway" else 4 if name in nullable_result_sources() else 3
        if name == "nullable_homogeneous_key":
            branches = 2
        if name in nullable_key_sources():
            branches = 4 if name in {"nullable_original_key", "nullable_second_key_use"} else 2
        if name in nullable_payload_sources():
            branches = 4 if name in {"nullable_payload_mixed", "nullable_mixed_payload_readback"} else 2
        if name in {**nullable_host_result_sources(), **nullable_nested_result_sources()}:
            branches = 2
        # Canonicalization can use ?: for the pure Null/Undefined selection.
        # The executed identity observer also checks both results separately.
        if not getter or len(re.findall(r"\bif\s*\(|\?", getter[1])) != branches:
            raise RuntimeError(f"{name}/{mode}: lost the {branches} live getter selections")


def nullable_observer_source(source, name):
    # Observation runs in the independent engines after the unchanged startup.
    # The published body needs no extra comparison or observer proof to do so.
    observed = source + "\n(function() { trace = 0;\n"
    for index, (arguments, tag, value) in enumerate(NULLABLE_OBSERVATIONS[name]):
        expected = json.dumps(value) if tag == "string" else "null" if tag == "null_value" else "undefined"
        observed += (f"var nullableObserved{index} = host.slot.get({arguments});\n"
                     f"if (nullableObserved{index} === {expected}) {{ trace = trace + {1 << index}; }}\n")
    for index, (argument, _, _, tag, value) in enumerate(NULLABLE_PAYLOAD_READBACKS.get(name, ()),
                                                        len(NULLABLE_OBSERVATIONS[name])):
        expected = json.dumps(value) if tag == "string" else "null" if tag == "null_value" else "undefined"
        invocation = f"host.slot.set({argument})"
        if name in nullable_nested_result_sources():
            invocation = f"host.slot.set({invocation})"
        observed += (f"var nullableObserved{index} = {invocation};\n"
                     f"if (nullableObserved{index} === {expected}) {{ trace = trace + {1 << index}; }}\n")
    return observed + "})();\n"


def nullable_identity_cpp(cpp, name):
    changed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", cpp)
    if count != 1:
        raise RuntimeError("nullable identity observer needs exactly one entry")
    changed += "\nint main() {\n    if (ctnative_test_entry() != 0) { return 90; }\n"
    for index, (arguments, tag, value) in enumerate(NULLABLE_OBSERVATIONS[name]):
        changed += (f"    const auto observed_{index} = g_host->slot->m_get({arguments});\n"
                    f"    if (observed_{index}.tag != ctnative::nullable_string::kind::{tag} ||\n"
                    f"        observed_{index}.value != {json.dumps(value)}) {{ return {91 + index}; }}\n")
    for index, (_, input_tag, input_value, tag, value) in enumerate(
            NULLABLE_PAYLOAD_READBACKS.get(name, ()), len(NULLABLE_OBSERVATIONS[name])):
        invocation = f"g_host->slot->m_set(input_{index})"
        if name in nullable_nested_result_sources():
            invocation = f"g_host->slot->m_set({invocation})"
        changed += (f"    ctnative::nullable_string input_{index};\n"
                    f"    input_{index}.tag = ctnative::nullable_string::kind::{input_tag};\n"
                    f"    input_{index}.value = {json.dumps(input_value)};\n"
                    f"    const auto observed_{index} = {invocation};\n"
                    f"    if (observed_{index}.tag != ctnative::nullable_string::kind::{tag} ||\n"
                    f"        observed_{index}.value != {json.dumps(value)}) {{ return {91 + index}; }}\n")
    return changed + "    return 0;\n}\n"


def source_calls(text):
    return [call.strip() for call in re.findall(
        r"^\s*(?:%[-\w.$]+ = )?ctjs\.(call(?:_direct)? [^\n{]+)", text, re.M)]


def contract(args, ir, name, binding="host"):
    config = owned.contract(args, ir, name, binding)
    value = json.loads(config.read_text())
    value["initial_intrinsics"] = ["Map"]
    config.write_text(json.dumps(value, indent=2) + "\n")
    return config


def resolve_getter(args, ir):
    text = ir.read_text()
    targets = re.findall(r"^\s*ctjs\.func (?:private )?@([^\s(]+)", text, re.M)
    end = text.index("\n  }\n")
    entry = text[:end]
    undefined = re.search(r"(%[-\w.$]+) = ctjs\.constant #ctjs\.undefined", entry)
    key = re.search(r'(%[-\w.$]+) = ctjs\.constant #ctjs\.string<"get">', entry)
    if len(targets) != 4 or not undefined or not key:
        raise RuntimeError("direct getter lost its source function chain")
    getter = re.search(r"(%[-\w.$]+) = ctjs\.get_property (%[-\w.$]+)\["
                       + re.escape(key[1]) + r"\]", entry)
    if not getter:
        raise RuntimeError("direct getter lost its property read")
    entry, count = re.subn(r"ctjs\.call " + re.escape(getter[1]) + r"\("
                          + re.escape(getter[2]) + r"\)",
        lambda _: f"ctjs.call_direct @{targets[3]}({getter[2]}, {undefined[1]}, {getter[1]})", entry)
    if count != 1:
        raise RuntimeError("direct getter control did not resolve exactly one call")
    output = args.work / "already_resolved.direct.mlir"
    output.write_text(entry + text[end:])
    return output


def lifetime(args, cpp, name, mode, value, compiler):
    changed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", cpp)
    if count != 1:
        raise RuntimeError("lifetime harness needs exactly one entry")
    # Observe the real runtime Map allocation without adding a strong owner.
    # A constant getter or leaked environment cannot pass these weak witnesses.
    changed = ("#include <memory>\n#include <vector>\n"
               "static std::vector<std::weak_ptr<const void>> ctn_test_maps;\n" + changed)
    changed, count = re.subn(r"return std::make_shared<(map_storage<K, V>|number_map<K>)>\(\);",
        lambda match: "auto made = std::make_shared<" + match[1] + ">(); "
                      "ctn_test_maps.emplace_back(made); return made;", changed)
    if count != 2:
        raise RuntimeError("Map allocation lifetime observer no longer matches both helpers")
    changed += r'''
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
    if (!owner_lifetime.expired() || table_lifetime.expired() || table->m_get() != SAVED_FIRST) {
        return 93;
    }
    table.reset();
    if (!table_lifetime.expired() || ctn_test_maps[0].expired()) { return 94; }
    for (int index = 0; index < 1024; ++index) {
        if (callable() != SAVED_NEXT || g_host->slot->m_get() != FRESH_NEXT) { return 95; }
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
'''
    # The growing-key method changes its Map on EVERY invocation. The saved
    # environment has had one extra call, so sharing the reentry allocation or
    # replacing later calls with a startup summary cannot satisfy this witness.
    growing = name in {"growing", "result_seeded_growing"}
    changed = changed.replace("SAVED_FIRST", "2" if growing else str(value))
    changed = changed.replace("SAVED_NEXT", "index + 3" if growing else str(value))
    changed = changed.replace("FRESH_NEXT", "index + 2" if growing else str(value))
    source = args.work / f"{name}.{mode}.lifetime.cpp"
    source.write_text(changed)
    binary = (args.work / f"{name}.{mode}.sanitized").resolve()
    host.run([compiler, *owned.FLAGS, "-O1", "-g", "-fno-omit-frame-pointer",
              "-fsanitize=address,undefined", "-fsanitize-address-use-after-scope",
              str(source), "-o", str(binary)])
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60,
        env=dict(os.environ, ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
                 UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
    if result.returncode or result.stdout != f"trace={value}\n" * 2:
        raise RuntimeError(f"{name}/{mode}: captured Map lifetime failure\n{result.stdout}{result.stderr}")


def shared_lifetime(args, cpp, name, mode, compiler):
    changed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", cpp)
    if count != 1:
        raise RuntimeError("shared lifetime harness needs exactly one entry")
    changed = ("#include <memory>\n#include <type_traits>\n#include <vector>\n"
               "static std::vector<std::weak_ptr<const void>> ctn_test_maps;\n" + changed)
    changed, count = re.subn(r"return std::make_shared<(map_storage<K, V>|number_map<K>)>\(\);",
        lambda match: "auto made = std::make_shared<" + match[1] + ">(); "
                      "ctn_test_maps.emplace_back(made); return made;", changed)
    if count != 2:
        raise RuntimeError("shared Map lifetime observer lost its allocation helpers")
    changed += r'''
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
        ctn_test_maps[0].expired() || getter() != 1) { return 91; }
    for (int index = 0; index < 4096; ++index) {
        auto churn = std::make_shared<ctn_slot>();
        churn->slot = std::make_shared<typename decltype(g_host->slot)::element_type>();
    }
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock()) { return 92; }
    BEFORE_FIRST
    if (SAVED_FIRST != 2 || getter() != 2 || g_host->slot->m_get() != 1) { return 93; }
    AFTER_FIRST
    for (int index = 0; index < 1024; ++index) {
        if (SAVED_NEXT != index + 3 || getter() != index + 3 ||
            g_host->slot->m_get() != index + 1 ||
            FRESH_NEXT != index + 2) { return 94; }
    }
    setter = {};
    if (ctn_test_maps[0].expired() || getter() != 1026) { return 95; }
    auto copied = getter;
    getter = {};
    if (ctn_test_maps[0].expired() || copied() != 1026) { return 96; }
    copied = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired()) { return 97; }
    auto fresh_getter = g_host->slot->m_get;
    std::weak_ptr fresh_owner = g_host;
    std::weak_ptr fresh_table = g_host->slot;
    g_host.reset();
    if (!fresh_owner.expired() || !fresh_table.expired() ||
        ctn_test_maps[1].expired() || fresh_getter() != 1025) { return 98; }
    fresh_getter = {};
    if (!ctn_test_maps[1].expired()) { return 99; }
    return 0;
}
'''
    if name == "shared_parameter":
        # This is a typed C++ caller, not permission for arbitrary JS exports.
        # New keys after startup must flow through the real formal. The Map
        # also owns string bytes after the caller mutates its original buffer.
        changed = changed.replace("CHECK_SIGNATURE", """
    static_assert(std::is_same_v<decltype(setter), std::function<js_num(std::string)>>);
    static_assert(!std::is_invocable_v<decltype(setter)>);
    static_assert(!std::is_invocable_v<decltype(setter), int>);
    static_assert(!std::is_invocable_v<decltype(setter), std::string, int>);
""")
        changed = changed.replace("BEFORE_FIRST", "std::string saved_key(96, 's');")
        changed = changed.replace("SAVED_FIRST", "setter(saved_key)")
        changed = changed.replace("AFTER_FIRST", """
    saved_key.assign(96, 't');
    if (setter(std::string(96, 's')) != 2 || getter() != 2) { return 100; }
""")
        changed = changed.replace("SAVED_NEXT", 'setter("saved-" + std::to_string(index))')
        changed = changed.replace("FRESH_NEXT", 'g_host->slot->m_set("fresh-" + std::to_string(index))')
    else:
        for marker in ("CHECK_SIGNATURE", "BEFORE_FIRST", "AFTER_FIRST"):
            changed = changed.replace(marker, "")
        changed = changed.replace("SAVED_FIRST", "setter()")
        changed = changed.replace("SAVED_NEXT", "setter()")
        changed = changed.replace("FRESH_NEXT", "g_host->slot->m_set()")
    source = args.work / f"{name}.{mode}.lifetime.cpp"
    source.write_text(changed)
    binary = (args.work / f"{name}.{mode}.sanitized").resolve()
    host.run([compiler, *owned.FLAGS, "-O1", "-g", "-fno-omit-frame-pointer",
              "-fsanitize=address,undefined", "-fsanitize-address-use-after-scope",
              str(source), "-o", str(binary)])
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60,
        env=dict(os.environ, ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
                 UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
    if result.returncode or result.stdout != "trace=1\n" * 2:
        raise RuntimeError(f"{name}/{mode}: shared Map lifetime failure\n"
                           f"{result.stdout}{result.stderr}")


def string_payload_lifetime(args, cpp, name, mode, compiler):
    joined = name in {"saved_join_string_saved", "guarded_saved_string_saved",
                      "shortcircuit_string_saved"}
    initial_size = 2 if joined else 1
    changed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", cpp)
    if count != 1:
        raise RuntimeError("string lifetime harness needs exactly one entry")
    changed = ("#include <memory>\n#include <type_traits>\n#include <vector>\n"
               "static std::vector<std::weak_ptr<const void>> ctn_test_maps;\n" + changed)
    changed, count = re.subn(r"return std::make_shared<(map_storage<K, V>|number_map<K>)>\(\);",
        lambda match: "auto made = std::make_shared<" + match[1] + ">(); "
                      "ctn_test_maps.emplace_back(made); return made;", changed)
    if count != 2:
        raise RuntimeError("string Map lifetime observer lost its allocation helpers")
    changed += r'''
int main() {
    const std::string expected = EXPECTED_STRING;
    const std::string other_expected = OTHER_EXPECTED_STRING;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1) { return 90; }
    auto owner = g_host;
    auto table = owner->slot;
    auto getter = table->m_get;
    auto setter = table->m_set;
    auto size = table->m_size;
    static_assert(std::is_same_v<decltype(getter), std::function<std::string(GETTER_PARAMETERS)>>);
    static_assert(std::is_same_v<decltype(setter), std::function<js_num(std::string)>>);
    auto read = [&]([[maybe_unused]] bool other) { return getter(GETTER_ARGUMENT); };
    auto saved = read(false);
    auto saved_other = read(true);
    if (saved != expected || saved_other != other_expected || setter(saved) != INITIAL_SIZE ||
        setter(saved_other) != INITIAL_SIZE) { return 91; }
    saved.assign(expected.size(), 'x');
    saved_other.assign(other_expected.size(), 'x');
    if (setter(expected) != INITIAL_SIZE || setter(other_expected) != INITIAL_SIZE) { return 92; }
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset();
    owner.reset();
    table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired()) { return 93; }
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock()) { return 94; }
    for (int index = 0; index < 128; ++index) {
        if (read(false) != expected || read(true) != other_expected ||
            setter("saved-" + std::to_string(index)) != index + INITIAL_SIZE + 1 ||
            size() != index + INITIAL_SIZE + 1 || g_host->slot->m_size() != INITIAL_SIZE) {
            return 95;
        }
    }
    auto survivor = read(false);
    auto other_survivor = read(true);
    getter = {};
    setter = {};
    if (ctn_test_maps[0].expired()) { return 96; }
    size = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired()) { return 97; }
    std::vector<std::string> churn;
    for (int index = 0; index < 4096; ++index) {
        churn.emplace_back(expected.size(), 'q');
    }
    if (survivor != expected || other_survivor != other_expected ||
        g_host->slot->m_get(FRESH_FIRST_ARGUMENT) != expected ||
        g_host->slot->m_get(FRESH_OTHER_ARGUMENT) != other_expected) { return 98; }
    g_host.reset();
    if (!ctn_test_maps[1].expired() || survivor != expected || other_survivor != other_expected) {
        return 99;
    }
    return 0;
}
'''
    changed = changed.replace("OTHER_EXPECTED_STRING",
        json.dumps(OTHER_STRING_RESULT if joined else STRING_RESULT))
    changed = changed.replace("EXPECTED_STRING", json.dumps(STRING_RESULT))
    changed = changed.replace("GETTER_PARAMETERS", "bool" if joined else "")
    changed = changed.replace("GETTER_ARGUMENT", "other" if joined else "")
    changed = changed.replace("FRESH_FIRST_ARGUMENT", "false" if joined else "")
    changed = changed.replace("FRESH_OTHER_ARGUMENT", "true" if joined else "")
    changed = changed.replace("INITIAL_SIZE", str(initial_size))
    source = args.work / f"{name}.{mode}.lifetime.cpp"
    source.write_text(changed)
    binary = (args.work / f"{name}.{mode}.sanitized").resolve()
    host.run([compiler, *owned.FLAGS, "-O1", "-g", "-fno-omit-frame-pointer",
              "-fsanitize=address,undefined", "-fsanitize-address-use-after-scope",
              str(source), "-o", str(binary)])
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60,
        env=dict(os.environ, ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
                 UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
    if result.returncode or result.stdout != f"trace={initial_size}\n" * 2:
        raise RuntimeError(f"{name}/{mode}: saved string lifetime failure\n"
                           f"{result.stdout}{result.stderr}")


def nullable_payload_lifetime(args, cpp, name, mode, compiler):
    changed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", cpp)
    if count != 1:
        raise RuntimeError("nullable lifetime harness needs exactly one entry")
    changed = ("#include <memory>\n#include <type_traits>\n#include <vector>\n"
               "static std::vector<std::weak_ptr<const void>> ctn_test_maps;\n" + changed)
    changed, count = re.subn(r"return std::make_shared<(map_storage<K, V>|number_map<K>)>\(\);",
        lambda match: "auto made = std::make_shared<" + match[1] + ">(); "
                      "ctn_test_maps.emplace_back(made); return made;", changed)
    if count != 2:
        raise RuntimeError("nullable lifetime observer lost its allocation helpers")
    changed += r'''
int main() {
    using result_type = ctnative::nullable_string;
    using kind = result_type::kind;
    const std::string expected = EXPECTED_STRING;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1) { return 90; }
    auto owner = g_host;
    auto table = owner->slot;
    auto getter = table->m_get;
    auto setter = table->m_set;
    auto size = table->m_size;
    static_assert(std::is_same_v<decltype(getter), std::function<result_type(bool)>>);
    static_assert(std::is_same_v<decltype(setter), std::function<js_num(result_type)>>);
    auto saved = getter(false);
    auto saved_null = getter(true);
    if (saved.tag != kind::string || saved.value != expected || saved_null.tag != kind::null_value ||
        !saved_null.value.empty() || setter(saved) != 2 || setter(saved_null) != 2) { return 91; }
    saved.value.assign(expected.size(), 'x');
    if (setter(result_type{expected}) != 2 || setter(result_type{std::string{}}) != 2) { return 92; }
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset();
    owner.reset();
    table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired()) { return 93; }
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock()) { return 94; }
    for (int index = 0; index < 128; ++index) {
        const auto text = getter(false);
        const auto absent = getter(true);
        if (text.tag != kind::string || text.value != expected || absent.tag != kind::null_value ||
            !absent.value.empty() || setter(result_type{"saved-" + std::to_string(index)}) != index + 3 ||
            size() != index + 3 || g_host->slot->m_size() != 2) { return 95; }
    }
    const auto survivor = getter(false);
    const auto null_survivor = getter(true);
    getter = {};
    setter = {};
    if (ctn_test_maps[0].expired()) { return 96; }
    size = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired()) { return 97; }
    std::vector<std::string> churn;
    for (int index = 0; index < 4096; ++index) { churn.emplace_back(expected.size(), 'q'); }
    const auto fresh = g_host->slot->m_get(false);
    const auto fresh_null = g_host->slot->m_get(true);
    if (survivor.tag != kind::string || survivor.value != expected ||
        null_survivor.tag != kind::null_value || !null_survivor.value.empty() ||
        fresh.tag != kind::string || fresh.value != expected || fresh_null.tag != kind::null_value) {
        return 98;
    }
    g_host.reset();
    if (!ctn_test_maps[1].expired() || survivor.value != expected ||
        null_survivor.tag != kind::null_value) { return 99; }
    return 0;
}
'''
    changed = changed.replace("EXPECTED_STRING", json.dumps(STRING_RESULT))
    source = args.work / f"{name}.{mode}.lifetime.cpp"
    source.write_text(changed)
    binary = (args.work / f"{name}.{mode}.sanitized").resolve()
    host.run([compiler, *owned.FLAGS, "-O1", "-g", "-fno-omit-frame-pointer",
              "-fsanitize=address,undefined", "-fsanitize-address-use-after-scope",
              str(source), "-o", str(binary)])
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60,
        env=dict(os.environ, ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
                 UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
    if result.returncode or result.stdout != "trace=2\n" * 2:
        raise RuntimeError(f"{name}/{mode}: nullable result lifetime failure\n"
                           f"{result.stdout}{result.stderr}")


def nullable_key_lifetime(args, cpp, name, mode, compiler):
    changed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", cpp)
    if count != 1:
        raise RuntimeError("nullable key lifetime harness needs exactly one entry")
    changed = ("#include <memory>\n#include <type_traits>\n#include <vector>\n"
               "static std::vector<std::weak_ptr<const void>> ctn_test_maps;\n" + changed)
    changed, count = re.subn(r"return std::make_shared<(map_storage<K, V>|number_map<K>)>\(\);",
        lambda match: "auto made = std::make_shared<" + match[1] + ">(); "
                      "ctn_test_maps.emplace_back(made); return made;", changed)
    if count != 2:
        raise RuntimeError("nullable key lifetime observer lost its allocation helpers")
    changed += r'''
int main() {
    using key_type = ctnative::nullable_string;
    using kind = key_type::kind;
    const std::string expected = EXPECTED_STRING;
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1) { return 90; }
    auto owner = g_host;
    auto table = owner->slot;
    auto getter = table->m_get;
    auto setter = table->m_set;
    auto size = table->m_size;
    static_assert(std::is_same_v<decltype(getter), std::function<key_type(bool)>>);
    static_assert(std::is_same_v<decltype(setter), std::function<js_num(key_type)>>);
    auto saved = getter(false);
    const auto absent = getter(true);
    if (saved.tag != kind::string || saved.value != expected || absent.tag != kind::null_value ||
        !absent.value.empty() || setter(saved) != 2 || setter(absent) != 3) { return 91; }
    saved.value.assign(expected.size(), 'x');
    if (setter(key_type{expected}) != 3 || setter(key_type{std::string{}}) != 4 ||
        setter(key_type{}) != 4 || setter(absent) != 4 || size() != 4) { return 92; }
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset();
    owner.reset();
    table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired()) { return 93; }
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock()) { return 94; }
    for (int index = 0; index < 128; ++index) {
        const std::string text = expected + std::to_string(index);
        auto caller = key_type{text};
        if (setter(caller) != index + 5) { return 95; }
        caller.value.assign(text.size(), 'q');
        if (setter(key_type{text}) != index + 5 || setter(absent) != index + 5 ||
            setter(key_type{}) != index + 5 || setter(key_type{std::string{}}) != index + 5 ||
            getter(false).value != expected || getter(true).tag != kind::null_value ||
            size() != index + 5 || g_host->slot->m_size() != 2) { return 96; }
    }
    const auto survivor = getter(false);
    const auto null_survivor = getter(true);
    getter = {};
    setter = {};
    if (ctn_test_maps[0].expired()) { return 97; }
    size = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired()) { return 98; }
    std::vector<std::string> churn;
    for (int index = 0; index < 4096; ++index) { churn.emplace_back(expected.size(), 'z'); }
    const auto fresh = g_host->slot->m_get(false);
    const auto fresh_null = g_host->slot->m_get(true);
    g_host.reset();
    if (!ctn_test_maps[1].expired() || survivor.tag != kind::string || survivor.value != expected ||
        null_survivor.tag != kind::null_value || !null_survivor.value.empty() ||
        fresh.tag != kind::string || fresh.value != expected || fresh_null.tag != kind::null_value) {
        return 99;
    }
    return 0;
}
'''
    changed = changed.replace("EXPECTED_STRING", json.dumps(STRING_RESULT))
    source = args.work / f"{name}.{mode}.lifetime.cpp"
    source.write_text(changed)
    binary = (args.work / f"{name}.{mode}.sanitized").resolve()
    host.run([compiler, *owned.FLAGS, "-O1", "-g", "-fno-omit-frame-pointer",
              "-fsanitize=address,undefined", "-fsanitize-address-use-after-scope",
              str(source), "-o", str(binary)])
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60,
        env=dict(os.environ, ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
                 UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
    if result.returncode or result.stdout != "trace=2\n" * 2:
        raise RuntimeError(f"{name}/{mode}: nullable key lifetime failure\n"
                           f"{result.stdout}{result.stderr}")


def nullable_stored_payload_lifetime(args, cpp, name, mode, compiler):
    changed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", cpp)
    if count != 1:
        raise RuntimeError("nullable stored-payload harness needs exactly one entry")
    changed = ("#include <memory>\n#include <type_traits>\n#include <vector>\n"
               "static std::vector<std::weak_ptr<const void>> ctn_test_maps;\n" + changed)
    changed, count = re.subn(r"return std::make_shared<(map_storage<K, V>|number_map<K>)>\(\);",
        lambda match: "auto made = std::make_shared<" + match[1] + ">(); "
                      "ctn_test_maps.emplace_back(made); return made;", changed)
    if count != 2:
        raise RuntimeError("nullable stored-payload observer lost its allocation helpers")
    changed += r'''
int main() {
    using result_type = ctnative::nullable_string;
    using kind = result_type::kind;
    const std::string expected = EXPECTED_STRING;
    const auto equal = [](const result_type & value, kind tag, const std::string & text = {}) {
        return value.tag == tag && value.value == text;
    };
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1) { return 90; }
    auto owner = g_host;
    auto table = owner->slot;
    auto getter = table->m_get;
    auto setter = table->m_set;
    auto size = table->m_size;
    static_assert(std::is_same_v<decltype(getter), std::function<result_type(bool)>>);
    static_assert(std::is_same_v<decltype(setter), std::function<result_type(result_type)>>);
    auto caller = getter(false);
    const auto saved = setter(caller);
    const auto absent = getter(true);
    caller.value.assign(expected.size(), 'x');
    if (!equal(saved, kind::string, expected) || !equal(setter(absent), kind::null_value) ||
        !equal(setter(result_type{}), kind::undefined) ||
        !equal(setter(result_type{std::string{}}), kind::string) || size() != 0) { return 91; }
    std::weak_ptr owner_lifetime = owner;
    std::weak_ptr table_lifetime = table;
    g_host.reset();
    owner.reset();
    table.reset();
    if (!owner_lifetime.expired() || !table_lifetime.expired() ||
        ctn_test_maps[0].expired()) { return 92; }
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 2 ||
        ctn_test_maps[0].lock() == ctn_test_maps[1].lock()) { return 93; }
    for (int index = 0; index < 128; ++index) {
        const std::string text = expected + std::to_string(index);
        auto input = result_type{text};
        auto returned = setter(input);
        input.value.assign(text.size(), 'q');
        const auto copied = returned;
        returned.value.assign(text.size(), 'z');
        if (!equal(copied, kind::string, text) || !equal(getter(false), kind::string, expected) ||
            !equal(getter(true), kind::null_value) || !equal(setter(absent), kind::null_value) ||
            !equal(setter(result_type{}), kind::undefined) ||
            !equal(setter(result_type{std::string{}}), kind::string) ||
            size() != 0 || g_host->slot->m_size() != 0) { return 94; }
    }
    const auto survivor = setter(getter(false));
    const auto null_survivor = setter(getter(true));
    const auto undefined_survivor = setter(result_type{});
    const auto empty_survivor = setter(result_type{std::string{}});
    getter = {};
    setter = {};
    if (ctn_test_maps[0].expired()) { return 95; }
    size = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired()) { return 96; }
    std::vector<std::string> churn;
    for (int index = 0; index < 4096; ++index) { churn.emplace_back(expected.size(), 'w'); }
    const auto fresh = g_host->slot->m_set(g_host->slot->m_get(false));
    g_host.reset();
    if (!ctn_test_maps[1].expired() || !equal(saved, kind::string, expected) ||
        !equal(survivor, kind::string, expected) || !equal(null_survivor, kind::null_value) ||
        !equal(undefined_survivor, kind::undefined) || !equal(empty_survivor, kind::string) ||
        !equal(fresh, kind::string, expected)) { return 97; }
    return 0;
}
'''
    changed = changed.replace("EXPECTED_STRING", json.dumps(STRING_RESULT))
    if name == "nullable_nested_result_saved":
        generated, separator, observer = changed.rpartition("\nint main() {\n")
        if not separator:
            raise RuntimeError("nested nullable observer lost its main function")
        # Exercise later same-method results with long caller strings and each
        # nullish tag after releasing the published owner. Only the appended
        # observer changes; emitted method bodies and helpers remain intact.
        for before, after in (
            ("const auto saved = setter(caller);", "const auto saved = setter(setter(caller));"),
            ("auto returned = setter(input);", "auto returned = setter(setter(input));"),
            ("const auto survivor = setter(getter(false));",
             "const auto survivor = setter(setter(getter(false)));"),
            ("const auto null_survivor = setter(getter(true));",
             "const auto null_survivor = setter(setter(getter(true)));"),
            ("const auto undefined_survivor = setter(result_type{});",
             "const auto undefined_survivor = setter(setter(result_type{}));"),
            ("const auto empty_survivor = setter(result_type{std::string{}});",
             "const auto empty_survivor = setter(setter(result_type{std::string{}}));"),
            ("const auto fresh = g_host->slot->m_set(g_host->slot->m_get(false));",
             "const auto fresh = g_host->slot->m_set(g_host->slot->m_set(g_host->slot->m_get(false)));"),
        ):
            if observer.count(before) != 1:
                raise RuntimeError("nested nullable observer lost an owning result call")
            observer = observer.replace(before, after)
        changed = generated + separator + observer
    if name == "nullable_host_result_saved":
        # This saved size callable itself consumes a nullable result. A fixed
        # distinct String entry observes both independent Maps without keeping
        # the overwritten payload alive or changing the setter's result.
        # Rewrite only the appended observer; generated helpers may contain
        # their own member size() calls and must remain byte-identical.
        generated, separator, observer = changed.rpartition("\nint main() {\n")
        if not separator:
            raise RuntimeError("nullable host-result observer lost its main function")
        size_call = 'size(result_type{std::string{"anchor"}})'
        observer, count = re.subn(r"(?<![.>\w])size\(\)", size_call, observer)
        if count != 2 or observer.count("g_host->slot->m_size()") != 1:
            raise RuntimeError("nullable host-result observer lost its three size calls")
        observer = observer.replace("g_host->slot->m_size()", "g_host->slot->m_" + size_call)
        observer = observer.replace(size_call + " != 0", size_call + " != 1")
        observer = observer.replace("    auto caller = getter(false);",
            "    static_assert(std::is_same_v<decltype(size), std::function<js_num(result_type)>>);\n"
            "    auto caller = getter(false);")
        observer = observer.replace("    for (int index = 0; index < 128; ++index) {",
            "    g_host->slot->m_set(g_host->slot->m_get(false));\n"
            "    for (int index = 0; index < 128; ++index) {")
        changed = generated + separator + observer
    source = args.work / f"{name}.{mode}.lifetime.cpp"
    source.write_text(changed)
    binary = (args.work / f"{name}.{mode}.sanitized").resolve()
    host.run([compiler, *owned.FLAGS, "-O1", "-g", "-fno-omit-frame-pointer",
              "-fsanitize=address,undefined", "-fsanitize-address-use-after-scope",
              str(source), "-o", str(binary)])
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60,
        env=dict(os.environ, ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
                 UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
    expected_trace = 1 if name == "nullable_host_result_saved" else 0
    if result.returncode or result.stdout != f"trace={expected_trace}\n" * 2:
        raise RuntimeError(f"{name}/{mode}: nullable stored-payload lifetime failure\n"
                           f"{result.stdout}{result.stderr}")


def standalone(args, output, name, value, compilers, nm):
    deduced = args.work / f"{name}.deduced.mlir"
    host.run([args.opt, str(output), "--ctnative-print-deduced", "-o", str(deduced)])
    for mode, ir in (("explicit", output), ("deduced", deduced)):
        cpp = host.run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
        size_signature = ("std::function<js_num(ctnative::nullable_string)>"
                          if name in nullable_host_result_sources() else "std::function<js_num()>")
        if (owned.VM.search(cpp) or "std::shared_ptr<ctn_slot>" not in cpp
                or size_signature not in cpp or "ctnative::map_size(" not in cpp
                or not re.search(r"std::shared_ptr<ctnative::method_\w+>\s+slot\s*;", cpp)):
            raise RuntimeError(f"{name}/{mode}: missing standalone Map/table/callable owners\n{cpp}")
        if name in parameter_sources():
            params = {"shared_parameter_number": "js_num", "shared_parameter_bool": "bool",
                      "shared_two_parameters": "std::string, js_num"}.get(name, "std::string")
            if f"std::function<js_num({params})>" not in cpp:
                raise RuntimeError(f"{name}/{mode}: missing typed setter arguments\n{cpp}")
        if name in RESULT_SIGNATURES:
            result, params, _ = RESULT_SIGNATURES[name]
            getter_params = "js_num" if name in {
                "result_formal", "result_seeded_formal", "seeded_dynamic_formal"} else ""
            if name in {**saved_join_sources(), **guarded_saved_sources(), **shortcircuit_sources(),
                        **nullable_result_sources(), **nullable_key_sources(),
                        **nullable_payload_sources(), **nullable_host_result_sources(), **nullable_nested_result_sources()}:
                getter_params = "bool"
            if name == "nullable_threeway":
                getter_params = "bool, bool"
            setter_result = "ctnative::nullable_string" if name in NULLABLE_PAYLOAD_READBACKS else "js_num"
            if (f"std::function<{result}({getter_params})>" not in cpp
                    or f"std::function<{setter_result}({params})>" not in cpp):
                raise RuntimeError(f"{name}/{mode}: missing typed producer/consumer signatures\n{cpp}")
            check_result_calls(cpp, name, mode)
        if name in payload_result_sources():
            payload = "std::string" if "string" in name else "bool"
            if f"std::shared_ptr<ctnative::map_storage<{payload}, {payload}>>" not in cpp:
                raise RuntimeError(f"{name}/{mode}: missing homogeneous owning Map carrier\n{cpp}")
        if name in MIXED_RESULT_TYPES:
            _, alternative = MIXED_RESULT_TYPES[name]
            variant = f"std::variant<bool, {alternative}>"
            key = {"result_seeded_mixed_contents": "double", "result_seeded_join_reseed": "double",
                   "result_seeded_bool_string_contents": "bool"}.get(name, variant)
            spellings = {key, key.replace("double", "js_num")}
            if not any(f"std::shared_ptr<ctnative::map_storage<{k}, {v}>>" in cpp
                       for k in spellings for v in {variant, variant.replace("double", "js_num")}):
                raise RuntimeError(f"{name}/{mode}: missing exact finite key/payload carrier\n{cpp}")
        source = args.work / f"{name}.{mode}.cpp"
        source.write_text(cpp)
        if name in {**nullable_result_sources(), **nullable_key_sources(), **nullable_payload_sources(),
                    **nullable_host_result_sources(), **nullable_nested_result_sources()}:
            key = "std::string" if name == "nullable_homogeneous_key" else "std::variant<bool, std::string>"
            if name in nullable_key_sources():
                key = "ctnative::nullable_string"
                if name == "nullable_key_identity_normalized":
                    key = "std::string"
                elif name in {"nullable_original_key", "nullable_second_key_use", "nullable_key_mixed"}:
                    key = "std::variant<bool, ctnative::nullable_string>"
            payload = "std::variant<bool, std::string>"
            if name in nullable_payload_sources():
                key = payload = "ctnative::nullable_string"
                if name in {"nullable_payload_mixed", "nullable_mixed_payload_readback"}:
                    key = payload = "std::variant<bool, ctnative::nullable_string>"
                elif name in {"nullable_payload_mixed_readback", "nullable_payload_mixed_identity",
                              "nullable_payload_mixed_saved"}:
                    payload = "std::variant<bool, ctnative::nullable_string>"
            if name in {**nullable_host_result_sources(), **nullable_nested_result_sources()}:
                key = "ctnative::nullable_string"
                payload = "std::variant<bool, ctnative::nullable_string>"
            if f"std::shared_ptr<ctnative::map_storage<{key}, {payload}>>" not in cpp:
                raise RuntimeError(f"{name}/{mode}: nullable signature changed the exact Map schema")
            source = args.work / f"{name}.{mode}.identity.cpp"
            source.write_text(nullable_identity_cpp(cpp, name))
        for index, compiler in enumerate(compilers):
            binary = (args.work / f"{name}.{mode}.{index}").resolve()
            host.run([compiler, *owned.FLAGS, str(source), "-o", str(binary)])
            if owned.VM.search(host.run([nm, "-C", str(binary)]).stdout):
                raise RuntimeError(f"{name}/{mode}: linked a VM symbol")
            if host.run([str(binary)]).stdout != f"trace={value}\n":
                raise RuntimeError(f"{name}/{mode}: standalone result mismatch")
        if name in {"ordinary", "mutate_map", "growing", "result_seeded_growing"}:
            lifetime(args, cpp, name, mode, value, compilers[1])
        if name in {"shared_growing", "shared_parameter"}:
            shared_lifetime(args, cpp, name, mode, compilers[1])
        if name in {"result_seeded_string_saved", "result_seeded_mixed_string_saved",
                    "saved_read_write_string_saved", "saved_join_string_saved",
                    "guarded_saved_string_saved", "shortcircuit_string_saved"}:
            string_payload_lifetime(args, cpp, name, mode, compilers[1])
        if name == "nullable_string_saved":
            nullable_payload_lifetime(args, cpp, name, mode, compilers[1])
        if name == "nullable_key_string_saved":
            nullable_key_lifetime(args, cpp, name, mode, compilers[1])
        if name in {"nullable_payload_saved", "nullable_payload_mixed_saved", "nullable_host_result_saved",
                    "nullable_nested_result_saved"}:
            nullable_stored_payload_lifetime(args, cpp, name, mode, compilers[1])


def check_call_preservation(original, output, name):
    if source_calls(original) != source_calls(output):
        raise RuntimeError(f"{name}: failed ownership changed live source call operands")
    if name.startswith(("saved_join", "guarded_saved", "shortcircuit", "nullable")):
        pattern = (r"^\s*(?:%[-\w.$]+(?::\d+)? = )?((?:ctjs\.(?:truthy|cond_br|br)|"
                   r"scf\.(?:if|yield))\b[^\n]*)")
        if re.findall(pattern, original, re.M) != re.findall(pattern, output, re.M):
            raise RuntimeError(f"{name}: failed ownership changed live branch/yield operands")


def check_prepared_result_calls(text, original, name):
    calls = re.findall(
        r"^\s*(%[-\w.$]+) = ctjs\.call_direct @([-\w.$]+)\(([^\n]+)\) "
        r"\{ctnative\.stored_call = 1 : i32\}", text, re.M)
    pairs = 2 if name.startswith("nullable") else 1
    getter_arguments = 5 if name.startswith("nullable") else 4
    if (len(source_calls(text)) != len(source_calls(original))
            or [target for _, target, _ in calls] != ["fn$4", "fn$5"] * pairs + ["fn$3"]
            or [len(arguments.split(", ")) for _, _, arguments in calls]
            != [getter_arguments, 5] * pairs + [4]
            or any(calls[index + 1][2].split(", ")[-1] != calls[index][0]
                   for index in range(0, pairs * 2, 2))
            or f'ctjs.store_global "trace", {calls[-1][0]}' not in text):
        raise RuntimeError(f"{name}: carrier refusal lost the prepared live result edge")


def forge_map_presence(text, payload="bool"):
    if payload not in {"bool", "string", "nullable_string"}:
        raise ValueError("forged presence needs a valid read tag")
    # Nullable alternatives are a read proof, not a scalar write/key proof.
    # Forge each accepted vocabulary independently, so parsing cannot reject
    # the control before its live read/presence evidence is rederived.
    scalar = "string" if payload == "nullable_string" else payload
    marked, count = re.subn(r"(^\s*%[-\w.$]+ = ctjs\.call [^\n{]+)(\{)?",
        lambda match: match[1].rstrip() + " {ctnative.map_present = true, ctnative.map_read_type = \""
                      + payload + "\", ctnative.map_write_type = \"" + scalar + "\""
                      + ", ctnative.map_key_type = \"" + scalar + "\""
                      + (", " if match[2] else "}"), methods.forge_reports(text), flags=re.M)
    if count == 0:
        raise RuntimeError("forged-presence control lost every live Map call")
    return marked


def check_budgets(args, ir, config, name, functions=4):
    original = ir.read_text()
    signatures = re.findall(r"^\s*ctjs\.func (.*?) -> !ctjs.value attributes \{.*?"
                            r"upvalue_count = (\d+) : i32", original, re.M)
    operations = ("create_object", "create_cell", "cell_set", "create_closure", "load_upvalue",
                  "construct", "set_property", "get_property", "call", "call_direct", "store_global")
    counts = {op: len(re.findall(rf"\bctjs\.{op}\b", original)) for op in operations}
    checked = {}
    rollback = []

    def admitted(budget):
        if budget in checked:
            return checked[budget]
        output = owned.lower(args, ir, f"{name}-budget-{budget}", config,
                             options=f"host-max-steps={budget}", cleanup=False)
        text = methods.census(output, functions, name)
        native = len(boundary.NATIVE.findall(text))
        if native not in (0, functions):
            raise RuntimeError(f"{name}/{budget}: published an incomplete native component")
        if native == 0:
            if re.findall(r"^\s*ctjs\.func (.*?) -> !ctjs.value attributes \{.*?"
                          r"upvalue_count = (\d+) : i32", text, re.M) != signatures:
                raise RuntimeError(f"{name}/{budget}: leaked speculative capture/signature rewrites")
            check_call_preservation(original, text, f"{name}/{budget}")
            for op, count in counts.items():
                if len(re.findall(rf"\bctjs\.{op}\b", text)) != count:
                    raise RuntimeError(f"{name}/{budget}: leaked speculative {op} rewrites")
            if "ctnative.host_owner_proved = true" in text:
                rollback.append(budget)
        checked[budget] = native == functions
        return checked[budget]

    low, high = 0, 100000
    if not admitted(high):
        raise RuntimeError(f"{name}: budget witness never admits its complete component")
    while low < high:
        middle = (low + high) // 2
        if admitted(middle):
            high = middle
        else:
            low = middle + 1
    for budget in range(max(0, high - 16), high + 1):
        if admitted(budget) != (budget >= high):
            raise RuntimeError(f"{name}: inconsistent admission at the measured budget boundary")
    print(f"{name}: first complete budget {high}; {len(checked)} cutoffs checked; "
          f"{len(rollback)} discard a speculative rewrite after original owner proof")
    return rollback
