#!/usr/bin/env python3
"""Test class-method callbacks that capture a fixed primitive (a `const` String and friends).

The native lowering may substitute the captured value, but must keep the
callback's DOM writes and its `this`/`new.target` checks. Each CASES source runs
under Node and the reference VM, then lowers natively and runs against a real
ctbrowser document; each BAD source must be refused.
"""

from CTNative.Browser.dom import dom
from CTNative.Browser.class_dom import callbacks
from CTNative.harness import run

# callbacks.METHOD with the written attribute value read from a captured `label`.
METHOD = callbacks.METHOD.replace("const local =", "const label = 'done'; const local =").replace(
    "node.setAttribute('inside', 'done')", "node.setAttribute('inside', label)"
)
# The capture holds 'inside', so the callback's comparison with 'done' is false:
# the probe returns false and stores 'inside'. The lowering once refused this
# exact source.
ORIGINAL = METHOD.replace("const label = 'done'", "const label = 'inside'")
MIXED = METHOD.replace(
    "const local =",
    "const count = 3, flag = true, empty = null, missing = void 0; const local =",
).replace(
    "return node.getAttribute('inside') === 'done';",
    "return count === 3 && flag && empty === null && missing === void 0"
    " && node.getAttribute('inside') === 'done';",
)
CASES = {
    "original-capture": ORIGINAL + callbacks.CALLS,
    "mixed-primitives": MIXED + callbacks.CALLS,
    "captured-helper": "const invoke = target => {"
    + MIXED.split("run(target) {", 1)[1].rsplit("}", 2)[0]
    + "}; class Item { run(target) { return invoke(target); } }"
    + callbacks.CALLS,
    "sibling-readers": METHOD.replace(
        "const result = local(target);",
        "const second = node => node.getAttribute('inside') === label;"
        " const first = local(target); const next = second(target); const result = first && next;",
    )
    + callbacks.CALLS,
    "conditional-return": MIXED.replace(
        "return result;", "if (result) { return true; } return false;"
    )
    + callbacks.CALLS,
}
BAD = {
    "global-undefined": MIXED.replace("void 0", "undefined"),
    "later-write": METHOD.replace("const label", "let label").replace(
        "const result =", "label = 'changed'; const result ="
    ),
    "write-after-call": METHOD.replace("const label", "let label").replace(
        "return result;", "label = 'changed'; return result;"
    ),
    "sibling-writer": METHOD.replace("const label", "let label").replace(
        "const result =", "const change = () => { label = 'changed'; }; change(); const result ="
    ),
    "unused-sibling-writer": METHOD.replace("const label", "let label").replace(
        "const result =", "const change = () => { label = 'changed'; }; const result ="
    ),
    "nested-writer": METHOD.replace("const label", "let label").replace(
        "const result =",
        "const change = () => { const nested = () => { label = 'changed'; }; nested(); };"
        " change(); const result =",
    ),
    "late-initialization": METHOD.replace("const label = 'done';", "let label;").replace(
        "const result =", "label = 'done'; const result ="
    ),
    "object": METHOD.replace("const label = 'done'", "const label = {}"),
    "bigint": METHOD.replace("const label = 'done'", "const label = 3n"),
    "input": METHOD.replace("const label = 'done'", "const label = target"),
    "computed": METHOD.replace("const label = 'done'", "const label = target.getAttribute('x')"),
    "returned": METHOD.replace("const result = local(target);", "const result = local;"),
    "stored": METHOD.replace("const result =", "target.saved = local; const result ="),
    "timer": METHOD.replace("const result =", "setTimeout(local, 0); const result ="),
    "event": METHOD.replace(
        "const result =", "target.addEventListener('click', local); const result ="
    ),
    "receiver": METHOD.replace("node.getAttribute('inside') === 'done'", "this === node"),
    "new-target": METHOD.replace("node =>", "function(node)").replace(
        "node.getAttribute('inside') === 'done'", "new.target === undefined"
    ),
    "discarded-effect": METHOD.replace(
        "node.setAttribute('inside', label)", "ambient(label)"
    ).replace("const result = local(target);", "local(target); const result = true;"),
    "forwarded-capture": METHOD.replace("const label = 'done'; ", ""),
}
# Every bad case gets the shared call harness. "forwarded-capture" moves the
# `label` declaration out of the method to the top of the probe, where it is a
# valid capture, so it is moved to CASES and must compile.
BAD = {
    name: ("const label = 'done'; " if name == "forwarded-capture" else "")
    + method
    + callbacks.CALLS
    for name, method in BAD.items()
}
CASES["forwarded-capture"] = BAD.pop("forwarded-capture")


def main(argv=None):
    args = dom.parse_args(__doc__, oracles=True, argv=argv)
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for name, body in CASES.items():
        original = name == "original-capture"
        expected = "false" if original else "true"
        value = "inside" if original else "done"
        text = callbacks.source(body)
        oracle = args.work / f"{name}.oracle.js"
        oracle.write_text(
            text
            + f"var first = {callbacks.ELEMENT}, second = {callbacks.ELEMENT};\n"
            + f"var result = probe(first, second) === {expected}"
            + " && first.trace === 'before,inside,after,' && second.trace === first.trace"
            + f" && first.attributes.inside === '{value}' && second.attributes.inside === '{value}';\n"
        )
        assert run([args.reference, str(oracle)]).stdout == "result=true\n", name
        oracle.write_text(oracle.read_text() + "console.log('result=' + result);\n")
        assert run([args.node, str(oracle)]).stdout == "result=true\n", name
        checks = (
            callbacks.CHECKS.replace("@STEPS@", "3")
            .replace("@ORDER@", '"before", "inside", "after"')
            .replace("@PRECHECKS@", "")
            .replace('== "done"', f'== "{value}"')
        )
        if original:
            checks = checks.replace("assert(@ENTRY@(", "assert(!@ENTRY@(")
        ir, contract = dom.prepare(args, name, text, 2, entry_name="probe")
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        for owned, optimize, manifest in dom.variants(declared):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            dom.standalone(args, native, label, checks, args.compilers, includes, libraries)
            executions += 4  # Explicit and deduced C++, each built by both compilers.
        dom.refuse(args, ir, contract, name + "-undeclared")
        dom.exhaust(args, ir, declared, name, [0, 8], "{budget} did not exhaust capture proof")
        refusals += 4  # Two undeclared-intrinsic refusals and two exhausted budgets (0, 8).
    for name, body in BAD.items():
        ir, contract = dom.prepare(args, name, callbacks.source(body), 2, entry_name="probe")
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        for owned, optimize, manifest in dom.variants(declared):
            diagnostic = dom.lower(
                args,
                ir,
                manifest,
                f"bad-{name}-{owned}-{optimize}",
                optimize=optimize,
                success=False,
                max_steps=1000000,
            )
            if name == "discarded-effect":
                # Substituting the literal succeeds before the ambient-effect
                # check fails; the refusal must restore every captured read.
                assert diagnostic.count("ctjs.load_upvalue") == ir.read_text().count(
                    "ctjs.load_upvalue"
                ), diagnostic
            refusals += 1
    print(
        f"DOM class callback captures: {len(CASES)} Node/VM sources, "
        f"{executions} native executions, {refusals} refusals"
    )


def test_captures(lit):
    main(lit.args("%dom_driver --node %node --reference %native_reference"))


if __name__ == "__main__":
    main()
