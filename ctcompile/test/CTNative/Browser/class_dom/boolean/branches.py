#!/usr/bin/env python3
"""Check that captured Boolean state in a class method stays correct when the
callback call or the store sits inside a bounded `if`, natively and against Node
and the VM, and that unsupported variants and hand-mutated IR are refused."""

import json
import re

from CTNative.Browser.dom import dom
from CTNative.Browser.class_dom import callbacks
from CTNative.Browser.class_dom.boolean import captures
from CTNative.Browser.class_dom.parameter_defaults import budget_boundary
from CTNative.Browser.dom.boolean.cells import source_operations
from CTNative.harness import run

CASES = {
    # Each case is captures.METHOD with the `mark()` call or a `seen` store moved
    # inside an `if`, to check the cell proof across bounded branches.
    "conditional-call": captures.METHOD.replace(
        "mark();", "if (target.hasAttribute('ready')) mark();"
    ),
    "conditional-store": captures.METHOD.replace(
        "const after", "if (target.hasAttribute('ready')) seen = false; const after"
    ),
    "both-arms": captures.METHOD.replace(
        "mark();",
        "if (target.hasAttribute('ready')) {\n"
        "      mark(); target.setAttribute('branch', pending());\n"
        "    } else {\n"
        "      seen = false; target.setAttribute('branch', pending());\n    }",
    ),
    "nested": captures.METHOD.replace(
        "mark();",
        "if (target.hasAttribute('ready')) {\n"
        "      mark(); if (target.hasAttribute('other')) seen = false;\n"
        "      target.setAttribute('branch', pending());\n    }",
    ),
    "snapshots": captures.METHOD.replace(
        "mark();",
        "if (target.hasAttribute('ready')) {\n"
        "      mark(); const snapshot = pending(); seen = false;\n"
        "      target.setAttribute('snapshot', snapshot);\n"
        "      target.setAttribute('owner', seen);\n"
        "    } else {\n"
        "      const snapshot = seen; mark();\n"
        "      target.setAttribute('snapshot', snapshot);\n"
        "      target.setAttribute('owner', seen);\n    }",
    ),
}
BASE = CASES["conditional-call"]
BAD = {
    "initialization-in-arm": BASE.replace(
        "let seen = false;", "let seen; if (target.hasAttribute('ready')) seen = false;"
    ),
    "initialization-both-arms": BASE.replace(
        "let seen = false;",
        "let seen; if (target.hasAttribute('ready')) seen = false; else seen = true;",
    ),
    "initialization-after-capture": BASE.replace("let seen = false;", "let seen;").replace(
        "const before", "seen = false; const before"
    ),
    # A callback created inside the branch is accepted; that case lives in
    # scopes.py as "branch-local-creator".
    "escaping-callback": BASE.replace("mark();", "{ target.saved = mark; mark(); }"),
    "argument": BASE.replace("mark();", "mark(target);"),
    "nonboolean-store": BASE.replace("mark();", "{ mark(); seen = target; }"),
    "computed-store": BASE.replace("mark();", "{ mark(); seen = !seen; }"),
    "loop-ancestor": BASE.replace(
        "if (target.hasAttribute('ready')) mark();",
        "while (target.hasAttribute('ready')) {\n"
        "      if (target.hasAttribute('other')) mark(); target.removeAttribute('ready');\n    }",
    ),
    "effect-before-call": BASE.replace("mark();", "{ ambient(); mark(); }"),
    "effect-after-call": BASE.replace("mark();", "{ mark(); ambient(); }"),
    "effect-else": BASE.replace("mark();", "mark(); else ambient();"),
    "effect-after-if": BASE.replace("const after", "ambient(); const after"),
    "effect-unused-method": BASE.replace(
        "class Item {", "class Item { unused() { ambient(); return true; }"
    ),
}


def observations(name, ready, other):
    """Return (attribute writes, result) of one probe call on an element whose
    `ready`/`other` attributes are present as given."""
    changed = ready
    events = []
    if name in ("conditional-store", "snapshots"):
        changed = not ready
    if name == "nested":
        changed = ready and not other
    if name == "both-arms" or (name == "nested" and ready):
        events.append(("branch", not changed))
    if name == "snapshots":
        events.extend((("snapshot", False), ("owner", changed)))
    events.extend((("before", True), ("after", not changed)))
    return [(key, str(value).lower()) for key, value in events], changed


def scenarios(name):
    """Yield (first_flags, second_flags, alias, expected_result, (first_events,
    second_events)) for each combination of `ready`/`other` attributes, with the
    probe called on the same element twice (alias) or on two elements."""
    for flags in range(4):
        first = (bool(flags & 1), bool(flags & 2))
        second = (not first[0], not first[1])
        for alias in (False, True):
            first_events, first_result = observations(name, *first)
            second_events, second_result = observations(name, *second)
            yield (
                first,
                second,
                alias,
                first_result and (first_result if alias else second_result),
                (
                    first_events * (3 if alias else 2),
                    second_events * (0 if alias else 1),
                ),
            )


def oracle(args, name, text, *, outcome=None):
    """Run every scenario in the VM and Node; both must report result=true."""
    text += "var result = true;\n"
    for first, second, alias, expected, events in scenarios(outcome or name):
        text += f"var first = {callbacks.ELEMENT}, second = {callbacks.ELEMENT};\n"
        for variable, flags in (("first", first), ("second", second)):
            for key, present in zip(("ready", "other"), flags):
                if present:
                    text += f"{variable}.attributes.{key} = '';\n"
        actual = "probe(first, " + ("first" if alias else "second") + ")"
        text += f"result = ({actual} === {str(expected).lower()}) && result;\n"
        for variable, writes in zip(("first", "second"), events):
            trace = "".join(key + "," for key, _ in writes)
            text += f"result = result && {variable}.trace === {json.dumps(trace)};\n"
            for key, value in dict(writes).items():
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
    for first, second, alias, expected, events in scenarios(name):
        owners = (("doc", "atoms", "button"), ("foreign_doc", "foreign_atoms", "other_button"))
        for (owner, table, node), flags in zip(owners, (first, second)):
            for key, present in zip(("ready", "other"), flags):
                text += f'flag({owner}, {table}, {node}, "{key}", {str(present).lower()});\n'
        actual = "@ENTRY@(element, " + ("alias" if alias else "foreign") + ")"
        text += f"assert(static_cast<bool>({actual}) == {str(expected).lower()});\n"
        for (owner, table, node), writes in zip(owners, events):
            keys = ", ".join(json.dumps(key) for key, _ in writes)
            text += f"check({owner}, {table}, {node}, {{{keys}}});\n"
            for key, value in dict(writes).items():
                text += f"assert({owner}.read().attribute_value({node}, {table}.intern({json.dumps(key)})) == {json.dumps(value)});\n"
    return text


def shape(text, name):
    """captures.shape() plus checks that the owner keeps its `scf.if` after the
    closures and the call or cell store the case expects."""
    owner, cell, closures, writer, reader = captures.shape(text)
    branches = list(re.finditer(r"^    (?:%\w+(?::\d+)? = )?scf\.if %\w+", owner[0], re.M))
    assert branches, "source lost its owner-level conditional"
    assert all(closure.start() < branches[0].start() for closure in closures)
    if name != "conditional-store":
        assert re.search(r"^      %\w+ = ctjs\.call", owner[0], re.M), owner[0]
    if name in ("conditional-store", "both-arms", "nested", "snapshots"):
        assert re.search(rf"^      +ctjs\.cell_set {re.escape(cell)},", owner[0], re.M), owner[0]
    if name == "nested":
        assert re.search(r"^      (?:%\w+(?::\d+)? = )?scf\.if %\w+", owner[0], re.M)
    return owner, cell, closures, writer, reader


def mutations(text):
    """Yield (label, mutated IR) pairs the lowering must refuse."""
    yield from captures.mutations(text)
    owner, cell, closures, writer, _ = shape(text, "conditional-call")
    name = re.search(r"@([^ (]+)", writer[0])[1]
    closure = next(closure[1] for closure in closures if name.endswith("$" + closure[3]))
    call = re.search(
        rf"^      %\w+ = ctjs\.(?:call_direct @{re.escape(name)}|call {re.escape(closure)})\([^\n]+",
        owner[0],
        re.M,
    )
    assert call, owner[0]
    # Wrap the call in 64 more valid `scf.if`s so only the 64-level nesting limit
    # in BooleanCaptures.cpp can refuse it; the JS frontend cannot produce this.
    predicate = re.search(r"^    scf\.if (%\w+)", owner[0], re.M)[1]
    nested = call[0]
    for _ in range(64):
        nested = f"      scf.if {predicate} {{\n{nested}\n      }}"
    yield (
        "too-deep",
        text[: owner.start() + call.start()] + nested + text[owner.start() + call.end() :],
    )
    # A state-bearing branch before the unconditional store must not gain
    # authorization from the later store or the later callback creators.
    store = re.search(rf"^    ctjs\.cell_set {re.escape(cell)}, (%\w+)", owner[0], re.M)
    earlier = (
        f"    %early = ctjs.truthy {store[1]}\n"
        f"    scf.if %early {{\n      ctjs.cell_set {cell}, {store[1]}\n    }}\n"
    )
    yield "branch-before-initialization", captures.insert(text, owner, store.start(), earlier)
    # Inside an accepted arm the cell may still only be used as a cell, not stored.
    yield (
        "conditional-cell-escape",
        captures.insert(text, owner, call.end(), f'      ctjs.store_global "escaped", {cell}'),
    )


def roots(text):
    """captures.roots() plus conditional roots, as (label, valid, invalid) IR."""
    yield from captures.roots(text)
    owner, cell, closures, _, _ = shape(text, "conditional-call")
    frame = re.search(r"(%\w+) = ctjs\.frame_enter \d+", owner[0])[1]
    exited = re.search(rf"^    ctjs\.frame_exit {re.escape(frame)}\b", owner[0], re.M)
    predicate = re.search(r"^    scf\.if (%\w+)", owner[0], re.M)[1]
    for label, value in [("cell", cell)] + [
        ("callback-" + closure[3], closure[1]) for closure in closures
    ]:
        # A root inside an `if` must still sit inside the frame, even though
        # that arm is skipped for some runtime inputs.
        root = f"    scf.if {predicate} {{\n      ctjs.root {value} in {frame}\n    }}"
        yield (
            "conditional-" + label,
            captures.insert(text, owner, exited.start(), root),
            captures.insert(text, owner, exited.end(), root),
        )


def main(argv=None):
    args = dom.parse_args(__doc__, oracles=True, argv=argv)
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for name, method in CASES.items():
        text = callbacks.source(method + captures.CALLS)
        oracle(args, name, text)
        ir, contract = dom.prepare(args, name, text, 2, entry_name="probe")
        shape(ir.read_text(), name)
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        for owned, optimize, manifest in dom.variants(declared):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            dom.standalone(args, native, label, checks(name), args.compilers, includes, libraries)
            executions += 4
        dom.refuse(args, ir, contract, name + "-undeclared")
        dom.exhaust(args, ir, declared, name, [0, 8], "{budget} did not exhaust class branch proof")
        refusals += 4
        if name == "conditional-call":
            refusals += budget_boundary(args, ir, declared, subject="Class Boolean branches")
            for label, valid, invalid in roots(ir.read_text()):
                path = args.work / f"rooted-{label}.mlir"
                path.write_text(valid)
                manifest = dict(declared, module_sha256=dom.fingerprint(args.opt, path))
                for owned, optimize, variant in dom.variants(manifest):
                    output = dom.lower(
                        args, path, variant, f"rooted-{label}-{owned}-{optimize}", optimize=optimize
                    ).read_text()
                    assert dom.NATIVE.search(output) and not dom.FUNCTION.search(output), output
                path.write_text(invalid)
                manifest = dict(declared, module_sha256=dom.fingerprint(args.opt, path))
                dom.refuse(args, path, manifest, "late-root-" + label)
                refusals += 2
            for label, changed in mutations(ir.read_text()):
                assert changed != ir.read_text(), label
                path = args.work / f"mutated-{label}.mlir"
                path.write_text(changed)
                manifest = dict(declared, module_sha256=dom.fingerprint(args.opt, path))
                dom.refuse(args, path, manifest, label)
                refusals += 2
    for name, method in BAD.items():
        ir, contract = dom.prepare(
            args, name, callbacks.source(method + captures.CALLS), 2, entry_name="probe"
        )
        if name == "loop-ancestor":
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
        f"DOM class Boolean branches: {len(CASES)} Node/VM sources, {executions} native executions, {refusals} refusals"
    )


def test_branches(lit):
    main(lit.args("%dom_driver --node %node --reference %native_reference"))


if __name__ == "__main__":
    main()
