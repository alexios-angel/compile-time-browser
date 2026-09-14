#!/usr/bin/env python3
"""Gate the pinned Bootstrap Data program with three document-owned input keys."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from CTNative.Browser import native_dom as dom
from CTNative.harness import find_compilers, run
from CTNative.HostContract import contract as host
from CTNative.Ownership.native_owned_global_maps.driver_common import CONSTANT_GLOBAL_NODE
from CTNative.Ownership.native_owned_global_maps.driver_umd import (
    observe_umd,
    prepare_umd,
    umd_contract,
    umd_source,
)
from Target.Cpp.harness import FLAGS, function_body

KEYS = ("element", "other", "absent")
ALLOCATIONS = "var element = {}; var other = {}; var absent = {};"
ALIASES = ((0, 1, 2), (0, 0, 2), (0, 1, 0), (0, 1, 1), (0, 0, 0))


def input_adapter(text):
    """Replace only the three probe allocations and their global aliases with inputs.

    The JavaScript pin and every vendor operation remain unchanged. This is a
    test input adapter over the imported script, not a source admission rewrite.
    """
    match = re.search(r"ctjs.func @_script_\$0\((.*?)\)(.*?)(?=\n  ctjs.func |\n})", text, re.S)
    if not match or len(re.findall(r"%[\w.$-]+: !ctjs.value", match[1])) != 3:
        raise RuntimeError("original script no longer has exactly three implicit arguments")
    entry = match[0]
    substitutions, removed, sites = {}, [], {}
    for name in KEYS:
        stores = list(
            re.finditer(r'^ *ctjs.store_global "' + name + r'", (%[\w.$-]+)\n', entry, re.M)
        )
        if len(stores) != 1:
            raise RuntimeError(f"{name}: expected one original key initialization")
        allocation = re.search(
            r"^ *" + re.escape(stores[0][1]) + r" = ctjs.create_object\n", entry, re.M
        )
        loads = list(
            re.finditer(r'^ *(%[\w.$-]+) = ctjs.load_global "' + name + r'"\n', entry, re.M)
        )
        if (
            not allocation
            or not loads
            or re.search(
                r'ctjs\.(?:load|store)_global "' + name + r'"',
                text[: match.start()] + text[match.end() :],
            )
        ):
            raise RuntimeError(f"{name}: key allocation/aliases left the original entry")
        argument = "%dom_" + name
        substitutions[stores[0][1]] = argument
        substitutions.update({load[1]: argument for load in loads})
        removed.extend([allocation, stores[0], *loads])
        sites[name] = {"allocation": allocation[0].strip(), "forwarded_loads": len(loads)}
    for operation in sorted(removed, key=lambda item: item.start(), reverse=True):
        entry = entry[: operation.start()] + entry[operation.end() :]
    entry = re.sub(r"%[\w.$-]+", lambda token: substitutions.get(token[0], token[0]), entry)
    arguments = ", ".join("%dom_" + name + ": !ctjs.value" for name in KEYS)
    entry = entry.replace(match[1] + ")", match[1] + ", " + arguments + ")", 1)
    adapted = text[: match.start()] + entry + text[match.end() :]
    if dom.FUNCTION.findall(adapted) != dom.FUNCTION.findall(text):
        raise RuntimeError("input adapter changed the source function census")
    for operation in ("construct", "create_closure", "call_direct", "call", "set_property"):
        if adapted.count("ctjs." + operation) != text.count("ctjs." + operation):
            raise RuntimeError("input adapter changed " + operation)
    if text.count("ctjs.create_object") - adapted.count("ctjs.create_object") != 3:
        raise RuntimeError("input adapter changed allocations other than the three key sites")
    return adapted, sites


CLIENT = r"""
#include <array>
#include <cassert>
#include <iostream>
#include <memory>
#include <type_traits>

int main() {
    using namespace ctbrowser;
    using session_type = @OWNER@;
    using scalar = ctnative::nullable_scalar;
    static_assert(!std::is_copy_constructible_v<session_type>);
    static_assert(!std::is_copy_assignable_v<session_type>);
    static_assert(!std::is_move_constructible_v<session_type>);
    static_assert(!std::is_move_assignable_v<session_type>);
    auto snapshot = [](const session_type & session) { return std::array{@OBSERVATIONS@}; };
    auto check = [](const auto & actual, const auto & expected) {
        for (unsigned i = 0; i < actual.size(); ++i) {
            assert(actual[i].tag == expected[i].tag && actual[i].value == expected[i].value);
        }
    };
    const std::array expected{@EXPECTED@};
    const std::array aliases{@ALIASES@};
    for (int lifetime = 0; lifetime < 16; ++lifetime) {
        session_type session, second;
        auto & doc = session.document();
        auto & other = second.document();
        const auto button = doc.create_element(doc.atoms().intern("button"));
        const auto child = doc.create_element(doc.atoms().intern("span"));
        const auto absent = doc.create_element(doc.atoms().intern("i"));
        const auto foreign_button = other.create_element(other.atoms().intern("button"));
        assert(button == foreign_button);
        assert(doc.append_child(doc.root(), button));
        assert(doc.append_child(button, child));
        assert(doc.append_child(doc.root(), absent));
        const std::array keys{element_ref{&doc, button}, element_ref{&doc, child}, element_ref{&doc, absent}};
        const element_ref foreign{&other, foreign_button}, copied = keys[0];
        for (const auto & value : snapshot(session)) { assert(value.tag == scalar::kind::undefined); }
        element_ref dangling = foreign;
#ifdef CTCOMPILE_TEST_DANGLING
        {
            atom_table temporary_atoms;
            auto temporary = std::make_unique<document>(temporary_atoms);
            dangling = {temporary.get(), temporary->create_element(temporary_atoms.intern("button"))};
        }
#endif
        auto reject_domains = [&] {
            const auto before = snapshot(session);
            for (element_ref bad : {foreign, dangling, element_ref{}}) {
                for (unsigned position = 0; position < keys.size(); ++position) {
                    auto arguments = keys;
                    arguments[position] = bad;
                    bool caught = false;
                    try { (void)session.invoke(arguments[0], arguments[1], arguments[2]); }
                    catch (const std::invalid_argument &) { caught = true; }
                    assert(caught);
                }
                for (unsigned position : {1u, 2u}) {
                    auto arguments = keys;
                    arguments[0] = {&doc, {}};
                    arguments[position] = bad;
                    bool domain_first = false;
                    try { (void)session.invoke(arguments[0], arguments[1], arguments[2]); }
                    catch (const std::invalid_argument &) { domain_first = true; }
                    assert(domain_first);
                }
            }
            check(snapshot(session), before);
        };
        reject_domains(); // Every domain precedes node validation and all source effects.
        for (unsigned variant = 0; variant < aliases.size(); ++variant) {
            const auto & indices = aliases[variant];
            (void)session.invoke(keys[indices[0]], keys[indices[1]], keys[indices[2]]);
            check(snapshot(session), expected[variant]);
            (void)session.invoke(keys[indices[0]], keys[indices[1]], keys[indices[2]]);
            check(snapshot(session), expected[variant]); // Source roots/Map/payload reset on reentry.
            (void)second.invoke(foreign, foreign, foreign);
            check(snapshot(second), expected.back());
            check(snapshot(session), expected[variant]); // Recorder observations are owner-local.
        }
        (void)session.invoke(copied, keys[1], keys[2]);
        check(snapshot(session), expected[0]);
        reject_domains();
        const auto before = snapshot(session);
        const auto text = doc.create_text("text");
        for (element_ref invalid : {element_ref{&doc, {}}, element_ref{&doc, text},
                 element_ref{&doc, {button.slot, button.generation + 2}}}) {
            for (unsigned position = 0; position < keys.size(); ++position) {
                auto arguments = keys;
                arguments[position] = invalid;
                bool caught = false;
                try { (void)session.invoke(arguments[0], arguments[1], arguments[2]); }
                catch (const std::exception &) { caught = true; }
                assert(caught);
            }
        }
        check(snapshot(session), before);
        // The table retains both nodes; detaching their subtree preserves identity.
        assert(doc.remove_child(button));
        assert(!doc.read().parent(button));
        (void)session.invoke(copied, keys[1], keys[2]);
        check(snapshot(session), expected[0]);
        // Private table, recorder and payload die before the owning document.
    }
    std::cout << "Bootstrap DOM Data passed\n";
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt", "clang", "node", "reference"):
        parser.add_argument("--" + name, required=True)
    for name in ("build", "include", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--nm", default=shutil.which("nm"))
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    compilers = find_compilers()
    compilers[1] = args.clang
    source, values, prefix = umd_source()
    js, _, original, _ = prepare_umd(args)
    observe_umd(args, js, values)
    adapted, sites = input_adapter(original.read_text())
    subject = args.work / "dom-inputs.mlir"
    subject.write_text(adapted)
    contract = json.loads(umd_contract(args, subject, "dom-inputs", values).read_text())
    contract.update(provider="ctbrowser-dom-data-session-v1", element_parameters=[0, 1, 2])
    report, _, _ = host.analyze(
        args.opt, subject, contract, args.work / "source-proof", options="max-steps=1000000"
    )
    if (
        report["proved"]
        or report["outer_key_objects"] != 0
        or report["outer_key_inputs"] != 0
        or len(report["slots"]) != 1
        or report["slots"][0]["candidate_edges"] != 23
        or report["reason"] != "property receiver lacks a fresh own-data object proof"
    ):
        raise RuntimeError(
            f"original Bootstrap must require local cell/receiver normalization: {report}"
        )
    if len(dom.FUNCTION.findall(adapted)) != 7 or len(values) != 19:
        raise RuntimeError("original Bootstrap function/observation census changed")
    (args.work / "provenance.json").write_text(
        json.dumps(
            {
                "program_bytes": len(source.encode()),
                "program_sha256": hashlib.sha256(source.encode()).hexdigest(),
                "input_adapter": sites,
                "source_functions": 7,
                "direct_calls": 23,
                "observations": 19,
            },
            indent=2,
        )
        + "\n"
    )

    expected = []
    for index, indices in enumerate(ALIASES):
        bindings = " ".join(
            "var " + name + " = " + ("{}" if origin == at else KEYS[origin]) + ";"
            for at, (name, origin) in enumerate(zip(KEYS, indices))
        )
        observed = args.work / f"alias-{index}.js"
        if source.count(ALLOCATIONS) != 1:
            raise RuntimeError("original probe key allocations changed")
        observed.write_text(source.replace(ALLOCATIONS, bindings))
        node = run(
            [args.node, "-e", CONSTANT_GLOBAL_NODE, str(observed), json.dumps(sorted(values))]
        )
        reference = run([args.reference, str(observed)])
        if reference.stdout != node.stdout:
            raise RuntimeError(f"alias partition {indices}: Node and interpreter disagree")
        rows = dict(line.split("=", 1) for line in node.stdout.splitlines())
        if rows.keys() != values.keys() or any(
            value != "null" and not value.isdigit() for value in rows.values()
        ):
            raise RuntimeError("alias observer lost the original scalar observations")
        expected.append(
            "std::array{"
            + ", ".join(
                "scalar::null()" if rows[name] == "null" else "scalar{" + rows[name] + ".0}"
                for name in sorted(values)
            )
            + "}"
        )
    client = (
        CLIENT.replace(
            "@OBSERVATIONS@", ", ".join("session.observe_" + name + "()" for name in sorted(values))
        )
        .replace("@EXPECTED@", ",\n".join(expected))
        .replace(
            "@ALIASES@",
            ", ".join(
                "std::array{" + ", ".join(str(value) + "u" for value in indices) + "}"
                for indices in ALIASES
            ),
        )
    )
    includes, libraries = dom.link_options(args)
    config = args.work / "native.contract.json"
    config.write_text(json.dumps(contract, indent=2) + "\n")

    def refuse(ir, requested, name, budget=1000000):
        checked, _, _ = host.analyze(
            args.opt, ir, requested, args.work / name, options=f"max-steps={budget}"
        )
        if (
            checked["proved"]
            or checked["outer_key_inputs"]
            or any(slot["proved_edges"] for slot in checked["slots"])
        ):
            raise RuntimeError(f"{name}: incomplete alias proof published usable evidence")
        manifest = args.work / f"{name}.json"
        manifest.write_text(json.dumps(requested, indent=2) + "\n")
        for optimize in (False, True):
            result = run(
                [
                    args.opt,
                    str(ir),
                    f"--ctnative-lower-to-emitc=host-manifest={manifest} host-max-steps={budget} optimize={str(optimize).lower()}",
                ],
                success=False,
            )
            if "native DOM Data" not in result.stderr or "emitc.func" in result.stdout:
                raise RuntimeError(f"{name}: missing complete native source refusal")

    refuse(subject, contract, "budget", budget=0)
    # The last payload set now targets another input. Equal inputs still work;
    # distinct inputs leave the following .value read without an object.
    last_set = list(re.finditer(r"ctjs.call_direct @fn\$4\([^\n]+", adapted))[-1]
    changed = last_set[0].replace("%dom_element", "%dom_other")
    if changed == last_set[0]:
        raise RuntimeError("alias counterexample lost its final payload set")
    unsafe = args.work / "unsafe-alias.mlir"
    unsafe.write_text(adapted[: last_set.start()] + changed + adapted[last_set.end() :])
    unsafe_contract = dict(contract, module_sha256=host.fingerprint(args.opt, unsafe))
    refuse(unsafe, unsafe_contract, "unsafe-alias")

    for optimize in (False, True):
        native = args.work / f"{optimize}.native.mlir"
        run(
            [
                args.opt,
                str(subject),
                f"--pass-pipeline=builtin.module(ctnative-lower-to-emitc{{host-manifest={config} host-max-steps=1000000 optimize={str(optimize).lower()}}},{dom.CLEANUP})",
                "-o",
                str(native),
            ]
        )
        if len(dom.NATIVE.findall(native.read_text())) != 7 or dom.FUNCTION.search(
            native.read_text()
        ):
            raise RuntimeError("original Bootstrap did not preserve all seven functions")
        if "ctnative.host_owner_proved = true" not in native.read_text():
            raise RuntimeError("normalized Bootstrap lacks complete owner proof")
        if not optimize:
            low, high = 0, 1000000
            while high - low > 1:
                middle = (low + high) // 2
                checked = subprocess.run(
                    [
                        args.opt,
                        str(subject),
                        f"--ctnative-lower-to-emitc=host-manifest={config} host-max-steps={middle} optimize=false",
                        "-o",
                        "/dev/null",
                    ],
                    text=True,
                    capture_output=True,
                )
                if checked.returncode == 0:
                    high = middle
                else:
                    low = middle
            refuse(subject, contract, "final-cutoff", budget=high - 1)
            print(
                f"Bootstrap DOM native proof: minimum work budget {high}; preceding cutoff refuses"
            )
        deduced = args.work / f"{optimize}.deduced.mlir"
        run([args.opt, str(native), "--ctnative-print-deduced", "-o", str(deduced)])
        for mode, module in (("explicit", native), ("deduced", deduced)):
            cpp = run([args.translate, "--mlir-to-cpp", str(module)]).stdout
            owners = re.findall(r"class (\w+_session) \{", cpp)
            if (
                len(owners) != 1
                or dom.VM.search(cpp)
                or re.search(
                    r"shared_ptr<ctnative::method_|shared_ptr<ctn_|\bmain\s*\(",
                    cpp,
                )
            ):
                raise RuntimeError("Bootstrap DOM retained an escaping root/table/callable")
            entry = function_body(cpp, "_script__0")
            calls = re.findall(r"\bfn_([456])\(", entry)
            if calls != [target[-1] for target in prefix.PROVIDER_OBJECT_TARGETS]:
                raise RuntimeError("Bootstrap DOM changed the 23 direct Data calls")
            if cpp.index("atom_table atoms_") > cpp.index("document document_"):
                raise RuntimeError("document outlives atoms")
            complete = client.replace("@OWNER@", owners[0])
            path = args.work / f"{optimize}.{mode}.cpp"
            path.write_text(cpp + complete)
            for index, compiler in enumerate(compilers):
                binary = path.with_suffix(f".{index}")
                result = run(
                    [compiler, *FLAGS, *includes, str(path), *libraries, "-o", str(binary)]
                )
                if (
                    result.stdout
                    or result.stderr
                    or dom.VM.search(run([args.nm, "-C", str(binary)]).stdout)
                ):
                    raise RuntimeError("Bootstrap DOM did not compile cleanly without Script")
                result = run([str(binary)])
                if result.stdout != "Bootstrap DOM Data passed\n" or result.stderr:
                    raise RuntimeError("Bootstrap DOM observations incomplete")
            if mode == "explicit":
                binary = path.with_suffix(".sanitized")
                run(
                    [
                        compilers[1],
                        *FLAGS,
                        "-O1",
                        "-g",
                        "-fno-omit-frame-pointer",
                        "-fsanitize=address,undefined",
                        "-fsanitize-address-use-after-scope",
                        "-DCTCOMPILE_TEST_DANGLING=1",
                        *includes,
                        str(path),
                        *libraries,
                        "-o",
                        str(binary),
                    ]
                )
                result = run(
                    [str(binary)],
                    environment=dict(
                        os.environ,
                        ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
                        UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
                    ),
                )
                if result.stdout != "Bootstrap DOM Data passed\n" or result.stderr:
                    raise RuntimeError("Bootstrap DOM lifetime check failed")
                broken = path.with_suffix(".missing-reset.cpp")
                changed = cpp.replace("data_table_0 = {};", "")
                if changed == cpp:
                    raise RuntimeError("reset control did not mutate generated storage")
                broken.write_text(changed + complete)
                binary = broken.with_suffix(".bin")
                run([compilers[0], *FLAGS, *includes, str(broken), *libraries, "-o", str(binary)])
                run([str(binary)], success=False)
                for member in (
                    "g_globalThis",
                    "g_console",
                    "g_instance",
                    "data_table_0",
                    "_script__0",
                ):
                    private = path.with_suffix(f".private-{member}.cpp")
                    private.write_text(cpp + f"\nint main() {{ (void)&{owners[0]}::{member}; }}\n")
                    refusal = run(
                        [compilers[0], *FLAGS, *includes, "-fsyntax-only", str(private)],
                        success=False,
                    )
                    if "private" not in refusal.stderr:
                        raise RuntimeError(
                            f"{member}: privacy control failed for an unrelated reason"
                        )
    if js.read_text() != source or subject.read_text() != adapted:
        raise RuntimeError("native gate rewrote its original source or input adapter")
    print(
        "Bootstrap DOM Data: pinned 3218-byte source with three explicit input-key adapters; 7/7 functions, 23 direct calls, 19 observations; "
        "five alias partitions, recorder/payload isolation, reentry, retained/detached keys, domain-first rejection and teardown; "
        "both policies/layouts/compilers, generated-client ASan/UBSan and mutation/privacy controls passed"
    )


if __name__ == "__main__":
    main()
