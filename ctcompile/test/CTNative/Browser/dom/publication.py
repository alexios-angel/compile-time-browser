#!/usr/bin/env python3
"""Lower class constructors that store `this` in a captured Map.

Built on records.py's WITNESS program. CASES must lower natively (the Map's
values become owned record pointers, not shared_ptr) and match Node and the
reference VM; BAD variants, missing intrinsics, a missing element parameter
and a zero step budget must be refused.
"""

import re

from CTNative.Browser.dom import dom
from CTNative.Browser.dom.records import CHECKS, ELEMENT, WITNESS, source
from CTNative.harness import run

# The constructor publishes the new instance by storing `this` in `state`.
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
# Map and Object plus the VM class helpers the importer emits for classes.
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
# Instances that also hold an element in `_element`.
ELEMENT_FIELD = (
    DIRECT.replace("constructor(n)", "constructor(n, target)")
    .replace("this.n = n;", "this.n = n; this._element = target;")
    .replace("read() { return this.n; }", "")
    .replace("new Item(7)", "new Item(7, element)")
    .replace("new Item(9)", "new Item(9, element)")
    .replace("current.read()", "current.n")
)
CASES["element-field"] = ELEMENT_FIELD.replace(
    "element.setAttribute('x', 'done')", "first._element.setAttribute('x', 'done')"
)
CASES["element-alias"] = ELEMENT_FIELD.replace(
    "element.setAttribute('x', 'done')", "saved._element.setAttribute('x', 'done')"
).replace(
    "return a === 8 && b === 9;",
    "return a === 8 && b === 9 && saved._element === element && current._element === element;",
)
CASES["element-pair"] = (
    CASES["element-alias"]
    .replace("new Item(9, element)", "new Item(9, other)")
    .replace("current._element === element", "current._element === other")
    .replace("  const a", "  current._element.setAttribute('y', 'current');\n  const a")
)
ELEMENT_PAIR_CHECKS = r"""
        (void)classes;
        (void)pressed;
        assert(@ENTRY@(element, foreign));
        assert(doc.read().attribute_value(button, atoms.intern("x")) == "done");
        assert(!doc.read().has_attribute(button, atoms.intern("y")));
        assert(foreign_doc.read().attribute_value(other_button, foreign_atoms.intern("y")) == "current");
        assert(@ENTRY@(alias, element));
        assert(doc.read().attribute_value(button, atoms.intern("y")) == "current");
        (void)doc.take_writes();
"""

# Variants the compiler must refuse.
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

for name, before, after in (
    ("write", "saved.n = 8;", "saved.n = 8; saved._element = element;"),
    ("nested-write", "saved.n = 8;", "saved.n = 8; if (first.n === 8) saved._element = null;"),
    ("repeated-store", "this._element = target;", "this._element = target; this._element = null;"),
    ("missing-actual", "new Item(7, element)", "new Item(7)"),
    ("mixed-actual", "new Item(7, element)", "new Item(7, {})"),
    ("replacement", "state.set('x', this);", "state.set('x', this); return {};"),
    ("parameter-observer", "this._element = target;", "this._element = target; target.nodeType;"),
    ("escape", "return a === 8 && b === 9;", "return saved;"),
    ("capture", "return a === 8 && b === 9;", "return () => saved._element;"),
    ("coercion", "return a === 8 && b === 9;", "return +saved._element;"),
    (
        "method",
        "state.set('x', this); }",
        "state.set('x', this); } unused() { return this._element; }",
    ),
):
    BAD["element-" + name] = CASES["element-field"].replace(before, after)
BAD["element-inheritance"] = (
    CASES["element-field"]
    .replace("class Item", "class Base")
    .replace(
        "  const first",
        "  class Item extends Base { constructor(n, target) { super(n, target); } }\n  const first",
    )
)


def main():
    args = dom.parse_args(__doc__, oracles=True)
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for name, body in CASES.items():
        text = source(body)
        inputs = 2 if name == "element-pair" else 1
        if inputs == 2:
            text = text.replace("probe(element)", "probe(element, other)")
        oracle = args.work / f"{name}.oracle.js"
        oracle.write_text(text + f"var result = probe({', '.join([ELEMENT] * inputs)});\n")
        assert run([args.reference, str(oracle)]).stdout == "result=true\n", name
        oracle.write_text(oracle.read_text() + "console.log('result=' + result);\n")
        assert run([args.node, str(oracle)]).stdout == "result=true\n", name
        ir, contract = dom.prepare(args, name, text, inputs, entry_name="probe", generic=True)
        declared = dict(contract, initial_intrinsics=INTRINSICS)
        for owned, optimize, manifest in dom.variants(declared):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            cpp = run([args.translate, "--mlir-to-cpp", str(native)]).stdout
            assert re.search(r"map_storage<std::string, ctn_\w+ \*>", cpp), label
            assert "shared_ptr<ctn_" not in cpp, label
            checks = ELEMENT_PAIR_CHECKS if inputs == 2 else CHECKS
            dom.standalone(args, native, label, checks, args.compilers, includes, libraries)
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
        if name.startswith("element-"):
            for owned, optimize, manifest in dom.variants(dict(declared, element_parameters=[])):
                dom.lower(
                    args,
                    ir,
                    manifest,
                    f"{name}-no-input-{owned}-{optimize}",
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
