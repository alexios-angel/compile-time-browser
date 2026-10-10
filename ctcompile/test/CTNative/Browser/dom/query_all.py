#!/usr/bin/env python3
"""Compile Element.querySelectorAll loops to native code and run them.

The result is a static snapshot: it keeps tree order and element identity and
does not change as the loop edits the DOM. The native code calls only the
public DOM/Core/Style libraries. Every entry in REFUSALS (out-of-bounds index,
mutated or escaping snapshot, unknown receiver, ...) must be refused.
"""

from CTNative.Browser.dom import dom

SOURCE = """function queryAll(element) {
  if (element.hasAttribute('data-invalid')) {
    element.setAttribute('data-before', 'yes');
    element.querySelectorAll('[');
    element.setAttribute('data-after', 'no');
    return 0;
  }
  if (element.hasAttribute('data-duplicate')) {
    return element.querySelectorAll('.selected, button').length;
  }
  const selector = element.hasAttribute('data-scope') ? ':scope > .selected' : '.selected';
  const nodes = element.querySelectorAll(selector);
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
        style::engine selectors{atoms};
        (void)pressed;
        const auto position = atoms.intern("data-index");
        const auto hovered = atoms.intern("data-hovered");
        assert(doc.set_attribute(button, classes, "selected"));
        (void)doc.take_writes();
        assert(@ENTRY@(alias, selectors).value() == 0); // Matching roots are excluded.
        assert(doc.take_writes().empty());
        const auto later = doc.create_element(atoms.intern("button"));
        const auto first = doc.create_element(atoms.intern("button"));
        assert(doc.append_child(child, first));
        assert(doc.append_child(button, later));
        assert(doc.set_attribute(first, classes, "selected"));
        assert(doc.set_attribute(later, classes, "selected"));
        const auto outside = doc.create_element(atoms.intern("button"));
        assert(doc.append_child(doc.root(), outside));
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
        assert(@ENTRY@(alias, selectors).value() == 2); // Each node matches both clauses, once.
        assert(doc.take_writes().empty());
        assert(doc.remove_attribute(button, duplicate));
        (void)doc.take_writes();
        assert(@ENTRY@(alias, selectors).value() == 2);
        auto writes = doc.take_writes();
        assert(writes.size() == 6);
        std::size_t index = 0;
        for (const auto id : {first, later}) {
            assert(writes[index].node == id && writes[index++].name == position);
            assert(writes[index].node == id && writes[index++].name == classes);
            assert(writes[index].node == id && writes[index++].name == hovered);
            assert(doc.read().attribute_value(id, classes).empty());
        }
        // Tree order survives reverse allocation order and membership-changing writes.
        assert(doc.read().attribute_value(first, position) == "0");
        assert(doc.read().attribute_value(later, position) == "1");
        assert(doc.read().attribute_value(first, hovered) == "true");
        assert(doc.read().attribute_value(later, hovered) == "false");
        for (const auto id : {button, outside, hidden}) {
            assert(doc.read().attribute_value(id, classes) == "selected");
            assert(!doc.read().has_attribute(id, position));
            assert(!doc.read().has_attribute(id, hovered));
        }
        assert(@ENTRY@(alias, selectors).value() == 0); // The next call takes a fresh snapshot.
        assert(doc.take_writes().empty());
        assert(doc.set_attribute(first, classes, "selected"));
        assert(doc.set_attribute(later, classes, "selected"));
        const auto scope = atoms.intern("data-scope");
        assert(doc.set_attribute(button, scope, ""));
        (void)doc.take_writes();
        assert(@ENTRY@(alias, selectors).value() == 1);
        writes = doc.take_writes();
        assert(writes.size() == 3);
        for (const auto & write : writes) assert(write.node == later);
        assert(doc.read().attribute_value(later, position) == "0");
        assert(doc.read().attribute_value(first, classes) == "selected");
        assert(doc.remove_attribute(button, scope));
        assert(doc.remove_child(button));
        (void)doc.take_writes();
        assert(@ENTRY@(alias, selectors).value() == 1); // Detached subtrees remain queryable.
        writes = doc.take_writes();
        assert(writes.size() == 3);
        for (const auto & write : writes) assert(write.node == first);
        // Selector errors preserve only source effects before the call.
        assert(doc.set_attribute(button, atoms.intern("data-invalid"), ""));
        (void)doc.take_writes();
        bool rejected = false;
        try { (void)@ENTRY@(alias, selectors); }
        catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected);
        assert(!doc.read().has_attribute(button, atoms.intern("data-after")));
        writes = doc.take_writes();
        assert(writes.size() == 1 && writes[0].name == atoms.intern("data-before"));
        style::engine wrong{foreign_atoms};
        rejected = false;
        try { (void)@ENTRY@(alias, wrong); }
        catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected && doc.take_writes().empty());
        for (const element_ref invalid : {
                 element_ref{}, element_ref{&doc, {}},
                 element_ref{&doc, {button.slot, button.generation + 1u}},
                 element_ref{&doc, doc.create_text("unchanged")}}) {
            rejected = false;
            try { (void)@ENTRY@(invalid, selectors); }
            catch (const std::bad_expected_access<dom_error> &) { rejected = true; }
            assert(rejected && doc.take_writes().empty());
        }
        (void)foreign;
"""

OWNED_CHECKS = r"""
        @ENTRY@_session session;
        auto & owned = session.document();
        const auto parent = owned.create_element(owned.atoms().intern("section"));
        const auto nested = owned.create_element(owned.atoms().intern("button"));
        assert(owned.append_child(parent, nested));
        assert(owned.set_attribute(nested, owned.atoms().intern("class"), "selected"));
        assert(session.selectors().set_state(nested, style::engine::state_hover, true));
        owned.log_writes(true);
        assert(session.invoke(element_ref{&owned, parent}).value() == 1);
        assert(owned.read().attribute_value(nested, owned.atoms().intern("data-index")) == "0");
        assert(owned.read().attribute_value(nested, owned.atoms().intern("data-hovered")) == "true");
        assert(owned.take_writes().size() == 3);
        assert(session.invoke(element_ref{&owned, parent}).value() == 0);
        bool foreign_rejected = false;
        try { (void)session.invoke(alias); }
        catch (const std::invalid_argument &) { foreign_rejected = true; }
        assert(foreign_rejected && owned.take_writes().empty());
"""

# Entry bodies the native lowering must refuse, keyed by test name.
REFUSALS = {
    "unguarded": "const nodes=element.querySelectorAll('*'); return nodes[0].matches('*');",
    "one-past": "const nodes=element.querySelectorAll('*'); "
    "for(let i=0;i<=nodes.length;i++) nodes[i].setAttribute('x','y'); return true;",
    "negative": "const nodes=element.querySelectorAll('*'); "
    "for(let i=-1;i<nodes.length;i++) nodes[i].setAttribute('x','y'); return true;",
    "fractional": "const nodes=element.querySelectorAll('*'); "
    "for(let i=0.5;i<nodes.length;i++) nodes[i].setAttribute('x','y'); return true;",
    "offset-index": "const nodes=element.querySelectorAll('*'); "
    "for(let i=0;i<nodes.length;i++) nodes[i+1].setAttribute('x','y'); return true;",
    "wrong-bound": "const nodes=element.querySelectorAll('.selected'); "
    "const others=element.querySelectorAll('*'); "
    "for(let i=0;i<others.length;i++) nodes[i].setAttribute('x','y'); return true;",
    "length-write": "const nodes=element.querySelectorAll('*'); nodes.length=0; return true;",
    "member-write": "const nodes=element.querySelectorAll('*'); nodes[0]=element; return true;",
    "push": "const nodes=element.querySelectorAll('*'); nodes.push(element); return true;",
    "return": "return element.querySelectorAll('*');",
    "retained": "element.saved=element.querySelectorAll('*'); return true;",
    "identity": "const first=element.querySelectorAll('*'); "
    "return first===element.querySelectorAll('*');",
    "capture": "const nodes=element.querySelectorAll('*'); return () => nodes.length;",
    "callback": "element.querySelectorAll('*').forEach(node => node.setAttribute('x','y')); "
    "return true;",
    "method": "const nodes=element.querySelectorAll('*'); "
    "for(let i=0;i<nodes.length;i++) nodes[i].focus(); return true;",
    "receiver": "const query=element.querySelectorAll; return query('*').length;",
    "unknown-receiver": "const other={}; return other.querySelectorAll('*').length;",
    "missing-argument": "return element.querySelectorAll().length;",
    "coercion": "return element.querySelectorAll(element).length;",
    "extra-argument": "return element.querySelectorAll('*','extra').length;",
}


def main():
    args = dom.parse_args(__doc__)
    compilers = args.compilers
    includes, libraries = dom.link_options(args, selectors=True)
    ir, contract = dom.prepare(args, "query-all", SOURCE, 1)
    contract["initial_intrinsics"] = ["Number"]
    for owned, optimize, manifest in dom.variants(contract):
        name = f"query-all-{owned}-{optimize}"
        native = dom.lower(args, ir, manifest, name, optimize=optimize)
        emitted = native.read_text()
        if (
            '"ctnative::Element.prototype.querySelectorAll.call"' not in emitted
            or "std::vector<ctnative::js_element_t>" not in emitted
            or "std::vector<ctbrowser::element_ref>" in emitted
        ):
            raise RuntimeError("querySelectorAll lost its typed browser snapshot")
        dom.standalone(
            args,
            native,
            name,
            CHECKS + (OWNED_CHECKS if owned else ""),
            compilers,
            includes,
            libraries,
        )
        # A declared dataset input still makes the loop require its conservative
        # dataset-alias proof, even though this body reads no saved dataset view.
        diagnostic = dom.lower(
            args,
            ir,
            dict(manifest, dataset_parameters=[0]),
            f"dataset-{name}",
            optimize=optimize,
            success=False,
        )
        if "backedge dataset-alias proof" not in diagnostic:
            raise RuntimeError("DOM snapshot loop lost the dataset mutation boundary")
    for name, body in REFUSALS.items():
        source = f"function bad(element) {{ {body} }}\n"
        refused, manifest = dom.prepare(args, name, source, 1, entry_name="bad")
        dom.refuse(args, refused, manifest, f"refused-{name}")
    # 16 = 4 provider/optimize variants x 2 layouts x 2 compilers.
    print(
        f"Element.querySelectorAll: 16 native executions, {2 * len(REFUSALS) + 4} refusals; "
        "DOM/Core/Style only"
    )


if __name__ == "__main__":
    main()
