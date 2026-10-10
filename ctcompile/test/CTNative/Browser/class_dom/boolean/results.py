#!/usr/bin/env python3
"""Check that a captured callback returning a Boolean from an `if`/`?:` arm
compiles natively and matches Node and the VM, keeps the state snapshots it
returns, and that non-Boolean or effectful variants are refused."""

import hashlib
import re

from CTNative.Browser.dom import dom
from CTNative.Browser.class_dom import callbacks
from CTNative.Browser.class_dom.boolean import captures
from CTNative.Browser.class_dom.boolean import conditionals
from CTNative.Browser.class_dom.boolean import mixed_captures as mixed
from CTNative.Browser.class_dom.parameter_defaults import budget_boundary
from CTNative.Browser.dom.boolean.cells import source_operations
from CTNative.harness import run

# The writer returns the old value of `seen` and flips it.
RESULT = "return seen ? (seen = false, true) : (seen = true, false);"
METHOD = captures.METHOD.replace("seen = true;", RESULT)
REPEATED = METHOD.replace(
    "    mark();",
    "    const snap1 = mark(), snap2 = mark(), snap3 = mark();\n"
    "    target.setAttribute('first', snap1); target.setAttribute('second', snap2);\n"
    "    target.setAttribute('third', snap3);",
)
# The early `return` already lowers to one `scf.if` that yields the result, so
# this case needs no nested return or completion cell.
EARLY = conditionals.METHOD.replace(
    conditionals.WRITE, "if (!seen) { seen = true; return true; } return false;"
)
CASES = {
    "original-helper-result": conditionals.METHOD.replace(
        conditionals.WRITE, "const chosen = seen ? true : false; if (chosen) { seen = true; }"
    ),
    "stateful-helper-result": METHOD,
    "repeated-results": REPEATED,
    "nested-results": REPEATED.replace(
        RESULT,
        "return seen ? (seen ? (seen = false, true) : false) : (seen = true, false);",
    ),
    "early-return": EARLY,
    "early-return-observed": EARLY.replace(
        "    mark();",
        "    const once = mark(), twice = mark();\n"
        "    target.setAttribute('first', once); target.setAttribute('second', twice);",
    ),
    "mixed-results": mixed.METHOD.replace(
        "const before = seen; seen = on; return before;",
        "return seen ? (seen = off, on) : (seen = on, off);",
    ),
}
# SHA-256 of the generated JavaScript for these four cases, pinned so an edit
# to the shared templates (captures, conditionals) cannot silently change what
# this test compiles.
SOURCE_HASHES = {
    "original-helper-result": "cbb816c38edf148a7d97ff50abfdb8e1500e6e36f96696f614351bc2be34fcd7",
    "stateful-helper-result": "4d1f45cef62a755065a9e379c69436ce2b87b0c17c4a22f9fbf5b9e49ecb24cd",
    "early-return": "8bfa015e220550281f774b48cda01c1c58d6e7372500c32fffe63fee9b17a30c",
    "early-return-observed": "a8173cae99c5f19ec46006da634e80a6bc3838b87792b0673b2ab4b844126ce2",
}
BAD = {
    "undefined-result": METHOD.replace("false, true", "false, void 0"),
    "number-result": METHOD.replace("false, true", "false, 0"),
    "string-result": METHOD.replace("false, true", "false, 'true'"),
    "computed-store": METHOD.replace("seen = false, true", "seen = !seen, true"),
    "effect-unselected-arm": METHOD.replace("seen = false, true", "seen = false, ambient(), true"),
    "effect-selected-arm": METHOD.replace("seen = true, false", "seen = true, ambient(), false"),
    "effect-after": METHOD.replace(
        "    return before !== after;", "    ambient(); return before !== after;"
    ),
    "early-tail-effect": EARLY.replace("} return false;", "} ambient(); return false;"),
    "early-nonboolean-tail": EARLY.replace("} return false;", "} return 0;"),
}


def observations(name):
    """Return the (attribute, value) writes one item.run() makes for case name."""
    result = []
    if name in ("repeated-results", "nested-results"):
        result = [("first", "false"), ("second", "true"), ("third", "false")]
    elif name == "early-return-observed":
        result = [("first", "true"), ("second", "false")]
    return result + [
        ("before", "true"),
        ("after", "true" if name == "original-helper-result" else "false"),
    ]


def shape(text, name="stateful-helper-result"):
    """Return (writer, branch): the writer closure and its result-yielding `scf.if`."""
    if name == "mixed-results":
        *_, leaves, _, _ = mixed.shape(text)
        writer = next(leaf for leaf in leaves if "ctjs.store_upvalue" in leaf[0])
    else:
        _, _, _, writer, _ = captures.shape(text)
    branch = re.search(
        r"^    (%\w+) = scf\.if (%\w+) -> \(!ctjs.value\) \{\n.*?^    \}$",
        writer[0],
        re.M | re.S,
    )
    assert branch, "source lost its original result-bearing helper conditional"
    assert len(re.findall(r"scf\.yield %\w+ : !ctjs.value", branch[0])) >= 2, branch[0]
    if name.startswith("early-return"):
        assert len(re.findall(r"ctjs\.store_upvalue", branch[0])) == 1, branch[0]
        assert len(re.findall(r"ctjs\.frame_exit", branch[0])) == 2, branch[0]
        assert len(re.findall(r"ctjs\.return", writer[0])) == 1, writer[0]
        assert f"    ctjs.return {branch[1]}\n" in writer[0], writer[0]
        assert not re.search(r"ctjs\.create_cell|scf\.index_switch", writer[0]), writer[0]
    elif name != "original-helper-result":
        assert len(re.findall(r"ctjs\.store_upvalue", branch[0])) >= 2, branch[0]
    return writer, branch


def result_roots(text):
    """captures.roots() plus a root of the branch result, as (label, valid, invalid)."""
    yield from captures.roots(text)
    writer, branch = shape(text)
    frame = re.search(r"(%\w+) = ctjs\.frame_enter \d+", writer[0])[1]
    exit_op = re.search(rf"^    ctjs\.frame_exit {re.escape(frame)}\b", writer[0], re.M)
    root = f"    ctjs.root {branch[1]} in {frame}"
    yield (
        "branch-result",
        captures.insert(text, writer, branch.end(), root),
        captures.insert(text, writer, exit_op.end(), root),
    )


def early_roots(text):
    """Yield (label, valid, invalid) roots placed before or after each arm's frame exit."""
    writer, branch = shape(text, "early-return")
    frame = re.search(r"(%\w+) = ctjs\.frame_enter \d+", writer[0])[1]
    callee = re.findall(r"(%\w+): !ctjs.value", writer[0].split("\n")[0])[2]
    exits = list(re.finditer(rf"^      ctjs\.frame_exit {re.escape(frame)}\b", branch[0], re.M))
    assert len(exits) == 2, branch[0]
    root = f"      ctjs.root {callee} in {frame}"
    for index, exit_op in enumerate(exits):
        yield (
            f"early-arm-{index}",
            captures.insert(text, writer, branch.start() + exit_op.start(), root),
            captures.insert(text, writer, branch.start() + exit_op.end(), root),
        )


def mutations(text):
    """Yield (label, mutated IR) pairs the lowering must refuse."""
    yield from captures.mutations(text)
    writer, branch = shape(text)
    first_yield = re.search(r"^      scf\.yield (%\w+) : !ctjs.value", branch[0], re.M)
    for label, attr in (
        ("undefined-yield", "#ctjs.undefined"),
        ("number-yield", "#ctjs.number<0>"),
        ("string-yield", '#ctjs.string<"bad">'),
    ):
        changed = branch[0].replace(
            first_yield[0],
            f"      %bad_yield = ctjs.constant {attr}\n      scf.yield %bad_yield : !ctjs.value",
            1,
        )
        yield (
            label,
            text[: writer.start() + branch.start()]
            + changed
            + text[writer.start() + branch.end() :],
        )
    callee = re.findall(r"(%\w+): !ctjs.value", writer[0].split("\n")[0])[2]
    changed = branch[0].replace(first_yield[0], f"      scf.yield {callee} : !ctjs.value", 1)
    yield (
        "callee-yield",
        text[: writer.start() + branch.start()] + changed + text[writer.start() + branch.end() :],
    )
    # An i1 result is valid IR but not a JS value; it must not count as a
    # Boolean result just because a ctjs.truthy produced it.
    changed = branch[0].replace("-> (!ctjs.value)", "-> (i1)")
    changed = re.sub(
        r"^      scf\.yield (%\w+) : !ctjs.value",
        lambda match: f"      %yield_bit = ctjs.truthy {match[1]}\n      scf.yield %yield_bit : i1",
        changed,
        flags=re.M,
    )
    body = writer[0][: branch.start()] + changed + writer[0][branch.end() :]
    original_read = re.search(r"(%\w+) = ctjs\.load_upvalue", writer[0])[1]
    body = body.replace(f"ctjs.return {branch[1]}", f"ctjs.return {original_read}")
    yield "non-value-result", text[: writer.start()] + body + text[writer.end() :]


def verifier_mutations(text):
    """Yield (label, IR) pairs with a wrong yield arity that the verifier rejects."""
    writer, branch = shape(text)
    first_yield = re.search(r"^      scf\.yield (%\w+) : !ctjs.value", branch[0], re.M)
    changed = branch[0].replace(first_yield[0], "      scf.yield", 1)
    yield (
        "missing-arm-result",
        text[: writer.start() + branch.start()] + changed + text[writer.start() + branch.end() :],
    )
    changed = branch[0].replace(
        first_yield[0],
        f"      scf.yield {first_yield[1]}, {first_yield[1]} : !ctjs.value, !ctjs.value",
        1,
    )
    yield (
        "extra-arm-result",
        text[: writer.start() + branch.start()] + changed + text[writer.start() + branch.end() :],
    )


def main():
    args = dom.parse_args(__doc__, oracles=True)
    includes, libraries = dom.link_options(args)
    executions = refusals = verifier_controls = 0
    for name, method in CASES.items():
        text = callbacks.source(method + captures.CALLS)
        if name in SOURCE_HASHES:
            assert hashlib.sha256(text.encode()).hexdigest() == SOURCE_HASHES[name], name
        observed = observations(name)
        expected = name != "original-helper-result"
        captures.oracle(args, name, text, observed=observed, expected=expected)
        ir, contract = dom.prepare(args, name, text, 2, entry_name="probe")
        shape(ir.read_text(), name)
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        for owned, optimize, manifest in dom.variants(declared):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            dom.standalone(
                args,
                native,
                label,
                captures.checks(name, observed=observed, expected=expected),
                args.compilers,
                includes,
                libraries,
            )
            executions += 4
        dom.refuse(args, ir, contract, name + "-undeclared")
        dom.exhaust(
            args, ir, declared, name, [0, 8], "{budget} did not exhaust Boolean result proof"
        )
        refusals += 4
        if name == "early-return":
            roots = early_roots(ir.read_text())
        elif name == "stateful-helper-result":
            refusals += budget_boundary(args, ir, declared, subject="Class Boolean result")
            roots = result_roots(ir.read_text())
        else:
            continue
        for label, valid, invalid in roots:
            path = args.work / f"rooted-{label}.mlir"
            path.write_text(valid)
            manifest = dict(declared, module_sha256=dom.fingerprint(args.opt, path))
            for owned, optimize, variant in dom.variants(manifest):
                dom.lower(
                    args, path, variant, f"rooted-{label}-{owned}-{optimize}", optimize=optimize
                )
            path.write_text(invalid)
            manifest = dict(declared, module_sha256=dom.fingerprint(args.opt, path))
            original = source_operations(args, invalid)
            for owned, optimize, variant in dom.variants(manifest):
                diagnostic = dom.lower(
                    args,
                    path,
                    variant,
                    f"late-root-{label}-{owned}-{optimize}",
                    optimize=optimize,
                    success=False,
                    max_steps=1000000,
                )
                assert source_operations(args, diagnostic) == original, diagnostic
                refusals += 1
        if name != "stateful-helper-result":
            continue
        for label, changed in mutations(ir.read_text()):
            assert changed != ir.read_text(), label
            path = args.work / f"mutated-{label}.mlir"
            path.write_text(changed)
            manifest = dict(declared, module_sha256=dom.fingerprint(args.opt, path))
            original = source_operations(args, changed)
            for owned, optimize, variant in dom.variants(manifest):
                diagnostic = dom.lower(
                    args,
                    path,
                    variant,
                    f"{label}-{owned}-{optimize}",
                    optimize=optimize,
                    success=False,
                    max_steps=1000000,
                )
                assert source_operations(args, diagnostic) == original, diagnostic
                refusals += 1
        for label, changed in verifier_mutations(ir.read_text()):
            path = args.work / f"verifier-{label}.mlir"
            path.write_text(changed)
            result = run([args.opt, str(path), "-o", "/dev/null"], success=False)
            assert "scf.yield" in result.stderr, result.stderr
            verifier_controls += 1
    for name, method in BAD.items():
        ir, contract = dom.prepare(
            args, name, callbacks.source(method + captures.CALLS), 2, entry_name="probe"
        )
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        original = source_operations(args, ir.read_text())
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
            assert source_operations(args, diagnostic) == original, diagnostic
            refusals += 1
    print(
        f"DOM class Boolean results: {len(CASES)} Node/VM sources, {executions} native executions, {refusals} refusals, {verifier_controls} verifier controls"
    )


if __name__ == "__main__":
    main()
