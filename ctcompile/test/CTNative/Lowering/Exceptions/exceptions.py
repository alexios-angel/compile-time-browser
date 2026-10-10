#!/usr/bin/env python3
"""Lit test: lower the JS exception fixtures to native C++ and check them.

Each fixture in PROGRAMS runs under Node, the interpreter and the compiled C++,
and all must agree. POSITIVES must compile fully native with a typed C++
try/catch; the rest must be refused with the boxed handler path intact.
"""

import argparse
import json
from pathlib import Path
import re

from CTNative.harness import (
    CLEANUP,
    CORE_INCLUDE,
    REFUSAL,
    VM_SYMBOL,
    build_and_run,
    native_tools,
    run,
)
from CTNative.Browser.dom.dom import link_options
from CTNative.Exports.boundary import FUNCTION as CTJS_FUNCTION, NATIVE as NATIVE_FUNCTION

# Fixture name -> (number of functions the importer must emit for it, the
# global numeric values the program must leave behind, checked against Node,
# the interpreter and the compiled C++).
PROGRAMS = {
    "guarded": (2, {"caught42": 42, "normal20": 20}),
    "two_sites": (2, {"first42": 42, "normal20": 20, "second107": 107}),
    "continuation": (2, {"caught14": 14, "normal21": 21}),
    "normal_return": (2, {"caught10": 10, "normal7": 7}),
    "unconditional": (2, {"caught33": 33}),
    "boolean_state": (2, {"caught42": 42, "normal0": 0}),
    "finally_override": (2, {"caught7": 7, "normal7": 7}),
    "object_payload": (2, {"object32": 32}),
    "boolean_payload": (2, {"boolean17": 17}),
    "string_payload": (2, {"string18": 18}),
    "boolean_value": (2, {"true17": 17, "false19": 19}),
    "string_value": (3, {"caught42": 42, "normal20": 20}),
    "string_sites": (2, {"first42": 42, "second107": 107, "normal20": 20}),
    "numeric_bits": (2, {"negativeZero42": 42, "nan43": 43}),
    "null_payload": (2, {"null19": 19}),
    "undefined_payload": (2, {"undefined20": 20}),
    "implicit_property": (2, {"explicit42": 42, "implicit42": 42}),
    "mixed_payload": (2, {"first42": 42, "second42": 42}),
    "mixed_string_payload": (2, {"first42": 42, "second42": 42}),
    "mixed_boolean_string_payload": (2, {"first42": 42, "second42": 42}),
    "computed_throw": (2, {"computed42": 42}),
    "mixed_concatenation": (2, {"caught42": 42, "normal20": 20}),
    "bare_finally": (2, {"normal120": 120, "returned10": 10}),
    "catch_finally": (2, {"finally121": 121}),
    "nested_catch": (2, {"nested7": 7}),
    "throwing_callee": (3, {"called42": 42}),
    "numeric_catch_helper": (4, {"caught42": 42, "normal20": 20}),
    "boolean_catch_helper": (3, {"true42": 42, "false20": 20}),
    "string_catch_helper": (4, {"caught42": 42, "normal20": 20}),
    "protected_nothrow_callee": (3, {"caught42": 42, "normal20": 20}),
    "protected_transitive_helper": (4, {"caught42": 42, "normal20": 20}),
    "protected_boolean_helper": (3, {"caught42": 42, "normal20": 20}),
    "protected_string_helper": (4, {"caught42": 42, "normal20": 20}),
    "protected_effectful_helper": (3, {"caught42": 42, "counter": 1}),
    "protected_mixed_callee": (4, {"caught42": 42, "caught40": 40}),
    "protected_escaping_callee": (3, {"normal42": 42, "caught42": 42}),
    "effectful_catch_helper": (4, {"caught42": 42, "counter": 1}),
    "property_catch_helper": (3, {"caught42": 42}),
    "recursive_catch_helper": (3, {"caught42": 42}),
    "throwing_catch_helper": (3, {"caught42": 42}),
    "helper_depth_limit": (35, {"caught42": 42}),
    "helper_work_limit": (3, {"caught42": 42}),
}
# Programs whose every function must lower to native code. Every other entry
# in PROGRAMS must be refused, with the original handler/throw ops intact.
POSITIVES = (
    "guarded",
    "two_sites",
    "continuation",
    "normal_return",
    "unconditional",
    "boolean_state",
    "finally_override",
    "boolean_payload",
    "string_payload",
    "boolean_value",
    "string_value",
    "string_sites",
    "numeric_bits",
    "numeric_catch_helper",
    "boolean_catch_helper",
    "string_catch_helper",
    "protected_nothrow_callee",
    "protected_transitive_helper",
    "protected_boolean_helper",
    "protected_string_helper",
    "computed_throw",
    "mixed_concatenation",
)
TWO_THROW_SITES = ("two_sites", "finally_override", "boolean_value", "string_sites", "numeric_bits")
STRING_LIFETIMES = (
    "string_value",
    "string_sites",
    "string_catch_helper",
    "protected_string_helper",
    "computed_throw",
)
DEFAULT_OPTIMIZATIONS = (
    "guarded",
    "string_value",
    "numeric_catch_helper",
    "string_catch_helper",
    "protected_nothrow_callee",
    "protected_string_helper",
    "computed_throw",
)
CALLEE_EFFECT_REFUSALS = (
    "effectful_catch_helper",
    "property_catch_helper",
    "recursive_catch_helper",
    "helper_depth_limit",
    "helper_work_limit",
    "protected_effectful_helper",
)
CPP_HELPER_CALLS = {
    "numeric_catch_helper": {"increment_1": 2, "addTwo_2": 1},
    "boolean_catch_helper": {"invert_1": 1},
    "string_catch_helper": {"decorate_1": 2},
    "protected_nothrow_callee": {"incrementProtected_1": 1},
    "protected_transitive_helper": {"incrementProtected_1": 2, "twiceProtected_2": 1},
    "protected_boolean_helper": {"invertProtected_1": 1},
    "protected_string_helper": {"decorateProtected_1": 2},
}
IMPORT_REFUSALS = ("catch_finally", "nested_catch")
IMPORT_REASON = "more than one protected region in a function"
NATIVE_PIPELINE = f"builtin.module(ctnative-lower-to-emitc{{optimize=false}},{CLEANUP})"
DEFAULT_NATIVE_PIPELINE = NATIVE_PIPELINE.replace("{optimize=false}", "")


NODE_ORACLE = r"""
const fs = require('node:fs');
const vm = require('node:vm');
const request = JSON.parse(fs.readFileSync(0, 'utf8'));
const realm = vm.createContext({});
vm.runInContext(request.source, realm, {timeout: 1000});
const observations = {};
for (const name of request.names) {
    const value = realm[name];
    if (typeof value !== 'number' || !Number.isFinite(value)) {
        throw new Error(name + ': expected a finite numeric observation');
    }
    observations[name] = value;
}
process.stdout.write(JSON.stringify(observations));
"""


def expected_text(expected):
    return "".join(f"{name}={expected[name]}\n" for name in sorted(expected))


def compare(observed, expected, label):
    if observed != expected:
        raise RuntimeError(f"{label}: {observed!r} != {expected!r}")


def node_oracle(node, source, expected, label):
    result = run(
        [node, "-e", NODE_ORACLE],
        input_text=json.dumps({"source": source, "names": sorted(expected)}),
    )
    observed = json.loads(result.stdout)
    compare(observed, expected, label)
    return observed


def imported_program(args, source, name, denominator, *, skipped=False):
    """Import `source` to ctjs MLIR and check it has `denominator` functions.

    When `skipped`, expect one fewer plus a `ctjs.skipped` marker for the
    unsupported function. Returns the MLIR path and the function count.
    """
    raw = args.work / f"{name}.raw.mlir"
    raw.unlink(missing_ok=True)
    imported = run(
        [
            args.translate,
            "--ctbrowser-js-to-ctjs",
            str(source),
            "--mlir-print-debuginfo",
            "-o",
            str(raw),
        ]
    )
    text = raw.read_text()
    count = len(CTJS_FUNCTION.findall(text))
    if skipped:
        if (
            count != denominator - 1
            or "ctjs.skipped" not in text
            or "function = 1 : i32" not in text
            or IMPORT_REASON not in text
            or "function 1 is not compiled: " + IMPORT_REASON not in imported.stderr
        ):
            raise RuntimeError(
                f"{name}: missing explicit unsupported source function\n{text}\n{imported.stderr}"
            )
    elif count != denominator or "ctjs.skipped" in text or "is not compiled:" in imported.stderr:
        raise RuntimeError(
            f"{name}: expected {denominator} complete source functions\n{text}\n{imported.stderr}"
        )
    return raw, count


def native_functions(module, denominator, label):
    text = module.read_text()
    if (
        CTJS_FUNCTION.search(text)
        or REFUSAL.search(text)
        or "ctjs.skipped" in text
        or len(NATIVE_FUNCTION.findall(text)) != denominator
    ):
        raise RuntimeError(f"{label}: expected native {denominator}/{denominator}\n{text}")


def prepare(args, raw, name):
    prepared = args.work / f"{name}.prepared.mlir"
    prepared.unlink(missing_ok=True)
    run(
        [
            args.opt,
            str(raw),
            "--ctjs-resolve-globals",
            "--ctjs-lift-to-scf",
            "--mlir-print-debuginfo",
            "-o",
            str(prepared),
        ]
    )
    return prepared


def lower(args, prepared, name, *, default_options=False):
    output = args.work / f"{name}.emitc.mlir"
    output.unlink(missing_ok=True)
    pipeline = DEFAULT_NATIVE_PIPELINE if default_options else NATIVE_PIPELINE
    run(
        [
            args.opt,
            str(prepared),
            "--pass-pipeline=" + pipeline,
            "--mlir-print-debuginfo",
            "-o",
            str(output),
        ]
    )
    return output


def operation_count(text, operation):
    return len(re.findall(r"^\s*(?:%[^=\n]+\s*=\s*)?ctjs\." + operation + r"\b", text, re.M))


def refused(prepared, output, imported, label):
    source, text = prepared.read_text(), output.read_text()
    remaining = len(CTJS_FUNCTION.findall(text))
    if (
        not remaining
        or len(REFUSAL.findall(text)) != remaining
        or remaining + len(NATIVE_FUNCTION.findall(text)) != imported
        or re.search(r"\bemitc\.func @main\(", text)
    ):
        raise RuntimeError(f"{label}: missing named refusal or changed denominator\n{text}")
    # Failed recovery must keep the boxed backend's original handler/throw path.
    # The explicitly skipped nested/finally functions have no imported body, so
    # their module-level `ctjs.skipped` marker must survive lowering.
    for operation in ("push_handler", "catch_land", "pop_handler", "check", "throw"):
        if operation_count(text, operation) != operation_count(source, operation):
            raise RuntimeError(f"{label}: refused recovery changed ctjs.{operation}\n{text}")
    if label in IMPORT_REFUSALS and "ctjs.skipped" not in text:
        raise RuntimeError(f"{label}: lowering hid an omitted source function")
    if operation_count(text, "try") or re.search(r"^\s*ctnative\.cpp_try\b", text, re.M):
        raise RuntimeError(f"{label}: refused recovery retained a speculative try region")
    if label in CALLEE_EFFECT_REFUSALS:
        reason = "native try/catch cannot prove a nonthrowing operation: `ctjs.call_direct`"
        if reason not in text:
            raise RuntimeError(f"{label}: refusal did not exercise the callee effect proof\n{text}")


def budget_controls(args, prepared):
    for limit in (0, 64):
        name = f"budget-{limit}"
        output = args.work / f"{name}.refused.mlir"
        output.unlink(missing_ok=True)
        run(
            [
                args.opt,
                str(prepared),
                f"--ctnative-lower-to-emitc=optimize=false exception-max-steps={limit}",
                "--mlir-print-debuginfo",
                "-o",
                str(output),
            ]
        )
        refused(prepared, output, 2, name)
        reason = 'ctnative.exception_refusal = "native exception recovery work budget exhausted"'
        if reason not in output.read_text():
            raise RuntimeError(f"{name}: refusal did not exercise the exception work budget")


def callee_mutation_controls(args, prepared, name, helper_name, denominator):
    # Start from a module that just lowered fully native, then make the helper
    # store to a global. The no-throw proof must be recomputed and fail, even
    # though the resolver's old report and a forged nothrow marker remain.
    text = prepared.read_text()
    helper = re.search(
        r"ctjs\.func private @" + re.escape(helper_name) + r"\$1\b[\s\S]*?"
        r"(?P<returned>^\s*ctjs\.return (?P<value>%\w+))",
        text,
        re.M,
    )
    if not helper:
        raise RuntimeError("callee mutation control lost its increment helper")
    insertion = f'    ctjs.store_global "lateMutation", {helper["value"]}\n'
    text = text[: helper.start("returned")] + insertion + text[helper.start("returned") :]
    text = text.replace(
        "upvalue_count = 0 : i32", "ctnative.nothrow = true, upvalue_count = 0 : i32"
    )
    changed = args.work / f"{name}.callee-mutated.prepared.mlir"
    changed.write_text(text)
    for label in (f"{name}.callee-mutated", f"{name}.callee-mutated-rerun"):
        output = lower(args, changed, label)
        refused(prepared, output, denominator, label)
        reason = "native try/catch cannot prove a nonthrowing operation: `ctjs.call_direct`"
        if reason not in output.read_text():
            raise RuntimeError(f"{label}: late helper mutation retained a stale effect proof")
        changed = output


def register_flow_controls(args):
    # Hand-written IR cases the JS fixtures cannot produce: two edges from one
    # branch to the same successor (the call must resolve), the same with one
    # edge carrying a different value (it must not, despite a stale resolver
    # report and a forged nothrow marker), and a chain that exhausts the
    # register-flow step budget. The helper's constant 4631107791820423168 is the
    # IEEE-754 bit pattern of 42.0.
    prefix = """module {
  ctjs.func @_script_$0(%this: !ctjs.value, %target: !ctjs.value, %enclosing: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %undefined = ctjs.constant #ctjs.undefined
    %closure = ctjs.create_closure %enclosing[1] this %undefined
    ctjs.store_global "checkedHelper", %closure
    %loaded = ctjs.load_global "checkedHelper"
    %condition = ctjs.truthy %undefined
"""
    suffix = """    %result = ctjs.call %forwarded(%undefined)
    ctjs.return %result
  }
  ctjs.func @checkedHelper$1(%this: !ctjs.value, %target: !ctjs.value, %enclosing: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %value = ctjs.constant #ctjs.number<4631107791820423168>
    ctjs.return %value
  }
}
"""
    branch = (
        "    cf.cond_br %condition, ^join(%loaded : !ctjs.value), "
        "^join(%loaded : !ctjs.value)\n"
        "  ^join(%forwarded: !ctjs.value):\n"
    )
    source = args.work / "register-flow-same.mlir"
    source.write_text(prefix + branch + suffix)
    result = args.work / "register-flow-same.resolved.mlir"
    run([args.opt, str(source), "--ctjs-resolve-globals", "-o", str(result)])
    positive = result.read_text()
    if (
        operation_count(positive, "call_direct") != 1
        or operation_count(positive, "call")
        or "ctjs.func private @checkedHelper$1" not in positive
    ):
        raise RuntimeError("same-successor register flow did not prove its common origin")
    report = positive.splitlines()[0]
    if not report.startswith("module attributes {ctjs.globals = "):
        raise RuntimeError("register-flow control lost its resolver report")
    changed = prefix + branch.replace("^join(%loaded", "^join(%undefined", 1) + suffix
    changed = changed.replace("module {", report, 1).replace(
        "upvalue_count = 0 : i32", "ctnative.nothrow = true, upvalue_count = 0 : i32"
    )
    for name in ("register-flow-mixed", "register-flow-mixed-rerun"):
        source = args.work / f"{name}.mlir"
        source.write_text(changed)
        result = args.work / f"{name}.resolved.mlir"
        run([args.opt, str(source), "--ctjs-resolve-globals", "-o", str(result)])
        changed = result.read_text()
        if (
            operation_count(changed, "call_direct")
            or operation_count(changed, "call") != 1
            or "ctjs.func private @checkedHelper$1" in changed
            or "another or unproved incoming value" not in changed
        ):
            raise RuntimeError(f"{name}: mixed incoming register retained a callee proof")
    # 2100 forwarding blocks run past the register-flow step budget (`maxSteps`,
    # 4096 by default, in ctcompile/lib/CTJS/Lowering/Globals/RegisterFlow.h), so
    # the resolver must give up rather than prove the call target.
    chain = "    cf.br ^chain0(%loaded : !ctjs.value)\n"
    for index in range(2100):
        chain += (
            f"  ^chain{index}(%v{index}: !ctjs.value):\n"
            f"    cf.br ^chain{index + 1}(%v{index} : !ctjs.value)\n"
        )
    chain += "  ^chain2100(%forwarded: !ctjs.value):\n"
    source = args.work / "register-flow-budget.mlir"
    source.write_text(prefix + chain + suffix)
    result = args.work / "register-flow-budget.resolved.mlir"
    run([args.opt, str(source), "--ctjs-resolve-globals", "-o", str(result)])
    text = result.read_text()
    if (
        operation_count(text, "call_direct")
        or operation_count(text, "call") != 1
        or "ctjs.func private @checkedHelper$1" in text
        or "register-flow work budget was exhausted" not in text
    ):
        raise RuntimeError("register-flow work exhaustion retained a partial callee proof")


def checked_callee_source_flow(raw, prepared, name):
    if not name.startswith("protected_"):
        return
    before, after = raw.read_text(), prepared.read_text()
    if name in POSITIVES:
        for operation in ("push_handler", "catch_land", "pop_handler", "check", "throw"):
            if operation_count(before, operation) != operation_count(after, operation):
                raise RuntimeError(f"{name}: resolving a checked callee changed ctjs.{operation}")
        if operation_count(after, "call"):
            raise RuntimeError(f"{name}: protected helper retained an unresolved call")
    if name == "protected_mixed_callee":
        if (
            not operation_count(after, "call")
            or re.search(r"ctjs\.call_direct @(?:increment|decrement)Protected\$", after)
            or "another or unproved incoming value" not in after
        ):
            raise RuntimeError("mixed protected callee was named from one incoming edge")
    if name == "protected_escaping_callee":
        if (
            "ctjs.func private @identityProtected$1" in after
            or "the binding is used by ctjs.store_global" not in after
        ):
            raise RuntimeError("handler-only callee escape retained a closed helper")


def generated_helper_limits(fixtures):
    """Write two fixtures that run correctly but exceed native exception limits.

    One is a chain of 33 helper calls (the call-depth limit is 32), the other a
    helper of about 2050 statements (past the recovery work budget). Neither uses
    recursion or unsupported JavaScript, so the only reason to refuse is the limit.
    """
    depth = "\n".join(
        f"function helper{index}(value) {{ return "
        + (f"helper{index + 1}(value)" if index < 32 else "value")
        + "; }"
        for index in range(33)
    )
    suffix = (
        "\nfunction helperLimit() { try { throw 42; } "
        "catch (value) { return helper0(value); } }\n"
        "var caught42 = helperLimit();\n"
    )
    (fixtures / "helper_depth_limit.js").write_text(depth + suffix)
    work = "function helper0(value) {\n" + "value = value + 0;\n" * 2050 + "return value;\n}"
    (fixtures / "helper_work_limit.js").write_text(work + suffix)


def standalone(args, module, name, expected, compilers, nm, *, wrong_state=False):
    """Print `module` as explicit and deduced C++, check its shape, and run it.

    Each build must print `expected`. With `wrong_state`, the output must also
    differ from guarded's expected values (it checks the result checker itself).
    """
    # Typed Number/String concatenation links the same public Core as the VM.
    _, libraries = link_options(
        argparse.Namespace(build=args.build, include=Path(CORE_INCLUDE[2:])), core_only=True
    )
    deduced = args.work / f"{name}.deduced.mlir"
    deduced.unlink(missing_ok=True)
    run(
        [
            args.opt,
            str(module),
            "--ctnative-print-deduced",
            "--mlir-print-debuginfo",
            "-o",
            str(deduced),
        ]
    )
    for mode, ir in [("explicit", module), ("deduced", deduced)]:
        cpp = run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
        if VM_SYMBOL.search(cpp) or re.search(r"#include [<\"]ctbrowser/", cpp):
            raise RuntimeError(f"{name}/{mode}: native output reaches the VM")
        if not re.search(r"\btry\s*\{", cpp) or not re.search(r"\bcatch\s*\(", cpp):
            raise RuntimeError(f"{name}/{mode}: native output lost its runtime try/catch")
        if not re.search(r"\bthrow\s+", cpp):
            raise RuntimeError(f"{name}/{mode}: native output lost its runtime throw")
        for helper, count in CPP_HELPER_CALLS.get(name, {}).items():
            # Dead result slots disappear, but their calls still execute.
            calls = r"(?:=\s*|^\s*)" + re.escape(helper) + r"\("
            if len(re.findall(calls, cpp, re.M)) != count:
                raise RuntimeError(f"{name}/{mode}: native output lost its calls to {helper}")
        catches = re.findall(r"\bcatch\s*\(([^)]*)\)", cpp)
        if any(
            "..." in caught or "std::exception" in caught or not re.search(r"\bconst\b.*&", caught)
            for caught in catches
        ):
            raise RuntimeError(
                f"{name}/{mode}: catch does not preserve the typed JS boundary: {catches}"
            )
        source = args.work / f"{name}.{mode}.cpp"
        source.write_text(cpp)
        for index, compiler in enumerate(compilers):
            binary = (args.work / f"{name}.{mode}.{index}").resolve()
            binary.unlink(missing_ok=True)
            observed = build_and_run(compiler, source, binary, libraries=libraries, nm=nm).stdout
            compare(observed, expected_text(expected), f"{name}/{mode}/{compiler}")
            if wrong_state:
                try:
                    compare(
                        observed,
                        expected_text(PROGRAMS["guarded"][1]),
                        "compiled wrong-state control",
                    )
                except RuntimeError as error:
                    if "caught42=32" not in str(error):
                        raise
                else:
                    raise RuntimeError("executed wrong-state binary escaped result checking")
        if name in STRING_LIFETIMES:
            sanitized = (args.work / f"{name}.{mode}.sanitized").resolve()
            compare(
                build_and_run(
                    compilers[1], source, sanitized, libraries=libraries, sanitize=True
                ).stdout,
                expected_text(expected),
                f"{name}/{mode}/ASan-UBSan",
            )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate")
    parser.add_argument("--opt")
    parser.add_argument("--reference")
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--node", required=True)
    parser.add_argument("--oracle-only", action="store_true")
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    generated_helper_limits(args.fixtures)
    node = args.node
    oracles = {}
    for name, (_, expected) in PROGRAMS.items():
        source = (args.fixtures / f"{name}.js").read_text()
        oracles[name] = node_oracle(node, source, expected, name)
    source = (args.fixtures / "guarded.js").read_text()
    if source.count("mark = 10;") != 1:
        raise RuntimeError("wrong-state control lost the unique throw-site mutation")
    wrong = source.replace("mark = 10;", "mark = 0;")
    wrong_expected = {"caught42": 32, "normal20": 20}
    observed = node_oracle(node, wrong, wrong_expected, "wrong-state control")
    try:
        compare(observed, PROGRAMS["guarded"][1], "wrong-state control")
    except RuntimeError as error:
        if "'caught42': 32" not in str(error):
            raise
    else:
        raise RuntimeError("wrong-state observation escaped the result checker")
    (args.work / "node.json").write_text(json.dumps(oracles, indent=2) + "\n")
    (args.work / "wrong-state.js").write_text(wrong)
    if args.oracle_only:
        print(
            f"native exceptions: {len(PROGRAMS)} independent Node programs and "
            "executed wrong-state control agree"
        )
        return
    if not (args.translate and args.opt and args.reference):
        parser.error("--translate, --opt and --reference are required unless --oracle-only")
    reference = args.reference
    compilers, nm = native_tools(reference)
    register_flow_controls(args)
    report = []
    for name, (denominator, expected) in PROGRAMS.items():
        source = args.fixtures / f"{name}.js"
        compare(
            run([str(reference), str(source)]).stdout,
            expected_text(expected),
            f"{name}/interpreter",
        )
        raw, imported = imported_program(
            args, source, name, denominator, skipped=name in IMPORT_REFUSALS
        )
        prepared = prepare(args, raw, name)
        checked_callee_source_flow(raw, prepared, name)
        output = lower(args, prepared, name)
        if name in POSITIVES:
            # The overriding finally also leaves an unreachable synthetic
            # rethrow in raw bytecode; recovery must prove that tail dead.
            throw_sites = 2 if name in TWO_THROW_SITES else 1
            if (
                operation_count(prepared.read_text(), "push_handler") != 1
                or operation_count(prepared.read_text(), "throw") != throw_sites
            ):
                raise RuntimeError(f"{name}: source exception paths disappeared before recovery")
            native_functions(output, denominator, name)
            standalone(args, output, name, expected, compilers, nm)
            if name == "guarded":
                budget_controls(args, prepared)
            if name == "numeric_catch_helper":
                callee_mutation_controls(args, prepared, name, "increment", denominator)
            if name == "protected_nothrow_callee":
                callee_mutation_controls(args, prepared, name, "incrementProtected", denominator)
            if name in DEFAULT_OPTIMIZATIONS:
                default_name = name + "-default"
                defaults = lower(args, prepared, default_name, default_options=True)
                native_functions(defaults, denominator, default_name)
                standalone(args, defaults, default_name, expected, compilers, nm)
        else:
            refused(prepared, output, imported, name)
        report.append(
            {
                "name": name,
                "source_functions": denominator,
                "imported": imported,
                "native": len(NATIVE_FUNCTION.findall(output.read_text())),
                "refusals": REFUSAL.findall(output.read_text()),
                "observations": expected,
                "interpreter_agrees": True,
                "interpreter_observations": expected,
            }
        )

    # This altered guarded.js produces the wrong throw-site state that restoring
    # the try-entry registers would produce. Run its generated C++ too; the same
    # observation checker must reject it against guarded's expected values.
    wrong_source = args.work / "wrong-state.js"
    compare(
        run([str(reference), str(wrong_source)]).stdout,
        expected_text(wrong_expected),
        "wrong-state/interpreter",
    )
    raw, _ = imported_program(args, wrong_source, "wrong-state", 2)
    output = lower(args, prepare(args, raw, "wrong-state"), "wrong-state")
    native_functions(output, 2, "wrong-state")
    standalone(args, output, "wrong-state", wrong_expected, compilers, nm, wrong_state=True)
    (args.work / "native.json").write_text(json.dumps(report, indent=2) + "\n")
    print(
        f"native exceptions: {len(POSITIVES)} complete native programs plus numeric/string defaults, "
        "throw-site state/two throws/catch continuation/normal return/unconditional "
        "throw/boolean and owning string payloads/state/closed catch and protected helpers/"
        "finally override/NaN/negative zero; "
        "Node and interpreter agree with GCC/Clang explicit/deduced, no VM; "
        f"owning string ASan/UBSan/lifetime checks; {len(PROGRAMS) - len(POSITIVES)} "
        "refusals, callee mutation/rerun/depth/work controls, zero/tight recovery budget "
        "refusals and executed wrong-state control; null-property refusal agrees with Node"
    )


if __name__ == "__main__":
    main()
