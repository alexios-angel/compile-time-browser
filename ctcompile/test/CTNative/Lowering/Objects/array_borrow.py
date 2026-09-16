#!/usr/bin/env python3
"""Prove SCF vector borrows preserve entry-owner lifetime and JavaScript identity."""

import argparse
import hashlib
import json
from pathlib import Path
import re

from CTNative.harness import find_compilers, run
from Target.Cpp.harness import FLAGS, function_body

ORIGINAL_SHA256 = "ac17441800c02afebc9c0f2ca529984c3fd21653ba676eff991ade6028daf490"
NODE = """const fs = require('fs'), vm = require('vm');
const scope = {};
vm.runInNewContext(fs.readFileSync(process.argv[1], 'utf8'), scope);
for (const name of JSON.parse(process.argv[2])) console.log(name + '=' + scope[name]);
"""
CLEANUP = (
    "emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,"
    "canonicalize,ctnative-prune-dead-stores,canonicalize)"
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt", "node", "reference"):
        parser.add_argument("--" + name, required=True)
    for name in ("original", "fixtures", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    original = args.original.read_text().split("//--- alias.js\n", 1)[1]
    if hashlib.sha256(original.encode()).hexdigest() != ORIGINAL_SHA256:
        raise RuntimeError("the preserved 165-byte alias.js source changed")
    source = args.work / "original.js"
    source.write_text(original)
    both = args.work / "original-both.js"
    both.write_text(original + "var observedFalse = blocked(false);\n")
    positives = {
        "original": (source, "blocked_1", {"observed": 2}),
        "original-both": (both, "blocked_1", {"observed": 2, "observedFalse": 2}),
        "unequal": (args.fixtures / "unequal.js", "selected_1", {"left": 297, "right": 322}),
        "nested": (
            args.fixtures / "nested.js",
            "nested_1",
            {"left": 2134, "middle": 3214, "right": 4231},
        ),
        "selected-counted": (
            args.fixtures / "selected-counted.js",
            "counted_1",
            {"left": 34, "right": 204},
        ),
    }
    counted = (args.fixtures / "counted.js").read_text()
    for trips, expected in enumerate((2935, 33195, 312935, 3133195)):
        source = args.work / f"counted-{trips}.js"
        source.write_text(
            counted.replace("guard = [0, 0, 0]", "guard = [" + ", ".join(["0"] * trips) + "]")
        )
        positives[f"counted-{trips}"] = (source, "counted_1", {"observed": expected})
    source = args.work / "counted-stride.js"
    source.write_text(counted.replace("++i", "i += 2"))
    positives["counted-stride"] = (source, "counted_1", {"observed": 312935})
    compilers = find_compilers()
    checked = 0
    mutations = 0
    for name, (source, function, observations) in positives.items():
        expected = "".join(f"{key}={value}\n" for key, value in sorted(observations.items()))
        node = run([args.node, "-e", NODE, str(source), json.dumps(sorted(observations))])
        reference = run([args.reference, str(source)])
        if node.stdout != expected or node.stderr or reference.stdout != expected:
            raise RuntimeError(f"{name}: Node/interpreter source observations changed")
        raw = args.work / f"{name}.raw.mlir"
        run([args.translate, "--ctbrowser-js-to-ctjs", str(source), "-o", str(raw)])
        for optimize in (False, True):
            prefix = f"{name}.{optimize}"
            native = args.work / f"{prefix}.native.mlir"
            run(
                [
                    args.opt,
                    str(raw),
                    "--ctjs-resolve-globals",
                    "--ctjs-lift-to-scf",
                    f"--ctnative-lower-to-emitc=optimize={str(optimize).lower()}",
                    "-o",
                    str(native),
                ]
            )
            ir = native.read_text()
            if "ctjs.func" in ir or "ctnative.not_native" in ir or "ctjs.skipped" in ir:
                raise RuntimeError(f"{prefix}: source did not lower completely\n{ir}")
            if '!emitc.ptr<!emitc.opaque<"std::vector<double>">>' not in ir:
                raise RuntimeError(f"{prefix}: selection lost its borrowed vector pointer")
            cleaned = args.work / f"{prefix}.cleaned.mlir"
            run(
                [
                    args.opt,
                    str(native),
                    "--pass-pipeline=builtin.module(" + CLEANUP + ")",
                    "-o",
                    str(cleaned),
                ]
            )
            deduced = args.work / f"{prefix}.deduced.mlir"
            run([args.opt, str(cleaned), "--ctnative-print-deduced", "-o", str(deduced)])
            for layout, module in (("explicit", cleaned), ("deduced", deduced)):
                cpp = run([args.translate, "--mlir-to-cpp", str(module)]).stdout
                if "ctbrowser::" in cpp:
                    raise RuntimeError(f"{prefix}/{layout}: generated code reaches the runtime")
                body = function_body(cpp, function)
                owners = (
                    4
                    if name == "selected-counted"
                    else 3 if name == "nested" or name.startswith("counted-") else 2
                )
                if len(re.findall(r"std::vector<double>\s+[A-Za-z_]\w*\s*;", body)) != owners:
                    raise RuntimeError(
                        f"{prefix}/{layout}: expected {owners} owning vectors\n{body}"
                    )
                borrowed = owners - 1 if "counted" in name else owners
                if len(re.findall(r"&[A-Za-z_]\w*", body)) != borrowed:
                    raise RuntimeError(
                        f"{prefix}/{layout}: expected {borrowed} borrowed owners\n{body}"
                    )
                file = args.work / f"{prefix}.{layout}.cpp"
                file.write_text(cpp)
                for index, compiler in enumerate(compilers):
                    binary = args.work / f"{prefix}.{layout}.{index}"
                    run([compiler, *FLAGS, str(file), "-o", str(binary)])
                    actual = run([str(binary.resolve())]).stdout
                    if actual != expected:
                        raise RuntimeError(f"{prefix}/{layout}: {actual!r} != {expected!r}")
                    checked += 1
                if name == "original":
                    # A vector copy still compiles but clears neither source owner.
                    changed, count = re.subn(
                        r"\b([A-Za-z_]\w*)->resize\(",
                        r"std::vector<double> ctnative_copy = *\1;\nctnative_copy.resize(",
                        cpp,
                    )
                    if count != 1:
                        raise RuntimeError("copy mutation did not find exactly one borrowed shrink")
                    file = args.work / f"{prefix}.{layout}.copy.cpp"
                    file.write_text(changed)
                    binary = args.work / f"{prefix}.{layout}.copy"
                    run([compilers[0], *FLAGS, str(file), "-o", str(binary)])
                    if run([str(binary.resolve())]).stdout != "observed=4\n":
                        raise RuntimeError(
                            "copy mutation did not expose the original identity loss"
                        )
                    mutations += 1
                if name.startswith("counted-"):
                    # Replacing the post-loop borrowed write by a copy preserves
                    # the loop's scalar work but must fail its owner observations.
                    changed, count = re.subn(
                        r"\(\*([A-Za-z_]\w*)\)\[",
                        r"std::vector<double> ctnative_copy = *\1;\nctnative_copy[",
                        cpp,
                    )
                    if count != 1:
                        raise RuntimeError("loop copy mutation did not find one borrowed write")
                    file = args.work / f"{prefix}.{layout}.copy.cpp"
                    file.write_text(changed)
                    binary = args.work / f"{prefix}.{layout}.copy"
                    run([compilers[0], *FLAGS, str(file), "-o", str(binary)])
                    if run([str(binary.resolve())]).stdout == expected:
                        raise RuntimeError("loop copy mutation concealed owner identity loss")
                    mutations += 1
    refusals = 0
    negatives = {
        "loop-bound": counted.replace("i < guard.length", "i < 3"),
        "loop-step": counted.replace("++i", 'i += "2"'),
        "loop-mutation": counted.replace("var saved = left;", "left[0] = 7; var saved = left;"),
        "loop-region": counted.replace("left = right;", "left = [7, 8];"),
        "loop-mixed": counted.replace("b = [3, 4, 5]", 'b = ["three", "four", "five"]'),
        "loop-external": counted.replace("function counted()", "function counted(a)")
        .replace("var a = [1, 2], b", "var b")
        .replace("counted();", "counted([1, 2]);"),
        "loop-returned": counted.replace(
            "return sum * 10000", "return left;\n  return sum * 10000"
        ).replace("counted();", "counted().length;"),
        "loop-offset": counted.replace("left[0];", "left[i + 1];"),
        "loop-zero-effect": counted.replace("guard = [0, 0, 0]", "guard = []").replace(
            "var saved = left;", "held = left; var saved = left;"
        ),
    }
    for name, text in negatives.items():
        source = args.work / f"{name}.js"
        source.write_text(text)
    sources = [
        source
        for source in sorted(args.fixtures.glob("*.js"))
        if source.stem not in ("unequal", "nested", "counted", "selected-counted")
    ]
    sources += [args.work / f"{name}.js" for name in negatives]
    for source in sources:
        raw = args.work / f"{source.stem}.raw.mlir"
        run([args.translate, "--ctbrowser-js-to-ctjs", str(source), "-o", str(raw)])
        for optimize in (False, True):
            result = run(
                [
                    args.opt,
                    str(raw),
                    "--ctjs-resolve-globals",
                    "--ctjs-lift-to-scf",
                    f"--ctnative-lower-to-emitc=optimize={str(optimize).lower()}",
                ]
            )
            if "ctnative.not_native" not in result.stdout or "emitc.func" in result.stdout:
                raise RuntimeError(f"{source.stem}: unproved borrow was admitted\n{result.stdout}")
            refusals += 1
    print(
        f"array borrows: {checked} native executions, {mutations} copy controls, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
