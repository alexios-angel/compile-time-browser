#!/usr/bin/env python3
"""Check final parameter use detection, warning cleanliness and source lowering."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess


MAIN = """
int main() {
    if (used(42) != 42 || unused(999, 35) != 42) { return 1; }
    if (explicit_void(42) != 42 || wrong_marker(42) != 42) { return 2; }
    if (other_callee(41) != 42 || non_parameter() != 7) { return 3; }
    if (verbatim_use(41) != 42 || opaque_use(41) != 42) { return 4; }
    if (captured_use(41) != 42 || unused_capture(999) != 42) { return 5; }
    if (omitted_argument(999) != 42 || loop_use(5) != 10) { return 6; }
    if (opaque_parameter(ExternalValue{42}) != 42) { return 7; }
    return 0;
}
"""


def run(command):
    result = subprocess.run(command, text=True, capture_output=True, timeout=120)
    if result.returncode:
        raise RuntimeError(f"{command!r}\n{result.stdout}{result.stderr}")
    return result.stdout


def function_body(text, name):
    match = re.search(r"\b" + re.escape(name) + r"\([^;{}]*\)\s*\{", text)
    if not match:
        raise RuntimeError(f"missing function {name}\n{text}")
    start = match.end()
    depth = 1
    for at in range(start, len(text)):
        depth += (text[at] == "{") - (text[at] == "}")
        if depth == 0:
            return text[start:at]
    raise RuntimeError(f"unterminated function {name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    compilers = []
    for choices in [("g++-13", "g++"), ("clang++-18", "clang++")]:
        compiler = next((shutil.which(name) for name in choices if shutil.which(name)), None)
        if not compiler:
            raise RuntimeError("parameter regression requires " + " or ".join(choices))
        compilers.append(compiler)

    def compile_and_run(label, cpp, harness="", expected=""):
        source = args.work / f"{label}.cpp"
        source.write_text(cpp + harness)
        for index, compiler in enumerate(compilers):
            binary = (args.work / f"{label}-{index}").resolve()
            run([compiler, "-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror",
                 "-Wconversion", "-pedantic", "-ffp-contract=off", "-I", str(args.fixtures),
                 str(source), "-o", str(binary)])
            assert run([str(binary)]) == expected

    for label in ["parameters", "hoisted"]:
        cpp = (args.fixtures / f"{label}.cpp").read_text()
        for name in ["used", "other_callee", "verbatim_use", "opaque_use", "captured_use", "loop_use"]:
            assert "static_cast<void>" not in function_body(cpp, name), (label, name, cpp)
        for name in ["unused", "explicit_void", "wrong_marker", "non_parameter", "opaque_parameter", "unused_capture", "omitted_argument"]:
            assert function_body(cpp, name).count("static_cast<void>") == 1, (label, name, cpp)
        assert "static_cast<void>(input)" not in function_body(cpp, "unused"), cpp
        assert "static_cast<void>(ignored)" in function_body(cpp, "unused"), cpp
        assert "touch(input)" in function_body(cpp, "other_callee"), cpp
        if label == "parameters":
            assert 'CTCOMPILE_PIN(answer, "unused-parameters.js:3:1", int32_t const);' in cpp, cpp
        compile_and_run(label, cpp, MAIN)

    cleaned = (args.fixtures / "cleanup.cpp").read_text()
    body = function_body(cleaned, "after_cleanup")
    assert body.count("static_cast<void>") == 1 and "int64_t" not in body, body
    compile_and_run("cleanup", cleaned, "\nint main() { return after_cleanup(99) == 42 ? 0 : 1; }\n")

    tests = Path(__file__).resolve().parents[2]
    native = args.work / "native.mlir"
    run(["cmake", f"-DTRANSLATE={args.translate}", f"-DOPT={args.opt}",
         f"-DSOURCE={args.fixtures / 'native-source.js'}", f"-DOUTPUT={native}",
         "-DOPTIMIZE=OFF", "-P", str(tests / "native-pipeline.cmake")])
    assert "ctnative.parameter_suppression" in native.read_text()
    deduced = args.work / "native-deduced.mlir"
    run([args.opt, "--ctnative-print-deduced", "--mlir-print-debuginfo", str(native), "-o", str(deduced)])
    decisions = []
    for label, ir in [("native", native), ("native-deduced", deduced)]:
        cpp = run([args.translate, "--mlir-to-cpp", str(ir)])
        counts = {}
        for name, expected in [("add", 0), ("ignored", 1), ("erased", 1)]:
            symbol = re.search(r"\b(" + name + r"_\d+)\(", cpp).group(1)
            body = function_body(cpp, symbol)
            counts[name] = body.count("static_cast<void>")
            assert counts[name] == expected, (label, name, body)
        decisions.append(counts)
        compile_and_run(label, cpp, expected="addResult=42\nerasedResult=42\nignoredResult=42\nretainedResult=42\n")
    assert decisions[0] == decisions[1], decisions
    print("unused parameters: exact suppressions retained, GCC/Clang clean, hoisted and deduced behavior agrees")


if __name__ == "__main__":
    main()
