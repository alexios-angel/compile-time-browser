#!/usr/bin/env python3
"""Execute the checked four-function captured Map publication without the VM."""

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


def standalone(args, output, name, value, compilers, nm):
    deduced = args.work / f"{name}.deduced.mlir"
    host.run([args.opt, str(output), "--ctnative-print-deduced", "-o", str(deduced)])
    for mode, ir in (("explicit", output), ("deduced", deduced)):
        cpp = host.run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
        if (owned.VM.search(cpp) or "std::shared_ptr<ctn_slot>" not in cpp
                or "std::function<js_num()>" not in cpp or "ctnative::map_size(" not in cpp
                or not re.search(r"std::shared_ptr<ctnative::method_\w+>\s+slot\s*;", cpp)):
            raise RuntimeError(f"{name}/{mode}: missing standalone Map/table/callable owners\n{cpp}")
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


def check_budgets(args, ir, config, name):
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
        text = methods.census(output, 4, name)
        native = len(boundary.NATIVE.findall(text))
        if native not in (0, 4):
            raise RuntimeError(f"{name}/{budget}: published an incomplete native component")
        if native == 0:
            if re.findall(r"^\s*ctjs\.func (.*?) -> !ctjs.value attributes \{.*?"
                          r"upvalue_count = (\d+) : i32", text, re.M) != signatures:
                raise RuntimeError(f"{name}/{budget}: leaked speculative capture/signature rewrites")
            for op, count in counts.items():
                if len(re.findall(rf"\bctjs\.{op}\b", text)) != count:
                    raise RuntimeError(f"{name}/{budget}: leaked speculative {op} rewrites")
            if "ctnative.host_owner_proved = true" in text:
                rollback.append(budget)
        checked[budget] = native == 4
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
    }
    saved = {}
    for name, (source, binding, value) in positives.items():
        js, ir, count = boundary.prepare(args, name, source)
        if count != 4:
            raise RuntimeError(f"{name}: lost the four-function source chain")
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
        text = methods.census(output, 4, name, admitted=4)
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

    # The next publication boundary needs a complete multi-method capture
    # graph. Keep both runtime methods and their shared mutable Map visible.
    multiple = SOURCE.replace("get() { return state.size; }",
        "get() { return state.size; }, set() { state.set('x', 1); return state.size; }")
    multiple = multiple.replace("var trace = host.slot.get();",
        "host.slot.set(); var trace = host.slot.get();")
    js, rejected, count = boundary.prepare(args, "multiple_methods", multiple)
    if count != 5 or (host.run([node, "-e", boundary.NODE, str(js)]).stdout != "trace=1\n"
            or host.run([str(reference), str(js)]).stdout != "trace=1\n"):
        raise RuntimeError("multiple_methods: lost the source chain or shared Map result")
    fresh = contract(args, rejected, "multiple_methods")
    methods.refused(args, rejected, "multiple_methods", fresh, admitted=0)
    print(f"native captured Map ownership: {len(positives)} programs at 4/4; "
          "Node/interpreter/GCC/Clang explicit+deduced and Map/table/callable lifetime pass; "
          f"{len(refusal_sources())} source refusals and contract/rerun/budget controls pass; "
          "three carrier refusals; next multi-method boundary 0/5 with Node/interpreter trace=1; "
          f"{len(rollback)} speculative rollback cutoffs")


if __name__ == "__main__":
    main()
