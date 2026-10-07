#!/usr/bin/env python3
"""Retain actual local Boolean state through class and closure normalization."""

import json
import re

from CTNative.Browser import native_dom as dom
from CTNative.Browser import native_class_dom_callbacks as callbacks
from CTNative.Browser.native_class_dom_parameter_defaults import budget_boundary
from CTNative.Browser.native_dom_boolean_cells import source_operations
from CTNative.harness import run

METHOD = """class Item {
  run(target) {
    let seen = false;
    const pending = () => !seen;
    const mark = () => { seen = true; };
    const before = pending();
    mark();
    const after = pending();
    target.setAttribute('before', before);
    target.setAttribute('after', after);
    return before !== after;
  }
}"""
CALLS = """
const item = new Item();
const first = item.run(element);
const second = item.run(other);
const third = item.run(element);
return first && second && third;
"""
CASES = {
    "read-write-read": METHOD,
    # Compose retained source captures with the exact numeric-origin proof.
    "numeric-origins": METHOD.replace("seen = false", "seen = !1").replace(
        "seen = true", "seen = !0"
    ),
    "sibling-writers": METHOD.replace(
        "const before", "const reset = () => { seen = false; };\n    const before"
    ).replace(
        "return before", "reset(); target.setAttribute('again', pending());\n    return before"
    ),
    "repeated-calls": METHOD.replace(
        "const after", "const middle = pending(); mark();\n    const after"
    ).replace("return before", "target.setAttribute('middle', middle); return before"),
    "owner-store": METHOD.replace("const after", "seen = false; const after").replace(
        "return before !== after;",
        "target.setAttribute('owner', seen); return before === after;",
    ),
    # These identical returns coalesce into one exit. This source exercises
    # the original query and branch; it makes no method-cloning claim.
    "coalesced-switch": METHOD.replace(
        "return before !== after;",
        "switch (target.hasAttribute('ready')) {\n"
        "      case true: return before !== after;\n"
        "      default: return before !== after;\n    }",
    ),
}
BAD = {
    "second-cell": METHOD.replace(
        "const before",
        "let other = false; const change = () => { other = true; }; change(); const before",
    ),
    # The original extra-capture source is positive in the mixed-capture test.
    "uncalled-writer": METHOD.replace(
        "const before", "const unused = () => { seen = false; }; const before"
    ),
    "nested-writer": METHOD.replace(
        "seen = true;", "const nested = () => { seen = true; }; nested();"
    ),
    "nonboolean-initial": METHOD.replace("seen = false", "seen = 'false'"),
    "input-initial": METHOD.replace("seen = false", "seen = target"),
    "nonboolean-leaf-store": METHOD.replace("seen = true", "seen = 'true'"),
    "computed-leaf-store": METHOD.replace("seen = true", "seen = !seen"),
    "input-owner-store": METHOD.replace("const after", "seen = target; const after"),
    "escaping-callback": METHOD.replace("mark();", "target.saved = mark; mark();"),
    "timer": METHOD.replace("mark();", "setTimeout(mark, 0);"),
    "event": METHOD.replace("mark();", "target.addEventListener('click', mark);"),
    "recursive": METHOD.replace("seen = true;", "seen = true; mark();"),
    "receiver": METHOD.replace("() => !seen", "() => this === seen"),
    "new-target": METHOD.replace("() => !seen", "function() { return new.target === seen; }"),
    "extra-argument": METHOD.replace("mark();", "mark(target);"),
    # The original conditional-call/store bodies are positive witnesses in
    # native_class_dom_boolean_branches.py.
    "loop-call": METHOD.replace(
        "mark();",
        "while (target.hasAttribute('ready')) { mark(); target.removeAttribute('ready'); }",
    ),
    # Unlike loop-call, state stays outside this loop. Its real completion
    # dispatch reaches an independent, currently unsupported arithmetic guard.
    "completion-loop": METHOD.replace(
        "return before !== after;",
        "const limit = target.hasAttribute('ready') ? 5 : 3;\n"
        "    for (var i = 0; i < limit; i = i + 1) {\n"
        "      if (i === 1) { continue; }\n"
        "      if (i === 3) { break; }\n"
        "      if (limit === 5) { return before; }\n"
        "    }\n    return !after;",
    ),
    "effect-leaf": METHOD.replace("seen = true;", "seen = true; ambient();"),
    "effect-before": METHOD.replace("const before", "ambient(); const before"),
    "effect-after": METHOD.replace("return before", "ambient(); return before"),
    "effect-unused-method": METHOD.replace(
        "class Item {", "class Item { unused() { ambient(); return true; }"
    ),
}


def functions(text):
    return list(re.finditer(r"^  ctjs\.func [^\n]+ \{\n.*?^  \}", text, re.M | re.S))


def shape(text):
    bodies = functions(text)
    for owner in bodies:
        captures = list(
            re.finditer(
                r"^    (%\w+) = ctjs\.create_closure (%\w+)\[(\d+)\] this (%\w+) captures (%\w+)$",
                owner[0],
                re.M,
            )
        )
        if len(captures) < 2:
            continue
        cell = captures[0][5]
        assert all(closure[5] == cell for closure in captures), owner[0]
        assert re.search(rf"^    {re.escape(cell)} = ctjs\.create_cell %\w+", owner[0], re.M)
        leaves = [
            next(
                body
                for body in bodies
                if re.search(rf"@[^ (]+\${closure[3]}\(", body[0].split("\n")[0])
            )
            for closure in captures
        ]
        writer = next(body for body in leaves if "ctjs.store_upvalue" in body[0])
        reader = next(body for body in leaves if "ctjs.load_upvalue" in body[0])
        assert all("upvalue_count = 1 : i32" in body[0] for body in leaves)
        return owner, cell, captures, writer, reader
    raise AssertionError("source lost exact shared load/store capture slots")


def identity_dispatch(text):
    """Force method cloning, then reach the unsupported dynamic-index guard."""
    owner, _, _, _, _ = shape(text)
    returned = re.search(r"^    ctjs\.return (%\w+)", owner[0], re.M)
    exited = re.search(r"^    ctjs\.frame_exit %\w+", owner[0], re.M)
    predicate = list(re.finditer(r"(%\w+) = ctjs\.truthy %\w+", owner[0]))[-1][1]
    assert exited.end() < returned.start()
    dispatch = (
        f"    %dispatch_index = arith.index_castui {predicate} : i1 to index\n"
        '    %dispatch_result = "scf.index_switch"(%dispatch_index) <{cases = array<i64: 0>}> ({\n'
        f"      scf.yield {returned[1]} : !ctjs.value\n"
        "    }, {\n"
        f"      scf.yield {returned[1]} : !ctjs.value\n"
        "    }) : (index) -> !ctjs.value\n"
    )
    body = owner[0][: exited.start()] + dispatch + owner[0][exited.start() :]
    body = body.replace(returned[0], "    ctjs.return %dispatch_result")
    return text[: owner.start()] + body + text[owner.end() :]


def events(name):
    result = [("before", "true"), ("after", "true" if name == "owner-store" else "false")]
    if name == "sibling-writers":
        result.append(("again", "true"))
    if name == "repeated-calls":
        result.append(("middle", "false"))
    if name == "owner-store":
        result.append(("owner", "false"))
    return result


def checks(name, *, observed=None, expected=True):
    result = r"""
        (void)classes;
        (void)pressed;
        foreign_doc.log_writes(true);
        const auto ready = [&](document & owner, atom_table & table, node_id id, bool value) {
            if (value) { assert(owner.set_attribute(id, table.intern("ready"), "")); }
            else { (void)owner.remove_attribute(id, table.intern("ready")); }
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
    for flag in (False, True):
        for alias in (False, True):
            result += f"ready(doc, atoms, button, {str(flag).lower()});\n"
            result += f"ready(foreign_doc, foreign_atoms, other_button, {str(not flag).lower()});\n"
            result += (
                "assert(static_cast<bool>(@ENTRY@(element, "
                + ("alias" if alias else "foreign")
                + ")) == "
                + str(expected).lower()
                + ");\n"
            )
            owners = (
                ("doc", "atoms", "button", 3 if alias else 2),
                ("foreign_doc", "foreign_atoms", "other_button", 0 if alias else 1),
            )
            for owner, table, node, count in owners:
                writes = (events(name) if observed is None else observed) * count
                keys = ", ".join(json.dumps(key) for key, _ in writes)
                result += f"check({owner}, {table}, {node}, {{{keys}}});\n"
                for key, value in dict(writes).items():
                    result += f"assert({owner}.read().attribute_value({node}, {table}.intern({json.dumps(key)})) == {json.dumps(value)});\n"
    return result


def oracle(args, name, text, *, observed=None, expected=True):
    text += "var result = true;\n"
    for flag in (False, True):
        for alias in (False, True):
            text += f"var first = {callbacks.ELEMENT}, second = {callbacks.ELEMENT};\n"
            text += ("first" if flag else "second") + ".attributes.ready = '';\n"
            text += (
                "result = (probe(first, "
                + ("first" if alias else "second")
                + ") === "
                + str(expected).lower()
                + ") && result;\n"
            )
            for variable, count in (("first", 3 if alias else 2), ("second", 0 if alias else 1)):
                writes = (events(name) if observed is None else observed) * count
                trace = "".join(key + "," for key, _ in writes)
                text += f"result = result && {variable}.trace === {json.dumps(trace)};\n"
                for key, value in dict(writes).items():
                    text += (
                        f"result = result && {variable}.attributes.{key} === {json.dumps(value)};\n"
                    )
    path = args.work / f"{name}.oracle.js"
    path.write_text(text)
    assert run([args.reference, str(path)]).stdout == "result=true\n", name
    path.write_text(text + "console.log('result=' + result);\n")
    assert run([args.node, str(path)]).stdout == "result=true\n", name


def insert(text, body, local_end, extra):
    at = body.start() + local_end
    return text[:at] + "\n" + extra + "\n" + text[at:]


def roots(text):
    owner, cell, captures, writer, reader = shape(text)
    selections = [
        (
            "cell",
            owner,
            cell,
            re.search(rf"^    ctjs\.cell_set {re.escape(cell)}, [^\n]+", owner[0], re.M),
        )
    ]
    for label, body in (("reader", reader), ("writer", writer)):
        index = re.search(r"@[^ (]+\$(\d+)\(", body[0])[1]
        closure = next(capture for capture in captures if capture[3] == index)
        selections.append((label + "-callback", owner, closure[1], closure))
        selections.append(
            (
                label + "-callee",
                body,
                re.findall(r"(%\w+): !ctjs.value", body[0].split("\n")[0])[2],
                re.search(r"^    %\w+ = ctjs\.frame_enter \d+", body[0], re.M),
            )
        )
    for label, body, value, after in selections:
        frame = re.search(r"(%\w+) = ctjs\.frame_enter \d+", body[0])[1]
        exited = re.search(rf"^    ctjs\.frame_exit {re.escape(frame)}\b", body[0], re.M)
        root = f"    ctjs.root {value} in {frame}"
        yield label, insert(text, body, after.end(), root), insert(text, body, exited.end(), root)


def mutations(text):
    owner, cell, captures, writer, reader = shape(text)
    closure = captures[0]
    yield "duplicate-creator", insert(
        text, owner, closure.end(), closure[0].replace(closure[1], "%duplicate", 1)
    )
    initial = re.search(rf"^    {re.escape(cell)} = ctjs\.create_cell (%\w+)", owner[0], re.M)[1]
    name = re.search(r"@([^ (]+)", writer[0])[1]
    exited = re.search(r"^    ctjs\.frame_exit %\w+", owner[0], re.M)
    yield "unselected-symbolic-caller", insert(
        text,
        owner,
        exited.start(),
        f"    %unselected = ctjs.call_direct @{name}({initial}, {initial}, {initial})",
    )
    yield "symbol-observer", text.replace(
        "module attributes {", f"module attributes {{test.boolean_observer = @{name}, ", 1
    )
    store = re.search(rf"^    ctjs\.cell_set {re.escape(cell)}, (%\w+)", owner[0], re.M)
    yield "cell-stored-as-value", text[: owner.start() + store.start()] + store[0].replace(
        store[1], cell
    ) + text[owner.start() + store.end() :]
    callee = re.search(r"ctjs\.store_upvalue (%\w+)\[0\], (%\w+)", writer[0])
    yield "callee-stored-as-value", text[: writer.start() + callee.start()] + callee[0].replace(
        callee[2], callee[1]
    ) + text[writer.start() + callee.end() :]
    loaded = re.search(r"ctjs\.load_upvalue %\w+\[0\]", reader[0])
    yield "wrong-slot", text[: reader.start() + loaded.start()] + loaded[0].replace(
        "[0]", "[1]"
    ) + text[reader.start() + loaded.end() :]
    yield "wrong-enclosing", text[: owner.start() + closure.start()] + closure[0].replace(
        closure[2] + "[", initial + "[", 1
    ) + text[owner.start() + closure.end() :]
    returned = re.search(r"^    ctjs\.return (%\w+)", owner[0], re.M)
    yield "returned-cell", text[
        : owner.start() + returned.start()
    ] + f"    ctjs.return {cell}" + text[owner.start() + returned.end() :]
    # Keep the defining literal in place, but move the only initialization
    # after the original callbacks and reads. SSA remains verifier-valid.
    missing = text[: owner.start() + store.start()] + text[owner.start() + store.end() :]
    moved_owner = next(body for body in functions(missing) if body.start() == owner.start())
    moved_exit = re.search(r"^    ctjs\.frame_exit %\w+", moved_owner[0], re.M)
    yield "late-initialization", insert(missing, moved_owner, moved_exit.start(), store[0])


def main():
    args = dom.parse_args(__doc__, oracles=True)
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for name, method in CASES.items():
        text = callbacks.source(method + CALLS)
        oracle(args, name, text)
        ir, contract = dom.prepare(args, name, text, 2, entry_name="probe")
        shape(ir.read_text())
        if name == "numeric-origins":
            owner, _, _, writer, _ = shape(ir.read_text())
            for body, bits in ((owner, "4607182418800017408"), (writer, "0")):
                literal = re.search(rf"(%\w+) = ctjs\.constant #ctjs.number<{bits}>", body[0])
                assert literal and re.search(
                    rf"ctjs\.unary not {re.escape(literal[1])}\b", body[0]
                ), "source lost its original !0/!1 producer"
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        for owned, optimize, manifest in dom.variants(declared):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            dom.standalone(args, native, label, checks(name), args.compilers, includes, libraries)
            executions += 4
        dom.refuse(args, ir, contract, name + "-undeclared")
        dom.exhaust(
            args, ir, declared, name, [0, 8], "{budget} did not exhaust Boolean capture proof"
        )
        refusals += 4
        if name == "read-write-read":
            refusals += budget_boundary(args, ir, declared, subject="Class Boolean capture")
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
                dom.refuse(args, path, manifest, f"late-root-{label}")
                refusals += 2
            for label, changed in mutations(ir.read_text()):
                assert changed != ir.read_text(), label
                path = args.work / f"mutated-{label}.mlir"
                path.write_text(changed)
                manifest = dict(declared, module_sha256=dom.fingerprint(args.opt, path))
                dom.refuse(args, path, manifest, label)
                refusals += 2
        if name == "coalesced-switch":
            # This verifier-valid identity wrapper reaches method cloning,
            # but the final DOM proof cannot type its dynamic index cast.
            # Preserve the entire CTJS/SCF input on that downstream refusal.
            path = args.work / "unsupported-dispatch.mlir"
            path.write_text(identity_dispatch(ir.read_text()))
            assert "scf.index_switch" in path.read_text()
            manifest = dict(declared, module_sha256=dom.fingerprint(args.opt, path))
            original = source_operations(args, path.read_text())
            for owned, optimize, variant in dom.variants(manifest):
                diagnostic = dom.lower(
                    args,
                    path,
                    variant,
                    f"dispatch-{owned}-{optimize}",
                    optimize=optimize,
                    success=False,
                    max_steps=1000000,
                )
                assert (
                    "DOM entry operation lacks a typed browser contract: arith.index_castui"
                    in diagnostic
                ), diagnostic
                assert source_operations(args, diagnostic) == original, diagnostic
                retained = diagnostic.partition("note: see current operation:")[2]
                assert "arith.index_castui" in retained and "cases = array<i64: 0>" in retained
                refusals += 1
    for name, method in BAD.items():
        ir, contract = dom.prepare(
            args, name, callbacks.source(method + CALLS), 2, entry_name="probe"
        )
        if name == "loop-call":
            assert "scf.while" in ir.read_text(), "loop refusal lost its actual loop"
        if name == "completion-loop":
            assert "scf.index_switch" in ir.read_text(), "completion refusal lost its dispatch"
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        original = (
            source_operations(args, ir.read_text())
            if name.startswith("effect-") or name == "completion-loop"
            else None
        )
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
            if name == "completion-loop":
                assert (
                    "DOM completion arithmetic must be a Boolean constant" in diagnostic
                ), diagnostic
            refusals += 1
    print(
        f"DOM class Boolean captures: {len(CASES)} Node/VM sources, {executions} native executions, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
