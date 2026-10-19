# CASES below copies each case's RUN lines from its .mlir file until that
# case's switch commit deletes them there; until then, edit both copies.
"""The CTJS/Import lit cases, one with a Python step among its RUN lines and
the others without, as pytest tests. test_case is parametrized over CASES,
whose keys are the cases' lit paths relative to this folder without .mlir:
`test_case[invocation-state]` is lit's `ctcompile ::
CTJS/Import/invocation-state.mlir`. Each test runs all of its case's RUN lines
in order through lit_lines.run, every line under bash, as lit runs them.
"""

from pathlib import Path

import lit_lines
import pytest

HERE = Path(__file__).resolve().parent
MAINS = {}

CASES = {
    "add": [
        "ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null | ctjs-opt | FileCheck %s",
    ],
    "async": [
        "ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null | ctjs-opt | FileCheck %s --check-prefix=LOWERED",
        "ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null | ctjs-opt | FileCheck %s --check-prefix=REFUSED",
        "ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null | ctjs-opt --ctjs-lower-to-emitc | FileCheck %s --check-prefix=EMITTED",
    ],
    "engine-helpers": [
        "ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null | FileCheck %s",
    ],
    "generator-invocation": [
        "ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null | ctjs-opt | FileCheck %s",
    ],
    "global-read-mode": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js 2>/dev/null | ctjs-opt | FileCheck %s",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/refused.js 2>/dev/null | ctjs-opt | FileCheck %s --check-prefix=REFUSED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/pending.js 2>/dev/null | ctjs-opt | FileCheck %s --check-prefix=PENDING",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/pending.js 2>/dev/null | ctjs-opt --ctjs-lower-to-emitc | mlir-translate --mlir-to-cpp --declare-variables-at-top | FileCheck %s --check-prefix=PENDINGCPP",
    ],
    "hoisted-vars": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | ctjs-opt | FileCheck %s",
        "ctjs-translate --ctbrowser-module-to-ctjs %t/source.js | ctjs-opt | FileCheck %s --check-prefix=MODULE",
    ],
    "invocation-state": [
        "split-file %s %t",
        "python3 %S/invocation-state.py --translate ctjs-translate --opt ctjs-opt --fixtures %t --work %t.check --node %node --reference %native_reference",
    ],
    "literal/definitions": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/keys.js 2>/dev/null | ctjs-opt | FileCheck %s --check-prefix=KEYS",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/computed.js 2>/dev/null | ctjs-opt | FileCheck %s --check-prefix=COMPUTED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/proto.js 2>/dev/null | ctjs-opt | FileCheck %s --check-prefix=PROTO",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/class.js 2>/dev/null | ctjs-opt | FileCheck %s --check-prefix=CLASS",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/native.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/boxed.js 2>/dev/null | ctjs-opt --ctjs-lower-to-emitc | mlir-translate --mlir-to-cpp --declare-variables-at-top | FileCheck %s --check-prefix=BOXED --implicit-check-not=ct_aot_set_index --implicit-check-not=__ctbrowser_define_own",
    ],
    "literal/helper-identity": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/result.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=RESULT --implicit-check-not=ctjs.define_own",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/capture.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=CAPTURE --implicit-check-not=ctjs.define_own",
        "%native_reference %t/result.js | FileCheck %s --check-prefix=VALUE",
        "%native_reference %t/capture.js | FileCheck %s --check-prefix=VALUE",
    ],
    "loop-property": [
        "ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null | ctjs-opt | FileCheck %s",
    ],
    "source-lines": [
        "ctjs-translate --ctbrowser-js-to-ctjs %s --mlir-print-debuginfo    | FileCheck %s",
    ],
    "unsupported": [
        "ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null | ctjs-opt | FileCheck %s",
    ],
}


@pytest.mark.parametrize("case", CASES)
def test_case(lit, case):
    lit_lines.run(lit, HERE / f"{case}.mlir", CASES[case], MAINS)
