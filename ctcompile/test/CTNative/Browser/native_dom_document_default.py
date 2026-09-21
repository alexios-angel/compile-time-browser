#!/usr/bin/env python3
"""Execute original Bootstrap R.find defaults under a proved document-root guard."""

import argparse
from pathlib import Path
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.Browser.native_dom_document import CURRENT_DOCUMENT_CHECKS, INPUT_CHECKS
from CTNative.Browser.native_dom_spread_length import FIND
from CTNative.harness import find_compilers

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


CHECKS = r"""
        (void)pressed;
        style::engine selectors{atoms};
        const auto invoke = [&](element_ref anchor, element_ref expected,
                                style::engine & engine) {
            return in_outer_document([&] { return @ENTRY@(@ARGUMENTS@, engine); });
        };
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

REFUSALS = {
    "unguarded-default": "return R.find('*').length;",
    "unrelated-query-guard": "if(document.querySelector('*')) return R.find('*').length; return 0;",
    "wrong-arm": "if(!document.documentElement) return R.find('*').length; return 0;",
    "unguarded-after-branch": "if(document.documentElement) anchor.setAttribute('seen','yes'); return R.find('*').length;",
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
    for anchor in (0, 1):
        ir, contract = dom.prepare(
            args, f"default-root-{anchor}", source(bool(anchor)), 2, entry_name="findDefault"
        )
        contract.update(
            current_document_parameter=anchor, initial_intrinsics=["Array", "Element", "Function"]
        )
        for owned in (False, True):
            manifest = dict(
                contract, provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
            )
            for optimize in (False, True):
                name = f"default-root-{anchor}-{owned}-{optimize}"
                native = dom.lower(args, ir, manifest, name, optimize=optimize)
                text = native.read_text()
                if (
                    "ctnative::document_scope" not in text
                    or "ctnative::document.documentElement" not in text
                    or "ctnative::Element.prototype.querySelectorAll.call" not in text
                ):
                    raise RuntimeError(f"{name}: original default root bypassed public DOM/Style")
                client = (
                    CURRENT_DOCUMENT_CHECKS
                    + CHECKS
                    + INPUT_CHECKS
                    + (OWNED_CHECKS if owned else "")
                ).replace("@ARGUMENTS@", "expected, anchor" if anchor else "anchor, expected")
                dom.standalone(args, native, name, client, compilers, includes, libraries)
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
        rejected_source = f"function bad(anchor, expected) {{ const R={{{FIND}}}; {body} }}\n"
        ir, manifest = dom.prepare(args, name, rejected_source, 2, entry_name="bad")
        manifest.update(
            current_document_parameter=0, initial_intrinsics=["Array", "Element", "Function"]
        )
        for optimize in (False, True):
            dom.lower(
                args, ir, manifest, f"refused-{name}-{optimize}", optimize=optimize, success=False
            )
            refusals += 1
    print(
        f"Bootstrap guarded default root: 32 native executions, {refusals} refusals; DOM/Core/Style only"
    )


if __name__ == "__main__":
    main()
