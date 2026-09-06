#!/usr/bin/env python3
"""Check scalar constexpr spelling, behavior, exact pins, and native source flow."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess


MAIN = """
int main() {
    if (static_chain() != 10.0 || integer_chain() != 85) { return 1; }
    if (runtime_parameter(3.0) != 11.0 || runtime_parameter(9.0) != 17.0) { return 2; }
    if (runtime_call() != 2 || runtime_call() != 3) { return 3; }
    if (const_abi_call() != 10 || const_abi_call() != 11) { return 4; }
    if (mutated_seed() != 9 || load_snapshot() != 5) { return 5; }
    if (heap_value() != 50) { return 6; }
    if (exact_division() != 1.5) { return 7; }
    if (!(inexact_division() > 0.333 && inexact_division() < 0.334)) { return 8; }
    if (!std::isinf(division_by_zero()) || std::signbit(division_by_zero())) { return 9; }
    if (!std::isinf(floating_overflow()) || std::signbit(floating_overflow())) { return 10; }
    if (std::fpclassify(floating_underflow()) != FP_SUBNORMAL) { return 11; }
    if (negative_zero() != 0.0 || !std::signbit(negative_zero())) { return 12; }
    if (!std::isinf(infinite_literal()) || !std::isnan(nan_literal())) { return 13; }
    if (scalar_selection() != -3.0 || unsigned_wrap() != 0) { return 14; }
    if (fractional_conversion() != 1 || cancellation() != 0.0) { return 15; }
    return 0;
}
"""

ISOLATION_MAIN = """
int main() {
    return ordinary_before() + marked() + ordinary_nested() + marked_after()
        + const_only() + constexpr_only() + wrong_marker() + ordinary_after() == 36 ? 0 : 1;
}
"""


def run(command, *, failure_site=None):
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    diagnostic = result.stdout + result.stderr
    if failure_site is not None:
        if result.returncode == 0 or failure_site not in diagnostic:
            raise RuntimeError(f"expected pin failure at {failure_site}: {command!r}\n{diagnostic}")
    elif result.returncode:
        raise RuntimeError(f"constexpr binding test failed: {command!r}\n{diagnostic}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    compilers = []
    for candidates in [("g++-13", "g++"), ("clang++-18", "clang++")]:
        compiler = next((shutil.which(name) for name in candidates if shutil.which(name)), None)
        if not compiler:
            raise RuntimeError("constexpr binding test requires " + " or ".join(candidates))
        compilers.append(compiler)
    flags = ["-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
             "-Wconversion", "-ffp-contract=off", "-I", str(args.fixtures.resolve())]

    def compile_and_run(label, text, harness="", *, defines=(), expected=""):
        source = args.work / f"{label}.cpp"
        source.write_text(text + harness)
        for index, compiler in enumerate(compilers):
            binary = (args.work / f"{label}-{index}").resolve()
            run([compiler, *flags, *defines, str(source), "-o", str(binary)])
            actual = run([str(binary)])
            if actual != expected:
                raise RuntimeError(f"{label}/{Path(compiler).name}: {actual!r} != {expected!r}")
            print(f"constexpr bindings: {Path(compiler).name} preserved {label} behavior")

    emitted = {}
    for label, harness in [("bindings", MAIN), ("hoisted", MAIN), ("isolation", ISOLATION_MAIN)]:
        text = (args.fixtures / f"{label}.cpp").read_text()
        if not text.strip():
            raise RuntimeError(f"empty emitted {label}.cpp")
        if label == "bindings":
            # This use is added after analysis, so it cannot itself influence
            # the proof. A plain const double does not satisfy this assertion.
            anchor = "return product;"
            if text.count(anchor) != 1:
                raise RuntimeError("missing unique transitive constexpr witness")
            text = text.replace(anchor, "static_assert(product == 10.0);\n  " + anchor, 1)
            anchor = "return selection_2;"
            if text.count(anchor) != 1:
                raise RuntimeError("missing unique static conversion/control witness")
            text = text.replace(anchor, "static_assert(selection_2 == -3.0);\n  " + anchor, 1)
        emitted[label] = text
        compile_and_run(label, text, harness)

    before = '"constexpr-bindings.js:1:1", double const);'
    after = '"constexpr-bindings.js:1:1", int32_t const);'
    if emitted["bindings"].count(before) != 1:
        raise RuntimeError("missing exact constexpr-auto type pin")
    mutated = emitted["bindings"].replace(before, after, 1)
    source = args.work / "wrong-pin.cpp"
    source.write_text(mutated + MAIN)
    for compiler in compilers:
        run([compiler, *flags, "-fsyntax-only", str(source)],
            failure_site="constexpr-bindings.js:1:1")
    compile_and_run("wrong-pin-disabled", mutated, MAIN, defines=("-DCTCOMPILE_NO_TYPE_PINS",))

    # Keep two different call-site inputs and disable optional precomputation:
    # the emitted function parameter stays dynamic while its local seed is static.
    tests = Path(__file__).resolve().parents[2]
    native = args.work / "native.mlir"
    run(["cmake", f"-DTRANSLATE={args.translate}", f"-DOPT={args.opt}",
         f"-DSOURCE={args.fixtures / 'constexpr-source.js'}", f"-DOUTPUT={native}",
         "-DOPTIMIZE=OFF", "-P", str(tests / "native-pipeline.cmake")])
    if "ctnative.constexpr_bindings" not in native.read_text():
        raise RuntimeError("native lowering omitted the constexpr policy marker")
    deduced = args.work / "native-deduced.mlir"
    run([args.opt, "--ctnative-print-deduced", "--mlir-print-debuginfo", str(native), "-o", str(deduced)])
    decisions = []
    for label, module in [("native", native), ("native-deduced", deduced)]:
        cpp = run([args.translate, "--mlir-to-cpp", str(module)])
        body = cpp.split("// ctcompile: function runtimeMix,", 1)[1]
        body = body.split("// ctcompile:", 1)[0]
        if not re.search(r"js_num runtimeMix_\d+\(js_num const input\)", body):
            raise RuntimeError(f"runtime parameter spelling changed:\n{body}")
        if "constexpr js_num staticSeed = 8.0;" not in body:
            raise RuntimeError(f"source literal did not become constexpr:\n{body}")
        if not re.search(r"(?:js_num|auto) const dynamicSum = input \+ staticSeed;", body):
            raise RuntimeError(f"runtime computation lost its source name/type:\n{body}")
        decisions.append(re.findall(r"\b(constexpr )?(?:js_num|auto)( const)? (staticSeed|dynamicSum)\b", body))
        compile_and_run(label, cpp, expected="first=11\nsecond=17\n")
    if decisions[0] != decisions[1]:
        raise RuntimeError(f"deduction changed constexpr/const decisions: {decisions}")


if __name__ == "__main__":
    main()
