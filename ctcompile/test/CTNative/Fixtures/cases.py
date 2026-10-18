# CASES below copies each case's RUN lines from its .test or .mlir file until
# that case's switch commit deletes them there; until then, edit both copies.
"""The CTNative/Fixtures lit cases whose Python step runs among other RUN lines
and that gates.py does not cover, as pytest tests. test_case is parametrized
over CASES, whose keys are the cases' lit paths relative to this folder without
.test or .mlir: `test_case[Objects/string-arrays]` is lit's `ctcompile ::
CTNative/Fixtures/Objects/string-arrays.test`. Each test runs all of its case's
RUN lines in order through lit_lines.run, every line under bash, as lit runs
them.
"""

from pathlib import Path

import pytest

import lit_lines

HERE = Path(__file__).resolve().parent
MAINS = {}

CASES = {
    "ControlFlow/functions.emitc": [
        "ctjs-translate --mlir-to-cpp %s | FileCheck %s --implicit-check-not=ctbrowser",
        "%compilation_unit --module %s --js %S/functions.js --work %t --name fixture",
        "not %compilation_unit --module %s --js %S/functions.js --work %t --name fixture_off_by_one --mutate fib20 2>&1 | FileCheck %s --check-prefix=OFF-BY-ONE",
        "not %compilation_unit --module %s --js %S/functions.js --work %t --name fixture_vm_linked --prebuilt ctcompile-test-native-vm-linked 2>&1 | FileCheck %s --check-prefix=VM-LINKED",
        "%compile_clean --module %s --work %t --name fixture",
        "%compile_clean --module %s --work %t --name fixture_unused_variable --mutate | FileCheck %s --check-prefix=MUTANT",
    ],
    "Objects/string-arrays": [
        "python3 %S/string-arrays.py --translate ctjs-translate --opt ctjs-opt --js %S/string-arrays.js --out %t/string-arrays.emitc.mlir",
        "ctjs-translate --mlir-to-cpp %t/string-arrays.emitc.mlir | FileCheck %s",
        "%compilation_unit --module %t/string-arrays.emitc.mlir --js %S/string-arrays.js --work %t --name string_arrays --core-build %build",
        "not %compilation_unit --module %t/string-arrays.emitc.mlir --js %S/string-arrays.js --work %t --name string_arrays_off_by_one --core-build %build --mutate size 2>&1 | FileCheck %s --check-prefix=OFF-BY-ONE",
        "%compile_clean --module %t/string-arrays.emitc.mlir --work %t --name string_arrays",
    ],
    "Objects/string-arrays-calls": [
        "python3 %S/string-arrays-calls.py --translate ctjs-translate --opt ctjs-opt --js %S/string-arrays-calls.js --out %t/string-arrays-calls.emitc.mlir",
        "ctjs-translate --mlir-to-cpp %t/string-arrays-calls.emitc.mlir | FileCheck %s",
        "%compilation_unit --module %t/string-arrays-calls.emitc.mlir --js %S/string-arrays-calls.js --work %t --name string_arrays_calls --core-build %build",
        "not %compilation_unit --module %t/string-arrays-calls.emitc.mlir --js %S/string-arrays-calls.js --work %t --name string_arrays_calls_negative --core-build %build --mutate result --mutate-as string 2>&1 | FileCheck %s --check-prefix=NEGATIVE",
        "%compile_clean --module %t/string-arrays-calls.emitc.mlir --work %t --name string_arrays_calls",
    ],
    "Objects/tuple-arrays": [
        "python3 %S/tuple-arrays.py --translate ctjs-translate --opt ctjs-opt --js %S/tuple-arrays.js --out %t/tuple-arrays.emitc.mlir",
        "ctjs-translate --mlir-to-cpp %t/tuple-arrays.emitc.mlir | FileCheck %s",
        "%compilation_unit --module %t/tuple-arrays.emitc.mlir --js %S/tuple-arrays.js --work %t --name tuple_arrays --core-build %build",
        "not %compilation_unit --module %t/tuple-arrays.emitc.mlir --js %S/tuple-arrays.js --work %t --name tuple_arrays_off_by_one --core-build %build --mutate table --mutate-as string 2>&1 | FileCheck %s --check-prefix=OFF-BY-ONE",
        "%compile_clean --module %t/tuple-arrays.emitc.mlir --work %t --name tuple_arrays",
    ],
    "Objects/tuple-arrays-equality": [
        "python3 %S/tuple-arrays-equality.py --translate ctjs-translate --opt ctjs-opt --js %S/tuple-arrays-equality.js --out %t/tuple-arrays-equality.emitc.mlir",
        "%compilation_unit --module %t/tuple-arrays-equality.emitc.mlir --js %S/tuple-arrays-equality.js --work %t --name tuple_arrays_equality --core-build %build",
        "not %compilation_unit --module %t/tuple-arrays-equality.emitc.mlir --js %S/tuple-arrays-equality.js --work %t --name tuple_arrays_equality_negative --core-build %build --mutate line --mutate-as string 2>&1 | FileCheck %s --check-prefix=NEGATIVE",
        "%compile_clean --module %t/tuple-arrays-equality.emitc.mlir --work %t --name tuple_arrays_equality",
    ],
    "Optimization/default-optimizations": [
        "rm -rf %t && mkdir -p %t",
        "%cmake -DTRANSLATE=ctjs-translate -DOPT=ctjs-opt -DSOURCE=%S/default-optimizations.js -DOPTIMIZATION_REPORT=ON -DOUTPUT=%t/enabled.mlir -P %S/../../Checks/pipeline.cmake",
        "%cmake -DTRANSLATE=ctjs-translate -DOPT=ctjs-opt -DSOURCE=%S/default-optimizations.js -DOPTIMIZATION_REPORT=ON -DOUTPUT=%t/disabled.mlir -DOPTIMIZE=OFF -P %S/../../Checks/pipeline.cmake",
        "%cmake -DTRANSLATE=ctjs-translate -DOPT=ctjs-opt -DSOURCE=%S/default-optimizations.js -DOPTIMIZATION_REPORT=ON -DOUTPUT=%t/no-precompute.mlir -DPRECOMPUTE=OFF -P %S/../../Checks/pipeline.cmake",
        "%cmake -DTRANSLATE=ctjs-translate -DOPT=ctjs-opt -DSOURCE=%S/default-optimizations.js -DOPTIMIZATION_REPORT=ON -DOUTPUT=%t/no-prune.mlir -DPRUNE_UNREACHABLE=OFF -P %S/../../Checks/pipeline.cmake",
        "%cmake -DTRANSLATE=ctjs-translate -DOPT=ctjs-opt -DSOURCE=%S/default-optimizations.js -DOPTIMIZATION_REPORT=ON -DOUTPUT=%t/explicit-precompute.mlir -DOPTIMIZE=OFF -DPRECOMPUTE=ON -P %S/../../Checks/pipeline.cmake",
        "grep -Pzo 'ctnative.precompute_summary = \\{[^}]*expressions = [1-9][0-9]* : i64' %t/enabled.mlir",
        "grep -q ctnative.reachability_summary %t/enabled.mlir",
        "not grep -E 'ctnative.(precompute|reachability)_summary' %t/disabled.mlir",
        "not grep ctnative.precompute_summary %t/no-precompute.mlir",
        "grep -q ctnative.reachability_summary %t/no-precompute.mlir",
        "grep -q ctnative.precompute_summary %t/no-prune.mlir",
        "not grep ctnative.reachability_summary %t/no-prune.mlir",
        "grep -q ctnative.precompute_summary %t/explicit-precompute.mlir",
        "not grep ctnative.reachability_summary %t/explicit-precompute.mlir",
        "not grep -E 'ctnative.(specialized_from|partial_evaluated|supercompile_summary|deforest_summary)' %t/enabled.mlir %t/disabled.mlir %t/no-precompute.mlir %t/no-prune.mlir %t/explicit-precompute.mlir",
        "%compilation_unit --module %t/enabled.mlir --js %S/default-optimizations.js --work %t --name defaults-enabled",
        "%compilation_unit --module %t/disabled.mlir --js %S/default-optimizations.js --work %t --name defaults-disabled",
        "%t/native-unit-defaults-enabled/unit > %t/enabled.out",
        "%t/native-unit-defaults-disabled/unit > %t/disabled.out",
        "printf 'answer=42\\nliteralNaNEqual=0\\nliteralStringEqual=1\\nnegativeInfinity=-inf\\nobserved=122\\nruntimeNaNEqual=0\\nruntimeNumberEqual=1\\ntrace=122\\n' | diff - %t/enabled.out",
        "diff %t/enabled.out %t/disabled.out",
        "not cmp -s %t/native-unit-defaults-enabled/unit.cpp %t/native-unit-defaults-disabled/unit.cpp",
    ],
    "Scalars/values.emitc": [
        "ctjs-translate --mlir-to-cpp %s | FileCheck %s --implicit-check-not=ctbrowser",
        "%compilation_unit --module %s --js %S/values.js --work %t --name values",
        "not %compilation_unit --module %s --js %S/values.js --work %t --name values_number --mutate third --mutate-as number 2>&1 | FileCheck %s --check-prefix=NUMBER",
        "not %compilation_unit --module %s --js %S/values.js --work %t --name values_boolean --mutate no --mutate-as boolean 2>&1 | FileCheck %s --check-prefix=BOOLEAN",
        "not %compilation_unit --module %s --js %S/values.js --work %t --name values_string --mutate greeting --mutate-as string 2>&1 | FileCheck %s --check-prefix=STRING",
        "not %compilation_unit --module %s --js %S/values.js --work %t --name values_undefined --mutate missing --mutate-as undefined 2>&1 | FileCheck %s --check-prefix=UNDEFINED",
        "not %compilation_unit --module %s --js %S/values.js --work %t --name values_null --mutate nothing --mutate-as null 2>&1 | FileCheck %s --check-prefix=NULL",
        "not %compilation_unit --module %s --js %S/values.js --work %t --name values_vacuous_mutation --mutate answer --mutate-as number 2>&1 | FileCheck %s --check-prefix=VACUOUS",
        "not %compilation_unit --module %s --js %S/values.js --work %t --name values_vm_linked --prebuilt ctcompile-test-native-values-vm-linked 2>&1 | FileCheck %s --check-prefix=VM-LINKED",
        "%compile_clean --module %s --work %t --name values",
    ],
}


@pytest.mark.parametrize("case", CASES)
def test_case(lit, case):
    mlir = HERE / f"{case}.mlir"
    lit_lines.run(lit, mlir if mlir.is_file() else HERE / f"{case}.test", CASES[case], MAINS)
