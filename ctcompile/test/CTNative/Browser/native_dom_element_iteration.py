#!/usr/bin/env python3
"""Execute proved element for-of loops through typed public browser snapshots."""

from CTNative.Browser import native_dom as dom
from CTNative.harness import without_intrinsic
from CTNative.Browser import native_dom_find_elements as indexed
from CTNative.Browser.native_dom_document import CURRENT_DOCUMENT_CHECKS, INPUT_CHECKS

INTRINSICS = [
    "Array",
    "Element",
    "Function",
    "Number",
    "__ctbrowser_for_of_open",
    "__ctbrowser_iter_next",
    "__ctbrowser_iter_close",
]


def source(kind):
    text = indexed.source("explicit" if kind == "direct" else kind)
    text = (
        text.replace(
            "  for (let i = 0; i < nodes.length; i++) {\n    const node = nodes[i];",
            "  let i = 0;\n  for (const node of nodes) {",
        )
        .replace(
            "    node.setAttribute('data-same', node === expected);",
            "    node.setAttribute('data-same', node === expected);\n    i++;",
        )
        .replace("return nodes.length;", "return i;")
    )
    if kind == "direct":
        text = text.replace(f"  const R={{{indexed.FIND}}};\n", "")
        text = text.replace("R.find('[', anchor)", "anchor.querySelectorAll('[')")
        text = text.replace(
            "R.find('.selected, .selected', anchor)",
            "anchor.querySelectorAll('.selected, .selected')",
        )
    return text


REFUSALS = {
    "unguarded-root-use": "for (const node of R.find('*')) node.matches('*'); "
    "return document.documentElement.matches('*');",
    "escaped-member": "for (const node of R.find('*',anchor)) return node; return false;",
    "stored-member": "for (const node of R.find('*',anchor)) anchor.saved=node; return 0;",
    "mutated-snapshot": "const nodes=R.find('*',anchor); nodes[0]=anchor; "
    "for (const node of nodes) node.matches('*'); return 0;",
    "custom-iterator": "const nodes={ [Symbol.iterator]() { return anchor; } }; "
    "for (const node of nodes) node.matches('*'); return 0;",
    "replaced-open": "__ctbrowser_for_of_open=anchor; "
    "for (const node of R.find('*',anchor)) node.matches('*'); return 0;",
    "replaced-next": "__ctbrowser_iter_next=anchor; "
    "for (const node of R.find('*',anchor)) node.matches('*'); return 0;",
    "replaced-close": "__ctbrowser_iter_close=anchor; "
    "for (const node of R.find('*',anchor)) node.matches('*'); return 0;",
    "replaced-concat": "Array.prototype.concat=anchor; "
    "for (const node of R.find('*',anchor)) node.matches('*'); return 0;",
    "replaced-query": "Element.prototype.querySelectorAll=anchor; "
    "for (const node of R.find('*',anchor)) node.matches('*'); return 0;",
}

# Admitted since b4aa79b57; see native_dom_find_elements.UNGUARDED_DEFAULT.
UNGUARDED_DEFAULT = {
    "unguarded-default": (
        "for (const node of R.find('*')) node.matches('*'); return 0;",
        r"""
        assert(invoke(alias).value() == 0);
        assert(doc.take_writes().empty());
""",
    )
}


def main():
    args = dom.parse_args(__doc__)
    vendor = dom.BOOTSTRAP
    if indexed.FIND + "," not in vendor.read_text():
        raise RuntimeError("Bootstrap R.find source pin changed")
    compilers = args.compilers
    includes, libraries = dom.link_options(args, selectors=True)
    refusals = 0
    for kind in ("direct", "explicit", "omitted", "undefined"):
        ir, contract = dom.prepare(args, kind, source(kind), 2, entry_name="findElements")
        contract.update(current_document_parameter=0, initial_intrinsics=INTRINSICS)
        for owned, optimize, manifest in dom.variants(contract):
            name = f"element-iteration-{kind}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, name, optimize=optimize)
            text = dom.run([args.translate, "--mlir-to-cpp", str(native)]).stdout
            if "std::vector<ctnative::js_element_t>" not in text or any(
                helper in text for helper in INTRINSICS[4:]
            ):
                raise RuntimeError(f"{name}: iterator protocol survived typed snapshot lowering")
            client = (
                CURRENT_DOCUMENT_CHECKS
                + indexed.CHECKS
                + INPUT_CHECKS
                + (indexed.OWNED_CHECKS if owned else "")
            )
            client = client.replace(
                "@DEFAULT@", "true" if kind in ("omitted", "undefined") else "false"
            )
            dom.standalone(args, native, name, client, compilers, includes, libraries)
            dom.lower(
                args,
                ir,
                manifest,
                name + "-budget",
                max_steps=0,
                optimize=optimize,
                success=False,
            )
            refusals += 1
            for intrinsic in INTRINSICS[4:] + ["Array", "Element"]:
                dom.lower(
                    args,
                    ir,
                    without_intrinsic(manifest, intrinsic),
                    name + "-missing-" + intrinsic,
                    optimize=optimize,
                    success=False,
                )
                refusals += 1
    for name, body in REFUSALS.items():
        text = f"function bad(anchor, expected) {{ const R={{{indexed.FIND}}}; {body} }}\n"
        ir, manifest = dom.prepare(args, name, text, 2, entry_name="bad")
        manifest.update(current_document_parameter=0, initial_intrinsics=INTRINSICS)
        dom.refuse(args, ir, manifest, f"refused-{name}")
        refusals += 2
    indexed.unguarded_default(args, UNGUARDED_DEFAULT, INTRINSICS, compilers, includes, libraries)
    print(f"Element for-of: 72 native executions, {refusals} refusals; DOM/Core/Style only")


if __name__ == "__main__":
    main()
