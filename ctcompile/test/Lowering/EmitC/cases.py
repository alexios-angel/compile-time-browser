# CASES below copies each case's RUN lines from its .mlir file until that
# case's switch commit deletes them there; until then, edit both copies.
"""The Lowering/EmitC lit cases as pytest tests. test_case is parametrized over
CASES, whose keys are the cases' lit paths relative to this folder without
.mlir: `test_case[globals]` is lit's `ctcompile :: Lowering/EmitC/globals.mlir`.
Each test runs all of its case's RUN lines in order through lit_lines.run,
every line under bash, as lit runs them.
"""

from pathlib import Path

import pytest

import lit_lines

HERE = Path(__file__).resolve().parent
MAINS = {}

CASES = {
    "arithmetic": [
        "ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null    | ctjs-opt --ctjs-lower-to-emitc --emitc-eliminate-block-arguments    | mlir-translate --mlir-to-cpp --declare-variables-at-top    | FileCheck %s",
        "ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null    | ctjs-opt --ctjs-lower-to-emitc --emitc-eliminate-block-arguments    | mlir-translate --mlir-to-cpp --declare-variables-at-top > %t.cpp    && %cxx %t.cpp",
    ],
    "bitwise": [
        "ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null    | FileCheck %s --check-prefix=IMPORT",
        "ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null    | ctjs-opt --ctjs-lower-to-emitc --emitc-eliminate-block-arguments    | mlir-translate --mlir-to-cpp --declare-variables-at-top    | FileCheck %s",
    ],
    "block-argument-hazard": [
        "mlir-translate %s --mlir-to-cpp --declare-variables-at-top | FileCheck %s",
    ],
    "block-arguments-eliminated": [
        "ctjs-opt %s --emitc-eliminate-block-arguments    | mlir-translate --mlir-to-cpp --declare-variables-at-top    | FileCheck %s",
        "ctjs-opt %s --emitc-eliminate-block-arguments    | mlir-translate --mlir-to-cpp --declare-variables-at-top > %t.cpp    && %cxx_exe %t.cpp -o %t.exe && %t.exe",
    ],
    "control-flow": [
        "ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null    | ctjs-opt --ctjs-lower-to-emitc --emitc-eliminate-block-arguments    | mlir-translate --mlir-to-cpp --declare-variables-at-top    | FileCheck %s",
        "ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null    | ctjs-opt --ctjs-lower-to-emitc --emitc-eliminate-block-arguments    | mlir-translate --mlir-to-cpp --declare-variables-at-top > %t.cpp    && %cxx %t.cpp",
    ],
    "end-to-end": [
        "ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null    | ctjs-opt --ctjs-lower-to-emitc    | mlir-translate --mlir-to-cpp --declare-variables-at-top    | FileCheck %s",
        "ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null    | ctjs-opt --ctjs-lower-to-emitc    | mlir-translate --mlir-to-cpp --declare-variables-at-top > %t.cpp    && %cxx %t.cpp",
    ],
    "entry-shape": [
        "mlir-translate %s --mlir-to-cpp --declare-variables-at-top | FileCheck %s",
        "mlir-translate %s --mlir-to-cpp --declare-variables-at-top > %t.cpp && %cxx %t.cpp",
    ],
    "globals": [
        "ctjs-opt %s --ctjs-lower-to-emitc --emitc-eliminate-block-arguments    | mlir-translate --mlir-to-cpp --declare-variables-at-top    | FileCheck %s",
        "ctjs-opt %s --ctjs-lower-to-emitc --emitc-eliminate-block-arguments    | mlir-translate --mlir-to-cpp --declare-variables-at-top > %t.cpp    && %cxx %t.cpp",
    ],
    "lift-to-scf": [
        "ctjs-opt %s --ctjs-lift-to-scf | FileCheck %s",
        "ctjs-opt %s '--ctjs-lift-to-scf=report=true' 2>&1 | FileCheck --check-prefix=REPORT %s",
    ],
    "lift-to-scf-handlers": [
        "ctjs-opt %s --ctjs-lift-to-scf | FileCheck %s --implicit-check-not='{{[^s]cf\\.}}'",
    ],
    "operators": [
        "ctjs-opt %s --ctjs-lower-to-emitc --emitc-eliminate-block-arguments    | mlir-translate --mlir-to-cpp --declare-variables-at-top    | FileCheck %s",
        "ctjs-opt %s --ctjs-lower-to-emitc --emitc-eliminate-block-arguments    | mlir-translate --mlir-to-cpp --declare-variables-at-top > %t.cpp    && %cxx %t.cpp",
    ],
    "refusals": [
        "ctjs-opt %s --ctjs-lower-to-emitc | FileCheck %s",
        "not ctjs-opt %s --emitc-eliminate-block-arguments 2>&1    | FileCheck --check-prefix=ORDER %s",
    ],
}


@pytest.mark.parametrize("case", CASES)
def test_case(lit, case):
    lit_lines.run(lit, HERE / f"{case}.mlir", CASES[case], MAINS)
