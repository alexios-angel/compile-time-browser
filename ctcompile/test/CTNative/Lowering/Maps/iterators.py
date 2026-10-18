#!/usr/bin/env python3
"""Lit test: a Map iterator lowers natively only when consumed immediately.

`Array.from(map.keys())` with no intervening Map mutation becomes a native array
snapshot. Every other use of the iterator (raw access, delayed consumption,
consuming twice, returning it) must be refused, in every pass mode, on a rerun,
and even when the input carries forged snapshot attributes.
"""

import re

from CTNative.Exports import boundary
from CTNative.harness import native_args, run


def lower(args, source, name, count, passes, *, admitted=0):
    """Run `passes` on `source` and check exactly `admitted` of `count` functions
    became native; with none admitted, check the iterator refusal left the IR intact."""
    output = args.work / f"{name}.native.mlir"
    run([args.opt, str(source), *passes, "-o", str(output)])
    text = output.read_text()
    remaining = len(boundary.FUNCTION.findall(text))
    if len(boundary.NATIVE.findall(text)) != admitted or remaining + admitted != count:
        raise RuntimeError(f"{name}: wrong native function count\n{text}")
    if not admitted:
        reasons = boundary.REFUSAL.findall(text)
        if len(reasons) != count or not any("native Map iterator" in r for r in reasons):
            raise RuntimeError(f"{name}: missing iterator refusal\n{text}")
        if (
            "ctnative.map_snapshot_copy" in text
            or "ctnative.map_action" in text
            or "ctnative.partial_evaluated" in text
            or "ctnative::map_snapshot_at" in text
        ):
            raise RuntimeError(f"{name}: refused iterator retained an optimization proof")
        for operation in ("ctjs.construct", "ctjs.call", "ctjs.call_direct"):
            # Refusal diagnostics can name operations too. Count only IR
            # definitions, including direct calls as a distinct operation.
            pattern = rf"^\s*%[^ =]+\s*=\s*{re.escape(operation)}\b"
            before = len(re.findall(pattern, source.read_text(), re.M))
            after = len(re.findall(pattern, text, re.M))
            if before != after:
                raise RuntimeError(
                    f"{name}: refusal changed {operation} count {before} -> {after}\n{text}"
                )
    return output


def main(argv=None):
    args = native_args(__doc__, argv=argv)
    node, reference = args.node, args.reference
    base = "function probe() { const map = new Map(); map.set(1, 10); "
    cases = {
        "raw_length": ("const it = map.keys(); return +(it.length === undefined);", 1),
        "raw_index": ("const it = map.values(); return +(it[0] === undefined);", 1),
        "raw_next": ("const it = map.keys(); return it.next().value;", 1),
        "iterator_write": ("const it = map.keys(); it[0] = 99; return Array.from(it)[0];", 1),
        "delayed_set": (
            "const it = map.keys(); map.set(2, 20); return Array.from(it).length;",
            2,
        ),
        "delayed_delete": (
            "const it = map.values(); map.delete(1); return Array.from(it).length;",
            0,
        ),
        "delayed_clear": (
            "const it = map.keys(); map.clear(); return Array.from(it).length;",
            0,
        ),
        "twice": (
            "const it = map.keys(); const first = Array.from(it); "
            "const second = Array.from(it); return first.length * 10 + second.length;",
            10,
        ),
        "conditional": (
            "const it = map.keys(); if (map.size) { return Array.from(it).length; } return 0;",
            1,
        ),
        "published": ("return map.keys();", 1),
    }
    modes = {
        "plain": ["--ctnative-lower-to-emitc=optimize=false"],
        "optimized": ["--ctnative-lower-to-emitc"],
        "partial": [
            "--ctnative-partial-evaluate",
            "--ctnative-lower-to-emitc=optimize=false",
            "--ctnative-deforest",
        ],
    }
    for name, (body, expected) in cases.items():
        observation = "+(probe().length === undefined)" if name == "published" else "probe()"
        source = base + body + f" }} var trace = {observation};"
        js, prepared, count = boundary.prepare(args, name, source)
        node_result = run([node, "-e", boundary.NODE, str(js)]).stdout
        if node_result != f"trace={expected}\n":
            raise RuntimeError(f"{name}: Node source witness changed: {node_result}")
        vm_result = run([str(reference), str(js)]).stdout
        # Compare the VM with Node only for these cases. Every case is refused
        # natively, so the rest (delayed_*, twice) need no VM-equivalence check,
        # and the VM's live-iterator semantics for them may still change.
        if name in {
            "raw_length",
            "raw_index",
            "raw_next",
            "iterator_write",
            "conditional",
            "published",
        }:
            if vm_result != node_result:
                raise RuntimeError(f"{name}: iterator object witness disagrees: {vm_result}")
        print(f"{name}: Node {node_result.strip()}, VM {vm_result.strip()}; native refused")
        for mode, passes in modes.items():
            output = lower(args, prepared, f"{name}-{mode}", count, passes)
            lower(args, output, f"{name}-{mode}-again", count, modes["plain"])
        forged = args.work / f"{name}.forged.mlir"
        text, changes = re.subn(
            r"^(.* = ctjs.call [^{\n]*)$",
            r'\1 {ctnative.map_snapshot_copy, ctnative.map_action = "keys"}',
            prepared.read_text(),
            flags=re.M,
        )
        if not changes:
            raise RuntimeError(f"{name}: forgery control did not mark any call")
        forged.write_text(text)
        for mode in ("plain", "partial"):
            lower(args, forged, f"{name}-forged-{mode}", count, modes[mode])

    # A named iterator consumed before any Map mutation is equivalent to the
    # nested expression and lowers natively. Both arrays outlive map.clear().
    source = (
        base + "const it = map.keys(); const keys = Array.from(it); "
        "const values = Array.from(map.values()); map.clear(); "
        "return keys[0] + values[0] + keys.length; } var trace = probe();"
    )
    js, prepared, count = boundary.prepare(args, "immediate", source)
    expected = "trace=12\n"
    if (
        run([node, "-e", boundary.NODE, str(js)]).stdout != expected
        or run([str(reference), str(js)]).stdout != expected
    ):
        raise RuntimeError("immediate: snapshot independence reference changed")
    for mode, passes in modes.items():
        lower(args, prepared, f"immediate-{mode}", count, passes, admitted=count)
    print(
        "Map iterators: ten source refusals across default/optimized/partial/deforest, "
        "rerun and forged proofs; immediate consumption remains native"
    )


def test_iterators(lit):
    main(
        lit.args(
            "--translate ctjs-translate --opt ctjs-opt --work %t --node %node --reference %native_reference"
        )
    )


if __name__ == "__main__":
    main()
