#!/usr/bin/env python3
"""Check native Element.querySelector against the shared public selector engine."""

import argparse
from pathlib import Path
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.harness import find_compilers

SOURCE = """function query(element, expected) {
  if (element.hasAttribute('data-invalid')) {
    element.setAttribute('data-before', 'yes');
    element.querySelector('[');
    element.setAttribute('data-after', 'no');
    return false;
  }
  const selector = element.hasAttribute('data-scope') ? ':scope > button' : 'button';
  const found = element.querySelector(selector);
  if (found) {
    found.setAttribute('data-query', found.matches(':scope'));
    return found === expected;
  }
  return false;
}
"""

CHECKS = r"""
        style::engine selectors{atoms};
        (void)pressed;
        // The root matches button but querySelector must exclude it.
        (void)doc.take_writes();
        assert(!@ENTRY@(alias, alias, selectors));
        assert(doc.take_writes().empty());
        const auto later = doc.create_element(atoms.intern("button"));
        const auto first = doc.create_element(atoms.intern("button"));
        assert(doc.append_child(child, first));
        assert(doc.append_child(button, later));
        const element_ref selected{&doc, first}, direct{&doc, later};
        (void)doc.take_writes();
        assert(@ENTRY@(alias, selected, selectors)); // Tree order, not allocation order.
        const auto marked = atoms.intern("data-query");
        assert(doc.read().attribute_value(first, marked) == "true");
        assert(!doc.read().has_attribute(later, marked));
        auto writes = doc.take_writes();
        assert(writes.size() == 1 && writes[0].node == first && writes[0].name == marked);
        assert(!@ENTRY@(alias, foreign, selectors)); // Identity includes document.
        (void)doc.take_writes();
        const auto scope = atoms.intern("data-scope");
        assert(doc.set_attribute(button, scope, ""));
        (void)doc.take_writes();
        assert(@ENTRY@(alias, direct, selectors)); // :scope anchors to the queried root.
        writes = doc.take_writes();
        assert(writes.size() == 1 && writes[0].node == later);
        assert(doc.remove_child(button));
        assert(@ENTRY@(alias, direct, selectors)); // Detached subtrees still query normally.
        assert(doc.remove_attribute(button, scope));
        assert(doc.remove_child(first));
        assert(doc.append_child(button, first));
        assert(@ENTRY@(alias, direct, selectors)); // Source calls observe the current tree order.
        (void)doc.take_writes();
        const auto host = doc.create_element(atoms.intern("div"));
        const auto shadow = doc.attach_shadow(host, true).value();
        const auto hidden = doc.create_element(atoms.intern("button"));
        assert(doc.append_child(shadow, hidden));
        assert(doc.append_child(doc.root(), host));
        (void)doc.take_writes();
        assert(!@ENTRY@(element_ref{&doc, host}, element_ref{&doc, hidden}, selectors));
        assert(doc.take_writes().empty()); // A host query never crosses its shadow root.
        // A derived selector error preserves effects before the call only.
        assert(doc.set_attribute(button, atoms.intern("data-invalid"), ""));
        (void)doc.take_writes();
        bool rejected = false;
        try { (void)@ENTRY@(alias, direct, selectors); }
        catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected);
        assert(doc.read().attribute_value(button, atoms.intern("data-before")) == "yes");
        assert(!doc.read().has_attribute(button, atoms.intern("data-after")));
        writes = doc.take_writes();
        assert(writes.size() == 1 && writes[0].name == atoms.intern("data-before"));
        style::engine wrong{foreign_atoms};
        rejected = false;
        try { (void)@ENTRY@(alias, direct, wrong); }
        catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected && doc.take_writes().empty()); // Validate engines before source effects.
"""

OWNED_CHECKS = r"""
        @ENTRY@_session session;
        auto & owned = session.document();
        const auto parent = owned.create_element(owned.atoms().intern("section"));
        const auto nested = owned.create_element(owned.atoms().intern("button"));
        assert(owned.append_child(parent, nested));
        assert(session.invoke(element_ref{&owned, parent}, element_ref{&owned, nested}));
        assert(owned.read().attribute_value(nested, owned.atoms().intern("data-query")) == "true");
"""


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
    ir, contract = dom.prepare(args, "query", SOURCE, 2)
    for owned in (False, True):
        manifest = dict(
            contract, provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
        )
        for optimize in (False, True):
            name = f"query-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, name, optimize=optimize)
            if '"ctnative::query_selector"' not in native.read_text():
                raise RuntimeError("querySelector bypassed the public selector wrapper")
            checks = CHECKS + (OWNED_CHECKS if owned else "")
            dom.standalone(args, native, name, checks, compilers, includes, libraries)
    refusals = (
        "return element.querySelector('button').matches(':scope');",
        "return element.querySelector('button');",
        "return element.querySelector();",
        "return element.querySelector(1);",
        "return element.querySelector('button', 'extra');",
        "return element.querySelectorAll('button');",
        "const query=element.querySelector; return query('button');",
    )
    for index, body in enumerate(refusals):
        source = f"function bad(element, expected) {{ {body} }}\n"
        refused, manifest = dom.prepare(args, f"refused-{index}", source, 2)
        for optimize in (False, True):
            dom.lower(
                args,
                refused,
                manifest,
                f"refused-{index}-{optimize}",
                optimize=optimize,
                success=False,
            )
    print("Element.querySelector: 16 native executions, 14 refusals; DOM/Core/Style only")


if __name__ == "__main__":
    main()
