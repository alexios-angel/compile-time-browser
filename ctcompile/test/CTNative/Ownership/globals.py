#!/usr/bin/env python3
"""Check live ordinary global ownership, standalone output and source refusals."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from CTNative.harness import CORE_INCLUDE, RUNTIME_INCLUDE, find_compilers
from CTNative.Exports import boundary

host = boundary.host
VM = re.compile(r"ctbrowser::(?:script|aot)::|\bct_aot_")
FLAGS = [
    "-std=c++23",
    RUNTIME_INCLUDE,
    CORE_INCLUDE,
    "-O2",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wconversion",
    "-pedantic",
    "-ffp-contract=off",
]
CLEANUP = (
    "emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,"
    "canonicalize,ctnative-prune-dead-stores,canonicalize)"
)


def contract(args, ir, name, binding="host", *, absent=(), undefined=()):
    value = host.manifest(args.opt, ir, absent=absent, undefined=undefined)
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
    if (
        len(boundary.FUNCTION.findall(text)) != count
        or boundary.NATIVE.search(text)
        or len(boundary.REFUSAL.findall(text)) != count
    ):
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
            changed += r"""
int main() {
    if (ctnative_test_entry() != 0) { return 90; }
    auto first = g_host;
    std::weak_ptr<ctn_slot> lifetime = first;
    g_host.reset();
    for (int index = 0; index < 4096; ++index) {
        auto churn = std::make_shared<ctn_slot>();
        churn->slot = ctnative::js_num{static_cast<double>(index)};
    }
    if (lifetime.expired() || first->slot.value() != 42) { return 91; }
    if (ctnative_test_entry() != 0) { return 92; }
    if (first == g_host || first->slot.value() != 42 || g_host->slot.value() != 42) { return 93; }
    first.reset();
    if (!lifetime.expired()) { return 94; }
    g_host.reset();
    return 0;
}
"""
            lifetime = args.work / f"{name}.{mode}.lifetime.cpp"
            lifetime.write_text(changed)
            binary = (args.work / f"{name}.{mode}.sanitized").resolve()
            host.run(
                [
                    compilers[1],
                    *FLAGS,
                    "-O1",
                    "-g",
                    "-fno-omit-frame-pointer",
                    "-fsanitize=address,undefined",
                    "-fsanitize-address-use-after-scope",
                    str(lifetime),
                    "-o",
                    str(binary),
                ]
            )
            result = subprocess.run(
                [str(binary)],
                capture_output=True,
                text=True,
                timeout=60,
                env=dict(
                    os.environ,
                    ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
                    UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
                ),
            )
            if result.returncode or result.stdout != expected * 2:
                raise RuntimeError(
                    f"{name}/{mode}: lifetime failure\n{result.stdout}{result.stderr}"
                )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--node", required=True)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    node, reference = args.node, args.reference
    compilers = find_compilers()
    nm = shutil.which("nm")
    if not all(compilers) or not nm or not VM.search(host.run([nm, "-C", str(reference)]).stdout):
        raise RuntimeError("need both compilers and a working VM-symbol positive control")
    positives = {
        "ordinary": ("var host = {}; host.slot = 42; var trace = host.slot;", "host", 42),
        "legacy_store_marker": (
            "var host = {}; host.slot = 42; var trace = host.slot;",
            "host",
            42,
        ),
        "legacy_field_marker": (
            "var host = {}; host.slot = 42; var trace = host.slot;",
            "host",
            42,
        ),
        "initialized": ("var host = {slot: 42}; var trace = host.slot;", "host", 42),
        "fraction": ("var host = {slot: 0.125}; var trace = host.slot;", "host", 0.125),
        "two_loads": (
            "var host = {}; host.slot = 42; var trace = host.slot; trace = host.slot;",
            "host",
            42,
        ),
        "ordinary_window": (
            "var window = {}; window.slot = 42; var trace = window.slot;",
            "window",
            42,
        ),
        "unobserved_numeric": (
            "var host = {slot: 42}; var ignored = 1; var trace = host.slot;",
            "host",
            42,
        ),
        "fixed_undefined": (
            "var host = {slot: 42}; var trace = host.slot; trace = undefined;",
            "host",
            "undefined",
        ),
    }
    saved = {}
    for name, (source, binding, value) in positives.items():
        js, ir, count = boundary.prepare(args, name, source)
        if count != 1:
            raise RuntimeError(f"{name}: changed source denominator")
        expected = f"trace={value}\n"
        observer = boundary.NODE
        if name == "fixed_undefined":
            observer = observer.replace(
                "typeof trace !== 'number' || !Number.isFinite(trace)", "trace !== undefined"
            )
        if host.run([node, "-e", observer, str(js)]).stdout != expected:
            raise RuntimeError(f"{name}: Node source oracle mismatch")
        reference_expected = "ignored=1\n" + expected if name == "unobserved_numeric" else expected
        if host.run([str(reference), str(js)]).stdout != reference_expected:
            raise RuntimeError(f"{name}: interpreter source oracle mismatch")
        if name.startswith("legacy_"):
            pattern = (
                r'ctjs\.store_global "trace", %[-\w.$]+'
                if name == "legacy_store_marker"
                else r"ctjs\.set_property %[-\w.$]+\[%[-\w.$]+\], %[-\w.$]+"
            )
            text, changed = re.subn(
                pattern, lambda match: match[0] + " {ctnative.method}", ir.read_text(), count=1
            )
            if changed != 1:
                raise RuntimeError(f"{name}: did not mark its exact source operation")
            ir.write_text(text)
        config = contract(
            args, ir, name, binding, undefined=("undefined",) if name == "fixed_undefined" else ()
        )
        output = lower(args, ir, name, config)
        text = output.read_text()
        if (
            len(boundary.NATIVE.findall(text)) != 1
            or boundary.FUNCTION.search(text)
            or boundary.REFUSAL.search(text)
            or "ctnative.host_owner_proved = true" not in text
        ):
            raise RuntimeError(f"{name}: expected 1/1 native with a live owner\n{text}")
        standalone(args, output, name, expected, compilers, nm)
        saved[name] = ir, config

    # A present-undefined contract authorizes only its exact unchanged binding.
    # Refusal must retain the load, including when the source owner proved but
    # the private normalization clone exhausted its remaining budget.
    ir, config = saved["fixed_undefined"]
    source = positives["fixed_undefined"][0]
    original, manifest_text = ir.read_text(), config.read_text()
    if original.count('ctjs.load_global "undefined"') != 1:
        raise RuntimeError("fixed undefined witness lost its source global read")
    ignored_writes = {
        "early-store": "undefined = 7; " + source,
        "late-store": source + " undefined = 7;",
        "inactive-store": source + " if (false) { undefined = 7; }",
    }
    observer = boundary.NODE.replace(
        "typeof trace !== 'number' || !Number.isFinite(trace)", "trace !== undefined"
    )
    # The frontend erases these sloppy writes, but explicit source IR stores
    # must still reject a contract that promises an unchanged host binding.
    written = args.work / "fixed-undefined-raw-store.mlir"
    text, count = re.subn(
        r'^( +)(%[-\w.$]+) = ctjs\.load_global "undefined"[^\n]*',
        lambda match: match[0] + f'\n{match[1]}ctjs.store_global "undefined", {match[2]}',
        original,
        count=1,
        flags=re.M,
    )
    if count != 1:
        raise RuntimeError("raw undefined store control lost its source load")
    written.write_text(text)
    written_config = contract(args, written, "fixed-undefined-raw-store", undefined=("undefined",))
    for policy, options in (("default", ""), ("disabled", "optimize=false")):
        label = f"fixed-undefined-{policy}"
        output = lower(args, ir, label, config, options=options)
        if output.read_text() != (args.work / "fixed_undefined.native.mlir").read_text():
            raise RuntimeError(f"{label}: fixed undefined changed with optimization policy")
        if policy == "disabled":
            standalone(args, output, label, "trace=undefined\n", compilers, nm)
        for name, declarations, reason in (
            ("missing", {}, "unproved host binding `undefined`"),
            (
                "absent",
                {"absent": ("undefined",)},
                "absent binding lacks source typeof lookup mode",
            ),
            ("other-name", {"undefined": ("otherUndefined",)}, "unproved host binding `undefined`"),
        ):
            fresh = contract(args, ir, f"{label}-{name}", **declarations)
            refused(args, ir, f"{label}-{name}", fresh, options=options, reason=reason)
        for name, changed_source in ignored_writes.items():
            js, changed, count = boundary.prepare(args, f"{label}-{name}", changed_source)
            if count != 1 or 'ctjs.store_global "undefined"' in changed.read_text():
                raise RuntimeError(f"{label}-{name}: sloppy undefined assignment survived import")
            expected = "trace=undefined\n"
            if host.run([node, "-e", observer, str(js)]).stdout != expected:
                raise RuntimeError(f"{label}-{name}: Node source oracle mismatch")
            if host.run([str(reference), str(js)]).stdout != expected:
                raise RuntimeError(f"{label}-{name}: interpreter source oracle mismatch")
            fresh = contract(args, changed, f"{label}-{name}", undefined=("undefined",))
            if name == "inactive-store":
                # Its empty branch still exceeds the complete global-owner proof.
                refused(
                    args,
                    changed,
                    f"{label}-{name}",
                    fresh,
                    options=options,
                    reason="an object literal that escapes",
                )
                continue
            output = lower(args, changed, f"{label}-{name}", fresh, options=options)
            standalone(args, output, f"{label}-{name}", expected, compilers, nm)
        refused(
            args,
            written,
            f"{label}-raw-store",
            written_config,
            options=options,
            reason="a fixed undefined host binding has a source write",
        )
        refused(
            args,
            ir,
            f"{label}-budget-zero",
            config,
            options=options + " host-max-steps=0",
            reason="budget",
        )
    _, changed, _ = boundary.prepare(args, "fixed-undefined-stale", source.replace("42", "43"))
    refused(args, changed, "fixed-undefined-stale", config, reason="fingerprint mismatch")
    forged = args.work / "fixed-undefined-forged.mlir"
    text, count = re.subn(
        r"\bmodule attributes \{",
        "module attributes {ctnative.host_owner_proved = true, ctnative.host_proved = true, "
        'ctnative.host_owner_reason = "", ',
        original,
        count=1,
    )
    if count != 1:
        raise RuntimeError("fixed undefined forgery did not mark its source module")
    forged.write_text(text)
    fresh = contract(args, forged, "fixed-undefined-forged")
    refused(
        args, forged, "fixed-undefined-forged", fresh, reason="unproved host binding `undefined`"
    )
    _, reasons = boundary.native(args, forged, "fixed-undefined-no-manifest", 1)
    if boundary.GLOBAL_ESCAPE not in reasons:
        raise RuntimeError("fixed undefined forgery supplied authority without a manifest")

    low, high = 0, 100000
    while high - low > 1:
        budget = (low + high) // 2
        candidate = lower(
            args,
            ir,
            f"fixed-undefined-budget-{budget}",
            config,
            options=f"host-max-steps={budget}",
            cleanup=False,
        )
        if boundary.NATIVE.search(candidate.read_text()):
            high = budget
        else:
            low = budget
    rollback = refused(
        args, ir, "fixed-undefined-rollback", config, options=f"host-max-steps={low}"
    )
    if "ctnative.host_owner_proved = true" not in rollback.read_text():
        raise RuntimeError("fixed undefined budget control did not reach private-clone rollback")
    if ir.read_text() != original or config.read_text() != manifest_text:
        raise RuntimeError("fixed undefined normalization rewrote supplied IR or manifest")

    ir, config = saved["ordinary"]
    # Default-disabled and default-enabled entry both preserve the driver's
    # fingerprinted IR before ownership admission; source operations remain.
    disabled = lower(args, ir, "ordinary-disabled", config, options="optimize=false")
    if disabled.read_text() != (args.work / "ordinary.native.mlir").read_text():
        raise RuntimeError("explicit host ownership changed with default optimization policy")
    refused(args, ir, "budget-zero", config, options="host-max-steps=0", reason="budget")
    refused(args, ir, "budget-tight", config, options="host-max-steps=32", reason="budget")
    forged = args.work / "forged.mlir"
    text, count = re.subn(
        r"\bmodule attributes \{",
        "module attributes {"
        'ctnative.host_owner_proved = true, ctnative.host_owner_reason = "", ',
        ir.read_text(),
        count=1,
    )
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
            reason = (
                "owned global root needs a proved owner and one definite numeric field"
                if name in ("boolean", "string")
                else None
            )
            output = refused(args, rejected, name, fresh, reason=reason)
        if name == "rewrite":
            refused(args, rejected, "stale", config, reason="fingerprint mismatch")
            refused(args, output, "rewrite-rerun", config, reason="fingerprint mismatch")
    print(
        f"native owned globals: {len(positives) + len(ignored_writes) - 1} complete 1/1 programs; Node/interpreter and "
        "explicit/deduced GCC/Clang agree; owning lifetime sanitizers and "
        f"{len(refusals)} source refusals plus stale/forged/budget controls pass; "
        "fixed undefined preserves its exact tag and rejects missing/absent/written bindings, "
        f"stale/forged proofs and incomplete normalization (rollback at {low}, complete at {high})"
    )


if __name__ == "__main__":
    main()
