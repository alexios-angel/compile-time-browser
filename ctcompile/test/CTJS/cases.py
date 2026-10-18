# CASES below copies each case's RUN lines from its .mlir file until that
# case's switch commit deletes them there; until then, edit both copies.
"""The CTJS lit cases with no pytest test elsewhere, as pytest tests:
test_case is parametrized over CASES, whose keys are the cases' lit paths
relative to this folder without .mlir, so `test_case[IR/invoke]` is lit's
`ctcompile :: CTJS/IR/invoke.mlir`. Each test runs all of its case's RUN lines
in order through lit_lines.run, every line under bash, as lit runs them.
CTJS/Import has its own cases.py.
"""

from pathlib import Path

import pytest

import lit_lines

HERE = Path(__file__).resolve().parent
MAINS = {}

CASES = {
    "IR/exceptions": [
        "split-file %s %t",
        "ctjs-opt %t/joined.mlir | ctjs-opt | FileCheck %s --check-prefix=ROUNDTRIP",
        "ctjs-opt %t/joined.mlir --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=TYPES --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.try",
        "not ctjs-opt %t/empty-block.mlir 2>&1 | FileCheck %s --check-prefix=EMPTY",
        "not ctjs-opt %t/state-count.mlir 2>&1 | FileCheck %s --check-prefix=BAD-STATE",
        "not ctjs-opt %t/catch-type.mlir 2>&1 | FileCheck %s --check-prefix=CATCH-TYPE",
        "not ctjs-opt %t/body-argument.mlir 2>&1 | FileCheck %s --check-prefix=ARGUMENTS",
        "not ctjs-opt %t/missing-payload.mlir 2>&1 | FileCheck %s --check-prefix=ARGUMENTS",
        "not ctjs-opt %t/wrong-terminator.mlir 2>&1 | FileCheck %s --check-prefix=TERMINATOR",
        "not ctjs-opt %t/misplaced-exit.mlir 2>&1 | FileCheck %s --check-prefix=EXIT",
        "not ctjs-opt %t/misplaced-yield.mlir 2>&1 | FileCheck %s --check-prefix=YIELD",
    ],
    "IR/invoke": [
        "split-file %s %t",
        "ctjs-opt %t/roundtrip.mlir | ctjs-opt | FileCheck %s --check-prefix=ROUNDTRIP",
        "not ctjs-opt %t/body-work.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-0",
        "not ctjs-opt %t/result-state.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-1",
        "not ctjs-opt %t/wrong-result.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-2",
        "not ctjs-opt %t/normal-argument.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-3",
        "not ctjs-opt %t/body-argument.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-4",
        "not ctjs-opt %t/payload-type.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-5",
        "not ctjs-opt %t/state-count.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-6",
        "not ctjs-opt %t/yield-count.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-7",
        "not ctjs-opt %t/wrong-yield.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-8",
        "not ctjs-opt %t/misplaced-exit.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-9",
        "not ctjs-opt %t/misplaced-yield.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-10",
        "not ctjs-opt %t/ordinary-result.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-2",
    ],
    "Transforms/absent-global-properties": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/plugin.js 2>/dev/null    | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=FOLD",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/plugin.js 2>/dev/null    | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc    | FileCheck %s --check-prefix=DROP",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/dynamic.js 2>/dev/null    | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/written.js 2>/dev/null    | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/declared.js 2>/dev/null    | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/host.js 2>/dev/null    | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=HOST",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/inherited.js 2>/dev/null    | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=INHERITED",
    ],
    "Transforms/absent-globals": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/probe.js 2>/dev/null    | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=PROBE",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/stored.js 2>/dev/null    | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/host.js 2>/dev/null    | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=HOST",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/dynamic.js 2>/dev/null    | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/window.js 2>/dev/null    | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/plain.js 2>/dev/null    | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=PLAIN",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/prefix-umd.js 2>/dev/null    | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=PREFIXUMD",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/prefix-declarations.js 2>/dev/null    | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=PREFIXPROPS",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/prefix-call.js 2>/dev/null    | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/prefix-twice.js 2>/dev/null    | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=KEPT",
    ],
    "Transforms/array-iteration": [
        "ctjs-translate --ctbrowser-js-to-ctjs %s    | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf    | FileCheck %s",
    ],
    "Transforms/resolve-globals": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/closed.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=CLOSED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/rebound.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=REBOUND",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/surplus.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=SURPLUS",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/global-object.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=DYNAMIC",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/skipped.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=SKIPPED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/constructor.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=CTOR",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/constructor-compared.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=COMPARED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/skipped-stores.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=SKIPSTORE",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/skipped-opaque.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=SKIPOPAQUE",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-literal.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=LITERAL",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-literal-chain.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=CHAIN",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-super.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=SUPER",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-extends-function.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=EXTENDSFN",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-prototype-replaced.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=PROTOSET",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-member.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=MEMBER",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-member-prototype.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=MEMBERPROTO",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-static-call.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=STATICCALL",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-static-polluted.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=STATICPOLLUTED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-static-realm.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=STATICREALM",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/ctor-static-written.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=STATICWRITTEN",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/window-lacks.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=WINLACKS",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/window-lacks-bound.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=WINLACKSBOUND",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/window-lacks-polluted.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=WINLACKSPOLLUTED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/argument-inert.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ARGINERT",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/argument-defined.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ARGDEFINED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/argument-tostring.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ARGTOSTRING",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/container-read.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=CONTAINERREAD",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/container-unknown.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=CONTAINERUNKNOWN",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/container-write.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=CONTAINERWRITE",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/container-stored.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=CONTAINERSTORED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/window-read.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=WINREAD",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/window-alias.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=WINALIAS",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/enter-read.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERREAD",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/enter-write.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERWRITE",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/enter-computed.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERCOMPUTED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/enter-unknown.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERUNKNOWN",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/enter-cycle.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERCYCLE",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/enter-inert.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERINERT",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/enter-rebound.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERREBOUND",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/enter-method.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERNAMED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/enter-binding.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERNAMED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/enter-method-write.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERMETHODWRITE",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/enter-stored.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERSTORED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/enter-cell.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERNAMED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/enter-return.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERNAMED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/enter-cell-overwritten.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERCELLLEAK",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/enter-return-stored.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERRETURNSTORED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/enter-return-value.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERRETURNVALUE",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/enter-arguments.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERARGS",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/enter-surplus.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=ENTERSURPLUS",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/self-write.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=SELFWRITE",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/closed.js 2>/dev/null | ctjs-opt '--ctjs-resolve-globals=report=true' 2>&1 >/dev/null | FileCheck %s --check-prefix=REPORT",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/closure-callee.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=IIFE",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/closure-callee.js 2>/dev/null | ctjs-opt '--ctjs-resolve-globals=report=true' 2>&1 >/dev/null | FileCheck %s --check-prefix=IIFEREPORT",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/closure-surplus.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=CLOSURESURPLUS",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/closure-arguments.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=CLOSUREARGS",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/name-arguments.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=NAMEARGS",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/this-top.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=THISTOP",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/this-called.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=THISCALLED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/this-arrow.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=THISARROW",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/this-iife.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=THISIIFE",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/this-returned.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=THISRETURNED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/this-field.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=THISFIELD",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/this-refused.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=THISREFUSED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/this-handed.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=THISHANDED",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/this-inherited.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=THISINHERITED",
    ],
    "invalid": [
        "ctjs-opt %s -split-input-file -verify-diagnostics",
    ],
    "roundtrip": [
        "ctjs-opt %s | ctjs-opt | FileCheck %s",
    ],
}


@pytest.mark.parametrize("case", CASES)
def test_case(lit, case):
    mlir = HERE / f"{case}.mlir"
    lit_lines.run(lit, mlir if mlir.is_file() else HERE / f"{case}.test", CASES[case], MAINS)
