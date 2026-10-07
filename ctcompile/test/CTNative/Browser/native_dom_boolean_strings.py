#!/usr/bin/env python3
"""Prove literal String equality inside exact synchronous Boolean borrowers."""

import hashlib

from CTNative.Browser import native_dom as dom

from CTNative.Browser import native_dom_boolean_cells as cells
from CTNative.Browser import native_dom_boolean_borrowers as borrowers

PENDING = """%read = ctjs.cell_get %cell
%name = ctjs.constant #ctjs.string<"transitionend">
%expected = ctjs.constant #ctjs.string<"transitionend">
%matches = ctjs.compare strict_eq %name, %expected
%condition = ctjs.truthy %matches
%selected = scf.if %condition -> (!ctjs.value) {
  %pending = ctjs.unary not %read
  scf.yield %pending : !ctjs.value
} else {
  scf.yield %read : !ctjs.value
}
ctjs.return %selected"""
JS = cells.JS.replace(
    "const pending = () => !seen;",
    "const eventName = 'transitionend';\n"
    "const pending = () => eventName === 'transitionend' ? !seen : seen;",
)


def program(body=PENDING, owner=cells.OWNER):
    return cells.program(
        owner, cells.function("pending$2", body) + cells.function("mark$3", cells.MARK)
    )


CASES = {"retained-equality": (program(), JS, {"before": True, "after": False}, True)}
for name, left, right, js_left, js_right, equal in (
    ("empty-unequal", '""', '"transitionend"', "''", "'transitionend'", False),
    ("astral-equal", '"🚀"', '"🚀"', "'🚀'", "'🚀'", True),
    ("unicode-distinct", '"é"', '"é"', "'é'", "'e\\u0301'", False),
    ("nul-suffix", '"a\\00b"', '"a\\00c"', "'a\\u0000b'", "'a\\u0000c'", False),
):
    body = PENDING.replace(
        '%name = ctjs.constant #ctjs.string<"transitionend">',
        f"%name = ctjs.constant #ctjs.string<{left}>",
    ).replace(
        '%expected = ctjs.constant #ctjs.string<"transitionend">',
        f"%expected = ctjs.constant #ctjs.string<{right}>",
    )
    source = JS.replace("eventName = 'transitionend'", f"eventName = {js_left}").replace(
        "eventName === 'transitionend'", f"eventName === {js_right}"
    )
    CASES[name] = (program(body), source, {"before": equal, "after": not equal}, True)

STRINGS = """%name = ctjs.constant #ctjs.string<"transitionend">
%expected = ctjs.constant #ctjs.string<"transitionend">
%matches = ctjs.compare strict_eq %name, %expected
ctjs.root %name in %frame
"""
STATEFUL = cells.framed(
    borrowers.SELECTED_RESULT.replace("%condition =", STRINGS + "%condition =").replace(
        "ctjs.truthy %pending", "ctjs.truthy %matches"
    )
)
CASES["framed-stateful"] = (
    borrowers.program(STATEFUL),
    borrowers.JS.replace("if (!seen)", "if ('transitionend' === 'transitionend')"),
    {"before": False, "after": True},
    True,
)


def bad_programs():
    bad = {}
    for name, value in (
        ("boolean", "#ctjs.boolean<true>"),
        ("number", "#ctjs.number<0>"),
        ("undefined", "#ctjs.undefined"),
    ):
        bad["mixed-" + name] = program(
            PENDING.replace('#ctjs.string<"transitionend">\n%matches', value + "\n%matches")
        )
    for name, kind in (("loose-equality", "eq"), ("ordering", "lt")):
        bad[name] = program(PENDING.replace("compare strict_eq", "compare " + kind))
    bad["string-condition"] = program(PENDING.replace("truthy %matches", "truthy %name"))
    bad["string-negation"] = program(
        PENDING.replace("%condition =", "%not = ctjs.unary not %name\n%condition =")
    )
    bad["string-return"] = program(PENDING.replace("return %selected", "return %name"))
    bad["string-store"] = program(
        PENDING.replace("ctjs.return", "ctjs.cell_set %cell, %name\nctjs.return")
    )
    bad["string-yield"] = program(PENDING.replace("scf.yield %pending", "scf.yield %name"))
    bad["unused-string-result"] = program(
        PENDING.replace(
            "%selected = scf.if %condition -> (!ctjs.value)",
            "%selected:2 = scf.if %condition -> (!ctjs.value, !ctjs.value)",
        )
        .replace(
            "scf.yield %pending : !ctjs.value",
            "scf.yield %pending, %name : !ctjs.value, !ctjs.value",
        )
        .replace(
            "scf.yield %read : !ctjs.value", "scf.yield %read, %name : !ctjs.value, !ctjs.value"
        )
        .replace("ctjs.return %selected", "ctjs.return %selected#0")
    )
    bad["unknown-producer"] = program(
        PENDING.replace(
            '%name = ctjs.constant #ctjs.string<"transitionend">',
            '%name = ctjs.load_global "unknownString"',
        )
    )
    bad["cell-comparison"] = program(PENDING.replace("strict_eq %name", "strict_eq %cell"))
    effect = '%ambient = ctjs.load_global "ambient"\n%u2 = ctjs.constant #ctjs.undefined\n%effect = ctjs.call %ambient(%u2)\n'
    bad["dormant-effect"] = program(PENDING.replace("} else {\n", "} else {\n" + effect))
    bad["late-owner-effect"] = program(
        owner=cells.OWNER.replace("ctjs.return %different", effect + "ctjs.return %different")
    )
    bad["root-after-exit"] = borrowers.program(
        STATEFUL.replace(
            "ctjs.frame_exit %frame", "ctjs.frame_exit %frame\nctjs.root %name in %frame"
        )
    )
    bad["unused-number-literal"] = program(
        PENDING.replace(
            "%condition =", "%two = ctjs.constant #ctjs.number<4611686018427387904>\n%condition ="
        )
    )
    return bad


def main():
    args = dom.parse_args(__doc__, oracles=True)
    original, js, _, _ = CASES["retained-equality"]
    assert (
        hashlib.sha256(original.encode()).hexdigest()
        == "6ef2487faab931dd143bf0eebe21945d3518fe3d280692e61ebe906fd568161e"
    )
    assert (
        hashlib.sha256(js.encode()).hexdigest()
        == "3be9b5c5a86921d8db1bffe741d752bac8008c65c73b3a0a7650327451beb020"
    )
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for name, (text, body, attributes, expected) in CASES.items():
        cells.oracle(args, name, body, attributes, expected)
        ir, contract = cells.prepare(args, name, text)
        for owned, optimize, manifest in dom.variants(contract):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            dom.standalone(
                args,
                native,
                label,
                cells.checks(attributes, expected),
                args.compilers,
                includes,
                libraries,
            )
            executions += 4
        dom.exhaust(
            args, ir, contract, name, [0, 8], "{budget} did not exhaust String borrower proof"
        )
        refusals += 2
        if name == "framed-stateful":
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
            assert cells.source_operations(args, diagnostic) == original, diagnostic
            refusals += 1
    print(
        f"DOM Boolean String comparisons: {len(CASES)} Node/VM sources, {executions} native executions, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
