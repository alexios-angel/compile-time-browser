# Optional Number/String transport, 2026-09-20 UTC

Resumed clean `61b688c3` on `ctcompile-v1`, using its handoff and the complete
`optional-global.js` refusal in `generic-addition.test`. No interrupted dirty
work or relevant September-7 branch remained. Linux process inspection (68
processes) and Windows CIM inspection (356 processes, including executable paths
and command lines) found no Claude Code executable, Node CLI or loop. This was
journaled before implementation. No browser or shared-file edit was needed.

## Landed behavior

- `1ff01365`: `nullable_number_string` owns exactly `undefined_t`, `js_null_t`,
  `js_num` or `js_string`, with undefined as its default. Named widening preserves
  nullable scalar/String tags and copies owning String values. Numeric/text/
  truthiness/typeof/printing operations reuse existing typed primitive and public
  Core helpers. Generic addition tests the original String alternative before
  choosing concatenation or numeric addition. Exact guards reject absent values
  when a definite Number/String observation is required. The former optional
  global-storage extraction overload remains for compatibility.
- `7ac81d52`: the existing inferred `Opt<Variant<Number,String>>` now has a native
  carrier through source globals, parameters, explicit returns, conditionals,
  loops and later primitive operations. The return census retains semantic types
  when joining carriers, so optional Boolean cannot stand in for optional Number.
  Global storage begins as undefined and preserves actual values on early reads;
  definite observations keep their checked extraction. Const qualification and
  deduced output recognize the owning carrier.

The original changing-global read/copy/reassign JavaScript remains unchanged in
both fixtures. Its former refusal pin is now a native admission check, and the
new fixture executes that body before additional observations. Those include
reads before declaration initialization, null/undefined/String/Number overwrites,
saved copies, all four return alternatives, skipped/executed loops, NaN, signed
zero, embedded NULs, numeric arithmetic, generic addition, concatenation,
truthiness and `typeof`.

Two subagents supplied the runtime and fixture drafts. Both later hit rate limits;
the parent took over their claims, reviewed the integration and ran the gates.
No completed independent review is claimed.

## Focused validation

All builds and executable checks ran on the devbox under
`/tmp/ctbrowser-devbox-build.lock`. Initial explicit targets:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime \
  ctcompile-test-native-reference </dev/null
```

The first build failed under `-Wmissing-field-initializers`: the new admission
`returnedType` member lacked a default initializer. Adding `= {}` fixed that
aggregate construction; the following **43-step build passed**. A later two-step
runtime rebuild covered a comment-only correction to the legacy global helper.

- Exact `ctcompile_native_runtime` CTest: **1/1 passed twice**, 0.01 seconds each
  (0.02 seconds total CTest time per run). Runtime checks exercise every admitted
  carrier pair, all four optional alternatives, widening, owning copies, numeric
  edge cases and excluded C++ construction types.
- `CTNative/Lowering/Scalars/{optional-number-string,generic-addition}.test`:
  **2/2 passed**, **14.43 seconds**.
- `CTNative/Lowering/Scalars/{optional-scalars.mlir,scalar-unions.mlir,
  string-union-coercions.test}` and `Target/Cpp/const-bindings.mlir`:
  **4/4 passed**, **11.24 seconds**.
- The new optional fixture was then strengthened to use explicit return branches
  for all four alternatives. Its final rerun passed **1/1**, **12.47 seconds**.
  It checks **53 Node/VM/native observations**, **eight native modes**
  (GCC/Clang × explicit/deduced × runtime/optimized), **four refusal controls**
  and **two distinguishing mutations**. Each native mode checks its VM transcript
  and absence of Script/boxed AOT symbols; Node independently checks values,
  types and `Object.is`. The old generic fixture retains its 48 observations;
  its optional-global case is now positive, leaving eight refusal controls.
- All **14 final code/test SHA-256 hashes** match the devbox. All **12 changed
  C++ files** pass pinned scoped formatting; `git diff --check` passes.
  Required `tools/format.sh --check` retains the same **16 pre-existing
  diagnostics** in untouched `ctbrowser/tools/ctdrive/ctdrive.cpp` (2),
  `HostContract/ProviderPaths.h` (2), `PartialEvaluation/Heap.h` (4) and
  `Symbolic/Facts.cpp` (8).

These are **six distinct selected lit cases**, not a full compiler suite. Full
CTest/lit, broad corpus/matrix, WPT/test262, separate sanitizer runs and full wtfjs
replay were **not run**. Historical browser/corpus measurements remain unchanged;
no full Bootstrap increase is claimed. No build ran locally and nothing was pushed.

Local logs: `/tmp/ctcompile-optional-number-string-{gate,gate2,regression,
runtime-final,returns,format}.log`. Final hashes:
`/tmp/ctcompile-optional-number-string-sha256.txt`.

## Next boundary

Carry closed Boolean/String values through parameters, returns and globals,
starting with `boolean-string-parameter.js` and `boolean-string-return.js` in
`generic-addition.test`. Use the typed String alternative; retain null/undefined
separately if a source global or later join introduces absence. Local Boolean/
String arithmetic already works and should reuse the same primitive operations.

Broader unions, mixed container storage, optional-union equality/ordering,
object conversion hooks and Symbol/BigInt arithmetic retain their separate
proof requirements. Core parser gaps, general UTF-16 alignment, raw Number alias
retirement, collections/document views and the planned `Symbol.hasInstance`
wrapper remain unfinished. Indexed Bootstrap still needs the NodeList boundary
above 1,000,000 resolved while preserving the separate 2^24 spread cap; full
Bootstrap and the application driver remain unfinished.
