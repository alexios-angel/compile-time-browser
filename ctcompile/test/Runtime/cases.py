"""Runtime lit cases mirrored under pytest."""

from pathlib import Path

import pytest

import lit_lines

HERE = Path(__file__).resolve().parent
CASES = {
    "format-core-free": [
        "split-file %s %t",
        "%gxx -O1 %t/main.cpp -o %t/gcc",
        "%clangxx -O1 %t/main.cpp -o %t/clang",
        "%nm -C %t/gcc | FileCheck %s --check-prefix=SYMBOLS --implicit-check-not=ctbrowser::number_to_string --implicit-check-not=ctbrowser::script:: --implicit-check-not=ctbrowser::aot::",
        "%nm -C %t/clang | FileCheck %s --check-prefix=SYMBOLS --implicit-check-not=ctbrowser::number_to_string --implicit-check-not=ctbrowser::script:: --implicit-check-not=ctbrowser::aot::",
        "%t/gcc | FileCheck %s --check-prefix=OUTPUT --match-full-lines",
        "%t/clang | FileCheck %s --check-prefix=OUTPUT --match-full-lines",
    ],
}


@pytest.mark.parametrize("case", CASES)
def test_case(lit, case):
    lit_lines.run(lit, HERE / f"{case}.test", CASES[case], {})
