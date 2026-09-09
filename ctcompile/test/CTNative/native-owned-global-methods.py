#!/usr/bin/env python3
"""Check live exported getter ownership through standalone native execution."""

import argparse
import importlib.util
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess


spec = importlib.util.spec_from_file_location(
    "owned", Path(__file__).with_name("native-owned-globals.py"))
owned = importlib.util.module_from_spec(spec)
spec.loader.exec_module(owned)
boundary = owned.boundary
host = owned.host

SOURCE = ("var host = {}; function make() { return {get() { return 42; }}; } "
          "host.slot = make(); var trace = host.slot.get();")
POSITIVES = {
    "ordinary": (SOURCE, "host", "trace=42\n"),
    "already_resolved": (SOURCE, "host", "trace=42\n"),
    "legacy_store_marker": (SOURCE, "host", "trace=42\n"),
    "legacy_field_marker": (SOURCE, "host", "trace=42\n"),
    "repeated": (SOURCE + " trace = host.slot.get();", "host", "trace=42\n"),
    "fraction": (SOURCE.replace("return 42;", "return 0.125;"), "host", "trace=0.125\n"),
    "initialized": ("function make() { return {get() { return 42; }}; } "
                    "var host = {slot: make()}; var trace = host.slot.get();",
                    "host", "trace=42\n"),
    "ordinary_window": (SOURCE.replace("host", "window"), "window", "trace=42\n"),
    "typed-boolean": (SOURCE.replace("return 42;", "return true;"), "host", "trace=true\n"),
    "typed-boolean-false": (SOURCE.replace("return 42;", "return false;"), "host", "trace=false\n"),
}

BOOLEAN_NODE = r'''
const fs = require('fs'), vm = require('vm');
const context = vm.createContext({});
vm.runInContext(fs.readFileSync(process.argv[1], 'utf8'), context);
const trace = vm.runInContext('trace', context);
if (typeof trace !== 'boolean') throw new Error('non-boolean trace');
process.stdout.write('trace=' + String(trace) + '\n');
'''


def resolve_getter(args, ir):
    # The source query already supports a current direct call. Preserve its
    # actual receiver and callee while making only that resolution explicit.
    text = ir.read_text()
    targets = re.findall(r"^\s*ctjs\.func (?:private )?@([^\s(]+)", text, re.M)
    undefined = re.search(r"(%[-\w.$]+) = ctjs\.constant #ctjs\.undefined", text)
    if len(targets) != 3 or not undefined:
        raise RuntimeError("direct-call fixture lost its source functions or undefined value")
    target = targets[2]
    text, count = re.subn(r"ctjs\.call (%[-\w.$]+)\((%[-\w.$]+)\)",
        lambda call: f"ctjs.call_direct @{target}({call[2]}, {undefined[1]}, {call[1]})", text)
    if count != 1:
        raise RuntimeError("direct-call fixture did not resolve exactly one getter invocation")
    output = args.work / "already_resolved.direct.mlir"
    output.write_text(text)
    return output


def legacy_marker(args, ir, name):
    # A current fingerprint authenticates this source, not old native facts.
    # The legacy marker would erase the observation or omit the owning field
    # from the shape census if preparation failed to reconstruct its facts.
    pattern = (r'ctjs\.store_global "trace", %[-\w.$]+' if name == "legacy_store_marker"
               else r'ctjs\.set_property %[-\w.$]+\[%[-\w.$]+\], %[-\w.$]+')
    text, count = re.subn(pattern, lambda match: match[0] + " {ctnative.method}",
                         ir.read_text(), count=1)
    if count != 1:
        raise RuntimeError(f"{name}: failed to mark its exact source operation")
    output = args.work / f"{name}.marked.mlir"
    output.write_text(text)
    return output


def census(ir, denominator, name, *, admitted=None):
    text = ir.read_text()
    remaining = len(boundary.FUNCTION.findall(text))
    native = len(boundary.NATIVE.findall(text))
    if (remaining + native != denominator
            or len(boundary.REFUSAL.findall(text)) != remaining):
        raise RuntimeError(f"{name}: changed source function accounting\n{text}")
    if admitted is not None and native != admitted:
        raise RuntimeError(f"{name}: expected {admitted}/{denominator} native, got {native}\n{text}")
    return text


def refused(args, ir, name, config, *, options="", reason=None, admitted=None):
    denominator = len(boundary.FUNCTION.findall(ir.read_text()))
    denominator += len(boundary.NATIVE.findall(ir.read_text()))
    output = owned.lower(args, ir, name, config, options=options, cleanup=False)
    text = census(output, denominator, name, admitted=admitted)
    if (re.search(r"\bemitc\.func @main\(", text)
            or "ctnative.host_owner_proved = false" not in text):
        raise RuntimeError(f"{name}: an invalid source graph supplied native ownership\n{text}")
    if reason and reason not in text:
        raise RuntimeError(f"{name}: missing refusal {reason!r}\n{text}")
    return output


def lifetime(args, cpp, name, mode, expected, compiler):
    # Retain owner, table and callable independently after the script entry
    # returns. Weak witnesses ensure that each heap owner actually dies when
    # its final strong handle is released; a leak cannot satisfy this test.
    changed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", cpp)
    if count != 1:
        raise RuntimeError("lifetime harness needs exactly one generated entry")
    changed += r'''
int main() {
    if (ctnative_test_entry() != 0) { return 90; }
    auto first = g_host;
    auto table = first->slot;
    auto callable = table->m_get;
    std::weak_ptr first_lifetime = first;
    std::weak_ptr table_lifetime = table;
    g_host.reset();
    for (int index = 0; index < 4096; ++index) {
        auto churn = std::make_shared<ctn_slot>();
        churn->slot = std::make_shared<typename decltype(table)::element_type>();
    }
    if (first_lifetime.expired() || table_lifetime.expired()) { return 91; }
    for (int index = 0; index < 1024; ++index) {
        if (first->slot->m_get() != 42 || table->m_get() != 42 || callable() != 42) {
            return 92;
        }
    }
    if (ctnative_test_entry() != 0) { return 93; }
    if (first == g_host || table == g_host->slot || g_host->slot->m_get() != 42) {
        return 94;
    }
    first.reset();
    if (!first_lifetime.expired() || table_lifetime.expired() || table->m_get() != 42) {
        return 95;
    }
    table.reset();
    if (!table_lifetime.expired() || callable() != 42) { return 96; }
    callable = {};
    std::weak_ptr second_lifetime = g_host;
    std::weak_ptr second_table_lifetime = g_host->slot;
    g_host.reset();
    if (!second_lifetime.expired() || !second_table_lifetime.expired()) { return 97; }
    return 0;
}
'''
    source = args.work / f"{name}.{mode}.lifetime.cpp"
    source.write_text(changed)
    binary = (args.work / f"{name}.{mode}.sanitized").resolve()
    host.run([compiler, *owned.FLAGS, "-O1", "-g", "-fno-omit-frame-pointer",
              "-fsanitize=address,undefined", "-fsanitize-address-use-after-scope",
              str(source), "-o", str(binary)])
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60,
        env=dict(os.environ, ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
                 UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
    if result.returncode or result.stdout != expected * 2:
        raise RuntimeError(f"{name}/{mode}: lifetime failure\n{result.stdout}{result.stderr}")


def standalone(args, output, name, expected, compilers, nm, *, result_type="js_num"):
    deduced = args.work / f"{name}.deduced.mlir"
    host.run([args.opt, str(output), "--ctnative-print-deduced", "-o", str(deduced)])
    for mode, ir in (("explicit", output), ("deduced", deduced)):
        cpp = host.run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
        if (owned.VM.search(cpp) or re.search(r"#include [<\"]ctbrowser/", cpp)
                or "std::shared_ptr<ctn_slot>" not in cpp
                or not re.search(r"std::shared_ptr<ctnative::method_\w+>\s+slot\s*;", cpp)
                or f"std::function<{result_type}()>" not in cpp):
            raise RuntimeError(f"{name}/{mode}: missing standalone owning table/callable carriers\n{cpp}")
        if result_type == "bool" and ("ctnative::global_boolean(" not in cpp
                                      or "ctnative::invoke_callable(" not in cpp):
            raise RuntimeError(f"{name}/{mode}: missing live Boolean call/observation\n{cpp}")
        source = args.work / f"{name}.{mode}.cpp"
        source.write_text(cpp)
        for index, compiler in enumerate(compilers):
            binary = (args.work / f"{name}.{mode}.{index}").resolve()
            host.run([compiler, *owned.FLAGS, str(source), "-o", str(binary)])
            if owned.VM.search(host.run([nm, "-C", str(binary)]).stdout):
                raise RuntimeError(f"{name}/{mode}: linked VM symbols")
            if host.run([str(binary)]).stdout != expected:
                raise RuntimeError(f"{name}/{mode}: standalone result mismatch")
        if name == "ordinary":
            lifetime(args, cpp, name, mode, expected, compilers[1])


def refusal_sources():
    prefix = "var host = {}; function make() { return {get() { return 42; }}; } "
    suffix = " var trace = host.slot.get();"
    return {
        "missing": prefix + suffix,
        "early_read": prefix + suffix + " host.slot = make();",
        "replacement": prefix + "host.slot = make(); host.slot = make();" + suffix,
        "root_replacement": prefix + "host.slot = make(); host = {};" + suffix,
        "owner_extra_field": prefix + "host.slot = make(); host.extra = 7;" + suffix,
        "table_extra_field": SOURCE.replace("return {get()", "return {extra: 7, get()"),
        "replaced_getter": prefix + "host.slot = make(); "
                           "host.slot.get = function() { return 43; };" + suffix,
        "published_owner": prefix + "host.slot = make(); var alias = host;" + suffix,
        "published_table": prefix + "host.slot = make(); var alias = host.slot;" + suffix,
        "published_callable": prefix + "host.slot = make(); var read = host.slot.get; "
                              "var trace = read();",
        "second_invocation": prefix + "host.slot = make(); make();" + suffix,
        "extra_allocation": prefix + "host.slot = make(); var spare = {};" + suffix,
        "extra_argument": SOURCE.replace("host.slot.get();", "host.slot.get(17);"),
        "receiver": SOURCE.replace("return 42;", "return this;"),
        "implicit_arguments": SOURCE.replace("return 42;", "return arguments.length;"),
        "capture": SOURCE.replace("function make() {", "function make() { const value = 42;")
                         .replace("return 42;", "return value;"),
        "effect": "var side = 0; " + SOURCE.replace("return 42;", "side = 1; return 42;"),
        "nonliteral": SOURCE.replace("return 42;", "return 40 + 2;"),
        "conditional": prefix + "var flag = 1; if (flag) { host.slot = make(); }" + suffix,
        "deleted": prefix + "host.slot = make(); delete host.slot;" + suffix,
        "dynamic": prefix + "var key = 'slot'; host[key] = make();" + suffix,
        "unknown": prefix + "host.slot = make(); inspect(host);" + suffix,
        "prototype": prefix + "host.slot = make(); host.__proto__ = {};" + suffix,
        "captured_map": Path(__file__).with_name("native-export-boundary.js").read_text(),
    }


def forge_reports(text):
    text, count = re.subn(r"\bmodule attributes \{", "module attributes {"
        'ctnative.host_owner_proved = true, ctnative.host_owner_reason = "", '
        'ctnative.host_proved = true, ctnative.host_slots = [], ', text, count=1)
    if count != 1:
        raise RuntimeError("forged-owner control did not mark the module")
    return text


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
        raise RuntimeError("need both compilers and a working VM-symbol positive control")

    saved = {}
    for name, (source, binding, expected) in POSITIVES.items():
        js, ir, count = boundary.prepare(args, name, source)
        if count != 3:
            raise RuntimeError(f"{name}: changed source denominator")
        if name == "already_resolved":
            ir = resolve_getter(args, ir)
        if name.startswith("legacy_"):
            ir = legacy_marker(args, ir, name)
        boolean_result = name in ("typed-boolean", "typed-boolean-false")
        node_driver = BOOLEAN_NODE if boolean_result else boundary.NODE
        if host.run([node, "-e", node_driver, str(js)]).stdout != expected:
            raise RuntimeError(f"{name}: Node source oracle mismatch")
        interpreted = host.run([str(reference), str(js)])
        if interpreted.stdout != expected:
            raise RuntimeError(f"{name}: interpreter source oracle mismatch")
        if boolean_result and not re.search(
                r"1 globals printed \(0 number, 1 boolean, 0 string, 0 null, 0 undefined\)",
                interpreted.stderr):
            raise RuntimeError(f"{name}: interpreter did not observe one definite Boolean")
        config = owned.contract(args, ir, name, binding)
        prepared_text, manifest_text = ir.read_text(), config.read_text()
        output = owned.lower(args, ir, name, config)
        text = census(output, 3, name, admitted=3)
        if "ctnative.host_owner_proved = true" not in text:
            raise RuntimeError(f"{name}: admitted without a live owner report\n{text}")
        if ir.read_text() != prepared_text or config.read_text() != manifest_text:
            raise RuntimeError(f"{name}: native preparation rewrote the supplied IR or manifest")
        standalone(args, output, name, expected, compilers, nm,
                   result_type="bool" if boolean_result else "js_num")
        if boolean_result:
            disabled = owned.lower(args, ir, name + "-disabled", config, options="optimize=false")
            if disabled.read_text() != text:
                raise RuntimeError(f"{name}: Boolean admission changed with optimization policy")
        saved[name] = ir, config, output

    ir, config, output = saved["ordinary"]
    disabled = owned.lower(args, ir, "ordinary-disabled", config, options="optimize=false")
    if disabled.read_text() != output.read_text():
        raise RuntimeError("explicit host ownership changed with default optimization policy")
    _, reasons = boundary.native(args, ir, "ordinary-no-manifest", 3, claimed=1)
    if boundary.GLOBAL_ESCAPE not in reasons:
        raise RuntimeError("uncontracted publication lost its global escape refusal")
    for budget in (0, 32):
        refused(args, ir, f"budget-{budget}", config, options=f"host-max-steps={budget}",
                reason="budget", admitted=1)
    rollback = []
    completed = None
    for budget in range(480, 545):
        candidate = owned.lower(args, ir, f"preparation-budget-{budget}", config,
                                options=f"host-max-steps={budget}", cleanup=False)
        text = census(candidate, 3, f"preparation-budget-{budget}")
        admitted = len(boundary.NATIVE.findall(text))
        if admitted == 3:
            completed = budget
            break
        if admitted != 1:
            raise RuntimeError(f"budget-{budget}: leaked a partial native call component")
        if "ctnative.host_owner_proved = true" in text:
            rollback.append(budget)
            for op in ("create_object", "set_property", "get_property", "call", "call_direct"):
                pattern = rf"^\s*(?:%[^=\n]+\s*=\s*)?ctjs\.{op}\b"
                if len(re.findall(pattern, ir.read_text(), re.M)) != len(re.findall(pattern, text, re.M)):
                    raise RuntimeError(f"budget-{budget}: incomplete preparation changed {op}")
    if not rollback or completed is None:
        raise RuntimeError("preparation budget control did not exercise rollback and completion")

    # An owner and callable identity cannot authorize an unsupported field
    # result at the definite Number/Boolean observation boundary. Refusal must
    # close over the entire prepared component, including the retained getter.
    for name, literal in (("string", "'answer'"), ("null", "null"), ("undefined", "void 0")):
        _, typed, count = boundary.prepare(args, f"typed-{name}",
                                          SOURCE.replace("return 42;", f"return {literal};"))
        fresh = owned.contract(args, typed, f"typed-{name}")
        rejected = owned.lower(args, typed, f"typed-{name}", fresh, cleanup=False)
        text = census(rejected, count, f"typed-{name}", admitted=0)
        if "ctnative.host_owner_proved = true" not in text:
            raise RuntimeError(f"typed-{name}: did not reach native type/component admission")

    # Proof-preserving internal preparation must validate the input fingerprint
    # before it rewrites anything. A changed literal still has a valid owner
    # graph, so accepting this old manifest would expose an automatic refresh.
    changed = args.work / "changed-literal.mlir"
    original_bits = struct.unpack("=Q", struct.pack("=d", 42))[0]
    changed_bits = struct.unpack("=Q", struct.pack("=d", 43))[0]
    text, count = re.subn(rf"#ctjs\.number<{original_bits}>", f"#ctjs.number<{changed_bits}>",
                         ir.read_text())
    if count != 1:
        raise RuntimeError("stale-manifest control did not change exactly one getter literal")
    changed.write_text(text)
    refused(args, changed, "stale-literal", config, reason="fingerprint mismatch", admitted=1)
    refused(args, args.work / "ordinary.raw.mlir", "stale-preparation", config,
            reason="fingerprint mismatch")

    forged = args.work / "forged.mlir"
    forged.write_text(forge_reports(changed.read_text()))
    _, reasons = boundary.native(args, forged, "forged-no-manifest", 3, claimed=1)
    if boundary.GLOBAL_ESCAPE not in reasons:
        raise RuntimeError("forged owner report supplied authority without a driver")
    first = refused(args, forged, "forged-stale", config,
                    reason="fingerprint mismatch", admitted=1)
    refused(args, first, "forged-stale-rerun", config,
            reason="fingerprint mismatch", admitted=1)

    # A completed lowering is not the source fingerprint. Rerunning it can
    # leave already-emitted functions intact but cannot claim a fresh proof.
    rerun = owned.lower(args, output, "admitted-rerun", config, cleanup=False)
    text = census(rerun, 3, "admitted-rerun", admitted=3)
    if ("ctnative.host_owner_proved = false" not in text
            or "fingerprint mismatch" not in text):
        raise RuntimeError("completed native output reused its original source proof")

    sources = refusal_sources()
    for name, source in sources.items():
        _, rejected, _ = boundary.prepare(args, name, source)
        fresh = owned.contract(args, rejected, name)
        refused(args, rejected, name, fresh, admitted=0 if name == "captured_map" else None)
        if name == "effect":
            # A current fingerprint authenticates source identity, never a
            # supplied native proof. Rebuild the unsafe getter body despite
            # successful reports and fabricated table/type preparation.
            forged = args.work / "forged-fresh.mlir"
            text = forge_reports(rejected.read_text())
            marker = (' {ctnative.owned_method_table_slot = "forged", '
                      'ctnative.method_table = "forged"}')
            text, count = re.subn(r"\bctjs\.create_object(?!\s*\{)",
                                 "ctjs.create_object" + marker, text)
            if count != 2:
                raise RuntimeError("fresh forgery did not mark both source allocations")
            forged.write_text(text)
            checked = owned.contract(args, forged, "forged-fresh")
            first = refused(args, forged, "forged-fresh-first", checked)
            if "fingerprint mismatch" in first.read_text():
                raise RuntimeError("fresh forgery did not reach live source-graph reanalysis")
            refused(args, first, "forged-fresh-rerun", checked)
    print(f"native owned global methods: {len(POSITIVES)} complete 3/3 programs; "
          "Node/interpreter and explicit/deduced GCC/Clang agree; post-entry owner/table/callable "
          "lifetime sanitizers; no-manifest 1/3; "
          f"{len(sources)} source refusals and stale/forged/rerun/budget controls pass; "
          f"preparation rolls back at {rollback}, completes at {completed}")


if __name__ == "__main__":
    main()
