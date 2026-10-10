#!/usr/bin/env python3
"""Store scalar-field records in local DOM-entry Maps as borrowed stack records."""

import re

from CTNative.Browser.dom import dom
from CTNative.harness import run

# The base program: a record read back from the Map (`saved`) must stay an
# alias of the original object after the key is deleted and reinserted, so
# writing through it updates `first`; a real DOM write sits in between. The
# @...@ placeholders are filled per case in CASES.
WITNESS = """const state = new Map();
  @DECL@
  const first = @MAKE7@;
  state.set('x', first);
  const saved = state.get('x');
  state.delete('x');
  const second = @MAKE9@;
  state.set('x', second);
  saved.n = 8;
  const current = state.get('x');
  element.setAttribute('x', 'done');
  const a = first.n;
  const b = @READ@;
  return a === 8 && b === 9;"""
CLASS = "class Item { constructor(n) { this.n = n; } read() { return this.n; } }"
CASES = {
    "literal": WITNESS.replace("@DECL@", "")
    .replace("@MAKE7@", "{n: 7}")
    .replace("@MAKE9@", "{n: 9}")
    .replace("@READ@", "current.n"),
    "instance": WITNESS.replace("@DECL@", CLASS)
    .replace("@MAKE7@", "new Item(7)")
    .replace("@MAKE9@", "new Item(9)")
    .replace("@READ@", "current.read()"),
    # Element keys compare by identity: the same parameter is the same key.
    "element": """const state = new Map();
  const first = {n: 7};
  state.set(element, first);
  const saved = state.get(element);
  saved.n = 8;
  element.setAttribute('x', 'done');
  const present = state.has(element);
  return first.n === 8 && present;""",
    # The record may be created before its Map, which is the order a class
    # constructor that stores `this` into a Map produces.
    "late-map": """const first = {n: 7};
  const state = new Map();
  state.set('x', first);
  const current = state.get('x');
  element.setAttribute('x', 'done');
  return current.n === 7 && first.n === 7;""",
    "fields": """const state = new Map();
  const first = {label: 'done', count: 1, on: true};
  state.set('a', first);
  state.set('b', {label: 'other', count: 2, on: false});
  const saved = state.get('a');
  saved.count = saved.count + state.get('b').count;
  element.setAttribute('x', saved.label);
  const other = state.get('b');
  const present = state.has('a');
  return first.count === 3 && other.on === false && present;""",
}

# C++ appended to each standalone native test: call the compiled entry with the
# element, an alias of it and an element from another document, then check the
# attribute write landed.
CHECKS = r"""
        (void)classes;
        (void)pressed;
        assert(@ENTRY@(element));
        assert(@ENTRY@(alias));
        assert(@ENTRY@(foreign));
        assert(doc.read().attribute_value(button, atoms.intern("x")) == "done");
        (void)doc.take_writes();
"""

LITERAL = CASES["literal"]
# Variants of the literal case the native lowering must refuse: name -> (source,
# expected diagnostic substring, or None when any refusal will do).
BAD = {
    "returned": (
        LITERAL.replace("return a === 8 && b === 9;", "return first;"),
        "DOM record escapes its fields and its Map",
    ),
    "browser-argument": (
        LITERAL.replace("element.setAttribute('x', 'done');", "element.setAttribute('x', first);"),
        None,
    ),
    "alias-returned": (LITERAL.replace("return a === 8 && b === 9;", "return saved;"), None),
    "absent": (
        LITERAL.replace("const b = current.n;", "const b = state.get('y').n;"),
        "native record Map requires present reads",
    ),
    "schema": (
        LITERAL.replace("const second = {n: 9};", "const second = {m: 9};"),
        "DOM record fields differ from their Map's schema",
    ),
    "field-kind": (
        LITERAL.replace("saved.n = 8;", "saved.n = 'eight';"),
        "DOM record field requires one Number, String or Boolean kind",
    ),
    "new-field": (
        LITERAL.replace("saved.n = 8;", "saved.m = 8;"),
        "DOM record field requires one Number, String or Boolean kind",
    ),
    "nested": (
        LITERAL.replace("const first = {n: 7};", "const first = {n: 7, inner: {n: 1}};"),
        None,
    ),
    # A literal and an element key may not be told apart at compile time.
    "ambiguous": (
        LITERAL.replace("state.set('x', second);", "state.set(element, second);").replace(
            "const current = state.get('x');",
            "const current = state.get(element);\n  const again = state.get('x');",
        ),
        "native record Map read may answer a record stored under an equal key",
    ),
    "maybe-deleted": (
        LITERAL.replace(
            "state.set('x', first);", "state.set('x', first);\n  state.delete(element);"
        ),
        "native record Map requires present reads",
    ),
    "mixed": (
        LITERAL.replace("state.set('x', second);", "state.set('x', second);\n  state.set('y', 1);"),
        "DOM Map payload requires a Number, String or Boolean",
    ),
    "two-maps": (
        LITERAL.replace(
            "const first = {n: 7};", "const other = new Map();\n  const first = {n: 7};"
        ).replace("state.set('x', first);", "state.set('x', first);\n  other.set('x', first);"),
        "DOM record belongs to one local Map",
    ),
    "computed-field": (
        LITERAL.replace("const a = first.n;", "const a = first['n' + ''];"),
        None,
    ),
    "compared": (
        LITERAL.replace("return a === 8 && b === 9;", "return saved === first;"),
        None,
    ),
}


def source(body):
    """Wrap `body` as the entry function `probe(element)`."""
    return "function probe(element) {\n  " + body + "\n}\n"


# A plain-object stand-in for an element, passed to probe() by the Node and
# VM oracles.
ELEMENT = (
    "{attributes: {}, setAttribute(k, v) { this.attributes[k] = String(v); },"
    " getAttribute(k) { return k in this.attributes ? this.attributes[k] : null; }}"
)


def intrinsics(body):
    """Return the intrinsics the host contract must declare for `body`."""
    return (["__ctbrowser_class_defined"] if "class " in body else []) + ["Map"]


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
        ir, contract = dom.prepare(args, name, text, 1, entry_name="probe")
        declared = dict(contract, initial_intrinsics=intrinsics(body))
        for owned, optimize, manifest in dom.variants(declared):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            cpp = run([args.translate, "--mlir-to-cpp", str(native)]).stdout
            # Stack owners the Map borrows: no owning or boxed record carrier.
            if not re.search(r"map_storage<[^,<>]+, ctn_\w+ \*>", cpp) or "shared_ptr<ctn_" in cpp:
                raise RuntimeError(f"{label}: records are not borrowed stack owners\n{cpp}")
            dom.standalone(args, native, label, CHECKS, args.compilers, includes, libraries)
            executions += 4
        undeclared = [name for name in intrinsics(body) if name != "Map"]
        dom.refuse(args, ir, dict(contract, initial_intrinsics=undeclared), name + "-undeclared")
        refusals += 2
        dom.lower(args, ir, declared, name + "-budget", success=False, max_steps=0)
        refusals += 1
    for name, (body, reason) in BAD.items():
        ir, contract = dom.prepare(args, "bad-" + name, source(body), 1, entry_name="probe")
        for owned, optimize, manifest in dom.variants(
            dict(contract, initial_intrinsics=intrinsics(body))
        ):
            diagnostic = dom.lower(
                args,
                ir,
                manifest,
                f"bad-{name}-{owned}-{optimize}",
                optimize=optimize,
                success=False,
                max_steps=1000000,
            )
            if reason is not None and reason not in diagnostic:
                raise RuntimeError(f"bad-{name}: expected {reason!r}\n{diagnostic}")
            refusals += 1
    print(
        f"DOM Map records: {len(CASES)} Node/VM sources, "
        f"{executions} native executions, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
