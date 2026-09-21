#!/usr/bin/env python3
"""Execute an explicitly bound source document against public DOM/Style."""

import argparse
from pathlib import Path
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.harness import find_compilers

ROOT_SOURCE = """function root(anchor, expected) {
  const root = document.documentElement;
  if (root) {
    root.setAttribute('data-root', 'yes');
    return root === expected;
  }
  return false;
}
"""

QUERY_SOURCE = """function query(anchor, expected) {
  if (anchor.hasAttribute('data-invalid')) {
    anchor.setAttribute('data-before', 'yes');
    document.querySelector('[');
    anchor.setAttribute('data-after', 'no');
    return false;
  }
  const bound = document;
  const found = bound.querySelector('button');
  if (found) {
    found.setAttribute('data-query', found.matches(':hover'));
    return found === expected;
  }
  return false;
}
"""

CHECK_PREFIX = r"""
        (void)pressed;
        style::engine selectors{atoms};
        const auto invoke = [&](element_ref anchor, element_ref expected,
                                style::engine & engine) {
            return @ENTRY@(@ARGUMENTS@, engine);
        };
        assert(doc.remove_child(button)); // A detached input still identifies its document.
        (void)doc.take_writes();
        assert(!invoke(alias, alias, selectors));
        assert(doc.take_writes().empty());
"""

ROOT_CHECKS = r"""
        doc.set_document_element(button);
        (void)doc.take_writes();
        assert(invoke(alias, alias, selectors));
        const auto marked = atoms.intern("data-root");
        assert(doc.read().attribute_value(button, marked) == "yes");
        auto writes = doc.take_writes();
        assert(writes.size() == 1 && writes[0].node == button && writes[0].name == marked);
        assert(!invoke(alias, foreign, selectors)); // Equal node bits in another document differ.
        doc.remove_document_element();
        (void)doc.take_writes();
        assert(!invoke(alias, alias, selectors));
        assert(doc.take_writes().empty());
        const auto replacement = doc.create_element(atoms.intern("html"));
        doc.set_document_element(replacement);
        const element_ref current{&doc, replacement};
        assert(invoke(alias, current, selectors)); // Read the current root on every invocation.
        assert(!invoke(alias, alias, selectors));
        assert(doc.read().attribute_value(replacement, marked) == "yes");
        foreign_doc.set_document_element(other_button);
        style::engine foreign_selectors{foreign_atoms};
        assert(invoke(foreign, foreign, foreign_selectors));
        assert(!invoke(foreign, alias, foreign_selectors));
"""

QUERY_CHECKS = r"""
        doc.set_document_element(button);
        (void)doc.take_writes();
        assert(invoke(alias, alias, selectors)); // Document selectors include the root element.
        const auto marked = atoms.intern("data-query");
        assert(doc.read().attribute_value(button, marked) == "false");
        auto writes = doc.take_writes();
        assert(writes.size() == 1 && writes[0].node == button && writes[0].name == marked);
        assert(selectors.set_state(button, style::engine::state_hover, true));
        assert(invoke(alias, alias, selectors));
        assert(doc.read().attribute_value(button, marked) == "true");
        assert(!invoke(alias, foreign, selectors));
        doc.remove_document_element();
        (void)doc.take_writes();
        assert(!invoke(alias, alias, selectors));
        assert(doc.take_writes().empty());
        const auto replacement = doc.create_element(atoms.intern("button"));
        doc.set_document_element(replacement);
        const element_ref current{&doc, replacement};
        assert(invoke(alias, current, selectors)); // A detached anchor queries its document.
        assert(doc.read().attribute_value(replacement, marked) == "false");
        assert(doc.read().attribute_value(button, marked) == "true");
        assert(selectors.set_state(replacement, style::engine::state_hover, true));
        assert(invoke(alias, current, selectors));
        assert(doc.read().attribute_value(replacement, marked) == "true");
        assert(doc.set_attribute(button, atoms.intern("data-invalid"), ""));
        (void)doc.take_writes();
        bool rejected = false;
        try { (void)invoke(alias, current, selectors); }
        catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected);
        assert(doc.read().attribute_value(button, atoms.intern("data-before")) == "yes");
        assert(!doc.read().has_attribute(button, atoms.intern("data-after")));
        writes = doc.take_writes();
        assert(writes.size() == 1 && writes[0].name == atoms.intern("data-before"));
        assert(doc.remove_attribute(button, atoms.intern("data-invalid")));
"""

INPUT_CHECKS = r"""
        (void)doc.take_writes();
        const auto text = doc.create_text("invalid input");
        for (const element_ref invalid : {
                 element_ref{}, element_ref{&doc, {}}, element_ref{&doc, text},
                 element_ref{&doc, {button.slot, button.generation + 2}}}) {
            for (const bool invalid_anchor : {false, true}) {
                bool rejected = false;
                try { (void)invoke(invalid_anchor ? invalid : alias,
                                   invalid_anchor ? alias : invalid, selectors); }
                catch (const std::exception &) { rejected = true; }
                assert(rejected && doc.take_writes().empty());
            }
        }
        style::engine wrong{foreign_atoms};
        bool wrong_rejected = false;
        try { (void)invoke(alias, alias, wrong); }
        catch (const std::invalid_argument &) { wrong_rejected = true; }
        assert(wrong_rejected && doc.take_writes().empty());
"""

OWNED_CHECKS = r"""
        @ENTRY@_session session;
        auto & owned = session.document();
        const auto owned_node = owned.create_element(owned.atoms().intern("button"));
        const element_ref input{&owned, owned_node};
        assert(!session.invoke(input, input));
        owned.set_document_element(owned_node);
        assert(session.invoke(input, input));
        owned.remove_document_element();
        assert(!session.invoke(input, input));
        const auto owned_replacement = owned.create_element(owned.atoms().intern("button"));
        owned.set_document_element(owned_replacement);
        const element_ref expected{&owned, owned_replacement};
        assert(session.invoke(@OWNED_ARGUMENTS@));
        owned.log_writes(true);
        (void)owned.take_writes();
        bool foreign_rejected = false;
        try { (void)session.invoke(foreign, foreign); }
        catch (const std::invalid_argument &) { foreign_rejected = true; }
        assert(foreign_rejected && owned.take_writes().empty());
"""

REFUSALS = {
    "replace-document": "document=element; return false;",
    "replace-root": "document.documentElement=element; return false;",
    "replace-method": "document.querySelector=element; return false;",
    "replace-alias-method": "const bound=document; bound.querySelector=element; return false;",
    "detached-method": "const select=document.querySelector; return select('button')===element;",
    "coercing-selector": "return document.querySelector(1)===element;",
    "missing-selector": "return document.querySelector()===element;",
    "extra-selector": "return document.querySelector('button', 'extra')===element;",
    "unguarded-root": "return document.documentElement.matches('*');",
    "unguarded-query": "return document.querySelector('button').matches('*');",
    "absent-root": "const root=document.documentElement; if(!root) return root.matches('*'); return false;",
    "root-escape": "return document.documentElement;",
    "query-escape": "return document.querySelector('button');",
    "document-escape": "return document;",
    "method-escape": "return document.querySelector;",
    "unproved-property": "return document.body===element;",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt", "clang"):
        parser.add_argument("--" + name, required=True)
    for name in ("build", "include", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--nm", default=shutil.which("nm"))
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args, selectors=True)
    cases = (
        ("root", ROOT_SOURCE, ROOT_CHECKS, 0),
        (
            "root-second",
            ROOT_SOURCE.replace("(anchor, expected)", "(expected, anchor)"),
            ROOT_CHECKS,
            1,
        ),
        ("query", QUERY_SOURCE, QUERY_CHECKS, 0),
    )
    refusals = 0
    for case, source, checks, anchor in cases:
        ir, contract = dom.prepare(args, case, source, 2)
        contract["current_document_parameter"] = anchor
        for owned in (False, True):
            manifest = dict(
                contract, provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
            )
            for optimize in (False, True):
                name = f"document-{case}-{owned}-{optimize}"
                native = dom.lower(args, ir, manifest, name, optimize=optimize)
                if (
                    '"ctnative::js_document_t"' not in native.read_text()
                    or "ctcompile/CTNative/Runtime/Browser.hpp" not in native.read_text()
                ):
                    raise RuntimeError(f"{name}: source document bypassed its typed browser view")
                client = CHECK_PREFIX + checks + INPUT_CHECKS + (OWNED_CHECKS if owned else "")
                client = client.replace(
                    "@ARGUMENTS@", "expected, anchor" if anchor else "anchor, expected"
                ).replace("@OWNED_ARGUMENTS@", "expected, input" if anchor else "input, expected")
                dom.standalone(args, native, name, client, compilers, includes, libraries)
                missing = dict(manifest)
                del missing["current_document_parameter"]
                for suffix, bad, budget in (
                    ("unbound", missing, None),
                    ("wrong-index", dict(manifest, current_document_parameter=2), None),
                    ("stale", dict(manifest, module_sha256="0" * 64), None),
                    ("budget", manifest, 0),
                ):
                    dom.lower(
                        args,
                        ir,
                        bad,
                        f"{name}-{suffix}",
                        optimize=optimize,
                        max_steps=budget,
                        success=False,
                    )
                    refusals += 1
    sources = {
        name: f"function bad(element, expected) {{ {body} }}\n" for name, body in REFUSALS.items()
    }
    sources["shadowed-document"] = ROOT_SOURCE.replace("(anchor, expected)", "(document, expected)")
    for name, source in sources.items():
        ir, manifest = dom.prepare(args, name, source, 2)
        manifest["current_document_parameter"] = 0
        for optimize in (False, True):
            dom.lower(
                args, ir, manifest, f"refused-{name}-{optimize}", optimize=optimize, success=False
            )
            refusals += 1
    print(
        f"Bound document root/query: {16 * len(cases)} native executions, {refusals} refusals; DOM/Core/Style only"
    )


if __name__ == "__main__":
    main()
