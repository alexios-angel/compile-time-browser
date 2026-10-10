#!/usr/bin/env python3
"""Prove literal !0/!1 Boolean origins without admitting computed state stores."""

import re

from CTNative.Browser.dom import dom
from CTNative.Browser.dom.boolean import branches
from CTNative.Browser.dom.boolean import cells


def negated_literals(text):
    def replace(match):
        indent, value, boolean = match.groups()
        bits = "0" if boolean == "true" else "4607182418800017408"
        return (
            f"{indent}{value}_number = ctjs.constant #ctjs.number<{bits}>\n"
            f"{indent}{value} = ctjs.unary not {value}_number"
        )

    return re.sub(
        r"^( *)(%\w+) = ctjs\.constant #ctjs.boolean<(true|false)>$", replace, text, flags=re.M
    )


def negated_source(text):
    return text.replace("seen = false", "seen = !1").replace("seen = true", "seen = !0")


OWNER = negated_literals(cells.OWNER)
MARK = negated_literals(cells.MARK)
HELPERS = cells.function("pending$2", cells.PENDING) + cells.function("mark$3", MARK)
CASES = {
    "owner-hoisted": (
        cells.program(OWNER),
        cells.JS.replace("seen = false", "seen = !1"),
        {"before": True, "after": False},
        True,
    ),
    "owner-direct": (
        cells.program(
            OWNER.replace(
                "%seen = ctjs.create_cell %u\nctjs.cell_set %seen, %no",
                "%seen = ctjs.create_cell %no",
            )
        ),
        cells.JS.replace("seen = false", "seen = !1"),
        {"before": True, "after": False},
        True,
    ),
    "helper-origin": (
        cells.program(helpers=HELPERS),
        cells.JS.replace("seen = true", "seen = !0"),
        {"before": True, "after": False},
        True,
    ),
    # A raw negative-zero NumberAttr is also numeric zero. The equivalent
    # JavaScript oracle need not import to the same unary-negation spelling.
    "signed-zero": (
        cells.program(
            helpers=HELPERS.replace("#ctjs.number<0>", "#ctjs.number<9223372036854775808>")
        ),
        cells.JS.replace("seen = true", "seen = !-0"),
        {"before": True, "after": False},
        True,
    ),
}
for name in ("sibling-writers", "repeated-calls", "conditional-store"):
    text, body, attributes, expected = cells.CASES[name]
    CASES[name] = negated_literals(text), negated_source(body), attributes, expected

BRANCH_CASES = {}
for name in ("both-arms", "owner-stores"):
    text, body, options = branches.CASES[name]
    BRANCH_CASES["conditional-" + name] = negated_literals(text), negated_source(body), options


def bad_programs():
    # Re-run the affected ownership/caller/body boundaries with proved
    # origins present, so a numeric Boolean cannot bypass those checks.
    original = cells.bad_programs()
    names = (
        "before-init",
        "uninitialized",
        "second-cell",
        "different-actual",
        "aliased-cell",
        "identity-returned",
        "borrower-returned-cell",
        "borrower-forwarded-cell",
        "recursive-borrower",
        "implicit-observer",
        "effect-in-borrower",
        "unused-bad-helper",
        "bad-frame",
        "body-symbol-observer",
        "receiver",
        "new-target",
        "callee",
        "extra-formal",
        "second-owner",
        "loop-call",
        "effect-before",
        "effect-after",
        "computed-store",
    )
    bad = {name: negated_literals(original[name]) for name in names}
    for name, literal in {
        "two": "#ctjs.number<4611686018427387904>",
        "negative-one": "#ctjs.number<13830554455654793216>",
        "nan": "#ctjs.number<9221120237041090560>",
        "infinity": "#ctjs.number<9218868437227405312>",
        "string": '#ctjs.string<"0">',
        "boolean": "#ctjs.boolean<false>",
        "null": "#ctjs.null",
        "undefined": "#ctjs.undefined",
    }.items():
        bad["literal-" + name] = cells.program(OWNER, HELPERS.replace("#ctjs.number<0>", literal))
    for name, replacement in {
        "bare-number-store": "ctjs.cell_set %cell, %yes_number",
        "returned-number": "ctjs.cell_set %cell, %yes\nctjs.return %yes_number",
        "double-not-store": "%twice = ctjs.unary not %yes\nctjs.cell_set %cell, %twice",
    }.items():
        mark = MARK.replace("ctjs.cell_set %cell, %yes", replacement)
        if name == "returned-number":
            mark = mark.replace("\n%u = ctjs.constant #ctjs.undefined\nctjs.return %u", "")
        bad[name] = cells.program(
            OWNER, cells.function("pending$2", cells.PENDING) + cells.function("mark$3", mark)
        )
    bad["nonliteral-owner"] = cells.program(
        OWNER.replace("ctjs.unary not %no_number", "ctjs.unary not %element"), HELPERS
    )
    bad["nonliteral-helper"] = cells.program(
        OWNER, HELPERS.replace("ctjs.unary not %yes_number", "ctjs.unary not %cell")
    )
    bad["arithmetic-origin"] = cells.program(
        OWNER.replace(
            "%no = ctjs.unary not %no_number",
            "%sum = ctjs.binary add %no_number, %no_number\n%no = ctjs.unary not %sum",
        ),
        HELPERS,
    )
    bad["branch-result-origin"] = cells.program(
        OWNER.replace(
            "%no = ctjs.unary not %no_number",
            "%condition = ctjs.truthy %no_number\n"
            "%selected = scf.if %condition -> (!ctjs.value) {\n"
            "  scf.yield %no_number : !ctjs.value\n"
            "} else {\n  scf.yield %no_number : !ctjs.value\n}\n"
            "%no = ctjs.unary not %selected",
        ),
        HELPERS,
    )
    bad["wrong-unary"] = cells.program(
        OWNER, HELPERS.replace("ctjs.unary not %yes_number", "ctjs.unary plus %yes_number")
    )
    bad["stamped-native-facts"] = bad["nonliteral-helper"].replace(
        "upvalue_count = 0 : i32",
        "upvalue_count = 0 : i32, ctnative.cell_args = array<i32: 0>, ctnative.carried = true",
    )
    for name in ("initialization-in-arm", "nonboolean-store", "unknown-effect-else"):
        bad["branch-" + name] = negated_literals(branches.bad_programs()[name])
    return bad


def main():
    args = dom.parse_args(__doc__, oracles=True)
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for fixture, cases in ((cells, CASES), (branches, BRANCH_CASES)):
        for name, (text, body, *expectations) in cases.items():
            fixture.oracle(args, name, body, *expectations)
            ir, contract = cells.prepare(args, name, text)
            for owned, optimize, manifest in dom.variants(contract):
                label = f"{name}-{owned}-{optimize}"
                native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
                dom.standalone(
                    args,
                    native,
                    label,
                    fixture.checks(*expectations),
                    args.compilers,
                    includes,
                    libraries,
                )
                executions += 4
            dom.exhaust(
                args, ir, contract, name, [0, 8], "{budget} did not exhaust Boolean origin proof"
            )
            refusals += 2
            if name == "sibling-writers":
                refusals += cells.budget_boundary(args, ir, contract)
    for name, text in bad_programs().items():
        ir, contract = cells.prepare(args, "bad-" + name, text)
        original = cells.source_operations(args, ir.read_text()) if "effect" in name else None
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
            if original is not None:
                assert cells.source_operations(args, diagnostic) == original, diagnostic
            refusals += 1
    print(
        f"DOM Boolean origins: {len(CASES) + len(BRANCH_CASES)} Node/VM sources, "
        f"{executions} native executions, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
