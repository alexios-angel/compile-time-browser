#!/usr/bin/env python3
"""Keep proved synchronous callback calls inside their original branch and loop."""

from CTNative.Browser.dom import dom
from CTNative.Browser.class_dom import callbacks
from CTNative.Browser.class_dom.callback import captures
from CTNative.harness import run

CONDITIONAL = captures.METHOD.replace(
    "target.setAttribute('before', 'done');",
    "if (target.hasAttribute('ready')) { target.setAttribute('before', 'done');",
).replace("return result;", "return result; } return true;")
LOOP = captures.METHOD.replace(
    "const result = local(target);",
    "let i = 0; while (i < 2) { local(target); ++i; } const result = true;",
)
CASES = {
    "conditional": CONDITIONAL + callbacks.CALLS,
    "loop": LOOP + callbacks.CALLS,
}
BAD = {
    "conditional-escape": CONDITIONAL.replace(
        "const result =", "target.saved = local; const result ="
    ),
    "loop-escape": LOOP.replace("local(target);", "target.saved = local; local(target);"),
    "conditional-effect": CONDITIONAL.replace(
        "node.setAttribute('inside', label)", "ambient(label)"
    ),
    "loop-effect": LOOP.replace("node.setAttribute('inside', label)", "ambient(label)"),
    "conditional-receiver": CONDITIONAL.replace("local(target)", "local.call(target, target)"),
    "loop-extra-argument": LOOP.replace("local(target)", "local(target, target)"),
    "conditional-missing-argument": CONDITIONAL.replace("local(target)", "local()"),
    "conditional-new-target": CONDITIONAL.replace("node =>", "function(node)").replace(
        "node.getAttribute('inside') === 'done'", "new.target === undefined"
    ),
    "cross-function": CONDITIONAL.replace(
        "const result = local(target);",
        "const invoke = () => local(target); const result = invoke();",
    ),
    "before-definition": LOOP.replace(
        "const label = 'done';", "local(target); const label = 'done';"
    ),
    "protected-call": CONDITIONAL.replace(
        "const result = local(target);",
        "let result; try { result = local(target); } catch (error) { result = false; }",
    ),
}


def main():
    args = dom.parse_args(__doc__, oracles=True)
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for name, body in CASES.items():
        conditional = name == "conditional"
        order = ["before", "inside"] + ([] if conditional else ["inside"]) + ["after"]
        checks = (
            callbacks.CHECKS.replace("@STEPS@", str(len(order)))
            .replace("@ORDER@", ", ".join('"' + key + '"' for key in order))
            .replace("@PRECHECKS@", callbacks.BRANCH_CHECKS if conditional else "")
        )
        text = callbacks.source(body)
        oracle = args.work / f"{name}.oracle.js"
        observation = text + f"var first = {callbacks.ELEMENT}, second = {callbacks.ELEMENT};\n"
        if conditional:
            observation += (
                "var result = probe(first, second) && first.trace === '' && second.trace === '';\n"
                "first.attributes.ready = '';\n"
                "result = result && probe(first, second) && first.trace === 'before,inside,after,'"
                " && second.trace === '';\n"
                "first.trace = ''; delete first.attributes.ready; second.attributes.ready = '';\n"
                "result = result && probe(first, second) && first.trace === ''"
                " && second.trace === 'before,inside,after,';\n"
                "first.attributes.ready = ''; second.trace = '';\n"
                "result = result && probe(first, second) && first.trace === 'before,inside,after,'"
                " && second.trace === first.trace;\n"
            )
        else:
            observation += (
                f"var result = probe(first, second) && first.trace === '{','.join(order)},'"
                " && second.trace === first.trace;\n"
            )
        oracle.write_text(observation)
        assert run([args.reference, str(oracle)]).stdout == "result=true\n", name
        oracle.write_text(oracle.read_text() + "console.log('result=' + result);\n")
        assert run([args.node, str(oracle)]).stdout == "result=true\n", name
        ir, contract = dom.prepare(args, name, text, 2, entry_name="probe")
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        for owned, optimize, manifest in dom.variants(declared):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            dom.standalone(args, native, label, checks, args.compilers, includes, libraries)
            executions += 4
        dom.refuse(args, ir, contract, name + "-undeclared")
        dom.exhaust(args, ir, declared, name, 0, "{budget} did not exhaust callback proof")
        refusals += 3
    for name, method in BAD.items():
        ir, contract = dom.prepare(
            args, name, callbacks.source(method + callbacks.CALLS), 2, entry_name="probe"
        )
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        for owned, optimize, manifest in dom.variants(declared):
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
        f"DOM class callback regions: {len(CASES)} Node/VM sources, "
        f"{executions} native executions, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
