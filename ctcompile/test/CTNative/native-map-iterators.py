#!/usr/bin/env python3
"""Require immediate materialization before a Map iterator becomes a native array."""

import argparse
import importlib.util
from pathlib import Path
import re


spec = importlib.util.spec_from_file_location(
    "boundary", Path(__file__).with_name("native-export-boundary.py"))
boundary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(boundary)
run = boundary.host.run


def lower(args, source, name, count, passes, *, admitted=0):
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
        if ('ctnative.map_snapshot_copy' in text or 'ctnative.map_action' in text
                or 'ctnative.partial_evaluated' in text or 'ctnative::map_snapshot_at' in text):
            raise RuntimeError(f"{name}: refused iterator retained an optimization proof")
        for operation in ("ctjs.construct", "ctjs.call", "ctjs.call_direct"):
            # Refusal diagnostics can name operations too. Count only IR
            # definitions, including direct calls as a distinct operation.
            pattern = rf"^\s*%[^ =]+\s*=\s*{re.escape(operation)}\b"
            before = len(re.findall(pattern, source.read_text(), re.M))
            after = len(re.findall(pattern, text, re.M))
            if before != after:
                raise RuntimeError(f"{name}: refusal changed {operation} count "
                                   f"{before} -> {after}\n{text}")
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--node")
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    node = boundary.node_executable(args)
    reference = boundary.reference_tool(args.opt)
    base = "function probe() { const map = new Map(); map.set(1, 10); "
    cases = {
        "raw_length": ("const it = map.keys(); return +(it.length === undefined);", 1),
        "raw_index": ("const it = map.values(); return +(it[0] === undefined);", 1),
        "raw_next": ("const it = map.keys(); return it.next().value;", 1),
        "iterator_write": ("const it = map.keys(); it[0] = 99; "
                           "return Array.from(it)[0];", 1),
        "delayed_set": ("const it = map.keys(); map.set(2, 20); "
                        "return Array.from(it).length;", 2),
        "delayed_delete": ("const it = map.values(); map.delete(1); "
                           "return Array.from(it).length;", 0),
        "delayed_clear": ("const it = map.keys(); map.clear(); "
                          "return Array.from(it).length;", 0),
        "twice": ("const it = map.keys(); const first = Array.from(it); "
                  "const second = Array.from(it); return first.length * 10 + second.length;", 10),
        "conditional": ("const it = map.keys(); if (map.size) { "
                        "return Array.from(it).length; } return 0;", 1),
        "published": ("return map.keys();", 1),
    }
    modes = {
        "plain": ["--ctnative-lower-to-emitc=optimize=false"],
        "optimized": ["--ctnative-lower-to-emitc"],
        "partial": ["--ctnative-partial-evaluate", "--ctnative-lower-to-emitc=optimize=false",
                    "--ctnative-deforest"],
    }
    for name, (body, expected) in cases.items():
        observation = "+(probe().length === undefined)" if name == "published" else "probe()"
        source = base + body + f" }} var trace = {observation};"
        js, prepared, count = boundary.prepare(args, name, source)
        node_result = run([node, "-e", boundary.NODE, str(js)]).stdout
        if node_result != f"trace={expected}\n":
            raise RuntimeError(f"{name}: Node source witness changed: {node_result}")
        vm_result = run([str(reference), str(js)]).stdout
        # Refused sources need no native/VM equivalence claim. In particular,
        # the VM's iterator storage/consumption semantics may keep improving.
        if name in {"raw_length", "raw_index", "raw_next", "iterator_write", "conditional", "published"}:
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
            prepared.read_text(), flags=re.M)
        if not changes:
            raise RuntimeError(f"{name}: forgery control did not mark any call")
        forged.write_text(text)
        for mode in ("plain", "partial"):
            lower(args, forged, f"{name}-forged-{mode}", count, modes[mode])

    # A named iterator with only inert builtin lookup before consumption is
    # equivalent to the nested expression. Both arrays outlive Map mutation.
    source = (base + "const it = map.keys(); const keys = Array.from(it); "
              "const values = Array.from(map.values()); map.clear(); "
              "return keys[0] + values[0] + keys.length; } var trace = probe();")
    js, prepared, count = boundary.prepare(args, "immediate", source)
    expected = "trace=12\n"
    if (run([node, "-e", boundary.NODE, str(js)]).stdout != expected
            or run([str(reference), str(js)]).stdout != expected):
        raise RuntimeError("immediate: snapshot independence reference changed")
    for mode, passes in modes.items():
        lower(args, prepared, f"immediate-{mode}", count, passes, admitted=count)
    print("Map iterators: ten source refusals across default/optimized/partial/deforest, "
          "rerun and forged proofs; immediate consumption remains native")


if __name__ == "__main__":
    main()
