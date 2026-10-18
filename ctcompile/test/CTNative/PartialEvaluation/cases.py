# CASES below copies each case's RUN lines from its .mlir file until that
# case's switch commit deletes them there; until then, edit both copies.
"""The CTNative/PartialEvaluation lit cases whose Python step runs among other RUN
lines, as pytest tests. test_case is parametrized over CASES, whose keys are
the cases' lit paths relative to this folder without .mlir: `test_case[long-
keys]` is lit's `ctcompile :: CTNative/PartialEvaluation/long-keys.mlir`. Each
test runs all of its case's RUN lines in order through lit_lines.run, every
line under bash, as lit runs them.
"""

from pathlib import Path

import pytest

import lit_lines

HERE = Path(__file__).resolve().parent
MAINS = {}

CASES = {
    "call/proof": [
        "split-file %s %t",
        "ctjs-opt %t/effects.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=EFFECTS",
        "ctjs-opt %t/effects.mlir --ctnative-partial-evaluate --ctnative-partial-evaluate | FileCheck %s --check-prefix=EFFECTS",
        "ctjs-opt %t/equal-effects.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=EQUAL",
        "ctjs-opt %t/anchors.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=ANCHORS",
        "ctjs-opt %t/fresh.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=FRESH",
        "ctjs-opt %t/fresh.mlir --ctnative-partial-evaluate --ctnative-partial-evaluate | FileCheck %s --check-prefix=FRESH",
        "ctjs-opt %t/fresh.mlir '--ctnative-partial-evaluate=max-nodes=3' | FileCheck %s --check-prefix=NODES",
        "ctjs-opt %t/sharing.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=SHARING",
        "python3 -c \"from pathlib import Path; p=Path(r'%t/sharing.mlir'); s=p.read_text(); a='private @original'+chr(36)+'1('; b='private @variant('; Path(r'%t/reverse-sharing.mlir').write_text(s.replace(a, 'private @temporary(').replace(b, a).replace('private @temporary(', b))\"",
        "ctjs-opt %t/reverse-sharing.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=SHARING",
        "ctjs-opt %t/zero.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=ZERO",
        "ctjs-opt %t/order.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=ORDER",
    ],
    "call/targets": [
        "split-file %s %t",
        "ctjs-opt %t/globals.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=GLOBAL",
        "python3 -c \"from pathlib import Path; p=Path(r'%t/globals.mlir'); s=p.read_text(); Path(r'%t/rebound.mlir').write_text(s.replace('    ctjs.return %result', '    ctjs.store_global \\\"helper\\\", %u\\n    ctjs.return %result'))\"",
        "ctjs-opt %t/rebound.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=UNPROVED",
        "python3 -c \"from pathlib import Path; p=Path(r'%t/globals.mlir'); s=p.read_text(); Path(r'%t/late.mlir').write_text(s.replace('    ctjs.store_global \\\"helper\\\", %closure', '    %effect = ctjs.binary add %u, %u\\n    ctjs.store_global \\\"helper\\\", %closure'))\"",
        "ctjs-opt %t/late.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=UNPROVED",
        "python3 -c \"from pathlib import Path; p=Path(r'%t/globals.mlir'); s=p.read_text(); Path(r'%t/missing.mlir').write_text(s.replace('    ctjs.store_global \\\"helper\\\", %closure', ''))\"",
        "ctjs-opt %t/missing.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=UNPROVED",
        "ctjs-opt %t/budget.mlir '--ctnative-partial-evaluate=max-steps=11' | FileCheck %s --check-prefix=ENOUGH",
        "ctjs-opt %t/budget.mlir '--ctnative-partial-evaluate=max-steps=6' | FileCheck %s --check-prefix=EXECUTION",
        "ctjs-opt %t/budget.mlir '--ctnative-partial-evaluate=max-steps=9' | FileCheck %s --check-prefix=PROOF",
        "ctjs-opt %t/budget.mlir '--ctnative-partial-evaluate=max-depth=1' | FileCheck %s --check-prefix=DEPTH",
        "python3 -c \"from pathlib import Path; p=Path(r'%t/budget.mlir'); Path(r'%t/exact.mlir').write_text(p.read_text().replace('ctjs.call_direct @variant', 'ctjs.call_direct @original'+chr(36)+'1'))\"",
        "ctjs-opt %t/exact.mlir '--ctnative-partial-evaluate=max-steps=6' | FileCheck %s --check-prefix=ENOUGH",
        "python3 -c \"from pathlib import Path; p=Path(r'%t/budget.mlir'); Path(r'%t/unknown.mlir').write_text(p.read_text().replace('ctjs.constant #ctjs.number<4631107791820423168>', 'ub.poison : !ctjs.value', 1))\"",
        "ctjs-opt %t/unknown.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=UNKNOWN",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/specialized.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-specialize --ctnative-partial-evaluate | FileCheck %s --check-prefix=SPECIALIZED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/specialized.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-specialize --ctnative-partial-evaluate --ctnative-partial-evaluate | FileCheck %s --check-prefix=SPECIALIZED",
    ],
    "long-keys": [
        "split-file %s %t",
        "python3 -c \"from pathlib import Path; Path(r'%t/long-keys.js').write_text(Path(r'%t/long-keys.js.in').read_text().replace('LONG_KEY', 'x' * 65537))\"",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/long-keys.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-partial-evaluate | FileCheck %s --check-prefix=LONG",
    ],
}


@pytest.mark.parametrize("case", CASES)
def test_case(lit, case):
    lit_lines.run(lit, HERE / f"{case}.mlir", CASES[case], MAINS)
