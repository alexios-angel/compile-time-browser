#!/usr/bin/env python3
"""Execute DOM entries that own local standard Maps keyed by element identity."""

import re

from CTNative.Browser import native_dom as dom
from CTNative.harness import FLAGS, FUNCTION, build_and_run, run

KEYS = """function remember(first, second) {
  const seen = new Map();
  seen.set(first, 1).set(second, 2);
  first.setAttribute('data-size', seen.size);
  const removed = seen.delete(second);
  return removed && seen.has(first);
}
"""
KEYS_CHECKS = r"""
        (void)classes;
        (void)pressed;
        const element_ref other{&doc, child};
        const auto size = atoms.intern("data-size");
        assert(!@ENTRY@(element, alias));
        assert(doc.read().attribute_value(button, size) == "1");
        assert(@ENTRY@(element, other));
        assert(doc.read().attribute_value(button, size) == "2");
        // Equal node bits in another document are another key.
        assert(@ENTRY@(alias, foreign));
        assert(doc.read().attribute_value(button, size) == "2");
        assert(!@ENTRY@(foreign, foreign));
        assert(foreign_doc.read().attribute_value(other_button,
                                                  foreign_atoms.intern("data-size")) == "1");
        (void)doc.take_writes();
        const auto text = doc.create_text("not an element");
        for (const element_ref invalid : {element_ref{nullptr, button}, element_ref{&doc, {}},
                                          element_ref{&doc, text}}) {
            for (const bool first : {true, false}) {
                bool rejected = false;
                try { (void)@ENTRY@(first ? invalid : element, first ? element : invalid); }
                catch (const std::exception &) { rejected = true; }
                assert(rejected);
                assert(doc.take_writes().empty());
            }
        }
"""

PAYLOADS = """function payload(first, second) {
  const names = new Map();
  names.set(first, 'first');
  names.set(second, 'second');
  const kept = names.get(first);
  names.delete(second);
  if (names.get(first) === undefined) { first.classList.add('cleared'); }
  return kept === 'first';
}
"""
PAYLOADS_CHECKS = r"""
        (void)pressed;
        (void)foreign;
        const element_ref other{&doc, child};
        assert(@ENTRY@(element, other));
        assert(doc.read().attribute_value(button, classes) == "btn");
        assert(!@ENTRY@(element, alias));
        assert(doc.read().attribute_value(button, classes) == "btn cleared");
        (void)doc.take_writes();
"""

COUNTS = """function counts(first, second) {
  const totals = new Map();
  totals.set(first, 0);
  totals.set(second, 5);
  if (totals.get(first)) { first.toggleAttribute('data-truthy', true); }
  return totals.get(first);
}
"""
COUNTS_CHECKS = r"""
        (void)classes;
        (void)pressed;
        (void)foreign;
        const element_ref other{&doc, child};
        const auto truthy = atoms.intern("data-truthy");
        const auto zero = @ENTRY@(element, other);
        static_assert(std::is_same_v<decltype(zero), const ctnative::nullable_scalar>);
        assert(zero.tag == ctnative::nullable_scalar::kind::number && zero.value == 0);
        assert(!doc.read().has_attribute(button, truthy));
        const auto five = @ENTRY@(element, alias);
        assert(five.tag == ctnative::nullable_scalar::kind::number && five.value == 5);
        assert(doc.read().has_attribute(button, truthy));
        (void)doc.take_writes();
"""

FLAGS_SOURCE = """function flags(element) {
  const state = new Map();
  state.set('open', true);
  state.clear();
  const owners = new Map();
  owners.set(element, false);
  return state.size === 0 && owners.get(element) === false && owners.has(element);
}
"""
FLAGS_CHECKS = r"""
        (void)classes;
        (void)pressed;
        assert(@ENTRY@(element));
        assert(@ENTRY@(alias));
        assert(@ENTRY@(foreign));
        assert(doc.take_writes().empty());
"""

# Bootstrap Data's shape: element keys own child Maps of instance keys. The
# child reads after the browser call keep their presence proof.
REGISTER = """function register(element, other) {
  const data = new Map();
  data.has(element) || data.set(element, new Map());
  data.has(other) || data.set(other, new Map());
  const instances = data.get(element);
  if (!instances.has('bs.button') && 0 === instances.size) { instances.set('bs.button', 1); }
  element.classList.add('registered');
  data.get(other).set('bs.button', 2);
  return data.has(element) && data.get(element).get('bs.button') || null;
}
"""
REGISTER_CHECKS = r"""
        (void)pressed;
        const element_ref other{&doc, child};
        const auto same = @ENTRY@(element, alias);
        static_assert(std::is_same_v<decltype(same), const ctnative::nullable_scalar>);
        assert(same.tag == ctnative::nullable_scalar::kind::number && same.value == 2);
        const auto distinct = @ENTRY@(element, other);
        assert(distinct.tag == ctnative::nullable_scalar::kind::number && distinct.value == 1);
        const auto across = @ENTRY@(alias, foreign);
        assert(across.tag == ctnative::nullable_scalar::kind::number && across.value == 1);
        assert(doc.read().attribute_value(button, classes) == "btn registered");
        (void)doc.take_writes();
"""

UNREGISTER = """function unregister(element) {
  const data = new Map();
  data.set(element, new Map());
  const instances = data.get(element);
  instances.set('bs.button', 1);
  instances.delete('bs.button');
  0 === instances.size && data.delete(element);
  return data.has(element);
}
"""
UNREGISTER_CHECKS = r"""
        (void)classes;
        (void)pressed;
        (void)alias;
        assert(!@ENTRY@(element));
        assert(!@ENTRY@(foreign));
        assert(doc.take_writes().empty());
"""

ENTRIES = (
    ("keys", KEYS, 2, KEYS_CHECKS),
    ("payloads", PAYLOADS, 2, PAYLOADS_CHECKS),
    ("counts", COUNTS, 2, COUNTS_CHECKS),
    ("flags", FLAGS_SOURCE, 1, FLAGS_CHECKS),
    ("register", REGISTER, 2, REGISTER_CHECKS),
    ("unregister", UNREGISTER, 1, UNREGISTER_CHECKS),
)

REFUSALS = (
    ("returned", "return new Map();", "DOM entry return must be a scalar"),
    ("stored", "saved = new Map(); return true;", "DOM"),
    (
        "map-key",
        "const m = new Map(); m.set(m, 1); return true;",
        "DOM Map key requires",
    ),
    (
        "element-payload",
        "const m = new Map(); m.set(element, element); return true;",
        "DOM Map payload requires",
    ),
    (
        "detached",
        "const m = new Map(); const has = m.has; return has.call(m, element);",
        "DOM Map method is detached",
    ),
    (
        "unsupported-method",
        "const m = new Map(); m.entries(); return true;",
        "DOM Map property is not a supported constant method or size",
    ),
    (
        "constructor-argument",
        "const m = new Map(undefined); return m.size === 0;",
        "DOM Map requires an empty construction",
    ),
    (
        "loose-payload",
        "const m = new Map(); m.set(element, 1); return m.get(element) == 1;",
        "DOM Map payload comparison requires strict equality",
    ),
    (
        "mixed-keys",
        "const m = new Map(); m.set(element, 1); m.set('x', 2); return m.size === 2;",
        "native DOM entry did not pass complete native admission: native Map needs supported keys",
    ),
    (
        "attribute-map",
        "const m = new Map(); element.setAttribute('data-map', m); return true;",
        "DOM",
    ),
    ("size-call", "const m = new Map(); return m.size() === 0;", "DOM"),
    (
        "child-unguarded",
        "const m = new Map(); m.set(element, new Map()); m.clear(); return m.get(element).size === 0;",
        "native DOM Map: nested native Map get requires presence",
    ),
    (
        "child-returned",
        "const m = new Map(); m.set(element, new Map()); return m.get(element);",
        "DOM entry return must be a scalar",
    ),
    (
        "child-shared",
        "const c = new Map(); const m = new Map(); m.set(element, c); m.set(element, c); return true;",
        "DOM parent Map payload requires one fresh child Map",
    ),
    (
        "child-mixed",
        "const m = new Map(); m.set(element, new Map()); m.set(element, 1); return true;",
        "DOM parent Map payload requires one fresh child Map",
    ),
    (
        "depth-three",
        "const m = new Map(); m.set(element, new Map()); m.get(element).set('k', new Map()); return true;",
        "DOM Map payload requires a Number, String or Boolean",
    ),
)


def execute(args, native, name, checks, includes, libraries):
    """Run the one typed entry against the real DOM. Unlike dom.standalone, a
    proved Map read may use the existing tagged scalar carrier."""
    text = native.read_text()
    entries = dom.NATIVE.findall(text)
    if len(entries) != 1 or FUNCTION.search(text) or "ctnative.not_native" in text:
        raise RuntimeError(f"{name}: expected exactly the typed entry\n{text}")
    deduced = args.work / f"{name}.deduced.mlir"
    run([args.opt, str(native), "--ctnative-print-deduced", "-o", str(deduced)])
    client = (
        dom.CLIENT.replace("@CHECKS@", checks)
        .replace("@ENTRY@", entries[0])
        .replace("@NAME@", name)
    )
    for mode, ir in (("explicit", native), ("deduced", deduced)):
        cpp = run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
        if dom.VM.search(cpp) or re.search(r"\bmain\s*\(", cpp):
            raise RuntimeError(f"{name}/{mode}: not a standalone native DOM entry\n{cpp}")
        if not re.search(r"ctnative::make_(?:number_)?map<ctbrowser::element_ref\b", cpp):
            raise RuntimeError(
                f"{name}/{mode}: Map storage is not keyed by element identity\n{cpp}"
            )
        source = args.work / f"{name}.{mode}.cpp"
        source.write_text(cpp + client)
        for index, compiler in enumerate(args.compilers):
            result = build_and_run(
                compiler,
                source,
                args.work / f"{name}.{mode}.{index}",
                flags=[*FLAGS, *includes],
                libraries=libraries,
                nm=args.nm,
                vm=dom.VM,
            )
            if result.stdout != f"native DOM {name}: passed\n":
                raise RuntimeError(f"{name}/{mode}: real DOM observations were not completed")


def main():
    args = dom.parse_args(__doc__)
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for label, source, parameters, checks in ENTRIES:
        ir, contract = dom.prepare(args, label, source, parameters)
        declared = dict(contract, initial_intrinsics=["Map"])
        for owned, optimize, manifest in dom.variants(declared):
            name = f"{label}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, name, optimize=optimize)
            execute(args, native, name, checks, includes, libraries)
            executions += 4
        # The provider must declare the original constructor.
        dom.refuse(args, ir, contract, f"{label}-undeclared", "DOM")
        refusals += 2
        if label == "keys":
            for budget in (0, 1, 20):
                dom.lower(
                    args,
                    ir,
                    declared,
                    f"budget-{budget}",
                    max_steps=budget,
                    success=False,
                )
                refusals += 1
    for label, body, reason in REFUSALS:
        source = f"function bad(element) {{ {body} }}\n"
        ir, contract = dom.prepare(args, f"refused-{label}", source, 1, entry_name="bad")
        dom.refuse(
            args,
            ir,
            dict(contract, initial_intrinsics=["Map"]),
            f"refused-{label}",
            reason,
        )
        refusals += 2
    # A declaration may not replace the declared constructor.
    source = "var Map = 0;\n" + FLAGS_SOURCE
    ir, contract = dom.prepare(args, "rebound", source, 1, entry_name="flags")
    dom.refuse(args, ir, dict(contract, initial_intrinsics=["Map"]), "rebound", "DOM")
    refusals += 2
    print(
        f"native DOM Map: {len(ENTRIES)} entries, {executions} native executions against the "
        f"real DOM (both providers/policies/layouts, GCC/Clang), {refusals} refusals"
    )


if __name__ == "__main__":
    main()
