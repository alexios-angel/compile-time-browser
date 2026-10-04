#!/usr/bin/env python3
"""Carry one private Boolean cell through bounded synchronous DOM branches."""

import subprocess

from CTNative.Browser import native_dom as dom
from CTNative.Browser import native_dom_boolean_cells as cells
from CTNative.Browser.native_class_dom_callbacks import ELEMENT
from CTNative.harness import run

MARK = "%ignored = ctjs.call_direct @mark$3(%u, %u, %u, %seen)"
RESET = "%reset = ctjs.call_direct @reset$4(%u, %u, %u, %seen)"
SETTER = """%key = ctjs.constant #ctjs.string<"setAttribute">
%setter = ctjs.get_property %element[%key]"""
PREDICATE = """%has_key = ctjs.constant #ctjs.string<"hasAttribute">
%has_fn = ctjs.get_property %element[%has_key]
%ready_key = ctjs.constant #ctjs.string<"ready">
%ready = ctjs.call %has_fn(%element, %ready_key)
%condition = ctjs.truthy %ready"""
MISSING_ELSE = "scf.if %condition {\n  " + MARK + "\n  scf.yield\n}"


def owner(branch=MISSING_ELSE, *, predicate=PREDICATE, extra=""):
    # Make the existing setter available for effects inside either arm.
    return (
        cells.OWNER.replace(SETTER + "\n", "")
        .replace("%before =", SETTER + "\n%before =")
        .replace(MARK, predicate + "\n" + branch)
        .replace("%equal =", extra + "\n%equal =")
    )


BOTH_ARMS = """scf.if %condition {
  %marked = ctjs.call_direct @mark$3(%u, %u, %u, %seen)
  %inside = ctjs.call_direct @pending$2(%u, %u, %u, %seen)
  %branch_key = ctjs.constant #ctjs.string<"branch">
  %written = ctjs.call %setter(%element, %branch_key, %inside)
  scf.yield
} else {
  %marked = ctjs.call_direct @mark$3(%u, %u, %u, %seen)
  %reset = ctjs.call_direct @reset$4(%u, %u, %u, %seen)
  %inside = ctjs.call_direct @pending$2(%u, %u, %u, %seen)
  %branch_key = ctjs.constant #ctjs.string<"branch">
  %written = ctjs.call %setter(%element, %branch_key, %inside)
  scf.yield
}"""
NESTED = """scf.if %condition {
  %marked = ctjs.call_direct @mark$3(%u, %u, %u, %seen)
  %other_key = ctjs.constant #ctjs.string<"other">
  %other = ctjs.call %has_fn(%element, %other_key)
  %inner = ctjs.truthy %other
  scf.if %inner {
    %reset = ctjs.call_direct @reset$4(%u, %u, %u, %seen)
    scf.yield
  }
  scf.yield
}"""
RESULTS = """%selected:2 = scf.if %condition -> (!ctjs.value, !ctjs.value) {
  %marked = ctjs.call_direct @mark$3(%u, %u, %u, %seen)
  %inside = ctjs.call_direct @pending$2(%u, %u, %u, %seen)
  scf.yield %before, %inside : !ctjs.value, !ctjs.value
} else {
  %inside = ctjs.call_direct @pending$2(%u, %u, %u, %seen)
  scf.yield %before, %inside : !ctjs.value, !ctjs.value
}"""
RESULT_WRITES = """%prefix_key = ctjs.constant #ctjs.string<"prefix">
%prefix_write = ctjs.call %setter(%element, %prefix_key, %selected#0)
%branch_key = ctjs.constant #ctjs.string<"branch">
%branch_write = ctjs.call %setter(%element, %branch_key, %selected#1)"""
OWNER_WRITES = """scf.if %condition {
  %yes = ctjs.constant #ctjs.boolean<true>
  ctjs.cell_set %seen, %yes
  scf.yield
} else {
  ctjs.cell_set %seen, %no
  scf.yield
}"""


def outcome(ready, other, *, nested=False, skipped=False, branch=False, results=False):
    changed = ready and (not other if nested else True) and not skipped
    attributes = {"branch": not changed} if branch else {}
    attributes.update(before=True, after=not changed)
    if results:
        attributes.update(prefix=True, branch=not changed)
    return attributes, changed


CASES = {
    "missing-else": (
        cells.program(owner()),
        cells.JS.replace("mark();", "if (element.hasAttribute('ready')) { mark(); }"),
        {},
    ),
    "both-arms": (
        cells.program(owner(BOTH_ARMS), cells.HELPERS + cells.function("reset$4", cells.RESET)),
        cells.JS.replace(
            "const before", "const reset = () => { seen = false; };\nconst before"
        ).replace(
            "mark();",
            "if (element.hasAttribute('ready')) {\n"
            "  mark(); element.setAttribute('branch', pending());\n"
            "} else {\n"
            "  mark(); reset(); element.setAttribute('branch', pending());\n}",
        ),
        {"branch": True},
    ),
    "nested": (
        cells.program(owner(NESTED), cells.HELPERS + cells.function("reset$4", cells.RESET)),
        cells.JS.replace(
            "const before", "const reset = () => { seen = false; };\nconst before"
        ).replace(
            "mark();",
            "if (element.hasAttribute('ready')) {\n"
            "  mark(); if (element.hasAttribute('other')) { reset(); }\n}",
        ),
        {"nested": True},
    ),
    "original-results": (
        cells.program(owner(RESULTS, extra=RESULT_WRITES)),
        cells.JS.replace(
            "mark();",
            "let prefix, branch;\nif (element.hasAttribute('ready')) {\n"
            "  mark(); prefix = before; branch = pending();\n"
            "} else { prefix = before; branch = pending(); }",
        ).replace(
            "return before",
            "element.setAttribute('prefix', prefix);\n"
            "element.setAttribute('branch', branch);\nreturn before",
        ),
        {"results": True},
    ),
    "owner-stores": (
        cells.program(owner(OWNER_WRITES), cells.function("pending$2", cells.PENDING)),
        cells.JS.replace(
            "mark();", "if (element.hasAttribute('ready')) { seen = true; } else { seen = false; }"
        ),
        {},
    ),
    "skipped-call": (
        # This exact branch was a wave3 refusal; it now preserves the initial state.
        cells.program(cells.OWNER.replace(MARK, "%condition = ctjs.truthy %no\n" + MISSING_ELSE)),
        cells.JS.replace("mark();", "if (false) { mark(); }"),
        {"skipped": True},
    ),
}


def bad_programs():
    body = owner()
    ambient = '%ambient = ctjs.load_global "ambient"\n%effect = ctjs.call %ambient(%u)\n'
    bad = {
        "initialization-in-arm": cells.program(
            body.replace("ctjs.cell_set %seen, %no\n", "")
            .replace(
                "%before = ctjs.call_direct @pending$2(%u, %u, %u, %seen)",
                "%before = ctjs.constant #ctjs.boolean<true>",
            )
            .replace(MARK, "ctjs.cell_set %seen, %no\n" + MARK)
        ),
        "initialization-after-if": cells.program(
            body.replace("ctjs.cell_set %seen, %no\n", "")
            .replace(
                "%before = ctjs.call_direct @pending$2(%u, %u, %u, %seen)",
                "%before = ctjs.constant #ctjs.boolean<true>",
            )
            .replace("%after =", "ctjs.cell_set %seen, %no\n%after =")
        ),
        "nonboolean-store": cells.program(
            body.replace(MARK, "ctjs.cell_set %seen, %element\n" + MARK)
        ),
        "computed-store": cells.program(
            body.replace(
                MARK, "%computed = ctjs.unary not %no\nctjs.cell_set %seen, %computed\n" + MARK
            )
        ),
        "escaped-in-arm": cells.program(
            body.replace(MARK, 'ctjs.store_global "escaped", %seen\n' + MARK)
        ),
        "second-cell": cells.program(
            body.replace(
                MARK, "%second = ctjs.create_cell %no\n" + MARK.replace("%seen)", "%second)")
            )
        ),
        "wrong-receiver": cells.program(body.replace(MARK, MARK.replace("(%u,", "(%element,"))),
        "unknown-effect-before-call": cells.program(body.replace(MARK, ambient + MARK)),
        "unknown-effect-after-call": cells.program(body.replace(MARK, MARK + "\n" + ambient)),
        "unknown-effect-else": cells.program(
            owner(MISSING_ELSE + " else {\n" + ambient + "scf.yield\n}")
        ),
        "unknown-effect-after-if": cells.program(body.replace("%after =", ambient + "%after =")),
        "skipped-bad-borrower": cells.program(
            owner(predicate="%condition = ctjs.truthy %no"),
            cells.HELPERS.replace("#ctjs.boolean<true>", '#ctjs.string<"bad">'),
        ),
        "conditional-borrower": cells.program(
            body,
            cells.HELPERS.replace(
                "ctjs.cell_set %cell, %yes",
                "%test = ctjs.truthy %yes\nscf.if %test {\nctjs.cell_set %cell, %yes\nscf.yield\n}",
            ),
        ),
        "symbol-observer": cells.program(body).replace(
            "module {", "module attributes {test.borrow_ref = @mark$3} {"
        ),
        "second-owner": cells.program(
            body, cells.HELPERS + cells.function("second$9", body, "element")
        ),
        "loop-ancestor": cells.program(
            owner(
                "scf.while : () -> () {\nscf.condition(%condition)\n} do {\n"
                + MISSING_ELSE
                + "\nscf.yield\n}"
            )
        ),
    }
    nested = MARK
    for _ in range(65):
        nested = "scf.if %condition {\n" + nested + "\nscf.yield\n}"
    bad["too-deep"] = cells.program(owner(nested))
    return bad


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
        text += f"result = result && element.trace === '{','.join(attributes)},';\n"
    text += "}\nreturn result;\n}\nvar result = verify();\n"
    path = args.work / f"{name}.oracle.js"
    path.write_text(text)
    actual = run([args.reference, str(path)])
    assert actual.stdout == "result=true\n", (name, "VM", actual.stdout, actual.stderr)
    path.write_text(text + "console.log('result=' + result);\n")
    actual = run([args.node, str(path)])
    assert actual.stdout == "result=true\n", (name, "Node", actual.stdout, actual.stderr)


def main():
    args = dom.parse_args(__doc__, oracles=True)
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
            args, ir, contract, name, [0, 8], "{budget} did not exhaust Boolean branch proof"
        )
        refusals += 2
        if name == "missing-else":
            refusals += cells.budget_boundary(args, ir, contract)
    for name, text in bad_programs().items():
        ir, contract = cells.prepare(args, "bad-" + name, text)
        original = cells.OPERATIONS.findall(ir.read_text())
        assert original, ir
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
            if name.startswith("unknown-effect"):
                assert cells.OPERATIONS.findall(diagnostic) == original, diagnostic
            refusals += 1
    for name, text in {
        "missing-result": cells.program(
            owner(
                RESULTS.replace("scf.yield %before, %inside", "scf.yield %before"),
                extra=RESULT_WRITES,
            )
        ),
        "missing-argument": cells.program(
            owner(MISSING_ELSE.replace("@mark$3(%u, %u, %u, %seen)", "@mark$3(%u, %u, %u)"))
        ),
    }.items():
        path = args.work / f"verifier-{name}.mlir"
        path.write_text(text)
        actual = subprocess.run([args.opt, str(path)], text=True, capture_output=True, timeout=30)
        assert actual.returncode == 1 and "error:" in actual.stderr, actual.stderr
        refusals += 1
    print(
        f"DOM Boolean branches: {len(CASES)} Node/VM sources, {executions} native executions, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
