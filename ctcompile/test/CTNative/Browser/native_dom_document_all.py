#!/usr/bin/env python3
"""Execute static bound-document snapshots against public DOM/Style."""

import argparse
from pathlib import Path
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.Browser.native_dom_document import INPUT_CHECKS
from CTNative.Browser.native_dom_query_all import REFUSALS as ELEMENT_REFUSALS
from CTNative.harness import find_compilers

SOURCE = """function queryAll(anchor, expected) {
  if (anchor.hasAttribute('data-invalid')) {
    anchor.setAttribute('data-before', 'yes');
    document.querySelectorAll('[');
    anchor.setAttribute('data-after', 'no');
    return 0;
  }
  const bound = document;
  if (anchor.hasAttribute('data-duplicate')) {
    return bound.querySelectorAll('.selected, button').length;
  }
  const nodes = bound.querySelectorAll('.selected');
  for (let i = 0; i < nodes.length; i++) {
    const node = nodes[i];
    node.setAttribute('data-index', i.toString());
    node.classList.remove('selected');
    node.setAttribute('data-hovered', node.matches(':hover'));
  }
  return nodes.length;
}
"""

CHECKS = r"""
        (void)pressed;
        style::engine selectors{atoms};
        const auto invoke = [&](element_ref anchor, element_ref expected,
                                style::engine & engine) {
            return @ENTRY@(@ARGUMENTS@, engine);
        };
        const auto position = atoms.intern("data-index");
        const auto hovered = atoms.intern("data-hovered");
        assert(doc.set_attribute(button, classes, "selected"));
        assert(doc.remove_child(button));
        (void)doc.take_writes();
        assert(invoke(alias, foreign, selectors).value() == 0);
        assert(doc.take_writes().empty()); // The matching input subtree is detached.
        doc.set_document_element(button);
        const auto later = doc.create_element(atoms.intern("button"));
        const auto first = doc.create_element(atoms.intern("button"));
        assert(doc.append_child(child, first));
        assert(doc.append_child(button, later));
        assert(doc.set_attribute(first, classes, "selected"));
        assert(doc.set_attribute(later, classes, "selected"));
        const auto outside = doc.create_element(atoms.intern("button"));
        assert(doc.set_attribute(outside, classes, "selected"));
        const auto host = doc.create_element(atoms.intern("div"));
        assert(doc.append_child(button, host));
        const auto shadow = doc.attach_shadow(host, true).value();
        const auto hidden = doc.create_element(atoms.intern("button"));
        assert(doc.append_child(shadow, hidden));
        assert(doc.set_attribute(hidden, classes, "selected"));
        assert(selectors.set_state(first, style::engine::state_hover, true));
        const auto duplicate = atoms.intern("data-duplicate");
        assert(doc.set_attribute(button, duplicate, ""));
        (void)doc.take_writes();
        assert(invoke(alias, foreign, selectors).value() == 3);
        assert(doc.take_writes().empty()); // Matching root included, each node only once.
        assert(doc.remove_attribute(button, duplicate));
        (void)doc.take_writes();
        assert(invoke(alias, foreign, selectors).value() == 3);
        auto writes = doc.take_writes();
        assert(writes.size() == 9);
        std::size_t index = 0;
        for (const auto id : {button, first, later}) {
            assert(writes[index].node == id && writes[index++].name == position);
            assert(writes[index].node == id && writes[index++].name == classes);
            assert(writes[index].node == id && writes[index++].name == hovered);
            assert(doc.read().attribute_value(id, classes).empty());
        }
        assert(doc.read().attribute_value(button, position) == "0");
        assert(doc.read().attribute_value(first, position) == "1");
        assert(doc.read().attribute_value(later, position) == "2");
        assert(doc.read().attribute_value(first, hovered) == "true");
        assert(doc.read().attribute_value(later, hovered) == "false");
        for (const auto id : {outside, hidden}) {
            assert(doc.read().attribute_value(id, classes) == "selected");
            assert(!doc.read().has_attribute(id, position));
            assert(!doc.read().has_attribute(id, hovered));
        }
        // Snapshot membership survives writes; the next query sees the changes.
        assert(invoke(alias, foreign, selectors).value() == 0);
        assert(doc.take_writes().empty());
        assert(doc.set_attribute(first, classes, "selected"));
        assert(doc.set_attribute(later, classes, "selected"));
        assert(selectors.set_state(first, style::engine::state_hover, false));
        assert(selectors.set_state(later, style::engine::state_hover, true));
        (void)doc.take_writes();
        assert(invoke(alias, foreign, selectors).value() == 2);
        assert(doc.take_writes().size() == 6);
        assert(doc.read().attribute_value(first, position) == "0");
        assert(doc.read().attribute_value(later, position) == "1");
        assert(doc.read().attribute_value(first, hovered) == "false");
        assert(doc.read().attribute_value(later, hovered) == "true");
        doc.remove_document_element();
        assert(doc.set_attribute(first, classes, "selected"));
        (void)doc.take_writes();
        assert(invoke(alias, foreign, selectors).value() == 0);
        assert(doc.take_writes().empty());
        const auto replacement = doc.create_element(atoms.intern("button"));
        doc.set_document_element(replacement);
        assert(doc.set_attribute(replacement, classes, "selected"));
        (void)doc.take_writes();
        assert(invoke(alias, foreign, selectors).value() == 1);
        writes = doc.take_writes();
        assert(writes.size() == 3);
        for (const auto & write : writes) assert(write.node == replacement);
        assert(doc.read().attribute_value(first, classes) == "selected");
        // The detached anchor still supplies its document, and errors preserve ordering.
        assert(doc.set_attribute(button, atoms.intern("data-invalid"), ""));
        (void)doc.take_writes();
        bool rejected = false;
        try { (void)invoke(alias, foreign, selectors); }
        catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected);
        assert(!doc.read().has_attribute(button, atoms.intern("data-after")));
        writes = doc.take_writes();
        assert(writes.size() == 1 && writes[0].name == atoms.intern("data-before"));
        assert(doc.remove_attribute(button, atoms.intern("data-before")));
"""

OWNED_CHECKS = r"""
        @ENTRY@_session session;
        auto & owned = session.document();
        const auto invoke_owned = [&](element_ref anchor, element_ref expected) {
            return session.invoke(@ARGUMENTS@);
        };
        const auto parent = owned.create_element(owned.atoms().intern("button"));
        const auto nested = owned.create_element(owned.atoms().intern("button"));
        assert(owned.append_child(parent, nested));
        const element_ref input{&owned, parent};
        const auto owned_class = owned.atoms().intern("class");
        assert(owned.set_attribute(parent, owned_class, "selected"));
        assert(owned.set_attribute(nested, owned_class, "selected"));
        assert(session.selectors().set_state(nested, style::engine::state_hover, true));
        owned.log_writes(true);
        assert(invoke_owned(input, input).value() == 0);
        assert(owned.take_writes().empty());
        owned.set_document_element(parent);
        (void)owned.take_writes();
        assert(invoke_owned(input, input).value() == 2);
        assert(owned.read().attribute_value(parent, owned.atoms().intern("data-index")) == "0");
        assert(owned.read().attribute_value(nested, owned.atoms().intern("data-index")) == "1");
        assert(owned.read().attribute_value(nested, owned.atoms().intern("data-hovered")) == "true");
        assert(owned.take_writes().size() == 6);
        assert(invoke_owned(input, input).value() == 0);
        owned.remove_document_element();
        assert(owned.set_attribute(nested, owned_class, "selected"));
        (void)owned.take_writes();
        assert(invoke_owned(input, input).value() == 0);
        assert(owned.take_writes().empty());
        const auto owned_replacement = owned.create_element(owned.atoms().intern("button"));
        owned.set_document_element(owned_replacement);
        assert(owned.set_attribute(owned_replacement, owned_class, "selected"));
        (void)owned.take_writes();
        const element_ref current{&owned, owned_replacement};
        assert(invoke_owned(input, current).value() == 1);
        assert(owned.take_writes().size() == 3);
        for (const bool foreign_anchor : {false, true}) {
            bool rejected = false;
            try { (void)invoke_owned(foreign_anchor ? foreign : input,
                                     foreign_anchor ? input : foreign); }
            catch (const std::invalid_argument &) { rejected = true; }
            assert(rejected && owned.take_writes().empty());
        }
"""

REFUSALS = {
    name: body.replace("element.querySelectorAll", "document.querySelectorAll")
    for name, body in ELEMENT_REFUSALS.items()
}
REFUSALS.update(
    {
        "replace-document": "document=element; return false;",
        "replace-method": "document.querySelectorAll=element; return false;",
        "replace-alias-method": "const bound=document; bound.querySelectorAll=element; return false;",
        "alias-length-write": "const nodes=document.querySelectorAll('*'); "
        "const alias=nodes; alias.length=0; return true;",
        "joined-snapshots": "let nodes; if(element.hasAttribute('data-first')) "
        "nodes=document.querySelectorAll('*'); else nodes=document.querySelectorAll('button'); "
        "return nodes.length;",
        "unguarded-default-root": "return document.documentElement.querySelectorAll('*').length;",
    }
)


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
    refusals = 0
    for anchor in (0, 1):
        source = (
            SOURCE if anchor == 0 else SOURCE.replace("(anchor, expected)", "(expected, anchor)")
        )
        ir, contract = dom.prepare(args, f"document-all-{anchor}", source, 2)
        contract.update(current_document_parameter=anchor, initial_intrinsics=["Number"])
        for owned in (False, True):
            manifest = dict(
                contract, provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
            )
            for optimize in (False, True):
                name = f"document-all-{anchor}-{owned}-{optimize}"
                native = dom.lower(args, ir, manifest, name, optimize=optimize)
                text = native.read_text()
                if (
                    '"ctnative::js_document_t"' not in text
                    or "std::vector<ctnative::js_element_t>" not in text
                    or "std::vector<ctbrowser::element_ref>" in text
                ):
                    raise RuntimeError(f"{name}: document snapshot lost its typed browser views")
                client = (CHECKS + INPUT_CHECKS + (OWNED_CHECKS if owned else "")).replace(
                    "@ARGUMENTS@", "expected, anchor" if anchor else "anchor, expected"
                )
                dom.standalone(args, native, name, client, compilers, includes, libraries)
                missing = dict(manifest)
                del missing["current_document_parameter"]
                for suffix, bad, budget in (
                    ("unbound", missing, None),
                    ("stale", dict(manifest, module_sha256="0" * 64), None),
                    ("budget", manifest, 0),
                    ("dataset", dict(manifest, dataset_parameters=[anchor]), None),
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
    for name, body in REFUSALS.items():
        ir, manifest = dom.prepare(
            args, name, f"function bad(element, expected) {{ {body} }}\n", 2, entry_name="bad"
        )
        manifest["current_document_parameter"] = 0
        for optimize in (False, True):
            dom.lower(
                args, ir, manifest, f"refused-{name}-{optimize}", optimize=optimize, success=False
            )
            refusals += 1
    print(
        f"Document.querySelectorAll: 32 native executions, {refusals} refusals; DOM/Core/Style only"
    )


if __name__ == "__main__":
    main()
