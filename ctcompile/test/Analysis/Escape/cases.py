# CASES below copies each case's RUN lines from its .test file until that
# case's switch commit deletes them there; until then, edit both copies.
"""The Analysis/Escape lit cases whose Python step runs among other RUN lines,
as pytest tests. test_case is parametrized over CASES, whose keys are the
cases' lit paths relative to this folder without .test: `test_case[aot/oracle]`
is lit's `ctcompile :: Analysis/Escape/aot/oracle.test`. Each test runs all of
its case's RUN lines in order through lit_lines.run: claims/check.py in this
process, and split-file, ctjs-translate, FileCheck, the AOT recorders and
tools/check/escape-oracle.py under bash, as lit runs them.
"""

from pathlib import Path

import pytest

from Analysis.Escape.claims import check
import lit_lines

HERE = Path(__file__).resolve().parent
MAINS = {(HERE / "claims/check.py").resolve(): check.main}

CASES = {
    "aot/oracle": [
        "ctcompile-test-escape-oracle-aot --out %t.rec",
        "python3 %{monorepo}/tools/check/escape-oracle.py --recording %t.rec --infer all-escapes --name aot-unchecked --expect-unclaimed 4 --expect-violations 0 --expect-sound 0",
        "ctcompile-test-escape-oracle-aot-return --out %t.return.rec",
        "python3 %{monorepo}/tools/check/escape-oracle.py --recording %t.return.rec --infer all-escapes --name aot-return --expect-unclaimed 5 --expect-violations 0 --expect-sound 0",
    ],
    "claims/add/cancellation": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name add-cancellation",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-add-cancellation.claims",
    ],
    "claims/add/latch": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name add-latch",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-add-latch.claims",
    ],
    "claims/aliased-element-overwrite": [
        "split-file %s %t",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name aliased-element-overwrite",
        "FileCheck %s < %t/escape-claims-aliased-element-overwrite.claims",
    ],
    "claims/bitand-index-overwrite": [
        "split-file %s %t",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name bitand-index-overwrite",
        "FileCheck %s < %t/escape-claims-bitand-index-overwrite.claims",
    ],
    "claims/bitnot/index-overwrite": [
        "split-file %s %t",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name bitnot-index-overwrite",
        "FileCheck %s < %t/escape-claims-bitnot-index-overwrite.claims",
    ],
    "claims/bitnot/latch": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name bitnot-latch",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-bitnot-latch.claims",
    ],
    "claims/bitor-xor-index-overwrite": [
        "split-file %s %t",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name bitor-xor-index-overwrite",
        "FileCheck %s < %t/escape-claims-bitor-xor-index-overwrite.claims",
    ],
    "claims/bootstrap": [
        "cat %{monorepo}/ctbrowser/vendor/bootstrap/bootstrap.bundle.js > %t.js && echo >> %t.js && cat %S/../../Types/bootstrap-driver.js >> %t.js",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t.js --work %t --name bootstrap",
    ],
    "claims/bounded-power": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name bounded-power",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-bounded-power.claims",
    ],
    "claims/cached/guarded-length": [
        "split-file %s %t",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name cached-guarded-length",
        "FileCheck %s < %t/escape-claims-cached-guarded-length.claims",
    ],
    "claims/cached/joined-length": [
        "split-file %s %t",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name cached-joined-length",
        "FileCheck %s < %t/escape-claims-cached-joined-length.claims",
    ],
    "claims/cached/length-guard": [
        "split-file %s %t",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name cached-length-guard",
        "FileCheck %s < %t/escape-claims-cached-length-guard.claims",
    ],
    "claims/cached/loaded-length-guard": [
        "split-file %s %t",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name cached-loaded-length-guard",
        "FileCheck %s < %t/escape-claims-cached-loaded-length-guard.claims",
    ],
    "claims/cached/shortcut-length": [
        "split-file %s %t",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name cached-shortcut-length",
        "FileCheck %s < %t/escape-claims-cached-shortcut-length.claims",
    ],
    "claims/cached/transported-length": [
        "split-file %s %t",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name cached-transported-length",
        "FileCheck %s < %t/escape-claims-cached-transported-length.claims",
    ],
    "claims/commuted-add-latch": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name commuted-add-latch",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-commuted-add-latch.claims",
    ],
    "claims/composed-index-overwrite": [
        "split-file %s %t",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name composed-index-overwrite",
        "FileCheck %s < %t/escape-claims-composed-index-overwrite.claims",
    ],
    "claims/disjoint/element-reload": [
        "split-file %s %t",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name disjoint-element-reload",
        "FileCheck %s < %t/escape-claims-disjoint-element-reload.claims",
    ],
    "claims/disjoint/index-overwrite": [
        "split-file %s %t",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name disjoint-index-overwrite",
        "FileCheck %s < %t/escape-claims-disjoint-index-overwrite.claims",
    ],
    "claims/fixture": [
        "cat %S/fixture/contents/*.js %S/fixture/primitives/*.js %S/fixture/escapes/*.js > %t.js",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t.js --work %t --name fixture --strict",
    ],
    "claims/invariant/add-sub-latch": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name invariant-add-sub-latch",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-invariant-add-sub-latch.claims",
    ],
    "claims/invariant/bitwise-latch": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name invariant-bitwise-latch",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-invariant-bitwise-latch.claims",
    ],
    "claims/invariant/division-latch": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name invariant-division-latch",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-invariant-division-latch.claims",
    ],
    "claims/invariant/element-overwrite": [
        "split-file %s %t",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name invariant-element-overwrite",
        "FileCheck %s < %t/escape-claims-invariant-element-overwrite.claims",
    ],
    "claims/invariant/length-reload": [
        "split-file %s %t",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name invariant-length-reload",
        "FileCheck %s < %t/escape-claims-invariant-length-reload.claims",
    ],
    "claims/invariant/power-latch": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name invariant-power-latch",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-invariant-power-latch.claims",
    ],
    "claims/invariant/product-latch": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name invariant-product-latch",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-invariant-product-latch.claims",
    ],
    "claims/invariant/reload": [
        "split-file %s %t",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name invariant-reload",
        "FileCheck %s < %t/escape-claims-invariant-reload.claims",
    ],
    "claims/invariant/string/index": [
        "split-file %s %t",
        "python3 %S/../../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name invariant-string-index",
        "FileCheck %s < %t/escape-claims-invariant-string-index.claims",
    ],
    "claims/invariant/string/length": [
        "split-file %s %t",
        "python3 %S/../../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name invariant-string-length",
        "FileCheck %s < %t/escape-claims-invariant-string-length.claims",
    ],
    "claims/invariant/string/snapshot": [
        "split-file %s %t",
        "python3 %S/../../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name invariant-string-snapshot",
        "FileCheck %s < %t/escape-claims-invariant-string-snapshot.claims",
    ],
    "claims/invariant/unary-latch": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name invariant-unary-latch",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-invariant-unary-latch.claims",
    ],
    "claims/left-shift-index-overwrite": [
        "split-file %s %t",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name left-shift-index-overwrite",
        "FileCheck %s < %t/escape-claims-left-shift-index-overwrite.claims",
    ],
    "claims/negated/guard": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name negated-guard",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-negated-guard.claims",
    ],
    "claims/negated/stride": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name negated-stride",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-negated-stride.claims",
    ],
    "claims/negative/add": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name negative-add",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-negative-add.claims",
    ],
    "claims/negative/divisor-index-overwrite": [
        "split-file %s %t",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name negative-divisor-index-overwrite",
        "FileCheck %s < %t/escape-claims-negative-divisor-index-overwrite.claims",
    ],
    "claims/negative/scaled-index-overwrite": [
        "split-file %s %t",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name negative-scaled-index-overwrite",
        "FileCheck %s < %t/escape-claims-negative-scaled-index-overwrite.claims",
    ],
    "claims/negative/string-latch": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name negative-string-latch",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-negative-string-latch.claims",
    ],
    "claims/negative/string": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name negative-string",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-negative-string.claims",
    ],
    "claims/nested-invariant-latch": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name nested-invariant-latch",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-nested-invariant-latch.claims",
    ],
    "claims/offset-index-overwrite": [
        "split-file %s %t",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name offset-index-overwrite",
        "FileCheck %s < %t/escape-claims-offset-index-overwrite.claims",
    ],
    "claims/own-element-overwrite": [
        "split-file %s %t",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name own-element-overwrite",
        "FileCheck %s < %t/escape-claims-own-element-overwrite.claims",
    ],
    "claims/power/identities": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name power-identities",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-power-identities.claims",
    ],
    "claims/power/index-overwrite": [
        "split-file %s %t",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name power-index-overwrite",
        "FileCheck %s < %t/escape-claims-power-index-overwrite.claims",
    ],
    "claims/primitive/add-latch": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name primitive-add-latch",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-primitive-add-latch.claims",
    ],
    "claims/primitive/addition": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name primitive-addition",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-primitive-addition.claims",
    ],
    "claims/primitive/bitwise": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name primitive-bitwise",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-primitive-bitwise.claims",
    ],
    "claims/primitive/division": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name primitive-division",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-primitive-division.claims",
    ],
    "claims/primitive/power": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name primitive-power",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-primitive-power.claims",
    ],
    "claims/primitive/product": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name primitive-product",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-primitive-product.claims",
    ],
    "claims/primitive/subtraction": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name primitive-subtraction",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-primitive-subtraction.claims",
    ],
    "claims/primitive/unary-latch": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name primitive-unary-latch",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-primitive-unary-latch.claims",
    ],
    "claims/primitive/unary": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name primitive-unary",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-primitive-unary.claims",
    ],
    "claims/quotient-index-overwrite": [
        "split-file %s %t",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name quotient-index-overwrite",
        "FileCheck %s < %t/escape-claims-quotient-index-overwrite.claims",
    ],
    "claims/remainder-index-overwrite": [
        "split-file %s %t",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name remainder-index-overwrite",
        "FileCheck %s < %t/escape-claims-remainder-index-overwrite.claims",
    ],
    "claims/reversed-index-overwrite": [
        "split-file %s %t",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name reversed-index-overwrite",
        "FileCheck %s < %t/escape-claims-reversed-index-overwrite.claims",
    ],
    "claims/right-shift-index-overwrite": [
        "split-file %s %t",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name right-shift-index-overwrite",
        "FileCheck %s < %t/escape-claims-right-shift-index-overwrite.claims",
    ],
    "claims/scaled-index-overwrite": [
        "split-file %s %t",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name scaled-index-overwrite",
        "FileCheck %s < %t/escape-claims-scaled-index-overwrite.claims",
    ],
    "claims/signed/bitnot": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name signed-bitnot",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-signed-bitnot.claims",
    ],
    "claims/signed/bitwise": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name signed-bitwise",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-signed-bitwise.claims",
    ],
    "claims/signed/division": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name signed-division",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-signed-division.claims",
    ],
    "claims/signed/product": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name signed-product",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-signed-product.claims",
    ],
    "claims/signed/right-shifts": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name signed-right-shifts",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-signed-right-shifts.claims",
    ],
    "claims/signed/subtraction": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name signed-subtraction",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-signed-subtraction.claims",
    ],
    "claims/signed/unary": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name signed-unary",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-signed-unary.claims",
    ],
    "claims/strict-guard": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name strict-guard",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-strict-guard.claims",
    ],
    "claims/sub/negative-latch": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name sub-negative-latch",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-sub-negative-latch.claims",
    ],
    "claims/sub/snapshot": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/../check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name sub-snapshot",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-sub-snapshot.claims",
    ],
    "claims/table-index-overwrite": [
        "split-file %s %t",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name table-index-overwrite",
        "FileCheck %s < %t/escape-claims-table-index-overwrite.claims",
        "FileCheck %s --check-prefix=RECORD < %t/escape-claims-table-index-overwrite.rec",
    ],
    "claims/unary-index-overwrite": [
        "split-file %s %t",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name unary-index-overwrite",
        "FileCheck %s < %t/escape-claims-unary-index-overwrite.claims",
    ],
    "claims/unit-power": [
        "split-file %s %t",
        "ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | FileCheck %s --check-prefix=IMPORT",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name unit-power",
        "FileCheck %s --check-prefix=CLAIM < %t/escape-claims-unit-power.claims",
    ],
    "claims/visited-index-overwrite": [
        "split-file %s %t",
        "python3 %S/check.py --oracle ctcompile-test-type-oracle --claims ctcompile-test-escape-claims --script %{monorepo}/tools/check/escape-oracle.py --corpus %t/source.js --work %t --name visited-index-overwrite",
        "FileCheck %s < %t/escape-claims-visited-index-overwrite.claims",
    ],
}


@pytest.mark.parametrize("case", CASES)
def test_case(lit, case):
    lit_lines.run(lit, HERE / f"{case}.test", CASES[case], MAINS)
