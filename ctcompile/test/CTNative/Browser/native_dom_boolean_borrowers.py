#!/usr/bin/env python3
"""Inline exact conditional Boolean borrowers before re-proving state edges."""

import subprocess

from CTNative.Browser import native_dom as dom
from CTNative.Browser import native_dom_boolean_branches as branches
from CTNative.Browser import native_dom_boolean_cells as cells
from CTNative.Browser import native_dom_boolean_origins as origins
from CTNative.Browser import native_dom_boolean_scopes as scopes

WRITE = "%yes = ctjs.constant #ctjs.boolean<true>\nctjs.cell_set %cell, %yes"
CONDITIONAL = (
    """%read = ctjs.cell_get %cell
%pending = ctjs.unary not %read
%condition = ctjs.truthy %pending
scf.if %condition {
"""
    + WRITE
    + """
scf.yield
}
ctjs.return %read"""
)
MARK = cells.function("mark$3", CONDITIONAL)
HELPERS = cells.function("pending$2", cells.PENDING) + MARK
OWNER = cells.OWNER.replace("@pending$2", "@mark$3").replace(branches.MARK + "\n", "")
JS_MARK = "const mark = () => { const before = seen; if (!seen) seen = true; return before; };"
JS = (
    cells.JS.replace("const pending = () => !seen;\n", "")
    .replace("const mark = () => { seen = true; };", JS_MARK)
    .replace("const before = pending();\nmark();", "const before = mark();")
    .replace("const after = pending();", "const after = mark();")
)
BOTH = (
    CONDITIONAL.replace("ctjs.truthy %pending", "ctjs.truthy %read")
    .replace(WRITE, WRITE.replace("<true>", "<false>"))
    .replace("\n}\nctjs.return", "\n} else {\n" + WRITE + "\nscf.yield\n}\nctjs.return")
    .replace(
        "scf.if %condition",
        "%preset = ctjs.constant #ctjs.boolean<true>\nctjs.cell_set %cell, %preset\nscf.if %condition",
    )
)
THREE = OWNER.replace(
    "%key =", "%again = ctjs.call_direct @mark$3(%u, %u, %u, %seen)\n%key ="
).replace(
    "%equal =",
    '%again_key = ctjs.constant #ctjs.string<"again">\n'
    "%third_write = ctjs.call %setter(%element, %again_key, %again)\n%equal =",
)
NESTED = CONDITIONAL.replace(
    WRITE,
    "%same = ctjs.compare strict_eq %read, %read\n%inner = ctjs.truthy %same\n"
    "scf.if %inner {\n" + WRITE + "\nctjs.root %cell in %frame\nscf.yield\n}",
)
CASES = {
    "return-snapshots": (
        cells,
        cells.program(OWNER, MARK),
        JS,
        {"before": False, "after": True},
        True,
    ),
    "both-arms": (
        cells,
        cells.program(THREE, cells.function("mark$3", BOTH)),
        "let seen = false;\n"
        "const mark = () => { const before = seen; seen = true; if (before) seen = false; else seen = true; return before; };\n"
        "const before = mark(), after = mark(), again = mark();\n"
        "element.setAttribute('before', before); element.setAttribute('after', after);\n"
        "element.setAttribute('again', again); return before !== after;",
        {"before": False, "after": True, "again": False},
        True,
    ),
    "nested-origins-roots": (
        cells,
        origins.negated_literals(
            cells.program(OWNER, cells.function("mark$3", cells.framed(NESTED)))
        ),
        origins.negated_source(
            JS.replace("if (!seen) seen = true;", "if (!seen) { if (seen === seen) seen = true; }")
        ),
        {"before": False, "after": True},
        True,
    ),
    "dynamic-owner": (
        branches,
        cells.program(branches.owner(), HELPERS),
        branches.CASES["missing-else"][1].replace("const mark = () => { seen = true; };", JS_MARK),
        {},
    ),
    "both-owner-arms": (
        branches,
        cells.program(
            branches.owner(branches.BOTH_ARMS), HELPERS + cells.function("reset$4", cells.RESET)
        ),
        branches.CASES["both-arms"][1].replace("const mark = () => { seen = true; };", JS_MARK),
        {"branch": True},
    ),
    "scoped-owner": (
        scopes,
        scopes.CASES["nested-state"][0].replace(cells.function("mark$3", cells.MARK), MARK),
        scopes.CASES["nested-state"][1].replace("const mark = () => { seen = true; };", JS_MARK),
        {"nested_state": True},
    ),
}


def nest(body, count, predicate="condition"):
    for _ in range(count):
        body = f"scf.if %{predicate} {{\n" + body + "\nscf.yield\n}"
    return body


def program(helper=CONDITIONAL, owner=OWNER, extra=""):
    return cells.program(owner, cells.function("mark$3", helper) + extra)


def bad_programs():
    ambient = '%ambient = ctjs.load_global "ambient"\n%u = ctjs.constant #ctjs.undefined\n%effect = ctjs.call %ambient(%u)\n'
    bad = {
        "before-init": program(owner=OWNER.replace("ctjs.cell_set %seen, %no\n", "")),
        "nonboolean-store": program(
            CONDITIONAL.replace("#ctjs.boolean<true>", '#ctjs.string<"true">')
        ),
        "computed-store": program(
            CONDITIONAL.replace("ctjs.cell_set %cell, %yes", "ctjs.cell_set %cell, %pending")
        ),
        "cell-as-condition": program(
            CONDITIONAL.replace("ctjs.truthy %pending", "ctjs.truthy %cell")
        ),
        "numeric-condition": program(
            CONDITIONAL.replace(
                "%condition =",
                "%one = ctjs.constant #ctjs.number<4607182418800017408>\n%condition =",
            ).replace("ctjs.truthy %pending", "ctjs.truthy %one")
        ),
        "undefined-condition": program(
            CONDITIONAL.replace(
                "%condition =", "%u = ctjs.constant #ctjs.undefined\n%condition ="
            ).replace("ctjs.truthy %pending", "ctjs.truthy %u")
        ),
        "raw-i1-condition": program(
            CONDITIONAL.replace(
                "%condition = ctjs.truthy %pending", "%condition = arith.constant true"
            )
        ),
        "result-bearing-if": program(
            CONDITIONAL.replace(
                "scf.if %condition {", "%selected = scf.if %condition -> (!ctjs.value) {"
            ).replace(
                "scf.yield\n}",
                "scf.yield %read : !ctjs.value\n} else {\nscf.yield %read : !ctjs.value\n}",
            )
        ),
        "forwarded-cell": program(
            CONDITIONAL.replace(
                WRITE,
                "%u = ctjs.constant #ctjs.undefined\n%forward = ctjs.call_direct @pending$2(%u, %u, %u, %cell)\n"
                + WRITE,
            ),
            extra=cells.function("pending$2", cells.PENDING),
        ),
        "recursive": program(
            CONDITIONAL.replace(
                WRITE,
                "%u = ctjs.constant #ctjs.undefined\n%again = ctjs.call_direct @mark$3(%u, %u, %u, %cell)\n"
                + WRITE,
            )
        ),
        "aliased-cell": program(
            CONDITIONAL.replace(WRITE, "%alias = ctjs.create_cell %cell\n" + WRITE)
        ),
        "identity-yielded": program(
            CONDITIONAL.replace(
                "scf.if %condition {", "%selected = scf.if %condition -> (!ctjs.value) {"
            ).replace(
                "scf.yield\n}",
                "scf.yield %cell : !ctjs.value\n} else {\nscf.yield %cell : !ctjs.value\n}",
            )
        ),
        "identity-returned": program(CONDITIONAL.replace("ctjs.return %read", "ctjs.return %cell")),
        "loop-ancestor": program(
            CONDITIONAL.replace(
                "scf.if %condition {", "scf.while : () -> () {\nscf.condition(%condition)\n} do {"
            )
        ),
        "root-after-exit": program(
            cells.framed(CONDITIONAL).replace(
                "ctjs.frame_exit %frame", "ctjs.frame_exit %frame\nctjs.root %cell in %frame"
            )
        ),
        "effect-then": program(CONDITIONAL.replace(WRITE, ambient + WRITE)),
        "effect-else": program(
            CONDITIONAL.replace(
                "\n}\nctjs.return", "\n} else {\n" + ambient + "scf.yield\n}\nctjs.return"
            )
        ),
        "effect-after-inline": program(
            owner=OWNER.replace(
                "%equal =", ambient.replace("%u = ctjs.constant #ctjs.undefined\n", "") + "%equal ="
            )
        ),
        "hidden-caller": program(
            extra=cells.function(
                "hidden$9",
                "%u = ctjs.constant #ctjs.undefined\n%result = ctjs.call_direct @mark$3(%u, %u, %u, %cell)\nctjs.return %result",
            )
        ),
        "symbol-observer": program().replace(
            "module {", "module attributes {test.borrow_ref = @mark$3} {"
        ),
        "public-helper": program().replace("ctjs.func private @mark", "ctjs.func @mark"),
        "wrong-receiver": program(owner=OWNER.replace("@mark$3(%u,", "@mark$3(%element,")),
        "second-cell": program(
            owner=OWNER.replace("%after =", "%other = ctjs.create_cell %no\n%after =").replace(
                "%after = ctjs.call_direct @mark$3(%u, %u, %u, %seen)",
                "%after = ctjs.call_direct @mark$3(%u, %u, %u, %other)",
            )
        ),
        "dormant-effect": program(
            extra=cells.function("unused$9", CONDITIONAL.replace(WRITE, ambient + WRITE))
        ),
    }
    bad["forged-facts"] = bad["cell-as-condition"].replace(
        "upvalue_count = 0 : i32",
        "upvalue_count = 0 : i32, ctnative.cell_args = array<i32: 0>, ctnative.carried = true",
    )
    bad["too-deep"] = program(CONDITIONAL.replace(WRITE, nest(WRITE, 64)))
    # Both source bodies fit their own depth limit; the inlined body would not.
    deep_owner = (
        "%u = ctjs.constant #ctjs.undefined\n%no = ctjs.constant #ctjs.boolean<false>\n%seen = ctjs.create_cell %no\n%condition = ctjs.truthy %no\n"
        + nest("%called = ctjs.call_direct @mark$3(%u, %u, %u, %seen)", 32)
        + "\nctjs.return %no"
    )
    for name, innermost in (
        ("state", WRITE),
        ("inert", "%unused = ctjs.constant #ctjs.boolean<true>"),
    ):
        helper = (
            "%read = ctjs.cell_get %cell\n%condition = ctjs.truthy %read\n"
            + nest(innermost, 32)
            + "\nctjs.return %read"
        )
        bad["combined-depth-" + name] = program(helper, deep_owner)
    return bad


def main():
    args = dom.parse_args(__doc__, oracles=True)
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for name, (fixture, text, body, *expected) in CASES.items():
        fixture.oracle(args, name, body, *expected)
        ir, contract = cells.prepare(args, name, text)
        for owned, optimize, manifest in dom.variants(contract):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            dom.standalone(
                args, native, label, fixture.checks(*expected), args.compilers, includes, libraries
            )
            executions += 4
        dom.exhaust(
            args, ir, contract, name, [0, 8], "{budget} did not exhaust conditional borrower proof"
        )
        refusals += 2
        if name == "scoped-owner":
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
            if (
                name.startswith("effect-")
                or name.startswith("combined-depth-")
                or name == "root-after-exit"
            ):
                assert cells.source_operations(args, diagnostic) == original, diagnostic
            if name.startswith("combined-depth-"):
                assert "combined conditional depth exhausted" in diagnostic, diagnostic
            refusals += 1
    for name, text in {
        "nested-return": program(CONDITIONAL.replace(WRITE, "ctjs.return %read\n" + WRITE)),
        "yield-result": program(CONDITIONAL.replace("scf.yield", "scf.yield %read : !ctjs.value")),
        "missing-argument": program(
            owner=OWNER.replace("@mark$3(%u, %u, %u, %seen)", "@mark$3(%u, %u, %u)")
        ),
    }.items():
        path = args.work / f"verifier-{name}.mlir"
        path.write_text(text)
        actual = subprocess.run([args.opt, str(path)], text=True, capture_output=True, timeout=30)
        assert actual.returncode == 1 and "error:" in actual.stderr, actual.stderr
        refusals += 1
    print(
        f"DOM Boolean borrowers: {len(CASES)} Node/VM sources, {executions} native executions, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
