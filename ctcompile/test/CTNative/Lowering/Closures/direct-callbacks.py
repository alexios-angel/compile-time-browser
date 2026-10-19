#!/usr/bin/env python3
"""Check that callback parameters lower natively, whether the callback call is
still indirect (`ctjs.call`) or already resolved to a direct call, and that
each unsafe variant of the hand-written guards.mlir is refused.
"""

import argparse
from pathlib import Path
import re

from CTNative.harness import CLEANUP, build_and_run, find_compilers, prepare_js, run, slotted_run

FUNCTION = re.compile(r"ctjs\.func (?:private )?@([^ (]+)\(.*?(?=\n  ctjs\.func |\n})", re.S)
NATIVE_PIPELINE = (
    f"builtin.module(ctnative-lower-to-emitc{{optimize=false observe-globals=true}},{CLEANUP})"
)


def resolve_callbacks(source, mixed=False):
    """Rewrite each wrapper's indirect `ctjs.call %arg3(...)` into a
    `ctjs.call_direct` to its known callback target, padding missing arguments
    with undefined. With `mixed`, only the first of invokeMixed's two calls is
    rewritten, so one direct and one indirect call remain."""
    functions = {match[1].split("$")[0]: match for match in FUNCTION.finditer(source)}
    callbacks = {
        "invoke": "doubleIt",
        "invokeMixed": "increment",
        "invokePadded": "pair",
        "invokeFactory": "make",
    }
    resolved = 0
    replacements = []
    for wrapper, target in callbacks.items():
        match = functions[wrapper]
        body = match[0]
        symbol = functions[target][1]
        header = functions[target][0].split(" -> ", 1)[0]
        # Every ctjs.func takes this, new.target and the callee before its
        # declared parameters.
        arity = header.count("!ctjs.value") - 3
        seen = 0

        def replace(call):
            nonlocal seen, resolved
            seen += 1
            if mixed and wrapper == "invokeMixed" and seen != 1:
                return call[0]
            operands = [operand.strip() for operand in call[1].split(",")]
            receiver, arguments = operands[0], operands[1:]
            if not re.search(re.escape(receiver) + r" = ctjs.constant #ctjs.undefined\b", body):
                raise RuntimeError("source callback no longer has an ordinary undefined receiver")
            if len(arguments) > arity:
                raise RuntimeError("source callback has surplus arguments")
            arguments += [receiver] * (arity - len(arguments))
            resolved += 1
            return (
                f"ctjs.call_direct @{symbol}("
                + ", ".join([receiver, receiver, "%arg3", *arguments])
                + ")"
            )

        body = re.sub(r"ctjs\.call %arg3\(([^)]*)\)", replace, body)
        if seen != (2 if wrapper == "invokeMixed" else 1):
            raise RuntimeError(f"{wrapper}: source callback sites changed")
        replacements.append((match.start(), match.end(), body))
    for start, end, body in reversed(replacements):
        source = source[:start] + body + source[end:]
    if resolved != (4 if mixed else 5):
        raise RuntimeError("pre-resolution did not visit the expected actual callback calls")
    return source


def guard_cases(base):
    """Variants of guards.mlir, each made by filling one placeholder comment
    (EXTRA_CALLER, WRAPPER_USE, FACTORY_USE, EXTRA_FUNCTION) or editing one
    call, mapped to (IR text, expected ctnative.callback_refusal)."""
    escape = "the callback value or its identity escapes the call-only parameter"
    other = (
        "ctjs.func @other$3(%receiver: !ctjs.value, %new_target: !ctjs.value, "
        "%callee: !ctjs.value, %value: !ctjs.value) -> !ctjs.value "
        "attributes {upvalue_count = 0 : i32} { ctjs.return %value }"
    )
    extra = base.replace("// EXTRA_FUNCTION", other)
    return {
        "mismatched-symbol": (
            extra.replace("@factory$2(%undefined,", "@other$3(%undefined,"),
            escape,
        ),
        "mixed-targets": (
            extra.replace(
                "// EXTRA_CALLER",
                "%other = ctjs.create_closure %callee[3] this %undefined\n"
                "    %again = ctjs.call_direct @wrapper$1(%undefined, %undefined, %wrapper, %other, %number)",
            ),
            "callers do not supply one known capture-free callback target",
        ),
        "identity": (
            base.replace(
                "// WRAPPER_USE",
                '%same = ctjs.compare strict_eq %factory, %factory\n    ctjs.store_global "same", %same',
            ),
            escape,
        ),
        "global-escape": (
            base.replace("// WRAPPER_USE", 'ctjs.store_global "saved", %factory'),
            escape,
        ),
        "raw-wrapper": (
            base.replace("// WRAPPER_USE", "%arguments = ctjs.make_arguments"),
            "the wrapper reads its raw argument window",
        ),
        "raw-short-callback": (
            base.replace("// FACTORY_USE", "%arguments = ctjs.make_arguments").replace(
                "ctjs.call_direct @factory$2(%undefined, %undefined, %factory, %value)",
                "ctjs.call %factory(%undefined)",
            ),
            escape,
        ),
        "new-target": (
            base.replace(
                "@factory$2(%undefined, %undefined, %factory,",
                "@factory$2(%undefined, %factory, %factory,",
            ),
            escape,
        ),
        # Keep the wrapper closure call-only so this reaches the new.target guard.
        "wrapper-new-target": (
            base.replace(
                "@wrapper$1(%undefined, %undefined, %wrapper,",
                "@wrapper$1(%undefined, %number, %wrapper,",
            ),
            "the wrapper call has a non-undefined new.target",
        ),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    compilers = find_compilers()
    prepared = args.work / "prepared.mlir"
    positive = args.fixtures / "positive.js"
    prepare_js(args, positive, args.work / "raw.mlir", prepared, "--mlir-print-debuginfo")
    original = prepared.read_text()
    if len(FUNCTION.findall(original)) != 10 or "ctjs.skipped" in original:
        raise RuntimeError("the source's ten functions were not all imported")
    expected = "direct42=42\nmixed85=85\nowned42=42\npadded73=73\n"
    for name, source in [
        ("indirect", original),
        ("direct", resolve_callbacks(original)),
        ("mixed", resolve_callbacks(original, mixed=True)),
    ]:
        input_ir = args.work / f"{name}.mlir"
        input_ir.write_text(source)
        output = args.work / f"{name}.emitc.mlir"
        run(
            [
                args.opt,
                str(input_ir),
                "--pass-pipeline=" + NATIVE_PIPELINE,
                "--mlir-print-debuginfo",
                "-o",
                str(output),
            ]
        )
        native = output.read_text()
        if (
            "ctjs.func" in native
            or "ctnative.not_native" in native
            or "ctnative.callback_refusal" in native
            or len(re.findall(r"\bemitc\.func\b", native)) != 10
        ):
            raise RuntimeError(f"{name}: expected all ten functions native\n{native}")
        deduced = args.work / f"{name}.deduced.mlir"
        run(
            [
                args.opt,
                str(output),
                "--ctnative-print-deduced",
                "--mlir-print-debuginfo",
                "-o",
                str(deduced),
            ]
        )
        for mode, module in [("explicit", output), ("deduced", deduced)]:
            cpp = run([args.translate, "--mlir-to-cpp", str(module)]).stdout
            if "ctbrowser::" in cpp:
                raise RuntimeError(f"{name}/{mode}: output reaches the boxed runtime")
            file = args.work / f"{name}.{mode}.cpp"
            file.write_text(cpp)
            for index, compiler in enumerate(compilers):
                binary = (args.work / f"{name}.{mode}.{index}").resolve()
                observed = build_and_run(compiler, file, binary).stdout
                if observed != expected:
                    raise RuntimeError(f"{name}/{mode}: {observed!r} != {expected!r}")

    base = (args.fixtures / "guards.mlir").read_text()
    for name, (source, reason) in guard_cases(base).items():
        file = args.work / f"{name}.mlir"
        file.write_text(source)
        output = run(
            [args.opt, str(file), "--ctnative-lower-to-emitc=optimize=false observe-globals=true"]
        ).stdout
        if (
            'ctnative.callback_refusal = "' + reason + '"' not in output
            or "ctnative.not_native" not in output
            or re.search(r"\bemitc\.func @main\(", output)
        ):
            raise RuntimeError(f"{name}: missing intended refusal\n{output}")
    # A direct call with too few operands must get the verifier's arity
    # diagnostic, and never reach getOperand(parameter) out of range.
    for name, source, diagnostic in [
        (
            "short-wrapper",
            base.replace("%wrapper, %factory, %number)", "%wrapper, %factory)"),
            "passes 4 operand(s)",
        ),
        (
            "short-direct",
            base.replace(
                "%undefined, %undefined, %factory, %value)", "%undefined, %undefined, %factory)"
            ),
            "passes 3 operand(s)",
        ),
    ]:
        file = args.work / f"{name}.mlir"
        file.write_text(source)
        result = slotted_run(
            [args.opt, str(file), "--ctnative-lower-to-emitc=optimize=false observe-globals=true"],
            text=True,
            capture_output=True,
            timeout=120,
        )
        if result.returncode != 1 or diagnostic not in result.stderr:
            raise RuntimeError(f"{name}: missing verifier refusal\n{result.stdout}{result.stderr}")
    print(
        "direct callbacks: native 10/10 before/after/mixed resolution, 4 observations, "
        "GCC/Clang explicit/deduced; 8 proof refusals and 2 malformed-signature controls"
    )


if __name__ == "__main__":
    main()
