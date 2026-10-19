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
    "Closures/constant-code": [
        "%compilation_unit --module %{obj}/constant_code.pipeline.emitc.mlir --js %S/constant-code.js --work %t --name constant_code --core-build %build",
        "%compile_clean --module %{obj}/constant_code.pipeline.emitc.mlir --work %t --name constant_code",
        "python3 %{monorepo}/tools/check/native-claims.py --translate ctjs-translate --opt ctjs-opt --corpus %S/constant-code.js --name constant_code --min-claimed 8 --no-default-optimizations",
        "%compilation_unit --module %{obj}/constant_code.pipeline.deduced.emitc.mlir --js %S/constant-code.js --work %t --name constant_code_deduced --core-build %build",
        "not %compilation_unit --module %{obj}/constant_code.pipeline.emitc.mlir --js %S/constant-code.js --work %t --name constant_code_mutated --mutate nested --mutate-as number --core-build %build 2>&1 | FileCheck %s --check-prefix=MUTATED",
        "%native_reference %S/constant-code.js | FileCheck %s --check-prefix=VALUES",
        '%node -e \'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); for (const key of Object.keys(scope).sort()) if (typeof scope[key] === "number") console.log(key + "=" + scope[key]);\' %S/constant-code.js | FileCheck %s --check-prefix=VALUES',
    ],
    "ControlFlow/functions.emitc": [
        "ctjs-translate --mlir-to-cpp %s | FileCheck %s --implicit-check-not=ctbrowser",
        "%compilation_unit --module %s --js %S/functions.js --work %t --name fixture",
        "not %compilation_unit --module %s --js %S/functions.js --work %t --name fixture_off_by_one --mutate fib20 2>&1 | FileCheck %s --check-prefix=OFF-BY-ONE",
        "not %compilation_unit --module %s --js %S/functions.js --work %t --name fixture_vm_linked --prebuilt ctcompile-test-native-vm-linked 2>&1 | FileCheck %s --check-prefix=VM-LINKED",
        "%compile_clean --module %s --work %t --name fixture",
        "%compile_clean --module %s --work %t --name fixture_unused_variable --mutate | FileCheck %s --check-prefix=MUTANT",
    ],
    "Objects/array-callbacks": [
        "python3 %S/string-arrays-calls.py --translate ctjs-translate --opt ctjs-opt --js %S/array-callbacks.js --out %t/array-callbacks.emitc.mlir",
        "ctjs-translate --mlir-to-cpp %t/array-callbacks.emitc.mlir | FileCheck %s",
        "%compilation_unit --module %t/array-callbacks.emitc.mlir --js %S/array-callbacks.js --work %t --name array_callbacks --core-build %build",
        "not %compilation_unit --module %t/array-callbacks.emitc.mlir --js %S/array-callbacks.js --work %t --name array_callbacks_negative --core-build %build --mutate kept --mutate-as string 2>&1 | FileCheck %s --check-prefix=NEGATIVE",
        "%compile_clean --module %t/array-callbacks.emitc.mlir --work %t --name array_callbacks",
    ],
    "Objects/array-sort": [
        "python3 %S/string-arrays-calls.py --translate ctjs-translate --opt ctjs-opt --js %S/array-sort.js --out %t/array-sort.emitc.mlir",
        "ctjs-translate --mlir-to-cpp %t/array-sort.emitc.mlir | FileCheck %s",
        "%compilation_unit --module %t/array-sort.emitc.mlir --js %S/array-sort.js --work %t --name array_sort --core-build %build",
        "not %compilation_unit --module %t/array-sort.emitc.mlir --js %S/array-sort.js --work %t --name array_sort_negative --core-build %build --mutate ascending --mutate-as string 2>&1 | FileCheck %s --check-prefix=NEGATIVE",
        "%compile_clean --module %t/array-sort.emitc.mlir --work %t --name array_sort",
    ],
    "Objects/dictionaries": [
        "python3 %S/string-arrays-calls.py --translate ctjs-translate --opt ctjs-opt --js %S/dictionaries.js --out %t/dictionaries.emitc.mlir",
        "ctjs-translate --mlir-to-cpp %t/dictionaries.emitc.mlir | FileCheck %s",
        "not %compilation_unit --module %t/dictionaries.emitc.mlir --js %S/dictionaries.js --work %t --name dictionaries_negative --core-build %build --mutate presence 2>&1 | FileCheck %s --check-prefix=NEGATIVE",
        "%compilation_unit --cxx g++ --module %t/dictionaries.emitc.mlir --js %S/dictionaries.js --work %t --name dictionaries_gcc --core-build %build",
        "%compilation_unit --cxx %dom_clang --module %t/dictionaries.emitc.mlir --js %S/dictionaries.js --work %t --name dictionaries_clang --core-build %build",
    ],
    "Objects/for-of": [
        "python3 %S/string-arrays-calls.py --translate ctjs-translate --opt ctjs-opt --js %S/for-of.js --out %t/for-of.emitc.mlir",
        "ctjs-translate --mlir-to-cpp %t/for-of.emitc.mlir | FileCheck %s",
        "%compilation_unit --module %t/for-of.emitc.mlir --js %S/for-of.js --work %t --name for_of --core-build %build",
        "not %compilation_unit --module %t/for-of.emitc.mlir --js %S/for-of.js --work %t --name for_of_negative --core-build %build --mutate e --mutate-as string 2>&1 | FileCheck %s --check-prefix=NEGATIVE",
        "%compile_clean --module %t/for-of.emitc.mlir --work %t --name for_of",
    ],
    "Objects/instance-registry": [
        "%compilation_unit --module %{obj}/instance_registry.pipeline.emitc.mlir --js %S/instance-registry.js --work %t --name instance_registry",
        "not %compilation_unit --module %{obj}/instance_registry.pipeline.emitc.mlir --js %S/instance-registry.js --work %t --name instance_registry_off_by_one --mutate total 2>&1 | FileCheck %s --check-prefix=OFF-BY-ONE",
        "%compilation_unit --module %{obj}/instance_registry.pipeline.deduced.emitc.mlir --js %S/instance-registry.js --work %t --name instance_registry_deduced",
        "%compile_clean --module %{obj}/instance_registry.pipeline.emitc.mlir --work %t --name instance_registry",
        "%compile_clean --module %{obj}/instance_registry.pipeline.deduced.emitc.mlir --work %t --name instance_registry_deduced",
        "%print_deduced --plain %{obj}/instance_registry.pipeline.emitc.mlir --deduced %{obj}/instance_registry.pipeline.deduced.emitc.mlir --mutated %{obj}/instance_registry.pipeline.mutated.emitc.mlir --work %t --name instance_registry",
    ],
    "Objects/loop-fresh-arrays": [
        "python3 %S/string-arrays-calls.py --translate ctjs-translate --opt ctjs-opt --js %S/loop-fresh-arrays.js --out %t/loop-fresh-arrays.emitc.mlir",
        "ctjs-translate --mlir-to-cpp %t/loop-fresh-arrays.emitc.mlir | FileCheck %s",
        "%compilation_unit --module %t/loop-fresh-arrays.emitc.mlir --js %S/loop-fresh-arrays.js --work %t --name loop_fresh_arrays --core-build %build",
        "not %compilation_unit --module %t/loop-fresh-arrays.emitc.mlir --js %S/loop-fresh-arrays.js --work %t --name loop_fresh_arrays_negative --core-build %build --mutate a --mutate-as string 2>&1 | FileCheck %s --check-prefix=NEGATIVE",
    ],
    "Objects/number-arrays": [
        "python3 %S/string-arrays-calls.py --translate ctjs-translate --opt ctjs-opt --js %S/number-arrays.js --out %t/number-arrays.emitc.mlir",
        "ctjs-translate --mlir-to-cpp %t/number-arrays.emitc.mlir | FileCheck %s",
        "%compilation_unit --module %t/number-arrays.emitc.mlir --js %S/number-arrays.js --work %t --name number_arrays --core-build %build",
        "not %compilation_unit --module %t/number-arrays.emitc.mlir --js %S/number-arrays.js --work %t --name number_arrays_negative --core-build %build --mutate offsets 2>&1 | FileCheck %s --check-prefix=NEGATIVE",
        "%compile_clean --module %t/number-arrays.emitc.mlir --work %t --name number_arrays",
    ],
    "Objects/refinements": [
        "python3 %S/string-arrays-calls.py --translate ctjs-translate --opt ctjs-opt --js %S/refinements.js --out %t/refinements.emitc.mlir",
        "ctjs-translate --mlir-to-cpp %t/refinements.emitc.mlir | FileCheck %s",
        "%compilation_unit --module %t/refinements.emitc.mlir --js %S/refinements.js --work %t --name refinements --core-build %build",
        "not %compilation_unit --module %t/refinements.emitc.mlir --js %S/refinements.js --work %t --name refinements_negative --core-build %build --mutate o1 2>&1 | FileCheck %s --check-prefix=NEGATIVE",
        "%compile_clean --module %t/refinements.emitc.mlir --work %t --name refinements",
    ],
    "Objects/spread": [
        "%compilation_unit --module %{obj}/spreads.pipeline.emitc.mlir --js %S/spread.js --work %t --name spreads",
        "not %compilation_unit --module %{obj}/spreads.pipeline.emitc.mlir --js %S/spread.js --work %t --name spreads_off_by_one --mutate ordered_answer 2>&1 | FileCheck %s --check-prefix=OFF-BY-ONE",
        "%compilation_unit --module %{obj}/spreads.pipeline.deduced.emitc.mlir --js %S/spread.js --work %t --name spreads_deduced",
        "%compile_clean --module %{obj}/spreads.pipeline.emitc.mlir --work %t --name spreads",
        "%print_deduced --plain %{obj}/spreads.pipeline.emitc.mlir --deduced %{obj}/spreads.pipeline.deduced.emitc.mlir --mutated %{obj}/spreads.pipeline.mutated.emitc.mlir --work %t --name spreads",
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
    "Objects/string-replace": [
        "%compilation_unit --module %{obj}/string_replace.pipeline.emitc.mlir --js %S/string-replace.js --work %t --name string_replace --core-build %build",
        "not %compilation_unit --module %{obj}/string_replace.pipeline.emitc.mlir --js %S/string-replace.js --work %t --name string_replace_off_by_one --core-build %build --mutate dashedLength11 2>&1 | FileCheck %s --check-prefix=OFF-BY-ONE",
        "%compilation_unit --module %{obj}/string_replace.pipeline.deduced.emitc.mlir --js %S/string-replace.js --work %t --name string_replace_deduced --core-build %build",
        "%compile_clean --module %{obj}/string_replace.pipeline.emitc.mlir --work %t --name string_replace",
        "%compile_clean --module %{obj}/string_replace.pipeline.deduced.emitc.mlir --work %t --name string_replace_deduced",
        "ctjs-translate --mlir-to-cpp %{obj}/string_replace.pipeline.emitc.mlir | FileCheck %s",
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
    "Objects/variant-fields": [
        "%compilation_unit --module %{obj}/variant_fields.pipeline.emitc.mlir --js %S/variant-fields.js --work %t --name variant_fields",
        "not %compilation_unit --module %{obj}/variant_fields.pipeline.emitc.mlir --js %S/variant-fields.js --work %t --name variant_fields_off_by_one --mutate overridden 2>&1 | FileCheck %s --check-prefix=OFF-BY-ONE",
        "%compilation_unit --module %{obj}/variant_fields.pipeline.deduced.emitc.mlir --js %S/variant-fields.js --work %t --name variant_fields_deduced",
        "%compile_clean --module %{obj}/variant_fields.pipeline.emitc.mlir --work %t --name variant_fields",
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
    "Scalars/console-log": [
        "rm -rf %t && mkdir -p %t",
        "%native_reference --page %S/console-log.html --script %S/console-log.js > %t/vm.txt",
        "FileCheck %s --match-full-lines --strict-whitespace < %t/vm.txt",
        "%cmake -DTRANSLATE=ctjs-translate -DOPT=ctjs-opt -DSOURCE=%S/console-log.js -DOUTPUT=%t/console.mlir -P %S/../../Checks/pipeline.cmake",
        "ctjs-translate --mlir-to-cpp %t/console.mlir -o %t/console.cpp",
        "grep -c 'ctnative::console.log(' %t/console.cpp | FileCheck %s --check-prefix=CALLS",
        "not %cmake -DTRANSLATE=ctjs-translate -DOPT=ctjs-opt -DSOURCE=%S/console-log-refused.js -DOUTPUT=%t/refused.mlir -P %S/../../Checks/pipeline.cmake 2>&1 | FileCheck %s --check-prefix=REFUSED",
        "python3 %S/console_log.py --cpp %t/console.cpp --expected %t/vm.txt --build %build --include %{monorepo}/ctbrowser/include --work %t",
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
