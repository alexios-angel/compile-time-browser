#!/usr/bin/env python3
"""Check raw-argument refusals and observer-free constructor companions."""

import argparse
import os
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from CTNative.harness import CLEANUP, FLAGS, find_compilers, run

NODE = """const fs = require('fs'), vm = require('vm'), scope = {};
vm.runInNewContext(fs.readFileSync(process.argv[1], 'utf8'), scope);
console.log('a=' + scope.a);
"""
RAW = "surplus constructor arguments are observed by arguments or a rest parameter"
FORWARD = (
    "surplus constructor arguments require an explicit, uncopied constructor "
    "with declared parameters"
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt", "node", "reference"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    compilers = find_compilers()
    args.work.mkdir(parents=True, exist_ok=True)

    def lower(source, prefix):
        imported = run([args.translate, "--ctbrowser-js-to-ctjs", str(source)]).stdout
        result = run(
            [
                args.opt,
                "--ctjs-resolve-globals",
                "--ctjs-lift-to-scf",
                "--ctnative-lower-to-emitc=optimize=false observe-globals=true",
            ],
            input_text=imported,
        ).stdout
        (args.work / (prefix + ".mlir")).write_text(result)
        return result

    def check(case):
        name, observer, expected = case
        source = args.fixtures / (name + ".js")
        text = source.read_text()
        refused = lower(source, name)
        reason = FORWARD if name == "derived" else RAW
        if 'ctnative.closure_reason = "' + reason + '"' not in refused:
            raise RuntimeError(
                f"{name}: constructor did not refuse at its surplus guard\n{refused}"
            )
        for command in ([args.node, "-e", NODE, str(source)], [args.reference, str(source)]):
            if "a=" + str(expected) not in run(command).stdout.splitlines():
                raise RuntimeError(f"{name}: raw argument reference changed")
        if name == "derived":
            companion = text.replace(
                "class Leaf extends Base {}",
                "class Leaf extends Base { constructor(n) { super(n); } }",
            )
            assert companion != text
        else:
            assert text.count(observer) == 1
            companion = text.replace(observer, "n")
            if name == "rest":
                # The rest declaration itself copies the raw window even when unused.
                assert companion.count("n, ...rest") == 1
                companion = companion.replace("n, ...rest", "n")
        positive = args.work / (name + ".positive.js")
        positive.write_text(companion)
        native = lower(positive, name + ".positive")
        if "ctnative.not_native" in native or "ctjs.func" in native:
            raise RuntimeError(f"{name}: observer-free companion is not native\n{native}")
        clean = run(
            [args.opt, "--pass-pipeline=builtin.module(" + CLEANUP + ")"], input_text=native
        ).stdout
        cpp = args.work / (name + ".cpp")
        cpp.write_text(run([args.translate, "--mlir-to-cpp"], input_text=clean).stdout)
        if "ctbrowser::script" in cpp.read_text():
            raise RuntimeError(f"{name}: native companion retains the VM")
        for index, compiler in enumerate(compilers):
            binary = args.work / (name + "." + str(index))
            run([compiler, *FLAGS, str(cpp), "-o", str(binary)])
            if run([str(binary)]).stdout != "a=1\n":
                raise RuntimeError(f"{name}: native companion changed the declared argument")
        return name + ": guarded refusal, VM/Node raw value, GCC/Clang companion passed"

    cases = [
        ("length", "arguments.length", 2),
        ("index", "arguments[1]", 2),
        ("rest", "rest[0]", 2),
        ("derived", "", 1),
    ]
    with ThreadPoolExecutor(max_workers=min(len(cases), os.cpu_count() or 1)) as pool:
        for message in pool.map(check, cases):
            print(message)


if __name__ == "__main__":
    main()
