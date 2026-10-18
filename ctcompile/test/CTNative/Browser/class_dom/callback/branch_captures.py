#!/usr/bin/env python3
"""Test callbacks that capture a fixed primitive and are created and called inside an `if` arm.

Each CASES source runs under Node and the reference VM, then lowers to a native
DOM entry that is compiled and run against a real ctbrowser document. Each BAD
source writes, escapes or misuses the capture and must be refused.
"""

import re

from CTNative.Browser.dom import dom
from CTNative.Browser.class_dom import callbacks
from CTNative.harness import run

METHOD = """class Item {
  run(target) {
    const label = 'done';
    if (target.hasAttribute('ready')) {
      const local = node => {
        node.setAttribute('inside', label);
        return node.getAttribute('inside') === label;
      };
      return local(target);
    }
    return true;
  }
}"""
# The callback also captures a Number, Boolean, null and undefined.
MIXED = METHOD.replace(
    "const label = 'done';",
    "const label = 'done'; const count = 3, flag = true, empty = null, missing = void 0;",
).replace(
    "return node.getAttribute('inside') === label;",
    "return count === 3 && flag && empty === null && missing === void 0"
    " && node.getAttribute('inside') === label;",
)
# The callback is created and called inside a second, nested `if` arm.
NESTED = METHOD.replace(
    "const local =", "if (target.hasAttribute('ready')) { const local ="
).replace("return local(target);", "return local(target); }")
CASES = {
    "original": METHOD + callbacks.CALLS,
    "mixed": MIXED + callbacks.CALLS,
    "nested": NESTED + callbacks.CALLS,
}
# Each source breaks one requirement: the capture is never written after its
# initialization, the callback never escapes or runs outside its arm, and its
# body uses neither `this`, `new.target` nor an unknown global effect.
BAD = {
    "later-write": METHOD.replace("const label", "let label").replace(
        "if (target", "label = 'changed'; if (target"
    ),
    "conditional-write": METHOD.replace("const label", "let label").replace(
        "const local =", "label = 'changed'; const local ="
    ),
    "conditional-initialization": METHOD.replace("const label = 'done';", "let label;").replace(
        "const local =", "label = 'done'; const local ="
    ),
    "late-initialization": METHOD.replace("const label = 'done';", "let label;").replace(
        "    return true;", "    label = 'done'; return true;"
    ),
    "sibling-writer": METHOD.replace("const label", "let label").replace(
        "const local =", "const change = () => { label = 'changed'; }; change(); const local ="
    ),
    "unused-writer": METHOD.replace("const label", "let label").replace(
        "const local =", "const change = () => { label = 'changed'; }; const local ="
    ),
    "descendant-writer": METHOD.replace("const label", "let label").replace(
        "const local =",
        "const change = () => { const nested = () => { label = 'changed'; }; nested(); };"
        " change(); const local =",
    ),
    "input": METHOD.replace("const label = 'done'", "const label = target"),
    "global-undefined": METHOD.replace("const label = 'done'", "const label = undefined"),
    "returned": METHOD.replace("return local(target);", "return local;"),
    "stored": METHOD.replace(
        "return local(target);", "target.saved = local; return local(target);"
    ),
    "timer": METHOD.replace("return local(target);", "setTimeout(local, 0); return local(target);"),
    "event": METHOD.replace(
        "return local(target);", "target.addEventListener('click', local); return local(target);"
    ),
    "cross-function": METHOD.replace(
        "return local(target);", "const invoke = () => local(target); return invoke();"
    ),
    "before-definition": METHOD.replace("const local =", "local(target); const local ="),
    "outside-arm": METHOD.replace("return local(target);", "").replace(
        "    return true;", "    return local(target);"
    ),
    "receiver": METHOD.replace("node.getAttribute('inside') === label", "this === node"),
    "new-target": METHOD.replace("node =>", "function(node)").replace(
        "node.getAttribute('inside') === label", "new.target === void 0"
    ),
    "extra-argument": METHOD.replace("local(target)", "local(target, target)"),
    "effect": METHOD.replace("node.setAttribute('inside', label)", "ambient(label)"),
    "loop-creation": METHOD.replace("if (target", "while (target").replace(
        "return local(target);", "local(target); target.removeAttribute('ready');"
    ),
}
BAD = {name: method + callbacks.CALLS for name, method in BAD.items()}
# The arm depends on a default parameter that the second call overrides, so the
# captured parameter is written conditionally.
BAD["default-parameter"] = METHOD.replace("run(target)", "run(target, enabled = true)").replace(
    "target.hasAttribute('ready')", "enabled"
).replace(
    "return node.getAttribute", "return enabled && node.getAttribute"
) + callbacks.CALLS.replace("item.run(element)", "item.run(element, false)")


def main(argv=None):
    args = dom.parse_args(__doc__, oracles=True, argv=argv)
    includes, libraries = dom.link_options(args)
    checks = (
        callbacks.CHECKS.replace("@STEPS@", "1")
        .replace("@ORDER@", '"inside"')
        .replace("@PRECHECKS@", callbacks.BRANCH_CHECKS)
    )
    executions = refusals = 0
    for name, body in CASES.items():
        text = callbacks.source(body)
        oracle = args.work / f"{name}.oracle.js"
        oracle.write_text(
            text + f"var first = {callbacks.ELEMENT}, second = {callbacks.ELEMENT};\n"
            "var result = probe(first, second) && first.trace === '' && second.trace === '';\n"
            "first.attributes.ready = '';\n"
            "result = result && probe(first, second) && first.trace === 'inside,'"
            " && second.trace === '';\n"
            "first.trace = ''; delete first.attributes.ready; second.attributes.ready = '';\n"
            "result = result && probe(first, second) && first.trace === ''"
            " && second.trace === 'inside,';\n"
            "first.attributes.ready = ''; second.trace = '';\n"
            "result = result && probe(first, second) && first.trace === 'inside,'"
            " && second.trace === first.trace;\n"
            "first.trace = ''; result = result && probe(first, first)"
            " && first.trace === 'inside,inside,';\n"
        )
        # The oracle toggles the `ready` attribute on each element in turn and
        # checks that only an element with `ready` set records the callback's write.
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
        dom.exhaust(
            args, ir, declared, name, [0, 8], "{budget} did not exhaust branch capture proof"
        )
        refusals += 4  # Two undeclared-intrinsic refusals and two exhausted budgets (0, 8).
    for name, body in BAD.items():
        ir, contract = dom.prepare(args, name, callbacks.source(body), 2, entry_name="probe")
        if name == "loop-creation":
            assert "scf.while" in ir.read_text(), "loop refusal lost its source loop"
        if name == "default-parameter":
            text = ir.read_text()
            cell = re.search(r"(%\w+) = ctjs\.create_cell %arg4\b", text)
            assert cell, "default refusal lost its captured parameter cell"
            assert re.search(
                rf"scf\.if\b[^{{]*\{{[^}}]*ctjs\.cell_set {re.escape(cell[1])},", text
            ), "default refusal lost its conditional parameter write"
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
            # The refused lowering must leave every captured read in place.
            if name == "effect":
                assert diagnostic.count("ctjs.load_upvalue") == ir.read_text().count(
                    "ctjs.load_upvalue"
                ), diagnostic
            refusals += 1
    print(
        f"DOM class branch captures: {len(CASES)} Node/VM sources, "
        f"{executions} native executions, {refusals} refusals"
    )


def test_branch_captures(lit):
    main(lit.args("%dom_driver --node %node --reference %native_reference"))


if __name__ == "__main__":
    main()
