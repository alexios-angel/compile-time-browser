#!/usr/bin/env python3
"""Native lowering of a class method whose Boolean `let` is initialized inside
one if-arm and captured by two closures created in that arm.

Checks each CASES source against Node and the reference VM, runs the native
build, and checks that every BAD source and every hand-mutated IR variant is
refused.
"""

import json
import re
import subprocess

from CTNative.Browser.dom import dom
from CTNative.Browser.class_dom import callbacks
from CTNative.Browser.class_dom.boolean import captures
from CTNative.Browser.class_dom.parameter_defaults import budget_boundary
from CTNative.Browser.dom.boolean.cells import source_operations
from CTNative.harness import run

# `seen` is declared without a value and initialized to false only inside an
# if-arm. "arm-only-state" is the base case that must compile natively; the
# other CASES vary it, and BAD lists variants that must be refused.
METHOD = captures.METHOD.replace(
    "let seen = false;",
    "let seen; if (target.hasAttribute('ready')) { seen = false;",
).replace("return before !== after;", "} return true;")
OPEN = "if (target.hasAttribute('ready')) { seen = false;"
CASES = {
    "arm-only-state": METHOD,
    "else-state": METHOD.replace(OPEN, "if (target.hasAttribute('ready')) {} else { seen = false;"),
    "nested-scope": METHOD.replace(
        OPEN,
        "if (target.hasAttribute('ready')) { if (target.hasAttribute('other')) { seen = false;",
    ).replace("} return true;", "} } return true;"),
    "nested-stores": METHOD.replace("seen = false", "seen = !1")
    .replace("seen = true", "seen = !0")
    .replace("const after", "if (target.hasAttribute('other')) seen = !1; const after"),
    "snapshots": METHOD.replace(
        "const after",
        "const snapshot = pending(); seen = false;\n"
        "    target.setAttribute('snapshot', snapshot); const after",
    ),
    "effectful-initializer": METHOD.replace(
        "seen = false;", "seen = (target.setAttribute('init', 'yes'), false);"
    ),
}
BAD = {
    "read-before-init": METHOD.replace(
        "seen = false;", "target.setAttribute('early', seen); seen = false;"
    ),
    "capture-before-init": METHOD.replace("seen = false;", "").replace(
        "const mark", "seen = false; const mark"
    ),
    "post-join-read": METHOD.replace("} return true;", "} return seen;"),
    "post-join-write": METHOD.replace("} return true;", "} seen = false; return true;"),
    "sibling-read": METHOD.replace(
        "} return true;", "} else { target.setAttribute('early', seen); } return true;"
    ),
    "sibling-write": METHOD.replace("} return true;", "} else { seen = false; } return true;"),
    "split-creators": METHOD.replace("    const pending = () => !seen;\n", "").replace(
        OPEN, "const pending = () => !seen; " + OPEN
    ),
    "computed-initializer": METHOD.replace("seen = false;", "seen = target.hasAttribute('other');"),
    "nonboolean-initializer": METHOD.replace("seen = false;", "seen = 'false';"),
    "nonboolean-leaf-store": METHOD.replace("seen = true;", "seen = 'true';"),
    "escape": METHOD.replace("mark();", "target.saved = mark; mark();"),
    "hidden-writer": METHOD.replace(
        "const before", "const unused = () => { seen = false; }; const before"
    ),
    "second-cell": METHOD.replace(
        "const before",
        "let second = false; const change = () => { second = true; }; change(); const before",
    ),
    "loop-scope": METHOD.replace(
        "if (target.hasAttribute('ready'))", "while (target.hasAttribute('ready'))"
    ).replace("} return true;", "target.removeAttribute('ready'); } return true;"),
    "effect-before-init": METHOD.replace("seen = false;", "ambient(); seen = false;"),
    "effect-after-call": METHOD.replace("mark();", "mark(); ambient();"),
    "effect-else": METHOD.replace("} return true;", "} else { ambient(); } return true;"),
    "effect-unused-method": METHOD.replace(
        "class Item {", "class Item { unused() { ambient(); return true; }"
    ),
}


def events(name, ready, other):
    """Return the (attribute, value) writes one probe() call makes on an element
    whose `ready`/`other` attributes are present as given."""
    enabled = not ready if name == "else-state" else ready
    if name == "nested-scope":
        enabled = enabled and other
    if not enabled:
        return []
    result = [("init", "yes")] if name == "effectful-initializer" else []
    if name == "snapshots":
        result.append(("snapshot", "false"))
    after = name == "snapshots" or (name == "nested-stores" and other)
    return result + [("before", "true"), ("after", str(after).lower())]


def scenarios(name):
    """Yield (first_flags, second_flags, alias, (first_writes, second_writes)) for
    each attribute combination, aliased or not; see captures.checks() for counts."""
    for flags in range(4):
        first = (bool(flags & 1), bool(flags & 2))
        second = (not first[0], not first[1])
        for alias in (False, True):
            yield (
                first,
                second,
                alias,
                (
                    events(name, *first) * (3 if alias else 2),
                    events(name, *second) * (0 if alias else 1),
                ),
            )


def oracle(args, name, text):
    """Run every scenario in the VM and Node; both must report result=true."""
    text += "var result = true;\n"
    for first, second, alias, writes in scenarios(name):
        text += f"var first = {callbacks.ELEMENT}, second = {callbacks.ELEMENT};\n"
        for variable, flags in (("first", first), ("second", second)):
            for key, present in zip(("ready", "other"), flags):
                if present:
                    text += f"{variable}.attributes.{key} = '';\n"
        text += "result = probe(first, " + ("first" if alias else "second") + ") && result;\n"
        for variable, observed in zip(("first", "second"), writes):
            trace = "".join(key + "," for key, _ in observed)
            text += f"result = result && {variable}.trace === {json.dumps(trace)};\n"
            for key, value in dict(observed).items():
                text += f"result = result && {variable}.attributes.{key} === {json.dumps(value)};\n"
    path = args.work / f"{name}.oracle.js"
    path.write_text(text)
    assert run([args.reference, str(path)]).stdout == "result=true\n", name
    path.write_text(text + "console.log('result=' + result);\n")
    assert run([args.node, str(path)]).stdout == "result=true\n", name


def checks(name):
    """C++ assertions for the native harness, one block per scenario."""
    text = r"""
        (void)classes;
        (void)pressed;
        foreign_doc.log_writes(true);
        const auto flag = [&](document & owner, atom_table & table, node_id id,
                              const char * key, bool value) {
            if (value) { assert(owner.set_attribute(id, table.intern(key), "")); }
            else { (void)owner.remove_attribute(id, table.intern(key)); }
            (void)owner.take_writes();
        };
        const auto check = [&](document & owner, atom_table & table, node_id id,
                               std::initializer_list<const char *> names) {
            const auto writes = owner.take_writes();
            assert(writes.size() == names.size());
            size_t index = 0;
            for (const auto * name : names) {
                assert(writes[index].node == id && writes[index].name == table.intern(name));
                ++index;
            }
        };
    """
    for first, second, alias, writes in scenarios(name):
        owners = (("doc", "atoms", "button"), ("foreign_doc", "foreign_atoms", "other_button"))
        for (owner, table, node), flags in zip(owners, (first, second)):
            for key, present in zip(("ready", "other"), flags):
                text += f'flag({owner}, {table}, {node}, "{key}", {str(present).lower()});\n'
        text += "assert(@ENTRY@(element, " + ("alias" if alias else "foreign") + "));\n"
        for (owner, table, node), observed in zip(owners, writes):
            keys = ", ".join(json.dumps(key) for key, _ in observed)
            text += f"check({owner}, {table}, {node}, {{{keys}}});\n"
            for key, value in dict(observed).items():
                text += f"assert({owner}.read().attribute_value({node}, {table}.intern({json.dumps(key)})) == {json.dumps(value)});\n"
    return text


def shape(text):
    """Find the method whose two arm-local closures capture one cell.

    Returns (owner, cell, made, store, closures, leaves): the owner function,
    the cell, its create_cell and in-arm cell_set matches, the create_closure
    matches and the two callback bodies.
    """
    bodies = captures.functions(text)
    for owner in bodies:
        closures = list(
            re.finditer(
                r"^      +(%\w+) = ctjs\.create_closure (%\w+)\[(\d+)\] this (%\w+) captures (%\w+)$",
                owner[0],
                re.M,
            )
        )
        if len(closures) != 2:
            continue
        cell = closures[0][5]
        assert all(closure[5] == cell for closure in closures), owner[0]
        made = re.search(rf"^    {re.escape(cell)} = ctjs\.create_cell (%\w+)", owner[0], re.M)
        assert made and f"{made[1]} = ctjs.constant #ctjs.undefined" in owner[0]
        assert len(re.findall(r"ctjs\.create_cell", owner[0])) == 1
        assert not re.search(r"^    ctjs\.cell_(?:get|set)", owner[0], re.M)
        store = re.search(rf"^      +ctjs\.cell_set {re.escape(cell)}, (%\w+)", owner[0], re.M)
        assert store and all(store.end() < closure.start() for closure in closures)
        leaves = [
            next(
                body
                for body in bodies
                if re.search(rf"@[^ (]+\${closure[3]}\(", body[0].split("\n")[0])
            )
            for closure in closures
        ]
        assert all("upvalue_count = 1 : i32" in body[0] for body in leaves)
        return owner, cell, made, store, closures, leaves
    raise AssertionError("source lost exact initialized arm and two local capture slots")


def mutations(text):
    """Yield (label, mutated IR) pairs the lowering must refuse."""
    owner, cell, made, store, closures, leaves = shape(text)
    closure = closures[0]
    writer = next(body for body in leaves if "ctjs.store_upvalue" in body[0])
    name = re.search(r"@([^ (]+)", writer[0])[1]
    yield (
        "duplicate-creator",
        captures.insert(
            text, owner, closure.end(), closure[0].replace(closure[1], "%duplicate", 1)
        ),
    )
    exit_op = re.search(r"^    ctjs\.frame_exit %\w+", owner[0], re.M)
    yield (
        "unselected-caller-after-join",
        captures.insert(
            text,
            owner,
            exit_op.start(),
            f"    %extra = ctjs.call_direct @{name}({made[1]}, {made[1]}, {made[1]})",
        ),
    )
    yield (
        "symbol-observer",
        text.replace(
            "module attributes {", f"module attributes {{test.boolean_observer = @{name}, ", 1
        ),
    )
    yield (
        "stored-cell-identity",
        text[: owner.start() + store.start()]
        + store[0].replace(store[1], cell)
        + text[owner.start() + store.end() :],
    )
    yield (
        "read-before-initialization",
        captures.insert(text, owner, store.start(), f"      %early = ctjs.cell_get {cell}"),
    )
    yield (
        "read-after-join",
        captures.insert(text, owner, exit_op.start(), f"    %late = ctjs.cell_get {cell}"),
    )
    branch_else = re.search(r"^    \} else \{", owner[0], re.M)
    yield (
        "write-in-sibling",
        captures.insert(text, owner, branch_else.end(), f"      ctjs.cell_set {cell}, {made[1]}"),
    )
    # Keep the value producer, but move initialization after both captures.
    body = owner[0][: store.start()] + owner[0][store.end() :]
    body = body.replace(closures[-1][0], closures[-1][0] + "\n" + store[0])
    yield "capture-before-initialization", text[: owner.start()] + body + text[owner.end() :]
    yield (
        "escaped-callback",
        captures.insert(
            text, owner, closure.end(), f'      ctjs.store_global "escaped", {closure[1]}'
        ),
    )


def roots(text):
    """Yield (label, valid, invalid) IR with a `ctjs.root` inside or after a frame;
    invalid is None when no late placement is tested."""
    owner, cell, made, _, closures, leaves = shape(text)
    frame = re.search(r"(%\w+) = ctjs\.frame_enter \d+", owner[0])[1]
    exited = re.search(rf"^    ctjs\.frame_exit {re.escape(frame)}\b", owner[0], re.M)
    root = f"    ctjs.root {cell} in {frame}"
    yield (
        "cell-before-scope",
        captures.insert(text, owner, made.end(), root),
        captures.insert(text, owner, exited.end(), root),
    )
    yield "cell-after-join", captures.insert(text, owner, exited.start(), root), None
    for closure, leaf in zip(closures, leaves):
        yield (
            "callback-" + closure[3],
            captures.insert(text, owner, closure.end(), f"      ctjs.root {closure[1]} in {frame}"),
            None,
        )
        entered = re.search(r"(%\w+) = ctjs\.frame_enter \d+", leaf[0])
        exited = re.search(rf"^    ctjs\.frame_exit {re.escape(entered[1])}\b", leaf[0], re.M)
        callee = re.findall(r"(%\w+): !ctjs.value", leaf[0].split("\n")[0])[2]
        root = f"    ctjs.root {callee} in {entered[1]}"
        yield (
            "callee-" + closure[3],
            captures.insert(text, leaf, entered.end(), root),
            captures.insert(text, leaf, exited.end(), root),
        )


def main(argv=None):
    args = dom.parse_args(__doc__, oracles=True, argv=argv)
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for name, method in CASES.items():
        text = callbacks.source(method + captures.CALLS)
        oracle(args, name, text)
        ir, contract = dom.prepare(args, name, text, 2, entry_name="probe")
        shape(ir.read_text())
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        for owned, optimize, manifest in dom.variants(declared):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            dom.standalone(args, native, label, checks(name), args.compilers, includes, libraries)
            executions += 4
        dom.refuse(args, ir, contract, name + "-undeclared")
        dom.exhaust(args, ir, declared, name, [0, 8], "{budget} did not exhaust arm-state proof")
        refusals += 4
        if name != "arm-only-state":
            continue
        refusals += budget_boundary(args, ir, declared, subject="Class Boolean arm state")
        for label, changed in mutations(ir.read_text()):
            assert changed != ir.read_text(), label
            path = args.work / f"mutated-{label}.mlir"
            path.write_text(changed)
            manifest = dict(declared, module_sha256=dom.fingerprint(args.opt, path))
            dom.refuse(args, path, manifest, label)
            refusals += 2
        for label, valid, invalid in roots(ir.read_text()):
            path = args.work / f"rooted-{label}.mlir"
            path.write_text(valid)
            manifest = dict(declared, module_sha256=dom.fingerprint(args.opt, path))
            for owned, optimize, variant in dom.variants(manifest):
                output = dom.lower(
                    args, path, variant, f"rooted-{label}-{owned}-{optimize}", optimize=optimize
                ).read_text()
                assert dom.NATIVE.search(output) and not dom.FUNCTION.search(output), output
            if invalid:
                path.write_text(invalid)
                manifest = dict(declared, module_sha256=dom.fingerprint(args.opt, path))
                dom.refuse(args, path, manifest, "late-root-" + label)
                refusals += 2
        owner, _, _, _, _, leaves = shape(ir.read_text())
        leaf = leaves[0]
        entered = re.search(r"^    (%\w+) = ctjs\.frame_enter \d+", leaf[0], re.M)
        callee = re.findall(r"(%\w+): !ctjs.value", leaf[0].split("\n")[0])[2]
        # A root before its context producer is rejected by SSA verification.
        invalid = captures.insert(
            ir.read_text(), leaf, entered.start(), f"    ctjs.root {callee} in {entered[1]}"
        )
        path = args.work / "verifier-root-before-frame.mlir"
        path.write_text(invalid)
        actual = subprocess.run([args.opt, str(path)], text=True, capture_output=True, timeout=30)
        assert actual.returncode == 1 and "error:" in actual.stderr, actual.stderr
        refusals += 1
    for name, method in BAD.items():
        ir, contract = dom.prepare(
            args, name, callbacks.source(method + captures.CALLS), 2, entry_name="probe"
        )
        if name == "loop-scope":
            assert "scf.while" in ir.read_text()
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        original = source_operations(args, ir.read_text()) if name.startswith("effect-") else None
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
            if original is not None:
                assert source_operations(args, diagnostic) == original, diagnostic
            refusals += 1
    print(
        f"DOM class Boolean arm state: {len(CASES)} Node/VM sources, {executions} native executions, {refusals} refusals"
    )


def test_arm_state(lit):
    main(lit.args("%dom_driver --node %node --reference %native_reference"))


if __name__ == "__main__":
    main()
