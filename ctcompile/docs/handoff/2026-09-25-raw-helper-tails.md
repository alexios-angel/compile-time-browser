# Raw iterator helper tails and conditional frame confinement

Resumed clean `ctcompile-v1` at `dcf9ce9d` from the closure-scheduling handoff,
recent journals and both commit histories. The old WIP branch was already an
ancestor. An interruption stopped the explicit test build after two objects;
the incremental build resumed. The parent completed the preserved test draft
and escape candidate after their agents stopped. No work was discarded.

## Landed

`395b4a75` reuses `mlir::eraseUnreachableBlocks` for resolved iterator helpers.
The raw identity, next and close bodies each have one live block followed by an
unreachable implicit return. Only those dead blocks are removed. The existing
terminal-throw, closed-identity, capture, state, lifetime and complete DOM proofs
remain required. Lookup is named `prepareTarget` to expose its mutation of the
private candidate. Its bounded census includes operations, operands, results,
blocks, arguments and successor edges, then reserves a quadratic work ceiling.

Both original getter and method witnesses now prepare from raw import without
changing their JavaScript. Their three retained invocations keep 15 saved slots
each and the original caught-anchor identity. Ten direct controls cover dead
versus live effects, three reachable CFG variants, wrong receiver and payload,
invalid attribute name, oversized dead input and a 400-case switch whose edges
carry no operands. Preparation refusal preserves the module and fingerprint.

`10d875dc` permits the independent retention proof to discharge a nested
UnknownOp inventory entry only with one root block and exclusively `scf.if`
ancestors. A site therefore has at most one instance per call. Complete current
contents, initialized region inputs, all-write acyclicity and every return's
reachable sites still decide confinement. Sixteen controls include selected
post-region identities, returned/saved children, overwrite, publication,
transient cycles, missing/dead lattices, repeated instances, raw-frame retention
and unsupported regions. Two historical fixture bodies are unchanged.
This proves frame confinement only, with no ownership or native admission gain.

## Focused validation

All builds and remote checks held `/tmp/ctbrowser-devbox-build.lock`; no local
C++ build ran. Explicit targets were `ctcompile-test-exception-recovery`,
`ctcompile-test-host-contract`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-analysis-completion`, `ctjs-opt`, `ctjs-translate` and
`ctcompile-test-native-reference`.

- Final `ctcompile_exception_recovery`: **1/1, 5.46 s** (5.47 s total).
- Final `ctcompile_host_contract`: **1/1, 2.82 s** (2.83 s total).
- `ctcompile_escape_analysis_completion`: **1/1, 0.01 s** (0.02 s total).
- `ctcompile_escape_analysis_arrays`: **1/1, 4.76 s** (4.77 s total).
- `outer-selection.py`: only the two unchanged original getter/method sources,
  including new raw-import execution and zero-budget checks. Native assertions
  cover exhausted/active iterators, visited state, caught identity and ordered
  writes on two documents. Structured checks retain both providers and
  optimization modes; raw checks use the document provider without optimization.
  Both printers and GCC/Clang run throughout. Node and VM source traces agree.
- **20** generated C++ files pass the no-Script/AOT/ctjs namespace check;
  their **40** binaries pass the harness's execution and symbol checks.
  Source selection preceded only the final refusal-path guard/accounting fixes;
  both affected CTests ran again afterward.
- All **eight** final implementation/test hashes match the devbox.
  `tools/format.sh --check`: **1127 C++, 159 Python, 114 web files pass**.
  Subsequent edits pass scoped clang-format/Black and `git diff --check`.
  All **10** original C++ raw literals and **71** Python newline-bearing AST
  String values are unchanged.

The first recovery run failed four obsolete raw-refusal expectations because
preparation now succeeded. The resumed test draft initially failed compilation
on `ConstantOp::setValue`; it now uses `setValueAttr`. A queued stale-test run
was cancelled before execution. Final native review found an unchecked
budgeted lookup and missing successor-edge accounting. Both were fixed; the
switch control pins the latter. The reviewer reported no remaining concrete
defect in its final message, then hit a rate limit before writing its report.
The parent preserved that review record. Existing stateful budget controls run,
but no new test targets the exact failed-lookup cutoff.

Fresh process checks inspected 11 Linux identities and 348 Windows processes,
with no Claude identity match. Sixty Linux permission failures made availability
uncertain; concurrent-agent rules applied. No browser/runtime/shared
implementation changed. No push, history rewrite or other branch edits.

Skipped: full CTest/compiler lit, whole native fixtures, broad corpora/matrices,
raw escape oracle, full Bootstrap, WPT/test262, Windows/macOS, sanitizers and
performance. These focused results are not a full-suite pass.

## Next boundary

Raw reachable helper CFGs remain refused: the new identity, next and getter
controls introduce a live continuation without changing the payload and still
hit the closed-identity, exact-local-capture or terminal-throw checks. Normalize
those edges using the existing upstream CFG utilities, then reprove every
source effect, capture and completion. Do not turn dead-tail pruning into
permission to bypass live CFG proof.

Nested allocation instances under loops or multiple root blocks still need
instance and lifetime correspondence. Frame confinement does not justify
region-local destruction or unique ownership. Mutable exceptional state,
escaping owners, unguarded Bootstrap defaults, the application driver and full
native Bootstrap remain unfinished.

Evidence: `../test-results/2026-09-25-raw-helper-tails` relative to the monorepo,
with focused logs, selector, generated text artifacts, source hashes,
preservation report, review record and `SHA256SUMS`.
