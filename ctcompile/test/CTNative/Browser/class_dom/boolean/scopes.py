#!/usr/bin/env python3
"""Check that a Boolean-writing callback created inside an `if` arm, and only
called inside that arm, compiles natively and matches Node and the VM, and that
callbacks used outside their arm, plus hand-mutated IR, are refused."""

import re
import subprocess

from CTNative.Browser.dom import dom
from CTNative.Browser.class_dom import callbacks
from CTNative.Browser.class_dom.boolean import branches
from CTNative.Browser.class_dom.boolean import captures
from CTNative.Browser.class_dom.parameter_defaults import budget_boundary
from CTNative.Browser.dom.boolean.cells import source_operations

# branches.BASE with the `mark` writer created inside the `if` arm that calls it.
METHOD = branches.BASE.replace("    const mark = () => { seen = true; };\n", "").replace(
    "if (target.hasAttribute('ready')) mark();",
    "if (target.hasAttribute('ready')) { const mark = () => { seen = true; }; mark(); }",
)
CASES = {
    "branch-local-creator": (METHOD, "conditional-call"),
    "else-creator": (
        METHOD.replace(
            "if (target.hasAttribute('ready'))", "if (target.hasAttribute('ready')) {} else"
        ),
        "conditional-store",
    ),
    "nested-call": (
        METHOD.replace(
            "mark(); }",
            "if (!target.hasAttribute('other')) mark();\n"
            "      target.setAttribute('branch', pending()); }",
        ),
        "nested",
    ),
    "local-reader-and-writer": (
        METHOD.replace("    const pending = () => !seen;\n", "")
        .replace("const before = pending();", "const before = !seen;")
        .replace("const after = pending();", "const after = !seen;")
        .replace("{ const mark", "{ const pending = () => !seen; const mark")
        .replace("mark(); }", "mark(); pending(); }"),
        "conditional-call",
    ),
}
BAD = {
    # `pending` is created outside the arm but `seen` is initialized inside it,
    # so this is refused. With both callbacks in the arm it compiles (arm_state.py).
    "initialization-in-arm": METHOD.replace("let seen = false;", "let seen;").replace(
        "{ const mark", "{ seen = false; const mark"
    ),
    "before-creator": METHOD.replace(
        "const mark = () => { seen = true; }; mark();",
        "mark(); const mark = () => { seen = true; };",
    ),
    "after-arm": METHOD.replace("mark(); }", "} mark();"),
    "sibling-arm": METHOD.replace("mark(); }", "} else { mark(); }"),
    "escaping": METHOD.replace("mark(); }", "target.saved = mark; mark(); }"),
    "deferred": METHOD.replace("mark(); }", "setTimeout(mark, 0); }"),
    "loop-creator": METHOD.replace(
        "if (target.hasAttribute('ready'))", "while (target.hasAttribute('ready'))"
    ).replace("mark(); }", "mark(); target.removeAttribute('ready'); }"),
    "effect-local": METHOD.replace("mark(); }", "mark(); ambient(); }"),
    "effect-else": METHOD.replace("mark(); }", "mark(); } else { ambient(); }"),
    "effect-unused-method": METHOD.replace(
        "class Item {", "class Item { unused() { ambient(); return true; }"
    ),
}


def shape(text):
    """Return (owner, cell, closure, leaf) for the arm-local writer callback,
    checking the cell is initialized before the `if` that creates it."""
    bodies = captures.functions(text)
    for owner in bodies:
        closures = list(
            re.finditer(
                r"^    +(%\w+) = ctjs\.create_closure (%\w+)\[(\d+)\] this (%\w+) captures (%\w+)$",
                owner[0],
                re.M,
            )
        )
        if len(closures) != 2:
            continue
        cell = closures[0][5]
        assert all(closure[5] == cell for closure in closures), owner[0]
        assert len(re.findall(r"ctjs\.create_cell", owner[0])) == 1, owner[0]
        assert re.search(rf"^    {re.escape(cell)} = ctjs\.create_cell %\w+", owner[0], re.M)
        store = re.search(rf"^    ctjs\.cell_set {re.escape(cell)}, %\w+", owner[0], re.M)
        branch = re.search(r"^    (?:%\w+(?::\d+)? = )?scf\.if %\w+", owner[0], re.M)
        assert store and branch and store.end() < branch.start()
        for closure in closures:
            leaf = next(
                body
                for body in bodies
                if re.search(rf"@[^ (]+\${closure[3]}\(", body[0].split("\n")[0])
            )
            assert "upvalue_count = 1 : i32" in leaf[0]
            if "ctjs.store_upvalue" in leaf[0]:
                assert closure[0].startswith("      "), closure[0]
                assert branch.end() < closure.start()
                return owner, cell, closure, leaf
    raise AssertionError("source lost its arm-local writer and initialized owner cell")


def writer_call(owner, closure, leaf):
    """Return (call match, writer symbol) for the call of the writer after its creation."""
    name = re.search(r"@([^ (]+)", leaf[0])[1]
    call = re.search(
        rf"^      +%\w+ = ctjs\.(?:call_direct @{re.escape(name)}|call {re.escape(closure[1])})\([^\n]+",
        owner[0],
        re.M,
    )
    assert call and closure.end() < call.start(), owner[0]
    return call, name


def mutations(text):
    """Yield (label, mutated IR) pairs. Labels starting "rooted-" must still lower;
    the lowering must refuse every other one."""
    owner, cell, closure, leaf = shape(text)
    call, name = writer_call(owner, closure, leaf)
    yield (
        "duplicate-creator",
        captures.insert(
            text, owner, closure.end(), closure[0].replace(closure[1], "%duplicate", 1)
        ),
    )
    yield (
        "escaped-creator",
        captures.insert(
            text, owner, closure.end(), f'      ctjs.store_global "escaped", {closure[1]}'
        ),
    )
    initial = re.search(rf"^    {re.escape(cell)} = ctjs\.create_cell (%\w+)", owner[0], re.M)[1]
    exited = re.search(r"^    ctjs\.frame_exit %\w+", owner[0], re.M)
    yield (
        "unselected-caller-after-join",
        captures.insert(
            text,
            owner,
            exited.start(),
            f"    %extra = ctjs.call_direct @{name}({initial}, {initial}, {initial})",
        ),
    )
    yield (
        "symbol-observer",
        text.replace(
            "module attributes {", f"module attributes {{test.boolean_observer = @{name}, ", 1
        ),
    )
    yield (
        "wrong-enclosing",
        text[: owner.start() + closure.start()]
        + closure[0].replace(closure[2] + "[", initial + "[", 1)
        + text[owner.start() + closure.end() :],
    )
    store = re.search(rf"^    ctjs\.cell_set {re.escape(cell)}, %\w+", owner[0], re.M)
    body = owner[0][: store.start()] + owner[0][store.end() :]
    body = body.replace(exited[0], store[0] + "\n" + exited[0])
    yield "initialization-after-arm", text[: owner.start()] + body + text[owner.end() :]
    loaded = re.search(r"ctjs\.store_upvalue (%\w+)\[0\], (%\w+)", leaf[0])
    yield (
        "callee-as-value",
        text[: leaf.start() + loaded.start()]
        + loaded[0].replace(loaded[2], loaded[1])
        + text[leaf.start() + loaded.end() :],
    )
    yield (
        "rooted-local-creator",
        captures.insert(
            text,
            owner,
            closure.end(),
            f"      ctjs.root {closure[1]} in "
            + re.search(r"(%\w+) = ctjs\.frame_enter", owner[0])[1],
        ),
    )
    frame = re.search(r"(%\w+) = ctjs\.frame_enter \d+", leaf[0])
    exited = re.search(rf"^    ctjs\.frame_exit {re.escape(frame[1])}\b", leaf[0], re.M)
    callee = re.findall(r"(%\w+): !ctjs.value", leaf[0].split("\n")[0])[2]
    root = f"    ctjs.root {callee} in {frame[1]}"
    yield "rooted-local-callee", captures.insert(text, leaf, frame.end(), root)
    yield "late-callee-root", captures.insert(text, leaf, exited.end(), root)


def verifier_controls(text):
    """Yield (label, IR) pairs that use the closure out of scope; the verifier rejects them."""
    owner, cell, closure, leaf = shape(text)
    call, name = writer_call(owner, closure, leaf)
    initial = re.search(rf"^    {re.escape(cell)} = ctjs\.create_cell (%\w+)", owner[0], re.M)[1]
    # Move only the creator below its original use: all other operands stay in scope.
    body = owner[0][: closure.start()] + owner[0][closure.end() :]
    body = body.replace(call[0], call[0] + "\n" + closure[0])
    yield "before-creation", text[: owner.start()] + body + text[owner.end() :]
    # A sibling or parent block cannot use the arm's closure SSA value, so the
    # verifier rejects these before the class proof ever runs.
    extra = f"%outside = ctjs.call_direct @{name}({initial}, {initial}, {closure[1]})"
    for label, boundary, indent in (
        ("sibling-arm", "    } else {", "      "),
        ("after-join", re.search(r"^    ctjs\.frame_exit %\w+", owner[0], re.M)[0], "    "),
    ):
        assert boundary in owner[0]
        replacement = (
            boundary + "\n" + indent + extra
            if label == "sibling-arm"
            else indent + extra + "\n" + boundary
        )
        body = owner[0].replace(boundary, replacement, 1)
        yield label, text[: owner.start()] + body + text[owner.end() :]


def main(argv=None):
    args = dom.parse_args(__doc__, oracles=True, argv=argv)
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for name, (method, outcome) in CASES.items():
        text = callbacks.source(method + captures.CALLS)
        branches.oracle(args, name, text, outcome=outcome)
        ir, contract = dom.prepare(args, name, text, 2, entry_name="probe")
        owner, cell, closure, leaf = shape(ir.read_text())
        writer_call(owner, closure, leaf)
        if name == "nested-call":
            assert re.search(r"^      (?:%\w+(?::\d+)? = )?scf\.if %\w+", owner[0], re.M)
        if name == "else-creator":
            assert "    } else {" in owner[0][: closure.start()]
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        for owned, optimize, manifest in dom.variants(declared):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            dom.standalone(
                args, native, label, branches.checks(outcome), args.compilers, includes, libraries
            )
            executions += 4
        dom.refuse(args, ir, contract, name + "-undeclared")
        dom.exhaust(args, ir, declared, name, [0, 8], "{budget} did not exhaust class scope proof")
        refusals += 4
        if name != "branch-local-creator":
            continue
        refusals += budget_boundary(args, ir, declared, subject="Class Boolean scopes")
        for label, changed in mutations(ir.read_text()):
            assert changed != ir.read_text(), label
            path = args.work / f"mutated-{label}.mlir"
            path.write_text(changed)
            manifest = dict(declared, module_sha256=dom.fingerprint(args.opt, path))
            if label.startswith("rooted-"):
                for owned, optimize, variant in dom.variants(manifest):
                    output = dom.lower(
                        args, path, variant, f"{label}-{owned}-{optimize}", optimize=optimize
                    ).read_text()
                    assert dom.NATIVE.search(output) and not dom.FUNCTION.search(output), output
            else:
                dom.refuse(args, path, manifest, label)
                refusals += 2
        for label, changed in verifier_controls(ir.read_text()):
            path = args.work / f"verifier-{label}.mlir"
            path.write_text(changed)
            actual = subprocess.run(
                [args.opt, str(path)], text=True, capture_output=True, timeout=30
            )
            assert actual.returncode == 1 and "error:" in actual.stderr, actual.stderr
            refusals += 1
    for name, method in BAD.items():
        ir, contract = dom.prepare(
            args, name, callbacks.source(method + captures.CALLS), 2, entry_name="probe"
        )
        if name == "loop-creator":
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
        f"DOM class Boolean scopes: {len(CASES)} Node/VM sources, {executions} native executions, {refusals} refusals"
    )


def test_scopes(lit):
    main(lit.args("%dom_driver --node %node --reference %native_reference"))


if __name__ == "__main__":
    main()
