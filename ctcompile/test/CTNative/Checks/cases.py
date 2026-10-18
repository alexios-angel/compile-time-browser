# CASES below copies each case's RUN lines from its .test file until that
# case's switch commit deletes them there; until then, edit both copies.
"""The CTNative/Checks lit cases with no test in a driver of their own, as
pytest tests. test_case is parametrized over CASES, whose keys are the cases'
lit paths relative to this folder without .test: `test_case[capture-census]` is
lit's `ctcompile :: CTNative/Checks/capture-census.test`. Each test runs all of
its case's RUN lines in order through lit_lines.run, every line under bash, as
lit runs them.
"""

from pathlib import Path

import pytest

import lit_lines

HERE = Path(__file__).resolve().parent
MAINS = {}

CASES = {
    "capture-census": [
        "ctjs-translate --ctbrowser-js-to-ctjs %{monorepo}/ctbrowser/vendor/bootstrap/bootstrap.bundle.js -o %t.mlir",
        "python3 %{monorepo}/tools/check/capture-census.py --expect-indexed 219 --expect-operands 1023 %t.mlir",
    ],
    "deduction-probe": [
        "%gxx -std=c++20 %S/../../../probes/deduction.cpp -o %t.gxx && %t.gxx",
        "%clangxx -std=c++20 %S/../../../probes/deduction.cpp -o %t.clangxx && %t.clangxx",
        "%host_cxx -std=c++20 -pedantic -Wall -Wextra -Werror -Wconversion %S/../../../probes/deduction.cpp -o %t.host && %t.host",
    ],
}


@pytest.mark.parametrize("case", CASES)
def test_case(lit, case):
    lit_lines.run(lit, HERE / f"{case}.test", CASES[case], MAINS)
