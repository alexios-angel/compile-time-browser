#!/usr/bin/env python3
"""Prove ordinary instanceof before class prototypes become structural C++ shapes."""

import argparse
from pathlib import Path

from CTNative.harness import run
from CTNative.HostContract import contract as host
from CTNative.Lowering.Objects import class_initialization as classes

SAME = """class A { constructor(n) { this.n = n; } }
const value = new A(7);
return +(value instanceof A);
"""
DIFFERENT = """class A { constructor(n) { this.n = n; } }
class B { constructor(n) { this.n = n; } }
const left = new A(7), right = new B(9);
return left.n + right.n + +(left instanceof B) + +(right instanceof A);
"""
CASES = {
    "same": (SAME, 1),
    "same_shape": (DIFFERENT, 16),
    "unconstructed_rhs": (
        DIFFERENT.replace(", right = new B(9)", "").replace(
            "left.n + right.n + +(left instanceof B) + +(right instanceof A)",
            "left.n + +(left instanceof B)",
        ),
        7,
    ),
    "alias": (
        SAME.replace("return +", "const alias = value; return +").replace(
            "(value instanceof A)", "(alias instanceof A)"
        ),
        1,
    ),
    "effects": (
        SAME.replace("this.n = n;", "this.n = n; this.n = this.n * 10 + 1;").replace(
            "return +", "value.n = value.n + 1; return value.n * 10 + +"
        ),
        721,
    ),
    "derived": (
        """class A { constructor(n) { this.n = n; } }
class B extends A { constructor(n) { super(n); } }
const value = new B(7);
return value.n * 100 + +(value instanceof A) + 10 * +(value instanceof B);
""",
        711,
    ),
}
REFUSALS = {
    "prototype_replaced": SAME.replace("const value", "A.prototype = {}; const value"),
    "prototype_alias": SAME.replace(
        "const value", "const prototype = A.prototype; prototype.changed = true; const value"
    ),
    "custom_hook": SAME.replace(
        "const value",
        "Object.defineProperty(A, Symbol.hasInstance, {value() { return false; }}); const value",
    ),
    "function_replaced": SAME.replace("const value", "Function = null; const value"),
    "default_hook_replaced": SAME.replace(
        "const value",
        "Object.defineProperty(Function.prototype, Symbol.hasInstance, "
        "{value() { return false; }}); const value",
    ),
    "replacement_return": SAME.replace("this.n = n;", "this.n = n; return {n};"),
    "unknown_origin": SAME.replace("(value instanceof A)", "({n: 7} instanceof A)"),
    "invalid_rhs": SAME.replace("(value instanceof A)", "(value instanceof 7)"),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt", "node", "reference"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    native_runs = refusals = 0
    for name, (body, expected) in (
        CASES | {name: (body, None) for name, body in REFUSALS.items()}
    ).items():
        source = args.work / f"{name}.js"
        source.write_text("function probe() {\n" + body + "}\nvar a = probe();\n")
        if expected is not None:
            for command in (
                [args.node, "-e", classes.NODE, str(source)],
                [args.reference, str(source)],
            ):
                if run(command).stdout != f"a={expected}\n":
                    raise RuntimeError(f"{name}: source observation changed")
        raw = args.work / f"{name}.raw.mlir"
        imported = run([args.translate, "--ctbrowser-js-to-ctjs", str(source), "-o", str(raw)])
        if "ctjs.skipped" in raw.read_text() or "is not compiled:" in imported.stderr:
            raise RuntimeError(f"{name}: importer skipped source")
        structured = args.work / f"{name}.structured.mlir"
        run(
            [
                args.opt,
                str(raw),
                "--ctjs-resolve-globals",
                "--ctjs-lift-to-scf",
                "--mlir-print-op-generic",
                "-o",
                str(structured),
            ]
        )
        identities = ["__ctbrowser_class_defined", "Function"]
        if name == "derived":
            identities += [
                "__ctbrowser_class_heritage",
                "__ctbrowser_bind_this",
                "__ctbrowser_init_fields",
                "__ctbrowser_super_get",
            ]
        manifest = dict(host.manifest(args.opt, structured), initial_intrinsics=identities)
        before = structured.read_text()
        if '"ctjs.instanceof"' not in before:
            raise RuntimeError(f"{name}: importer lost the original instanceof")
        prepared = classes.prepare(args, name, structured, manifest, success=expected is not None)
        if expected is None:
            refusals += 1
            continue
        after = prepared.read_text()
        # The importer also constructs Errors in unreachable super guards.
        # Count the explicit source constructions whose effects must remain.
        if "ctjs.instanceof" in after or after.count("ctjs.construct") != body.count("new "):
            raise RuntimeError(f"{name}: preparation lost a constructor or retained instanceof")
        for control, changed, options, diagnostic in (
            (
                "missing_function",
                dict(manifest, initial_intrinsics=[i for i in identities if i != "Function"]),
                "",
                "class instanceof requires the standard Function identity",
            ),
            ("stale", dict(manifest, module_sha256="0" * 64), "", "fingerprint mismatch"),
            ("budget", manifest, "max-steps=0", "budget exhausted"),
        ):
            classes.prepare(
                args,
                f"{name}-{control}",
                structured,
                changed,
                success=False,
                options=options,
                diagnostic=diagnostic,
            )
            refusals += 1
        for optimize in (False, True):
            native = args.work / f"{name}.{optimize}.native.mlir"
            run(
                [
                    args.opt,
                    str(prepared),
                    f"--ctnative-lower-to-emitc=optimize={str(optimize).lower()}",
                    "-o",
                    str(native),
                ]
            )
            native_runs += classes.check_executable(args, f"{name}.{optimize}", native, expected)
    print(
        f"ordinary instanceof: {len(CASES)} Node/VM cases, {native_runs} native runs, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
