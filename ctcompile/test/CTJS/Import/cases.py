# CASES below copies each case's RUN lines from its .mlir file until that
# case's switch commit deletes them there; until then, edit both copies.
"""The CTJS/Import lit case whose Python step runs among other RUN lines, as a
pytest test. test_case is parametrized over CASES, whose keys are the case's
lit paths relative to this folder without .mlir: `test_case[invocation-state]`
is lit's `ctcompile :: CTJS/Import/invocation-state.mlir`. Each test runs all
of its case's RUN lines in order through lit_lines.run, every line under bash,
as lit runs them.
"""

from pathlib import Path

import pytest

import lit_lines

HERE = Path(__file__).resolve().parent
MAINS = {}

CASES = {
    "invocation-state": [
        "split-file %s %t",
        "python3 %S/invocation-state.py --translate ctjs-translate --opt ctjs-opt --fixtures %t --work %t.check --node %node --reference %native_reference",
    ],
}


@pytest.mark.parametrize("case", CASES)
def test_case(lit, case):
    lit_lines.run(lit, HERE / f"{case}.mlir", CASES[case], MAINS)
