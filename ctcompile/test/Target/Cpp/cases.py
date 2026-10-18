# CASES below copies each case's RUN lines from its .mlir file until that
# case's switch commit deletes them there; until then, edit both copies.
"""The Target/Cpp lit cases (upstream/ has its own cases.py) as pytest tests.
test_case is parametrized over CASES, whose keys are the cases' paths relative
to this folder without .mlir: `test_case[member-call]` is lit's `ctcompile ::
Target/Cpp/member-call.mlir`. Each test runs all of its case's RUN lines in
order through lit_lines.run: a case's own Python driver (six of them have one)
in this process, and split-file, ctjs-translate, ctjs-opt, FileCheck and the
compiled programs under bash, as lit runs them.
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
    "exceptions": [
        "split-file %s %t",
        "ctjs-translate --mlir-to-cpp %t/valid.mlir > %t/explicit.cpp",
        "FileCheck %s --check-prefixes=COMMON,EXPLICIT < %t/explicit.cpp",
        "ctjs-opt %t/valid.mlir --ctnative-print-deduced --mlir-print-debuginfo -o %t/deduced.mlir",
        "ctjs-translate --mlir-to-cpp %t/deduced.mlir > %t/deduced.cpp",
        "FileCheck %s --check-prefixes=COMMON,DEDUCED < %t/deduced.cpp",
        "ctjs-translate --mlir-to-cpp --declare-variables-at-top %t/deduced.mlir > %t/hoisted.cpp",
        "FileCheck %s --check-prefix=HOISTED < %t/hoisted.cpp",
        "%gxx %t/explicit.cpp -o %t/explicit-gcc",
        "%t/explicit-gcc",
        "%clangxx %t/explicit.cpp -o %t/explicit-clang",
        "%t/explicit-clang",
        "%gxx %t/deduced.cpp -o %t/deduced-gcc",
        "%t/deduced-gcc",
        "%clangxx %t/deduced.cpp -o %t/deduced-clang",
        "%t/deduced-clang",
        "%gxx %t/hoisted.cpp -o %t/hoisted-gcc",
        "%t/hoisted-gcc",
        "%clangxx %t/hoisted.cpp -o %t/hoisted-clang",
        "%t/hoisted-clang",
        "not ctjs-opt %t/empty-region.mlir 2>&1 | FileCheck %s --check-prefix=EMPTY-REGION",
        "not ctjs-opt %t/empty-block.mlir 2>&1 | FileCheck %s --check-prefix=EMPTY-BLOCK",
        "not ctjs-opt %t/catch-arity.mlir 2>&1 | FileCheck %s --check-prefix=CATCH",
        "not ctjs-opt %t/catch-type.mlir 2>&1 | FileCheck %s --check-prefix=CATCH",
        "not ctjs-opt %t/unterminated.mlir 2>&1 | FileCheck %s --check-prefix=TRY-END",
        "not ctjs-opt %t/wrong-terminator.mlir 2>&1 | FileCheck %s --check-prefix=TRY-END",
        "not ctjs-opt %t/wrong-parent.mlir 2>&1 | FileCheck %s --check-prefix=PARENT",
        "not ctjs-opt %t/throw-type.mlir 2>&1 | FileCheck %s --check-prefix=THROW-TYPE",
        "not ctjs-opt %t/throw-fallthrough.mlir 2>&1 | FileCheck %s --check-prefix=THROW-FALLTHROUGH",
    ],
    "member-call": [
        "ctjs-translate --mlir-to-cpp %s | FileCheck %s",
        "python3 %S/member-call.py --translate ctjs-translate --source %s --work %t.run",
    ],
    "native/nan": [
        "split-file %s %t",
        "ctjs-translate --mlir-to-cpp %t/native.mlir > %t/native.cpp",
        "FileCheck %s --check-prefix=NATIVE < %t/native.cpp",
        "%gxx -O2 %t/native.cpp -o %t/gcc && %t/gcc",
        "%clangxx -O2 %t/native.cpp -o %t/clang && %t/clang",
        "ctjs-translate --mlir-to-cpp %t/ordinary.mlir | FileCheck %s --check-prefix=ORDINARY",
        "ctjs-translate --mlir-to-cpp %t/wrong-marker.mlir | FileCheck %s --check-prefix=ORDINARY",
    ],
    "native/number": [
        "ctjs-translate --mlir-to-cpp %s > %t.explicit.cpp",
        "FileCheck %s --check-prefix=CPP < %t.explicit.cpp",
        "ctjs-opt --ctnative-print-deduced --mlir-print-debuginfo %s | ctjs-translate --mlir-to-cpp > %t.deduced.cpp",
        "FileCheck %s --check-prefix=DEDUCED < %t.deduced.cpp",
        "ctjs-opt --ctnative-print-deduced --mlir-print-debuginfo %s | ctjs-translate --mlir-to-cpp --declare-variables-at-top > %t.hoisted.cpp",
        "%gxx -O2 %t.explicit.cpp -o %t.gcc && %t.gcc",
        "%clangxx -O2 %t.explicit.cpp -o %t.clang && %t.clang",
        "%gxx -O2 %t.deduced.cpp -o %t.gcc && %t.gcc",
        "%clangxx -O2 %t.deduced.cpp -o %t.clang && %t.clang",
        "%gxx -O2 %t.hoisted.cpp -o %t.gcc && %t.gcc",
        "%clangxx -O2 %t.hoisted.cpp -o %t.clang && %t.clang",
        "ctjs-opt --ctnative-print-deduced=mutate=1 --mlir-print-debuginfo %s | ctjs-translate --mlir-to-cpp > %t.mutated.cpp",
        "not %gxx -O2 %t.mutated.cpp -o %t.mutated 2>&1 | FileCheck %s --check-prefix=MUTATED",
    ],
    "native/string": [
        "split-file %s %t",
        "ctjs-translate --mlir-to-cpp %t/valid.mlir > %t.explicit.cpp",
        "FileCheck %s --check-prefix=CPP < %t.explicit.cpp",
        "ctjs-opt --ctnative-print-deduced --mlir-print-debuginfo %t/valid.mlir | ctjs-translate --mlir-to-cpp > %t.deduced.cpp",
        "FileCheck %s --check-prefix=DEDUCED < %t.deduced.cpp",
        "%gxx -O2 %t.explicit.cpp -o %t.gcc && %t.gcc",
        "%clangxx -O2 %t.explicit.cpp -o %t.clang && %t.clang",
        "%gxx -O2 %t.deduced.cpp -o %t.gcc && %t.gcc",
        "%clangxx -O2 %t.deduced.cpp -o %t.clang && %t.clang",
        "not ctjs-opt %t/borrowed-throw.mlir 2>&1 | FileCheck %s --check-prefix=BORROWED",
    ],
    "primitive-exceptions": [
        "split-file %s %t",
        "ctjs-translate --mlir-to-cpp %t/valid.mlir > %t/explicit.cpp",
        "FileCheck %s < %t/explicit.cpp",
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
        "not ctjs-opt %t/borrowed-catch.mlir 2>&1 | FileCheck %s --check-prefix=CATCH",
        "not ctjs-opt %t/borrowed-throw.mlir 2>&1 | FileCheck %s --check-prefix=THROW",
        "not ctjs-opt %t/reference-throw.mlir 2>&1 | FileCheck %s --check-prefix=THROW",
        "not ctjs-opt %t/pointer-throw.mlir 2>&1 | FileCheck %s --check-prefix=THROW",
        "not ctjs-opt %t/mixed-catch.mlir 2>&1 | FileCheck %s --check-prefix=MIXED",
        "not ctjs-opt %t/mixed-rethrow.mlir 2>&1 | FileCheck %s --check-prefix=MIXED",
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
