#!/usr/bin/env python3
"""Execute nested DOM and original Bootstrap for-of through typed snapshots."""

import argparse
from pathlib import Path
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.Browser.native_dom_document import CURRENT_DOCUMENT_CHECKS, INPUT_CHECKS
from CTNative.Browser.native_dom_element_iteration import INTRINSICS
from CTNative.Browser.native_dom_spread_length import FIND
from CTNative.harness import find_compilers

BODY = """  let count = 0;
  const groups = @OUTER@;
  for (const group of groups) {
    group.classList.remove('group');
    const nodes = @INNER@;
    for (const node of nodes) {
      node.setAttribute('data-index', count.toString());
      node.classList.remove('item');
      node.setAttribute('data-hovered', node.matches(':hover'));
      node.setAttribute('data-same', node === expected);
      if (node.hasAttribute('data-invalid')) {
        node.setAttribute('data-before', 'yes');
        node.matches('[');
        node.setAttribute('data-after', 'no');
      }
      count++;
    }
  }
  return count;
"""


def source(kind):
    text = "function nestedElements(anchor, expected) {\n"
    body = BODY
    if kind == "direct":
        outer = "anchor.querySelectorAll('.group, .group')"
        inner = "group.querySelectorAll('.item, .item')"
        body = body.replace(
            "  const groups = @OUTER@;",
            "  const groups = @OUTER@;\n  const copied = [].concat(...groups);\n"
            "  if (copied.length !== groups.length) return 0;",
        )
    else:
        text += f"  const R={{{FIND}}};\n"
        outer = "R.find('.group, .group', anchor)"
        inner = "R.find('.item, .item', group)"
        if kind == "default":
            text += "  if (!document.documentElement) return 0;\n"
            outer = "R.find('.group, .group')"
    return text + body.replace("@OUTER@", outer).replace("@INNER@", inner) + "}\n"


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
        (void)doc.take_writes();
        assert(invoke(alias, foreign, selectors).value() == 0);
        assert(doc.take_writes().empty());
        const auto last_group = doc.create_element(atoms.intern("div"));
        const auto empty_group = doc.create_element(atoms.intern("div"));
        const auto last = doc.create_element(atoms.intern("button"));
        const auto second = doc.create_element(atoms.intern("button"));
        const auto first = doc.create_element(atoms.intern("button"));
        assert(doc.append_child(button, empty_group));
        assert(doc.append_child(button, last_group));
        assert(doc.append_child(child, first));
        assert(doc.append_child(child, second));
        assert(doc.append_child(last_group, last));
        for (const auto id : {child, empty_group, last_group}) {
            assert(doc.set_attribute(id, classes, "group"));
        }
        for (const auto id : {first, second, last}) {
            assert(doc.set_attribute(id, classes, "item"));
        }
        // A matching receiver, detached node and shadow descendant stay outside the snapshots.
        assert(doc.set_attribute(button, classes, "group item"));
        const auto outside = doc.create_element(atoms.intern("button"));
        assert(doc.set_attribute(outside, classes, "item"));
        const auto host = doc.create_element(atoms.intern("div"));
        assert(doc.append_child(child, host));
        const auto shadow = doc.attach_shadow(host, true).value();
        const auto hidden = doc.create_element(atoms.intern("button"));
        assert(doc.append_child(shadow, hidden));
        assert(doc.set_attribute(hidden, classes, "item"));
        assert(selectors.set_state(first, style::engine::state_hover, true));
        const element_ref expected{&doc, first};
        (void)doc.take_writes();
        if (@DEFAULT@) {
            assert(invoke(alias, expected, selectors).value() == 0);
            assert(doc.take_writes().empty());
        }
        doc.set_document_element(button);
        (void)doc.take_writes();
        assert(invoke(alias, expected, selectors).value() == 3);
        auto writes = doc.take_writes();
        assert(writes.size() == 15);
        std::size_t at = 0;
        const auto check_group = [&](node_id id) {
            assert(writes[at].node == id && writes[at].name == classes);
            ++at;
            assert(doc.read().attribute_value(id, classes).empty());
        };
        const auto check_item = [&](node_id id, std::string_view index) {
            for (const auto name : {position, classes, hovered, same}) {
                assert(writes[at].node == id && writes[at].name == name);
                ++at;
            }
            assert(doc.read().attribute_value(id, position) == index);
            assert(doc.read().attribute_value(id, classes).empty());
            assert(doc.read().attribute_value(id, hovered) == (id == first ? "true" : "false"));
            assert(doc.read().attribute_value(id, same) == (id == first ? "true" : "false"));
        };
        check_group(child);
        check_item(first, "0");
        check_item(second, "1");
        check_group(empty_group);
        check_group(last_group);
        check_item(last, "2");
        assert(at == writes.size());
        for (const auto id : {button, outside, hidden}) {
            assert(!doc.read().has_attribute(id, position));
        }
        assert(invoke(alias, expected, selectors).value() == 0);
        assert(doc.take_writes().empty());
        assert(doc.set_attribute(child, classes, "group"));
        assert(doc.set_attribute(first, classes, "item"));
        assert(selectors.set_state(first, style::engine::state_hover, false));
        doc.remove_document_element();
        assert(!doc.read().parent(button));
        (void)doc.take_writes();
        if (@DEFAULT@) {
            assert(invoke(alias, foreign, selectors).value() == 0);
            assert(doc.take_writes().empty());
            doc.set_document_element(button);
        }
        assert(invoke(alias, foreign, selectors).value() == 1);
        assert(doc.take_writes().size() == 5);
        assert(doc.read().attribute_value(first, hovered) == "false");
        assert(doc.read().attribute_value(first, same) == "false");
        assert(doc.set_attribute(child, classes, "group"));
        assert(doc.set_attribute(first, classes, "item"));
        assert(doc.set_attribute(first, atoms.intern("data-invalid"), ""));
        (void)doc.take_writes();
        bool rejected = false;
        try { (void)invoke(alias, expected, selectors); }
        catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected && !doc.read().has_attribute(first, atoms.intern("data-after")));
        writes = doc.take_writes();
        assert(writes.size() == 6 && writes.back().name == atoms.intern("data-before"));
"""

OWNED_CHECKS = r"""
        @ENTRY@_session session;
        auto & owned = session.document();
        const auto invoke_owned = [&](element_ref anchor, element_ref expected) {
            return in_outer_document([&] { return session.invoke(anchor, expected); });
        };
        const auto parent = owned.create_element(owned.atoms().intern("section"));
        const auto group = owned.create_element(owned.atoms().intern("div"));
        const auto item = owned.create_element(owned.atoms().intern("button"));
        assert(owned.append_child(parent, group));
        assert(owned.append_child(group, item));
        assert(owned.set_attribute(group, owned.atoms().intern("class"), "group"));
        assert(owned.set_attribute(item, owned.atoms().intern("class"), "item"));
        assert(session.selectors().set_state(item, style::engine::state_hover, true));
        const element_ref input{&owned, parent}, expected_owned{&owned, item};
        owned.log_writes(true);
        if (@DEFAULT@) {
            assert(invoke_owned(input, expected_owned).value() == 0);
            assert(owned.take_writes().empty());
        }
        owned.set_document_element(parent);
        (void)owned.take_writes();
        assert(invoke_owned(input, expected_owned).value() == 1);
        assert(owned.take_writes().size() == 5);
        assert(owned.read().attribute_value(item, owned.atoms().intern("data-index")) == "0");
        assert(owned.read().attribute_value(item, owned.atoms().intern("data-hovered")) == "true");
        assert(owned.read().attribute_value(item, owned.atoms().intern("data-same")) == "true");
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
    "escaping-inner-member": "return node;",
    "retained-inner-member": "anchor.saved=node;",
    "unknown-inner-effect": "external(node);",
    "replaced-inner-next": "__ctbrowser_iter_next=anchor;",
}

# Preserve the exact source that the preceding single-loop fixture refused.
PREVIOUS_REFUSAL = (
    "for (const node of R.find('*',anchor)) "
    "for (const child of node.querySelectorAll('*')) child.matches('*'); return 0;"
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
    vendor = Path(__file__).resolve().parents[4] / "ctbrowser/vendor/bootstrap/bootstrap.bundle.js"
    if FIND + "," not in vendor.read_text():
        raise RuntimeError("Bootstrap R.find source pin changed")
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args, selectors=True)
    refusals = executions = 0
    for kind in ("direct", "explicit", "default"):
        ir, contract = dom.prepare(args, kind, source(kind), 2, entry_name="nestedElements")
        contract.update(current_document_parameter=0, initial_intrinsics=INTRINSICS)
        for owned in (False, True):
            manifest = dict(
                contract, provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
            )
            for optimize in (False, True):
                name = f"nested-iteration-{kind}-{owned}-{optimize}"
                native = dom.lower(args, ir, manifest, name, optimize=optimize)
                text = dom.run([args.translate, "--mlir-to-cpp", str(native)]).stdout
                if "std::vector<ctnative::js_element_t>" not in text or any(
                    helper in text for helper in INTRINSICS[4:]
                ):
                    raise RuntimeError(f"{name}: nested iteration retained the VM protocol")
                client = (
                    CURRENT_DOCUMENT_CHECKS
                    + CHECKS
                    + INPUT_CHECKS
                    + (OWNED_CHECKS if owned else "")
                ).replace("@DEFAULT@", "true" if kind == "default" else "false")
                dom.standalone(args, native, name, client, compilers, includes, libraries)
                executions += 2 * len(compilers)
                controls = [("budget", manifest, 0)]
                for intrinsic in INTRINSICS[4:] + ["Array", "Element"]:
                    controls.append(
                        (
                            "missing-" + intrinsic,
                            dict(
                                manifest,
                                initial_intrinsics=[i for i in INTRINSICS if i != intrinsic],
                            ),
                            None,
                        )
                    )
                for suffix, bad, budget in controls:
                    dom.lower(
                        args,
                        ir,
                        bad,
                        name + "-" + suffix,
                        optimize=optimize,
                        max_steps=budget,
                        success=False,
                    )
                    refusals += 1
    ir, manifest = dom.prepare(
        args,
        "previous-nested-open",
        f"function bad(anchor, expected) {{ const R={{{FIND}}}; {PREVIOUS_REFUSAL} }}\n",
        2,
        entry_name="bad",
    )
    manifest.update(current_document_parameter=0, initial_intrinsics=INTRINSICS)
    for optimize in (False, True):
        native = dom.lower(
            args, ir, manifest, f"previous-nested-open-{optimize}", optimize=optimize
        )
        if len(dom.NATIVE.findall(native.read_text())) != 1:
            raise RuntimeError("the previously refused nested source did not emit its entry")
    bad_sources = {
        name: "function bad(anchor, expected) { "
        "for (const group of anchor.querySelectorAll('*')) "
        "for (const node of group.querySelectorAll('*')) { " + body + " } return 0; }\n"
        for name, body in REFUSALS.items()
    }
    bad_sources["mutated-inner-snapshot"] = """function bad(anchor, expected) {
      for (const group of anchor.querySelectorAll('*')) {
        const nodes = group.querySelectorAll('*'); nodes[0] = anchor;
        for (const node of nodes) node.matches('*');
      }
      return 0;
    }\n"""
    bad_sources["custom-inner-iterator"] = """function bad(anchor, expected) {
      for (const group of anchor.querySelectorAll('*')) {
        const nodes = { [Symbol.iterator]() { return group; } };
        for (const node of nodes) node.matches('*');
      }
      return 0;
    }\n"""
    bad_sources["replaced-outer-prototype"] = """function bad(anchor, expected) {
      for (const group of anchor.querySelectorAll('*')) {
        Element.prototype.querySelectorAll = anchor;
        for (const node of group.querySelectorAll('*')) node.matches('*');
      }
      return 0;
    }\n"""
    bad_sources["reassigned-loop-holder"] = (
        f"function bad(anchor, expected) {{ const R={{{FIND}}}; "
        "for (const group of anchor.querySelectorAll('*')) { R.find=anchor; "
        "for (const node of R.find('*',group)) node.matches('*'); } return 0; }\n"
    )
    bad_sources["conditional-loop-holder"] = (
        "function bad(anchor, expected) { let R; "
        f"if (anchor.hasAttribute('ready')) R={{{FIND}}}; "
        "for (const group of anchor.querySelectorAll('*')) "
        "for (const node of R.find('*',group)) node.matches('*'); return 0; }\n"
    )
    bad_sources["escaped-loop-holder"] = (
        f"function bad(anchor, expected) {{ const R={{{FIND}}}; "
        "for (const group of anchor.querySelectorAll('*')) { external(R); "
        "for (const node of R.find('*',group)) node.matches('*'); } return 0; }\n"
    )
    bad_sources["nullable-inner-receiver"] = (
        f"function bad(anchor, expected) {{ const R={{{FIND}}}; "
        "for (const group of anchor.querySelectorAll('*')) "
        "for (const node of R.find('*',group.closest('.missing'))) node.matches('*'); "
        "return 0; }\n"
    )
    for name, text in bad_sources.items():
        ir, manifest = dom.prepare(args, name, text, 2, entry_name="bad")
        manifest.update(current_document_parameter=0, initial_intrinsics=INTRINSICS)
        for optimize in (False, True):
            dom.lower(
                args, ir, manifest, f"refused-{name}-{optimize}", optimize=optimize, success=False
            )
            refusals += 1
    print(
        f"Nested element for-of: {executions} native executions, 2 previous-source lowering checks, "
        f"{refusals} refusals; DOM/Core/Style only"
    )


if __name__ == "__main__":
    main()
