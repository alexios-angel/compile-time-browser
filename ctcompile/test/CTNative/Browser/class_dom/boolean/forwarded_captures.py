#!/usr/bin/env python3
"""Check that a callback capturing both a constant Boolean from an outer scope
(forwarded through the class method's closure) and the method's own mutable
Boolean cell compiles natively and matches Node and the VM, and that variants
where the outer value can change, plus hand-mutated IR, are refused."""

import hashlib
import re

from CTNative.Browser.dom import dom
from CTNative.Browser.class_dom import callbacks
from CTNative.Browser.class_dom.boolean import captures
from CTNative.Browser.class_dom.parameter_defaults import budget_boundary
from CTNative.Browser.dom.boolean.cells import source_operations
from CTNative.harness import run

# The reader closure captures both the outer constant `off` (forwarded from the
# enclosing frame) and the mutable `seen` cell. SOURCE_HASHES pins the generated
# source so main()'s expected enclosing_indices ([0, -1] for the reader) stay valid.
METHOD = "const off = false;\n" + captures.METHOD.replace("() => !seen", "() => off === seen")
CASES = {
    "forwarded": METHOD,
    "reordered": METHOD.replace("off === seen", "seen === off"),
    # Both callbacks capture `off`, at different capture positions.
    "shared-forwarded": METHOD.replace("seen = true;", "if (seen === off) { seen = true; }"),
    "multiple-fixed": METHOD.replace("const off = false;", "const off = false, on = true;")
    .replace("off === seen", "off === seen === on")
    .replace("seen = true;", "seen = on;"),
    "reinitialized": METHOD.replace("    mark();", "    mark(); seen = false; mark(); mark();"),
}
SOURCE_HASHES = {
    "forwarded": "90d5173b81686931d5c41656c6024956bfde265474cb7f7e1985da914d8df182",
}
BAD = {
    "forwarded-writer": METHOD.replace("const off", "let off").replace(
        "seen = true;", "seen = true; off = true;"
    ),
    "dormant-writer": METHOD.replace(
        "const off = false;",
        "let off = false; const unused = () => { off = true; };",
    ),
    "deep-dormant-writer": METHOD.replace(
        "const off = false;",
        "let off = false; const unused = () => { const middle = () => {"
        " const nested = () => { off = true; }; }; };",
    ),
    "reassigned-fixed": METHOD.replace("const off", "let off") + "\noff = true;\n",
    "early-fixed": METHOD.replace("const off = false;", "let off;") + "\noff = false;\n",
    "early-state": METHOD.replace("let seen = false;", "let seen;").replace(
        "    const before = pending();", "    seen = false; const before = pending();"
    ),
    "forwarded-state": METHOD.replace(
        "const off = false;", "const off = false; let seen = false;"
    ).replace("    let seen = false;\n", ""),
    "number-fixed": METHOD.replace("off = false", "off = 0"),
    "string-fixed": METHOD.replace("off = false", "off = 'false'"),
    "effect-after": METHOD.replace(
        "    return before !== after;", "    ambient(); return before !== after;"
    ),
}

# A `ctjs.create_closure` line. enclosing_indices gives, per capture slot, the
# parent closure's slot it forwards (>= 0) or -1 for a value created locally.
CLOSURE = (
    r"^    (%\w+) = ctjs\.create_closure (%\w+)\[(\d+)\] this (%\w+) captures ([^{\n]+?)"
    r"(?: \{enclosing_indices = array<i32: ([^>]+)>\})?$"
)


def shape(text):
    """Find the method that creates the two callbacks and check their slots.

    Returns (owner, cell, closures, leaves, slots, indices, enclosing): the
    owner function, its mutable cell, the create_closure matches, the callback
    bodies, each callback's captured operands, the cell's slot in each, and
    each callback's enclosing_indices.
    """
    bodies = captures.functions(text)
    for owner in bodies:
        closures = list(re.finditer(CLOSURE, owner[0], re.M))
        if len(closures) != 2 or not any(closure[6] for closure in closures):
            continue
        slots = [re.findall(r"%\w+", closure[5]) for closure in closures]
        enclosing = [
            [int(index) for index in closure[6].split(",")] if closure[6] else [-1] * len(slot)
            for closure, slot in zip(closures, slots)
        ]
        leaves = [
            next(
                body
                for body in bodies
                if re.search(rf"@[^ (]+\${closure[3]}\(", body[0].split("\n")[0])
            )
            for closure in closures
        ]
        written = {
            slot[int(index)]
            for slot, leaf in zip(slots, leaves)
            for index in re.findall(r"ctjs\.store_upvalue %\w+\[(\d+)\]", leaf[0])
        }
        assert len(written) == 1, written
        cell = written.pop()
        indices = [slot.index(cell) for slot in slots]
        assert re.search(rf"^    {re.escape(cell)} = ctjs\.create_cell %\w+", owner[0], re.M)
        for slot, outer, mutable, leaf in zip(slots, enclosing, indices, leaves):
            assert len(outer) == len(slot) and outer[mutable] == -1, (slot, outer)
            assert f"upvalue_count = {len(slot)} : i32" in leaf[0], leaf[0]
            assert all(
                int(index) < len(slot)
                for index in re.findall(r"(?:load|store)_upvalue %\w+\[(\d+)\]", leaf[0])
            )
            for index, value in enumerate(slot):
                if index == mutable:
                    continue
                assert outer[index] >= 0, outer
                assert re.search(
                    rf"^    {re.escape(value)} = ctjs\.constant #ctjs.undefined$", owner[0], re.M
                ), owner[0]
        return owner, cell, closures, leaves, slots, indices, enclosing
    raise AssertionError("source lost forwarded fixed and local mutable capture slots")


def parent_capture(text, owner):
    """Return (parent function, create_closure match) that creates owner."""
    index = re.search(r"@[^ (]+\$(\d+)\(", owner[0])[1]
    matches = [
        (parent, closure)
        for parent in captures.functions(text)
        for closure in re.finditer(CLOSURE, parent[0], re.M)
        if closure[3] == index
    ]
    assert len(matches) == 1, matches
    return matches[0]


def replace(text, body, operation, changed):
    """Replace match operation inside function match body with changed."""
    return (
        text[: body.start() + operation.start()] + changed + text[body.start() + operation.end() :]
    )


def roots(text):
    """Yield (label, valid, invalid) IR with a `ctjs.root` placed inside or after
    the frame of the cell, each fixed outer value, each closure and callee."""
    owner, cell, closures, leaves, _, _, _ = shape(text)
    parent, creator = parent_capture(text, owner)
    fixed = re.findall(r"%\w+", creator[5])
    selected = []
    for label, body, value in [("state", owner, cell)] + [
        (f"outer-fixed-{index}", parent, value) for index, value in enumerate(fixed)
    ]:
        store = re.search(rf"^    ctjs\.cell_set {re.escape(value)}, [^\n]+", body[0], re.M)
        assert store, body[0]
        selected.append((label, body, value, store))
    for index, (closure, leaf) in enumerate(zip(closures, leaves)):
        selected.append((f"closure-{index}", owner, closure[1], closure))
        callee = re.findall(r"(%\w+): !ctjs.value", leaf[0].split("\n")[0])[2]
        frame = re.search(r"^    %\w+ = ctjs\.frame_enter \d+", leaf[0], re.M)
        selected.append((f"callee-{index}", leaf, callee, frame))
    read = re.search(r"^    (%\w+) = ctjs\.load_upvalue %\w+\[0\]", leaves[0][0], re.M)
    selected.append(("fixed-read", leaves[0], read[1], read))
    for label, body, value, after in selected:
        frame = re.search(r"(%\w+) = ctjs\.frame_enter \d+", body[0])[1]
        exited = re.search(rf"^    ctjs\.frame_exit {re.escape(frame)}\b", body[0], re.M)
        root = f"    ctjs.root {value} in {frame}"
        yield (
            label,
            captures.insert(text, body, after.end(), root),
            captures.insert(text, body, exited.end(), root),
        )


def mutations(text):
    """Yield (label, mutated IR) pairs the lowering must refuse."""
    owner, cell, closures, leaves, slots, indices, enclosing = shape(text)
    closure, reader, writer = closures[0], leaves[0], leaves[1]
    parent, creator = parent_capture(text, owner)
    for label, body, made in (("leaf", owner, closure), ("parent", parent, creator)):
        yield (
            f"duplicate-{label}-creator",
            captures.insert(text, body, made.end(), made[0].replace(made[1], "%duplicate", 1)),
        )
        initial = re.search(r"(%\w+) = ctjs\.constant #ctjs.undefined", body[0])[1]
        yield (
            f"wrong-{label}-enclosing",
            replace(text, body, made, made[0].replace(made[2] + "[", initial + "[", 1)),
        )
    fixed_slot = next(index for index, outer in enumerate(enclosing[0]) if outer >= 0)
    count = len(slots[0])
    for label, outer in (("out-of-range-forward", 1000), ("missing-forward", -1)):
        changed = list(enclosing[0])
        changed[fixed_slot] = outer
        yield (
            label,
            replace(
                text, owner, closure, closure[0].replace(closure[6], ", ".join(map(str, changed)))
            ),
        )
    duplicate = list(slots[0])
    duplicate[fixed_slot] = cell
    local = [-1] * count
    changed = (
        closure[0]
        .replace(closure[5], ", ".join(duplicate))
        .replace(closure[6], ", ".join(map(str, local)))
    )
    yield "duplicate-mutable-slot", replace(text, owner, closure, changed)
    for label, body, original_count in (("leaf", reader, count), ("parent", owner, 1)):
        yield (
            f"{label}-upvalue-count",
            text[: body.start()]
            + body[0].replace(
                f"upvalue_count = {original_count} : i32",
                f"upvalue_count = {original_count + 1} : i32",
            )
            + text[body.end() :],
        )
    loaded = re.search(r"^    (%\w+) = ctjs\.load_upvalue (%\w+)\[(\d+)\]", reader[0], re.M)
    exited = re.search(r"^    ctjs\.frame_exit %\w+", reader[0], re.M)
    for label, index in (("unused-negative-load", -1), ("unused-invalid-load", count)):
        yield (
            label,
            captures.insert(
                text,
                reader,
                exited.start(),
                f"    %unused = ctjs.load_upvalue {loaded[2]}[{index}]",
            ),
        )
    yield (
        "wrong-read-callee",
        replace(text, reader, loaded, loaded[0].replace(loaded[2] + "[", "%arg0[", 1)),
    )
    yield (
        "hidden-fixed-store",
        captures.insert(
            text,
            reader,
            exited.start(),
            f"    ctjs.store_upvalue {loaded[2]}[{fixed_slot}], {loaded[1]}",
        ),
    )
    name = re.search(r"@([^ (]+)", writer[0])[1]
    initial = re.search(rf"^    {re.escape(cell)} = ctjs\.create_cell (%\w+)", owner[0], re.M)[1]
    owner_exit = re.search(r"^    ctjs\.frame_exit %\w+", owner[0], re.M)
    yield (
        "unselected-caller",
        captures.insert(
            text,
            owner,
            owner_exit.start(),
            f"    %unselected = ctjs.call_direct @{name}({initial}, {initial}, {initial})",
        ),
    )
    yield (
        "symbol-observer",
        text.replace(
            "module attributes {", f"module attributes {{test.forwarded_observer = @{name}, ", 1
        ),
    )


def verifier_mutations(text):
    """Yield (label, IR) pairs with bad enclosing_indices that the verifier rejects."""
    owner, cell, closures, _, _, _, _ = shape(text)
    closure = closures[0]
    for label, indices in (
        ("negative-fixed-sentinel", "-2, -1"),
        ("negative-mutable-sentinel", "0, -2"),
        ("short-enclosing", "0"),
        ("forwarded-mutable-cell", "0, 0"),
    ):
        yield label, replace(text, owner, closure, closure[0].replace(closure[6], indices))
    for label, value in (("cell-placeholder", cell), ("argument-placeholder", "%arg3")):
        operands = re.findall(r"%\w+", closure[5])
        operands[0] = value
        yield (
            label,
            replace(text, owner, closure, closure[0].replace(closure[5], ", ".join(operands))),
        )


def main(argv=None):
    args = dom.parse_args(__doc__, oracles=True, argv=argv)
    includes, libraries = dom.link_options(args)
    executions = refusals = verifier_controls = 0
    for name, method in CASES.items():
        text = callbacks.source(method + captures.CALLS)
        if name in SOURCE_HASHES:
            assert hashlib.sha256(text.encode()).hexdigest() == SOURCE_HASHES[name], name
        captures.oracle(args, name, text)
        ir, contract = dom.prepare(args, name, text, 2, entry_name="probe")
        *_, indices, enclosing = shape(ir.read_text())
        if name == "forwarded":
            assert indices == [1, 0] and enclosing == [[0, -1], [-1]], (indices, enclosing)
        if name == "reordered":
            assert indices == [0, 0] and enclosing == [[-1, 0], [-1]], (indices, enclosing)
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        for owned, optimize, manifest in dom.variants(declared):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            dom.standalone(
                args, native, label, captures.checks(name), args.compilers, includes, libraries
            )
            executions += 4
        dom.refuse(args, ir, contract, name + "-undeclared")
        dom.exhaust(
            args, ir, declared, name, [0, 8], "{budget} did not exhaust forwarded-capture proof"
        )
        refusals += 4
        if name != "forwarded":
            continue
        refusals += budget_boundary(args, ir, declared, subject="Forwarded Boolean captures")
        for label, valid, invalid in roots(ir.read_text()):
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
            assert result.returncode and "enclosing_indices" in result.stderr, result.stderr
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
        f"DOM class forwarded Boolean captures: {len(CASES)} Node/VM sources, "
        f"{executions} native executions, {refusals} refusals, {verifier_controls} verifier controls"
    )


def test_forwarded_captures(lit):
    main(lit.args("%dom_driver --node %node --reference %native_reference"))


if __name__ == "__main__":
    main()
