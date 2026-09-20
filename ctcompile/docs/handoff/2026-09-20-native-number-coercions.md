# Typed Number conversions and NaN, 2026-09-20 UTC

Continued clean **87c0dc2c**, following the Number handoff's next literal/conversion
step. No interrupted dirty work needed recovery. Parallel agents implemented
the token/runtime checks and audited dependent optimizations and callers.

**5c2f4332** adds the explicit `js_nan_t` constructor token to `js_basic_num`.
`js_boolean_t::to_number()` and the Boolean/nullable `ctnative::to_number()`
overloads return `ctnative::js_num`. Undefined becomes NaN, null and false become
positive zero, and numeric payloads retain NaN and signed zero. The existing
tagged storage distinguishes present NaN from absence. No new JavaScript type,
implicit numeric promotion, allocation or VM dependency was added.

Scalar conversion lowering now emits a Number result followed by `.value()` for
the current f64 arithmetic/storage consumers. Deforestation and dead-code pruning
recognize only `ctnative::js_num.value()` with no arguments/templates and an f64
result as pure. Unknown receivers/methods remain effect barriers. The ownership
dataflow checker follows extraction only from known Number-producing calls.

**30e9e3c7** uses `ctnative::js_num{ctnative::js_nan_t{}}.value()` for binary64 NaN
literals under the native numeric-alias marker. Ordinary EmitC, a wrong-typed
marker and other floating formats retain their previous spelling. Float attributes
remain numeric IR constants; optimization sees the same values. The new printer
test compiles GCC/Clang output and checks deduction, arrays, NaN and negative zero.

Raw arithmetic, Map/vector/JSON storage and call/capture signatures remain on the
compatibility representation. No source proof, fixture JS, browser code or
interpreter/VM semantics changed. The returned-closure documentation example now
extracts `.value()` for its raw numeric arithmetic.

## Focused validation

All devbox commands ran under `/tmp/ctbrowser-devbox-build.lock`. The affected
build passed **16 steps**; the final corrected-test sync needed no Ninja work:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime ctcompile-test-native-reference ctcompile-native-pipeline-optional_scalars ctcompile-native-pipeline-scalar_unions
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime
```

From `projects/compile-time-browser`, runtime CTest passed **1/1**, 0.02s total.
The first selected lit run had **9 passes / 1 failure**, 251.47s. The new NaN
fixture omitted `CTCOMPILE_PIN`, so its deduced C++ failed to compile. Adding
the existing macro fixed the fixture; its exact rerun passed **1/1**, 0.96s.
Thus **10 distinct selected lit cases** passed on the final changes:

```sh
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_native_runtime$'
~/.lit-venv/bin/lit -v -j 2 build/ctcompile/test --filter='^ctcompile :: ((CTNative/((Lowering/Scalars/(optional-scalars|scalar-unions)[.]mlir)|(Fixtures/Scalars/(optional-scalars|scalar-unions)[.]test)|(Optimization/deforestation[.]mlir)|(Lowering/Emission/(prune-dead-stores|unused-call)[.]mlir)|(Browser/native-dom[.]test)))|(Target/Cpp/(native-nan|readable-floats)[.]mlir))$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: Target/Cpp/native-nan[.]mlir$'
```

Four ownership cases passed through existing `driver_execution.check_positive`:
`constant_nan`, `constant_boolean_branch_lifetime`, `scalar_alias_branch_lifetime`
and `local_add_saved_results`. The temporary driver called `setup()`, selected
only those rows from `positive_cases()`, and ran their existing complete checks:

```sh
PYTHONPATH=ctcompile/test python3 /tmp/ctcompile-coercion-ownership.py --translate build/ctcompile/tools/ctjs-translate/ctjs-translate --opt build/ctcompile/tools/ctjs-opt/ctjs-opt --node /home/ubuntu/tools/node-v26.8.1/bin/node --reference build/ctcompile/test/ctcompile-test-native-reference --work build/ctcompile/test/number-coercion-ownership
```

The selected tests include scalar Node/interpreter comparisons, both C++ compilers
and printing modes, existing mutation/lifetime/sanitizer checks and Script-symbol
audits. Native DOM executes against public subsystems; it is not a full browser/VM
differential measurement. Exact purity regressions retain unknown-member barriers.
All **13** final code/test SHA-256 hashes matched the devbox.

Scoped clang-format, Black/Python syntax and whitespace checks passed. Required
`tools/format.sh --check` retains the same **16** pre-existing diagnostics in
untouched `ctdrive.cpp` (2), `HostContract/ProviderPaths.h` (2),
`PartialEvaluation/Heap.h` (4) and `Symbolic/Facts.cpp` (8). Full CTest/lit,
broad corpus/matrix and WPT/test262 were skipped. No push.

Process checks found no Claude identity among 65 Linux / 367 Windows processes;
56 Linux executable links were unreadable. Availability remained uncertain, so
edits stayed in ctcompile and the requested external plans.

## Next boundary

Migrate the remaining Number value carrier in `LoweringSupport.cpp::carrierType`
with `EmitC/Expressions.cpp` literals, `Operations.cpp` arithmetic/math and
call/capture signatures. Keep explicit adapters for Map/vector/JSON storage;
do not accidentally change SameValueZero through class name lookup. Retire the
global `using js_num = double` only after its clients migrate.

Introduce `js_basic_string<char>` / `js_string` with an admitted String operation
group, reusing public Core Unicode algorithms. Collections/document views,
Object/Array prototypes and the proved `instanceof`/`Symbol.hasInstance` wrapper
remain. Resolve NodeList >1,000,000 undefined slots before indexed Bootstrap
`R.find` consumers; retain the separate 2^24 spread cap. Full Bootstrap and the
application driver are unfinished.
