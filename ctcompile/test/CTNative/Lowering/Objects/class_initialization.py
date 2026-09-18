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

# The implementation hook and function metadata retain separate Node/VM
# observations. Inherited static getter lookup now agrees between the engines.
OBSERVATIONS = {
    "empty": (7, 7),
    "number": (92, 92),
    "method": (92, 92),
    "method-arguments": (3737, 3737),
    "method-branches": (3434, 3434),
    "method-loop": (5555, 5555),
    "method-dispatch": (11131321, 11131321),
    "method-dispatch-ambient": (7, 7),
    "method-dispatch-shadow": (7, 7),
    "method-dispatch-throw": (7, 7),
    "method-branch-ambient": (7, 7),
    "method-branch-shadow": (7, 7),
    "constructor-branch": (7, 7),
    "static-branch": (7, 7),
    "method-empty": (7, 7),
    "method-chain": (92, 92),
    "method-chain-empty": (7, 7),
    "method-chain-effects": (7437, 7437),
    "method-chain-order": (264, 264),
    "method-chain-replace": (9, 9),
    "method-chain-argument-replace": (79, 79),
    "method-chain-identity": (1, 1),
    "method-chain-argument-receiver": (9, 9),
    "method-chain-return-receiver": (7, 7),
    "method-chain-cycle": (7, 7),
    "method-shadow": (9, 9),
    "method-extracted": (1, 1),
    "method-constructor-read": (1, 1),
    "method-constructor-write": (9, 9),
    "method-constructor-call": (8, 8),
    "method-constructor-order": (132, 132),
    "method-constructor-chain": (48, 48),
    "method-constructor-constant": (7, 7),
    "method-constructor-identity": (1, 1),
    "method-constructor-argument-replace": (79, 79),
    "method-constructor-return-object": (9, 9),
    "method-constructor-argument-receiver": (9, 9),
    "method-constructor-return-receiver": (7, 7),
    "method-constructor-self-replace": (79, 79),
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
    "static-getter": (1, 1),
    "constructor-identity": (1, 1),
    "descriptor": (0, 0),
    "static-constant": (7, 7),
    "static-defaults": (923, 923),
    "static-defaults-chain": (12423, 12423),
    "static-default-fields": (7, 7),
    "static-chain": (1, 1),
    "static-methods": (192, 192),
    "static-setter": (7, 7),
    "static-duplicate": (9, 9),
    "static-write": (9, 9),
    "static-alias-write": (11, 11),
    "static-effect": (79, 79),
    "static-identity": (1, 1),
    "static-descriptor": (0, 0),
    "static-receiver": (1, 1),
    "static-foreign-receiver": (11, 11),
    "static-captured": (7, 7),
    "static-dynamic": (7, 7),
    "static-cycle": (7, 7),
    "static-repeated": (11, 11),
    "static-global-effect": (73, 73),
    "static-forward-chain": (1, 1),
    "static-order": (1323, 1323),
    "static-unused-setter": (7, 7),
    "static-name": (1, 0),
    "static-length": (7, 0),
    "static-home": (1, 0),
    "static-caller": (1, 1),
    "static-arguments": (1, 1),
    "bootstrap-config-defaults": (7, 7),
}
POSITIVES = {
    "empty",
    "number",
    "method",
    "method-arguments",
    "method-branches",
    "method-loop",
    "method-dispatch",
    "method-empty",
    "method-chain",
    "method-chain-empty",
    "method-chain-effects",
    "method-chain-order",
    "method-chain-cycle",
    "method-constructor-call",
    "method-constructor-order",
    "method-constructor-chain",
    "method-constructor-constant",
    "static-constant",
    "static-defaults",
    "static-defaults-chain",
    "static-chain",
    "static-methods",
    "static-repeated",
    "static-forward-chain",
    "static-order",
}
PREPARATION = "--ctnative-specialize-class-initialization="


def check_constructed_methods(args):
    checked = refused = 0
    for name, expected in {
        "plain-method": 7,
        "plain-before-store": None,
        "plain-constructor-before-store": None,
        "plain-borrowed-write": 9,
        "plain-self-replace": 79,
        "plain-constructor-write": 7,
        "plain-detached": 1,
        "plain-prototype": 83,
        "plain-prototype-identity": 1,
        "plain-prototype-replace": 79,
        "plain-prototype-self-replace": 79,
        "plain-prototype-before-store": None,
    }.items():
        source = args.fixtures / f"{name}.js"
        for command in ([args.node, "-e", NODE, str(source)], [args.reference, str(source)]):
            result = run(command, success=expected is not None)
            if expected is not None and result.stdout != f"a={expected}\n":
                raise RuntimeError(f"{name}: constructed method observation changed")
        if name in ("plain-method", "plain-prototype"):
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


def prepare(args, name, source, manifest, *, success, options="", diagnostic=""):
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
        or diagnostic not in result.stderr
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


def check_overflow_input(args, source):
    # This malformed closure index is rejected by the parser, before the host
    # fingerprint or preparation proof can inspect it.
    overflow = args.work / "overflow-closure.mlir"
    changed, count = re.subn(
        r"(ctjs.create_closure %\w+\[)\d+(\])",
        r"\g<1>4294967297\2",
        source.read_text(),
        count=1,
    )
    if count != 1:
        raise RuntimeError("overflow closure control could not find its creation")
    overflow.write_text(changed)
    output = args.work / "overflow-parsed.mlir"
    result = run([args.opt, str(overflow), "-o", str(output)], success=False)
    if (
        result.returncode != 1
        or "integer constant out of range for attribute" not in result.stderr
        or (output.exists() and output.read_text())
    ):
        raise RuntimeError("overflow closure index escaped its parser refusal")


def check_getter_parent(args, source, manifest, prepared):
    # A numeric function index is insufficient: make_closure requires the
    # current function's closure, not an arbitrary value of the same IR type.
    forged = args.work / "getter-enclosing-closure.mlir"
    changed, count = re.subn(
        r"(?P<prefix>(?P<getter>%\w+) = ctjs.create_closure )%arg2"
        r"(?P<index>\[\d+\] this )(?P<undefined>%\w+)"
        r"(?P<tail>\n[ \t]*%\w+ = ctjs.constant #ctjs.undefined\n"
        r'[ \t]*ctjs.define_accessor "NAME" on %\w+ get (?P=getter) set %\w+)',
        lambda match: (
            match["prefix"]
            + match["undefined"]
            + match["index"]
            + match["undefined"]
            + match["tail"]
        ),
        source.read_text(),
        count=1,
    )
    if count != 1:
        raise RuntimeError("getter closure control could not find its NAME definition")
    forged.write_text(changed)
    prepare(
        args,
        "getter-enclosing-closure",
        forged,
        dict(manifest, module_sha256=host.fingerprint(args.opt, forged)),
        success=False,
    )
    text = source.read_text()
    definition = re.search(r'ctjs.define_accessor "NAME" on (%\w+) get (%\w+) set (%\w+)', text)
    if not definition:
        raise RuntimeError("getter home control lost its NAME definition")
    constructor, getter, undefined = definition.groups()
    home = re.search(rf"(?m)^([ \t]*ctjs.set_property {getter}\[%\w+\], ){constructor}$", text)
    if not home:
        raise RuntimeError("getter home control lost its source assignment")
    for label, replacement in (
        ("wrong-getter-home", home[1] + undefined),
        ("repeated-getter-home", home[0] + "\n" + home[0]),
    ):
        altered = args.work / f"{label}.mlir"
        altered.write_text(text[: home.start()] + replacement + text[home.end() :])
        prepare(
            args,
            label,
            altered,
            dict(manifest, module_sha256=host.fingerprint(args.opt, altered)),
            success=False,
        )
    closures = re.findall(
        r"(%\w+) = ctjs.create_closure %\w+\[(\d+)\] this %\w+\n"
        r"\s*%\w+ = ctjs.constant #ctjs.undefined\n"
        r'\s*ctjs.define_accessor "[^"]+" on %\w+ get \1 set %\w+',
        text,
    )
    symbols = re.compile(r"^\s*ctjs\.func\b[^@\n]*@([^\s(]+)", re.M)
    functions = symbols.findall(text)
    indices = {index for _, index in closures}
    getters = {name for name in functions if name.rsplit("$", 1)[-1] in indices}
    if len(getters) != 2 or symbols.findall(prepared.read_text()) != [
        name for name in functions if name not in getters
    ]:
        raise RuntimeError("class preparation did not remove exactly its two expanded getters")

    # Symbol references in the module's own attributes and nested operations
    # must retain the definition, even when closure uses are fully expanded.
    attribute = f"test.getter_ref = @{sorted(getters)[0]}"
    for label, pattern, replacement in (
        ("module", r"^module attributes \{", rf"\g<0>{attribute}, "),
        ("function", r"(\bctjs.func[^\n]*\battributes \{)", rf"\g<0>{attribute}, "),
        (
            "operation",
            r"^\s*%\w+ = ctjs.constant #ctjs.undefined$",
            rf"\g<0> {{{attribute}}}",
        ),
        ("unresolved", r"^module attributes \{", rf"\g<0>{attribute}::@missing, "),
    ):
        changed, count = re.subn(pattern, replacement, text, count=1, flags=re.M)
        if count != 1:
            raise RuntimeError(f"getter {label} reference control lost its attribute site")
        altered = args.work / f"getter-{label}-reference.mlir"
        altered.write_text(changed)
        prepare(
            args,
            f"getter-{label}-reference",
            altered,
            dict(manifest, module_sha256=host.fingerprint(args.opt, altered)),
            success=False,
            diagnostic=(
                "class initialization has an unresolved symbol reference"
                if label == "unresolved"
                else "static getter has a remaining symbol reference"
            ),
        )
    return 7


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
    vendor = Path(__file__).resolve().parents[5] / "ctbrowser/vendor/bootstrap/bootstrap.bundle.js"
    bootstrap = vendor.read_text()
    defaults = (args.fixtures / "static-defaults.js").read_text()
    for name in ("Default", "DefaultType"):
        body = f"        static get {name}() {{\n            return {{}}\n        }}"
        if body not in bootstrap or body not in defaults:
            raise RuntimeError(f"Bootstrap Config {name} getter source pin changed")
    # Keep every original Config method, even though this entry only reads Default.
    # Its iterator/throw exits remain a separate proof boundary from local dispatch.
    start = bootstrap.index("    class W {")
    end = bootstrap.index("    class B extends W {", start)
    (args.fixtures / "bootstrap-config-defaults.js").write_text(
        "function configDefaults() {\n"
        + bootstrap[start:end]
        + "\n    var instance = new W();\n    var result = W.Default;\n"
        "    result.n = 7;\n    return result.n;\n}\nvar a = configDefaults();\n"
    )
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
        reference_output = f"a={reference_expected}\n"
        if name == "static-global-effect":
            reference_output += "count=3\n"
        if reference.stdout != reference_output:
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
        if name == "method-dispatch":
            for operation in (
                "scf.index_switch",
                "arith.index_castui",
                "arith.trunci",
                "ub.poison",
            ):
                if operation not in structured.read_text():
                    raise RuntimeError(f"method dispatch no longer exercises {operation}")
        diagnostic = {
            "method-dispatch-ambient": "unknown call, binding or reflective effect",
            "method-dispatch-shadow": "class method is observed or shadowed",
            "method-dispatch-throw": "complete capture-free source functions",
            "bootstrap-config-defaults": "complete capture-free source functions",
        }.get(name, "")
        prepared = prepare(
            args, name, structured, manifest, success=name in POSITIVES, diagnostic=diagnostic
        )
        preparation_refusals += name not in POSITIVES
        if name == "static-chain":
            preparation_refusals += check_getter_parent(args, structured, manifest, prepared)
        if name == "empty":
            check_overflow_input(args, structured)
            for label, control, options in (
                ("no-authority", dict(manifest, initial_intrinsics=[]), ""),
                ("stale", dict(manifest, module_sha256="0" * 64), ""),
                ("budget", manifest, "max-steps=0"),
            ):
                prepare(args, label, structured, control, success=False, options=options)
                preparation_refusals += 1
        if name in (
            "empty",
            "method",
            "method-chain-order",
            "method-loop",
            "method-dispatch",
            "method-constructor-order",
            "static-chain",
            "static-repeated",
            "static-forward-chain",
            "static-defaults-chain",
        ):
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
