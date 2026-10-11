#!/usr/bin/env python3
"""Scalarize one Boolean cell shared by synchronous DOM helper functions.

A local `let seen = false` is captured by helper closures that read and write
it. Each case is run by Node and the reference VM, lowered to native C++ and
executed, and invalid variants must be refused with the source IR intact.
"""

import json
import re

from CTNative.Browser.dom import dom
from CTNative.HostContract.contract import fingerprint
from CTNative.harness import run, slotted_run, threshold


def function(name, body, argument="cell", private=True):
    parameters = ", ".join(f"%{name}: !ctjs.value" for name in ("this", "new", "callee", argument))
    return (
        f"  ctjs.func {'private ' if private else ''}@{name}({parameters})"
        " -> !ctjs.value attributes {upvalue_count = 0 : i32} {\n"
        + "\n".join("    " + line for line in body.splitlines())
        + "\n  }\n"
    )


PENDING = """%read = ctjs.cell_get %cell
%answer = ctjs.unary not %read
ctjs.return %answer"""
MARK = """%yes = ctjs.constant #ctjs.boolean<true>
ctjs.cell_set %cell, %yes
%u = ctjs.constant #ctjs.undefined
ctjs.return %u"""
RESET = MARK.replace("<true>", "<false>")
HELPERS = function("pending$2", PENDING) + function("mark$3", MARK)
OWNER = """%u = ctjs.constant #ctjs.undefined
%no = ctjs.constant #ctjs.boolean<false>
%seen = ctjs.create_cell %u
ctjs.cell_set %seen, %no
%before = ctjs.call_direct @pending$2(%u, %u, %u, %seen)
%ignored = ctjs.call_direct @mark$3(%u, %u, %u, %seen)
%after = ctjs.call_direct @pending$2(%u, %u, %u, %seen)
%key = ctjs.constant #ctjs.string<"setAttribute">
%setter = ctjs.get_property %element[%key]
%before_key = ctjs.constant #ctjs.string<"before">
%first_write = ctjs.call %setter(%element, %before_key, %before)
%after_key = ctjs.constant #ctjs.string<"after">
%second_write = ctjs.call %setter(%element, %after_key, %after)
%equal = ctjs.compare strict_eq %before, %after
%different = ctjs.unary not %equal
ctjs.return %different"""
JS = """let seen = false;
const pending = () => !seen;
const mark = () => { seen = true; };
const before = pending();
mark();
const after = pending();
element.setAttribute('before', before);
element.setAttribute('after', after);
return before !== after;"""


def program(owner=OWNER, helpers=HELPERS):
    return "module {\n" + function("probe$1", owner, "element", False) + helpers + "}\n"


def framed(body):
    return "%frame = ctjs.frame_enter 4\nctjs.root %cell in %frame\n" + body.replace(
        "ctjs.return", "ctjs.frame_exit %frame\nctjs.return"
    )


SIBLINGS = OWNER.replace(
    "%key =",
    "%reset = ctjs.call_direct @reset$4(%u, %u, %u, %seen)\n"
    "%again = ctjs.call_direct @pending$2(%u, %u, %u, %seen)\n%key =",
).replace(
    "%equal =",
    '%again_key = ctjs.constant #ctjs.string<"again">\n'
    "%third_write = ctjs.call %setter(%element, %again_key, %again)\n%equal =",
)
REPEATED = OWNER.replace(
    "%after =",
    "%middle = ctjs.call_direct @pending$2(%u, %u, %u, %seen)\n"
    "%twice = ctjs.call_direct @mark$3(%u, %u, %u, %seen)\n%after =",
).replace(
    "%equal =",
    '%middle_key = ctjs.constant #ctjs.string<"middle">\n'
    "%third_write = ctjs.call %setter(%element, %middle_key, %middle)\n%equal =",
)
CASES = {
    "read-write-read": (program(), JS, {"before": True, "after": False}, True),
    "direct-init-framed": (
        program(
            OWNER.replace(
                "%seen = ctjs.create_cell %u\nctjs.cell_set %seen, %no",
                "%seen = ctjs.create_cell %no",
            ),
            function("pending$2", framed(PENDING)) + function("mark$3", framed(MARK)),
        ),
        JS,
        {"before": True, "after": False},
        True,
    ),
    "sibling-writers": (
        program(SIBLINGS, HELPERS + function("reset$4", RESET)),
        JS.replace("const before", "const reset = () => { seen = false; };\nconst before")
        .replace(
            "element.setAttribute('before'",
            "reset();\nconst again = pending();\nelement.setAttribute('before'",
        )
        .replace("return before", "element.setAttribute('again', again);\nreturn before"),
        {"before": True, "after": False, "again": True},
        True,
    ),
    "repeated-calls": (
        program(REPEATED),
        JS.replace("const after", "const middle = pending();\nmark();\nconst after").replace(
            "return before", "element.setAttribute('middle', middle);\nreturn before"
        ),
        {"before": True, "after": False, "middle": False},
        True,
    ),
    "owner-store": (
        program(OWNER.replace("%after =", "ctjs.cell_set %seen, %no\n%after =")),
        JS.replace("const after", "seen = false;\nconst after"),
        {"before": True, "after": True},
        False,
    ),
    "conditional-store": (
        program(
            helpers=HELPERS.replace(
                "ctjs.cell_set %cell, %yes",
                "%condition = ctjs.truthy %yes\nscf.if %condition {\n  ctjs.cell_set %cell, %yes\n  scf.yield\n}",
            )
        ),
        JS.replace("seen = true;", "if (true) seen = true;"),
        {"before": True, "after": False},
        True,
    ),
}


def bad_programs():
    bad = {
        "before-init": program(
            OWNER.replace("ctjs.cell_set %seen, %no\n", "").replace(
                "%ignored =", "ctjs.cell_set %seen, %no\n%ignored ="
            )
        ),
        "uninitialized": program(OWNER.replace("ctjs.cell_set %seen, %no\n", "")),
        "nonboolean-init": program(
            OWNER.replace("ctjs.create_cell %u", "ctjs.create_cell %element")
        ),
        "nonboolean-owner-store": program(
            OWNER.replace("ctjs.cell_set %seen, %no", "ctjs.cell_set %seen, %element")
        ),
        "nonboolean-leaf-store": program(
            helpers=HELPERS.replace("#ctjs.boolean<true>", '#ctjs.string<"true">')
        ),
        "computed-store": program(
            helpers=HELPERS.replace(
                "ctjs.cell_set %cell, %yes",
                "%computed = ctjs.unary not %yes\nctjs.cell_set %cell, %computed",
            )
        ),
        "second-cell": program(
            OWNER.replace("%before =", "%other = ctjs.create_cell %no\n%before =").replace(
                "%after = ctjs.call_direct @pending$2(%u, %u, %u, %seen)",
                "%after = ctjs.call_direct @pending$2(%u, %u, %u, %other)",
            )
        ),
        "different-actual": program(
            OWNER.replace(
                "%after = ctjs.call_direct @pending$2(%u, %u, %u, %seen)",
                "%after = ctjs.call_direct @pending$2(%u, %u, %u, %no)",
            )
        ),
        "incoming-cell": program(
            OWNER.replace("%seen = ctjs.create_cell %u\nctjs.cell_set %seen, %no\n", "").replace(
                "%seen", "%element"
            )
        ),
        "identity-returned": program(OWNER.replace("ctjs.return %different", "ctjs.return %seen")),
        "identity-stored": program(
            OWNER.replace("%before =", 'ctjs.store_global "escaped", %seen\n%before =')
        ),
        "identity-compared": program(
            OWNER.replace("%before =", "%identity = ctjs.compare strict_eq %seen, %seen\n%before =")
        ),
        "aliased-cell": program(
            OWNER.replace("%before =", "%alias = ctjs.create_cell %seen\n%before =")
        ),
        "public-helper": program(
            helpers=HELPERS.replace("ctjs.func private @mark", "ctjs.func @mark")
        ),
        "closure-creator": program(
            OWNER.replace(
                "%before =", "%escaped = ctjs.create_closure %callee[3] this %this\n%before ="
            )
        ),
        "borrower-returned-cell": program(
            helpers=HELPERS.replace("ctjs.return %answer", "ctjs.return %cell")
        ),
        "borrower-forwarded-cell": program(
            helpers=HELPERS.replace(
                "%yes =",
                "%forwarded = ctjs.call_direct @pending$2(%this, %new, %callee, %cell)\n%yes =",
            )
        ),
        "recursive-borrower": program(
            helpers=HELPERS.replace(
                "%yes =", "%recur = ctjs.call_direct @mark$3(%this, %new, %callee, %cell)\n%yes ="
            )
        ),
        "implicit-observer": program(
            helpers=HELPERS.replace("ctjs.return %answer", "ctjs.return %callee")
        ),
        "effect-in-borrower": program(
            helpers=HELPERS.replace(
                "%yes =",
                '%ambient = ctjs.load_global "ambient"\n%unused = ctjs.constant #ctjs.undefined\n%effect = ctjs.call %ambient(%unused)\n%yes =',
            )
        ),
        "unused-bad-helper": program(
            helpers=HELPERS
            + function("unused$9", MARK.replace("#ctjs.boolean<true>", '#ctjs.string<"bad">'))
        ),
        "bad-frame": program(
            helpers=HELPERS.replace(
                "%read =",
                "%frame = ctjs.frame_enter 1\nctjs.frame_exit %frame\nctjs.root %cell in %frame\n%read =",
            )
        ),
        "body-symbol-observer": program().replace(
            "module {", "module attributes {test.borrow_ref = @mark$3} {"
        ),
    }
    for label, operands in {
        "receiver": "%element, %u, %u, %seen",
        "new-target": "%u, %element, %u, %seen",
        "callee": "%u, %u, %element, %seen",
    }.items():
        bad[label] = program(OWNER.replace("@mark$3(%u, %u, %u, %seen)", f"@mark$3({operands})"))
    bad["extra-formal"] = program(
        OWNER.replace("@mark$3(%u, %u, %u, %seen)", "@mark$3(%u, %u, %u, %seen, %no)"),
        function("pending$2", PENDING)
        + function("mark$3", MARK).replace(
            "%cell: !ctjs.value)", "%cell: !ctjs.value, %extra: !ctjs.value)"
        ),
    )
    bad["second-owner"] = program(helpers=HELPERS + function("second$9", OWNER, "element"))
    bad["loop-call"] = program(
        OWNER.replace(
            "%ignored = ctjs.call_direct @mark$3(%u, %u, %u, %seen)",
            "scf.while : () -> () {\n  %condition = ctjs.truthy %no\n  scf.condition(%condition)\n} do {\n"
            "  %nested = ctjs.call_direct @mark$3(%u, %u, %u, %seen)\n  scf.yield\n}",
        )
    )
    for label, before in (("effect-before", "%before ="), ("effect-after", "%equal =")):
        bad[label] = program(
            OWNER.replace(
                before,
                '%ambient = ctjs.load_global "ambient"\n%effect = ctjs.call %ambient(%u)\n'
                + before,
            )
        )
    bad["stamped-native-facts"] = bad["nonboolean-leaf-store"].replace(
        "upvalue_count = 0 : i32",
        "upvalue_count = 0 : i32, ctnative.cell_args = array<i32: 0>, ctnative.carried = true",
    )
    return bad


def prepare(args, name, text):
    raw = args.work / f"{name}.raw.mlir"
    ir = args.work / f"{name}.mlir"
    raw.write_text(text)
    run([args.opt, str(raw), "-o", str(ir)])
    return ir, {
        "version": 1,
        "provider": dom.PROVIDERS[0],
        "module_sha256": fingerprint(args.opt, ir),
        "entry": "probe$1",
        "element_parameters": [0],
    }


def checks(attributes, result):
    expected = str(result).lower()
    text = "(void)classes; (void)pressed;\n"
    text += f"assert(static_cast<bool>(@ENTRY@(element)) == {expected});\n"
    text += "const auto writes = doc.take_writes();\n"
    text += f"assert(writes.size() == {len(attributes)});\n"
    for index, (key, value) in enumerate(attributes.items()):
        text += f'assert(writes[{index}].name == atoms.intern("{key}"));\n'
        text += f'assert(doc.read().attribute_value(button, atoms.intern("{key}")) == "{str(value).lower()}");\n'
    text += "for (int repeat = 0; repeat < 3; ++repeat) {\n"
    text += f"assert(static_cast<bool>(@ENTRY@(alias)) == {expected});\n"
    text += f"assert(static_cast<bool>(@ENTRY@(foreign)) == {expected});\n"
    for key, value in attributes.items():
        text += f'assert(doc.read().attribute_value(button, atoms.intern("{key}")) == "{str(value).lower()}");\n'
        text += f'assert(foreign_doc.read().attribute_value(other_button, foreign_atoms.intern("{key}")) == "{str(value).lower()}");\n'
    return text + "}\n"


def oracle(args, name, body, attributes, expected):
    source = args.work / f"{name}.oracle.js"
    text = "function probe(element) {\n" + body + "\n}\n"
    text += "function verify() {\n"
    text += "var element = {attributes: {}, trace: [], setAttribute(k, v) { this.attributes[k] = String(v); this.trace.push(k); }};\n"
    text += (
        "var result = true;\nfor (var repeat = 0; repeat < 3; ++repeat) {\nelement.trace = [];\n"
    )
    text += f"result = result && probe(element) === {str(expected).lower()};\n"
    for key, value in attributes.items():
        text += f"result = result && element.attributes.{key} === '{str(value).lower()}';\n"
    text += f"result = result && element.trace.join(',') === '{','.join(attributes)}';\n}}\n"
    text += "return result;\n}\nvar result = verify();\n"
    source.write_text(text)
    observed = run([args.reference, str(source)])
    assert observed.stdout == "result=true\n", (name, "VM", observed.stdout, observed.stderr)
    source.write_text(text + "console.log('result=' + result);\n")
    observed = run([args.node, str(source)])
    assert observed.stdout == "result=true\n", (name, "Node", observed.stdout, observed.stderr)


OPERATIONS = re.compile(
    # A refused module is printed in custom syntax, while a pass error prints
    # the module in generic syntax with quoted operation names. Match both and
    # count every source operation, including ordinary calls.
    r'^\s*(?:%[^=\n]+ = )?"?((?:ctjs|scf)\.[a-z_]+)\b',
    re.M,
)


def source_operations(args, text):
    """Verify and generically print source or a pass error's retained module."""
    _, diagnostic, retained = text.partition("note: see current operation:")
    if diagnostic:
        text = retained.strip()
    # Custom assembly omits implicit empty scf.yield ops; generic assembly
    # prints them. Reprint both sides generically so the rollback comparison
    # also covers those yields.
    generic = run([args.opt, "--mlir-print-op-generic"], input_text=text).stdout
    operations = OPERATIONS.findall(generic)
    assert operations, generic
    return operations


def budget_boundary(args, ir, contract):
    """Check the last refused budget and source rollback on one representative."""
    config = args.work / "budget-boundary.json"
    config.write_text(json.dumps(contract) + "\n")
    original = source_operations(args, ir.read_text())
    refusals = 0

    def lowers(limit, *, require_source=False):
        nonlocal refusals
        output = args.work / f"budget-boundary-{limit}.mlir"
        flags = f"host-manifest={config} optimize=false host-max-steps={limit}"
        result = slotted_run(
            [
                args.opt,
                str(ir),
                f"--pass-pipeline=builtin.module(ctnative-lower-to-emitc{{{flags}}},{dom.CLEANUP})",
                "-o",
                str(output),
            ],
            capture_output=True,
            text=True,
            timeout=120,
        )
        assert result.returncode in (0, 1), result.stderr
        text = output.read_text() if result.returncode == 0 else result.stderr
        if result.returncode == 0 and dom.NATIVE.search(text):
            assert not dom.FUNCTION.search(text), text
            return True
        assert "budget exhausted" in text, text
        if require_source or result.returncode == 0:
            assert source_operations(args, text) == original, text
        refusals += 1
        return False

    assert not lowers(0)
    assert lowers(1000000)
    first = threshold(lowers, high=1000000)
    assert not lowers(first - 1, require_source=True) and lowers(first)
    print(f"DOM Boolean proof minimum work budget: {first}; preceding cutoff preserves source")
    return refusals


def main():
    args = dom.parse_args(__doc__, oracles=True)
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for name, (text, body, attributes, expected) in CASES.items():
        oracle(args, name, body, attributes, expected)
        ir, contract = prepare(args, name, text)
        for owned, optimize, manifest in dom.variants(contract):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            dom.standalone(
                args,
                native,
                label,
                checks(attributes, expected),
                args.compilers,
                includes,
                libraries,
            )
            executions += 4
        dom.exhaust(
            args, ir, contract, name, [0, 8], "{budget} did not exhaust Boolean state proof"
        )
        refusals += 2
        if name == "read-write-read":
            refusals += budget_boundary(args, ir, contract)
    for name, text in bad_programs().items():
        ir, contract = prepare(args, "bad-" + name, text)
        original = source_operations(args, ir.read_text())
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
            if name.startswith("effect-"):
                # The Boolean-cell proof may succeed before the final DOM check
                # rejects the unknown call, so the refusal must leave every
                # original cell and call in the module.
                assert source_operations(args, diagnostic) == original, diagnostic
            refusals += 1
    for name, text in {
        "missing-argument": program(
            OWNER.replace("@mark$3(%u, %u, %u, %seen)", "@mark$3(%u, %u, %u)")
        ),
        "unknown-target": program(OWNER.replace("@mark$3(", "@missing$9(")),
    }.items():
        path = args.work / f"verifier-{name}.mlir"
        path.write_text(text)
        result = slotted_run([args.opt, str(path)], capture_output=True, text=True, timeout=30)
        assert result.returncode == 1 and "error:" in result.stderr, result.stderr
        refusals += 1
    print(
        f"DOM Boolean cells: {len(CASES)} Node/VM sources, {executions} native executions, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
