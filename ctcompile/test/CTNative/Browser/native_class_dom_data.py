#!/usr/bin/env python3
"""Prove constructor key transport; retain the incomplete DOM session refusal."""

import argparse
from pathlib import Path
import re

from CTNative.Browser import native_dom as dom
from CTNative.Exports import boundary
from CTNative.harness import run
from CTNative.HostContract import contract as host
from CTNative.Lowering.Objects import class_initialization_inputs as classes


def source(constructor=False):
    inputs = Path(__file__).resolve().parents[1] / "Lowering/Objects/Inputs/class-initialization"
    text = (inputs / "50.mlir").read_text()
    kind = "constructor" if constructor else "holder"
    original = text.split(f"//--- class-map-record-nested-vendor-{kind}.js\n", 1)[1].split(
        "//---", 1
    )[0]
    root = Path(__file__).resolve().parents[4]
    vendor = (root / "ctbrowser/vendor/bootstrap/bootstrap.bundle.js").read_text()
    data = (
        vendor[
            vendor.index("    const t = new Map,") : vendor.index('        i = "transitionend",')
        ]
        .rstrip()
        .removesuffix(",")
        + ";\n"
    )
    if data not in original:
        raise RuntimeError("complete vendor Data declaration changed")
    if constructor:
        original = (
            original.replace("constructor(n)", "constructor(n, key)")
            .replace('e.set("element", "bs.item", this)', 'e.set(key, "bs.item", this)')
            .replace("new Item(2)", "new Item(2, element)")
            .replace("new Item(7)", "new Item(7, element)")
        )
    adapted = (
        original.replace("function probe() {", "function probe(element) {", 1)
        .replace('"element"', "element")
        .replace("return savedFirst.n", "a = savedFirst.n", 1)
        .replace("var a = probe();", "")
    )
    if data not in adapted:
        raise RuntimeError("DOM input adapter changed the vendor declaration")
    return adapted


def object_constructors(args, constructor):
    original = (
        constructor.replace("probe(element)", "probe()")
        .replace("    const t", "    const element = {};\n    const t", 1)
        .replace("a = savedFirst.n", "return savedFirst.n", 1)
        + "\nvar a = probe();\n"
    )
    checked = 0
    for label, text, expected in (
        ("object", original, 59112),
        (
            "alias",
            original.replace("    const t", "    const alias = element;\n    const t", 1).replace(
                "new Item(7, element)", "new Item(7, alias)"
            ),
            59112,
        ),
        (
            "distinct",
            original.replace("    const t", "    const other = {};\n    const t", 1)
            .replace("new Item(7, element)", "new Item(7, other)")
            .replace('e.get(element, "bs.item").n * 1000', 'e.get(other, "bs.item").n * 1000')
            .replace(
                "return savedFirst.n",
                'return (e.get(element, "bs.item") === null) * 100000 + savedFirst.n',
            ),
            159112,
        ),
    ):
        name = "class-map-record-constructor-key-" + label
        js, ir, _ = boundary.prepare(args, name, text)
        for command in ([args.node, "-e", classes.NODE, str(js)], [args.reference, str(js)]):
            if run(command).stdout != f"a={expected}\n":
                raise RuntimeError(f"{name}: source observation changed")
        requested = host.manifest(args.opt, ir)
        requested.update(initial_intrinsics=["Map", "__ctbrowser_class_defined"])
        prepared = classes.prepare(args, name, ir, requested, success=True)
        for optimize in (False, True):
            native = args.work / f"{name}-{optimize}.native.mlir"
            run(
                [
                    args.opt,
                    str(prepared),
                    f"--ctnative-lower-to-emitc=optimize={str(optimize).lower()}",
                    "-o",
                    str(native),
                ]
            )
            checked += classes.check_executable(args, f"{name}-{optimize}", native, expected)
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt", "node", "reference"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    original = source()
    constructor = source(constructor=True)
    cases = {
        "input": (original, 1, ""),
        "alias": (
            original.replace("    class Item", "    const alias = element;\n    class Item", 1)
            .replace("e.get(element,", "e.get(alias,")
            .replace("e.remove(element,", "e.remove(alias,"),
            1,
            "",
        ),
        "two-inputs": (
            original.replace("probe(element)", "probe(element, other)").replace(
                'e.set(element, "bs.item", second)', 'e.set(other, "bs.item", second)'
            ),
            2,
            "class nested Map DOM inputs require an alias proof",
        ),
        "property-observer": (
            original.replace("    class Item", "    element.n;\n    class Item", 1),
            1,
            "class DOM input has an observer outside its proved Map keys",
        ),
        "coercion-observer": (
            original.replace("    class Item", "    element + '';\n    class Item", 1),
            1,
            "class DOM input has an observer outside its proved Map keys",
        ),
        "captured-observer": (
            original.replace(
                "    class Item", "    const unused = () => element;\n    class Item", 1
            ),
            1,
            "unknown call, binding or reflective effect (op ctjs.create_closure)",
        ),
        "constructor": (constructor, 1, ""),
        "constructor-alias": (
            constructor.replace(
                "    class Item", "    const alias = element;\n    class Item", 1
            ).replace("new Item(7, element)", "new Item(7, alias)"),
            1,
            "",
        ),
        "constructor-two-inputs": (
            constructor.replace("probe(element)", "probe(element, other)").replace(
                "new Item(7, element)", "new Item(7, other)"
            ),
            2,
            "class nested Map DOM inputs require an alias proof",
        ),
    }
    for label, statement in (
        ("field", "this.element = key;"),
        ("property", "key.n;"),
        ("coercion", "key + '';"),
        ("capture", "const unused = () => key;"),
        ("suffix", "a = 17;"),
    ):
        cases["constructor-" + label] = (
            constructor.replace(
                'e.set(key, "bs.item", this);', f'e.set(key, "bs.item", this); {statement}'
            ),
            1,
            "class ",
        )
    observed = prepared_count = refusals = native_refusals = 0
    for name, (text, parameters, diagnostic) in cases.items():
        if not diagnostic:
            observed_source = args.work / f"{name}.oracle.js"
            observed_source.write_text(text + "\nvar a; probe({});\n")
            for command in (
                [args.node, "-e", classes.NODE, str(observed_source)],
                [args.reference, str(observed_source)],
            ):
                expected = 59112 if name.startswith("constructor") else 15927
                if run(command).stdout != f"a={expected}\n":
                    raise RuntimeError(f"{name}: source observation changed")
            observed += 1
        ir, contract = dom.prepare(args, name, text, parameters, entry_name="probe")
        contract.update(
            provider="ctbrowser-dom-data-session-v1",
            roots=[{"binding": "host", "properties": ["slot"]}],
            observations=["a"],
            absent_bindings=[],
            undefined_bindings=[],
            initial_intrinsics=["Map", "__ctbrowser_class_defined"],
        )
        prepared = classes.prepare(
            args, name, ir, contract, success=not diagnostic, diagnostic=diagnostic
        )
        if diagnostic:
            refusals += 1
            continue
        prepared_count += 1
        body = prepared.read_text()
        # Two record constructions and two independently owned child Maps survive.
        if body.count("ctjs.construct") != 4 or "ctjs.set_property" not in body:
            raise RuntimeError(f"{name}: preparation erased a record or child owner")
        entry = re.search(
            r"ctjs.func(?: private)? @probe\$\d+\((.*?)\)(.*?)(?=\n  ctjs.func |\n})", body, re.S
        )
        if not entry:
            raise RuntimeError(f"{name}: preparation lost its source entry")
        arguments = re.findall(r"(%[\w.$-]+): !ctjs.value", entry[1])
        if len(arguments) != 4 or any(
            arguments[-1] in line and "ctjs.root " not in line for line in entry[2].splitlines()
        ):
            raise RuntimeError(f"{name}: a DOM input escaped its discharged key uses")
        checked = dict(contract, module_sha256=host.fingerprint(args.opt, prepared))
        for optimize in (False, True):
            result = dom.lower(
                args,
                prepared,
                checked,
                f"{name}-owner-{optimize}",
                optimize=optimize,
                success=False,
            )
            if "native DOM Data" not in result:
                raise RuntimeError("local class Data acquired an unproved session owner")
            native_refusals += 1
        for label, requested, options in (
            ("stale", dict(contract, module_sha256="0" * 64), ""),
            ("no-map", dict(contract, initial_intrinsics=["__ctbrowser_class_defined"]), ""),
            ("budget", contract, "max-steps=0"),
            ("small-budget", contract, "max-steps=100"),
            ("missing-input", dict(contract, element_parameters=[]), ""),
        ):
            classes.prepare(args, f"{name}-{label}", ir, requested, success=False, options=options)
            refusals += 1
    executions = object_constructors(args, constructor)
    print(
        f"class DOM Data: {observed} Node/VM observations, {prepared_count} prepared entries, "
        f"{refusals} preparation refusals, {native_refusals} incomplete-session refusals; "
        f"{executions} native object-key executions; no native DOM admission"
    )


if __name__ == "__main__":
    main()
