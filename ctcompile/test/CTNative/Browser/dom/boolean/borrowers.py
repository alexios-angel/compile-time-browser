#!/usr/bin/env python3
"""Test helpers that conditionally write a captured Boolean cell (Boolean "borrowers").

A helper like `mark = () => { const before = seen; if (!seen) seen = true;
return before; }` borrows the caller's `seen` cell. The native lowering inlines
it into its caller before re-proving the cell's state. Each case is written as
hand-made ctjs IR plus the equivalent JavaScript, which is checked against Node
and the VM before the IR is lowered natively; the bad programs must be refused
and the malformed ones rejected by the verifier.
"""

from CTNative.Browser.dom import dom
from CTNative.Browser.dom.boolean import branches
from CTNative.Browser.dom.boolean import cells
from CTNative.Browser.dom.boolean import origins
from CTNative.Browser.dom.boolean import scopes
from CTNative.harness import slotted_run

# The helper's store: seen = true.
WRITE = "%yes = ctjs.constant #ctjs.boolean<true>\nctjs.cell_set %cell, %yes"
# mark$3: read the cell, store true only if it was false, return the old value.
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
# Stores true, then stores false or true in the two arms.
BOTH = (
    CONDITIONAL.replace("ctjs.truthy %pending", "ctjs.truthy %read")
    .replace(WRITE, WRITE.replace("<true>", "<false>"))
    .replace("\n}\nctjs.return", "\n} else {\n" + WRITE + "\nscf.yield\n}\nctjs.return")
    .replace(
        "scf.if %condition",
        "%preset = ctjs.constant #ctjs.boolean<true>\nctjs.cell_set %cell, %preset\nscf.if %condition",
    )
)
# The owner with a third mark() call, written to the `again` attribute.
THREE = OWNER.replace(
    "%key =", "%again = ctjs.call_direct @mark$3(%u, %u, %u, %seen)\n%key ="
).replace(
    "%equal =",
    '%again_key = ctjs.constant #ctjs.string<"again">\n'
    "%third_write = ctjs.call %setter(%element, %again_key, %again)\n%equal =",
)
# The store sits in a second, always-true `if` and roots the cell there.
NESTED = CONDITIONAL.replace(
    WRITE,
    "%same = ctjs.compare strict_eq %read, %read\n%inner = ctjs.truthy %same\n"
    "scf.if %inner {\n" + WRITE + "\nctjs.root %cell in %frame\nscf.yield\n}",
)
# The `if` yields the old value from both arms.
RETAINED_RESULT = CONDITIONAL.replace(
    "scf.if %condition {", "%selected = scf.if %condition -> (!ctjs.value) {"
).replace(
    "scf.yield\n}",
    "scf.yield %read : !ctjs.value\n} else {\nscf.yield %read : !ctjs.value\n}",
)
SELECTED_RESULT = RETAINED_RESULT.replace("ctjs.return %read", "ctjs.return %selected")
# The `if` yields the old and the new value; a second `if` selects between them.
MULTIPLE_RESULTS = """%read = ctjs.cell_get %cell
%pending = ctjs.unary not %read
%condition = ctjs.truthy %pending
%selected:2 = scf.if %condition -> (!ctjs.value, !ctjs.value) {
  %yes = ctjs.constant #ctjs.boolean<true>
  ctjs.cell_set %cell, %yes
  %after = ctjs.cell_get %cell
  scf.yield %read, %after : !ctjs.value, !ctjs.value
} else {
  %no = ctjs.constant #ctjs.boolean<false>
  ctjs.cell_set %cell, %no
  %after = ctjs.cell_get %cell
  scf.yield %read, %after : !ctjs.value, !ctjs.value
}
%old_condition = ctjs.truthy %selected#0
%answer = scf.if %old_condition -> (!ctjs.value) {
  scf.yield %selected#1 : !ctjs.value
} else {
  %yes = ctjs.constant #ctjs.boolean<true>
  scf.yield %yes : !ctjs.value
}
ctjs.return %answer"""
JS_MULTIPLE = "const mark = () => { const before = seen; if (!before) seen = true; else seen = false; const after = seen; return before ? after : true; };"
NESTED_RESULTS = MULTIPLE_RESULTS.replace(
    "scf.yield %selected#1 : !ctjs.value",
    "%inner = ctjs.truthy %selected#1\n"
    "%nested = scf.if %inner -> (!ctjs.value) {\n"
    "  scf.yield %selected#0 : !ctjs.value\n} else {\n"
    "  ctjs.root %cell in %frame\n"
    "  scf.yield %selected#1 : !ctjs.value\n}\n"
    "scf.yield %nested : !ctjs.value",
)
# Unconditional store; only the returned value goes through the `if`.
RESULT_ONLY = """%read = ctjs.cell_get %cell
%pending = ctjs.unary not %read
%condition = ctjs.truthy %read
%yes = ctjs.constant #ctjs.boolean<true>
ctjs.cell_set %cell, %yes
%selected = scf.if %condition -> (!ctjs.value) {
  scf.yield %read : !ctjs.value
} else {
  scf.yield %pending : !ctjs.value
}
ctjs.return %selected"""
# name -> (fixture module, MLIR program, JavaScript source, *arguments for
# that fixture's oracle() and checks(): (attributes, result) for cells,
# (options,) for branches and scopes).
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
    # A helper that returns a value yielded through its conditional; the
    # lowering once refused this exact source and must keep accepting it.
    "retained-result": (
        cells,
        cells.program(OWNER, cells.function("mark$3", RETAINED_RESULT)),
        JS,
        {"before": False, "after": True},
        True,
    ),
    "selected-result": (
        cells,
        cells.program(OWNER, cells.function("mark$3", SELECTED_RESULT)),
        JS,
        {"before": False, "after": True},
        True,
    ),
    "multiple-results": (
        cells,
        cells.program(THREE, cells.function("mark$3", MULTIPLE_RESULTS)),
        JS.replace(JS_MARK, JS_MULTIPLE)
        .replace(
            "element.setAttribute('before'", "const again = mark();\nelement.setAttribute('before'"
        )
        .replace(
            "return before !== after;",
            "element.setAttribute('again', again);\nreturn before !== after;",
        ),
        {"before": True, "after": False, "again": True},
        True,
    ),
    "nested-results-roots": (
        cells,
        origins.negated_literals(
            cells.program(OWNER, cells.function("mark$3", cells.framed(NESTED_RESULTS)))
        ),
        origins.negated_source(
            JS.replace(
                JS_MARK,
                JS_MULTIPLE.replace(
                    "return before ? after : true;",
                    "return before ? (after ? before : after) : true;",
                ),
            )
        ),
        {"before": True, "after": False},
        True,
    ),
    "result-only-snapshot": (
        cells,
        cells.program(OWNER, cells.function("mark$3", RESULT_ONLY)),
        JS.replace(
            JS_MARK,
            "const mark = () => { const before = seen, pending = !seen; seen = true; return before ? before : pending; };",
        ),
        {"before": True, "after": True},
        False,
    ),
    "owner-result-prefix": (
        branches,
        cells.program(
            branches.owner(branches.RESULTS, extra=branches.RESULT_WRITES),
            cells.function("pending$2", cells.PENDING) + cells.function("mark$3", MULTIPLE_RESULTS),
        ),
        branches.CASES["original-results"][1].replace(
            "const mark = () => { seen = true; };", JS_MULTIPLE
        ),
        {"results": True},
    ),
}


def nest(body, count, predicate="condition"):
    """Wrap `body` in `count` nested `scf.if %predicate` regions."""
    for _ in range(count):
        body = f"scf.if %{predicate} {{\n" + body + "\nscf.yield\n}"
    return body


def program(helper=CONDITIONAL, owner=OWNER, extra=""):
    """Return a module with `owner` as the entry and `helper` as mark$3."""
    return cells.program(owner, cells.function("mark$3", helper) + extra)


def bad_programs():
    """Return name -> module that verifies but that the lowering must refuse."""
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
        # 4607182418800017408 is the IEEE-754 bit pattern of the Number 1.0.
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
        "undefined-result": program(
            SELECTED_RESULT.replace(
                "%read =", "%u = ctjs.constant #ctjs.undefined\n%read ="
            ).replace("} else {\nscf.yield %read", "} else {\nscf.yield %u")
        ),
        "numeric-result": program(
            SELECTED_RESULT.replace(
                "%read =", "%zero = ctjs.constant #ctjs.number<0>\n%read ="
            ).replace("scf.yield %read", "scf.yield %zero", 1)
        ),
        "nonboolean-unused-result": program(
            MULTIPLE_RESULTS.replace("%read =", "%u = ctjs.constant #ctjs.undefined\n%read =")
            .replace("scf.yield %read, %after", "scf.yield %read, %u", 1)
            .split("%old_condition =", 1)[0]
            + "ctjs.return %selected#0"
        ),
        "computed-result-store": program(
            SELECTED_RESULT.replace(
                "ctjs.return %selected", "ctjs.cell_set %cell, %selected\nctjs.return %selected"
            )
        ),
        "nested-nonboolean-result": program(
            cells.framed(NESTED_RESULTS).replace(
                "scf.yield %selected#0", "%u = ctjs.constant #ctjs.undefined\nscf.yield %u"
            )
        ),
        "result-only-nonboolean": program(
            RESULT_ONLY.replace("%read =", "%u = ctjs.constant #ctjs.undefined\n%read =").replace(
                "scf.yield %read", "scf.yield %u"
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
        "effect-result-then": program(SELECTED_RESULT.replace(WRITE, ambient + WRITE)),
        "effect-result-else": program(SELECTED_RESULT.replace("} else {", "} else {\n" + ambient)),
        "result-root-after-exit": program(
            cells.framed(NESTED_RESULTS).replace(
                "ctjs.frame_exit %frame", "ctjs.frame_exit %frame\nctjs.root %selected#0 in %frame"
            )
        ),
        "result-hidden-caller": program(
            MULTIPLE_RESULTS,
            extra=cells.function(
                "hidden$9",
                "%u = ctjs.constant #ctjs.undefined\n%result = ctjs.call_direct @mark$3(%u, %u, %u, %cell)\nctjs.return %result",
            ),
        ),
        "result-symbol-observer": program(MULTIPLE_RESULTS).replace(
            "module {", "module attributes {test.borrow_ref = @mark$3} {"
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
    # Proof attributes written into the input IR must not be trusted.
    bad["forged-facts"] = bad["cell-as-condition"].replace(
        "upvalue_count = 0 : i32",
        "upvalue_count = 0 : i32, ctnative.cell_args = array<i32: 0>, ctnative.carried = true",
    )
    # 65 nested ifs in one helper: past the 64-level nesting limit.
    bad["too-deep"] = program(CONDITIONAL.replace(WRITE, nest(WRITE, 64)))
    # Owner and helper each nest 32 deep, under the 64-level combined limit in
    # LocalState.cpp on their own; inlined together they reach it.
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


def main(argv=None):
    args = dom.parse_args(__doc__, oracles=True, argv=argv)
    includes, libraries = dom.link_options(args)

    # Every case writes only files named after itself (the oracle source,
    # the prepared module, the name-owned-optimize builds and the
    # name-budget-N refusals), so they run side by side (dom.parallel).
    # budget_boundary's fixed budget-boundary-* outputs stay sequential below.
    def check_case(item):
        name, (fixture, text, body, *expected) = item
        fixture.oracle(args, name, body, *expected)
        ir, contract = cells.prepare(args, name, text)
        boundary = None
        executions = 0
        for owned, optimize, manifest in dom.variants(contract):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            dom.standalone(
                args, native, label, fixture.checks(*expected), args.compilers, includes, libraries
            )
            executions += 4  # Explicit and deduced C++, each built by both compilers.
        dom.exhaust(
            args, ir, contract, name, [0, 8], "{budget} did not exhaust conditional borrower proof"
        )
        refusals = 2
        if name in ("scoped-owner", "owner-result-prefix"):
            boundary = (ir, contract)
        return executions, refusals, boundary

    case_results = dom.parallel(check_case, CASES.items())
    executions = sum(result[0] for result in case_results)
    refusals = sum(result[1] for result in case_results)
    for boundary in (result[2] for result in case_results):
        if boundary is not None:
            refusals += cells.budget_boundary(args, *boundary)

    def check_bad(item):
        name, text = item
        ir, contract = cells.prepare(args, "bad-" + name, text)
        original = cells.source_operations(args, ir.read_text())
        count = 0
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
                or name in ("root-after-exit", "result-root-after-exit")
            ):
                assert cells.source_operations(args, diagnostic) == original, diagnostic
            if name.startswith("combined-depth-"):
                assert "combined conditional depth exhausted" in diagnostic, diagnostic
            count += 1
        return count

    refusals += sum(dom.parallel(check_bad, bad_programs().items()))

    # Malformed IR that the ctjs verifier itself must reject.
    def check_malformed(item):
        name, text = item
        path = args.work / f"verifier-{name}.mlir"
        path.write_text(text)
        actual = slotted_run([args.opt, str(path)], text=True, capture_output=True, timeout=30)
        assert actual.returncode == 1 and "error:" in actual.stderr, actual.stderr
        return 1

    refusals += sum(
        dom.parallel(
            check_malformed,
            {
                "nested-return": program(CONDITIONAL.replace(WRITE, "ctjs.return %read\n" + WRITE)),
                "yield-result": program(
                    CONDITIONAL.replace("scf.yield", "scf.yield %read : !ctjs.value")
                ),
                "yield-missing-result": program(
                    MULTIPLE_RESULTS.replace(
                        "scf.yield %read, %after : !ctjs.value, !ctjs.value",
                        "scf.yield %read : !ctjs.value",
                        1,
                    )
                ),
                "yield-type-mismatch": program(
                    SELECTED_RESULT.replace(
                        "scf.yield %read : !ctjs.value", "scf.yield %condition : i1", 1
                    )
                ),
                "yield-sibling-value": program(
                    SELECTED_RESULT.replace("} else {\nscf.yield %read", "} else {\nscf.yield %yes")
                ),
                "missing-argument": program(
                    owner=OWNER.replace("@mark$3(%u, %u, %u, %seen)", "@mark$3(%u, %u, %u)")
                ),
            }.items(),
        )
    )
    print(
        f"DOM Boolean borrowers: {len(CASES)} Node/VM sources, {executions} native executions, {refusals} refusals"
    )


def test_borrowers(lit):
    main(lit.args("%dom_driver --node %node --reference %native_reference"))


if __name__ == "__main__":
    main()
