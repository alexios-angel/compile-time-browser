# Symbol construction and sibling captures, 2026-09-21 UTC

Continued clean `aebfa918`, then resumed the same work after interruption. The
previous handoff named fresh Symbol construction and methods; the older retained
`nested-sibling.js` witness supplied the parallel closure task. No unmerged
`codex-wip-20260907` branch or predecessor edits remained at startup. Linux and
Windows process identity checks found no Claude CLI or loop, recorded in the
shared journal before work. This slice changes only ctcompile code/tests/docs.

## Landed

- `cb07719d` proves fresh Symbol construction and direct primitive methods in
  the existing fingerprinted DOM and intrinsic entry contracts. Zero arguments
  or one exact String/undefined argument call the existing native factory.
  Immutable constructor aliases are admitted. Direct zero-argument `toString`
  and `valueOf` calls require the exact primitive receiver and original
  prototype; inference and host-result retyping preserve Symbol results.
- `46ae2476` forwards data captures through a bound sibling call when caller and
  callee already capture the identical cell in the same creator frame. Immutable
  values and mutable pointers reuse the existing capture proof and annotations.
  A candidate lift runs on a disposable module and publishes only when both
  sides lift. Failure keeps the original callable binding executable. The old
  `nested-sibling.js` body is unchanged and now admitted. Missing captures,
  deeper relays, escaping callers and reassigned function bindings stay refused.

The new Symbol source path uses `ctnative::Symbol`, `js_symbol_t` and their
existing methods. It adds no runtime implementation, GC, VM context or Script
dependency. Symbol copies preserve identity; descriptions own their snapshots.
No browser or runtime-oracle implementation changed. The sibling rollback test
deliberately executes refused CTJS through the existing boxed development backend;
it does not provide a native fallback.

## Focused validation

All builds/tests ran on the devbox under `/tmp/ctbrowser-devbox-build.lock`.
Explicit `tools/remote-build.sh` targets across the corrected runs were
`ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract`,
`ctcompile-test-native-runtime`, `ctcompile-test-native-reference` and
`ctcompile-native-pipeline-deep_bindings`. No no-argument build was used.

The exact CTest selections were:

```sh
ctest --test-dir build --output-on-failure --no-tests=error \
  -R '^(ctcompile_host_contract|ctcompile_native_runtime)$'
ctest --test-dir build --output-on-failure --no-tests=error \
  -R '^ctcompile_host_contract$'
```

They passed **2/2 in 0.57 s**, and the later host check passed **1/1 in 0.58 s**.
That is two distinct CTests, not three. The selected lit cases were:

| Case under `CTNative/` | Final result |
| --- | --- |
| `Lowering/Scalars/symbol-boundary.test` | Passed in the initial three-case Symbol selection. |
| `Exports/native-intrinsic-symbols.test` | Passed with DOM Symbols, **2/2 in 51.03 s**. |
| `Browser/native-dom-symbols.test` | Passed in that corrected two-case selection. |
| `Lowering/Closures/closure-refusals.mlir` | Passed in both selected closure runs. |
| `Lowering/Closures/primitive-cells.test` | Passed in both selected closure runs. |
| `Fixtures/Closures/deep-binding.test` | Passed after regenerating its build-owned modules. |
| `Lowering/Closures/sibling-captures.test` | Final source passed alone, **1/1 in 6.63 s**. |

Use the generated `build/ctcompile/test` configuration with anchored filters.
The final sibling selection was:

```sh
~/.lit-venv/bin/lit -av projects/compile-time-browser/build/ctcompile/test \
  --filter='^ctcompile :: CTNative/Lowering/Closures/sibling-captures[.]test$'
```

The preceding closure filter selected exactly the other three rows plus
`sibling-captures.test`. Seven distinct lit cases passed across corrected runs;
no single seven-case full replay was made.

Measured observations:

- Symbol exports: **48 native executions, 74 refusals, one identity mutation**.
  Eight Node/VM observations include a 14-part primitive-method transcript.
  Checks cover repeated fresh construction, aliasing, loop identities, formatting
  of absent and empty descriptions, owning String snapshots, NUL
  and surrogate text. The old fresh/method refusal sources execute unchanged.
- DOM Symbols: **32 native executions, 36 refusals, one identity mutation**.
  The existing 21 attribute observations and two return paths remain, along
  with branch/loop/return transport and the unchanged former fresh refusal.
- Sibling captures: **five Node/VM observations in four native executions**
  (optimized/runtime lowering on GCC/Clang), four refusals and one mutation.
  Native artifacts use ordinary capture parameters and reach zero ctbrowser
  symbols. The refused escaping closure installs all four boxed entries with
  zero interpreted entries and produces `rollbackResult=9` after two calls.
  Existing primitive-cell and deep-binding execution/printing controls pass.
- All **16 final code/test hashes** match the devbox: nine Symbol files and
  seven sibling files. Scoped formatting passes for **12 C++ files and two
  Python files**; the embedded rollback Python parses and follows Black.
  A separate read-only agent review found no concrete sibling-proof defect.

Required `tools/format.sh --check` still reports **16 pre-existing diagnostics**
in `ctbrowser/tools/ctdrive/ctdrive.cpp`,
`ctcompile/lib/CTNative/HostContract/ProviderPaths.h`,
`ctcompile/lib/CTNative/PartialEvaluation/Heap.h` and
`ctcompile/lib/CTNative/Symbolic/Facts.cpp`. It stops before repository-wide
Python/web formatting. Changed Python files passed separately; this is not a
repository formatting pass.

Corrections made during validation:

- Const-incorrect MLIR handles in the sibling draft initially stopped the
  explicit build; corrected before execution gates.
- Fresh Symbol results initially reached the host void-result retyping path.
  Adding the missing `returnsSymbol()` guard fixed the source-lowering crash.
  The object-description refusal also needed its explicit `bad` entry name.
- The prebuilt deep-binding module still used the former raw Number alias.
  Regenerating `ctcompile-native-pipeline-deep_bindings` fixed that failure;
  no fixture source or expected result changed.
- The boxed regression needed the existing boxed fixture unused-variable/
  parameter warning policy. Suppression surrounds only its generated include;
  its client and all native output retain the normal warning gate.
- Initial multi-command SSH scripts consumed their following commands as stdin.
  Gates now use `ssh -n`; the skipped selected lit commands were run explicitly.

Local evidence includes `/tmp/ctcompile-fresh-symbol-gate4.log`,
`/tmp/ctcompile-fresh-symbol-lit.log`, `/tmp/ctcompile-fresh-symbol-hashes.txt`,
`/tmp/ctcompile-sibling-gate2.log`, `-gate3.log`, `-gate4.log`,
`/tmp/ctcompile-sibling-hashes.txt` and `/tmp/ctcompile-sibling-format.log`.
These are transient logs, not required checked-in artifacts.

Skipped: full CTest/compiler lit, broad corpus/native matrix, full Bootstrap,
WPT/test262, Windows, new sanitizer runs and push. Historical sanitizer and
browser compliance results retain their original scope.

## Exact next boundaries

For Symbol source support, `.description` requires distinct undefined/String
transport. The existing native API returns `optional<js_string>`; the DOM nullable
String currently represents null/String and must not silently change meaning.
Then broaden intrinsic exports to useful parameters/helper calls. Registry,
symbol-keyed fields, coercion and custom hooks remain separate proofs. The
historical custom `Symbol.hasInstance` gap is still Node true/one call versus VM
false/zero calls; it was not remeasured here.

For Bootstrap, resume the original B/Data+B constructor-time `e.set(..., this)`
boundary in `HostContract/ClassInitialization/Sources.cpp`. The existing proof
only sinks one direct terminal Map registration from a non-inherited constructor
with the sole proved Map capture. Original registration crosses helpers and
inherited construction. Data retains the receiver through a captured outer
element Map and nested DATA_KEY Map, conditional conflict checks, nullable gets
and delete/empty cleanup. Prove the complete call/Map origins, partial
initialization, exception/reentry behavior and enclosing-owner lifetime. Preserve
the original conflict checks, `e.set`, `e.remove`, `P.off`, configuration and
disposal bodies. This session did not admit or remeasure full Bootstrap.

Deeper sibling relays, String ordering/UTF-16 alignment, collections/document
views and the application driver also remain unfinished.
