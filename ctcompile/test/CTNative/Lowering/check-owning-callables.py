#!/usr/bin/env python3
"""Check named returned lambdas, owning captures, and concrete callable behavior."""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess


def run(command, *, environment=None):
    result = subprocess.run(command, text=True, capture_output=True, timeout=120,
                            env=environment)
    if result.returncode:
        raise RuntimeError(f"{command!r}\n{result.stdout}{result.stderr}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    tests = Path(__file__).resolve().parents[2]
    compilers = []
    for choices in [("g++-13", "g++"), ("clang++-18", "clang++")]:
        compiler = next((shutil.which(name) for name in choices if shutil.which(name)), None)
        if not compiler:
            raise RuntimeError("owning callable regression requires " + " or ".join(choices))
        compilers.append(compiler)
    fixtures = {
        "owning": "independentResult=101\nlifetimeResult=42\nloopResult=16\nmutationResult=42\nsharedResult=15\n",
        "scalar-string": "keywordResult=42\nscalarResult=1342\nstringResult=11\n",
        "fallback": "fallbackResult=42\n",
    }
    emitted = []
    for fixture, expected in fixtures.items():
        module = args.work / f"{fixture}.mlir"
        run(["cmake", f"-DTRANSLATE={args.translate}", f"-DOPT={args.opt}",
             f"-DSOURCE={args.fixtures / (fixture + '.js')}", f"-DOUTPUT={module}",
             "-DOPTIMIZE=OFF", "-P", str(tests / "native-pipeline.cmake")])
        deduced = args.work / f"{fixture}-deduced.mlir"
        run([args.opt, "--ctnative-print-deduced", "--mlir-print-debuginfo", str(module),
             "-o", str(deduced)])
        decisions = []
        for label, ir in [("plain", module), ("deduced", deduced)]:
            cpp = run([args.translate, "--mlir-to-cpp", str(ir)])
            assert "std::function<js_num(js_num)>" in cpp, cpp
            assert "#include <functional>" in cpp, cpp
            if fixture == "fallback":
                assert "std::tuple<js_num>" in cpp and "std::make_tuple(" in cpp, cpp
                assert "std::get<0>" in cpp, cpp
            else:
                assert "std::make_tuple(" not in cpp and "std::get<" not in cpp, cpp
            assert "ctnative::invoke_callable(" in cpp, cpp
            lambdas = re.findall(
                r"ctnative::ctn_env_\w+ const (ctn_lambda\w*) = \[([^\]]*)\]\(([^\n]*)\) -> ([^\n]+) \{",
                cpp)
            assert lambdas, cpp
            for name, captures, parameters, result in lambdas:
                assert "&" not in captures and "mutable" not in parameters, captures
                assert re.search(rf"return {re.escape(name)};", cpp), cpp
                for capture in captures.split(", ") if captures else []:
                    assert re.fullmatch(r"(capture_\w+) = std::move\(\1\)", capture), capture
            if fixture == "owning":
                assert "capture_state = std::move(capture_state)" in cpp, cpp
                assert "js_num const argument_delta" in cpp, cpp
                assert re.search(r"const state = ctnative::", cpp), cpp
                # The original source body now lives inside the owning lambda;
                # neither a forwarding call nor an unused lifted definition remains.
                assert not re.search(r"\bfn_2\s*\(", cpp), cpp
                assert "ctnative::map_get(capture_state," in cpp, cpp
                assert "static_cast<void>(capture_state)" not in cpp, cpp
                assert "static_cast<void>(argument_delta)" not in cpp, cpp
            elif fixture == "scalar-string":
                assert "std::function<std::string(std::string)>" in cpp, cpp
                assert "capture_template = std::move(capture_template)" in cpp, cpp
                assert "js_num const argument_concept" in cpp, cpp
            decisions.append(lambdas)
            source = args.work / f"{fixture}-{label}.cpp"
            source.write_text(cpp)
            emitted.append((source, expected))
            for index, compiler in enumerate(compilers):
                binary = (args.work / f"{fixture}-{label}-{index}").resolve()
                run([compiler, "-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror",
                     "-Wconversion", "-pedantic", "-ffp-contract=off", str(source), "-o", str(binary)])
                assert run([str(binary)]) == expected
        assert decisions[0] == decisions[1], decisions

    # Clang's address sanitizer catches a borrowed factory frame even when the
    # unsanitized stack happens to keep its old bytes. Long strings additionally
    # exercise heap ownership; leak checks cover std::function/shared_ptr cleanup.
    environment = dict(os.environ, ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
                       UBSAN_OPTIONS="halt_on_error=1")
    for source, expected in emitted:
        binary = source.with_suffix(".sanitized").resolve()
        run([compilers[1], "-std=c++23", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
             "-Wconversion", "-pedantic", "-ffp-contract=off", "-fno-omit-frame-pointer",
             "-fsanitize=address,undefined", "-fsanitize-address-use-after-scope",
             str(source), "-o", str(binary)])
        assert run([str(binary)], environment=environment) == expected
    print("owning callables: 9 observations agree, plain/deduced, GCC/Clang and ASan/UBSan")


if __name__ == "__main__":
    main()
