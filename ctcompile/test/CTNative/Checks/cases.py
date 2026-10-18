# CASES below copies each case's RUN lines from its .test file until that
# case's switch commit deletes them there; until then, edit both copies.
"""The CTNative/Checks lit case whose Python step runs among other RUN lines, as a
pytest test. test_case is parametrized over CASES, whose keys are the case's
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
}


@pytest.mark.parametrize("case", CASES)
def test_case(lit, case):
    lit_lines.run(lit, HERE / f"{case}.test", CASES[case], MAINS)
