# Number value carrier, 2026-09-20 UTC

Resumed clean **5d084ecf** and the prior Number-coercion handoff. No interrupted
dirty work or `codex-wip-20260907` branch remained. Parallel agents handled
runtime/client and signature/optimizer changes; root integrated and gated them.

- **dbc82d9a**: const/deduction handling for `ctnative::js_num`, exact typed
  Number/Boolean exception payloads, and the native Number target fixture.
- **25472dd7**: `carrier::number` becomes opaque `ctnative::js_num`; arithmetic,
  comparisons, fields, calls, captures, scalar conversions and browser counts
  use it. Binary64 literals construct the class explicitly. Standard math calls
  extract operands and wrap results; the existing exponentiation guard remains.
- **ae8124d6**: 15 additional Number signature/shape tests and their runtime
  proof paths, preserving all JavaScript/CTJS fixture bodies.

Map/vector/JSON storage stays raw binary64. Map arguments and proved numeric
reads cross explicit adapters, preserving SameValueZero and signed zero.
`vec_length`, `map_size`, `vec_push` and optional `dom_number` use Number at their
JavaScript boundary. Public Core calls, C formatting and proved native vector
indices receive raw values. The raw global `js_num = double` alias remains for
compatibility; JavaScript signatures explicitly name `ctnative::js_num`.

No browser code, VM semantics, source proof or accepted JavaScript type set was
changed. Number const extraction remains a narrowly recognized pure/read-only
operation; unrelated opaque calls retain their barriers.

## Measured focused validation

Every devbox operation used `/tmp/ctbrowser-devbox-build.lock`. The initial
affected build passed 60 steps, followed by 40 steps for the remaining ODS/pass
updates. The DOM index fix built four steps; storage pipelines built 18 steps.
Final test-only synchronization needed no Ninja work. Explicit targets used:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime ctcompile-test-native-reference ctcompile-native-pipeline-numeric ctcompile-native-pipeline-optional_scalars ctcompile-native-pipeline-scalar_unions
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime ctcompile-native-pipeline-numeric ctcompile-native-pipeline-optional_scalars ctcompile-native-pipeline-scalar_unions
tools/remote-build.sh ctjs-opt ctjs-translate
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-native-pipeline-array_shrink ctcompile-native-pipeline-array_shrink_unoptimized ctcompile-native-pipeline-array_overwrite_loop ctcompile-native-pipeline-array_overwrite_loop_unoptimized ctcompile-native-pipeline-deforestation
```

Exact runtime CTest passed **1/1**, 0.02s total, from the devbox project root:

```sh
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_native_runtime$'
```

Lit used `~/.lit-venv/bin/lit -v -j 3 build/ctcompile/test --filter=REGEX`,
except the query-all/spread/maps run used `-j 2`. Each regex was anchored to
`^ctcompile :: (...)$` and listed only the exact paths below. **36 distinct
selected cases passed** across these attempts:

| Attempt | Exact selection (paths relative to `ctcompile/test`) | Result |
|---|---|---|
| Initial | `CTNative/Lowering/Scalars/{numeric,optional-scalars,scalar-unions}.mlir`; `CTNative/Fixtures/Scalars/{optional-scalars,scalar-unions}.test`; `CTNative/Fixtures/ControlFlow/globals.test`; `CTNative/Lowering/Closures/{returned-closures,method-tables,owning-callables}.mlir`; `CTNative/Lowering/Exceptions/exceptions.mlir`; `CTNative/Optimization/deforestation.mlir`; `CTNative/Lowering/Emission/print-deduced.mlir`; `Target/Cpp/{native-number,const-bindings}.mlir` | 8 passed / 6 failed, 70.43s |
| Matcher fixes and browser adapters | The three failing fixture `.test` files, `Lowering/Scalars/scalar-unions.mlir`, `Optimization/deforestation.mlir`, `Target/Cpp/native-number.mlir`, plus `CTNative/Browser/native-dom-{strings,query-all}.test` | 7 passed / 1 failed, 157.84s |
| DOM index adapter | `CTNative/Browser/native-dom-{query-all,spread-length}.test`; `CTNative/Lowering/Maps/maps.mlir` | 2 passed / 1 failed, 79.12s |
| Storage and Map pin | `CTNative/Fixtures/Objects/{array-shrink,array-overwrite-loop}.test`; `CTNative/Fixtures/Optimization/deforestation.test`; `CTNative/Lowering/Maps/maps.mlir` | 4/4, 10.70s |
| Additional signature/shape pins | `CTNative/Lowering/Admission/{global-undefined,refusal-equality-order,divergence-refusals}.mlir`; `CTNative/Lowering/Scalars/{unary-plus,string-control-flow}.mlir`; `CTNative/Lowering/Objects/{struct,object-argument-lift,shape-field-names,array,one-shape-one-definition,receiver-lift}.mlir`; `CTNative/Lowering/Closures/{nested-closure-lift,closure-refusals,callback-arguments,closure-lift}.mlir` | 12 passed / 3 failed, 4.90s |
| Runtime proof controls | `CTNative/Lowering/Admission/{refusal-equality-order,divergence-refusals}.mlir`; `CTNative/Lowering/Closures/callback-arguments.mlir` | 3/3, 1.08s |

An earlier malformed filter was rejected before running any lit cases; it is
not included in these counts. The first six failures were missing typed Number
declaration normalization, an old parameter/length pin, and invalid FileCheck
variable syntax in a new LABEL. Their scalar differential binaries had passed.
The query-all failure found the missing `.value()` before `size_t` conversion
and old raw-number client comparisons. The later Map failure was a return pin.
The final three exposed prior precomputation removing intended runtime checks
and additive symbolic attributes after a callback marker: `optimize=false` and
exact marker matching retain those checks without changing their source bodies
or weakening admission.

Eight selected ownership cases passed using existing complete per-case checks:
`result_formal`, `local_add_saved_results`, `local_numeric_branch_lifetime`,
`size_one_saved_lifetime`, `local_field_readback_lifetime`,
`nested_map_saved_delete`, `recorder_entry_objects`, and `captured_template_child`.
The nested case reports two typed observations and two distinguishing mutations;
the recorder/template pair reports 62 typed observations and three mutations.
The first five passed before a nested observer rewrite accidentally changed
`shared_ptr::get()` in the embedded runtime. Restricting the new adapters to
the appended observer fixed that test client; only the remaining three reran.

The temporary driver calls `driver_execution.check_positive` for the first five,
selects one row for `driver_nested_maps.check_nested_maps`, and selects the named
recorder/template rows for `driver_recorder.check_recorders`. Its mutation rows
are `recorder_entry_remove_other`, `recorder_entry_fresh_reinsert`, and
`template_child_stale_key`. It uses:

```sh
PYTHONPATH=ctcompile/test python3 /tmp/ctcompile-number-carrier-ownership.py --translate build/ctcompile/tools/ctjs-translate/ctjs-translate --opt build/ctcompile/tools/ctjs-opt/ctjs-opt --node /home/ubuntu/tools/node-v26.8.1/bin/node --reference build/ctcompile/test/ctcompile-test-native-reference --work build/ctcompile/test/number-carrier-ownership
# Corrected remaining three; the first-five selection is empty in this copy:
PYTHONPATH=ctcompile/test python3 /tmp/ctcompile-number-carrier-ownership-remaining.py --translate build/ctcompile/tools/ctjs-translate/ctjs-translate --opt build/ctcompile/tools/ctjs-opt/ctjs-opt --node /home/ubuntu/tools/node-v26.8.1/bin/node --reference build/ctcompile/test/ctcompile-test-native-reference --work build/ctcompile/test/number-carrier-ownership
```

The selected checks include existing Node/interpreter comparisons, GCC/Clang,
printing variants, type-pin mutations, lifetime/sanitizer checks and native
symbol audits. Browser selector cases exercise public DOM/Style; they are not
full browser/VM differential measurements. All **62** final code/test SHA-256
hashes matched the devbox. Scoped clang-format covered 20 C++ files; Black and
syntax checks covered 17 changed Python files. Source observers and raw storage
pins were preserved. A stale raw-Boolean client argument/comparison was repaired
where encountered, without changing its JavaScript body.

Required `tools/format.sh --check` still reports **16 pre-existing diagnostics**
in untouched `ctdrive.cpp` (2), `HostContract/ProviderPaths.h` (2),
`PartialEvaluation/Heap.h` (4), and `Symbolic/Facts.cpp` (8). Full CTest/lit,
broad corpus/matrix and WPT/test262 were skipped. No push.

Initial process checks found no Claude identity among 67 Linux / 373 Windows
processes, but 57 Linux executable links were unreadable. Availability remained
uncertain; all edits stayed in ctcompile and the requested external plans.

## Semantic backlog and next boundary

The user asked that typed values account for the oddities in wtfjs. The new
[semantic edge-case plan](../plans/native-js-semantics.md) records its README
revision and distinguishes implemented building blocks from untested specimens.
It assigns coercion, equality, sparse arrays, primitive boxing/prototype hooks,
evaluation order, syntax and browser/async work to their owning layers. No
wtfjs corpus or illustrative snippet was executed in this documentation step.

Next introduce `js_basic_string<char>` / `js_string` with one admitted String
operation group and selected primitive-coercion witnesses. Retire the raw global
Number alias with its remaining printer/storage clients. The typed
`Symbol.hasInstance` / equivalent `std::holds_alternative` wrapper stays scheduled
with class/prototype proofs. Resolve NodeList indices above 1,000,000 returning
undefined before indexed Bootstrap `R.find`; retain its independent 2^24 spread
cap. Collections/document views, full Bootstrap and the application driver remain.
