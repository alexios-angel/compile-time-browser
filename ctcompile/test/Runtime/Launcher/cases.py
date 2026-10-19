# CASES below copies each case's RUN lines from its .test file until that
# case's switch commit deletes them there; until then, edit both copies.
"""The Runtime/Launcher lit cases as pytest tests: test_case is parametrized
over CASES, whose keys are the cases' names without .test, so
`test_case[page]` is lit's `ctcompile :: Runtime/Launcher/page.test`. Each test
runs all of its case's RUN lines in order through lit_lines.run, every line
under bash, as lit runs them.
"""

from pathlib import Path

import pytest

import lit_lines

HERE = Path(__file__).resolve().parent
MAINS = {}

CASES = {
    "application": [
        "ctcompile-test-launcher-vm > %t.vm.out 2> %t.vm.err",
        "ctcompile-test-launcher-aot > %t.aot.out 2> %t.aot.err",
        "diff %t.vm.out %t.aot.out",
        "test -s %t.vm.out",
        "FileCheck %s --check-prefix=AOT < %t.aot.err",
        "FileCheck %s --check-prefix=VM < %t.vm.err",
    ],
    "page": [
        "env CTBROWSER_FONTS=font8x8 CTBROWSER_NETWORK=0 ctcompile-test-launcher-page-vm %t.vm.ppm 2> %t.vm.err",
        "env CTBROWSER_FONTS=font8x8 CTBROWSER_NETWORK=0 ctcompile-test-launcher-page-aot %t.aot.ppm 2> %t.aot.err",
        "cmp %t.vm.ppm %t.aot.ppm",
        "test $(stat -c %%s %t.aot.ppm) -ge 230400",
        "FileCheck %s --check-prefix=AOT < %t.aot.err",
        "FileCheck %s --check-prefix=VM < %t.vm.err",
    ],
}


@pytest.mark.parametrize("case", CASES)
def test_case(lit, case):
    mlir = HERE / f"{case}.mlir"
    lit_lines.run(lit, mlir if mlir.is_file() else HERE / f"{case}.test", CASES[case], MAINS)
