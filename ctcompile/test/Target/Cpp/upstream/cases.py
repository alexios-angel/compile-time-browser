# CASES below copies each case's RUN lines from its .mlir file until that
# case's switch commit deletes them there; until then, edit both copies.
"""Upstream MLIR's Target/Cpp lit tests, run against this repository's fork
of the C++ emitter, as pytest tests: test_case is parametrized over CASES,
whose keys are the file names without .mlir, so `test_case[const]` is lit's
`ctcompile :: Target/Cpp/upstream/const.mlir`. As lit.local.cfg here does,
`mlir-translate` in their RUN lines names ctjs-translate, not LLVM's
mlir-translate; without a built ctjs-translate the tests are skipped, as lit
then marks the folder unsupported. Each test runs all of its case's RUN lines
in order through lit_lines.run, every line under bash, as lit runs them.
"""

from pathlib import Path

import pytest

import lit_lines

HERE = Path(__file__).resolve().parent
MAINS = {}

CASES = {
    "arithmetic_operators": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s",
    ],
    "attrs": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s",
    ],
    "bitwise_operators": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s",
    ],
    "call": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s -check-prefix=CPP-DEFAULT",
        "mlir-translate -mlir-to-cpp -declare-variables-at-top %s | FileCheck %s -check-prefix=CPP-DECLTOP",
    ],
    "cast": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s",
    ],
    "class": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s",
    ],
    "common-cpp": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s",
    ],
    "comparison_operators": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s",
    ],
    "conditional": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s",
    ],
    "const": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s -check-prefix=CPP-DEFAULT",
        "mlir-translate -mlir-to-cpp -declare-variables-at-top %s | FileCheck %s -check-prefix=CPP-DECLTOP",
    ],
    "control_flow": [
        "mlir-translate -mlir-to-cpp -declare-variables-at-top %s | FileCheck %s -check-prefix=CPP-DECLTOP",
    ],
    "declare_func": [
        "mlir-translate -mlir-to-cpp %s | FileCheck --match-full-lines %s",
    ],
    "do": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s -check-prefix=CPP-DEFAULT",
    ],
    "expressions": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s -check-prefix=CPP-DEFAULT",
        "mlir-translate -mlir-to-cpp -declare-variables-at-top %s | FileCheck %s -check-prefix=CPP-DECLTOP",
    ],
    "file": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s --check-prefix NO-FILTER",
        "mlir-translate -mlir-to-cpp -file-id=non-existing %s | FileCheck %s --check-prefix NON-EXISTING",
        "mlir-translate -mlir-to-cpp -file-id=file_one %s | FileCheck %s --check-prefix FILE-ONE",
        "mlir-translate -mlir-to-cpp -file-id=file_two %s | FileCheck %s --check-prefix FILE-TWO",
    ],
    "for": [
        "mlir-translate -mlir-to-cpp %s | FileCheck --match-full-lines %s -check-prefix=CPP-DEFAULT",
        "mlir-translate -mlir-to-cpp -declare-variables-at-top %s | FileCheck --match-full-lines %s -check-prefix=CPP-DECLTOP",
    ],
    "for_loop_induction_vars": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s",
    ],
    "func": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s -check-prefix=CPP-DEFAULT",
        "mlir-translate -mlir-to-cpp -declare-variables-at-top %s | FileCheck %s -check-prefix=CPP-DECLTOP",
    ],
    "global": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s -check-prefix=CPP-DEFAULT",
        "mlir-translate -mlir-to-cpp -declare-variables-at-top %s | FileCheck %s -check-prefix=CPP-DECLTOP",
    ],
    "if": [
        "mlir-translate -mlir-to-cpp %s | FileCheck --match-full-lines %s -check-prefix=CPP-DEFAULT",
        "mlir-translate -mlir-to-cpp -declare-variables-at-top %s | FileCheck --match-full-lines %s -check-prefix=CPP-DECLTOP",
    ],
    "invalid": [
        "mlir-translate -split-input-file -mlir-to-cpp -verify-diagnostics %s",
    ],
    "invalid_declare_variables_at_top": [
        "mlir-translate -split-input-file -declare-variables-at-top -mlir-to-cpp -verify-diagnostics %s",
    ],
    "literal": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s -check-prefix=CPP-DEFAULT",
        "mlir-translate -mlir-to-cpp -declare-variables-at-top %s | FileCheck %s -check-prefix=CPP-DECLTOP",
    ],
    "literal_call_operand": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s -check-prefix=CPP-DEFAULT",
        "mlir-translate -mlir-to-cpp -declare-variables-at-top %s | FileCheck %s -check-prefix=CPP-DECLTOP",
    ],
    "logical_operators": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s",
    ],
    "lvalue": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s",
    ],
    "member": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s -check-prefix=CPP-DEFAULT",
    ],
    "no_extra_semicolon": [
        "mlir-translate -mlir-to-cpp %s | FileCheck --match-full-lines %s",
        "mlir-translate -mlir-to-cpp -declare-variables-at-top %s | FileCheck --match-full-lines %s",
    ],
    "stdops": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s -check-prefix=CPP-DEFAULT",
        "mlir-translate -mlir-to-cpp -declare-variables-at-top %s | FileCheck %s -check-prefix=CPP-DECLTOP",
    ],
    "subscript": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s -check-prefix=CPP-DEFAULT",
        "mlir-translate -mlir-to-cpp -declare-variables-at-top %s | FileCheck %s -check-prefix=CPP-DECLTOP",
    ],
    "switch": [
        "mlir-translate -mlir-to-cpp %s | FileCheck --match-full-lines %s -check-prefix=CPP-DEFAULT",
        "mlir-translate -mlir-to-cpp -declare-variables-at-top %s | FileCheck --match-full-lines %s -check-prefix=CPP-DECLTOP",
    ],
    "types": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s",
    ],
    "unary_operators": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s",
    ],
    "variable": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s -check-prefix=CPP-DEFAULT",
        "mlir-translate -mlir-to-cpp -declare-variables-at-top %s | FileCheck %s -check-prefix=CPP-DECLTOP",
    ],
    "verbatim": [
        "mlir-translate -mlir-to-cpp %s | FileCheck %s --match-full-lines",
        "mlir-translate -mlir-to-cpp -declare-variables-at-top %s | FileCheck %s --match-full-lines",
    ],
}


@pytest.mark.parametrize("case", CASES)
def test_case(lit, case):
    translate = lit.tools["ctjs-translate"]
    if not Path(translate).is_file():
        pytest.skip("ctjs-translate is not built; lit.local.cfg marks this folder unsupported")
    # A new dict for this test only: other tests keep LLVM's mlir-translate.
    lit.tools = dict(lit.tools, **{"mlir-translate": translate})
    lit_lines.run(lit, HERE / f"{case}.mlir", CASES[case], MAINS)
