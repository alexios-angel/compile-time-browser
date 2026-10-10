#!/usr/bin/env python3
"""Test fixed primitive captures declared outside a class and forwarded into its callbacks.

The constants are declared at the top of the probe, so the class method's
closure captures them and passes them on through its own capture slots
(`enclosing_indices`) to the inner callback. CASES must lower and run natively
with the same DOM writes as Node and the reference VM; BAD sources and the
mutated `ctjs.create_closure` operations must be refused.
"""

import re

from CTNative.Browser.dom import dom
from CTNative.Browser.class_dom import callbacks
from CTNative.Browser.class_dom.callback import captures
from CTNative.harness import run

DECLARATIONS = (
    "const label = 'done'; const count = 3, flag = true, empty = null, missing = void 0; "
)
# Every primitive is forwarded from the probe into the method's callback.
MIXED = DECLARATIONS + captures.MIXED.replace(DECLARATIONS, "")
# `label` is forwarded; the other primitives stay local to the method.
LOCAL_AND_OUTER = "const label = 'done'; " + captures.MIXED.replace("const label = 'done'; ", "")
# A top-level arrow function, not a method, forwards `label` to its callback.
HELPER = (
    "const label = 'done'; const invoke = target => {"
    + captures.METHOD.split("run(target) {", 1)[1]
    .rsplit("}", 2)[0]
    .replace("const label = 'done'; ", "")
    + ("}; class Item { run(target) { return invoke(target); } }")
)
CASES = {
    "all-forwarded": MIXED + callbacks.CALLS,
    "local-and-forwarded": LOCAL_AND_OUTER + callbacks.CALLS,
    "helper-forwarded": HELPER + callbacks.CALLS,
}
# captures.METHOD with `label` forwarded from the probe; the base for BAD.
SIMPLE = "const label = 'done'; " + captures.METHOD.replace("const label = 'done'; ", "")
BAD = {
    "sibling-writer": SIMPLE.replace("const label", "let label").replace(
        "const local =", "const change = () => { label = 'changed'; }; change(); const local ="
    ),
    "unused-writer": SIMPLE.replace("const label", "let label").replace(
        "const local =", "const change = () => { label = 'changed'; }; const local ="
    ),
    "descendant-writer": SIMPLE.replace("const label", "let label").replace(
        "const local =",
        "const change = () => { const nested = () => { label = 'changed'; }; nested(); };"
        " change(); const local =",
    ),
    "later-write": SIMPLE.replace("const label", "let label") + "label = 'changed';",
    "late-initialization": SIMPLE.replace("const label = 'done';", "let label;")
    + "label = 'done';",
    "object": SIMPLE.replace("const label = 'done'", "const label = {}"),
    "input": SIMPLE.replace("const label = 'done'", "const label = element"),
    "global-undefined": SIMPLE.replace("const label = 'done'", "const label = undefined"),
    "function": SIMPLE.replace("const label = 'done'", "const label = () => 'done'"),
    "self": SIMPLE.replace(
        "node.setAttribute('inside', label)", "node.setAttribute('inside', local)"
    ),
    "returned": SIMPLE.replace("const result = local(target);", "const result = local;"),
    "stored": SIMPLE.replace("const result =", "target.saved = local; const result ="),
    "timer": SIMPLE.replace("const result =", "setTimeout(local, 0); const result ="),
    "effect": SIMPLE.replace("node.setAttribute('inside', label)", "ambient(label)"),
}
# 65 nested callbacks, each forwarding `label`: past the forwarding depth limit.
deep = callbacks.LEAF.replace("'inside', 'done'", "'inside', label")
for _ in range(64):
    deep = "node => { const nested = " + deep + "; return nested(node); }"
BAD["deep-forwarding"] = "const label = 'done'; " + callbacks.METHOD.replace(callbacks.LEAF, deep)


def mutations(args, ir):
    """Yield (name, mutated IR path, verifier reason) for edits of the one
    `ctjs.create_closure`: bad capture slots, operand swaps and a duplicate
    creation. A reason means the ctjs verifier itself must reject the edit
    with that message; None means it verifies and the lowering must refuse it.
    """
    text = run([args.opt, str(ir), "--mlir-print-op-generic"]).stdout
    made = re.search(
        r'(%\w+) = "ctjs.create_closure"\(([^)]*)\) <\{enclosing_indices = array<i32: 0>, function = (\d+) : i32\}>[^\n]*',
        text,
    )
    assert made is not None, text
    operands = made[2].split(", ")
    assert len(operands) == 3, made[0]
    cases = {
        "slot-bounds": (made[0].replace("array<i32: 0>", "array<i32: 9>"), None),
        "slot-length": (
            made[0].replace("array<i32: 0>", "array<i32: 0, 0>"),
            "`enclosing_indices` has 2 entries for 1 capture",
        ),
        "slot-sentinel": (
            made[0].replace("array<i32: 0>", "array<i32: -2>"),
            "the only negative entry is -1",
        ),
        "placeholder": (
            made[0].replace(
                "(" + made[2] + ")", "(" + ", ".join([*operands[:2], operands[0]]) + ")"
            ),
            "placeholder",
        ),
        "parent-callee": (
            made[0].replace(
                "(" + made[2] + ")", "(" + ", ".join([operands[2], *operands[1:]]) + ")"
            ),
            None,
        ),
        "duplicate-creator": (
            made[0] + "\n    " + made[0].replace(made[1], "%extra_creation", 1),
            None,
        ),
    }
    for name, (replacement, verifier_reason) in cases.items():
        path = args.work / f"mutated-{name}.mlir"
        path.write_text(text[: made.start()] + replacement + text[made.end() :])
        yield name, path, verifier_reason


def main():
    args = dom.parse_args(__doc__, oracles=True)
    includes, libraries = dom.link_options(args)
    checks = (
        callbacks.CHECKS.replace("@STEPS@", "3")
        .replace("@ORDER@", '"before", "inside", "after"')
        .replace("@PRECHECKS@", "")
    )
    executions = refusals = 0
    for name, body in CASES.items():
        text = callbacks.source(body)
        oracle = args.work / f"{name}.oracle.js"
        oracle.write_text(
            text + f"var first = {callbacks.ELEMENT}, second = {callbacks.ELEMENT};\n"
            "var result = probe(first, second) && first.trace === 'before,inside,after,'"
            " && second.trace === first.trace;\n"
        )
        assert run([args.reference, str(oracle)]).stdout == "result=true\n", name
        oracle.write_text(oracle.read_text() + "console.log('result=' + result);\n")
        assert run([args.node, str(oracle)]).stdout == "result=true\n", name
        ir, contract = dom.prepare(args, name, text, 2, entry_name="probe")
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        for owned, optimize, manifest in dom.variants(declared):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            dom.standalone(args, native, label, checks, args.compilers, includes, libraries)
            executions += 4  # Explicit and deduced C++, each built by both compilers.
        dom.refuse(args, ir, contract, name + "-undeclared")
        dom.exhaust(args, ir, declared, name, [0, 8], "{budget} did not exhaust forwarding proof")
        refusals += 4  # Two undeclared-intrinsic refusals and two exhausted budgets (0, 8).
    for name, method in BAD.items():
        ir, contract = dom.prepare(
            args, name, callbacks.source(method + callbacks.CALLS), 2, entry_name="probe"
        )
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
            if name == "effect":
                assert diagnostic.count("ctjs.load_upvalue") == ir.read_text().count(
                    "ctjs.load_upvalue"
                ), diagnostic
            if name == "deep-forwarding":
                assert "class callback forwarding depth limit exceeded" in diagnostic, diagnostic
            refusals += 1
    ir, contract = dom.prepare(
        args, "simple", callbacks.source(SIMPLE + callbacks.CALLS), 2, entry_name="probe"
    )
    declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
    for name, changed, verifier_reason in mutations(args, ir):
        if verifier_reason:
            # Malformed metadata has no canonical fingerprint: the ctjs verifier
            # rejects it before any native proof or rewrite can see it.
            checked = run([args.opt, str(changed), "-o", "/dev/null"], success=False)
            assert checked.returncode == 1 and verifier_reason in checked.stderr, checked.stderr
            refusals += 1
        else:
            manifest = dict(declared, module_sha256=dom.fingerprint(args.opt, changed))
            dom.refuse(args, changed, manifest, name)
            refusals += 2
    print(
        f"DOM class callback forwarding: {len(CASES)} Node/VM sources, {executions} native executions, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
