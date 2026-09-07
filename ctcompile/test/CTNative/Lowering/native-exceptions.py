#!/usr/bin/env python3
"""Check native throw-site state, typed catches and conservative boundaries."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


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
}
POSITIVES = ("guarded", "two_sites", "continuation", "normal_return",
             "unconditional", "boolean_state", "finally_override", "boolean_payload",
             "string_payload", "boolean_value", "string_value", "string_sites", "numeric_bits")
TWO_THROW_SITES = ("two_sites", "finally_override", "boolean_value", "string_sites", "numeric_bits")
STRING_LIFETIMES = ("string_value", "string_sites")
DEFAULT_OPTIMIZATIONS = ("guarded", "string_value")
IMPORT_REFUSALS = ("catch_finally", "nested_catch")
IMPORT_REASON = "more than one protected region in a function"
# Existing interpreter behavior for null property access differs from Node:
# it returns undefined instead of entering the catch. Pin that difference on
# this refusal-only control; never use it as an admitted native oracle.
INTERPRETER_DIVERGENCES = {
    "implicit_property": {"explicit42": 42, "implicit42": "undefined"},
}
NATIVE_PIPELINE = ("builtin.module(ctnative-lower-to-emitc{optimize=false},"
                   "emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,"
                   "canonicalize,ctnative-prune-dead-stores,canonicalize))")
DEFAULT_NATIVE_PIPELINE = NATIVE_PIPELINE.replace("{optimize=false}", "")

CTJS_FUNCTION = re.compile(r"^\s*ctjs\.func\b", re.M)
NATIVE_FUNCTION = re.compile(r"^\s*emitc\.func\b", re.M)
REFUSAL = re.compile(r'ctnative\.not_native = "((?:[^"\\]|\\.)*)"')
VM_SYMBOL = re.compile(r"ctbrowser::(?:script|aot)::|\bct_aot_")


def run(command, *, environment=None, input_text=None):
    result = subprocess.run(command, text=True, capture_output=True, timeout=120,
                            env=environment, input=input_text)
    if result.returncode:
        raise RuntimeError(f"{command!r}\n{result.stdout}{result.stderr}")
    return result


def build_path(opt, relative):
    executable = Path(shutil.which(opt) or opt).resolve()
    for parent in executable.parents:
        candidate = parent / relative
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"cannot locate {relative} beside {opt}")


def node_executable(args):
    node = args.node or os.environ.get("CTCOMPILE_NODE") or shutil.which("node")
    if node:
        return node
    if args.opt:
        cache = build_path(args.opt, "CMakeCache.txt").read_text()
        match = re.search(r"^CTCOMPILE_BOOTSTRAP_NODE:FILEPATH=(.+)$", cache, re.M)
        if match and Path(match[1]).is_file():
            return match[1]
    raise RuntimeError("native exception regression requires independent Node; pass --node")


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
    result = run([node, "-e", NODE_ORACLE], input_text=json.dumps(
        {"source": source, "names": sorted(expected)}))
    observed = json.loads(result.stdout)
    compare(observed, expected, label)
    return observed


def imported_program(args, source, name, denominator, *, skipped=False):
    raw = args.work / f"{name}.raw.mlir"
    raw.unlink(missing_ok=True)
    imported = run([args.translate, "--ctbrowser-js-to-ctjs", str(source),
                    "--mlir-print-debuginfo", "-o", str(raw)])
    text = raw.read_text()
    count = len(CTJS_FUNCTION.findall(text))
    if skipped:
        if (count != denominator - 1 or "ctjs.skipped" not in text or
                "function = 1 : i32" not in text or IMPORT_REASON not in text or
                "function 1 is not compiled: " + IMPORT_REASON not in imported.stderr):
            raise RuntimeError(f"{name}: missing explicit unsupported source function\n"
                               f"{text}\n{imported.stderr}")
    elif count != denominator or "ctjs.skipped" in text or "is not compiled:" in imported.stderr:
        raise RuntimeError(f"{name}: expected {denominator} complete source functions\n"
                           f"{text}\n{imported.stderr}")
    return raw, count


def native_functions(module, denominator, label):
    text = module.read_text()
    if (CTJS_FUNCTION.search(text) or REFUSAL.search(text) or "ctjs.skipped" in text or
            len(NATIVE_FUNCTION.findall(text)) != denominator):
        raise RuntimeError(f"{label}: expected native {denominator}/{denominator}\n{text}")


def prepare(args, raw, name):
    prepared = args.work / f"{name}.prepared.mlir"
    prepared.unlink(missing_ok=True)
    run([args.opt, str(raw), "--ctjs-resolve-globals", "--ctjs-lift-to-scf",
         "--mlir-print-debuginfo", "-o", str(prepared)])
    return prepared


def lower(args, prepared, name, *, default_options=False):
    output = args.work / f"{name}.emitc.mlir"
    output.unlink(missing_ok=True)
    pipeline = DEFAULT_NATIVE_PIPELINE if default_options else NATIVE_PIPELINE
    run([args.opt, str(prepared), "--pass-pipeline=" + pipeline,
         "--mlir-print-debuginfo", "-o", str(output)])
    return output


def operation_count(text, operation):
    return len(re.findall(r"^\s*(?:%[^=\n]+\s*=\s*)?ctjs\." + operation + r"\b", text, re.M))


def refused(prepared, output, imported, label):
    source, text = prepared.read_text(), output.read_text()
    remaining = len(CTJS_FUNCTION.findall(text))
    if (not remaining or len(REFUSAL.findall(text)) != remaining or
            remaining + len(NATIVE_FUNCTION.findall(text)) != imported or
            re.search(r"\bemitc\.func @main\(", text)):
        raise RuntimeError(f"{label}: missing named refusal or changed denominator\n{text}")
    # Failed recovery must keep the boxed backend's original handler/throw path.
    # The explicitly skipped nested/finally functions have no imported body;
    # their retained module-level omission marker remains part of the evidence.
    for operation in ("push_handler", "catch_land", "pop_handler", "check", "throw"):
        if operation_count(text, operation) != operation_count(source, operation):
            raise RuntimeError(f"{label}: refused recovery changed ctjs.{operation}\n{text}")
    if label in IMPORT_REFUSALS and "ctjs.skipped" not in text:
        raise RuntimeError(f"{label}: lowering hid an omitted source function")
    if operation_count(text, "try") or re.search(r"^\s*ctnative\.cpp_try\b", text, re.M):
        raise RuntimeError(f"{label}: refused recovery retained a speculative try region")


def budget_controls(args, prepared):
    for limit in (0, 64):
        name = f"budget-{limit}"
        output = args.work / f"{name}.refused.mlir"
        output.unlink(missing_ok=True)
        run([args.opt, str(prepared),
             f"--ctnative-lower-to-emitc=optimize=false exception-max-steps={limit}",
             "--mlir-print-debuginfo", "-o", str(output)])
        refused(prepared, output, 2, name)
        reason = ('ctnative.exception_refusal = '
                  '"native exception recovery work budget exhausted"')
        if reason not in output.read_text():
            raise RuntimeError(f"{name}: refusal did not exercise the exception work budget")


def execution_tools(args):
    compilers = []
    for choices in [("g++-13", "g++"), ("clang++-18", "clang++")]:
        compiler = next((shutil.which(name) for name in choices if shutil.which(name)), None)
        if not compiler:
            raise RuntimeError("native exception regression requires " + " or ".join(choices))
        compilers.append(compiler)
    reference = args.reference or build_path(args.opt, "test/ctcompile-test-native-reference")
    nm = shutil.which("nm") or shutil.which("llvm-nm")
    if not nm or not VM_SYMBOL.search(run([nm, "-C", str(reference)]).stdout):
        raise RuntimeError("VM symbol detector failed its interpreter positive control")
    return reference, compilers, nm


def standalone(args, module, name, expected, compilers, nm, *, wrong_state=False):
    deduced = args.work / f"{name}.deduced.mlir"
    deduced.unlink(missing_ok=True)
    run([args.opt, str(module), "--ctnative-print-deduced", "--mlir-print-debuginfo",
         "-o", str(deduced)])
    flags = ["-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror", "-Wconversion",
             "-pedantic", "-ffp-contract=off"]
    for mode, ir in [("explicit", module), ("deduced", deduced)]:
        cpp = run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
        if VM_SYMBOL.search(cpp) or re.search(r"#include [<\"]ctbrowser/", cpp):
            raise RuntimeError(f"{name}/{mode}: native output reaches the VM")
        if not re.search(r"\btry\s*\{", cpp) or not re.search(r"\bcatch\s*\(", cpp):
            raise RuntimeError(f"{name}/{mode}: native output lost its runtime try/catch")
        if not re.search(r"\bthrow\s+", cpp):
            raise RuntimeError(f"{name}/{mode}: native output lost its runtime throw")
        catches = re.findall(r"\bcatch\s*\(([^)]*)\)", cpp)
        if any("..." in caught or "std::exception" in caught or
               not re.search(r"\bconst\b.*&", caught) for caught in catches):
            raise RuntimeError(f"{name}/{mode}: catch does not preserve the typed JS boundary: {catches}")
        source = args.work / f"{name}.{mode}.cpp"
        source.write_text(cpp)
        for index, compiler in enumerate(compilers):
            binary = (args.work / f"{name}.{mode}.{index}").resolve()
            binary.unlink(missing_ok=True)
            run([compiler, *flags, str(source), "-o", str(binary)])
            if VM_SYMBOL.search(run([nm, "-C", str(binary)]).stdout):
                raise RuntimeError(f"{name}/{mode}: compiled binary reaches the VM")
            observed = run([str(binary)]).stdout
            compare(observed, expected_text(expected), f"{name}/{mode}/{compiler}")
            if wrong_state:
                try:
                    compare(observed, expected_text(PROGRAMS["guarded"][1]),
                            "compiled wrong-state control")
                except RuntimeError as error:
                    if "caught42=32" not in str(error):
                        raise
                else:
                    raise RuntimeError("executed wrong-state binary escaped result checking")
        if name in STRING_LIFETIMES:
            sanitized = (args.work / f"{name}.{mode}.sanitized").resolve()
            environment = dict(os.environ,
                               ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
                               UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
            run([compilers[1], *flags, "-O1", "-g", "-fno-omit-frame-pointer",
                 "-fsanitize=address,undefined", "-fsanitize-address-use-after-scope",
                 str(source), "-o", str(sanitized)])
            compare(run([str(sanitized)], environment=environment).stdout,
                    expected_text(expected), f"{name}/{mode}/ASan-UBSan")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate")
    parser.add_argument("--opt")
    parser.add_argument("--reference")
    parser.add_argument("--node")
    parser.add_argument("--oracle-only", action="store_true")
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    node = node_executable(args)
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
        print(f"native exceptions: {len(PROGRAMS)} independent Node programs and "
              "executed wrong-state control agree")
        return
    if not args.translate or not args.opt:
        parser.error("--translate and --opt are required unless --oracle-only")
    reference, compilers, nm = execution_tools(args)
    report = []
    for name, (denominator, expected) in PROGRAMS.items():
        source = args.fixtures / f"{name}.js"
        interpreter_expected = INTERPRETER_DIVERGENCES.get(name, expected)
        compare(run([str(reference), str(source)]).stdout, expected_text(interpreter_expected),
                f"{name}/interpreter")
        raw, imported = imported_program(args, source, name, denominator,
                                         skipped=name in IMPORT_REFUSALS)
        prepared = prepare(args, raw, name)
        output = lower(args, prepared, name)
        if name in POSITIVES:
            # The overriding finally also leaves an unreachable synthetic
            # rethrow in raw bytecode; recovery must prove that tail dead.
            throw_sites = 2 if name in TWO_THROW_SITES else 1
            if (operation_count(prepared.read_text(), "push_handler") != 1 or
                    operation_count(prepared.read_text(), "throw") != throw_sites):
                raise RuntimeError(f"{name}: source exception paths disappeared before recovery")
            native_functions(output, denominator, name)
            standalone(args, output, name, expected, compilers, nm)
            if name == "guarded":
                budget_controls(args, prepared)
            if name in DEFAULT_OPTIMIZATIONS:
                default_name = name + "-default"
                defaults = lower(args, prepared, default_name, default_options=True)
                native_functions(defaults, denominator, default_name)
                standalone(args, defaults, default_name, expected, compilers, nm)
        else:
            refused(prepared, output, imported, name)
        report.append({"name": name, "source_functions": denominator, "imported": imported,
                       "native": len(NATIVE_FUNCTION.findall(output.read_text())),
                       "refusals": REFUSAL.findall(output.read_text()), "observations": expected,
                       "interpreter_agrees": interpreter_expected == expected,
                       "interpreter_observations": interpreter_expected})

    # This altered source executes the specific wrong throw-site state that
    # restoring the try-entry registers would produce. Run its generated C++,
    # too; the same observation checker must reject it against guarded's oracle.
    wrong_source = args.work / "wrong-state.js"
    compare(run([str(reference), str(wrong_source)]).stdout, expected_text(wrong_expected),
            "wrong-state/interpreter")
    raw, _ = imported_program(args, wrong_source, "wrong-state", 2)
    output = lower(args, prepare(args, raw, "wrong-state"), "wrong-state")
    native_functions(output, 2, "wrong-state")
    standalone(args, output, "wrong-state", wrong_expected, compilers, nm, wrong_state=True)
    (args.work / "native.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"native exceptions: {len(POSITIVES)} complete native programs plus numeric/string defaults, "
          "throw-site state/two throws/catch continuation/normal return/unconditional "
          "throw/boolean and owning string payloads/state/finally override/NaN/negative zero; "
          "Node and interpreter agree with GCC/Clang explicit/deduced, no VM; "
          f"owning string ASan/UBSan/lifetime checks; {len(PROGRAMS) - len(POSITIVES)} "
          "refusals, zero/tight budget "
          "refusals and executed wrong-state control; null-property refusal pins "
          "the existing interpreter/Node divergence")


if __name__ == "__main__":
    main()
