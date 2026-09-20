#!/usr/bin/env python3
"""Execute confined Bootstrap R.find and NodeList spread/concat counts."""

import argparse
from pathlib import Path
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.harness import find_compilers

FIND = (
    "find: (t, e = document.documentElement) => "
    "[].concat(...Element.prototype.querySelectorAll.call(e, t))"
)
SOURCE = """function spreadLength(element) {
  if (element.hasAttribute('data-invalid')) {
    element.setAttribute('data-before', 'yes');
    const invalid = [].concat(...element.querySelectorAll('['));
    element.setAttribute('data-after', 'no');
    return invalid.length;
  }
  const selector = element.hasAttribute('data-scope') ? ':scope > .selected' : '.selected, .selected';
  const nodes = Element.prototype.querySelectorAll.call(element, selector);

  const copied = [].concat(...nodes);
  const first = element.querySelector('.selected');
  if (first) first.classList.remove('selected');
  return nodes.length + copied.length + element.querySelectorAll(selector).length;
}
"""

BOOTSTRAP_SOURCE = (
    "function spreadLength(element) {\n"
    f"  const R={{{FIND}}};\n"
    "  const nodes=element.querySelectorAll('.selected');\n"
    "  const copied=R.find('.selected', element);\n"
    "  const first=element.querySelector('.selected');\n"
    "  if(first) first.classList.remove('selected');\n"
    "  return nodes.length + copied.length + element.querySelectorAll('.selected').length;\n"
    "}\n"
)

BOOTSTRAP_CHECKS = r"""
        (void)pressed;
        (void)foreign;
        style::engine selectors{atoms};
        assert(@ENTRY@(alias, selectors).value() == 0);
        assert(doc.set_attribute(child, classes, "selected"));
        const auto nested = doc.create_element(atoms.intern("button"));
        assert(doc.append_child(child, nested));
        assert(doc.set_attribute(nested, classes, "selected"));
        (void)doc.take_writes();
        assert(@ENTRY@(alias, selectors).value() == 5);
        auto writes = doc.take_writes();
        assert(writes.size() == 1 && writes.front().node == child);
        assert(@ENTRY@(alias, selectors).value() == 2);
        writes = doc.take_writes();
        assert(writes.size() == 1 && writes.front().node == nested);
        assert(@ENTRY@(alias, selectors).value() == 0 && doc.take_writes().empty());
"""

CHECKS = r"""
        (void)pressed;
        (void)foreign;
        style::engine selectors{atoms};
        assert(doc.set_attribute(button, classes, "selected"));
        (void)doc.take_writes();
        assert(@ENTRY@(alias, selectors).value() == 0); // Root exclusion and empty results.
        assert(doc.take_writes().empty());
        const auto nested = doc.create_element(atoms.intern("button"));
        assert(doc.append_child(child, nested));
        const auto direct = doc.create_element(atoms.intern("button"));
        assert(doc.append_child(button, direct));
        const auto outside = doc.create_element(atoms.intern("button"));
        assert(doc.append_child(doc.root(), outside));
        const auto host = doc.create_element(atoms.intern("div"));
        assert(doc.append_child(button, host));
        const auto shadow = doc.attach_shadow(host, true).value();
        const auto hidden = doc.create_element(atoms.intern("button"));
        assert(doc.append_child(shadow, hidden));
        for (const auto node : {nested, direct, outside, hidden})
            assert(doc.set_attribute(node, classes, "selected"));
        (void)doc.take_writes();
        assert(@ENTRY@(alias, selectors).value() == 5); // Saved 2 + copied 2 + fresh 1.
        auto writes = doc.take_writes();
        assert(writes.size() == 1 && writes.front().node == nested);
        for (const auto node : {button, direct, outside, hidden})
            assert(doc.read().attribute_value(node, classes) == "selected");
        assert(@ENTRY@(alias, selectors).value() == 2); // Saved 1 + copied 1 + fresh 0.
        writes = doc.take_writes();
        assert(writes.size() == 1 && writes.front().node == direct);
        assert(@ENTRY@(alias, selectors).value() == 0);
        assert(doc.take_writes().empty());
        assert(doc.set_attribute(nested, classes, "selected"));
        assert(doc.set_attribute(direct, classes, "selected"));
        assert(doc.set_attribute(button, atoms.intern("data-scope"), ""));
        (void)doc.take_writes();
        assert(@ENTRY@(alias, selectors).value() == 3); // Scoped list stays 1 after nested mutation.
        assert(doc.remove_attribute(button, atoms.intern("data-scope")));
        assert(doc.remove_child(button));
        (void)doc.take_writes();
        assert(@ENTRY@(alias, selectors).value() == 2); // Detached root still queries its descendants.
        assert(doc.set_attribute(button, atoms.intern("data-invalid"), ""));
        (void)doc.take_writes();
        bool rejected = false;
        try { (void)@ENTRY@(alias, selectors); }
        catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected && !doc.read().has_attribute(button, atoms.intern("data-after")));
        writes = doc.take_writes();
        assert(writes.size() == 1 && writes.front().name == atoms.intern("data-before"));
        style::engine wrong{foreign_atoms};
        rejected = false;
        try { (void)@ENTRY@(alias, wrong); }
        catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected && doc.take_writes().empty());
"""

OWNED_CHECKS = r"""
    {
        @ENTRY@_session session;
        auto & owned = session.document();
        const auto parent = owned.create_element(owned.atoms().intern("section"));
        const auto nested = owned.create_element(owned.atoms().intern("button"));
        assert(owned.append_child(parent, nested));
        assert(owned.set_attribute(nested, owned.atoms().intern("class"), "selected"));
        owned.log_writes(true);
        assert(session.invoke(element_ref{&owned, parent}).value() == 2);
        assert(owned.take_writes().size() == 1);
        assert(session.invoke(element_ref{&owned, parent}).value() == 0);
        bool rejected = false;
        try { (void)session.invoke(alias); }
        catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected && owned.take_writes().empty());
    }
"""

REFUSALS = {
    "nonempty-receiver": "return [element].concat(...nodes).length;",
    "extra-argument": "return [].concat(element, ...nodes).length;",
    "extra-spread": "return [].concat(...nodes, ...nodes).length;",
    "mutated-receiver": "const target=[]; target.push(element); "
    "return target.concat(...nodes).length;",
    "detached-concat": "const concat=[].concat; return concat(...nodes).length;",
    "replacement": "Array.prototype.concat=element; return [].concat(...nodes).length;",
    "spreadability": "element[Symbol.isConcatSpreadable]=true; return [].concat(...nodes).length;",
    "escaped": "return [].concat(...nodes);",
    "indexed": "return [].concat(...nodes)[0];",
    "identity": "const copy=[].concat(...nodes); return copy===copy;",
    "mutated-result": "const copy=[].concat(...nodes); copy.length=0; return copy.length;",
    "wrong-snapshot": "return [].concat(...'abc').length;",
    "default-document": f"const R={{{FIND}}}; return R.find('*').length;",
    "selector-coercion": "return [].concat(...element.querySelectorAll(1)).length;",
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
    vendor = Path(__file__).resolve().parents[4] / "ctbrowser/vendor/bootstrap/bootstrap.bundle.js"
    if FIND + "," not in vendor.read_text():
        raise RuntimeError("Bootstrap R.find source pin changed")
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args, selectors=True)
    for name, source, checks in (
        ("bootstrap", BOOTSTRAP_SOURCE, BOOTSTRAP_CHECKS),
        ("direct", SOURCE, CHECKS),
    ):
        ir, contract = dom.prepare(args, name, source, 1, entry_name="spreadLength")
        contract["initial_intrinsics"] = ["Array", "Element", "Function"]
        for owned in (False, True):
            manifest = dict(
                contract, provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
            )
            for optimize in (False, True):
                case = f"spread-{name}-{owned}-{optimize}"
                native = dom.lower(args, ir, manifest, case, optimize=optimize)
                if '"ctnative::Element.prototype.querySelectorAll.call"' not in native.read_text():
                    raise RuntimeError("spread count lost its public Style selector call")
                dom.standalone(
                    args,
                    native,
                    case,
                    checks + (OWNED_CHECKS if owned else ""),
                    compilers,
                    includes,
                    libraries,
                )
                for missing in ("Array", "Element", "Function"):
                    dom.lower(
                        args,
                        ir,
                        dict(
                            manifest,
                            initial_intrinsics=[
                                item for item in manifest["initial_intrinsics"] if item != missing
                            ],
                        ),
                        f"{case}-missing-{missing}",
                        optimize=optimize,
                        success=False,
                    )
    for name, body in REFUSALS.items():
        source = (
            "function bad(element) { const nodes=element.querySelectorAll('*'); " + body + " }\n"
        )
        ir, contract = dom.prepare(args, name, source, 1, entry_name="bad")
        contract["initial_intrinsics"] = ["Array", "Element", "Function"]
        for optimize in (False, True):
            dom.lower(
                args, ir, contract, f"refused-{name}-{optimize}", optimize=optimize, success=False
            )
    print(
        f"DOM spread/concat counts: 32 native executions, {24 + 2 * len(REFUSALS)} refusals; DOM/Core/Style only"
    )


if __name__ == "__main__":
    main()
