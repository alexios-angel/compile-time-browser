"""Runtime lit cases mirrored under pytest."""

from pathlib import Path

import pytest

import lit_lines

HERE = Path(__file__).resolve().parent
CASES = {
    "format-core-free": [
        "split-file %s %t",
        "%gxx --core-free -O1 %t/main.cpp -o %t/gcc",
        "%clangxx --core-free -O1 %t/main.cpp -o %t/clang",
        "%nm -C %t/gcc | FileCheck %s --check-prefix=SYMBOLS --implicit-check-not=ctbrowser::number_to_string --implicit-check-not=ctbrowser::script:: --implicit-check-not=ctbrowser::aot::",
        "%nm -C %t/clang | FileCheck %s --check-prefix=SYMBOLS --implicit-check-not=ctbrowser::number_to_string --implicit-check-not=ctbrowser::script:: --implicit-check-not=ctbrowser::aot::",
        "%t/gcc | FileCheck %s --check-prefix=OUTPUT --match-full-lines",
        "%t/clang | FileCheck %s --check-prefix=OUTPUT --match-full-lines",
        "%gxx --core-free -O1 %t/number.cpp -o %t/number",
        "%t/number | FileCheck %s --check-prefix=NUMBER --match-full-lines",
        "not %gxx --core-free -O1 -DMUTATE_CORE_LINK %t/number.cpp -o %t/number-mutated 2>&1 | FileCheck %s --check-prefix=LINK",
        "%clangxx --core-free -c %t/number.cpp -o %t/number.o",
        "python3 %S/native_link_checks.py --build %build",
    ],
}


@pytest.mark.parametrize("case", CASES)
def test_case(lit, case):
    lit_lines.run(lit, HERE / f"{case}.test", CASES[case], {})
