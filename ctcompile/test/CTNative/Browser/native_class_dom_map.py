#!/usr/bin/env python3
"""Compose confined Map captures with the complete native DOM class proof."""

from CTNative.Browser import native_dom as dom
from CTNative.harness import run

CASES = {
    "local": """const state = new Map();
  class Item { key() { return 'x'; } }
  const item = new Item();
  state.set(element, 1);
  element.setAttribute(item.key(), 'done');
  return state.get(element) === 1;""",
    "method": """const state = new Map();
  class Item { read(key) { return state.get(key); } }
  const item = new Item();
  state.set(element, 1);
  element.setAttribute('x', 'done');
  return item.read(element) === 1;""",
    "helper": """const state = new Map();
  const lookup = key => state.get(key);
  class Item { read(key) { return lookup(key); } }
  const item = new Item();
  state.set(element, 1);
  const before = lookup(element);
  element.setAttribute('x', 'done');
  const after = item.read(element);
  return before === 1 && after === 1;""",
    "unused": """const state = new Map();
  class Item { unused() { return state.get('x') === 1; } read() { return true; } }
  const item = new Item();
  state.set('x', 1);
  return item.read();""",
}

CASES["constants"] = """const state = new Map();
  const amount = 1, label = 'x', enabled = true, empty = null, absent = void 0;
  const lookup = key => state.get(key) === amount;
  class Item {
    read(key) { return lookup(key); }
    key() { return label; }
    valid() { return enabled && empty === null && absent === void 0; }
  }
  const item = new Item();
  state.set(element, 1);
  element.setAttribute(item.key(), 'done');
  const value = item.read(element);
  const valid = item.valid();
  return value && valid;"""
CASES["constant-chain"] = (
    CASES["constants"]
    .replace("  class Item", "  const wrap = key => lookup(key);\n  class Item")
    .replace("return lookup(key)", "return wrap(key)")
)

CHECKS = r"""
        (void)classes;
        (void)pressed;
        assert(@ENTRY@(element));
        assert(@ENTRY@(alias));
        assert(@ENTRY@(foreign));
        (void)doc.take_writes();
"""


def source(body):
    return "function probe(element) {\n" + body + "\n}\n"


def main():
    args = dom.parse_args(__doc__, oracles=True)
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for name, body in CASES.items():
        text = source(body)
        oracle = args.work / f"{name}.oracle.js"
        oracle.write_text(text + "var result = probe({setAttribute() {}});\n")
        assert run([args.reference, str(oracle)]).stdout == "result=true\n"
        oracle.write_text(oracle.read_text() + "console.log('result=' + result);\n")
        assert run([args.node, str(oracle)]).stdout == "result=true\n"
        ir, contract = dom.prepare(args, name, text, 1, entry_name="probe")
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined", "Map"])
        for owned, optimize, manifest in dom.variants(declared):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            checks = CHECKS
            if name != "unused":
                checks += 'assert(doc.read().attribute_value(button, atoms.intern("x")) == "done");'
            dom.standalone(
                args,
                native,
                label,
                checks,
                args.compilers,
                includes,
                libraries,
                allow_nullable_scalar=True,
            )
            executions += 4
        dom.refuse(
            args,
            ir,
            dict(contract, initial_intrinsics=["__ctbrowser_class_defined"]),
            name + "-undeclared",
        )
        refusals += 2
        dom.lower(args, ir, declared, name + "-budget", success=False, max_steps=0)
        refusals += 1
    bad = {
        "conditional-call": CASES["helper"]
        .replace("  const after = item.read(element);\n", "")
        .replace("after === 1", "item.read(element) === 1"),
        "mutable": CASES["method"]
        .replace("const state", "let state")
        .replace("  return item.read", "  state = new Map();\n  return item.read"),
        "detached": CASES["method"].replace(
            "return state.get(key)", "const get = state.get; return get(key)"
        ),
        "escaped-owner": CASES["method"].replace("return state.get(key)", "return state"),
        "unused-unknown": CASES["unused"].replace("state.get('x') === 1", "state.unknown()"),
        "unused-bad-key": CASES["unused"].replace("state.get('x') === 1", "state.get({}) === 1"),
        "prototype": "Map.prototype.get = () => 2;\n" + CASES["method"],
        "replaced-binding": "Map = 0;\n" + CASES["local"],
        "property-binding": "element.Map = 0;\n" + CASES["local"],
    }
    bad.update(
        {
            "constant-changing": CASES["constants"]
            .replace("const amount", "let amount")
            .replace("  const value", "  amount = 9;\n  const value"),
            "constant-writer": CASES["constants"]
            .replace("const amount", "let amount")
            .replace("state.get(key) === amount", "state.get(key) === amount++"),
            "constant-late": CASES["constants"]
            .replace(
                "  const amount = 1, label = 'x', enabled = true, empty = null, absent = void 0;\n",
                "",
            )
            .replace(
                "  class Item",
                "  const amount = 1, label = 'x', enabled = true, empty = null, absent = void 0;\n  class Item",
            ),
            "constant-input": CASES["constants"].replace("amount = 1", "amount = element"),
            "constant-object": CASES["constants"].replace("amount = 1", "amount = {n: 1}"),
            "constant-bigint": CASES["constants"].replace("amount = 1", "amount = 1n"),
            "constant-call": CASES["constants"].replace("state.get(key) === amount", "amount()"),
            "constant-store": CASES["constants"].replace(
                "read(key) { return", "read(key) { amount.n = 3; return"
            ),
            "constant-unused-effect": CASES["constants"]
            .replace(
                "  class Item", "  const hidden = n => { ambient(n + amount); };\n  class Item"
            )
            .replace("    key()", "    unused(n) { hidden(n); }\n    key()"),
            "constant-helper-escape": CASES["constants"].replace(
                "return lookup(key)", "element.setAttribute('bad', lookup); return lookup(key)"
            ),
        }
    )
    # Constant transport does not authorize arithmetic on a possibly absent Map read.
    for name in ("constants", "constant-chain"):
        bad[name + "-arithmetic"] = (
            CASES[name]
            .replace("state.get(key) === amount", "state.get(key) + amount")
            .replace("return value && valid;", "return value === 2 && valid;")
        )
    for name, body in bad.items():
        ir, contract = dom.prepare(args, name, source(body), 1, entry_name="probe")
        for owned, optimize, manifest in dom.variants(
            dict(contract, initial_intrinsics=["__ctbrowser_class_defined", "Map"])
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
        f"DOM class Maps: {len(CASES)} Node/VM sources, {executions} native executions, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
