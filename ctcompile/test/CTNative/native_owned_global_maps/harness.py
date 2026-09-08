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
    RESULT_SIGNATURES,
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
        "result_seeded_map_get": ["get", "set", "get"],
        "result_seeded_repeated": ["get", "set", "get", "set", "get"],
        "result_seeded_overwrite": ["get", "set", "get"],
        "result_seeded_growing": ["get", "set", "get"],
        "result_seeded_formal": ["get", "set", "get", "set", "size"],
        **{name: ["get", "set", "get"] for name in key_fact_sources()},
        **{name: ["get", "set", "get"] for name in joined_result_sources()},
        **{name: ["get", "set", "get"] for name in size_result_sources()},
        **{name: ["get", "set", "size"] for name in payload_result_sources()},
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
              **size_result_sources(), **payload_result_sources()}
    if name in seeded and not re.search(r"ctnative::map_get(?:_\w+)?\(", cpp):
        raise RuntimeError(f"{name}/{mode}: replaced the live seeded Map lookup with a summary")
    if name in size_result_sources() and "ctnative::map_delete(" not in cpp:
        raise RuntimeError(f"{name}/{mode}: dropped the live size-keyed deletion")
    if name == "result_seeded_string_saved" and "ctnative::map_delete(" not in cpp:
        raise RuntimeError(f"{name}/{mode}: dropped the saved string's source deletion")


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
    if (ctnative_test_entry() != 0 || ctn_test_maps.size() != 1) { return 90; }
    auto owner = g_host;
    auto table = owner->slot;
    auto getter = table->m_get;
    auto setter = table->m_set;
    auto size = table->m_size;
    static_assert(std::is_same_v<decltype(getter), std::function<std::string()>>);
    static_assert(std::is_same_v<decltype(setter), std::function<js_num(std::string)>>);
    auto saved = getter();
    if (saved != expected || setter(saved) != 1) { return 91; }
    saved.assign(expected.size(), 'x');
    if (setter(expected) != 1) { return 92; }
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
        if (getter() != expected || setter("saved-" + std::to_string(index)) != index + 2 ||
            size() != index + 2 || g_host->slot->m_size() != 1) { return 95; }
    }
    auto survivor = getter();
    getter = {};
    setter = {};
    if (ctn_test_maps[0].expired()) { return 96; }
    size = {};
    if (!ctn_test_maps[0].expired() || ctn_test_maps[1].expired()) { return 97; }
    std::vector<std::string> churn;
    for (int index = 0; index < 4096; ++index) {
        churn.emplace_back(expected.size(), 'q');
    }
    if (survivor != expected || g_host->slot->m_get() != expected) { return 98; }
    g_host.reset();
    if (!ctn_test_maps[1].expired() || survivor != expected) { return 99; }
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
    if result.returncode or result.stdout != "trace=1\n" * 2:
        raise RuntimeError(f"{name}/{mode}: saved string lifetime failure\n"
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
            getter_params = "js_num" if name in {
                "result_formal", "result_seeded_formal", "seeded_dynamic_formal"} else ""
            if (f"std::function<{result}({getter_params})>" not in cpp
                    or f"std::function<js_num({params})>" not in cpp):
                raise RuntimeError(f"{name}/{mode}: missing typed producer/consumer signatures\n{cpp}")
            check_result_calls(cpp, name, mode)
        if name in payload_result_sources():
            payload = "std::string" if "string" in name else "bool"
            if f"std::shared_ptr<ctnative::map_storage<{payload}, {payload}>>" not in cpp:
                raise RuntimeError(f"{name}/{mode}: missing homogeneous owning Map carrier\n{cpp}")
        source = args.work / f"{name}.{mode}.cpp"
        source.write_text(cpp)
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
        if name == "result_seeded_string_saved":
            string_payload_lifetime(args, cpp, name, mode, compilers[1])


def check_call_preservation(original, output, name):
    if source_calls(original) != source_calls(output):
        raise RuntimeError(f"{name}: failed ownership changed live source call operands")


def check_prepared_result_calls(text, original, name):
    calls = re.findall(
        r"^\s*(%[-\w.$]+) = ctjs\.call_direct @([-\w.$]+)\(([^\n]+)\) "
        r"\{ctnative\.stored_call = 1 : i32\}", text, re.M)
    if (len(source_calls(text)) != len(source_calls(original))
            or [target for _, target, _ in calls] != ["fn$4", "fn$5", "fn$3"]
            or [len(arguments.split(", ")) for _, _, arguments in calls] != [4, 5, 4]
            or calls[1][2].split(", ")[-1] != calls[0][0]
            or f'ctjs.store_global "trace", {calls[2][0]}' not in text):
        raise RuntimeError(f"{name}: carrier refusal lost the prepared live result edge")


def forge_map_presence(text):
    marked, count = re.subn(r"(^\s*%[-\w.$]+ = ctjs\.call [^\n{]+)(\{)?",
        lambda match: match[1].rstrip() + " {ctnative.map_present = true"
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
