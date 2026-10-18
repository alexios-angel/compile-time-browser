#!/usr/bin/env python3
"""Native lowering of a Boolean cell that is initialized inside one branch arm.

Each case is checked three ways: Node and the reference VM run the JavaScript,
the native build runs the lowered IR, and invalid variants (reads before
initialization, uses from a sibling arm or after the join, unknown effects)
must be refused.
"""

import subprocess

from CTNative.Browser.dom import dom
from CTNative.Browser.dom.boolean import branches
from CTNative.Browser.dom.boolean import cells
from CTNative.Browser.dom.boolean import origins
from CTNative.Browser.class_dom.callbacks import ELEMENT
from CTNative.harness import run

INITIAL = "%no = ctjs.constant #ctjs.boolean<false>\nctjs.cell_set %seen, %no"
BEFORE = "%before = ctjs.call_direct @pending$2(%u, %u, %u, %seen)"
AFTER = "%after = ctjs.call_direct @pending$2(%u, %u, %u, %seen)"
ARM = "\n".join((INITIAL, BEFORE, branches.MARK, AFTER))
NEUTRAL = (
    "%yes = ctjs.constant #ctjs.boolean<true>\nscf.yield %yes, %yes : !ctjs.value, !ctjs.value"
)
YIELD = "scf.yield %before, %after : !ctjs.value, !ctjs.value"
OTHER = """%other_key = ctjs.constant #ctjs.string<"other">
%other = ctjs.call %has_fn(%element, %other_key)
%inner = ctjs.truthy %other"""
WRITES = cells.OWNER[cells.OWNER.index("%key =") :]


def selection(arm=ARM + "\n" + YIELD, otherwise=NEUTRAL, condition="condition", result="selected"):
    return (
        f"%{result}:2 = scf.if %{condition} -> (!ctjs.value, !ctjs.value) {{\n"
        + arm
        + "\n} else {\n"
        + otherwise
        + "\n}"
    )


def owner(branch=None):
    return (
        "%u = ctjs.constant #ctjs.undefined\n%seen = ctjs.create_cell %u\n"
        + branches.PREDICATE
        + "\n"
        + (selection() if branch is None else branch)
        + "\n"
        + WRITES.replace("%before)", "%selected#0)")
        .replace("%after)", "%selected#1)")
        .replace("%before, %after", "%selected#0, %selected#1")
    )


JS_ARM = """let seen = false;
const pending = () => !seen;
const mark = () => { seen = true; };
before = pending();
mark();
after = pending();"""
JS_WRITES = """element.setAttribute('before', before);
element.setAttribute('after', after);
return before !== after;"""
JS = (
    "let before = true, after = true;\nif (element.hasAttribute('ready')) {\n"
    + JS_ARM
    + "\n}\n"
    + JS_WRITES
)
NESTED_STATE = ARM.replace(
    branches.MARK,
    branches.MARK
    + "\n%twice = ctjs.call_direct @mark$3(%u, %u, %u, %seen)\n"
    + OTHER
    + "\nscf.if %inner {\n"
    + branches.RESET
    + "\nscf.yield\n}",
)
MISSING_ELSE = (
    "%u = ctjs.constant #ctjs.undefined\n%seen = ctjs.create_cell %u\n"
    + branches.PREDICATE
    + "\nscf.if %condition {\n"
    + ARM
    + "\n"
    + WRITES[: WRITES.index("%equal =")]
    + "scf.yield\n}\n%result = ctjs.constant #ctjs.boolean<true>\nctjs.return %result"
)
ROOTED = "%frame = ctjs.frame_enter 8\n" + owner().replace(
    "%seen = ctjs.create_cell %u", "%seen = ctjs.create_cell %u\nctjs.root %seen in %frame"
).replace(NEUTRAL, "ctjs.root %seen in %frame\n" + NEUTRAL).replace(
    "%key =", "ctjs.root %seen in %frame\n%key ="
).replace("ctjs.return", "ctjs.frame_exit %frame\nctjs.return")
CASES = {
    "then-results": (cells.program(owner()), JS, {}),
    "else-results": (
        cells.program(
            owner(
                "%inverse = ctjs.unary not %ready\n%inverted = ctjs.truthy %inverse\n"
                + selection(NEUTRAL, ARM + "\n" + YIELD, "inverted")
            )
        ),
        JS.replace(
            "if (element.hasAttribute('ready'))", "if (!element.hasAttribute('ready')) {} else"
        ),
        {},
    ),
    "nested-scope": (
        cells.program(
            owner(
                selection(
                    OTHER + "\n" + selection(result="inside", condition="inner") + "\n"
                    "scf.yield %inside#0, %inside#1 : !ctjs.value, !ctjs.value"
                )
            )
        ),
        JS.replace(
            "element.hasAttribute('ready')",
            "element.hasAttribute('ready') && element.hasAttribute('other')",
        ),
        {"nested_scope": True},
    ),
    "missing-else": (
        cells.program(MISSING_ELSE),
        "let before, after;\nif (element.hasAttribute('ready')) {\n"
        + JS_ARM
        + "\n"
        + JS_WRITES[: JS_WRITES.index("return")]
        + "}\nreturn true;",
        {"missing_else": True},
    ),
    "nested-state": (
        cells.program(
            owner(selection(NESTED_STATE + "\n" + YIELD)),
            cells.HELPERS + cells.function("reset$4", cells.RESET),
        ),
        JS.replace("const mark", "const reset = () => { seen = false; };\nconst mark").replace(
            "mark();", "mark(); mark(); if (element.hasAttribute('other')) { reset(); }"
        ),
        {"nested_state": True},
    ),
    "origins-roots": (
        origins.negated_literals(
            cells.program(
                ROOTED,
                cells.function("pending$2", cells.framed(cells.PENDING))
                + cells.function("mark$3", cells.framed(cells.MARK)),
            )
        ),
        origins.negated_source(JS),
        {},
    ),
}


def bad_programs():
    body = owner()
    ambient = '%ambient = ctjs.load_global "ambient"\n%effect = ctjs.call %ambient(%u)\n'
    bad = {
        "no-initialization": cells.program(body.replace(INITIAL, "")),
        "before-init-read": cells.program(
            body.replace(INITIAL, "%early = ctjs.cell_get %seen\n" + INITIAL)
        ),
        "before-init-call": cells.program(
            body.replace(
                INITIAL, "%early = ctjs.call_direct @pending$2(%u, %u, %u, %seen)\n" + INITIAL
            )
        ),
        "sibling-read": cells.program(
            owner(selection(otherwise="%sibling = ctjs.cell_get %seen\n" + NEUTRAL))
        ),
        "sibling-call": cells.program(owner(selection(otherwise=branches.MARK + "\n" + NEUTRAL))),
        "sibling-initialization": cells.program(owner(selection(otherwise=ARM + "\n" + YIELD))),
        "join-read": cells.program(body.replace("%key =", "%joined = ctjs.cell_get %seen\n%key =")),
        "join-write": cells.program(
            body.replace(
                "%key =",
                "%yes = ctjs.constant #ctjs.boolean<true>\nctjs.cell_set %seen, %yes\n%key =",
            )
        ),
        "join-call": cells.program(
            body.replace(
                "%key =", "%joined = ctjs.call_direct @pending$2(%u, %u, %u, %seen)\n%key ="
            )
        ),
        "initialization-only-in-child": cells.program(
            body.replace(INITIAL, OTHER + "\nscf.if %inner {\n" + INITIAL + "\nscf.yield\n}")
        ),
        "nonboolean-initialization": cells.program(
            body.replace("ctjs.cell_set %seen, %no", "ctjs.cell_set %seen, %element")
        ),
        "computed-initialization": cells.program(
            body.replace(
                "ctjs.cell_set %seen, %no",
                "%computed = ctjs.unary not %no\nctjs.cell_set %seen, %computed",
            )
        ),
        "aliased-cell": cells.program(
            body.replace(BEFORE, "%alias = ctjs.create_cell %seen\n" + BEFORE)
        ),
        "escaped-cell": cells.program(
            body.replace(BEFORE, 'ctjs.store_global "escaped", %seen\n' + BEFORE)
        ),
        "second-cell": cells.program(
            body.replace(BEFORE, "%second = ctjs.create_cell %no\n" + BEFORE).replace(
                AFTER, AFTER.replace("%seen)", "%second)")
            )
        ),
        "hidden-caller": cells.program(
            body,
            cells.HELPERS
            + cells.function(
                "hidden$9",
                "%u = ctjs.constant #ctjs.undefined\n%result = ctjs.call_direct @mark$3(%u, %u, %u, %cell)\nctjs.return %result",
            ),
        ),
        "symbol-observer": cells.program(body).replace(
            "module {", "module attributes {test.borrow_ref = @mark$3} {"
        ),
        "owner-root-after-exit": cells.program(
            ROOTED.replace(
                "ctjs.frame_exit %frame", "ctjs.frame_exit %frame\nctjs.root %seen in %frame"
            )
        ),
        "sibling-root-after-exit": cells.program(
            ROOTED.replace(
                "ctjs.root %seen in %frame\n%yes",
                "ctjs.frame_exit %frame\nctjs.root %seen in %frame\n%yes",
            )
        ),
        "unknown-effect-before-init": cells.program(body.replace(INITIAL, ambient + INITIAL)),
        "unknown-effect-after-init": cells.program(body.replace(BEFORE, ambient + BEFORE)),
        "unknown-effect-after-if": cells.program(body.replace("%key =", ambient + "%key =")),
        "unknown-effect-other-arm": cells.program(owner(selection(otherwise=ambient + NEUTRAL))),
    }
    for name, operand in (
        ("receiver", "%element, %u, %u"),
        ("new-target", "%u, %element, %u"),
        ("callee", "%u, %u, %element"),
    ):
        bad[name] = cells.program(body.replace("@mark$3(%u, %u, %u,", "@mark$3(" + operand + ","))
    for name in (
        "nonboolean-leaf-store",
        "computed-store",
        "public-helper",
        "borrower-returned-cell",
        "borrower-forwarded-cell",
        "recursive-borrower",
        "implicit-observer",
        "effect-in-borrower",
        "unused-bad-helper",
        "bad-frame",
    ):
        text = cells.bad_programs()[name]
        original = cells.function("probe$1", cells.OWNER, "element", False)
        assert original in text, name
        bad[name] = text.replace(original, cells.function("probe$1", body, "element", False))
    bad["extra-formal"] = cells.program(
        body.replace("@mark$3(%u, %u, %u, %seen)", "@mark$3(%u, %u, %u, %seen, %no)"),
        cells.function("pending$2", cells.PENDING)
        + cells.function("mark$3", cells.MARK).replace(
            "%cell: !ctjs.value)", "%cell: !ctjs.value, %extra: !ctjs.value)"
        ),
    )
    bad["forged-facts"] = bad["sibling-read"].replace(
        "upvalue_count = 0 : i32",
        "upvalue_count = 0 : i32, ctnative.cell_args = array<i32: 0>, ctnative.carried = true",
    )
    bad["loop-scope"] = cells.program(
        MISSING_ELSE.replace(
            "scf.if %condition {", "scf.while : () -> () {\nscf.condition(%condition)\n} do {"
        )
    )
    nested = ARM + "\n" + WRITES[: WRITES.index("%equal =")]
    for _ in range(65):
        nested = "scf.if %condition {\n" + nested + "\nscf.yield\n}"
    bad["too-deep"] = cells.program(
        "%u = ctjs.constant #ctjs.undefined\n%seen = ctjs.create_cell %u\n"
        + branches.PREDICATE
        + "\n"
        + nested
        + "\n%result = ctjs.constant #ctjs.boolean<true>\nctjs.return %result"
    )
    return bad


def outcome(ready, other, *, nested_scope=False, nested_state=False, missing_else=False):
    """Return the expected (attribute values, return value) for one input pair.

    `ready` and `other` say whether those attributes are present. The helper
    that sets the cell runs only when `ready` holds; the nested variants also
    require `other` (nested_scope) or its absence (nested_state). With no else
    arm, nothing is written unless `ready` holds and the result is always true.
    """
    changed = ready and (other if nested_scope else not other if nested_state else True)
    attributes = {"before": True, "after": not changed}
    if missing_else:
        return attributes if ready else {}, True
    return attributes, changed


def checks(options):
    text = "(void)classes; (void)pressed;\nforeign_doc.log_writes(true);\n"
    text += "for (int repeat = 0; repeat < 2; ++repeat) {\n"
    for flags in range(4):
        ready, other = bool(flags & 1), bool(flags & 2)
        attributes, expected = outcome(ready, other, **options)
        for document, atoms, node, target in (
            ("doc", "atoms", "button", "alias"),
            ("foreign_doc", "foreign_atoms", "other_button", "foreign"),
        ):
            text += "{\n"
            for key, present in (("ready", ready), ("other", other)):
                if present:
                    text += (
                        f'(void){document}.set_attribute({node}, {atoms}.intern("{key}"), "");\n'
                    )
                else:
                    text += f'(void){document}.remove_attribute({node}, {atoms}.intern("{key}"));\n'
            text += f"(void){document}.take_writes();\n"
            text += f"assert(static_cast<bool>(@ENTRY@({target})) == {str(expected).lower()});\n"
            text += f"const auto writes = {document}.take_writes();\nassert(writes.size() == {len(attributes)});\n"
            for index, (key, value) in enumerate(attributes.items()):
                text += f'assert(writes[{index}].name == {atoms}.intern("{key}"));\n'
                text += f'assert({document}.read().attribute_value({node}, {atoms}.intern("{key}")) == "{str(value).lower()}");\n'
            text += "}\n"
    return text + "}\n"


def oracle(args, name, body, options):
    text = "function probe(element) {\n" + body + "\n}\n"
    text += "function verify() {\nconst element = " + ELEMENT + ";\nlet result = true;\n"
    text += "for (let repeat = 0; repeat < 2; ++repeat) {\n"
    for flags in range(4):
        ready, other = bool(flags & 1), bool(flags & 2)
        attributes, expected = outcome(ready, other, **options)
        for key, present in (("ready", ready), ("other", other)):
            text += (
                f"element.attributes.{key} = '';\n"
                if present
                else f"delete element.attributes.{key};\n"
            )
        text += "element.trace = '';\n"
        text += f"result = probe(element) === {str(expected).lower()} && result;\n"
        for key, value in attributes.items():
            text += f"result = result && element.attributes.{key} === '{str(value).lower()}';\n"
        text += f"result = result && element.trace === '{','.join(attributes) + ',' if attributes else ''}';\n"
    text += "}\nreturn result;\n}\nvar result = verify();\n"
    path = args.work / f"{name}.oracle.js"
    path.write_text(text)
    actual = run([args.reference, str(path)])
    assert actual.stdout == "result=true\n", (name, "VM", actual.stdout, actual.stderr)
    path.write_text(text + "console.log('result=' + result);\n")
    actual = run([args.node, str(path)])
    assert actual.stdout == "result=true\n", (name, "Node", actual.stdout, actual.stderr)


def main(argv=None):
    args = dom.parse_args(__doc__, oracles=True, argv=argv)
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for name, (text, body, options) in CASES.items():
        oracle(args, name, body, options)
        ir, contract = cells.prepare(args, name, text)
        for owned, optimize, manifest in dom.variants(contract):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            dom.standalone(
                args, native, label, checks(options), args.compilers, includes, libraries
            )
            executions += 4
        dom.exhaust(
            args, ir, contract, name, [0, 8], "{budget} did not exhaust Boolean scope proof"
        )
        refusals += 2
        if name == "nested-state":
            refusals += cells.budget_boundary(args, ir, contract)
    for name, text in bad_programs().items():
        ir, contract = cells.prepare(args, "bad-" + name, text)
        original = cells.source_operations(args, ir.read_text())
        for owned, optimize, manifest in dom.variants(contract):
            diagnostic = dom.lower(
                args,
                ir,
                manifest,
                f"bad-{name}-{owned}-{optimize}",
                optimize=optimize,
                success=False,
                max_steps=1000000,
            )
            # These refusals can be reached after the pass has started
            # rewriting, so check that the source IR comes back unchanged.
            if name.startswith("unknown-effect") or name in (
                "effect-in-borrower",
                "owner-root-after-exit",
                "sibling-root-after-exit",
            ):
                assert cells.source_operations(args, diagnostic) == original, diagnostic
            refusals += 1
    for name, text in {
        "missing-result": cells.program(owner().replace(YIELD, "scf.yield %before : !ctjs.value")),
        "missing-argument": cells.program(
            owner().replace("@mark$3(%u, %u, %u, %seen)", "@mark$3(%u, %u, %u)")
        ),
    }.items():
        path = args.work / f"verifier-{name}.mlir"
        path.write_text(text)
        actual = subprocess.run([args.opt, str(path)], text=True, capture_output=True, timeout=30)
        assert actual.returncode == 1 and "error:" in actual.stderr, actual.stderr
        refusals += 1
    print(
        f"DOM Boolean scopes: {len(CASES)} Node/VM sources, {executions} native executions, {refusals} refusals"
    )


def test_scopes(lit):
    main(lit.args("%dom_driver --node %node --reference %native_reference"))


if __name__ == "__main__":
    main()
