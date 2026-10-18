# CASES below copies each case's RUN lines from its .mlir file until that
# case's switch commit deletes them there; until then, edit both copies.
"""The Target/Cpp lit cases whose Python driver runs among other RUN lines, as
pytest tests. test_case is parametrized over CASES, whose keys are the cases'
names without .mlir: `test_case[member-call]` is lit's `ctcompile ::
Target/Cpp/member-call.mlir`. Each test runs all of its case's RUN lines in
order through lit_lines.run: the case's own driver in this process, and
split-file, ctjs-translate, ctjs-opt, FileCheck and the compiled programs under
bash, as lit runs them.
"""

import importlib
from pathlib import Path

import pytest

import lit_lines

HERE = Path(__file__).resolve().parent
# The drivers' names are hyphenated, so import_module, not import.
MAINS = {
    (HERE / f"{name}.py").resolve(): importlib.import_module(f"Target.Cpp.{name}").main
    for name in (
        "call-exceptions",
        "callable-bodies",
        "const-bindings",
        "member-call",
        "readable-floats",
        "unused-parameters",
    )
}

CASES = {
    "call-exceptions": [
        "split-file %s %t",
        "ctjs-translate --mlir-to-cpp %t/valid.mlir > %t/explicit.cpp",
        "ctjs-opt %t/valid.mlir --ctnative-print-deduced --mlir-print-debuginfo -o %t/deduced.mlir",
        "ctjs-translate --mlir-to-cpp %t/deduced.mlir > %t/deduced.cpp",
        "ctjs-translate --mlir-to-cpp --declare-variables-at-top %t/deduced.mlir > %t/hoisted.cpp",
        "%gxx -O2 %t/explicit.cpp -o %t/explicit-gcc && %t/explicit-gcc",
        "%clangxx -O2 %t/explicit.cpp -o %t/explicit-clang && %t/explicit-clang",
        "%gxx -O2 %t/deduced.cpp -o %t/deduced-gcc && %t/deduced-gcc",
        "%clangxx -O2 %t/deduced.cpp -o %t/deduced-clang && %t/deduced-clang",
        "%gxx -O2 %t/hoisted.cpp -o %t/hoisted-gcc && %t/hoisted-gcc",
        "%clangxx -O2 %t/hoisted.cpp -o %t/hoisted-clang && %t/hoisted-clang",
        "%clangxx -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fsanitize-address-use-after-scope %t/explicit.cpp -o %t/asan-explicit",
        "env ASAN_OPTIONS=detect_stack_use_after_return=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 %t/asan-explicit",
        "%clangxx -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fsanitize-address-use-after-scope %t/deduced.cpp -o %t/asan-deduced",
        "env ASAN_OPTIONS=detect_stack_use_after_return=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 %t/asan-deduced",
        "%python %S/call-exceptions.py --opt ctjs-opt --fixture %t/valid.mlir --work %t/controls",
        "ctjs-translate --mlir-to-cpp %t/controls/wrong-state.mlir > %t/wrong-state.cpp",
        "%gxx -O2 %t/wrong-state.cpp -o %t/wrong-state",
        "not %t/wrong-state",
    ],
    "callable-bodies": [
        "split-file %s %t",
        "ctjs-translate --mlir-to-cpp %t/callables.mlir > %t/callables.cpp",
        "ctjs-translate --mlir-to-cpp --declare-variables-at-top %t/callables.mlir > %t/hoisted.cpp",
        "python3 %S/callable-bodies.py --fixtures %t --work %t.executables --translate ctjs-translate",
    ],
    "const-bindings": [
        "split-file %s %t",
        "ctjs-translate --mlir-to-cpp %t/bindings.mlir > %t/bindings.cpp",
        "FileCheck %s --check-prefix=BINDINGS < %t/bindings.cpp",
        "ctjs-translate --mlir-to-cpp --declare-variables-at-top %t/bindings.mlir > %t/hoisted.cpp",
        "FileCheck %s --check-prefix=HOISTED < %t/hoisted.cpp",
        "ctjs-translate --mlir-to-cpp %t/isolation.mlir > %t/isolation.cpp",
        "FileCheck %s --check-prefix=ISOLATION < %t/isolation.cpp",
        "python3 %S/const-bindings.py --fixtures %t --work %t.executables",
    ],
    "member-call": [
        "ctjs-translate --mlir-to-cpp %s | FileCheck %s",
        "python3 %S/member-call.py --translate ctjs-translate --source %s --work %t.run",
    ],
    "readable-floats": [
        "split-file %s %t",
        "ctjs-translate --mlir-to-cpp %t/native.mlir | FileCheck %s --check-prefix=NATIVE",
        "ctjs-translate --mlir-to-cpp %t/unmarked.mlir | FileCheck %s --check-prefix=UPSTREAM",
        "ctjs-translate --mlir-to-cpp %t/scopes.mlir | FileCheck %s --check-prefix=SCOPES",
        "ctjs-translate --mlir-to-cpp %t/wrong-marker.mlir | FileCheck %s --check-prefix=UPSTREAM",
        "python3 %S/readable-floats.py --translate ctjs-translate --work %t.bits",
    ],
    "unused-parameters": [
        "split-file %s %t",
        "ctjs-translate --mlir-to-cpp %t/parameters.mlir > %t/parameters.cpp",
        "ctjs-translate --mlir-to-cpp --declare-variables-at-top %t/parameters.mlir > %t/hoisted.cpp",
        "ctjs-opt --canonicalize %t/cleanup.mlir | ctjs-translate --mlir-to-cpp > %t/cleanup.cpp",
        "python3 %S/unused-parameters.py --fixtures %t --work %t.executables --translate ctjs-translate --opt ctjs-opt",
    ],
}


@pytest.mark.parametrize("case", CASES)
def test_case(lit, case):
    lit_lines.run(lit, HERE / f"{case}.mlir", CASES[case], MAINS)
