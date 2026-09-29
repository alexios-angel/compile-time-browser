#!/usr/bin/env python3
"""Compose constructor publication with local DOM record ownership."""

import re

from CTNative.Browser import native_dom as dom
from CTNative.Browser.native_dom_records import CHECKS, ELEMENT, WITNESS, source
from CTNative.harness import run

CLASS = """class Item {
  constructor(n) { this.n = n; state.set('x', this); }
  read() { return this.n; }
}"""
DIRECT = (
    WITNESS.replace("@DECL@", CLASS)
    .replace("@MAKE7@", "new Item(7)")
    .replace("@MAKE9@", "new Item(9)")
    .replace("@READ@", "current.read()")
    .replace("  state.set('x', first);\n", "")
    .replace("  state.set('x', second);\n", "")
)
CASES = {
    "direct": DIRECT,
    "snapshot": DIRECT.replace(
        "read() { return this.n; }",
        "read() { return this.n; } count() { return Object.getOwnPropertyNames(this).length; }",
    ).replace(
        "return a === 8 && b === 9;",
        "const keys = current.count();\n  return a === 8 && b === 9 && keys === 1;",
    ),
    "inherited": DIRECT.replace("class Item", "class Base")
    .replace(
        "  const first",
        "  class Item extends Base { constructor(n) { super(n); this.extra = n + 1; } }\n  const first",
    )
    .replace("return a === 8 && b === 9;", "return a === 8 && b === 9 && current.extra === 10;"),
    "captured": DIRECT.replace(
        "class Item",
        "const register = (owner, key, value) => { state.set(key, value); };\n  class Item",
    ).replace("state.set('x', this)", "register('owner', 'x', this)"),
}
INTRINSICS = [
    "Map",
    "Object",
    "__ctbrowser_class_defined",
    "__ctbrowser_class_heritage",
    "__ctbrowser_bind_this",
    "__ctbrowser_init_fields",
]
INHERITED_METHOD = CASES.pop("inherited")
INHERITED_FIELDS = INHERITED_METHOD.replace("read() { return this.n; }", "").replace(
    "current.read()", "current.n"
)

CASES["inherited"] = INHERITED_METHOD.replace(
    "return a === 8 && b === 9 && current.extra === 10;",
    "const extra = current.extra;\n  return a === 8 && b === 9 && extra === 10;",
)
CASES["inherited-conditional"] = INHERITED_METHOD
CASES["inherited-fields"] = INHERITED_FIELDS
CASES["saved-conditional"] = DIRECT.replace(
    "return a === 8 && b === 9;",
    "return (b !== 9 && saved.n === 0) || (b === 9 && saved.n === 8);",
)

BAD = {
    "indirect-set": DIRECT.replace("state.set('x', this)", "state.set.call(state, 'x', this)"),
    "host-alias": DIRECT.replace(
        "element.setAttribute('x', 'done')", "element.setAttribute('x', saved)"
    ),
    "suffix-observer": DIRECT.replace(
        "state.set('x', this);", "state.set('x', this); this.n = state.get('x').n;"
    ),
    "replacement-return": DIRECT.replace(
        "state.set('x', this);", "state.set('x', this); return {n: 99};"
    ),
    "hidden-caller": DIRECT.replace(
        "  const first", "  const hidden = () => new Item(5);\n  const first"
    ),
    "map-escape": DIRECT.replace("return a === 8 && b === 9;", "return state;"),
    "record-escape": DIRECT.replace("return a === 8 && b === 9;", "return saved;"),
    "missing-record": DIRECT.replace(
        "const current = state.get('x');", "const current = state.get('missing');"
    ),
    "replaced-binding": "Map = 0;\n" + DIRECT,
    "property-binding": "element.Map = 0;\n" + DIRECT,
    "prototype": "Map.prototype.set = () => {};\n" + DIRECT,
    "helper-effect": CASES["captured"].replace(
        "state.set(key, value);", "state.set(key, value); unknown(value);"
    ),
    "helper-replaced": CASES["captured"]
    .replace("const register", "let register")
    .replace("  const first", "  register = () => {};\n  const first"),
    "snapshot-field-change": CASES["snapshot"].replace("saved.n = 8;", "saved.extra = 8;"),
}
for name, statement in {
    "nested-write": "if (b === 9) { saved.n = 8; } return a === 8 && b === 9;",
    "nested-method": "return a === 8 && current.read() === 9;",
    "nested-detached-method": "if (b === 9) { const method = current.read; return method(); } return false;",
    "nested-map-read": "return a === 8 && state.get('x').n === 9;",
    "nested-missing-field": "return a === 8 && current.missing === 9;",
    "nested-prototype": "return a === 8 && current.__proto__ === 9;",
    "nested-dynamic-field": "return a === 8 && current[element.getAttribute('key')] === 9;",
    "nested-alias-return": "if (b === 9) { return saved; } return current;",
    "captured-alias": "return () => saved.n;",
    "nested-host-alias": "if (b === 9) { element.setAttribute('x', saved); } return true;",
}.items():
    BAD[name] = DIRECT.replace("return a === 8 && b === 9;", statement)


def main():
    args = dom.parse_args(__doc__, oracles=True)
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for name, body in CASES.items():
        text = source(body)
        oracle = args.work / f"{name}.oracle.js"
        oracle.write_text(text + f"var result = probe({ELEMENT});\n")
        assert run([args.reference, str(oracle)]).stdout == "result=true\n", name
        oracle.write_text(oracle.read_text() + "console.log('result=' + result);\n")
        assert run([args.node, str(oracle)]).stdout == "result=true\n", name
        ir, contract = dom.prepare(args, name, text, 1, entry_name="probe", generic=True)
        declared = dict(contract, initial_intrinsics=INTRINSICS)
        for owned, optimize, manifest in dom.variants(declared):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            cpp = run([args.translate, "--mlir-to-cpp", str(native)]).stdout
            assert re.search(r"map_storage<std::string, ctn_\w+ \*>", cpp), label
            assert "shared_ptr<ctn_" not in cpp, label
            dom.standalone(args, native, label, CHECKS, args.compilers, includes, libraries)
            executions += 4
        for missing in ("Map", "__ctbrowser_class_defined") + (
            ("Object",) if name == "snapshot" else ()
        ):
            for owned, optimize, manifest in dom.variants(
                dict(
                    contract, initial_intrinsics=[value for value in INTRINSICS if value != missing]
                )
            ):
                dom.lower(
                    args,
                    ir,
                    manifest,
                    f"{name}-missing-{missing}-{owned}-{optimize}",
                    optimize=optimize,
                    success=False,
                    max_steps=1000000,
                )
                refusals += 1
        for owned, provider in enumerate(dom.PROVIDERS):
            dom.lower(
                args,
                ir,
                dict(declared, provider=provider),
                f"{name}-budget-{owned}",
                success=False,
                max_steps=0,
            )
            refusals += 1
    for name, body in BAD.items():
        ir, contract = dom.prepare(
            args, "bad-" + name, source(body), 1, entry_name="probe", generic=True
        )
        for owned, optimize, manifest in dom.variants(
            dict(contract, initial_intrinsics=INTRINSICS)
        ):
            dom.lower(
                args,
                ir,
                manifest,
                f"bad-{name}-{owned}-{optimize}",
                optimize=optimize,
                success=False,
                max_steps=1000000,
            )
            refusals += 1
    print(
        f"DOM constructor publication: {len(CASES)} Node/VM sources, {executions} native executions, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
