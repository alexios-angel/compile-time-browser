# Typed Number bitwise operations and return dispatch, 2026-09-21 UTC

Continued clean `0587aa5d` on `ctcompile-v1`. The previous handoff identified
`Closures/primitive-cells.test`'s retained `index-switch.js` as the next compiler
boundary. No interrupted edits or relevant September-7 branch remained. Two
agents handled runtime/Core implementation and source checks while the main
thread handled lowering and return dispatch. A later independent review found
the missing clone charge described below.

Linux process inspection (69 processes) and Windows CIM inspection (358) found
no Claude executable, CLI or loop. Before browser landing, repeated inspection
(71 Linux, 354 Windows) again confirmed stopped; both checks were journaled.
The runtime agent also checked before browser edit batches.

## Landed

- `2526d714`: public `ctbrowser/core/number.hpp` extracts existing ToInt32 and
  ToUint32. Core CMake registers it; `script/vm/properties_inline.hpp` and
  `Script/builtins/values.cpp` delegate VM and Math clz32/imul conversions to it.
  Original coercion order and numeric behavior are retained.
- `827eadff`: `js_basic_num<double>` gains typed `~`, `&`, `|`, `^`, `<<`, `>>`
  and `.unsigned_shift_right(...)`, using Core conversion and masked counts.
  All results remain `js_num`. Raw implicit numeric operands remain excluded.
- `279436f1`: ordinary/static bitwise IR and unary bit-not use existing primitive
  Number conversion and the typed operators. Unsigned shift is a const member
  call. Object hooks, BigInt, Symbol and unsupported unions still refuse.
- `0636ced2`: the unchanged `mixed_concatenation.js` exception witness now runs
  natively; its prior refusal was stale after typed String arithmetic. The
  exception checker uses the existing public Core link helper and retains its
  Script/boxed-AOT symbol rejection.
- `b2c99d93`: ordinary return dispatch reuses `normalizeStructuredExits` on a
  disposable clone. Copy cost is reserved before allocation; successful bodies
  undergo fresh inference/admission and later refusal restores their originals.
  `structure-max-steps` bounds this work. Poison placeholders are chosen for
  their destination String-union type through existing scalar conversions.

## Focused validation

All builds and executable checks ran on the devbox under the shared build lock.
Explicit build commands were:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime \
  ctcompile-test-native-reference ctbrowser-test-math_basics \
  ctbrowser-test-vm_operators </dev/null
tools/remote-build.sh ctjs-opt ctjs-translate </dev/null
```

The initial 199-step build and subsequent focused incremental builds passed.
CTest selection `^(math_basics|vm_operators|ctcompile_native_runtime)$` passed
**3/3**. Native runtime checks include **23 conversion rows** and typed operator
and edge-case assertions.

Seven distinct selected lit cases passed across corrected runs:

- `Target/Cpp/native-number.mlir`.
- `CTNative/Lowering/Scalars/primitive-bitwise.test`: **76 main observations**
  in **eight native modes** (GCC/Clang × explicit/deduced × runtime/optimized),
  original `tilde.js` and `bits.js` on both compilers, four refusals and two
  distinguishing mutations. Node asserts types/values; generated binaries
  match VM output and reject Script/boxed-AOT symbols. Cases cover conversion,
  wrapping, non-finite values, masked shifts and operand evaluation order.
- `CTNative/Lowering/Admission/refusal-operands.mlir` and
  `CTNative/Lowering/Admission/divergence-refusals.mlir`. These three bitwise
  cases passed together **3/3 in 13.41 seconds** after printed-name corrections.
- `CTNative/Lowering/Closures/primitive-cells.test`: original 44 observations
  in eight modes, original shared-cell witness, four remaining refusals, two
  mutations and the newly admitted original return-dispatch witness.
- `CTNative/Lowering/Closures/return-dispatch.test`: **eight observations in six
  native modes** (runtime explicit/deduced and optimized explicit, GCC/Clang),
  mutation, budgets zero/one, and BigInt late-admission rollback retaining the
  original switch. Final isolated run: **1/1 in 6.25 seconds**.
- `CTNative/Lowering/Exceptions/exceptions.mlir`: **42 Node/VM programs**, with
  **22 native positives**, existing refusal/effect/budget controls and mutation.
  Its existing five String-lifetime programs retain explicit/deduced Clang
  ASan/UBSan checks. The corrected combined run took **83.42 seconds**; only
  the then-unfixed return-dispatch mutation pin failed in that run.

Initial bitwise failures were test spelling: EmitC elides its dialect prefix
inside function bodies. Initial return-dispatch lowering exposed Number poison
placeholders in String-union SCF results; the destination-typed fix resolved it.
Review also found allocation before budget charging; precharging now covers the
copy. Mutation attempts were rejected by the harness because a union print had
no numeric anchor, the numeric load preceded that anchor, or a Boolean print
used the tagged helper. The final test checks two numeric observations and
mutates the later one; the gate fails on the actual changed value. Budget pins
were corrected to include the printed `private` function visibility.

All **19 final code/test SHA-256 hashes match the devbox**. Historical source
sections in the primitive-cell and exception fixtures remain byte-identical.
Changed C++ files pass scoped pinned clang-format; the Python checker passes
Black and whitespace checks pass. Required `tools/format.sh --check` retains
**16 pre-existing diagnostics** in untouched `ctbrowser/tools/ctdrive/ctdrive.cpp`
(2), `HostContract/ProviderPaths.h` (2), `PartialEvaluation/Heap.h` (4), and
`Symbolic/Facts.cpp` (8). That C++ failure skips its broader Python/web stages.

No full CTest/lit, broad corpus/native matrix, WPT/test262, full wtfjs replay or
full Bootstrap run occurred. No additional sanitizer run beyond the selected
exception fixture occurred. Nothing was built locally or pushed.
Logs: `/tmp/ctcompile-bitwise-{build,gate,lit,lit2,format}.log`,
`/tmp/ctcompile-return-dispatch-{build,build2,lit,gate3,lit4,lit5,lit6,lit7}.log`.
Final manifest: `/tmp/ctcompile-bitwise-dispatch-sha256.txt`.

## Next boundary

The retained `nested-sibling.js` still refuses a captured function binding when
a nested writer calls a sibling reader. Prove that binding separately from the
already-supported cell pointer. The next primitive type work is relational
conversion and String ordering, with UTF-16 semantics and existing oracle gaps
kept explicit. Broader unions, containers, object hooks, Unicode alignment,
collections/document views, `Symbol.hasInstance`, NodeList indexing above
1,000,000 (separate from the 2^24 spread cap), full Bootstrap and the application
driver remain unfinished.
