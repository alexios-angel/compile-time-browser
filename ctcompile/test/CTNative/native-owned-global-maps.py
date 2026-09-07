#!/usr/bin/env python3
"""Execute checked published Map methods and live primitive results without the VM."""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


spec = importlib.util.spec_from_file_location(
    "methods", Path(__file__).with_name("native-owned-global-methods.py"))
methods = importlib.util.module_from_spec(spec)
spec.loader.exec_module(methods)
owned, boundary, host = methods.owned, methods.boundary, methods.host
SOURCE = Path(__file__).with_name("native-export-boundary.js").read_text()
SHARED = SOURCE.replace("get() { return state.size; }",
    "get() { return state.size; }, set() { state.set('x', 1); return state.size; }")
SHARED = SHARED.replace("var trace = host.slot.get();",
    "host.slot.set(); var trace = host.slot.get();")
PARAMETER = SHARED.replace("set() { state.set('x', 1)", "set(key) { state.set(key, 1)")
PARAMETER = PARAMETER.replace("host.slot.set();", "host.slot.set('x');")
CALL_RESULT = PARAMETER.replace("host.slot.set('x');", "host.slot.set(host.slot.get());")


def parameter_sources():
    return {
        "shared_parameter": (PARAMETER, "host", 1),
        "shared_parameter_repeated": (PARAMETER.replace("host.slot.set('x');",
            "host.slot.set('x'); host.slot.set('y'); host.slot.set('x');"), "host", 2),
        "shared_parameter_number": (PARAMETER.replace("host.slot.set('x');",
            "host.slot.set(1); host.slot.set(2); host.slot.set(1);"), "host", 2),
        "shared_parameter_bool": (PARAMETER.replace("host.slot.set('x');",
            "host.slot.set(true); host.slot.set(false); host.slot.set(true);"), "host", 2),
        "shared_parameter_alias": (PARAMETER.replace("set(key) { state.set(key, 1)",
            "set(key) { const alias = key; state.set(alias, 1)"), "host", 1),
        "shared_two_parameters": (PARAMETER.replace("set(key) { state.set(key, 1)",
            "set(key, value) { state.set(key, value)").replace("host.slot.set('x');",
            "host.slot.set('x', 1); host.slot.set('y', 2); host.slot.set('x', 3);"), "host", 2),
    }


def result_sources():
    observed_size = CALL_RESULT.replace("get() { return state.size; },",
        "size() { return state.size; }, get() { return state.has(false); },")
    observed_size = observed_size.replace("var trace = host.slot.get();",
                                          "var trace = host.slot.size();")
    repeated_bool = observed_size.replace("host.slot.set(host.slot.get());",
        "host.slot.set(host.slot.get()); host.slot.set(host.slot.get());")
    return {
        "parameter_call_result": (CALL_RESULT, "host", 1),
        "result_reverse_members": (CALL_RESULT.replace(
            "get() { return state.size; }, set(key) { state.set(key, 1); return state.size; }",
            "set(key) { state.set(key, 1); return state.size; }, get() { return state.size; }"),
            "host", 1),
        "result_repeated": (CALL_RESULT.replace("host.slot.set(host.slot.get());",
            "host.slot.set(host.slot.get()); host.slot.set(host.slot.get());"), "host", 2),
        "result_alias": (CALL_RESULT.replace("get() { return state.size; }",
            "get() { const result = state.size; return result; }")
            .replace("set(key) { state.set(key, 1)",
                     "set(key) { const alias = key; state.set(alias, 1)"), "host", 1),
        # JS evaluates the two actuals left to right. The first inserts key 0
        # and yields 1; the second inserts key 1 and yields 2. The setter must
        # overwrite key 1. Reversing the actuals instead inserts key 2, making
        # the final growing getter return 4 rather than 3.
        "result_argument_order": (CALL_RESULT.replace("get() { return state.size; }",
            "get() { state.set(state.size, 1); return state.size; }")
            .replace("set(key) { state.set(key, 1)", "set(key, value) { state.set(key, value)")
            .replace("host.slot.set(host.slot.get());",
                     "host.slot.set(host.slot.get(), host.slot.get());"), "host", 3),
        "result_bool": (repeated_bool, "host", 2),
        "result_delete": (observed_size.replace("state.has(false)", "state.delete(false)")
            .replace("host.slot.set(host.slot.get());",
                     "host.slot.set(host.slot.get()); " * 3), "host", 2),
        "result_string": (observed_size.replace("return state.has(false);",
            "state.set('produced', 1); return '" + "result-key-" * 12 + "';"), "host", 2),
        "result_formal": (observed_size.replace("get() { return state.has(false); }",
            "get(key) { state.has(key); return key; }")
            .replace("host.slot.set(host.slot.get());", "host.slot.set(host.slot.get(7));"),
            "host", 1),
    }


RESULT_SIGNATURES = {
    "parameter_call_result": ("js_num", "js_num", 5),
    "result_reverse_members": ("js_num", "js_num", 5),
    "result_repeated": ("js_num", "js_num", 5),
    "result_alias": ("js_num", "js_num", 5),
    "result_argument_order": ("js_num", "js_num, js_num", 5),
    "result_bool": ("bool", "bool", 6),
    "result_delete": ("bool", "bool", 6),
    "result_string": ("std::string", "std::string", 6),
    "result_formal": ("js_num", "js_num", 6),
}


def check_result_calls(cpp, name, mode):
    entry = re.search(r"\bmain\(\)\s*\{(.*?)^\}", cpp, re.M | re.S)
    if not entry:
        raise RuntimeError(f"{name}/{mode}: missing native entry for result-call census")
    methods_by_value = dict(re.findall(
        r"(\w+)\s*=\s*ctnative::method_get<&[^>\n]+::m_(\w+)>\(", entry[1]))
    calls = re.findall(
        r"(?:\b(\w+)\s*=\s*)?ctnative::invoke_callable\((\w+)([^;\n]*)\);", entry[1])
    sequence, pending = [], []
    for result, callee, arguments in calls:
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
            if not pending or actuals != pending:
                raise RuntimeError(f"{name}/{mode}: setter lost live producing-call operands/order")
            pending.clear()
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
    }[name]
    if sequence != expected or "ctnative::map_set(" not in cpp:
        raise RuntimeError(f"{name}/{mode}: lost runtime getter/mutation/final observation calls")


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
    growing = name == "growing"
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


def standalone(args, output, name, value, compilers, nm):
    deduced = args.work / f"{name}.deduced.mlir"
    host.run([args.opt, str(output), "--ctnative-print-deduced", "-o", str(deduced)])
    for mode, ir in (("explicit", output), ("deduced", deduced)):
        cpp = host.run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
        if (owned.VM.search(cpp) or "std::shared_ptr<ctn_slot>" not in cpp
                or "std::function<js_num()>" not in cpp or "ctnative::map_size(" not in cpp
                or not re.search(r"std::shared_ptr<ctnative::method_\w+>\s+slot\s*;", cpp)):
            raise RuntimeError(f"{name}/{mode}: missing standalone Map/table/callable owners\n{cpp}")
        if name in parameter_sources():
            params = {"shared_parameter_number": "js_num", "shared_parameter_bool": "bool",
                      "shared_two_parameters": "std::string, js_num"}.get(name, "std::string")
            if f"std::function<js_num({params})>" not in cpp:
                raise RuntimeError(f"{name}/{mode}: missing typed setter arguments\n{cpp}")
        if name in RESULT_SIGNATURES:
            result, params, _ = RESULT_SIGNATURES[name]
            getter_params = "js_num" if name == "result_formal" else ""
            if (f"std::function<{result}({getter_params})>" not in cpp
                    or f"std::function<js_num({params})>" not in cpp):
                raise RuntimeError(f"{name}/{mode}: missing typed producer/consumer signatures\n{cpp}")
            check_result_calls(cpp, name, mode)
        source = args.work / f"{name}.{mode}.cpp"
        source.write_text(cpp)
        for index, compiler in enumerate(compilers):
            binary = (args.work / f"{name}.{mode}.{index}").resolve()
            host.run([compiler, *owned.FLAGS, str(source), "-o", str(binary)])
            if owned.VM.search(host.run([nm, "-C", str(binary)]).stdout):
                raise RuntimeError(f"{name}/{mode}: linked a VM symbol")
            if host.run([str(binary)]).stdout != f"trace={value}\n":
                raise RuntimeError(f"{name}/{mode}: standalone result mismatch")
        if name in {"ordinary", "mutate_map", "growing"}:
            lifetime(args, cpp, name, mode, value, compilers[1])
        if name in {"shared_growing", "shared_parameter"}:
            shared_lifetime(args, cpp, name, mode, compilers[1])


def refusal_sources():
    return {
        "mutable_capture": SOURCE.replace("return {", "state = new Map(); return {"),
        "cyclic_payload": SOURCE.replace("return state.size;", "state.set('x', state); return state.size;"),
        "object_payload": SOURCE.replace("return state.size;", "state.set('x', {}); return state.size;"),
        "map_key": SOURCE.replace("return state.size;", "state.set(state, 1); return state.size;"),
        "detached_method": SOURCE.replace("return state.size;", "var set = state.set; set('x', 1); return state.size;"),
        "set_wrong_arity": SOURCE.replace("return state.size;", "state.set('x'); return state.size;"),
        "get_wrong_arity": SOURCE.replace("return state.size;", "state.get('x', 1); return state.size;"),
        "method_escape": SOURCE.replace("return state.size;", "return state.set;"),
        "map_return": SOURCE.replace("return state.size;", "return state.set('x', 1);"),
        "method_replaced": SOURCE.replace("return state.size;", "state.set = 1; return state.size;"),
        "snapshot": SOURCE.replace("return state.size;", "state.keys(); return state.size;"),
        "published_map": SOURCE.replace("return {", "host.map = state; return {"),
        "map_alias": SOURCE.replace("return {", "var saved = state; return {"),
        "replaced_map": "Map = function() {};\n" + SOURCE,
        "late_replaced_map": SOURCE + "\nMap = function() {};",
        "map_prototype": SOURCE.replace("const state", "Map.prototype.extra = 1; const state"),
        "constructed_args": SOURCE.replace("new Map()", "new Map([])"),
        "second_map": SOURCE.replace("return {", "new Map(); return {"),
        "second_factory": SOURCE.replace("host.slot = factory();", "host.slot = factory(); factory();"),
        "published_owner": SOURCE + "\nvar alias = host;",
        "published_table": SOURCE + "\nvar alias = host.slot;",
        "published_callable": SOURCE + "\nvar alias = host.slot.get;",
        "table_replaced": SOURCE + "\nhost.slot = {};",
        "getter_replaced": SOURCE + "\nhost.slot.get = function() { return 7; };",
        "receiver": SOURCE.replace("return state.size;", "return this;"),
        "arguments": SOURCE.replace("return state.size;", "return arguments.length;"),
        "call_argument": SOURCE.replace("host.slot.get();", "host.slot.get(1);"),
        "effect": "var side = 0;\n" + SOURCE.replace("return state.size;", "side = 1; return state.size;"),
        "throw": SOURCE.replace("return state.size;", "throw 7;"),
        "unknown": SOURCE + "\ninspect(host);",
    }


def parameter_refusals():
    return {
        "parameter_missing": PARAMETER + "\nhost.slot.set();",
        "parameter_extra": PARAMETER + "\nhost.slot.set('y', 1);",
        "parameter_heterogeneous": PARAMETER + "\nhost.slot.set(1);",
        "parameter_object": PARAMETER.replace("host.slot.set('x');", "host.slot.set({});"),
        "parameter_callback": PARAMETER.replace("host.slot.set('x');",
            "host.slot.set(function() { return 'x'; });"),
        "parameter_unproved": PARAMETER.replace("host.slot.set('x');", "host.slot.set(host.key);"),
        "parameter_getter_extra": PARAMETER.replace("host.slot.get();", "host.slot.get('x');"),
        "parameter_second_missing": parameter_sources()["shared_two_parameters"][0]
                                    + "\nhost.slot.set('z');",
        "parameter_second_heterogeneous": parameter_sources()["shared_two_parameters"][0]
                                          + "\nhost.slot.set('z', true);",
    }


def result_refusals():
    return {
        "result_unknown_map_get": CALL_RESULT.replace("get() { return state.size; }",
            "get() { return state.get(0); }"),
        "result_seeded_map_get": CALL_RESULT.replace("get() { return state.size; }",
            "get() { state.set(0, 1); return state.get(0); }"),
        "result_unknown_effect": CALL_RESULT.replace("get() { return state.size; }",
            "get() { inspect(state); return state.size; }"),
        "result_unknown_call": CALL_RESULT.replace("get() { return state.size; }",
            "get() { state.has(0); return inspect(); }"),
        "result_recursive": CALL_RESULT.replace("get() { return state.size; }",
            "get() { state.has(0); return host.slot.get(); }"),
        "result_mixed_return": CALL_RESULT.replace("get() { return state.size; }",
            "get() { if (state.has(0)) { return 1; } return false; }"),
        "result_mixed_actual": CALL_RESULT + "\nhost.slot.set('x');",
        "result_missing_actual": CALL_RESULT.replace("get() { return state.size; }",
            "get(key) { state.has(key); return state.size; }"),
        "result_map_return": CALL_RESULT.replace("get() { return state.size; }",
            "get() { return state; }"),
    }


def check_call_preservation(original, output, name):
    if source_calls(original) != source_calls(output):
        raise RuntimeError(f"{name}: failed ownership changed live source call operands")


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
    }
    saved = {}
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
        "nullable_key": ("state.set(1, 2); state.set(1, 3); state.set(state.get(1), 4); "
                         "state.delete(3); return state.size;"),
    }.items():
        js, rejected, _ = boundary.prepare(args, name, SOURCE.replace("return state.size;", body))
        if name == "nullable_key" and (
                host.run([node, "-e", boundary.NODE, str(js)]).stdout != "trace=1\n"
                or host.run([str(reference), str(js)]).stdout != "trace=1\n"):
            raise RuntimeError("nullable_key: Node/interpreter observation mismatch")
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
            forged.write_text(methods.forge_reports(rejected.read_text()))
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
            forged.write_text(methods.forge_reports(rejected.read_text()))
            forged_config = contract(args, forged, "result-forged")
            failed = methods.refused(args, forged, "result-forged", forged_config, admitted=0)
            check_call_preservation(forged.read_text(), failed.read_text(), "result-forged")
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
    print(f"native captured Map ownership: {len(positives)} complete programs (4/4, 5/5, 6/6); "
          "Node/interpreter/GCC/Clang explicit+deduced and Map/table/callable lifetime pass; "
          f"{len(refusal_sources())} source refusals and contract/rerun/budget controls pass; "
          f"three carrier refusals and {len(shared_refusals)} shared-method refusals; "
          f"{len(parameter_refusals())} argument refusals preserve current call operands; "
          "typed parameterized setters 5/5 with changing source and saved-callable keys; "
          f"{len(result_sources())} live result programs preserve call order and operands; "
          f"{len(result_refusals())} result-proof refusals and missing-return carrier refusal; "
          f"{len(rollback)} speculative rollback cutoffs")


if __name__ == "__main__":
    main()
