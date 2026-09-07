#!/usr/bin/env python3
"""Check live ordinary global ownership, standalone output and source refusals."""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


spec = importlib.util.spec_from_file_location(
    "boundary", Path(__file__).with_name("native-export-boundary.py"))
boundary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(boundary)
host = boundary.host
VM = re.compile(r"ctbrowser::(?:script|aot)::|\bct_aot_")
FLAGS = ["-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror", "-Wconversion",
         "-pedantic", "-ffp-contract=off"]
CLEANUP = ("emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,"
           "canonicalize,ctnative-prune-dead-stores,canonicalize)")


def contract(args, ir, name, binding="host"):
    value = host.manifest(args.opt, ir)
    value["roots"][0]["binding"] = binding
    path = args.work / f"{name}.contract.json"
    path.write_text(json.dumps(value, indent=2) + "\n")
    return path


def lower(args, ir, name, config, *, options="", cleanup=True):
    output = args.work / f"{name}.native.mlir"
    flags = f"host-manifest={config} {options}"
    pipeline = f"builtin.module(ctnative-lower-to-emitc{{{flags}}}"
    pipeline += f",{CLEANUP})" if cleanup else ")"
    host.run([args.opt, str(ir), "--pass-pipeline=" + pipeline, "-o", str(output)])
    return output


def refused(args, ir, name, config, *, options="", reason=None):
    output = lower(args, ir, name, config, options=options, cleanup=False)
    text = output.read_text()
    count = len(boundary.FUNCTION.findall(ir.read_text()))
    if (len(boundary.FUNCTION.findall(text)) != count or boundary.NATIVE.search(text)
            or len(boundary.REFUSAL.findall(text)) != count):
        raise RuntimeError(f"{name}: ownership refusal lost source functions\n{text}")
    if reason and reason not in text:
        raise RuntimeError(f"{name}: missing refusal {reason!r}\n{text}")
    for op in ("create_object", "store_global", "load_global", "set_property", "get_property"):
        pattern = rf"^\s*(?:%[^=\n]+\s*=\s*)?ctjs\.{op}\b"
        if len(re.findall(pattern, ir.read_text(), re.M)) != len(re.findall(pattern, text, re.M)):
            raise RuntimeError(f"{name}: failed ownership proof changed ctjs.{op}")
    return output


def standalone(args, output, name, expected, compilers, nm):
    deduced = args.work / f"{name}.deduced.mlir"
    host.run([args.opt, str(output), "--ctnative-print-deduced", "-o", str(deduced)])
    for mode, ir in (("explicit", output), ("deduced", deduced)):
        cpp = host.run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
        if VM.search(cpp) or "std::shared_ptr<ctn_slot>" not in cpp:
            raise RuntimeError(f"{name}/{mode}: missing standalone owning carrier\n{cpp}")
        source = args.work / f"{name}.{mode}.cpp"
        source.write_text(cpp)
        for index, compiler in enumerate(compilers):
            binary = (args.work / f"{name}.{mode}.{index}").resolve()
            host.run([compiler, *FLAGS, str(source), "-o", str(binary)])
            if VM.search(host.run([nm, "-C", str(binary)]).stdout):
                raise RuntimeError(f"{name}/{mode}: linked VM symbols")
            if host.run([str(binary)]).stdout != expected:
                raise RuntimeError(f"{name}/{mode}: standalone result mismatch")
        if name == "ordinary":
            # Exercise the generated storage after entry returns, with its
            # original global handle released and a second entry allocation.
            changed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", cpp)
            if count != 1:
                raise RuntimeError("lifetime harness needs one generated entry")
            changed += r'''
int main() {
    if (ctnative_test_entry() != 0) { return 90; }
    auto first = g_host;
    std::weak_ptr<ctn_slot> lifetime = first;
    g_host.reset();
    for (int index = 0; index < 4096; ++index) {
        auto churn = std::make_shared<ctn_slot>();
        churn->slot = index;
    }
    if (lifetime.expired() || first->slot != 42) { return 91; }
    if (ctnative_test_entry() != 0) { return 92; }
    if (first == g_host || first->slot != 42 || g_host->slot != 42) { return 93; }
    first.reset();
    if (!lifetime.expired()) { return 94; }
    g_host.reset();
    return 0;
}
'''
            lifetime = args.work / f"{name}.{mode}.lifetime.cpp"
            lifetime.write_text(changed)
            binary = (args.work / f"{name}.{mode}.sanitized").resolve()
            host.run([compilers[1], *FLAGS, "-O1", "-g", "-fno-omit-frame-pointer",
                      "-fsanitize=address,undefined", "-fsanitize-address-use-after-scope",
                      str(lifetime), "-o", str(binary)])
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60,
                env=dict(os.environ, ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
                         UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
            if result.returncode or result.stdout != expected * 2:
                raise RuntimeError(f"{name}/{mode}: lifetime failure\n{result.stdout}{result.stderr}")


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
    nm = shutil.which("nm")
    if not all(compilers) or not nm or not VM.search(host.run([nm, "-C", str(reference)]).stdout):
        raise RuntimeError("need both compilers and a working VM-symbol positive control")
    positives = {
        "ordinary": ("var host = {}; host.slot = 42; var trace = host.slot;", "host", 42),
        "initialized": ("var host = {slot: 42}; var trace = host.slot;", "host", 42),
        "fraction": ("var host = {slot: 0.125}; var trace = host.slot;", "host", 0.125),
        "two_loads": ("var host = {}; host.slot = 42; var trace = host.slot; trace = host.slot;", "host", 42),
        "ordinary_window": ("var window = {}; window.slot = 42; var trace = window.slot;", "window", 42),
        "unobserved_numeric": ("var host = {slot: 42}; var ignored = 1; var trace = host.slot;", "host", 42),
    }
    saved = {}
    for name, (source, binding, value) in positives.items():
        js, ir, count = boundary.prepare(args, name, source)
        if count != 1:
            raise RuntimeError(f"{name}: changed source denominator")
        expected = f"trace={value}\n"
        if host.run([node, "-e", boundary.NODE, str(js)]).stdout != expected:
            raise RuntimeError(f"{name}: Node source oracle mismatch")
        reference_expected = "ignored=1\n" + expected if name == "unobserved_numeric" else expected
        if host.run([str(reference), str(js)]).stdout != reference_expected:
            raise RuntimeError(f"{name}: interpreter source oracle mismatch")
        config = contract(args, ir, name, binding)
        output = lower(args, ir, name, config)
        text = output.read_text()
        if (len(boundary.NATIVE.findall(text)) != 1 or boundary.FUNCTION.search(text)
                or boundary.REFUSAL.search(text) or "ctnative.host_owner_proved = true" not in text):
            raise RuntimeError(f"{name}: expected 1/1 native with a live owner\n{text}")
        standalone(args, output, name, expected, compilers, nm)
        saved[name] = ir, config
    ir, config = saved["ordinary"]
    # Default-disabled and default-enabled entry both preserve the driver's
    # fingerprinted IR before ownership admission; source operations remain.
    disabled = lower(args, ir, "ordinary-disabled", config, options="optimize=false")
    if disabled.read_text() != (args.work / "ordinary.native.mlir").read_text():
        raise RuntimeError("explicit host ownership changed with default optimization policy")
    refused(args, ir, "budget-zero", config, options="host-max-steps=0", reason="budget")
    refused(args, ir, "budget-tight", config, options="host-max-steps=32", reason="budget")
    forged = args.work / "forged.mlir"
    text, count = re.subn(r"\bmodule attributes \{", "module attributes {"
        'ctnative.host_owner_proved = true, ctnative.host_owner_reason = "", ', ir.read_text(), count=1)
    if count != 1:
        raise RuntimeError("forged-owner control did not mark the module")
    forged.write_text(text)
    _, reasons = boundary.native(args, forged, "forged-no-manifest", 1)
    if boundary.GLOBAL_ESCAPE not in reasons:
        raise RuntimeError("forged owner report supplied authority without a driver")
    refusals = {
        "missing": "var host = {}; var trace = host.slot;",
        "before_write": "var host = {}; var trace = host.slot; host.slot = 42;",
        "rewrite": "var host = {}; host.slot = 1; host.slot = 42; var trace = host.slot;",
        "rebind": "var host = {slot: 1}; host = {slot: 42}; var trace = host.slot;",
        "extra_field": "var host = {slot: 42, extra: 1}; var trace = host.slot;",
        "published_alias": "var host = {slot: 42}; var alias = host; var trace = alias.slot;",
        "conditional": "var flag = 1; var host = {}; if (flag) { host.slot = 42; } var trace = host.slot;",
        "deleted": "var host = {slot: 42}; delete host.slot; var trace = host.slot;",
        "dynamic": "var key = 'slot'; var host = {}; host[key] = 42; var trace = host.slot;",
        "unknown": "var host = {slot: 42}; unknown(host); var trace = host.slot;",
        "accessor": "var host = {get slot() { return 42; }}; var trace = host.slot;",
        "prototype": "var host = {slot: 42}; host.__proto__ = {}; var trace = host.slot;",
        "cycle": "var host = {}; host.slot = host; var trace = 42;",
        "boolean": "var host = {slot: true}; var observed = host.slot; var trace = 42;",
        "string": "var host = {slot: 'answer'}; var observed = host.slot; var trace = 42;",
    }
    for name, source in refusals.items():
        _, rejected, _ = boundary.prepare(args, name, source)
        fresh = contract(args, rejected, name)
        # Some independent getter functions may retain baseline native support;
        # the owner entry must still refuse. Single-entry cases are all atomic.
        if name == "accessor":
            output = lower(args, rejected, name, fresh, cleanup=False)
            if re.search(r"\bemitc\.func @main\(", output.read_text()):
                raise RuntimeError("accessor owner was admitted")
        else:
            reason = ("owned global root needs a proved owner and one definite numeric field"
                      if name in ("boolean", "string") else None)
            output = refused(args, rejected, name, fresh, reason=reason)
        if name == "rewrite":
            refused(args, rejected, "stale", config, reason="fingerprint mismatch")
            refused(args, output, "rewrite-rerun", config, reason="fingerprint mismatch")
    print(f"native owned globals: {len(positives)} complete 1/1 programs; Node/interpreter and "
          "explicit/deduced GCC/Clang agree; owning lifetime sanitizers and "
          f"{len(refusals)} source refusals plus stale/forged/budget controls pass")


if __name__ == "__main__":
    main()
