# Primitive union shared cells, 2026-09-21 UTC

Resumed clean `3a0fb32f` on `ctcompile-v1`, following the shared-cell boundary
in HANDOFF and the unchanged `shared-mixed.js` refusal. No interrupted native
edits or relevant September-7 branch remained. Linux process inspection
(69 processes) and Windows CIM inspection (351 processes, with executable paths
and command lines) found no Claude executable, CLI or loop; journaled before
implementation. Two agents drafted focused source checks and independently
reviewed the compiler flow. Review found no concrete implementation issue.

## Landed

`cdf2c0a5` admits primitive cell stores only when the existing type join retains
the selected storage carrier. Existing scalar conversions handle both single
alternatives and compatible sub-unions. The five String-containing cell families
are Number/String, Boolean/String and their optional forms, plus optional String.
Cell loads own their String snapshots; owner locals and nested captured pointers
share the existing storage path. This change adds no runtime carrier or inference rule.

Initialization now follows inference's existing `kAssignedBeforeRead` proof:
when a write dominates every read, the hoisted initial need not fit a definite
union. Otherwise the emitted cell retains its actual initial, including undefined.
The source initial expression remains evaluated. Undefined, null, false, zero,
NaN and empty text retain their distinct behavior.

## Focused validation

All builds and executable checks ran on the devbox under the shared build lock:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-reference \
  ctcompile-native-pipeline-shared_cells </dev/null
```

The **seven-step build passed**, including plain, deduced and mutated numeric
shared-cell pipeline modules. Four distinct selected lit cases pass:

- `CTNative/Lowering/Scalars/strings.mlir`,
  `CTNative/Lowering/Closures/closure-refusals.mlir`, and
  `CTNative/Fixtures/Closures/shared-cell.test`: **3/3**, **5.65 seconds**.
  These preserve the original source bodies, numeric-cell differential,
  GCC/Clang clean compilation, deduction/provenance, mutation and escape controls.
- `CTNative/Lowering/Closures/primitive-cells.test`: **1/1**, **16.69 seconds**
  after the source adaptations below. It checks **44 main observations** in
  **eight native modes** (GCC/Clang × explicit/deduced × runtime/optimized),
  plus the unchanged original shared-variable witness on both compilers.
  Node checks value and type; native output matches the VM transcript and
  excludes Script/boxed AOT symbols. Five refusal controls and two distinguishing
  mutations pass. Whole-union stores cover Number/String and Boolean/String
  into their optional forms and optional String into both optional union cells.

Two initial new-fixture attempts failed in **0.10 seconds each** at admission.
The draft's early-return chains produced unsupported `scf.index_switch`;
nested conditional returns produced the same operation. Positive selectors now
use successive assignments and one return, retaining the same 44 Node results.
The initial early-return function is preserved as `index-switch.js`, with its
exact refusal. A nested function calling a sibling closure over shared state
also reached an existing captured-function binding refusal. The positive nested
writer now returns the cell directly while its sibling reader is called from
the owner; the original draft remains as `nested-sibling.js`. These are recorded
boundaries, not new admission. The historical `shared-mixed.js` is byte-identical
and executes unchanged. No compiler correction was needed after the first build.

All **five final code/test SHA-256 hashes match the devbox**. Both changed C++
files pass pinned scoped formatting; whitespace checks pass. Required
`tools/format.sh --check` retains **16 pre-existing diagnostics** in untouched
`ctbrowser/tools/ctdrive/ctdrive.cpp` (2), `HostContract/ProviderPaths.h` (2),
`PartialEvaluation/Heap.h` (4), and `Symbolic/Facts.cpp` (8). Its C++ failure skips
broader Python/web formatting. No Python or shell file changed.

No separate CTest, sanitizer, full lit/CTest, broad corpus/native matrix,
WPT/test262 or full wtfjs replay ran. No browser/VM/runtime implementation changed,
no build ran locally and nothing was pushed. Full Bootstrap remains unmeasured.
Logs: `/tmp/ctcompile-primitive-cells-{build,regression,gate,gate2,gate3,format}.log`.
Final manifest: `/tmp/ctcompile-primitive-cells-sha256.txt`.

## Next boundary

Normalize ordinary structured return dispatch, starting with preserved
`index-switch.js` in the new fixture. Reuse `normalizeStructuredExits` from
`Lowering/Exceptions/Recovery/Structure.cpp`, declared in `Recovery.h` and
already used on a disposable clone by `HostContract/ClassInitialization/Proof.cpp`.
It preserves selected switch regions and yields under a work budget. Failure
may leave its clone partly changed: publish only a complete successful clone,
then run fresh admission/inference. Do not merely admit an unlowered switch.

The retained `nested-sibling.js` still requires a captured-function binding proof;
it is distinct from forwarding a cell pointer. String relational comparison,
broader unions, mixed containers, object hooks, Core parser gaps and general
UTF-16 alignment remain. Collections/document views, `Symbol.hasInstance`,
NodeList indices above 1,000,000 (with the separate 2^24 spread cap), full Bootstrap
and the application driver are unfinished.
