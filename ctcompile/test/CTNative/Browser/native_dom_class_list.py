#!/usr/bin/env python3
"""Execute native classList methods against the shared DOM token-list core."""

import argparse
from pathlib import Path
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.harness import find_compilers

SOURCE = """function classes(element) {
  const list = element.classList;
  if (element.hasAttribute('data-invalid-add')) {
    element.setAttribute('data-before', 'yes');
    list.add('good', 'bad token', '');
    element.setAttribute('data-after', 'no');
    return false;
  }
  if (element.hasAttribute('data-invalid-remove')) {
    element.setAttribute('data-before', 'yes');
    list.remove('active', '', 'bad token');
    element.setAttribute('data-after', 'no');
    return false;
  }
  if (element.hasAttribute('data-empty')) {
    list.add();
    list.remove();
    return list.contains('');
  }
  if (element.hasAttribute('data-derived')) {
    const child = element.querySelector('span');
    if (child) {
      const parent = child.closest('button');
      if (parent) {
        parent.classList.add('derived');
        parent.classList.remove('btn');
        return parent.classList.contains('derived');
      }
    }
    return false;
  }
  const token = element.hasAttribute('data-other') ? 'other' : 'active';
  list.add(token, token, 'x');
  const present = list.contains(token);
  element.setAttribute('class', 'active active other');
  list.remove('active', 'missing', 'active');
  return present && list.contains('other') && !list.contains('active') && !list.contains('a b');
}
"""

CHECKS = r"""
        style::engine selectors{atoms};
        (void)foreign;
        (void)pressed;
        // Saved token lists read current attributes; contains never writes.
        for (const auto id : {button, doc.create_element(atoms.intern("g"), node_ns::svg)}) {
            assert(doc.set_attribute(id, classes, "\tbtn  btn\n"));
            (void)doc.take_writes();
            assert(@ENTRY@(element_ref{&doc, id}, selectors));
            assert(doc.read().attribute_value(id, classes) == "other");
            const auto writes = doc.take_writes();
            assert(writes.size() == 3);
            for (const auto & write : writes) assert(write.node == id && write.name == classes);
        }
        assert(doc.set_attribute(button, atoms.intern("data-other"), ""));
        assert(@ENTRY@(alias, selectors));
        assert(doc.remove_attribute(button, atoms.intern("data-other")));
        const auto empty = atoms.intern("data-empty");
        assert(doc.set_attribute(button, empty, ""));
        assert(doc.remove_attribute(button, classes));
        (void)doc.take_writes();
        const auto absent_version = doc.version();
        assert(!@ENTRY@(alias, selectors));
        assert(!doc.read().has_attribute(button, classes));
        assert(doc.version() == absent_version && doc.take_writes().empty());
        assert(doc.set_attribute(button, classes, " a\ta  b\n"));
        (void)doc.take_writes();
        assert(!@ENTRY@(alias, selectors));
        assert(doc.read().attribute_value(button, classes) == "a b");
        assert(doc.take_writes().size() == 2); // Includes the second same-value write.
        assert(doc.set_attribute(button, classes, ""));
        (void)doc.take_writes();
        assert(!@ENTRY@(alias, selectors));
        assert(doc.read().has_attribute(button, classes));
        assert(doc.take_writes().size() == 2);
        assert(doc.remove_attribute(button, empty));
        // Every token is validated before mutation; source effects keep their order.
        for (bool add : {true, false}) {
            const auto invalid = atoms.intern(add ? "data-invalid-add" : "data-invalid-remove");
            assert(doc.set_attribute(button, invalid, ""));
            assert(doc.set_attribute(button, classes, "active active other"));
            (void)doc.take_writes();
            bool rejected = false;
            try { (void)@ENTRY@(alias, selectors); }
            catch (const std::bad_expected_access<token_argument_error> & error) {
                rejected = error.error().index == 1 && error.error().error ==
                    (add ? token_error::whitespace : token_error::empty);
            }
            assert(rejected);
            assert(doc.read().attribute_value(button, classes) == "active active other");
            assert(!doc.read().has_attribute(button, atoms.intern("data-after")));
            const auto writes = doc.take_writes();
            assert(writes.size() == 1 && writes[0].name == atoms.intern("data-before"));
            assert(doc.remove_attribute(button, invalid));
        }
        assert(doc.set_attribute(button, atoms.intern("data-derived"), ""));
        assert(doc.set_attribute(button, classes, "btn"));
        (void)doc.take_writes();
        assert(@ENTRY@(alias, selectors));
        assert(doc.read().attribute_value(button, classes) == "derived");
        assert(doc.take_writes().size() == 2);
        // Input validation precedes every class-list mutation.
        for (const element_ref invalid : {
                 element_ref{}, element_ref{&doc, {}},
                 element_ref{&doc, {button.slot, button.generation + 1u}},
                 element_ref{&doc, doc.create_text("unchanged")}}) {
            bool rejected = false;
            try { (void)@ENTRY@(invalid, selectors); }
            catch (const std::bad_expected_access<dom_error> &) { rejected = true; }
            assert(rejected && doc.take_writes().empty());
        }
"""

OWNED_CHECKS = r"""
        @ENTRY@_session session;
        auto & owned = session.document();
        const auto node = owned.create_element(owned.atoms().intern("button"));
        assert(session.invoke(element_ref{&owned, node}));
        assert(owned.read().attribute_value(node, owned.atoms().intern("class")) == "other");
        owned.log_writes(true);
        bool rejected = false;
        try { (void)session.invoke(alias); }
        catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected && owned.take_writes().empty());
"""

VOID_SOURCE = """function update(element) {
  element.classList.add('kept', 'removed');
  return element.classList.remove('removed');
}
"""

VOID_CHECKS = r"""
        static_assert(std::is_void_v<decltype(@ENTRY@(element))>);
        @ENTRY@(element);
        assert(doc.read().attribute_value(button, classes) == "btn kept");
        assert(doc.take_writes().size() == 2);
        (void)alias; (void)foreign; (void)pressed;
"""

REFUSALS = (
    "return element.classList.contains();",
    "return element.classList.contains('x', 'y');",
    "return element.classList.contains(1);",
    "element.classList.add('x', 1); return true;",
    "element.classList.remove(undefined); return true;",
    "element.classList.add(element); return true;",
    "const method=element.classList.add; method('x'); return true;",
    "element.classList.contains = element.classList.add; return true;",
    "element.saved = element.classList; return true;",
    "return element.classList;",
    "return element.querySelector('span').classList.contains('x');",
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
    ir, contract = dom.prepare(args, "classes", SOURCE, 1)
    for owned in (False, True):
        manifest = dict(
            contract, provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
        )
        for optimize in (False, True):
            name = f"classes-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, name, optimize=optimize)
            for method in ("contains", "add", "remove"):
                if f'"ctnative::{method}_class"' not in native.read_text():
                    raise RuntimeError(f"classList.{method} bypassed the public DOM helper")
            dom.standalone(
                args,
                native,
                name,
                CHECKS + (OWNED_CHECKS if owned else ""),
                compilers,
                includes,
                libraries,
            )
    for index, body in enumerate(REFUSALS):
        source = f"function bad(element) {{ {body} }}\n"
        refused, manifest = dom.prepare(args, f"refused-{index}", source, 1)
        for optimize in (False, True):
            dom.lower(
                args,
                refused,
                manifest,
                f"refused-{index}-{optimize}",
                optimize=optimize,
                success=False,
            )
    # Even no-argument writes invalidate saved dataset views. A membership
    # query leaves the same source epoch intact.
    for method in ("add", "remove", "contains"):
        token = "'x'" if method == "contains" else ""
        source = (
            "function snapshot(element) { const data=element.dataset; "
            f"element.classList.{method}({token}); "
            "const keys=Object.keys(data); return keys.length; }"
        )
        snapshot, manifest = dom.prepare(args, f"snapshot-{method}", source, 1)
        manifest.update(initial_intrinsics=["Object"], dataset_parameters=[0])
        for optimize in (False, True):
            result = dom.lower(
                args,
                snapshot,
                manifest,
                f"snapshot-{method}-{optimize}",
                optimize=optimize,
                success=method == "contains",
            )
            if method != "contains" and "crosses a source mutation" not in result:
                raise RuntimeError(f"classList.{method} refusal did not retain the mutation epoch")
    void_ir, void_contract = dom.prepare(args, "void", VOID_SOURCE, 1)
    for owned in (False, True):
        manifest = dict(
            void_contract, provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
        )
        for optimize in (False, True):
            name = f"void-{owned}-{optimize}"
            native = dom.lower(args, void_ir, manifest, name, optimize=optimize)
            dom.standalone(args, native, name, VOID_CHECKS, compilers, includes, libraries)
    for budget in (0, 1, 20):
        dom.lower(args, ir, contract, f"budget-{budget}", max_steps=budget, success=False)
    stale = dict(contract, module_sha256="0" * 64)
    if "fingerprint mismatch" not in dom.lower(args, ir, stale, "stale", success=False):
        raise RuntimeError("classList accepted a stale source fingerprint")
    print(f"classList: 32 native executions, {2 * len(REFUSALS) + 8} refusals; DOM/Core/Style only")


if __name__ == "__main__":
    main()
