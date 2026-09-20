# Primitive String coercions, 2026-09-20 UTC

Resumed clean **2fe9a029** from HANDOFF and the maintained type plan. No interrupted
dirty changes or September 7 WIP branch remained. Parallel agents audited Core
semantics, added the focused source fixture and reviewed compiler admission.
Linux executable-link access was initially incomplete; a subsequent complete
comm/command-line inspection plus Windows executable/CLI checks confirmed Claude
stopped before the Core edit, with another check before landing it.

## What landed

- **d1f98f2a** changes only `ctbrowser/lib/Core/number_format.cpp`: check
  `abs(value) < 1e15` before the integer fast-path cast. It removes undefined
  behavior for large finite Numbers without changing the formatting algorithm.
  No VM binding or Script source changed; focused VM formatting answers remain.
- **ff023451** adds `js_string.to_number() -> js_num` and `js_string + js_num` /
  `js_num + js_string` overloads. These call public Core `string_to_number` and
  `number_to_string`, the same functions used by Script. Concatenation still
  delegates to the existing owning String concatenation implementation.
- **0d5cdec0** admits exact String unary `+`/`-` and exact String/Number `+` in
  either order. Lowering emits `.to_number()` and typed addition; unary minus
  negates the converted Number. The unary-plus identity pattern still receives
  only original Number operands. Const analysis recognizes only the exact
  String-to-Number method signature. The existing differential helper accepts
  `--core-build`, reusing configured Core/allocator dependencies from the native
  link helper; the default remains a header-only link.

For example, the emitted value operations now read `text.to_number()` and
`prefix + number`. No VM, GC, generic property lookup or universal value box was
added. Existing optional String concatenation and raw storage adapters remain.
Object coercion, optional/union String numeric conversion, String/Boolean addition,
loose String/Number equality and String ordering still refuse at compile time.

## Measured focused checks

Builds and remote commands held `/tmp/ctbrowser-devbox-build.lock`. Explicit
build targets across the two builds and final synchronization were:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime ctcompile-test-native-reference
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime ctcompile-test-native-reference ctbrowser-test-number_format ctbrowser-test-core_text
tools/remote-build.sh ctjs-opt ctjs-translate
```

The final synchronization reported no work. All **13** final code/test SHA-256
hashes matched the devbox. Tests used Node **v26.8.1**, GCC **13.3.0**, and pinned
Clang **24.0.0git e3986d2253e4cf600d9c55badda4ddb0ec0f0ce2**. The reference VM came
from this tested tree, including the Core guard fix. The wtfjs inventory revision
remains `2c00c4a5759c3d8a37b1237619ea664c16ce90ee`; the `baNaNa` expression is adapted
to function parameters and observable globals to retain runtime conversion.

Exact successful test selections on the devbox:

```sh
ctest --test-dir build --output-on-failure --no-tests=error -R '^(ctcompile_native_runtime|number_format|core_text)$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: (CTNative/Lowering/Scalars/(strings|unary-plus)[.]mlir|Target/Cpp/native-string[.]mlir)$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/(Lowering/Scalars/string-coercions[.]test|Browser/native-dom-strings[.]test)$'
```

The three CTests pass in **0.03s**; an earlier runtime-only check also passed.
The three existing lit cases pass in **2.26s**; the final two-case selection
passes in **155.36s**. These are **five distinct selected lit cases**, not a full
compiler suite.

`string-coercions.test` checks **18 distinct observations** against explicit
Node value/type/NaN/zero-sign expectations, then compares native with the VM in
**eight executions**: GCC/Clang × explicit/deduced printing × optimized/runtime
lowering. It covers `baNaNa`, concatenation versus arithmetic, both addition
orders, negative zero, empty/Unicode whitespace, hex, infinities, invalid/NUL
strings and decimal/exponent formatting thresholds. IR/C++ pins retain the
runtime conversion and both mixed operand signatures. **Six source refusals**
cover objects, optional String conversion, String/Boolean addition, loose equality
and ordering. **One String mutation** must fail naming the `banana` global.

The initial new-test run passed Node and runtime IR/C++ pins but failed to link:
the raw Core archive also needs its configured mimalloc dependency. Reusing the
existing native link helper fixed the link. The successful gate explicitly uses
`g++` for its GCC arms; the build's default compiler is Clang. No JS semantics or
expected values changed to get the test passing.

A focused sanitizer control compiled the old and corrected Core formatter with
`-fsanitize=float-cast-overflow,undefined -fno-sanitize-recover=all -O2` on the
devbox. The old source fails at `1e20`, before the guard; the corrected source
passes roundtrips of ±1e20, ±1e21 and ±DBL_MAX, plus negative-zero formatting.
The existing `core_text` and `number_format` tests retain regression coverage.
This was a Core formatter check, not a full sanitizer build.

Pinned C++ formatting, both changed Python files' Black/AST checks and whitespace
checks pass. Required `tools/format.sh --check` retains **16 pre-existing**
diagnostics in four untouched files: `ctdrive.cpp`, `ProviderPaths.h`, `Heap.h`
and `Facts.cpp`. Full CTest/lit, broad corpus/matrix, WPT/test262 and a complete
wtfjs replay were skipped. No push occurred.

## Exact next boundary

Extend proved String numeric arithmetic (`-`, `*`, `/`, `%`, `**`) through the
same `.to_number()` boundary, with source controls. Then handle the remaining
Boolean/absence-to-String addition pairs. Do not widen equality or ordering via
a general numeric admission predicate: String lexicographic comparison and loose
equality have different semantics. Object hooks and optional/union String numeric
conversion still need proofs.

Core parsing still has reviewed gaps for repeated signs, radix values beyond
uint64, overflow/underflow followed by garbage, and oversized exponents. This
batch did not remeasure those cases. Fix and compare them separately in Core;
do not duplicate a parser in the native class. General UTF-16 length/index/casing
alignment remains separate from this type migration. The formatter guard fix
also does not widen existing bounded Number-key proofs.

Raw Number alias retirement, collections/document views and the planned
`Symbol.hasInstance` wrapper remain. Indexed Bootstrap `R.find` still needs the
NodeList >1,000,000 undefined-slot boundary reconciled with the separate 2^24
spread cap preserved. Full Bootstrap and the application driver are unfinished.

## Extended arithmetic and primitive operands, 2026-09-20 UTC

Continued clean **48c93641** for the user's arithmetic/Boolean overload request,
then the follow-up request to continue String numeric arithmetic and primitive
coercions. Process checks found no Claude executable/CLI/loop among 67 Linux and
359 Windows processes, with no unreadable candidate CLI. No browser code changed.

**9c23d2e6** adds two constrained friend templates in `js_basic_string` for
built-in arithmetic and `js_boolean_t`. Boolean values retain `true`/`false` text;
numeric values use public Core formatting after binary64 conversion. Integer
precision follows Number semantics (`9007199254740993LL` becomes
`9007199254740992`); extended floating-point overflow is checked before narrowing.
The existing explicit constructors and typed Number overloads remain. Pointer,
null-pointer, enum and merely convertible class arguments are rejected.
Runtime checks cover 18 inputs in both operand orders, including signed zero,
NaN, both Boolean types and extended-range numbers. Extended-range checks run
only where `long double` has a wider range than `double`.

`nullable_scalar.to_string()` selects the existing four primitive alternatives
and returns `js_string`, preserving null/undefined, Boolean words, NaN and zero
formatting. **dd5021d5** enables exact String numeric subtraction, multiplication,
division, remainder and exponentiation through `.to_number()` and the existing
Number operations/guards. String addition also accepts Boolean and finite scalar
unions; tagged values use `.to_string()`, with an exact const-method signature.
Numeric comparison/equality admission is unchanged.

Focused devbox commands (all under the build lock):

```sh
tools/remote-build.sh ctcompile-test-native-runtime
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_native_runtime$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: (CTNative/Lowering/Scalars/(string-arithmetic[.]test|string-coercions[.]test|strings[.]mlir|optional-scalars[.]mlir|scalar-unions[.]mlir)|Target/Cpp/native-string[.]mlir)$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Lowering/(Scalars/string-arithmetic[.]test|Admission/(refusal-operands|divergence-refusals)[.]mlir)$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Lowering/Admission/refusal-operands[.]mlir$'
```

Runtime CTest passes **1/1 (0.02s)**. The six-case lit selection initially passed
five and failed the new fixture at an unproved global `undefined` binding
(**6.52s**). Uninitialized local values now supply the same undefined primitive;
no global identity proof was added. The next selection passed the arithmetic and
divergence cases but stopped in an old array-refusal control (**7.11s**).
Default precomputation had erased its branch before admission. Runtime-only
settings now preserve the array, bitwise and frame-slot refusal controls; the
single-case rerun passes **1/1 (0.13s)**. That is **eight distinct selected lit
cases passing across the corrected runs**, not a full suite.

The new `string-arithmetic.test` checks **26 Node value/type/NaN/zero-sign
observations**, then VM/native agreement in **eight executions** (GCC/Clang ×
explicit/deduced output × optimized/runtime lowering). It retains four refusal
controls and one numeric mutation. Six observations exercise a single parameter
with Number, Boolean, null and undefined tags, including NaN and negative zero.
The existing 18-observation String coercion test also passes its eight modes,
six refusals and mutation; its former exact Boolean refusal now uses an
unsupported String/Number parameter union. Admission fixtures now pin admitted
Number/String addition and refused optional String subtraction.

GCC additionally compiles the same Runtime/NativeRuntime.cpp and a standalone
String.hpp concatenation probe with C++23, -O2, -pedantic, -Wall, -Wextra, -Werror
and -Wconversion. These two checks compile only; runtime execution is the named
CTest and source fixtures. The source fixtures retain their Script/AOT symbol
audits. All **11** final code/test hashes match the devbox. Seven changed C++
files pass pinned formatting; whitespace and document checks pass. Required
`tools/format.sh --check` still reports the same 16 pre-existing diagnostics in
four untouched files. No full CTest/lit, broad corpus/matrix, WPT/test262,
sanitizer replay or full wtfjs replay ran; no push occurred.

The next coercion boundary is optional String numeric conversion and closed
Boolean/String union concatenation. Keep missing/null tags and the original
finite-alternative proof. Mixed String/Number joins, object hooks, String
ordering and loose equality do not gain support from these overloads. Shared
Core parser gaps, UTF-16 alignment, raw Number alias retirement, collections,
document views, `Symbol.hasInstance` and indexed Bootstrap remain separate work.
