#!/usr/bin/env python3
"""Gate owned synchronous DOM entries; retained Data keys remain outside this contract."""

import argparse
import os
from pathlib import Path
import re
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.harness import find_compilers, run
from CTNative.HostContract.contract import fingerprint
from Target.Cpp.harness import FLAGS

ACTION = dom.ACTION.replace("toggle(element)", "toggle(element, expected)").replace(
    "return active;", "return element === expected;"
)
QUERIES = """function queries(first, second) {
  first.toggleAttribute('data-first', first.matches('button:hover'));
  return second.matches('button:hover');
}
"""
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
        session_type session, foreign_session;
        auto & doc = session.document();
        auto & atoms = doc.atoms();
        auto & foreign_doc = foreign_session.document();
        const auto button = doc.create_element(atoms.intern("button"));
        const auto child = doc.create_element(atoms.intern("span"));
        const auto foreign_button = foreign_doc.create_element(foreign_doc.atoms().intern("button"));
        assert(button == foreign_button); // Same bits, different owners.
        assert(doc.append_child(doc.root(), button));
        assert(doc.append_child(button, child));
        const element_ref element{&doc, button}, alias = element;
        const element_ref descendant{&doc, child}, foreign{&foreign_doc, foreign_button};
        doc.log_writes(true);
        @CHECKS@
        (void)doc.take_writes();

        // A foreign owner need not still be alive: compare domains before dereferencing it.
        element_ref dangling = foreign;
#ifdef CTCOMPILE_TEST_DANGLING
        {
            atom_table temporary_atoms;
            auto temporary = std::make_unique<document>(temporary_atoms);
            dangling = {temporary.get(), temporary->create_element(temporary_atoms.intern("button"))};
        }
#endif
        for (element_ref rejected : {foreign, dangling, element_ref{}}) {
            for (bool first : {false, true}) {
                bool caught = false;
                try {
                    (void)session.invoke(first ? rejected : element, first ? alias : rejected);
                } catch (const std::invalid_argument &) { caught = true; }
                assert(caught && doc.take_writes().empty());
            }
            // Invalid first local ID must not be validated before the later foreign domain.
            bool domain_first = false;
            try { (void)session.invoke(element_ref{&doc, {}}, rejected); }
            catch (const std::invalid_argument &) { domain_first = true; }
            assert(domain_first && doc.take_writes().empty());
        }
        const auto text = doc.create_text("not an element");
        (void)doc.take_writes();
        for (element_ref invalid : {element_ref{&doc, {}}, element_ref{&doc, text},
                 element_ref{&doc, {button.slot, button.generation + 2}}}) {
            for (bool first : {false, true}) {
                bool caught = false;
                try { (void)session.invoke(first ? invalid : element, first ? alias : invalid); }
                catch (const std::exception &) { caught = true; }
                assert(caught && doc.take_writes().empty());
            }
        }
        // Data cannot retain these handles: this provider admits synchronous actions only.
    }
    std::cout << "owned DOM passed\n";
}
"""
ACTION_CHECKS = r"""
        const auto classes = atoms.intern("class"), pressed = atoms.intern("aria-pressed");
        for (bool active : {true, false}) {
            assert(session.invoke(element, alias));
            assert(doc.read().attribute_value(button, classes) == (active ? "active" : ""));
            assert(doc.read().attribute_value(button, pressed) == (active ? "true" : "false"));
            assert(doc.take_writes().size() == 2);
        }
        assert(!session.invoke(element, descendant));
        assert(doc.remove_child(button));
        assert(!doc.read().parent(button) && doc.read().parent(child) == button);
        assert(session.invoke(element, alias));
        assert(doc.read().attribute_value(button, pressed) == "false");
"""
CLOSEST_CHECKS = r"""
        assert(doc.set_attribute(button, atoms.intern("data-bs-toggle"), "button"));
        assert(session.invoke(descendant, element));
        assert(!session.invoke(descendant, descendant));
        assert(doc.remove_child(button));
        assert(session.invoke(descendant, alias));
"""
QUERY_CHECKS = r"""
        auto & selectors = session.selectors();
        assert(selectors.set_state(button, style::engine::state_hover, true));
        assert(session.invoke(element, alias));
        assert(doc.read().has_attribute(button, atoms.intern("data-first")));
        assert(!session.invoke(element, descendant));
        assert(selectors.set_state(button, style::engine::state_hover, false));
        assert(!session.invoke(element, alias));
        assert(!doc.read().has_attribute(button, atoms.intern("data-first")));
        assert(doc.remove_child(button));
        assert(selectors.set_state(button, style::engine::state_hover, true));
        assert(session.invoke(element, alias));
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--include", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--nm", default=shutil.which("nm"))
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    compilers = find_compilers()
    compilers[1] = args.clang
    for name, source, checks in (
        ("action", ACTION, ACTION_CHECKS),
        ("closest", dom.CLOSEST, CLOSEST_CHECKS),
        ("queries", QUERIES, QUERY_CHECKS),
    ):
        ir, contract = dom.prepare(args, name, source, 2)
        contract["provider"] = "ctbrowser-dom-session-v1"
        if name == "action":
            stale = dict(contract, module_sha256="0" * 64)
            if "fingerprint mismatch" not in dom.lower(args, ir, stale, "stale", success=False):
                raise RuntimeError("stale session contract acquired a document owner")
            dom.lower(
                args, ir, dict(contract, element_parameters=[0]), "missing-input", success=False
            )
            dom.lower(args, ir, dict(contract, roots=[]), "mixed-contract", success=False)
            forged = args.work / "forged.mlir"
            text, count = re.subn(
                r"\bmodule attributes \{",
                "module attributes {ctnative.host_dom_session = true, ",
                ir.read_text(),
                count=1,
            )
            if not count:
                text = text.replace(
                    "module {", "module attributes {ctnative.host_dom_session = true} {", 1
                )
            forged.write_text(text)
            if forged.read_text() == ir.read_text():
                raise RuntimeError("forged session control did not add an attribute")
            unproved = run(
                [args.opt, str(forged), "--ctnative-lower-to-emitc=optimize=false"]
            ).stdout
            if "ctbrowser::element_ref" in unproved or "class toggle_1_session" in unproved:
                raise RuntimeError("an IR annotation authorized a DOM session without a manifest")
        includes, libraries = dom.link_options(args, selectors=name != "action")
        for optimize in (False, True):
            label = f"{name}-{optimize}"
            native = dom.lower(args, ir, contract, label, optimize=optimize)
            entries = dom.NATIVE.findall(native.read_text())
            if len(entries) != 1 or dom.FUNCTION.search(native.read_text()):
                raise RuntimeError("owned DOM must contain exactly one native source entry")
            deduced = args.work / f"{label}.deduced.mlir"
            run([args.opt, str(native), "--ctnative-print-deduced", "-o", str(deduced)])
            for mode, module in (("explicit", native), ("deduced", deduced)):
                cpp = run([args.translate, "--mlir-to-cpp", str(module)]).stdout
                if dom.VM.search(cpp) or re.search(
                    r"shared_ptr|weak_ptr|nullable_scalar|invoke_callable|\bmain\s*\(", cpp
                ):
                    raise RuntimeError("owned DOM output contains runtime/callable storage")
                if cpp.index("atom_table atoms_") > cpp.index("document document_"):
                    raise RuntimeError("document outlives its atom table")
                client = CLIENT.replace("@OWNER@", entries[0] + "_session").replace(
                    "@CHECKS@", checks
                )
                path = args.work / f"{label}.{mode}.cpp"
                path.write_text(cpp + client)
                for index, compiler in enumerate(compilers):
                    binary = path.with_suffix(f".{index}")
                    built = run(
                        [
                            compiler,
                            *FLAGS,
                            *includes,
                            str(path),
                            *libraries,
                            "-o",
                            str(binary),
                        ]
                    )
                    if built.stdout or built.stderr:
                        raise RuntimeError("owned DOM did not compile cleanly")
                    if dom.VM.search(run([args.nm, "-C", str(binary)]).stdout):
                        raise RuntimeError("owned DOM links Script/AOT")
                    if run([str(binary)]).stdout != "owned DOM passed\n":
                        raise RuntimeError("owned DOM observations incomplete")
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
                    if result.stdout != "owned DOM passed\n" or result.stderr:
                        raise RuntimeError("owned DOM lifetime check failed")

    refusals = (
        "function invalid(element) { return element; }",
        "function invalid(element) { saved = element; return true; }",
        "function invalid(element) { const data = new Map(); data.set(element, 1); return true; }",
        "function invalid(element) { element.saved = element; return true; }",
        "function invalid(element) { element.addEventListener('click', () => element); return true; }",
    )
    for index, source in enumerate(refusals):
        js = args.work / f"refused-{index}.js"
        ir = js.with_suffix(".mlir")
        js.write_text(source)
        run([args.translate, "--ctbrowser-js-to-ctjs", str(js), "-o", str(ir)])
        entry = next(
            symbol for symbol in dom.FUNCTION.findall(ir.read_text()) if symbol != "_script_$0"
        )
        contract = {
            "version": 1,
            "provider": "ctbrowser-dom-session-v1",
            "module_sha256": fingerprint(args.opt, ir),
            "entry": entry,
            "element_parameters": [0],
        }
        dom.lower(args, ir, contract, f"refused-{index}", success=False)
    print(
        "owned DOM: 3 sources, 5 retention refusals, both policies/layouts/compilers, ASan/UBSan passed"
    )


if __name__ == "__main__":
    main()
