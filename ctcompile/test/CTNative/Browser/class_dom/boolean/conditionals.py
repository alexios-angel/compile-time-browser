#!/usr/bin/env python3
"""Check that a class method conditionally writing a captured Boolean
(`if (!seen) { seen = true; }`) compiles natively and matches Node and the VM,
and that unsupported variants and hand-mutated IR are refused."""

import hashlib
import json
import re

from CTNative.Browser.dom import dom
from CTNative.Browser.class_dom import callbacks
from CTNative.Browser.class_dom.boolean import captures
from CTNative.Browser.class_dom.boolean import arm_state as arms
from CTNative.Browser.class_dom.parameter_defaults import budget_boundary
from CTNative.Browser.dom.boolean.cells import source_operations
from CTNative.harness import run

# main() pins the `conditional-writer` source by SHA-256; keep WRITE and the
# base METHOD byte-for-byte so that pin still matches.
WRITE = "if (!seen) { seen = true; }"
METHOD = captures.METHOD.replace("seen = true;", WRITE)
CASES = {
    "conditional-writer": METHOD,
    "skipped-store": METHOD.replace("if (!seen)", "if (seen)").replace(
        "return before !== after;", "return before === after;"
    ),
    "nested-numeric": METHOD.replace("seen = false", "seen = !1")
    .replace(WRITE, "if (!seen) { if (seen === false) { seen = !0; } }")
    .replace("mark();", "mark(); mark();"),
    "else-stores": METHOD.replace(WRITE, WRITE + " else { seen = false; }")
    .replace("mark();", "mark(); target.setAttribute('middle', pending()); mark();")
    .replace("return before !== after;", "return before === after;"),
    "snapshot-return": METHOD.replace(
        WRITE, "const snapshot = !seen; " + WRITE + " return snapshot;"
    )
    .replace("mark();", "const saved = mark();")
    .replace("return before", "target.setAttribute('snapshot', saved); return before"),
    "arm-state": arms.METHOD.replace("seen = true;", WRITE),
}
BAD = {
    "nonboolean-store": METHOD.replace("seen = true;", "seen = 'true';"),
    "computed-store": METHOD.replace("seen = true;", "seen = !seen;"),
    # Not refused here because they compile: the extra-capture source is covered
    # in mixed_captures.py ("extra-condition"), and the early-return and
    # helper-result-if sources in results.py.
    "loop-leaf": METHOD.replace(WRITE, "while (!seen) { seen = true; }"),
    "nested-callback": METHOD.replace(
        WRITE, "if (!seen) { const nested = () => { seen = true; }; nested(); seen = true; }"
    ),
    "recursive": METHOD.replace("seen = true;", "seen = true; mark();"),
    "escaping": METHOD.replace("mark();", "target.saved = mark; mark();"),
    "deferred": METHOD.replace("mark();", "setTimeout(mark, 0);"),
    "effect-then": METHOD.replace("seen = true;", "seen = true; ambient();"),
    # Every call in CALLS starts with seen == false, so this arm never runs.
    "effect-else": METHOD.replace(WRITE, WRITE + " else { ambient(); }"),
    "effect-nested-skipped": METHOD.replace(
        WRITE, "if (seen) { if (seen) { ambient(); } } " + WRITE
    ),
    "effect-after": METHOD.replace("return before", "ambient(); return before"),
    "effect-unused-method": METHOD.replace(
        "class Item {", "class Item { unused() { ambient(); return true; }"
    ),
}


def events(name):
    """Return the (attribute, value) writes one item.run() makes for case name."""
    result = [("middle", "false")] if name == "else-stores" else []
    result += [
        ("before", "true"),
        ("after", "true" if name in ("skipped-store", "else-stores") else "false"),
    ]
    if name == "snapshot-return":
        result.append(("snapshot", "true"))
    return result


def oracle(args, name, text):
    """Run the entry with and without aliasing in the VM and Node; both must agree."""
    if name == "arm-state":
        return arms.oracle(args, "arm-only-state", text)
    text += "var result = true;\n"
    for alias in (False, True):
        text += f"var first = {callbacks.ELEMENT}, second = {callbacks.ELEMENT};\n"
        text += "result = probe(first, " + ("first" if alias else "second") + ") && result;\n"
        for variable, count in (("first", 3 if alias else 2), ("second", 0 if alias else 1)):
            observed = events(name) * count
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
    """C++ assertions for the native harness, matching oracle()."""
    if name == "arm-state":
        return arms.checks("arm-only-state")
    text = r"""
        (void)classes;
        (void)pressed;
        foreign_doc.log_writes(true);
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
    for alias in (False, True):
        text += "assert(@ENTRY@(element, " + ("alias" if alias else "foreign") + "));\n"
        for owner, table, node, count in (
            ("doc", "atoms", "button", 3 if alias else 2),
            ("foreign_doc", "foreign_atoms", "other_button", 0 if alias else 1),
        ):
            observed = events(name) * count
            keys = ", ".join(json.dumps(key) for key, _ in observed)
            text += f"check({owner}, {table}, {node}, {{{keys}}});\n"
            for key, value in dict(observed).items():
                text += f"assert({owner}.read().attribute_value({node}, {table}.intern({json.dumps(key)})) == {json.dumps(value)});\n"
    return text


def shape(text, name="conditional-writer"):
    """Return the writer closure, checking its store sits in an `scf.if` on a
    truthy test and that the `if` yields no result."""
    if name == "arm-state":
        *_, leaves = arms.shape(text)
        writer = next(body for body in leaves if "ctjs.store_upvalue" in body[0])
    else:
        _, _, _, writer, _ = captures.shape(text)
    truth = re.search(r"^    (%\w+) = ctjs\.truthy %\w+", writer[0], re.M)
    assert truth and re.search(rf"^    scf\.if {re.escape(truth[1])} \{{", writer[0], re.M)
    assert re.search(r"^      +ctjs\.store_upvalue %\w+\[0\], %\w+", writer[0], re.M)
    assert not re.search(r"scf\.if[^\n]*->", writer[0]), writer[0]
    return writer


def mutations(text):
    """Yield (label, mutated IR) pairs the lowering must refuse."""
    yield from captures.mutations(text)
    writer = shape(text)
    branch = re.search(r"^    scf\.if (%\w+) \{\n.*?^    \}$", writer[0], re.M | re.S)
    assert branch
    store = re.search(r"ctjs\.store_upvalue (%\w+)\[0\], (%\w+)", branch[0])
    assert store
    # An undefined arm result remains unsupported even when its result is unused.
    result_if = (
        branch[0]
        .replace("scf.if", "%conditional_result = scf.if", 1)
        .replace(" {", " -> (!ctjs.value) {", 1)
    )
    result_if = result_if.replace(
        "    } else {", f"      scf.yield {store[2]} : !ctjs.value\n    }} else {{"
    )
    result_if = result_if[:-5] + (
        "      %other_result = ctjs.constant #ctjs.undefined\n"
        "      scf.yield %other_result : !ctjs.value\n    }"
    )
    yield (
        "resultful-helper-if",
        text[: writer.start() + branch.start()] + result_if + text[writer.start() + branch.end() :],
    )
    condition = re.search(r"^    (%\w+) = ctjs\.truthy %\w+", writer[0], re.M)
    changed = (
        writer[0][: condition.start()]
        + (f"    {condition[1]} = arith.constant true\n")
        + writer[0][condition.end() :]
    )
    yield "unproved-i1-condition", text[: writer.start()] + changed + text[writer.end() :]
    # Wrap the branch in 64 more `scf.if`s. The nesting limit counts every
    # arm, even ones that add no capture or effect.
    nested = branch[0]
    for _ in range(64):
        nested = f"    scf.if {branch[1]} {{\n{nested}\n    }}"
    yield (
        "conditional-depth",
        text[: writer.start() + branch.start()] + nested + text[writer.start() + branch.end() :],
    )


def nested_root(text):
    """Return IR with a `ctjs.root` of the writer's callee inside the store's arm."""
    writer = shape(text)
    frame = re.search(r"(%\w+) = ctjs\.frame_enter \d+", writer[0])[1]
    callee = re.findall(r"(%\w+): !ctjs.value", writer[0].split("\n")[0])[2]
    store = re.search(r"^      ctjs\.store_upvalue[^\n]+", writer[0], re.M)
    return captures.insert(text, writer, store.end(), f"      ctjs.root {callee} in {frame}")


def main():
    args = dom.parse_args(__doc__, oracles=True)
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for name, method in CASES.items():
        text = callbacks.source(method + captures.CALLS)
        # Pin the generated `conditional-writer` source so an edit to the shared
        # helpers (captures, callbacks) cannot silently change what this test covers.
        if name == "conditional-writer":
            assert hashlib.sha256(text.encode()).hexdigest() == (
                "ef07e8ff2cb61d4d8cfbeafd00a6e44b20b85d8f0824fd08ea3b8b3fef18cb16"
            )
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
        dom.exhaust(args, ir, declared, name, [0, 8], "{budget} did not exhaust conditional proof")
        refusals += 4
        if name != "conditional-writer":
            continue
        refusals += budget_boundary(args, ir, declared, subject="Class conditional Boolean leaf")
        for label, valid, invalid in [
            *captures.roots(ir.read_text()),
            ("nested-writer-callee", nested_root(ir.read_text()), None),
        ]:
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
    for name, method in BAD.items():
        ir, contract = dom.prepare(
            args, name, callbacks.source(method + captures.CALLS), 2, entry_name="probe"
        )
        if name == "loop-leaf":
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
        f"DOM class Boolean conditional leaves: {len(CASES)} Node/VM sources, {executions} native executions, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
