#!/usr/bin/env python3
"""Execute original Element.prototype selector calls against public DOM/Style."""

import argparse
from pathlib import Path
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.Browser import native_dom_query as query
from CTNative.Browser import native_dom_query_all as query_all
from CTNative.harness import find_compilers


def prototype_source(source, method, calls):
    original = f"element.{method}("
    if source.count(original) != calls:
        raise RuntimeError(f"{method}: the shared selector fixture changed")
    source = source.replace(
        "{\n",
        "{\n  const constructor = Element;\n"
        "  const prototype = constructor.prototype;\n"
        f"  const select = prototype.{method};\n",
        1,
    )
    source = source.replace(original, "select.call(element, ")
    # The invalid-selector branch also retains Bootstrap's original spelling.
    return source.replace(
        "select.call(element, '[')", f"Element.prototype.{method}.call(element, '[')"
    )


QUERY_SOURCE = prototype_source(query.SOURCE, "querySelector", 2).replace(
    "found.setAttribute('data-query', found.matches(':scope'));",
    "const children = Element.prototype.querySelectorAll.call(found, 'button');\n"
    "    found.setAttribute('data-query', !children.length && found.matches(':scope'));",
)
QUERY_ALL_SOURCE = prototype_source(query_all.SOURCE, "querySelectorAll", 3)

MATCHES_SOURCE = prototype_source(dom.MATCHES, "matches", 1)
CLOSEST_SOURCE = prototype_source(dom.CLOSEST, "closest", 1)
MATCHES_OWNED_CHECKS = r"""
        @ENTRY@_session session;
        auto & owned = session.document();
        const auto node = owned.create_element(owned.atoms().intern("button"));
        assert(owned.set_attribute(node, owned.atoms().intern("class"), "btn"));
        const element_ref input{&owned, node};
        assert(!session.invoke(input));
        assert(session.selectors().set_state(node, style::engine::state_hover, true));
        assert(session.invoke(input));
        assert(owned.read().has_attribute(node, owned.atoms().intern("data-matched")));
"""
CLOSEST_OWNED_CHECKS = r"""
        @ENTRY@_session session;
        auto & owned = session.document();
        const auto parent = owned.create_element(owned.atoms().intern("button"));
        const auto nested = owned.create_element(owned.atoms().intern("span"));
        assert(owned.append_child(parent, nested));
        const auto key = owned.atoms().intern("data-bs-toggle");
        assert(owned.set_attribute(parent, key, "button"));
        assert(session.invoke(element_ref{&owned, nested}, element_ref{&owned, parent}));
        assert(owned.remove_attribute(parent, key));
        assert(!session.invoke(element_ref{&owned, nested}, element_ref{&owned, parent}));
"""

PROTOTYPE_CHECKS = r"""
        static_assert(std::is_same_v<decltype((ctnative::Element.prototype.matches)),
                                     const ctnative::matches_method &>);
        static_assert(std::is_same_v<decltype((ctnative::Element.prototype.closest)),
                                     const ctnative::closest_method &>);
        static_assert(std::is_same_v<decltype((ctnative::Element.prototype.querySelector)),
                                     const ctnative::query_selector_method &>);
        static_assert(std::is_same_v<decltype((ctnative::Element.prototype.querySelectorAll)),
                                     const ctnative::query_selector_all_method &>);
        static_assert(&ctnative::matches == &ctnative::Element.prototype.matches);
        static_assert(&ctnative::closest == &ctnative::Element.prototype.closest);
        static_assert(&ctnative::querySelector == &ctnative::Element.prototype.querySelector);
        static_assert(&ctnative::querySelectorAll == &ctnative::Element.prototype.querySelectorAll);
"""

REFUSALS = {
    "replace-element": "Element=element; return false;",
    "replace-function": "Function=element; return false;",
    "replace-prototype": "Element.prototype=element; return false;",
    "replace-method": "Element.prototype.querySelector=Element.prototype.querySelectorAll; "
    "return false;",
    "replace-call": "Element.prototype.querySelector.call=element; return false;",
    "replace-function-call": "Function.prototype.call=element; return false;",
    "replace-alias": "const prototype=Element.prototype; "
    "prototype.querySelectorAll=Element.prototype.querySelector; return false;",
    "detached-call": "const invoke=Element.prototype.querySelector.call; "
    "return invoke(element, 'button');",
    "wrong-call-receiver": "const invoke=Element.prototype.querySelector.call; "
    "return invoke.call(element, element, 'button');",
    "prototype-receiver": "return Element.prototype.querySelector(element, 'button');",
    "null-receiver": "return Element.prototype.querySelectorAll.call(null, '*').length;",
    "unknown-receiver": "return Element.prototype.querySelectorAll.call({}, '*').length;",
    "unguarded-receiver": "const found=element.querySelector('button'); "
    "return Element.prototype.querySelectorAll.call(found, '*').length;",
    "absent-receiver": "const found=element.querySelector('button'); "
    "if(!found) return Element.prototype.querySelectorAll.call(found, '*').length; return 0;",
    "method-escape": "return Element.prototype.querySelector;",
    "call-escape": "return Element.prototype.querySelector.call;",
    "element-escape": "return Element.prototype.querySelector.call(element, '*');",
    "snapshot-escape": "return Element.prototype.querySelectorAll.call(element, '*');",
    "coercion": "return Element.prototype.querySelectorAll.call(element, 1).length;",
    "missing-receiver": "return Element.prototype.querySelector.call();",
    "missing-selector": "return Element.prototype.querySelector.call(element);",
    "extra-selector": "return Element.prototype.querySelectorAll.call(element, '*', '*').length;",
    "matches-coercion": "return Element.prototype.matches.call(element, 1);",
    "matches-missing-selector": "return Element.prototype.matches.call(element);",
    "matches-extra-selector": "return Element.prototype.matches.call(element, '*', '*');",
    "matches-prototype-receiver": "return Element.prototype.matches(element, '*');",
    "matches-detached-call": "const invoke=Element.prototype.matches.call; return invoke(element, '*');",
    "matches-replaced": "Element.prototype.matches=element; return false;",
    "closest-escape": "return Element.prototype.closest.call(element, '*');",
    "closest-unguarded": "return Element.prototype.closest.call(element, '*').matches('*');",
    "closest-null-receiver": "return Element.prototype.closest.call(null, '*');",
    "closest-wrong-call-receiver": "const invoke=Element.prototype.closest.call; return invoke.call(element, element, '*');",
    "closest-replaced": "Element.prototype.closest=element; return false;",
    "default-document": "return Element.prototype.querySelectorAll.call("
    "document.documentElement, '*').length;",
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
        ("query", QUERY_SOURCE, 2, query.CHECKS, query.OWNED_CHECKS, []),
        ("query-all", QUERY_ALL_SOURCE, 1, query_all.CHECKS, query_all.OWNED_CHECKS, ["Number"]),
        ("matches", MATCHES_SOURCE, 1, dom.MATCHES_CHECKS, MATCHES_OWNED_CHECKS, []),
        ("closest", CLOSEST_SOURCE, 2, dom.CLOSEST_CHECKS, CLOSEST_OWNED_CHECKS, []),
    )
    for case, source, parameters, checks, owned_checks, extra_intrinsics in cases:
        ir, contract = dom.prepare(args, case, source, parameters)
        contract["initial_intrinsics"] = ["Element", "Function", *extra_intrinsics]
        for owned in (False, True):
            manifest = dict(
                contract, provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
            )
            for optimize in (False, True):
                name = f"prototype-{case}-{owned}-{optimize}"
                native = dom.lower(args, ir, manifest, name, optimize=optimize)
                for method in {
                    "query": ("querySelector", "querySelectorAll"),
                    "query-all": ("querySelectorAll",),
                    "matches": ("matches",),
                    "closest": ("closest",),
                }[case]:
                    if f'"ctnative::Element.prototype.{method}.call"' not in native.read_text():
                        raise RuntimeError(f"{case}: prototype call bypassed the native method")
                dom.standalone(
                    args,
                    native,
                    name,
                    PROTOTYPE_CHECKS + checks + (owned_checks if owned else ""),
                    compilers,
                    includes,
                    libraries,
                )
                for missing in ("Element", "Function"):
                    dom.lower(
                        args,
                        ir,
                        dict(
                            manifest,
                            initial_intrinsics=[
                                intrinsic
                                for intrinsic in manifest["initial_intrinsics"]
                                if intrinsic != missing
                            ],
                        ),
                        f"{name}-missing-{missing}",
                        optimize=optimize,
                        success=False,
                    )
    for name, body in REFUSALS.items():
        source = f"function bad(element) {{ {body} }}\n"
        ir, manifest = dom.prepare(args, name, source, 1, entry_name="bad")
        manifest["initial_intrinsics"] = ["Element", "Function"]
        for optimize in (False, True):
            dom.lower(
                args, ir, manifest, f"refused-{name}-{optimize}", optimize=optimize, success=False
            )
    print(
        f"Element.prototype selectors: {16 * len(cases)} native executions, "
        f"{2 * len(REFUSALS) + 8 * len(cases)} refusals; "
        "DOM/Core/Style only"
    )


if __name__ == "__main__":
    main()
