# Captured-cell continuation ordering

Continued clean `ctcompile-v1` at `2979bd1f`, following its recorded captured-cell
boundary. The old WIP branch was already an ancestor; there were no abandoned
edits. This iteration was interrupted after validation, then resumed to verify
the saved results and commit without repeating completed gates.

## Landed behavior

`3f2024e7` extends `DOMSource::resolveCell` only for immutable Root/CellGet uses
inside Invoke normal/unwind continuations. It still requires the cell and any
single assignment in the source block before the entire enclosing operation.
The complete use census, immutable capture target, callable scheduling and
cleanup rules are unchanged. Unknown regions, continuation-local writes,
publication and unresolved cell-handle transport still refuse.

Full preparation already uses `forwardRecoveredCaptures` to follow exact saved
slots and equal active cell origins, check dominance and remove dead transport.
The implicit thrown payload is distinct from saved cell state. No second alias
mechanism or runtime representation was added.

Eleven direct resolver controls check normal/unwind snapshots and retained
effects; a preceding assignment; late, nested and repeated writes; publication;
saved-cell transport; unsupported region ancestry; nested If reads; and zero/
one-short budgets. Failed proofs retain every read and publish no capture map.
Both original structured getter/method sources now reach the later callable-object
ordering refusal. Their full preparation still rolls back. No new native
JavaScript source or full Bootstrap admission is claimed.

## Focused validation

All builds and remote checks held `/tmp/ctbrowser-devbox-build.lock`. Explicit
targets: `ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`,
`ctjs-opt`, `ctjs-translate` and `ctcompile-test-native-reference`.

- Exact `ctcompile_exception_recovery`: **1/1, 5.33 s** (5.34 s total).
- Exact `ctcompile_host_contract`: **1/1, 2.72 s** (2.74 s total).
- Selected source differential: `invocation-return`, `write-boolean-snapshot`
  and `helper-next-record`, retaining every existing refusal and original outer
  control. Provider/printing/optimization variants, GCC/Clang and Node/reference
  comparisons pass. This was not the whole fixture or a compiler lit replay.
- All **24 generated C++** files pass forbidden-namespace checks; the harness
  binary checks pass. All **three** final source hashes match the devbox.
- `tools/format.sh --check`: **1127 C++, 159 Python, 114 web files pass**.
  `git diff --check` passes. Final independent three-file static review is clean.
- All **eight** original embedded C++ raw literals and **70** Python multiline
  literals are unchanged; the direct cell test adds one separate IR specimen.

The first production gate exposed the two stale original-source diagnostics.
The new test's first build caught a BlockArgument/TypedValue ternary mismatch;
an explicit `mlir::Value` fixes it. Initial static review missed that compile
error; final review covered the correction. A first build command consumed the
following shell input, so its log was treated as build-only and both CTests were
then explicitly invoked. No absent test result was counted as a pass.

Skipped: full CTest/compiler lit, whole native fixtures, broad corpus/matrix
runs, raw escape oracle, full Bootstrap, WPT/test262, Windows/macOS, sanitizers
and performance. No browser/shared implementation, local C++ build, push,
history rewrite or other branch changes occurred.

Fresh process checks inspected 11 Linux identities and 343 Windows
Get-CimInstance records with no Claude matches. Sixty Linux permission failures
left availability uncertain, so concurrent-agent restrictions applied. Three
agents investigated escape origins, test design and native source semantics.
The first two hit rate limits before edits; the third completed the bounded
final review. Escape analysis is unchanged, with no new precision measurement.

## Exact next work

The measured refusal is `DOM helper object has nonlocal or unordered uses`,
from `Analysis/ClosedCallable.cpp`, called by `DOMSource::resolveMethods` in
`DOM/Source/Entry.cpp`. Inspect the actual failing holder operands before editing:
the order check runs before the use-shape census and can hide either a method
read/call crossing Try/Invoke ancestry or retained `InvokeExit` holder state.

The shared analyzer currently follows If/While and direct protected calls,
but neither Try nor Invoke continuations. It still requires unique unconditional
own callable stores, preceding slot initialization and exact receiver identity.
For saved transport, reuse the existing observed-protocol lifetime proof and
the analyzer's projected-use callback, or prove the transport dead before
removing it. Region ancestry alone is insufficient authority.

Review all shared callers in class initialization Sources, Publication and
CapturedMapHelpers. Preserve saved/caught/later holder observations, replacement
slot and read-before-initialization refusals. After that boundary,
`precedesInStructuredBody` may independently refuse closure scheduling inside
Invoke continuations; measure it before extending its wider caller contract.

The parallel escape trace confirmed that `computeVerdicts` inventories root
sites while structured contents can describe nested allocations. Supporting
nested origins still needs a complete site and instance-identity proof,
especially for repeated allocations. Do not remove that prerequisite using
contents facts alone.

Mutable exceptional state, Invoke loop conditions, raw helper CFG normalization,
escaping owners, broader iterators, unguarded Bootstrap defaults, the native
application driver and full native Bootstrap remain unfinished.

Evidence: `../test-results/2026-09-25-captured-cell-continuations` relative to the
monorepo root, with focused logs (including failures), selector, preservation
and remote hash reports, independent review and verified `SHA256SUMS`.
