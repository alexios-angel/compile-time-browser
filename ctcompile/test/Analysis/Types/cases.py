# CASES below copies each case's RUN lines from its .test file until that
# case's switch commit deletes them there; until then, edit both copies.
"""The Analysis/Types lit cases whose Python step runs among other RUN lines,
as pytest tests. test_case is parametrized over CASES, whose keys are the
cases' lit paths relative to this folder without .test: `test_case[claims/p5]`
is lit's `ctcompile :: Analysis/Types/claims/p5.test`. Each test runs all of
its case's RUN lines in order through lit_lines.run; every line, the type
recorders and tools/check/type-oracle.py piped into FileCheck included, runs
under bash, as lit runs it.
"""

from pathlib import Path

import pytest

import lit_lines

HERE = Path(__file__).resolve().parent
MAINS = {}

CASES = {
    "claims/array-callbacks": [
        "ctcompile-test-type-oracle",
        "ctcompile-test-type-oracle --script %S/../../../CTNative/Fixtures/Objects/array-callbacks.js --out %t.rec",
        "ctcompile-test-type-claims --script %S/../../../CTNative/Fixtures/Objects/array-callbacks.js --out %t.claims 2>&1 | FileCheck %s --check-prefix=CLAIMS",
        "python3 %{monorepo}/tools/check/type-oracle.py --recording %t.rec --claims %t.claims --name array_callbacks --max-report 0 --expect-violations 0 | FileCheck %s",
    ],
    "claims/bootstrap": [
        "ctcompile-test-type-oracle",
        "cat %{monorepo}/ctbrowser/vendor/bootstrap/bootstrap.bundle.js > %t.js && echo >> %t.js && cat %S/../bootstrap-driver.js >> %t.js",
        "ctcompile-test-type-oracle --script %t.js --out %t.rec",
        "ctcompile-test-type-claims --script %t.js --out %t.claims 2>&1 | FileCheck %s --check-prefix=CLAIMS",
        "python3 %{monorepo}/tools/check/type-oracle.py --recording %t.rec --claims %t.claims --name bootstrap --max-report 0 --expect-violations 0 | FileCheck %s",
    ],
    "claims/fixture": [
        "ctcompile-test-type-oracle",
        "ctcompile-test-type-oracle --script %S/fixture.js --out %t.rec",
        "ctcompile-test-type-claims --script %S/fixture.js --out %t.claims 2>&1 | FileCheck %s --check-prefix=CLAIMS",
        "python3 %{monorepo}/tools/check/type-oracle.py --recording %t.rec --claims %t.claims --name fixture --max-report 0 --expect-violations 0 | FileCheck %s",
    ],
    "claims/p5": [
        "ctcompile-test-type-oracle",
        "ctcompile-test-type-oracle --script %{monorepo}/ctbrowser/vendor/p5/p5.js --out %t.rec",
        "ctcompile-test-type-claims --script %{monorepo}/ctbrowser/vendor/p5/p5.js --out %t.claims 2>&1 | FileCheck %s --check-prefix=CLAIMS",
        "python3 %{monorepo}/tools/check/type-oracle.py --recording %t.rec --claims %t.claims --name p5 --max-report 0 --expect-violations 0 | FileCheck %s",
    ],
    "claims/phaser": [
        "ctcompile-test-type-oracle",
        "ctcompile-test-type-oracle --script %{monorepo}/ctbrowser/vendor/phaser/phaser.js --out %t.rec",
        "ctcompile-test-type-claims --script %{monorepo}/ctbrowser/vendor/phaser/phaser.js --out %t.claims 2>&1 | FileCheck %s --check-prefix=CLAIMS",
        "python3 %{monorepo}/tools/check/type-oracle.py --recording %t.rec --claims %t.claims --name phaser --max-report 0 --expect-violations 0 | FileCheck %s",
    ],
    "claims/scalar-unions": [
        "ctcompile-test-type-oracle",
        "ctcompile-test-type-oracle --script %S/../../../CTNative/Fixtures/Scalars/scalar-unions.js --out %t.rec",
        "ctcompile-test-type-claims --script %S/../../../CTNative/Fixtures/Scalars/scalar-unions.js --out %t.claims 2>&1 | FileCheck %s --check-prefix=CLAIMS",
        "python3 %{monorepo}/tools/check/type-oracle.py --recording %t.rec --claims %t.claims --name scalar_unions --max-report 0 --expect-violations 0 | FileCheck %s",
    ],
}


@pytest.mark.parametrize("case", CASES)
def test_case(lit, case):
    lit_lines.run(lit, HERE / f"{case}.test", CASES[case], MAINS)
