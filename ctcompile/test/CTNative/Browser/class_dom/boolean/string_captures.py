#!/usr/bin/env python3
"""Copy proved fixed Strings into equality tests around local Boolean state."""

import hashlib
import re

from CTNative.Browser.dom import dom
from CTNative.Browser.class_dom import callbacks
from CTNative.Browser.class_dom.boolean import captures
from CTNative.Browser.class_dom.boolean import forwarded_captures as forwarded
from CTNative.Browser.class_dom.boolean import mixed_captures as mixed
from CTNative.Browser.class_dom.parameter_defaults import budget_boundary
from CTNative.Browser.dom.boolean.cells import source_operations
from CTNative.harness import run

PENDING = "eventName === 'transitionend' ? !seen : seen"
LOCAL = captures.METHOD.replace(
    "    let seen = false;", "    const eventName = 'transitionend';\n    let seen = false;"
).replace("() => !seen", "() => " + PENDING)
FORWARDED = "const eventName = 'transitionend';\n" + captures.METHOD.replace(
    "() => !seen", "() => " + PENDING
)
CASES = {
    "local-string": LOCAL,
    "forwarded-string": FORWARDED,
    "forwarded-unequal": FORWARDED.replace(
        "eventName = 'transitionend'", "eventName = 'animationend'"
    ),
    "forwarded-shared": FORWARDED.replace(
        "seen = true;", "if (!seen) { if (eventName === 'transitionend') { seen = true; } }"
    ),
}
SOURCE_HASHES = {
    "local-string": "d428825418d63d02b4efb9cbd44d690da7adee91e3698f8e32fde6d00f9c26f7",
    "forwarded-string": "f353c255ce729af9022f0d3680f2ee59caa2bb418fddc9c267e1c5ea916d2297",
}
BAD = {
    # Preserve earlier String/Boolean comparisons as refusals.
    "old-local-mixed-types": mixed.BAD["string-fixed"],
    "old-forwarded-mixed-types": forwarded.BAD["string-fixed"],
    # Keep an actual state write: a fixed-only String reader is supported.
    "string-return": LOCAL.replace(PENDING, "{ seen = false; return eventName; }"),
    "string-yield": LOCAL.replace(PENDING, "eventName === 'transitionend' ? eventName : seen"),
    "string-truth": LOCAL.replace(PENDING, "eventName ? !seen : seen"),
    "string-store": FORWARDED.replace("seen = true;", "seen = eventName;"),
    "mutable-string": FORWARDED.replace("const eventName", "let eventName").replace(
        "seen = true;", "seen = true; eventName = 'animationend';"
    ),
    "dormant-string-writer": FORWARDED.replace(
        "const eventName = 'transitionend';",
        "let eventName = 'transitionend'; const unused = () => {"
        " const nested = () => { eventName = 'animationend'; }; };",
    ),
    "early-string": FORWARDED.replace("const eventName = 'transitionend';", "let eventName;")
    + "\neventName = 'transitionend';\n",
    "effect-after": LOCAL.replace(
        "    return before !== after;", "    ambient(); return before !== after;"
    ),
}


def observations(name):
    if name == "forwarded-unequal":
        return [("before", "false"), ("after", "true")]
    return [("before", "true"), ("after", "false")]


def capture_fixture(name):
    return forwarded if name.startswith("forwarded") else mixed


def shape(text, name="local-string"):
    result = capture_fixture(name).shape(text)
    reader = result[3][0]
    assert 'ctjs.constant #ctjs.string<"transitionend">' in reader[0], reader[0]
    assert "ctjs.compare strict_eq" in reader[0], reader[0]
    assert "scf.if" in reader[0], reader[0]
    return result


def mutations(text):
    yield from mixed.mutations(text)
    _, _, _, leaves, _, indices = shape(text)
    reader = leaves[0]
    fixed = re.search(r"^    (%\w+) = ctjs\.load_upvalue (%\w+)\[0\]", reader[0], re.M)
    exited = re.search(r"^    ctjs\.frame_exit %\w+", reader[0], re.M)
    # A discarded comparison still needs two values of the same proved type.
    yield (
        "unused-mixed-comparison",
        captures.insert(
            text,
            reader,
            exited.start(),
            f"    %mixed_state = ctjs.load_upvalue {fixed[2]}[{indices[0]}]\n"
            f"    %unused_compare = ctjs.compare strict_eq {fixed[1]}, %mixed_state",
        ),
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
        captures.oracle(args, name, text, observed=observed)
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
                captures.checks(name, observed=observed),
                args.compilers,
                includes,
                libraries,
            )
            executions += 4
        dom.refuse(args, ir, contract, name + "-undeclared")
        dom.exhaust(
            args, ir, declared, name, [0, 8], "{budget} did not exhaust fixed-String capture proof"
        )
        refusals += 4
        if name not in SOURCE_HASHES:
            continue
        if name == "local-string":
            refusals += budget_boundary(args, ir, declared, subject="Fixed String captures")
        for label, valid, invalid in capture_fixture(name).roots(ir.read_text()):
            path = args.work / f"{name}-rooted-{label}.mlir"
            path.write_text(valid)
            manifest = dict(declared, module_sha256=dom.fingerprint(args.opt, path))
            for owned, optimize, variant in dom.variants(manifest):
                dom.lower(
                    args,
                    path,
                    variant,
                    f"{name}-rooted-{label}-{owned}-{optimize}",
                    optimize=optimize,
                )
            path.write_text(invalid)
            manifest = dict(declared, module_sha256=dom.fingerprint(args.opt, path))
            original = source_operations(args, invalid)
            for owned, optimize, variant in dom.variants(manifest):
                diagnostic = dom.lower(
                    args,
                    path,
                    variant,
                    f"{name}-late-root-{label}-{owned}-{optimize}",
                    optimize=optimize,
                    success=False,
                    max_steps=1000000,
                )
                assert source_operations(args, diagnostic) == original, diagnostic
                refusals += 1
        if name != "local-string":
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
        for label, changed in mixed.verifier_mutations(ir.read_text()):
            path = args.work / f"verifier-{label}.mlir"
            path.write_text(changed)
            result = run([args.opt, str(path), "-o", "/dev/null"], success=False)
            assert result.returncode and "enclosing_indices" in result.stderr, result.stderr
            verifier_controls += 1
    for name, method in BAD.items():
        ir, contract = dom.prepare(
            args, name, callbacks.source(method + captures.CALLS), 2, entry_name="probe"
        )
        if name == "string-return":
            _, _, _, leaves, _, indices = mixed.shape(ir.read_text())
            assert re.search(rf"ctjs\.store_upvalue %\w+\[{indices[0]}\]", leaves[0][0])
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
        f"DOM class fixed String captures: {len(CASES)} Node/VM sources, "
        f"{executions} native executions, {refusals} refusals, {verifier_controls} verifier controls"
    )


if __name__ == "__main__":
    main()
