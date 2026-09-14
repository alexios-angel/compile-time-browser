#!/usr/bin/env python3
"""Compile the pinned Bootstrap Data probe with methods attached to their owner."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil

from CTNative.harness import find_compilers
from CTNative.Ownership.native_owned_global_maps.driver_common import (
    comparable_provenance,
    forge_leaf_evidence,
    host,
    methods,
    owned,
)
from CTNative.Ownership.native_owned_global_maps.driver_umd import (
    observe_umd,
    prepare_umd,
    umd_contract,
    umd_source,
)


def configure(args, ir, name, values):
    path = umd_contract(args, ir, name, values)
    value = json.loads(path.read_text())
    value["provider"] = "closed-source-session-v1"
    path.write_text(json.dumps(value, indent=2) + "\n")
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt", "node", "reference"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    compilers, nm = find_compilers(), shutil.which("nm") or shutil.which("llvm-nm")
    if not all(compilers) or not nm:
        raise RuntimeError("need GCC, Clang and nm")
    if not owned.VM.search(host.run([nm, "-C", args.reference]).stdout):
        raise RuntimeError("VM symbol control is ineffective")
    js, _, subject, values = prepare_umd(args)
    expected = observe_umd(args, js, values)
    source, _, _ = umd_source()
    expected_calls = re.findall(r"globalThis\.bootstrap\.(set|get|remove)\(", source)
    config = configure(args, subject, "session", values)
    for policy, options in (("default", ""), ("disabled", "optimize=false")):
        name = "session-" + policy
        flags = options + " host-max-steps=1000000"
        output = owned.lower(args, subject, name, config, options=flags)
        methods.census(output, 7, name, admitted=7)
        deduced = args.work / f"{name}.deduced.mlir"
        host.run([args.opt, str(output), "--ctnative-print-deduced", "-o", str(deduced)])
        for mode, ir in (("explicit", output), ("deduced", deduced)):
            host.run(
                [
                    "cmake",
                    f"-DTRANSLATE={args.translate}",
                    f"-DMODULE={ir}",
                    "-DCOMPILERS=" + ",".join(compilers),
                    f"-DWORK={args.work}",
                    f"-DNAME={name}-{mode}",
                    "-P",
                    str(Path(__file__).resolve().parents[1] / "Checks/compile-clean.cmake"),
                ]
            )
            cpp = host.run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
            entry = re.search(r"\bmain\(\)\s*\{(.*?)^\}", cpp, re.M | re.S)
            if owned.VM.search(cpp) or not entry:
                raise RuntimeError("session must have a standalone native entry")
            calls = re.findall(r"ctnative::invoke_session<&[^>\n]+::m_(\w+)>\(", entry[1])
            if calls != expected_calls or len(calls) != 23 or "ctnative::method_get<" in entry[1]:
                raise RuntimeError("session lost direct Data call order or extracted a method")
            storage = re.findall(r"^  (.+) captured_map;$", cpp, re.M)
            if len(storage) != 1 or "std::shared_ptr<" + storage[0] + ">" in cpp:
                raise RuntimeError("session must own its outer Map by value")
            if "std::tuple<" + storage[0] + " *>" in cpp or re.search(
                r"\b(?:capture|initialize)_(?:get|set|remove)\b", cpp
            ):
                raise RuntimeError("Data members must use their owned Map without stored captures")
            cpp += """
using session_type = typename decltype(g_globalThis->bootstrap)::element_type;
static_assert(!std::is_copy_constructible_v<session_type>);
static_assert(!std::is_move_constructible_v<session_type>);
static_assert(!std::is_copy_assignable_v<session_type>);
static_assert(!std::is_move_assignable_v<session_type>);
static_assert(std::is_member_function_pointer_v<decltype(&session_type::m_get)>);
static_assert(std::is_member_function_pointer_v<decltype(&session_type::m_set)>);
static_assert(std::is_member_function_pointer_v<decltype(&session_type::m_remove)>);
static_assert(std::is_pointer_v<decltype(std::declval<session_type &>().capture_map())>);
static_assert(sizeof(session_type) ==
              sizeof(std::remove_pointer_t<decltype(std::declval<session_type &>().capture_map())>));
"""
            native = args.work / f"{name}.{mode}.cpp"
            native.write_text(cpp)
            for index, compiler in enumerate(compilers):
                binary = native.with_suffix(f".{index}").resolve()
                host.run([compiler, *owned.FLAGS, str(native), "-o", str(binary)])
                if owned.VM.search(host.run([nm, "-C", str(binary)]).stdout):
                    raise RuntimeError("session linked Script symbols")
                result = host.run([str(binary)])
                if result.stdout != expected or result.stderr:
                    raise RuntimeError("session differs from the typed Node/VM observations")
            # The Map family can still be owned after the global publication is
            # replaced. Calls require that table; no method owns it independently.
            lifetime = native.with_suffix(".lifetime.cpp")
            changed, count = re.subn(r"\bmain\(\)", "session_entry()", cpp)
            assert count == 1
            lifetime.write_text(changed + """
int main() {
    if (session_entry() != 0) { return 1; }
    auto table = g_globalThis->bootstrap;
    auto element = g_element;
    std::weak_ptr lifetime = table;
    auto child = ctnative::map_get_present(table->capture_map(), element);
    auto payload = table->m_get(element, "bs.collapse");
    std::weak_ptr payload_lifetime = payload.object;
    std::weak_ptr other_child = ctnative::map_get_present(table->capture_map(), g_other);
    std::weak_ptr other_key = g_other;
    g_globalThis.reset();
    table->m_remove(element, "bs.collapse");
    if (ctnative::map_size(child) != 0) { return 7; }
    table->m_set(element, "bs.alert", 47.0);
    auto replacement = ctnative::map_get_present(table->capture_map(), element);
    if (replacement == child) { return 8; }
    if (ctnative::global_number(table->m_get(element, "bs.alert")) != 47) { return 2; }
    if (session_entry() != 0) { return 3; }
    if (ctnative::global_number(table->m_get(element, "bs.alert")) != 47) { return 4; }
    if (!ctnative::object_strict_equal(g_globalThis->bootstrap->m_get(element, "bs.alert"),
                                     ctnative::nullable_scalar::null())) { return 5; }
    table.reset();
    if (!lifetime.expired()) { return 6; }
    if (!other_child.expired() || !other_key.expired()) { return 9; }
    if (ctnative::map_size(child) != 0 ||
        ctnative::global_number(ctnative::map_get_present(replacement, std::string("bs.alert")))
            != 47) { return 10; }
    if (ctnative::global_number(ctnative::object_get_field_76616c7565(payload)) != 64) {
        return 11;
    }
    payload.object.reset();
    if (!payload_lifetime.expired()) { return 12; }
    return 0;
}
""")
            binary = lifetime.with_suffix(".sanitized").resolve()
            host.run(
                [
                    compilers[1],
                    *owned.FLAGS,
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
            result = host.run(
                [str(binary)],
                environment=dict(
                    os.environ,
                    ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
                    UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
                ),
            )
            if result.stdout != expected * 2 or result.stderr:
                raise RuntimeError("session reentry/lifetime check failed")

        def refuse(ir, manifest, label, extra=flags):
            result = host.run(
                [args.opt, str(ir), f"--ctnative-lower-to-emitc=host-manifest={manifest} {extra}"],
                success=False,
            )
            if (
                "native session requires a completely admitted captured method table"
                not in result.stderr
            ):
                raise RuntimeError(f"{label}: expected a session admission diagnostic")
            if "emitc.func" in result.stdout:
                raise RuntimeError(f"{label}: refused session emitted native code")

        refuse(subject, config, name + "-budget", options + " host-max-steps=0")
        forged = args.work / f"{name}.forged.mlir"
        forged.write_text(forge_leaf_evidence(subject.read_text()))
        refuse(forged, config, name + "-stale")
        fresh = configure(args, forged, name + "-fresh", values)
        checked = owned.lower(args, forged, name + "-fresh", fresh, options=flags)
        methods.census(checked, 7, name + "-fresh", admitted=7)
        if comparable_provenance(
            host.run([args.translate, "--mlir-to-cpp", str(checked)]).stdout, forged
        ) != comparable_provenance(
            host.run([args.translate, "--mlir-to-cpp", str(output)]).stdout, subject
        ):
            raise RuntimeError("forged report attributes changed session emission")
        escaped = args.work / f"{name}.escaped.mlir"
        text = subject.read_text()
        # Change one actual Data method use into an observable global store.
        call = re.search(r"ctjs.call (%[\w.$-]+)\(", text)
        read = (
            re.search(re.escape(call[1]) + r" = ctjs.get_property [^\n]+", text) if call else None
        )
        if not read:
            raise RuntimeError("escape control lost the actual Data callee read")
        position = read.end()
        text = (
            text[:position]
            + '\n    ctjs.store_global "traceErrorCount", '
            + call[1]
            + text[position:]
        )
        escaped.write_text(text)
        fresh = configure(args, escaped, name + "-escaped", values)
        refuse(escaped, fresh, name + "-escaped")
    print(
        "Data session: pinned 3218-byte source, 7/7 functions, 23 direct calls, 19 observations; "
        "both policies/layouts/GCC/Clang; by-value outer Map, direct member captures, "
        "nonmovable member ABI and ASan/UBSan saved-child/payload lifetimes; "
        "budget/stale/callable-escape refuse"
    )


if __name__ == "__main__":
    main()
