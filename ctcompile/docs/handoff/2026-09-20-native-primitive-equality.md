# Typed primitive equality, 2026-09-20 UTC

Resumed clean `92c0628e` on `ctcompile-v1`, from the handoff and the three
preserved `equality.js` controls in `generic-addition.test`,
`optional-number-string.test` and `boolean-string-transport.test`. No interrupted
dirty work or relevant September-7 branch remained. Linux process inspection
(68 processes) and Windows CIM inspection (353 processes, including executable
paths and command lines) found no Claude executable, Node CLI or loop. This was
journaled before implementation. No browser or shared implementation file changed.

## Landed behavior

- `0111e8e3`: `primitive_equal` and `primitive_strict_equal` accept the nine
  existing typed primitive carriers and the two absence tokens, returning
  `js_boolean_t`. A constrained visitor traverses the existing finite variants,
  borrows String storage and reuses the existing nullable scalar tags. Strict
  equality checks JavaScript kinds; loose equality compares two Strings before
  coercion and rejects nullish/String matches. Scalar equality and public Core
  parsing supply the remaining behavior. NaN never equals itself, signed zeros
  compare equal, and Boolean false differs from String `"false"`. No combined
  boxed carrier or String copy is introduced. Raw Map variants, objects, raw
  C++ arithmetic, pointers and merely convertible classes are excluded.
- `12f34e16`: the existing String comparison admission rule now accepts two
  proved primitive carriers when at least one includes String. Lowering emits
  the typed helper for mixed/optional pairs; exact String pairs retain direct
  C++ comparison. Existing `!=`/`!==` lowering negates the Boolean result.
  Object hooks, broader unions, Symbol/BigInt comparisons and ordering remain
  outside this admission change.

Two subagents drafted runtime checks and source fixtures independently of the
compiler changes. Both later hit rate limits. The parent preserved their drafts,
took over the runtime claims after its agent stopped, reviewed the implementation
and ran all gates. No completed independent review is claimed.

The three historical equality programs execute unchanged. The generic witness
runs at the start of the main fixture; the two optional witnesses run separately
to preserve their duplicate `convert` declarations. The main fixture checks
48 observations across all nine carriers, parameter/return/global transport,
early reads and saved values, cross-union pairs, equality/inequality, absence,
NaN, signed zero, Boolean words, empty text, whitespace/radix conversion and NULs.
The two optional witnesses add three observations each.

## Focused validation

All builds and executable checks ran on the devbox under
`/tmp/ctbrowser-devbox-build.lock`:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime \
  ctcompile-test-native-reference ctcompile-native-pipeline-string_snapshots </dev/null
```

The **nine-step build passed** on its first attempt, including regenerated
plain/deduced/mutated String-snapshot modules.

- Exact `ctcompile_native_runtime` CTest: **1/1 passed**, **0.01 seconds**
  (0.02 seconds total CTest time). It exercises equality in both directions
  across all carrier families, same/different primitive kinds, null/undefined,
  NaN, signed zero, text coercion, NUL/surrogates and excluded argument types.
- Initial selected lit run: **2/4 passed**, **18.82 seconds**. Passing cases:
  `CTNative/Lowering/Scalars/primitive-equality.test` and
  `CTNative/Fixtures/Maps/string-snapshots.test`. The new fixture checks
  **48 main observations in eight native modes** (GCC/Clang × explicit/deduced
  × runtime/optimized), **six additional observations** in the two original
  optional programs on GCC and Clang, **five refusal controls** and **two
  distinguishing mutations**. Node checks expected value and type; native
  binaries match the VM transcript and reject Script/boxed AOT symbols.
  The existing snapshot fixture retains differential execution, clean GCC/Clang
  compiles, deduction/provenance checks and its mutation.
- Two historical pin failures were corrected without changing source bodies:
  `Lowering/Maps/string-snapshots.mlir` expected the already-supported optional
  String numeric/addition cases to fail, and `Lowering/Scalars/strings.mlir`
  expected Number/String returns to lack a carrier. The mixed String equality
  probes now pin the new helper; optional numeric/addition probes pin their
  existing helpers with optimization disabled. The shared-cell probe stays
  refused, now at the exact String and Number assignment diagnostics.
- Final selected regression run: **5/5 passed**, **20.61 seconds**:
  `Lowering/Scalars/{generic-addition,optional-number-string,
  boolean-string-transport}.test`, `Lowering/Scalars/strings.mlir` and
  `Lowering/Maps/string-snapshots.mlir`. The three former equality refusal pins
  are positive controls; their remaining refusal counts are five, three and
  three respectively. All original source sections are preserved.

These are **seven distinct selected lit cases**, not a full compiler suite.
All **10 final code/test SHA-256 hashes match the devbox**. All **four changed
C++ files** pass pinned scoped formatting, and whitespace checks pass.
Required `tools/format.sh --check` retains the same **16 pre-existing C++
diagnostics** in untouched `ctbrowser/tools/ctdrive/ctdrive.cpp` (2),
`HostContract/ProviderPaths.h` (2), `PartialEvaluation/Heap.h` (4) and
`Symbolic/Facts.cpp` (8). Its early C++ failure skips broader Python/web formatting.
A final comment-only clarification in `strings.mlir` was synced and hash-checked;
its RUN lines and JavaScript remain the tested content.

Full CTest/lit, broad corpus/native matrix, WPT/test262, separate sanitizer runs
and full wtfjs replay were **not run**. Historical snapshot sanitizer/full-suite
measurements retain their old scope. No full Bootstrap improvement is claimed,
no build ran locally and nothing was pushed.

Local logs: `/tmp/ctcompile-primitive-equality-{gate,regression,format}.log`.
Final hashes: `/tmp/ctcompile-primitive-equality-sha256.txt`.

## Next boundary

Support finite String-containing unions in shared cells and captured bindings,
starting with the unchanged `shared-mixed.js` in `Lowering/Scalars/strings.mlir`.
Its before/call/after comparison is now representable, but String and Number
assignments to the shared union still fail admission. Add proved widening at
those assignments, retain owning copies, and preserve absence on observable
initial reads. Exercise writes through captured pointers and saved reads across
later mutations; keep unsupported wider unions refused.

String ordering, broader unions, mixed container storage, object conversion
hooks and Symbol/BigInt arithmetic retain separate proof requirements. Core
parser gaps, general UTF-16 alignment, raw Number alias retirement,
collections/document views and the planned `Symbol.hasInstance` wrapper remain.
Indexed Bootstrap still needs the NodeList boundary above 1,000,000 resolved,
with the separate 2^24 spread cap retained. Full Bootstrap and the application
driver remain unfinished.
