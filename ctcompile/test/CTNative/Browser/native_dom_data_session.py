#!/usr/bin/env python3
"""Gate private DOM Data storage against the unchanged source proof and real documents."""

import argparse
import importlib.util
import os
from pathlib import Path
import re
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.harness import find_compilers, run
from Target.Cpp.harness import FLAGS

_spec = importlib.util.spec_from_file_location(
    "dom_data_inputs", Path(__file__).parents[1] / "HostContract" / "dom-data-inputs.py"
)
source = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(source)

CLIENT = r"""
#include <cassert>
#include <iostream>
#include <memory>
#include <type_traits>

int main() {
    using namespace ctbrowser;
    using session_type = @OWNER@;
    static_assert(!std::is_copy_constructible_v<session_type>);
    static_assert(!std::is_copy_assignable_v<session_type>);
    static_assert(!std::is_move_constructible_v<session_type>);
    static_assert(!std::is_move_assignable_v<session_type>);
    for (int lifetime = 0; lifetime < 16; ++lifetime) {
        session_type session, second;
        auto & doc = session.document();
        auto & other = second.document();
        auto button = doc.create_element(doc.atoms().intern("button"));
        auto child = doc.create_element(doc.atoms().intern("span"));
        auto foreign_button = other.create_element(other.atoms().intern("button"));
        assert(button == foreign_button);
        assert(doc.append_child(doc.root(), button));
        assert(doc.append_child(button, child));
        const element_ref element{&doc, button}, alias = element;
        const element_ref distinct{&doc, child}, foreign{&other, foreign_button};
        auto snapshot = [&] {
            return std::array{session.observe_traceAfter(), session.observe_traceBefore(),
                              session.observe_traceEntered(), session.observe_traceOther()};
        };
        auto check = [&](bool equal) {
            assert(ctnative::global_number(session.observe_traceEntered()) == 1);
            assert(!ctnative::global_boolean(session.observe_traceBefore()));
            assert(ctnative::global_boolean(session.observe_traceOther()) == equal);
            assert(ctnative::global_boolean(session.observe_traceAfter()) != equal);
        };
        assert(session.observe_traceEntered().tag == ctnative::nullable_scalar::kind::undefined);
        element_ref dangling = foreign;
#ifdef CTCOMPILE_TEST_DANGLING
        {
            atom_table temporary_atoms;
            auto temporary = std::make_unique<document>(temporary_atoms);
            dangling = {temporary.get(), temporary->create_element(temporary_atoms.intern("button"))};
        }
#endif
        auto reject_foreign = [&] {
            auto before = snapshot();
            for (element_ref rejected : {foreign, dangling, element_ref{}}) {
                for (bool first : {false, true}) {
                    bool caught = false;
                    try { (void)session.invoke(first ? rejected : element, first ? alias : rejected); }
                    catch (const std::invalid_argument &) { caught = true; }
                    assert(caught);
                }
                bool domain_first = false;
                try { (void)session.invoke(element_ref{&doc, {}}, rejected); }
                catch (const std::invalid_argument &) { domain_first = true; }
                assert(domain_first);
            }
            auto after = snapshot();
            for (unsigned i = 0; i < before.size(); ++i) {
                assert(before[i].tag == after[i].tag && before[i].value == after[i].value);
            }
        };
        reject_foreign(); // No source store may run before all domains are checked.
        (void)session.invoke(element, alias);
        check(true);
        (void)session.invoke(element, distinct);
        check(false);
        assert(second.observe_traceEntered().tag == ctnative::nullable_scalar::kind::undefined);
        (void)second.invoke(foreign, foreign);
        assert(!ctnative::global_boolean(second.observe_traceAfter()));
        check(false);
        (void)session.invoke(element, distinct); // Source allocates fresh Map on every entry.
        check(false);
        reject_foreign();
        auto before = snapshot();
        const auto text = doc.create_text("text");
        for (element_ref invalid : {element_ref{&doc, {}}, element_ref{&doc, text},
                 element_ref{&doc, {button.slot, button.generation + 2}}}) {
            for (bool first : {false, true}) {
                bool caught = false;
                try { (void)session.invoke(first ? invalid : element, first ? alias : invalid); }
                catch (const std::exception &) { caught = true; }
                assert(caught);
            }
        }
        auto after = snapshot();
        for (unsigned i = 0; i < before.size(); ++i) {
            assert(before[i].tag == after[i].tag && before[i].value == after[i].value);
        }
        // The table retains element while detachment preserves document/node identity.
        assert(doc.remove_child(button));
        assert(!doc.read().parent(button));
        (void)session.invoke(element, distinct);
        check(false);
        // The private table dies before the document; no external capture survives.
    }
    std::cout << "DOM Data passed\n";
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
    read_client = (
        CLIENT.replace(
            "assert(!ctnative::global_boolean(session.observe_traceBefore()));",
            "assert(session.observe_traceBefore().tag == ctnative::nullable_scalar::kind::undefined);",
        )
        .replace(
            "assert(ctnative::global_boolean(session.observe_traceOther()) == equal);",
            "assert(equal ? ctnative::global_number(session.observe_traceOther()) == 42 : "
            "session.observe_traceOther().tag == ctnative::nullable_scalar::kind::undefined);",
        )
        .replace(
            "assert(ctnative::global_boolean(session.observe_traceAfter()) != equal);",
            "assert(equal ? session.observe_traceAfter().tag == ctnative::nullable_scalar::kind::undefined : "
            "ctnative::global_number(session.observe_traceAfter()) == 42);",
        )
        .replace(
            "assert(!ctnative::global_boolean(second.observe_traceAfter()));",
            "assert(second.observe_traceAfter().tag == ctnative::nullable_scalar::kind::undefined);",
        )
    )
    for variant, original, client_source in (
        ("has", source.SOURCE, CLIENT),
        (
            "read",
            source.SOURCE.replace("return state.has(key);", "return state.get(key);"),
            read_client,
        ),
        (
            "snapshot",
            source.SOURCE.replace(
                "state.set(key, value); return state.size;",
                "state.set(key, value); return Array.from(state.values()).length;",
            ),
            CLIENT,
        ),
    ):
        js, ir, contract, count = source.prepare(args, "data-" + variant, original)
        if count != 7:
            raise RuntimeError("Data source census changed")
        report, _ = source.check_report(
            args, ir, contract, "source-proof-" + variant, variant != "snapshot"
        )
        observed = args.work / f"{variant}.observed.js"
        observed.write_text(original + source.OBSERVER)
        expected = source.EXPECTED
        if variant == "read":
            expected = (
                expected.replace("traceAfter=true", "traceAfter=42")
                .replace("traceBefore=false", "traceBefore=undefined")
                .replace("traceOther=false", "traceOther=undefined")
            )
        node = source.NODE.replace(
            "typeof value !== 'number'", "typeof value !== 'number' && typeof value !== 'undefined'"
        )
        for command in ([args.node, "-e", node, str(observed)], [args.reference, str(observed)]):
            if run(command).stdout != expected:
                raise RuntimeError(
                    f"{variant}: source observations disagree with native expectations"
                )
        if variant == "snapshot":
            # Preserve the first refused source: helper support alone cannot
            # authorize a missing complete-family snapshot proof.
            if report["reason"] != "property call lacks a current source getter proof":
                raise RuntimeError("snapshot source refusal changed")
            for optimize in (False, True):
                dom.lower(
                    args, ir, contract, variant + str(optimize), optimize=optimize, success=False
                )
            continue
        includes, libraries = dom.link_options(args)
        for optimize in (False, True):
            native = dom.lower(args, ir, contract, variant + str(optimize), optimize=optimize)
            text = native.read_text()
            if len(dom.NATIVE.findall(text)) != count - 1 or dom.FUNCTION.search(text):
                raise RuntimeError("only the proved inert declaration may be omitted")
            deduced = args.work / f"{variant}-{optimize}.deduced.mlir"
            run([args.opt, str(native), "--ctnative-print-deduced", "-o", str(deduced)])
            for mode, module in (("explicit", native), ("deduced", deduced)):
                cpp = run([args.translate, "--mlir-to-cpp", str(module)]).stdout
                owner = re.findall(r"class (\w+_session) \{", cpp)
                if (
                    len(owner) != 1
                    or dom.VM.search(cpp)
                    or re.search(
                        r"shared_ptr<ctnative::method_|shared_ptr<ctn_|invoke_callable|invoke_session|\bmain\s*\(",
                        cpp,
                    )
                ):
                    raise RuntimeError("DOM Data retained an escaping source/table/callable")
                if ("using map_storage = std::map" in cpp) != (variant != "snapshot"):
                    raise RuntimeError("wrong storage for the source snapshot behavior")
                if cpp.index("atom_table atoms_") > cpp.index("document document_"):
                    raise RuntimeError("document outlives atoms")
                client = "\n#include <array>\n" + client_source.replace("@OWNER@", owner[0])
                path = args.work / f"{variant}-{optimize}.{mode}.cpp"
                path.write_text(cpp + client)
                for index, compiler in enumerate(compilers):
                    binary = path.with_suffix(f".{index}")
                    compiled = run(
                        [compiler, *FLAGS, *includes, str(path), *libraries, "-o", str(binary)]
                    )
                    if compiled.stdout or compiled.stderr:
                        raise RuntimeError("DOM Data did not compile cleanly")
                    if dom.VM.search(run([args.nm, "-C", str(binary)]).stdout):
                        raise RuntimeError("DOM Data linked Script/AOT")
                    if run([str(binary)]).stdout != "DOM Data passed\n":
                        raise RuntimeError("DOM Data observations incomplete")
                if mode == "explicit":
                    sanitized = path.with_suffix(".sanitized")
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
                            str(sanitized),
                        ]
                    )
                    result = run(
                        [str(sanitized)],
                        environment=dict(
                            os.environ,
                            ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
                            UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
                        ),
                    )
                    if result.stdout != "DOM Data passed\n" or result.stderr:
                        raise RuntimeError("DOM Data lifetime check failed")
                    broken = path.with_suffix(".missing-reset.cpp")
                    changed = cpp.replace("data_table_0 = {};", "")
                    if changed == cpp:
                        raise RuntimeError("reset mutation did not change generated storage")
                    broken.write_text(changed + client)
                    binary = broken.with_suffix(".bin")
                    run(
                        [
                            compilers[0],
                            *FLAGS,
                            *includes,
                            str(broken),
                            *libraries,
                            "-o",
                            str(binary),
                        ]
                    )
                    run([str(binary)], success=False)
                    private = path.with_suffix(".private.cpp")
                    private.write_text(cpp + f"\nint main() {{ {owner[0]} s; (void)s.g_host; }}\n")
                    run(
                        [compilers[0], *FLAGS, *includes, "-fsyntax-only", str(private)],
                        success=False,
                    )
        if js.read_text() != original:
            raise RuntimeError("native gate changed original source")
    print(
        "DOM Data: 2 sources, each 7 source / 6 native functions; has/get, one preserved snapshot refusal, private storage, alias/reset/interleaving, "
        "domain-first validation, retained/detached keys and teardown; both policies/layouts/"
        "compilers, ASan/UBSan and mutation/privacy controls passed"
    )


if __name__ == "__main__":
    main()
