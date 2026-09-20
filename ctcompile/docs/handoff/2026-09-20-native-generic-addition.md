# Generic primitive addition, 2026-09-20 UTC

Resumed clean `a7abf61b` on `ctcompile-v1`. Its handoff, current-work journal and
native-type plan all identified generic optional/union `+` as the next boundary.
No unfinished ctcompile diff or relevant unmerged September-7 branch remained.
Linux executable/CLI inspection (69 processes) and Windows CIM inspection (349
processes, including executable paths and command lines) found no Claude Code or
loop processes; that result was journaled before implementation. Two independent
agents drafted the runtime carrier/checks and source fixture while the main
thread implemented inference and lowering.

## Landed behavior

- `bede47ad`: `number_string` owns exactly `js_num` or `js_string`. Constrained
  `ctnative::add` accepts the seven existing exact/finite primitive carriers.
  It selects concatenation from their original tags, preserving Boolean words,
  null/undefined spelling, NULs and surrogate joins; otherwise it calls existing
  numeric conversions and Number addition. No universal value box is introduced.
- `b9aa1580`: type inference proves a Number/String result for finite primitive
  operands. Lowering transports that carrier through arguments, separate returns,
  conditional and loop edges, later arithmetic, truthiness, `typeof`, exact String
  concatenation and global stores/observations. Exact Number/String values widen
  into the union. Const qualification and deduced output retain its owning type.
- A global uses `std::optional<number_string>` to distinguish missing storage
  from a real Number zero; extraction checks initialization. This optional is
  not the source null/undefined carrier. The preserved changing-global source
  reads infer `Opt<Variant<Number,String>>` and remain refused.

Runtime checks exercise all supported carrier pairs and both union alternatives,
including false versus String digits, null versus undefined, NaN, signed zero,
embedded NULs, surrogate joins, chained addition and saved owning copies. Type
inference adds optional-String, chained-result and unknown-operand controls.

## Focused validation

All builds and executable checks ran on the devbox under
`/tmp/ctbrowser-devbox-build.lock`. Explicit build targets:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime \
  ctcompile-test-type-inference ctcompile-test-native-reference </dev/null
```

- CTest selection
  `-R '^(ctcompile_native_runtime|ctcompile_type_inference)$'`:
  **2/2 passed**, 0.27 seconds initially and 0.25 seconds after the first fixture
  correction. The runtime target took 0.01 seconds in each run; inference took
  0.25 and 0.23 seconds respectively.
- `CTNative/Lowering/Scalars/generic-addition.test`: initial corrected
  **1/1 pass**, 13.28 seconds, with 44 observations. The final fixture adds
  separate return branches and loop-carried addition: **48 observations**,
  **eight modes** (GCC/Clang × explicit/deduced × runtime/optimized), **nine
  refusal controls** and **two distinguishing mutations**. It passes in the
  selected eight-case run below. Each native mode checks the VM transcript and
  native binary symbols; Node independently checks expected value/type with
  `Object.is`. No Script or boxed AOT dependency enters the native executable.
- Eight selected lit cases through `build/ctcompile/test`:
  `CTNative/Lowering/Scalars/{generic-addition.test,string-union-coercions.test,
  string-coercions.test,string-arithmetic.test,optional-scalars.mlir,
  scalar-unions.mlir}`, `CTNative/Lowering/Admission/refusal-operands.mlir`,
  and `Target/Cpp/const-bindings.mlir`. The first run passed **6/8** in
  **17.05 seconds**; both stale expectations were corrected and the two affected
  cases then passed **2/2** in **0.31 seconds**. Thus all **eight distinct cases**
  pass across corrected runs; this was not a full compiler lit run.
- All **18 final code/test SHA-256 hashes** matched the devbox. Pinned scoped
  C++ formatting and `git diff --check` passed. Required `tools/format.sh --check`
  retained the same **16 pre-existing diagnostics**:
  `ctbrowser/tools/ctdrive/ctdrive.cpp` (2), `HostContract/ProviderPaths.h` (2), `PartialEvaluation/Heap.h`
  (4), and `Symbolic/Facts.cpp` (8). No changed C++ file produced a diagnostic.

The devbox was initially unreachable; starting it and refreshing its access rule
restored access. A subsequent home-IP rotation required another `allow-ip`.
Both failures occurred before build/test execution. No local build was attempted.
Local logs are `/tmp/ctcompile-generic-add-{gate,gate2,focused,focused2,corrected}.log`
and `/tmp/ctcompile-generic-add-format.log`; hashes are in
`/tmp/ctcompile-generic-add-sha256.txt`.

Full CTest, full compiler lit, broad corpus/matrix, WPT/test262, standalone
sanitizer runs and a complete wtfjs replay were **not run**. Existing WPT/test262
and corpus numbers remain historical; no full Bootstrap increase is claimed.

## Preserved controls and next boundary

The old optional-add/optional-pair bodies in `string-union-coercions.test`,
Boolean-add body in `string-coercions.test`, Boolean/String addition body in
`refusal-operands.mlir` and mixed-present return body in `optional-scalars.mlir`
are unchanged JavaScript, now checked as native. The broader Boolean/Number/String
control fails when its `scf.if` result lacks a carrier, before the addition.

The first new fixture failed on a changing-global read: its source type includes
absence. The complete read/copy/reassign body is retained as `optional-global.js`
with an exact diagnostic pin; the positive fixture retains the global stores.
Implement a source carrier that preserves undefined, null, Number and String
before promoting that control. Test early reads and saved copies across later
writes in both directions; do not reinterpret the storage optional as null.

Boolean/String signatures/globals, wider unions and mixed container payloads
remain outside this slice. Number/String equality/ordering, object conversion
hooks, Symbol/BigInt arithmetic, public Core parser gaps and general UTF-16
alignment need separate work. The scheduled `Symbol.hasInstance` wrapper belongs
with constructor/prototype proof, not with this union. Collections/document views,
the indexed Bootstrap NodeList boundary above 1,000,000 (preserving the separate
2^24 spread cap), full Bootstrap and the application driver remain unfinished.
