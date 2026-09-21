#!/usr/bin/env python3
"""Execute proved element for-of loops through typed public browser snapshots."""

import argparse
from pathlib import Path
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.Browser import native_dom_find_elements as indexed
from CTNative.Browser.native_dom_document import CURRENT_DOCUMENT_CHECKS, INPUT_CHECKS
from CTNative.harness import find_compilers

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
    "unguarded-default": "for (const node of R.find('*')) node.matches('*'); return 0;",
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
    if indexed.FIND + "," not in vendor.read_text():
        raise RuntimeError("Bootstrap R.find source pin changed")
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args, selectors=True)
    refusals = 0
    for kind in ("direct", "explicit", "omitted", "undefined"):
        ir, contract = dom.prepare(args, kind, source(kind), 2, entry_name="findElements")
        contract.update(current_document_parameter=0, initial_intrinsics=INTRINSICS)
        for owned in (False, True):
            manifest = dict(
                contract, provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
            )
            for optimize in (False, True):
                name = f"element-iteration-{kind}-{owned}-{optimize}"
                native = dom.lower(args, ir, manifest, name, optimize=optimize)
                text = dom.run([args.translate, "--mlir-to-cpp", str(native)]).stdout
                if "std::vector<ctnative::js_element_t>" not in text or any(
                    helper in text for helper in INTRINSICS[4:]
                ):
                    raise RuntimeError(
                        f"{name}: iterator protocol survived typed snapshot lowering"
                    )
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
                    bad = dict(
                        manifest,
                        initial_intrinsics=[item for item in INTRINSICS if item != intrinsic],
                    )
                    dom.lower(
                        args,
                        ir,
                        bad,
                        name + "-missing-" + intrinsic,
                        optimize=optimize,
                        success=False,
                    )
                    refusals += 1
    for name, body in REFUSALS.items():
        text = f"function bad(anchor, expected) {{ const R={{{indexed.FIND}}}; {body} }}\n"
        ir, manifest = dom.prepare(args, name, text, 2, entry_name="bad")
        manifest.update(current_document_parameter=0, initial_intrinsics=INTRINSICS)
        for optimize in (False, True):
            dom.lower(
                args, ir, manifest, f"refused-{name}-{optimize}", optimize=optimize, success=False
            )
            refusals += 1
    print(f"Element for-of: 64 native executions, {refusals} refusals; DOM/Core/Style only")


if __name__ == "__main__":
    main()
