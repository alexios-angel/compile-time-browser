#!/usr/bin/env python3
"""Execute original Bootstrap R.find with confined indexed element consumers."""

import argparse
from pathlib import Path
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.Browser.native_dom_document import CURRENT_DOCUMENT_CHECKS, INPUT_CHECKS
from CTNative.Browser.native_dom_spread_length import FIND
from CTNative.harness import find_compilers

BODY = """  if (anchor.hasAttribute('data-invalid')) {
    anchor.setAttribute('data-before', 'yes');
    const invalid = R.find('['@RECEIVER@);
    anchor.setAttribute('data-after', 'no');
    return invalid.length;
  }
  const nodes = R.find('.selected, .selected'@RECEIVER@);
  for (let i = 0; i < nodes.length; i++) {
    const node = nodes[i];
    node.setAttribute('data-index', i.toString());
    node.classList.remove('selected');
    node.setAttribute('data-hovered', node.matches(':hover'));
    node.setAttribute('data-same', node === expected);
  }
  return nodes.length;
"""


def source(kind):
    declaration = f"function findElements(anchor, expected) {{\n  const R={{{FIND}}};\n"
    if kind == "explicit":
        return declaration + BODY.replace("@RECEIVER@", ", anchor") + "}\n"
    if kind == "undefined":
        return (
            declaration
            + "  if (!document.documentElement) return 0;\n"
            + BODY.replace("@RECEIVER@", ", undefined")
            + "}\n"
        )
    return (
        declaration
        + "  if (document.documentElement) {\n"
        + BODY.replace("@RECEIVER@", "")
        + "  }\n  return 0;\n}\n"
    )


CHECKS = r"""
        (void)pressed;
        style::engine selectors{atoms};
        const auto invoke = [&](element_ref anchor, element_ref expected,
                                style::engine & engine) {
            return in_outer_document([&] { return @ENTRY@(anchor, expected, engine); });
        };
        const auto position = atoms.intern("data-index");
        const auto hovered = atoms.intern("data-hovered");
        const auto same = atoms.intern("data-same");
        assert(doc.remove_child(button));
        assert(doc.set_attribute(button, classes, "selected"));
        (void)doc.take_writes();
        assert(invoke(alias, foreign, selectors).value() == 0);
        assert(doc.take_writes().empty());
        const auto later = doc.create_element(atoms.intern("button"));
        const auto first = doc.create_element(atoms.intern("button"));
        assert(doc.append_child(child, first));
        assert(doc.append_child(button, later));
        assert(doc.set_attribute(first, classes, "selected"));
        assert(doc.set_attribute(later, classes, "selected"));
        const element_ref expected{&doc, first};
        const auto outside = doc.create_element(atoms.intern("button"));
        assert(doc.set_attribute(outside, classes, "selected"));
        const auto host = doc.create_element(atoms.intern("div"));
        assert(doc.append_child(button, host));
        const auto shadow = doc.attach_shadow(host, true).value();
        const auto hidden = doc.create_element(atoms.intern("button"));
        assert(doc.append_child(shadow, hidden));
        assert(doc.set_attribute(hidden, classes, "selected"));
        assert(selectors.set_state(first, style::engine::state_hover, true));
        (void)doc.take_writes();
        if (@DEFAULT@) {
            assert(invoke(alias, expected, selectors).value() == 0);
            assert(doc.take_writes().empty()); // A detached input does not create a document root.
        }
        doc.set_document_element(button);
        (void)doc.take_writes();
        assert(invoke(alias, expected, selectors).value() == 2);
        auto writes = doc.take_writes();
        assert(writes.size() == 8);
        std::size_t index = 0;
        for (const auto id : {first, later}) {
            for (const auto name : {position, classes, hovered, same}) {
                assert(writes[index].node == id && writes[index].name == name);
                ++index;
            }
            assert(doc.read().attribute_value(id, classes).empty());
        }
        // Reverse allocation order, duplicate selectors and writes preserve snapshot membership.
        assert(doc.read().attribute_value(first, position) == "0");
        assert(doc.read().attribute_value(later, position) == "1");
        assert(doc.read().attribute_value(first, hovered) == "true");
        assert(doc.read().attribute_value(later, hovered) == "false");
        assert(doc.read().attribute_value(first, same) == "true");
        assert(doc.read().attribute_value(later, same) == "false");
        for (const auto id : {button, outside, hidden}) {
            assert(doc.read().attribute_value(id, classes) == "selected");
            assert(!doc.read().has_attribute(id, position));
        }
        assert(invoke(alias, expected, selectors).value() == 0);
        assert(doc.take_writes().empty());
        assert(doc.set_attribute(first, classes, "selected"));
        assert(selectors.set_state(first, style::engine::state_hover, false));
        doc.remove_document_element();
        (void)doc.take_writes();
        if (@DEFAULT@) {
            assert(invoke(alias, expected, selectors).value() == 0);
            assert(doc.take_writes().empty());
            const auto replacement = doc.create_element(atoms.intern("section"));
            const auto replacement_child = doc.create_element(atoms.intern("button"));
            assert(doc.append_child(replacement, replacement_child));
            assert(doc.set_attribute(replacement_child, classes, "selected"));
            doc.set_document_element(replacement);
            (void)doc.take_writes();
            assert(invoke(alias, element_ref{&doc, replacement_child}, selectors).value() == 1);
            writes = doc.take_writes();
            assert(writes.size() == 4 && writes[0].node == replacement_child);
            assert(doc.read().attribute_value(first, classes) == "selected");
            assert(doc.read().attribute_value(replacement_child, same) == "true");
        } else {
            assert(invoke(alias, foreign, selectors).value() == 1); // Detached explicit receiver.
            assert(doc.take_writes().size() == 4);
            assert(doc.read().attribute_value(first, hovered) == "false");
            assert(doc.read().attribute_value(first, same) == "false");
        }
        assert(doc.set_attribute(button, atoms.intern("data-invalid"), ""));
        (void)doc.take_writes();
        bool rejected = false;
        try { (void)invoke(alias, expected, selectors); }
        catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected && !doc.read().has_attribute(button, atoms.intern("data-after")));
        writes = doc.take_writes();
        assert(writes.size() == 1 && writes[0].name == atoms.intern("data-before"));
"""

OWNED_CHECKS = r"""
        @ENTRY@_session session;
        auto & owned = session.document();
        const auto invoke_owned = [&](element_ref anchor, element_ref expected) {
            return in_outer_document([&] { return session.invoke(anchor, expected); });
        };
        const auto parent = owned.create_element(owned.atoms().intern("section"));
        const auto nested = owned.create_element(owned.atoms().intern("button"));
        assert(owned.append_child(parent, nested));
        const element_ref input{&owned, parent}, expected_owned{&owned, nested};
        assert(owned.set_attribute(nested, owned.atoms().intern("class"), "selected"));
        assert(session.selectors().set_state(nested, style::engine::state_hover, true));
        owned.log_writes(true);
        if (@DEFAULT@) {
            assert(invoke_owned(input, expected_owned).value() == 0);
            assert(owned.take_writes().empty());
        }
        owned.set_document_element(parent);
        (void)owned.take_writes();
        assert(invoke_owned(input, expected_owned).value() == 1);
        assert(owned.read().attribute_value(nested, owned.atoms().intern("data-index")) == "0");
        assert(owned.read().attribute_value(nested, owned.atoms().intern("data-hovered")) == "true");
        assert(owned.read().attribute_value(nested, owned.atoms().intern("data-same")) == "true");
        assert(owned.take_writes().size() == 4);
        assert(invoke_owned(input, expected_owned).value() == 0);
        for (const bool foreign_anchor : {false, true}) {
            bool rejected = false;
            try { (void)invoke_owned(foreign_anchor ? foreign : input,
                                     foreign_anchor ? expected_owned : foreign); }
            catch (const std::invalid_argument &) { rejected = true; }
            assert(rejected && owned.take_writes().empty());
        }
"""

REFUSALS = {
    "unproved-index": "const nodes=R.find('*',anchor); return nodes[0].matches('*');",
    "one-past": "const nodes=R.find('*',anchor); "
    "for(let i=0;i<=nodes.length;i++) nodes[i].setAttribute('x','y'); return true;",
    "offset-index": "const nodes=R.find('*',anchor); "
    "for(let i=0;i<nodes.length;i++) nodes[i+1].setAttribute('x','y'); return true;",
    "wrong-bound": "const nodes=R.find('.selected',anchor); const others=R.find('*',anchor); "
    "for(let i=0;i<others.length;i++) nodes[i].setAttribute('x','y'); return true;",
    "length-write": "const nodes=R.find('*',anchor); nodes.length=0; return true;",
    "member-write": "const nodes=R.find('*',anchor); nodes[0]=anchor; return true;",
    "push": "const nodes=R.find('*',anchor); nodes.push(anchor); return true;",
    "escape": "return R.find('*',anchor);",
    "retained": "anchor.saved=R.find('*',anchor); return true;",
    "identity": "const nodes=R.find('*',anchor); return nodes===nodes;",
    "unguarded-default": "const nodes=R.find('*'); "
    "for(let i=0;i<nodes.length;i++) nodes[i].setAttribute('x','y'); return true;",
    "replace-query-all": "Element.prototype.querySelectorAll=anchor; return R.find('*',anchor).length;",
    "replace-call": "Function.prototype.call=anchor; return R.find('*',anchor).length;",
    "replace-concat": "Array.prototype.concat=anchor; return R.find('*',anchor).length;",
    "element-spreadability": "Element.prototype[Symbol.isConcatSpreadable]=true; "
    "return R.find('*',anchor).length;",
    "node-spreadability": "anchor[Symbol.isConcatSpreadable]=true; return R.find('*',anchor).length;",
    "replace-iterator": "NodeList.prototype[Symbol.iterator]=anchor; return R.find('*',anchor).length;",
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
    refusals = 0
    for kind in ("explicit", "omitted", "undefined"):
        ir, contract = dom.prepare(args, kind, source(kind), 2, entry_name="findElements")
        contract.update(
            current_document_parameter=0,
            initial_intrinsics=["Array", "Element", "Function", "Number"],
        )
        for owned in (False, True):
            manifest = dict(
                contract, provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
            )
            for optimize in (False, True):
                name = f"find-elements-{kind}-{owned}-{optimize}"
                native = dom.lower(args, ir, manifest, name, optimize=optimize)
                text = native.read_text()
                if (
                    "ctnative::Element.prototype.querySelectorAll.call" not in text
                    or "std::vector<ctnative::js_element_t>" not in text
                    or "std::vector<ctbrowser::element_ref>" in text
                    or (kind != "explicit" and "ctnative::document.documentElement" not in text)
                ):
                    raise RuntimeError(f"{name}: R.find bypassed its typed public DOM/Style views")
                client = (
                    CURRENT_DOCUMENT_CHECKS
                    + CHECKS
                    + INPUT_CHECKS
                    + (OWNED_CHECKS if owned else "")
                ).replace("@DEFAULT@", "false" if kind == "explicit" else "true")
                dom.standalone(args, native, name, client, compilers, includes, libraries)
                controls = [
                    ("budget", manifest, 0),
                    ("dataset", dict(manifest, dataset_parameters=[0]), None),
                ]
                if kind != "explicit":
                    missing = dict(manifest)
                    del missing["current_document_parameter"]
                    controls.append(("unbound", missing, None))
                for intrinsic in manifest["initial_intrinsics"]:
                    controls.append(
                        (
                            f"missing-{intrinsic}",
                            dict(
                                manifest,
                                initial_intrinsics=[
                                    item
                                    for item in manifest["initial_intrinsics"]
                                    if item != intrinsic
                                ],
                            ),
                            None,
                        )
                    )
                for suffix, bad, budget in controls:
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
    for name, body in REFUSALS.items():
        ir, manifest = dom.prepare(
            args,
            name,
            f"function bad(anchor, expected) {{ const R={{{FIND}}}; {body} }}\n",
            2,
            entry_name="bad",
        )
        manifest.update(
            current_document_parameter=0,
            initial_intrinsics=["Array", "Element", "Function", "Number"],
        )
        for optimize in (False, True):
            dom.lower(
                args, ir, manifest, f"refused-{name}-{optimize}", optimize=optimize, success=False
            )
            refusals += 1
    print(
        f"Bootstrap R.find elements: 48 native executions, {refusals} refusals; DOM/Core/Style only"
    )


if __name__ == "__main__":
    main()
