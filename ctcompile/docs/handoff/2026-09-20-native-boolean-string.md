# Boolean/String transport, 2026-09-20 UTC

Resumed clean `435aa9b8` on `ctcompile-v1`, from the previous handoff and the
preserved `boolean-string-parameter.js` and `boolean-string-return.js` refusals
in `generic-addition.test`. No interrupted dirty work or relevant September-7
branch remained. Linux process inspection (66 processes) and Windows CIM
inspection (358 processes, executable paths and command lines) found no Claude
Code executable, Node CLI or loop. The result was journaled before implementation.
No browser or shared implementation file changed.

## Landed behavior

- `9b234233`: `boolean_string` owns exactly `js_boolean_t` or `js_string`.
  `nullable_boolean_string` adds distinct `undefined_t` and `js_null_t`
  alternatives, defaulting to undefined. Numeric/text/truthiness/typeof/printing
  helpers reuse the existing typed primitives and public Core operations.
  Widening preserves every source tag; definite observers check the alternative.
  Generic addition inspects the original String tag before selecting numeric
  addition or concatenation. Existing raw Boolean/String overloads remain
  compatible; no whole-union Map adapter is introduced.
- `0e1928ce`: compiler transport carries the two types through parameters, explicit returns,
  conditionals, loops and global reads/writes. Globals start as undefined and
  saved Strings remain owning copies. The return census retains semantic types;
  Boolean/Number/String joins still have no admitted carrier. Const/deduced
  printing recognizes the new types. Helper-name tables dispatch `typeof` and
  global printing; the existing narrow comparison gates remain unchanged.
- Map storage keeps its raw String representation and independently proved
  per-operation alternatives. A proved String extraction from a typed scalar
  uses `std::get<js_string>`, then unwraps at the existing storage boundary.
  Nullable union temporaries still fail the Map key/write proof gate.

Two subagents supplied runtime and source-test drafts, then released their claims
for parent integration. The runtime agent also completed a read-only compiler
review and found no concrete defect. The fixture agent later hit a rate limit;
its completed draft was preserved and gated by the parent.

The historical parameter and return programs execute unchanged in separate
compilation units, avoiding their duplicate `convert` declarations. The main
fixture adds 50 observations for Boolean false versus String `"false"`, `"0"`
and empty text; early global reads, all four optional return alternatives,
overwrite/saved-copy behavior, skipped/executed loops, arithmetic, addition,
concatenation, truthiness, `typeof`, NaN, signed zero and embedded NULs. The
separate original return program has two further observations.

## Focused validation

Every build and executable check ran on the devbox under
`/tmp/ctbrowser-devbox-build.lock`. Explicit build targets:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime \
  ctcompile-test-native-reference </dev/null
```

The first build failed in the changed `StringValues/Operations.cpp`: `auto`
inferred `ctjs::ValueType`, which could not receive a generic type after stripping
an LValue. Declaring `mlir::Type` fixed it; the remaining **four build steps passed**.

- Exact `ctcompile_native_runtime` CTest: **1/1 passed**, 0.01 seconds
  (0.02 seconds total CTest time). Runtime checks cover typed constructors,
  absence, widening, owning copies, numeric/String differences, surrogate
  concatenation and addition across admitted carrier pairs.
- `CTNative/Lowering/Scalars/{boolean-string-transport,generic-addition}.test`:
  **2/2 passed**, **16.22 seconds**. The new main program passes 50
  Node/VM/native observations in **eight native modes** (GCC/Clang ×
  explicit/deduced × runtime/optimized). Its original return program passes
  two observations on GCC and Clang. Four refusal controls and two numeric/text
  mutations pass. The existing generic fixture now has six refusal controls
  and three admission controls; all original source bodies remain unchanged.
- Initial selected regression run: **4/6 passed**, **57.52 seconds**. Passing
  cases were `Scalars/{optional-number-string,string-union-coercions}.test`,
  `Admission/refusal-operands.mlir` and `Target/Cpp/const-bindings.mlir`.
  `Scalars/scalar-unions.mlir` still expected the now-supported Boolean/String
  return to fail. Its unchanged source now pins the typed return; its rerun
  passed **1/1**, **0.20 seconds**.
- `Maps/map-mixed.mlir` retained its refusals, but two expectations cited the
  former missing optional-union carrier. They now pin the existing exact
  nullable Map key/write diagnostics, preserving original/forged/stale proofs
  and repeated-lowering controls. Its final rerun passed **1/1**, **55.18 seconds**.
  This includes 75 associative/ordered Map observations, 38 retained refusal
  controls, two existing admitted scalar-key controls, lifetime checks and
  the case's focused sanitizer variants.

These are **eight distinct selected lit cases**, not a full compiler suite.
The selected Map case includes its existing ASan/UBSan runs, owning-value lifetime
checks and Script-symbol scans. No standalone broad sanitizer suite, full
CTest/lit, corpus/native matrix, WPT/test262 or full wtfjs replay ran. Historical
browser/corpus measurements retain their scope; no full Bootstrap improvement
is claimed. No build ran locally and nothing was pushed.

All **19 final code/test SHA-256 hashes match the devbox**. All 13 changed C++
files pass pinned scoped formatting. The changed Python harness passes the installed `black --check`; `python3 -m black` was unavailable
in that interpreter. Required `tools/format.sh --check` retains the same 16
pre-existing diagnostics in untouched `ctbrowser/tools/ctdrive/ctdrive.cpp` (2),
`HostContract/ProviderPaths.h` (2), `PartialEvaluation/Heap.h` (4) and
`Symbolic/Facts.cpp` (8). Its early C++ failure skips the broader Python/web
format phases. Whitespace checks pass.

Local logs: `/tmp/ctcompile-boolean-string-{gate,gate2,regression,return-pin,
map-pin,format,format-final}.log`. Final code/test hashes:
`/tmp/ctcompile-boolean-string-sha256.txt`.

## Next boundary

Implement strict and loose equality for the admitted String-containing primitive
unions. Start with the complete `equality.js` controls in
`boolean-string-transport.test` and `optional-number-string.test`; promote them
only when their source semantics execute correctly. Keep type identity for
strict equality, and JavaScript conversion for loose equality. Cover
null/undefined, Boolean/Number/String distinctions, NaN and signed zero; reuse
existing primitive operations without a universal boxed value.

String ordering, broader unions, mixed container transport, object conversion
hooks and Symbol/BigInt arithmetic retain separate proof requirements. Core
parser gaps, general UTF-16 alignment, raw Number alias retirement,
collections/document views and the planned `Symbol.hasInstance` wrapper remain.
Indexed Bootstrap still needs the NodeList boundary above 1,000,000 resolved
while preserving the separate 2^24 spread cap. Full Bootstrap and the application
driver remain unfinished.
