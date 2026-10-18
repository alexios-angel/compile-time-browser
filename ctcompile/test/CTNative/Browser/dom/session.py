#!/usr/bin/env python3
"""Test natively compiled DOM entry functions that take element handles and run synchronously.

The generated session class owns its document. Entries are checked against
foreign, dangling and invalid handles, and under ASan/UBSan. Programs that keep
a handle beyond the call (as Bootstrap's Data store keeps them) are outside
this host contract and must be refused.
"""

import re

from CTNative.Browser.dom import dom
from CTNative.harness import build_and_run, forge_module, run
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
CLIENT = (
    r"""
#include <cassert>
#include <iostream>
#include <memory>
#include <type_traits>
"""
    + dom.SESSION_PREAMBLE
    + r"""
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
)
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


def main(argv=None):
    args = dom.parse_args(__doc__, argv=argv)
    compilers = args.compilers
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
            forged.write_text(forge_module(ir.read_text(), "ctnative.host_dom_session = true"))
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
                flags = [*FLAGS, *includes]
                for index, compiler in enumerate(compilers):
                    binary = path.with_suffix(f".{index}")
                    result = build_and_run(
                        compiler,
                        path,
                        binary,
                        flags=flags,
                        libraries=libraries,
                        nm=args.nm,
                        vm=dom.VM,
                        clean=True,
                    )
                    if result.stdout != "owned DOM passed\n":
                        raise RuntimeError("owned DOM observations incomplete")
                if mode == "explicit":
                    result = build_and_run(
                        compilers[1],
                        path,
                        path.with_suffix(".sanitized"),
                        flags=[*flags, "-DCTCOMPILE_TEST_DANGLING=1"],
                        libraries=libraries,
                        sanitize=True,
                    )
                    if result.stdout != "owned DOM passed\n" or result.stderr:
                        raise RuntimeError("owned DOM lifetime check failed")

    # Each program keeps the element handle beyond the call (returned, stored in a
    # global, a Map, an expando or a listener closure), so each must be refused.
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


def test_session(lit):
    main(lit.args("%dom_driver"))


if __name__ == "__main__":
    main()
