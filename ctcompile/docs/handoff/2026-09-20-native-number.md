# Basic Number values, 2026-09-20 UTC

Resumed the clean **59307d41** / **92024490** Boolean migration. No interrupted
dirty work or old Codex WIP needed recovery. Parallel agents handled the naming
plan, Number class, caller migration and namespace review.

**21b2dee2** changes the type vocabulary to `ctnative::js_basic_num<T>` and
`ctnative::js_basic_string<T>`, with `js_num` / `js_string` aliases for `double`
and `char`. The maintained design and external master-plan parts 01/24/25 agree.
The subsequent user request adds **planned** `js_nan_t`: an explicit construction
token for a Number NaN, with Number `typeof`, false truthiness, unequal-to-self
strict equality and existing SameValue/SameValueZero rules. It introduces no
absence sentinel or separate JavaScript domain. Implement it with numeric
literals/conversions; the token is not part of this runtime commit.

**bb265540** adds `Runtime/Number.hpp`. `js_basic_num<double>` has explicit exact
binary64 construction/extraction, contextual truthiness, arithmetic and partial
ordering. Other representations are constrained out pending their proofs.
`global_number` returns `ctnative::js_num`; the emitter calls `.value()` before
passing the value to C varargs. Nullable/object scalar adapters preserve the
Number tag, NaN and signed zero. Existing numeric C++ clients extract explicitly.
The DOM session client also fixes two raw-bool comparisons left from **92024490**,
including their replacement needles, without changing source JS or expectations.

Raw arithmetic, Map storage, JSON, closure signatures and the f64 printer remain
on the global `using js_num = double` compatibility alias. Namespace review
caught generated tuples and callable signatures inside `namespace ctnative`;
they now spell `::js_num`, preventing accidental class adoption. The ordinary
f64 printer runs outside that namespace. Map NaN/zero normalization is unchanged.
No browser code, VM semantics, admission proofs or source fixtures changed.

## Focused validation

All builds/tests ran on the devbox under `/tmp/ctbrowser-devbox-build.lock`.
The first explicit build passed **11 steps**; qualifying captured types required
a **10-step** rebuild. The final client sync reported no Ninja work. Targets:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime ctcompile-test-native-reference ctcompile-native-pipeline-scalar_unions ctcompile-native-pipeline-numeric
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime
```

From `projects/compile-time-browser` on the devbox, runtime CTest passed **1/1**
(0.02s total). The first lit command passed **8/8** (44.11s); the separate owning
callables case passed **1/1** (11.05s). The first filter also named the nonexistent
`owning-callables.test`; the real `.mlir` case was then selected explicitly:

```sh
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_native_runtime$'
~/.lit-venv/bin/lit -v -j 2 build/ctcompile/test --filter='^ctcompile :: CTNative/((Lowering/Scalars/numeric[.]mlir)|(Lowering/Admission/global-undefined[.]mlir)|(Lowering/Closures/(returned-closures|method-tables)[.]mlir)|(Lowering/Closures/owning-callables[.]test)|(Fixtures/ControlFlow/globals[.]test)|(Fixtures/Scalars/scalar-unions[.]test)|(Browser/native-dom-data-session[.]test)|(Ownership/native-data-session[.]test))$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Lowering/Closures/owning-callables[.]mlir$'
```

Eight selected ownership cases also passed through the existing driver:

- `check_positive`: `result_formal`, `local_field_readback_lifetime`,
  `local_numeric_branch_lifetime`, `size_one_saved_lifetime`, `object_argument_exact`.
- `check_nested_maps`: only `nested_map_saved_delete`; its two distinguishing
  mutations, source observations, optimization/forgery and lifetime checks ran.
- `check_recorders`: only `recorder_entry_objects` and `captured_template_child`;
  62 typed observations and three selected mutations (`recorder_entry_remove_other`,
  `recorder_entry_fresh_reinsert`, `template_child_stale_key`). The selected rows'
  optimization, forgery, output, lifetime and sanitizer checks remained intact.

The temporary driver called `driver_execution.setup()` / `check_positive()` and
restricted the existing nested/recorder case dictionaries to those names; all
other recorder/refusal/snapshot/template rows were excluded. Exact invocation:

```sh
PYTHONPATH=ctcompile/test python3 /tmp/ctcompile-number-ownership-focused.py --translate build/ctcompile/tools/ctjs-translate/ctjs-translate --opt build/ctcompile/tools/ctjs-opt/ctjs-opt --node /home/ubuntu/tools/node-v26.8.1/bin/node --reference build/ctcompile/test/ctcompile-test-native-reference --work build/ctcompile/test/number-ownership-focused
```

All **20** changed code/test SHA-256 hashes matched the final devbox source.
Scoped clang-format, Black on **11** Python files, syntax and whitespace checks
passed. Required `tools/format.sh --check` still reports the same **16** pre-existing
diagnostics: `ctbrowser/tools/ctdrive/ctdrive.cpp` (2),
`ctcompile/lib/CTNative/HostContract/ProviderPaths.h` (2),
`ctcompile/lib/CTNative/PartialEvaluation/Heap.h` (4), and
`ctcompile/lib/CTNative/Symbolic/Facts.cpp` (8). Those files were not changed.
No full CTest/lit, broad corpus/matrix or WPT/test262 run was requested or performed.
Only the selected checks' existing sanitizer runs were used.

Process inspection found no Claude identity among 67 Linux and 362 Windows
processes, but 57 Linux executable links were unreadable. Availability was treated
as uncertain; edits stayed in ctcompile and the requested external plans.

## Next boundary

Migrate Number literals, conversion/math lowering and signatures in coherent
groups, retaining explicit storage/optional/collection adapters. Add `js_nan_t`
with numeric literal/conversion emission. Introduce `js_basic_string<char>` /
`js_string` with its first admitted String operation group. Retire the global
raw alias only after all clients migrate. Preserve the scheduled typed
`instanceof`/`Symbol.hasInstance` wrapper with constructor/prototype proofs.

Indexed Bootstrap `R.find` still needs the NodeList >1,000,000 undefined-slot
boundary reconciled; the independent 2^24 spread cap remains. Document views,
Object/Array prototypes, BigInt/Symbol, full Bootstrap and the application driver
are unfinished.
