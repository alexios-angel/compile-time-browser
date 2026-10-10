#!/usr/bin/env python3
"""Execute original Bootstrap R.find defaults with checked document-root receivers."""

from CTNative.Browser.dom import dom
from CTNative.harness import without_intrinsic
from CTNative.Browser.dom.document.basics import CURRENT_DOCUMENT_CHECKS, INPUT_CHECKS
from CTNative.Browser.dom.spread_length import FIND

BODY = """  if (anchor.hasAttribute('data-invalid')) {
    anchor.setAttribute('data-before', 'yes');
    const invalid = R.find('['@DEFAULT@);
    anchor.setAttribute('data-after', 'no');
    return invalid.length;
  }
  if (anchor.hasAttribute('data-hover')) return R.find(':hover'@DEFAULT@).length;
  const nodes = R.find('.selected'@DEFAULT@);
  return nodes.length;
"""


def source(negated):
    declaration = f"  const R={{{FIND}}};\n"
    if negated:
        return (
            "function findDefault(expected, anchor) {\n"
            + declaration
            + "  if (!document.documentElement) return 0;\n"
            + BODY.replace("@DEFAULT@", ", undefined")
            + "}\n"
        )
    return (
        "function findDefault(anchor, expected) {\n"
        + declaration
        + "  if (document.documentElement) {\n"
        + BODY.replace("@DEFAULT@", "")
        + "  }\n  return 0;\n}\n"
    )


CHECKS = dom.SELECTOR_CHECKS_PREAMBLE + r"""
        assert(doc.remove_child(button));
        assert(doc.set_attribute(button, classes, "selected"));
        assert(doc.set_attribute(child, classes, "selected"));
        const auto nested = doc.create_element(atoms.intern("button"));
        assert(doc.append_child(child, nested));
        assert(doc.set_attribute(nested, classes, "selected"));
        const auto outside = doc.create_element(atoms.intern("button"));
        assert(doc.set_attribute(outside, classes, "selected"));
        const auto host = doc.create_element(atoms.intern("div"));
        assert(doc.append_child(button, host));
        const auto shadow = doc.attach_shadow(host, true).value();
        const auto hidden = doc.create_element(atoms.intern("button"));
        assert(doc.append_child(shadow, hidden));
        assert(doc.set_attribute(hidden, classes, "selected"));
        (void)doc.take_writes();
        assert(invoke(alias, foreign, selectors).value() == 0);
        assert(doc.take_writes().empty()); // A detached matching subtree is not a document root.
        doc.set_document_element(button);
        (void)doc.take_writes();
        assert(invoke(alias, foreign, selectors).value() == 2);
        assert(doc.take_writes().empty()); // Element selectors exclude the matching root.
        assert(doc.set_attribute(child, classes, ""));
        (void)doc.take_writes();
        assert(invoke(alias, foreign, selectors).value() == 1);
        assert(doc.take_writes().empty());
        assert(doc.set_attribute(child, classes, "selected"));
        const auto hover = atoms.intern("data-hover");
        assert(doc.set_attribute(button, hover, ""));
        assert(selectors.set_state(button, style::engine::state_hover, true));
        assert(selectors.set_state(nested, style::engine::state_hover, true));
        (void)doc.take_writes();
        assert(invoke(alias, foreign, selectors).value() == 1);
        assert(selectors.set_state(nested, style::engine::state_hover, false));
        assert(invoke(alias, foreign, selectors).value() == 0);
        assert(doc.take_writes().empty());
        assert(doc.remove_attribute(button, hover));
        doc.remove_document_element();
        (void)doc.take_writes();
        assert(invoke(alias, foreign, selectors).value() == 0);
        assert(doc.take_writes().empty());
        const auto replacement = doc.create_element(atoms.intern("section"));
        const auto replacement_child = doc.create_element(atoms.intern("button"));
        assert(doc.append_child(replacement, replacement_child));
        assert(doc.set_attribute(replacement, classes, "selected"));
        assert(doc.set_attribute(replacement_child, classes, "selected"));
        doc.set_document_element(replacement);
        (void)doc.take_writes();
        assert(invoke(alias, foreign, selectors).value() == 1);
        assert(doc.take_writes().empty()); // The detached anchor still identifies its document.
        for (const auto id : {button, child, nested, outside, hidden})
            assert(doc.read().attribute_value(id, classes) == "selected");
        const auto invalid = atoms.intern("data-invalid");
        assert(doc.set_attribute(button, invalid, ""));
        (void)doc.take_writes();
        bool rejected = false;
        try { (void)invoke(alias, foreign, selectors); }
        catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected && !doc.read().has_attribute(button, atoms.intern("data-after")));
        auto writes = doc.take_writes();
        assert(writes.size() == 1 && writes[0].name == atoms.intern("data-before"));
        assert(doc.read().attribute_value(button, atoms.intern("data-before")) == "yes");
        assert(doc.remove_attribute(button, atoms.intern("data-before")));
        doc.remove_document_element();
        (void)doc.take_writes();
        assert(invoke(alias, foreign, selectors).value() == 0);
        assert(doc.take_writes().empty()); // The root guard prevents even the preceding source effect.
        assert(doc.remove_attribute(button, invalid));
        foreign_doc.set_document_element(other_button);
        const auto foreign_child = foreign_doc.create_element(foreign_atoms.intern("button"));
        assert(foreign_doc.append_child(other_button, foreign_child));
        assert(foreign_doc.set_attribute(foreign_child, foreign_atoms.intern("class"), "selected"));
        style::engine foreign_selectors{foreign_atoms};
        assert(invoke(foreign, alias, foreign_selectors).value() == 1);
"""

OWNED_CHECKS = r"""
    {
        @ENTRY@_session session;
        auto & owned = session.document();
        const auto invoke_owned = [&](element_ref anchor, element_ref expected) {
            return in_outer_document([&] { return session.invoke(@ARGUMENTS@); });
        };
        const auto parent = owned.create_element(owned.atoms().intern("button"));
        const auto nested = owned.create_element(owned.atoms().intern("button"));
        assert(owned.append_child(parent, nested));
        const element_ref input{&owned, parent};
        const auto owned_class = owned.atoms().intern("class");
        assert(owned.set_attribute(parent, owned_class, "selected"));
        assert(owned.set_attribute(nested, owned_class, "selected"));
        owned.log_writes(true);
        assert(invoke_owned(input, input).value() == 0);
        assert(owned.take_writes().empty());
        owned.set_document_element(parent);
        (void)owned.take_writes();
        assert(invoke_owned(input, input).value() == 1);
        assert(owned.take_writes().empty());
        owned.remove_document_element();
        (void)owned.take_writes();
        assert(invoke_owned(input, input).value() == 0);
        assert(owned.take_writes().empty());
        const auto replacement = owned.create_element(owned.atoms().intern("section"));
        const auto replacement_child = owned.create_element(owned.atoms().intern("button"));
        assert(owned.append_child(replacement, replacement_child));
        assert(owned.set_attribute(replacement_child, owned_class, "selected"));
        owned.set_document_element(replacement);
        (void)owned.take_writes();
        assert(invoke_owned(input, input).value() == 1);
        assert(owned.take_writes().empty());
        assert(owned.set_attribute(parent, owned.atoms().intern("data-invalid"), ""));
        (void)owned.take_writes();
        bool invalid_rejected = false;
        try { (void)invoke_owned(input, input); }
        catch (const std::invalid_argument &) { invalid_rejected = true; }
        assert(invalid_rejected && !owned.read().has_attribute(parent, owned.atoms().intern("data-after")));
        const auto owned_writes = owned.take_writes();
        assert(owned_writes.size() == 1 && owned_writes[0].name == owned.atoms().intern("data-before"));
        for (const bool foreign_anchor : {false, true}) {
            bool rejected = false;
            try { (void)invoke_owned(foreign_anchor ? foreign : input,
                                     foreign_anchor ? input : foreign); }
            catch (const std::invalid_argument &) { rejected = true; }
            assert(rejected && owned.take_writes().empty());
        }
    }
"""

CHECKED_CHECKS = r"""
        (void)pressed;
        style::engine selectors{atoms};
        const auto invoke = [&](element_ref anchor, element_ref expected,
                                style::engine & engine) {
            return in_outer_document([&] { return @ENTRY@(anchor, expected, engine); });
        };
        const auto check_roots = [&](ctbrowser::document & tested, node_id anchor,
                                     auto && call) {
            tested.log_writes(true);
            const auto absent = [&] {
                bool rejected = false;
                try { assert(call().value() == 0); }
                catch (const std::bad_expected_access<dom_error> &) { rejected = true; }
                assert(rejected == @THROWS@);
                assert(tested.take_writes().empty());
            };
            (void)tested.take_writes();
            absent();
            const auto root = tested.create_element(tested.atoms().intern("section"));
            const auto child = tested.create_element(tested.atoms().intern("button"));
            const auto nested = tested.create_element(tested.atoms().intern("button"));
            assert(tested.append_child(root, child));
            assert(tested.append_child(child, nested));
            const auto selected = tested.atoms().intern("class");
            for (const auto id : {root, child, nested})
                assert(tested.set_attribute(id, selected, "selected"));
            tested.set_document_element(root);
            (void)tested.take_writes();
            assert(call().value() == @FIRST@); // Element selectors exclude the matching root.
            auto writes = tested.take_writes();
            @WRITES@
            tested.remove_document_element();
            (void)tested.take_writes();
            absent();
            const auto replacement = tested.create_element(tested.atoms().intern("section"));
            const auto replacement_child = tested.create_element(tested.atoms().intern("button"));
            assert(tested.append_child(replacement, replacement_child));
            assert(tested.set_attribute(replacement, selected, "selected"));
            assert(tested.set_attribute(replacement_child, selected, "selected"));
            tested.set_document_element(replacement);
            (void)tested.take_writes();
            assert(call().value() == @SECOND@); // A detached anchor still binds the current document.
            writes = tested.take_writes();
            @WRITES@
            (void)anchor;
            @EFFECTS@
        };
        assert(doc.remove_child(button));
        check_roots(doc, button, [&] { return invoke(alias, foreign, selectors); });
        assert(foreign_doc.remove_child(other_button));
        style::engine foreign_selectors{foreign_atoms};
        check_roots(foreign_doc, other_button,
                    [&] { return invoke(foreign, alias, foreign_selectors); });
"""

CHECKED_OWNED_CHECKS = r"""
    {
        @ENTRY@_session session;
        auto & owned = session.document();
        const auto parent = owned.create_element(owned.atoms().intern("button"));
        const element_ref input{&owned, parent};
        const auto invoke_owned = [&](element_ref first, element_ref second) {
            return in_outer_document([&] { return session.invoke(first, second); });
        };
        check_roots(owned, parent, [&] { return invoke_owned(input, input); });
        for (const bool foreign_anchor : {false, true}) {
            bool rejected = false;
            try { (void)invoke_owned(foreign_anchor ? foreign : input,
                                     foreign_anchor ? input : foreign); }
            catch (const std::invalid_argument &) { rejected = true; }
            assert(rejected && owned.take_writes().empty());
        }
    }
"""

CHECKED_EFFECTS = r"""
            assert(tested.set_attribute(anchor, tested.atoms().intern("data-invalid"), ""));
            (void)tested.take_writes();
            bool invalid = false;
            try { (void)call(); }
            catch (const std::invalid_argument &) { invalid = true; }
            assert(invalid);
            const auto before = tested.atoms().intern("data-before");
            const auto after = tested.atoms().intern("data-after");
            writes = tested.take_writes();
            assert(writes.size() == 1 && writes[0].node == anchor && writes[0].name == before);
            assert(tested.read().attribute_value(anchor, before) == "yes");
            assert(!tested.read().has_attribute(anchor, after));
            assert(tested.remove_attribute(anchor, before));
            tested.remove_document_element();
            (void)tested.take_writes();
            bool missing = false;
            try { (void)call(); }
            catch (const std::bad_expected_access<dom_error> &) { missing = true; }
            assert(missing); // Receiver validation precedes even an invalid selector.
            writes = tested.take_writes();
            assert(writes.size() == 1 && writes[0].node == anchor && writes[0].name == before);
            assert(tested.read().attribute_value(anchor, before) == "yes");
            assert(!tested.read().has_attribute(anchor, after));
"""

CHECKED_ROOTS = {
    "unguarded-default": "return R.find('*').length;",
    "unrelated-query-guard": "if(document.querySelector('*')) return R.find('*').length; return 0;",
    "wrong-arm": "if(!document.documentElement) return R.find('*').length; return 0;",
    "unguarded-after-branch": "if(document.documentElement) anchor.setAttribute('seen','yes'); return R.find('*').length;",
}


def checked_checks(name):
    writes = "assert(writes.empty());"
    if name == "unguarded-after-branch":
        writes = """assert(writes.size() == 1 && writes[0].node == anchor &&
                           writes[0].name == tested.atoms().intern("seen"));
            assert(tested.read().attribute_value(anchor, tested.atoms().intern("seen")) == "yes");
            assert(tested.remove_attribute(anchor, tested.atoms().intern("seen")));
            (void)tested.take_writes();"""
    return (
        CHECKED_CHECKS.replace("@THROWS@", str(name != "unrelated-query-guard").lower())
        .replace("@FIRST@", "0" if name == "wrong-arm" else "2")
        .replace("@SECOND@", "0" if name == "wrong-arm" else "1")
        .replace("@WRITES@", writes)
        .replace("@EFFECTS@", CHECKED_EFFECTS if name == "checked-root-effects" else "")
    )


REFUSALS = {
    "root-escape": "if(document.documentElement) return document.documentElement; return false;",
    "snapshot-escape": "if(document.documentElement) return R.find('*'); return false;",
    "replace-document": "if(document.documentElement) { document=anchor; return R.find('*').length; } return 0;",
    "replace-root": "if(document.documentElement) { document.documentElement=anchor; return R.find('*').length; } return 0;",
    "root-accessor": "Object.defineProperty(document,'documentElement',{get(){return anchor;}}); if(document.documentElement) return R.find('*').length; return 0;",
    "replace-query-all": "Element.prototype.querySelectorAll=anchor; if(document.documentElement) return R.find('*').length; return 0;",
    "replace-call": "Function.prototype.call=anchor; if(document.documentElement) return R.find('*').length; return 0;",
    "replace-concat": "Array.prototype.concat=anchor; if(document.documentElement) return R.find('*').length; return 0;",
    "unknown-call": "if(document.documentElement) { unknown(); return R.find('*').length; } return 0;",
    "coercing-selector": "if(document.documentElement) return R.find(1).length; return 0;",
    "missing-selector": "if(document.documentElement) return R.find().length; return 0;",
    "extra-argument": "if(document.documentElement) return R.find('*',anchor,expected).length; return 0;",
    "explicit-null": "if(document.documentElement) return R.find('*',null).length; return 0;",
    "unknown-default": "if(document.documentElement) return R.find('*',unknown).length; return 0;",
}


def main():
    args = dom.parse_args(__doc__)
    vendor = dom.BOOTSTRAP
    if FIND + "," not in vendor.read_text():
        raise RuntimeError("Bootstrap R.find source pin changed")
    compilers = args.compilers
    includes, libraries = dom.link_options(args, selectors=True)
    refusals = 0
    executions = 0
    cases = [
        (f"default-root-{anchor}", source(bool(anchor)), CHECKS, OWNED_CHECKS, anchor)
        for anchor in (0, 1)
    ]
    for name, body in {
        **CHECKED_ROOTS,
        "checked-root-effects": BODY.replace("@DEFAULT@", ""),
    }.items():
        cases.append(
            (
                name,
                f"function findDefault(anchor, expected) {{ const R={{{FIND}}}; {body} }}\n",
                checked_checks(name),
                CHECKED_OWNED_CHECKS,
                0,
            )
        )
    for case, text, checks, owned_checks, anchor in cases:
        ir, contract = dom.prepare(args, case, text, 2, entry_name="findDefault")
        contract.update(
            current_document_parameter=anchor, initial_intrinsics=["Array", "Element", "Function"]
        )
        for owned, optimize, manifest in dom.variants(contract):
            name = f"{case}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, name, optimize=optimize)
            text = native.read_text()
            if (
                "ctnative::document_scope" not in text
                or "ctnative::document.documentElement" not in text
                or "ctnative::Element.prototype.querySelectorAll.call" not in text
            ):
                raise RuntimeError(f"{name}: original default root bypassed public DOM/Style")
            client = (
                CURRENT_DOCUMENT_CHECKS + checks + INPUT_CHECKS + (owned_checks if owned else "")
            ).replace("@ARGUMENTS@", "expected, anchor" if anchor else "anchor, expected")
            dom.standalone(args, native, name, client, compilers, includes, libraries)
            executions += 4
            if not case.startswith("default-root-"):
                dom.lower(
                    args,
                    ir,
                    manifest,
                    f"{name}-budget",
                    optimize=optimize,
                    max_steps=0,
                    success=False,
                )
                refusals += 1
                continue
            missing = dict(manifest)
            del missing["current_document_parameter"]
            controls = [
                ("unbound", missing, None),
                ("wrong-index", dict(manifest, current_document_parameter=2), None),
                ("stale", dict(manifest, module_sha256="0" * 64), None),
                ("budget", manifest, 0),
            ]
            for intrinsic in ("Array", "Element", "Function"):
                controls.append(
                    (
                        f"missing-{intrinsic}",
                        without_intrinsic(manifest, intrinsic),
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
        rejected_source = f"function bad(anchor, expected) {{ const R={{{FIND}}}; {body} }}\n"
        ir, manifest = dom.prepare(args, name, rejected_source, 2, entry_name="bad")
        manifest.update(
            current_document_parameter=0, initial_intrinsics=["Array", "Element", "Function"]
        )
        dom.refuse(args, ir, manifest, f"refused-{name}")
        refusals += 2
    print(
        f"Bootstrap checked default root: {executions} native executions, {refusals} refusals; DOM/Core/Style only"
    )


if __name__ == "__main__":
    main()
