#!/usr/bin/env python3
"""Check that two callbacks sharing one mutable Boolean cell and also capturing
fixed Boolean constants compile natively and match Node and the VM, and that
variants where a fixed capture can change, plus hand-mutated IR, are refused."""

import hashlib
import re

from CTNative.Browser.dom import dom
from CTNative.Browser.class_dom import callbacks
from CTNative.Browser.class_dom.boolean import captures
from CTNative.Browser.class_dom.boolean import conditionals
from CTNative.Browser.class_dom.parameter_defaults import budget_boundary
from CTNative.Browser.dom.boolean.cells import source_operations
from CTNative.harness import run

# Base program: two callbacks capture one mutable Boolean cell (`seen`) and
# fixed Boolean constants (`off`, `on`). In the lowered IR the mutable cell is
# at upvalue index 1 in the reader and 0 in the writer; main() asserts that.
METHOD = (
    captures.METHOD.replace(
        "let seen = false;", "const off = false, on = true;\n    let seen = false;"
    )
    .replace("const pending = () => !seen;", "const pending = () => off === seen;")
    .replace(
        "const mark = () => { seen = true; };",
        "const mark = () => { const before = seen; seen = on; return before; };",
    )
)
CASES = {
    "mixed-slots": METHOD,
    "swapped-slots": METHOD.replace("off === seen", "seen === off").replace(
        "const before = seen; seen = on;",
        "const desired = on; const before = seen; seen = desired;",
    ),
    "repeated-reset": METHOD.replace("    mark();", "    mark(); seen = false; mark(); mark();"),
    "conditional-leaf": METHOD.replace("seen = on;", "if (on) { seen = on; }"),
    # The same fixed cell sits at different capture slots in the two callbacks.
    "three-slots": METHOD.replace("off === seen", "off === seen === on"),
    # Programs from captures.py and conditionals.py with one extra fixed capture;
    # SOURCE_HASHES pins their exact text so an edit upstream is noticed.
    "extra-reader": captures.METHOD.replace(
        "const pending", "const flag = true; const pending"
    ).replace("() => !seen", "() => seen === flag"),
    "extra-condition": conditionals.METHOD.replace(
        "const pending", "const flag = true; const pending"
    ).replace("if (!seen)", "if (seen === flag)"),
}
# SHA-256 of the full generated source for these cases. A change to an imported
# METHOD or to callbacks.source() fails here instead of silently testing a
# different program.
SOURCE_HASHES = {
    "mixed-slots": "3d7d43ec563abf14d1a857681cacd3c6b72658ac423da145d07f47bb3fe4535d",
    "extra-reader": "b9a02865ea1a724385fca786e4e119323f905d458963ddfeac214dd0fd0be313",
    "extra-condition": "0012fb20616a7ab0de4a40cb05ca3af77c0bfc30a062547f27916c451822334a",
}
# Programs the native lowering must refuse, leaving the source operations
# unchanged: a writer to a fixed capture, a capture read before initialization,
# a non-Boolean or computed value, an escaping callback, or a side effect.
BAD = {
    "second-mutable": METHOD.replace(
        "    const before = pending();",
        "    const change = () => { off = true; }; change(); const before = pending();",
    ),
    "dormant-writer": METHOD.replace(
        "    const before = pending();",
        "    const unused = () => { off = true; }; const before = pending();",
    ),
    "forwarded-dormant-writer": METHOD.replace(
        "    const before = pending();",
        "    const unused = () => { const nested = () => { off = true; }; };\n"
        "    const before = pending();",
    ),
    "fixed-owner-write": METHOD.replace("    mark();", "    off = true; mark();"),
    "early-fixed-capture": METHOD.replace(
        "const off = false, on = true;", "let off; const on = true;"
    ).replace("    const before = pending();", "    off = false; const before = pending();"),
    "early-state-capture": METHOD.replace("let seen = false;", "let seen;").replace(
        "    const before = pending();", "    seen = false; const before = pending();"
    ),
    "string-fixed": METHOD.replace("off = false", "off = 'false'"),
    "numeric-fixed": METHOD.replace("off = false", "off = 0"),
    "computed-fixed": METHOD.replace("on = true", "on = target.hasAttribute('ready')"),
    "computed-store": METHOD.replace("seen = on;", "seen = !seen;"),
    "escaped-callback": METHOD.replace("    mark();", "    target.saved = mark; mark();"),
    "deferred-callback": METHOD.replace("    mark();", "    setTimeout(mark, 0);"),
    "effect-leaf": METHOD.replace("seen = on;", "seen = on; ambient();"),
    "effect-dormant-arm": METHOD.replace(
        "seen = on;", "if (on) { seen = on; } else { ambient(); }"
    ),
    "effect-after": METHOD.replace(
        "    return before !== after;", "    ambient(); return before !== after;"
    ),
}


def observations(name):
    """Return the (attribute, value) writes one item.run() makes for case name."""
    if name == "extra-reader":
        return [("before", "false"), ("after", "true")]
    return [("before", "true"), ("after", "true" if name == "extra-condition" else "false")]


def shape(text):
    """Find the method creating two callbacks with more than one capture.

    Returns (owner, cell, closures, leaves, slots, indices): the owner function,
    the one cell a callback writes, the create_closure matches, the callback
    bodies, each callback's captured operands and the cell's slot in each.
    """
    bodies = captures.functions(text)
    for owner in bodies:
        closures = list(
            re.finditer(
                r"^    (%\w+) = ctjs\.create_closure (%\w+)\[(\d+)\] this (%\w+) captures (.+)$",
                owner[0],
                re.M,
            )
        )
        if len(closures) != 2:
            continue
        slots = [re.findall(r"%\w+", closure[5]) for closure in closures]
        if not any(len(slot) > 1 for slot in slots):
            continue
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
        for slot, leaf in zip(slots, leaves):
            assert f"upvalue_count = {len(slot)} : i32" in leaf[0], leaf[0]
            assert len(set(slot)) == len(slot), slot
            assert all(
                int(index) < len(slot)
                for index in re.findall(r"(?:load|store)_upvalue %\w+\[(\d+)\]", leaf[0])
            )
        for value in set(sum(slots, [])):
            assert re.search(rf"^    {re.escape(value)} = ctjs\.create_cell %\w+", owner[0], re.M)
        return owner, cell, closures, leaves, slots, indices
    raise AssertionError("source lost its shared mutable and fixed Boolean capture slots")


def roots(text):
    """Yield (label, valid, invalid) IR with a `ctjs.root` placed inside or after
    the frame of each captured cell, closure and callee."""
    owner, cell, closures, leaves, slots, _ = shape(text)
    selected = []
    for value in sorted(set(sum(slots, []))):
        store = re.search(rf"^    ctjs\.cell_set {re.escape(value)}, [^\n]+", owner[0], re.M)
        selected.append(("state" if value == cell else "fixed-" + value[1:], owner, value, store))
    for index, (closure, leaf) in enumerate(zip(closures, leaves)):
        selected.append((f"closure-{index}", owner, closure[1], closure))
        callee = re.findall(r"(%\w+): !ctjs.value", leaf[0].split("\n")[0])[2]
        frame = re.search(r"^    %\w+ = ctjs\.frame_enter \d+", leaf[0], re.M)
        selected.append((f"callee-{index}", leaf, callee, frame))
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
    owner, cell, closures, leaves, slots, indices = shape(text)
    closure, reader, writer = closures[0], leaves[0], leaves[1]
    yield (
        "duplicate-creator",
        captures.insert(
            text, owner, closure.end(), closure[0].replace(closure[1], "%duplicate", 1)
        ),
    )
    initial = re.search(rf"^    {re.escape(cell)} = ctjs\.create_cell (%\w+)", owner[0], re.M)[1]
    name = re.search(r"@([^ (]+)", writer[0])[1]
    exited = re.search(r"^    ctjs\.frame_exit %\w+", owner[0], re.M)
    yield (
        "unselected-caller",
        captures.insert(
            text,
            owner,
            exited.start(),
            f"    %unselected = ctjs.call_direct @{name}({initial}, {initial}, {initial})",
        ),
    )
    yield (
        "symbol-observer",
        text.replace(
            "module attributes {", f"module attributes {{test.mixed_observer = @{name}, ", 1
        ),
    )
    fixed_slot = next(index for index, value in enumerate(slots[0]) if value != cell)
    duplicate = list(slots[0])
    duplicate[fixed_slot] = cell
    changed = closure[0].replace("captures " + closure[5], "captures " + ", ".join(duplicate))
    yield (
        "duplicate-mutable-slot",
        text[: owner.start() + closure.start()] + changed + text[owner.start() + closure.end() :],
    )
    load = re.search(r"ctjs\.load_upvalue (%\w+)\[(\d+)\]", reader[0])
    for label, index in (("out-of-range-slot", len(slots[0])), ("negative-slot", -1)):
        changed = load[0].replace(f"[{load[2]}]", f"[{index}]")
        yield (
            label,
            text[: reader.start() + load.start()] + changed + text[reader.start() + load.end() :],
        )
    store = re.search(r"ctjs\.store_upvalue (%\w+)\[(\d+)\], (%\w+)", writer[0])
    yield (
        "callee-as-value",
        text[: writer.start() + store.start()]
        + store[0].replace(", " + store[3], ", " + store[1])
        + text[writer.start() + store.end() :],
    )
    yield (
        "wrong-enclosing",
        text[: owner.start() + closure.start()]
        + closure[0].replace(closure[2] + "[", initial + "[", 1)
        + text[owner.start() + closure.end() :],
    )
    # Add a store to the writer's fixed slot 1. Replacing reads of fixed slots
    # with their constant must not hide this write; it must be refused.
    extra = f"    ctjs.store_upvalue {store[1]}[1], {store[3]}"
    writer_exit = re.search(r"^    ctjs\.frame_exit %\w+", writer[0], re.M)
    yield "additional-fixed-writer", captures.insert(text, writer, writer_exit.start(), extra)
    yield (
        "upvalue-count",
        text[: reader.start()]
        + reader[0].replace("upvalue_count = 2 : i32", "upvalue_count = 3 : i32")
        + text[reader.end() :],
    )
    # Initialize the mutable cell from a formal parameter instead of a literal.
    # Fixed-cell discovery also normalizes parameters, and must refuse this
    # before that normalization could replace the parameter.
    made = re.search(rf"^    {re.escape(cell)} = ctjs\.create_cell %\w+", owner[0], re.M)
    yield (
        "formal-initial",
        text[: owner.start() + made.start()]
        + f"    {cell} = ctjs.create_cell %arg3"
        + text[owner.start() + made.end() :],
    )


def verifier_mutations(text):
    """Yield (label, IR) pairs with bad enclosing_indices that the verifier rejects."""
    owner, _, closures, _, _, _ = shape(text)
    closure = closures[0]
    for label, indices in (
        ("negative-enclosing", "-2, -1"),
        ("short-enclosing", "-1"),
        ("swapped-enclosing", "-1, 0"),
    ):
        changed = closure[0] + " {enclosing_indices = array<i32: " + indices + ">}"
        yield (
            label,
            text[: owner.start() + closure.start()]
            + changed
            + text[owner.start() + closure.end() :],
        )


def main(argv=None):
    args = dom.parse_args(__doc__, oracles=True, argv=argv)
    includes, libraries = dom.link_options(args)
    executions = refusals = verifier_controls = 0
    for name, method in CASES.items():
        text = callbacks.source(method + captures.CALLS)
        if name in SOURCE_HASHES:
            assert hashlib.sha256(text.encode()).hexdigest() == SOURCE_HASHES[name], name
        expected = name != "extra-condition"
        observed = observations(name)
        captures.oracle(args, name, text, observed=observed, expected=expected)
        ir, contract = dom.prepare(args, name, text, 2, entry_name="probe")
        *_, indices = shape(ir.read_text())
        if name in ("mixed-slots", "swapped-slots"):
            assert indices == ([1, 0] if name == "mixed-slots" else [0, 1]), indices
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
            args, ir, declared, name, [0, 8], "{budget} did not exhaust mixed-capture proof"
        )
        refusals += 4
        if name != "mixed-slots":
            continue
        refusals += budget_boundary(args, ir, declared, subject="Mixed Boolean captures")
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
        f"DOM mixed Boolean captures: {len(CASES)} Node/VM sources, {executions} native executions, {refusals} refusals, {verifier_controls} verifier controls"
    )


def test_mixed_captures(lit):
    main(lit.args("%dom_driver --node %node --reference %native_reference"))


if __name__ == "__main__":
    main()
