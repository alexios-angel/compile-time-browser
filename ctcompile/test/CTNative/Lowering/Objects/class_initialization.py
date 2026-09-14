#!/usr/bin/env python3
"""Keep class initialization, mutable helper effects and native admission distinct."""

import argparse
import json
from pathlib import Path
import re
import subprocess

from CTNative.Exports.boundary import FUNCTION, NATIVE, REFUSAL
from CTNative.HostContract import contract as host
from CTNative.harness import find_compilers, run
from CTNative.Lowering.Objects.constructor_refusals import NODE, check_mutable_helper, check_native
from Target.Cpp.harness import FLAGS

# The implementation hook and inherited static getter have separate Node/VM
# observations. A native refusal is not permission to equate the two engines.
OBSERVATIONS = {
    "empty": (7, 7),
    "number": (92, 92),
    "method": (92, 92),
    "method-arguments": (3737, 3737),
    "method-empty": (7, 7),
    "method-shadow": (9, 9),
    "method-extracted": (1, 1),
    "method-constructor-read": (1, 1),
    "method-constructor-write": (9, 9),
    "method-self-replace": (79, 79),
    "method-duplicate": (9, 9),
    "method-captured": (7, 7),
    "method-dynamic": (7, 7),
    "method-return-object": (9, 9),
    "helper-override": (0, 1),
    "helper-late": (0, 1),
    "helper-global-alias": (0, 1),
    "prototype-late": (9, 9),
    "prototype-alias": (11, 11),
    "field-initializer": (7, 7),
    "inherited": (7, 7),
    "static-getter": (1, 0),
    "constructor-identity": (1, 1),
    "descriptor": (0, 0),
}
POSITIVES = {"empty", "number", "method", "method-arguments", "method-empty"}
PREPARATION = "--ctnative-specialize-class-initialization="


def check_constructed_methods(args):
    checked = refused = 0
    for name, expected in {
        "plain-method": 7,
        "plain-before-store": None,
        "plain-borrowed-write": 9,
        "plain-self-replace": 79,
        "plain-constructor-write": 7,
        "plain-detached": 1,
    }.items():
        source = args.fixtures / f"{name}.js"
        for command in ([args.node, "-e", NODE, str(source)], [args.reference, str(source)]):
            result = run(command, success=expected is not None)
            if expected is not None and result.stdout != f"a={expected}\n":
                raise RuntimeError(f"{name}: constructed method observation changed")
        if name == "plain-method":
            checked += check_native(args, source, name, expected)
            continue
        raw = args.work / f"{name}.raw.mlir"
        run([args.translate, "--ctbrowser-js-to-ctjs", str(source), "-o", str(raw)])
        functions = len(FUNCTION.findall(raw.read_text()))
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
            check_refusal(name, result.stdout, functions)
            refused += 1
    return checked, refused


def check_refusal(name, text, functions):
    remaining = len(FUNCTION.findall(text))
    reasons = REFUSAL.findall(text)
    if (
        remaining + len(NATIVE.findall(text)) != functions
        or len(reasons) != remaining
        or not all(reasons)
        or not re.search(r"ctjs.func @_script_\$0\(.*ctnative.not_native", text)
        or re.search(r"emitc.func @main\(", text)
    ):
        raise RuntimeError(f"{name}: lost named native refusal boundary\n{text}")


def prepare(args, name, source, manifest, *, success, options=""):
    config = args.work / f"{name}.json"
    output = args.work / f"{name}.prepared.mlir"
    config.write_text(json.dumps(manifest, indent=2) + "\n")
    result = run(
        [
            args.opt,
            str(source),
            PREPARATION + f"manifest={config} {options}",
            "-o",
            str(output),
        ],
        success=success,
    )
    if not success and (
        result.returncode != 1
        or "error:" not in result.stderr
        or (output.exists() and output.read_text())
    ):
        raise RuntimeError(f"{name}: preparation failed without a diagnostic or emitted partial IR")
    return output


def check_proof_inputs(args, source, manifest, prepared, name):
    text = source.read_text()
    nested = args.work / "nested.mlir"
    changed, count = re.subn(
        r"(^[ \t]*ctjs.return [^\n]*\n)",
        "    builtin.module {\n"
        "      ctjs.func private @external$999() -> !ctjs.value attributes {upvalue_count = 0 : i32}\n"
        "    }\n\\1",
        text,
        count=1,
        flags=re.M,
    )
    if count != 1:
        raise RuntimeError("nested function control could not find the script return")
    nested.write_text(changed)
    prepare(
        args,
        "nested",
        nested,
        dict(manifest, module_sha256=host.fingerprint(args.opt, nested)),
        success=False,
    )
    duplicate = args.work / "duplicate-closure.mlir"
    changed, count = re.subn(
        r"(^[ \t]*)%\w+( = ctjs.create_closure[^\n]*\n)",
        r"\g<0>\1%duplicate\2",
        text,
        count=1,
        flags=re.M,
    )
    if count != 1:
        raise RuntimeError("duplicate closure control could not find its creation")
    duplicate.write_text(changed)
    prepare(
        args,
        "duplicate-closure",
        duplicate,
        dict(manifest, module_sha256=host.fingerprint(args.opt, duplicate)),
        success=False,
    )
    forged = args.work / "forged.mlir"
    changed, count = re.subn(
        r"(\bctjs.func[^\n]*\battributes \{)",
        r'\1ctnative.not_native = "forged", ctnative.forged = true, ',
        text,
    )
    if count != len(FUNCTION.findall(text)):
        raise RuntimeError("forged annotation control did not mark every source function")
    forged.write_text(changed)
    signed = dict(manifest, module_sha256=host.fingerprint(args.opt, forged))
    prepare(args, "forged-untrusted", forged, dict(signed, initial_intrinsics=[]), success=False)
    output = prepare(args, "forged-trusted", forged, signed, success=True)
    if output.read_text() != prepared.read_text():
        raise RuntimeError("supplied native annotations survived successful live reanalysis")

    # Find and pin the exact first complete census. Each unsuccessful limit
    # must withhold the entire rewrite; successful limits produce identical IR.
    low, high = 0, 100000
    while high - low > 1:
        limit = (low + high) // 2
        output = args.work / f"cutoff-{limit}.mlir"
        result = subprocess.run(
            [
                args.opt,
                str(source),
                PREPARATION + f"manifest={args.work / (name + '.json')} max-steps={limit}",
                "-o",
                str(output),
            ],
            capture_output=True,
            text=True,
            timeout=120,
        )
        if result.returncode == 0:
            if output.read_text() != prepared.read_text():
                raise RuntimeError("work limit changed successful class initialization IR")
            high = limit
        else:
            if (
                result.returncode != 1
                or "class initialization work budget exhausted" not in result.stderr
                or (output.exists() and output.read_text())
            ):
                raise RuntimeError(f"work limit {limit} crashed or published partial proof")
            low = limit
    prepare(args, "last-cutoff", source, manifest, success=False, options=f"max-steps={low}")
    output = prepare(
        args, "first-complete", source, manifest, success=True, options=f"max-steps={high}"
    )
    if output.read_text() != prepared.read_text():
        raise RuntimeError("first complete work budget changed the prepared program")
    return high


def check_executable(args, name, native, expected):
    text = native.read_text()
    if any(token in text for token in ("ctjs.func", "ctnative.not_native", "ctjs.skipped")):
        raise RuntimeError(f"{name}: trusted class did not lower completely\n{text}")
    clean = args.work / f"{name}.clean.mlir"
    run(
        [
            args.opt,
            str(native),
            "--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))",
            "-o",
            str(clean),
        ]
    )
    deduced = args.work / f"{name}.deduced.mlir"
    run([args.opt, str(clean), "--ctnative-print-deduced", "-o", str(deduced)])
    checked = 0
    for layout, module in (("explicit", clean), ("deduced", deduced)):
        cpp = run([args.translate, "--mlir-to-cpp", str(module)]).stdout
        if any(token in cpp for token in ("ctbrowser::", '"prototype"', '"__home"')):
            raise RuntimeError(f"{name}: native class retained runtime or prototype storage")
        file = args.work / f"{name}.{layout}.cpp"
        file.write_text(cpp)
        for index, compiler in enumerate(find_compilers()):
            binary = file.with_suffix(f".{index}")
            run([compiler, *FLAGS, str(file), "-o", str(binary)])
            if run([str(binary.resolve())]).stdout != f"a={expected}\n":
                raise RuntimeError(f"{name}/{layout}: native class observations differ")
            checked += 1
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt", "node", "reference"):
        parser.add_argument("--" + name, required=True)
    for name in ("fixtures", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    refusals = 0
    preparation_refusals = 0
    checked = 0
    cutoffs = {}
    for name, (node_expected, reference_expected) in OBSERVATIONS.items():
        source = args.fixtures / f"{name}.js"
        node = run([args.node, "-e", NODE, str(source)])
        reference = run([args.reference, str(source)])
        if node.stdout != f"a={node_expected}\n" or node.stderr:
            raise RuntimeError(f"{name}: Node observation changed\n{node.stdout}{node.stderr}")
        if reference.stdout != f"a={reference_expected}\n":
            raise RuntimeError(
                f"{name}: interpreter observation changed\n{reference.stdout}{reference.stderr}"
            )
        raw = args.work / f"{name}.raw.mlir"
        imported = run([args.translate, "--ctbrowser-js-to-ctjs", str(source), "-o", str(raw)])
        text = raw.read_text()
        if "ctjs.skipped" in text or "is not compiled:" in imported.stderr:
            raise RuntimeError(f"{name}: importer skipped source functions")
        check_mutable_helper(text)
        functions = len(FUNCTION.findall(text))
        structured = args.work / f"{name}.structured.mlir"
        run(
            [
                args.opt,
                str(raw),
                "--ctjs-resolve-globals",
                "--ctjs-lift-to-scf",
                "-o",
                str(structured),
            ]
        )
        manifest = dict(
            host.manifest(args.opt, structured),
            initial_intrinsics=["__ctbrowser_class_defined"],
        )
        prepared = prepare(args, name, structured, manifest, success=name in POSITIVES)
        preparation_refusals += name not in POSITIVES
        if name == "empty":
            for label, control, options in (
                ("no-authority", dict(manifest, initial_intrinsics=[]), ""),
                ("stale", dict(manifest, module_sha256="0" * 64), ""),
                ("budget", manifest, "max-steps=0"),
            ):
                prepare(args, label, structured, control, success=False, options=options)
                preparation_refusals += 1
        if name in ("empty", "method"):
            cutoffs[name] = check_proof_inputs(args, structured, manifest, prepared, name)
            preparation_refusals += 4
        for optimize in (False, True):
            native = args.work / f"{name}.{optimize}.untrusted.mlir"
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
            check_refusal(f"{name}/{optimize}/untrusted", native.read_text(), functions)
            if name == "helper-global-alias":
                check_mutable_helper(native.read_text())
            refusals += 1
            if name in POSITIVES:
                native = args.work / f"{name}.{optimize}.trusted.mlir"
                run(
                    [
                        args.opt,
                        str(prepared),
                        f"--ctnative-lower-to-emitc=optimize={str(optimize).lower()}",
                        "-o",
                        str(native),
                    ]
                )
                checked += check_executable(args, f"{name}.{optimize}", native, node_expected)
    plain_checked, plain_refused = check_constructed_methods(args)
    print(
        f"class initialization controls: {len(OBSERVATIONS)} source observations, "
        f"{checked} native executions, {refusals} unprepared refusals, "
        f"{preparation_refusals} preparation refusals, first complete budgets {cutoffs}"
    )
    print(
        f"constructed method controls: {plain_checked} native executions, {plain_refused} refusals"
    )


if __name__ == "__main__":
    main()
