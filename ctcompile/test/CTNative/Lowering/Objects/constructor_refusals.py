#!/usr/bin/env python3
"""Pin observable prototype behavior before checking the native refusal boundary."""

import argparse
from pathlib import Path
import re

from CTNative.Exports.boundary import FUNCTION, NATIVE, REFUSAL
from CTNative.harness import find_compilers, run
from Target.Cpp.harness import FLAGS

NODE = """const fs = require('fs'), vm = require('vm');
const scope = {};
vm.runInNewContext(fs.readFileSync(process.argv[1], 'utf8'), scope);
console.log('a=' + scope.a);
"""
OBSERVATIONS = {
    "prototype-written": 2,
    "inherited-call": 7,
    "default-prototype": 7,
    "late-mutation": 9,
    "alias-mutation": 11,
    "helper-mutation": 13,
    "prototype-replacement": 79,
    "mutable-class-helper": 1,
    "new-target": 7,
    "constructor-argument": 7,
    "arrow-constructor": 7,
}
SCALARS = {
    "prototype-written": 2,
    "inherited": 10117,
    "shadow": 117,
    "borrowed": 7,
    "primitives": 1111,
    "literal-primitives": 1111,
    "boolean-default": 1,
}


def check_native(args, source, name, expected):
    checked = 0
    raw = args.work / f"{name}.raw.mlir"
    run([args.translate, "--ctbrowser-js-to-ctjs", str(source), "-o", str(raw)])
    for optimize in (False, True):
        native = args.work / f"{name}.{optimize}.mlir"
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
        if any(
            token in native.read_text()
            for token in ("ctjs.func", "ctnative.not_native", "ctjs.skipped")
        ):
            raise RuntimeError(f"{name}: scalar prototype did not lower\n{native.read_text()}")
        clean = args.work / f"{name}.{optimize}.clean.mlir"
        run(
            [
                args.opt,
                str(native),
                "--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))",
                "-o",
                str(clean),
            ]
        )
        deduced = args.work / f"{name}.{optimize}.deduced.mlir"
        run([args.opt, str(clean), "--ctnative-print-deduced", "-o", str(deduced)])
        for layout, module in (("explicit", clean), ("deduced", deduced)):
            cpp = run([args.translate, "--mlir-to-cpp", str(module)]).stdout
            if "ctbrowser::" in cpp or '"prototype"' in cpp:
                raise RuntimeError(
                    f"{name}: native scalar fields retained runtime/prototype storage"
                )
            file = args.work / f"{name}.{optimize}.{layout}.cpp"
            file.write_text(cpp)
            for index, compiler in enumerate(find_compilers()):
                binary = file.with_suffix(f".{index}")
                run([compiler, *FLAGS, str(file), "-o", str(binary)])
                if run([str(binary.resolve())]).stdout != f"a={expected}\n":
                    raise RuntimeError(f"{name}: native scalar prototype observations differ")
                checked += 1
    return checked


def check_mutable_helper(text):
    loaded = re.search(r'(%\w+) = ctjs.load_global "__ctbrowser_class_defined"', text)
    if loaded is None or not re.search(r"ctjs.call " + re.escape(loaded[1]) + r"\(", text):
        raise RuntimeError("the mutable class-definition hook lost its ordinary global call")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scalars", action="store_true")
    for name in ("translate", "opt", "node", "reference"):
        parser.add_argument("--" + name, required=True)
    for name in ("fixtures", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    refusals = 0
    observations = SCALARS if args.scalars else OBSERVATIONS
    checked = 0
    for name, expected in observations.items():
        source = args.fixtures / f"{name}.js"
        node_expected = 0 if name == "mutable-class-helper" else expected
        node = run([args.node, "-e", NODE, str(source)], success=name != "arrow-constructor")
        reference = run([args.reference, str(source)])
        if name == "arrow-constructor":
            if "TypeError" not in node.stderr or node.stdout:
                raise RuntimeError(f"{name}: Node did not throw TypeError")
        elif node.stdout != f"a={node_expected}\n" or node.stderr:
            raise RuntimeError(f"{name}: Node observation changed\n{node.stdout}{node.stderr}")
        if reference.stdout != f"a={expected}\n":
            raise RuntimeError(
                f"{name}: interpreter observation changed\n{reference.stdout}{reference.stderr}"
            )
        if args.scalars and name != "primitives":
            checked += check_native(args, source, name, expected)
            continue
        # The original source now has a positive native gate in prototype-scalars.mlir.
        if name == "prototype-written":
            continue
        raw = args.work / f"{name}.raw.mlir"
        imported = run([args.translate, "--ctbrowser-js-to-ctjs", str(source), "-o", str(raw)])
        text = raw.read_text()
        if "ctjs.skipped" in text or "is not compiled:" in imported.stderr:
            raise RuntimeError(f"{name}: importer skipped source functions")
        functions = len(FUNCTION.findall(text))
        if name == "mutable-class-helper":
            check_mutable_helper(text)
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
            text = result.stdout
            remaining = len(FUNCTION.findall(text))
            reasons = REFUSAL.findall(text)
            if (
                remaining + len(NATIVE.findall(text)) != functions
                or len(reasons) != remaining
                or not all(reasons)
                or not re.search(r"ctjs.func @_script_\$0\(.*ctnative.not_native", text)
                or re.search(r"emitc.func @main\(", text)
            ):
                raise RuntimeError(f"{name}/{optimize}: lost native refusal boundary\n{text}")
            if name == "mutable-class-helper":
                check_mutable_helper(text)
            if (
                name == "arrow-constructor"
                and "cannot be constructed even without lexical this reads" not in text
            ):
                raise RuntimeError("the constructor proof lost its unconditional arrow refusal")
            refusals += 1
    print(
        f"constructor controls: {len(observations)} source observations, {checked} native executions, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
