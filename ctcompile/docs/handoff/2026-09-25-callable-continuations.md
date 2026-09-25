# Callable continuations and nested-site inventory

Continued clean `ctcompile-v1` at `ed11eedd` from the captured-cell handoff.
The old WIP branch was already an ancestor. No abandoned source edits remained.
Two interruptions killed the initial test/review agents before their edits;
the parent finished the direct callable tests and resumed validation.

## Landed

`366027ba` extends the shared callable-object ordering proof through Try and
Invoke normal/unwind regions. The actual original holder trace contains two
root stores, three method reads and three exact receiver calls. No saved-holder
transport remains at this stage, so no projection or transport waiver was added.
The definition and unique unconditional slot stores must precede the enclosing
region. Complete holder uses, receiver identity and independent callable/effect
proofs remain mandatory. Class-initialization callers retain their separate
alias and slot-before-capture checks.

Thirteen direct controls cover both Try regions, both Invoke outcomes, nested
If reads, late/repeated/nested stores, publication, saved Invoke state, returned
holders, unknown regions, wrong receiver identity, early reads and zero/short
budgets. Analysis preserves the IR. The two unchanged original JavaScript
sources now refuse at closure call arity/source order. No new JavaScript source
or full Bootstrap admission is claimed.

The host gate caught a changed expectation: an unused own-slot read in unwind
is now removable. The existing inert-body proof then removes the invocation.
The exact original fixture moved to the positive cases and receives full DOM
reproof for both providers. A corresponding observed closure/store still
refuses, retaining the original closure and StoreGlobal inside unwind.

`9118c71e` adds an escape regression using the unchanged historical nested-origin
receiver fixture. Contents is complete and inventories both arrays. The fresh
legacy verdict query inventories one root array and omits the nested allocation;
the existing retention harness still refuses and discharges nothing. Production
analysis is unchanged. This pins a prerequisite, not a confinement improvement.

## Focused validation

All remote work held `/tmp/ctbrowser-devbox-build.lock`. Explicit targets were
`ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`, `ctjs-opt`,
`ctjs-translate`, `ctcompile-test-native-reference` and
`ctcompile-test-escape-analysis-arrays`.

- `ctcompile_exception_recovery`: **1/1, 5.41 s** (5.42 s total).
- `ctcompile_host_contract`: **1/1, 2.74 s** (2.75 s total).
- `ctcompile_escape_analysis_arrays`: **1/1, 4.77 s**.
- Selected source differential: `invocation-return`, `write-boolean-snapshot`
  and `helper-next-record`, retaining all original/refusal controls. Both
  providers, printing/optimization modes, GCC/Clang and Node/reference checks
  pass. This was not the entire fixture or compiler lit.
- All **24** generated C++ files pass forbidden-namespace checks; the source
  harness binary checks pass. All **five** final source hashes match the devbox.
- `tools/format.sh --check`: **1127 C++, 159 Python, 114 web files pass**.
  The initial check caught host-test formatting, which was corrected.
  `git diff --check` passes. Independent native static review is clean;
  subsequent formatting changed only whitespace.
- All **nine** original DOMURI raw literals, **28** DOMJSON raw literals and
  **70** Python multiline literal values remain unchanged. The escape fixture
  is reused verbatim. A production comment and host-test whitespace changed
  after native gates; no executable behavior changed and those gates were not
  replayed. The later escape build synchronized the final files.

The initial diagnostic binary invocation lacked its fixture argument and ran
no tests; the exact verbose recovery CTest then passed in 5.31 s and captured
the holder uses. The first production recovery run failed only stale original
boundary expectations. The host failure was diagnosed above. An escape target
typo produced an unknown-target error and ran no tests; the corrected target
and exact CTest passed. No failed or absent result is counted as a pass.

An empty Git index lock left at the first interruption was preserved in the
evidence directory after Linux process/fuser and Windows process checks found
no live Git process. Removal, explicit staging and commit held the Git lock.

Fresh availability checks inspected 11 Linux identities and 350 Windows
Get-CimInstance processes. No Claude match was found, but 60 Linux permission
failures made availability uncertain. Concurrent-agent restrictions applied;
there were no browser/shared implementation edits. Parallel agents traced
escape prerequisites and shared callers; a later agent completed the host
diagnosis/final native review and another completed the escape regression.

Skipped: full CTest/compiler lit, whole native fixtures, broad corpus/matrix
runs, raw escape oracle, full Bootstrap, WPT/test262, Windows/macOS, sanitizers
and performance. No local C++ build, push, history rewrite or other branch edits.

## Next boundary

The measured refusal is `DOM helper call has unsupported arity or source order`
from `DOM/Source/Expansion.cpp`. Holder reads now become original closures;
`DOMSource::precedesInStructuredBody` in `Source/Captures.cpp` still allows Try
body and direct Invoke body calls, but not Invoke continuations or Try catch.
Trace the exact call and its capture assignments before extending that shared
predicate. Review its callback, field and capture-scheduling callers; preserve
initialization dominance, immutable snapshots and all normal/unwind effects.

Nested allocation support still needs a complete site inventory and instance
proof, especially when an allocation repeats. Complete contents alone does not
authorize ownership. Mutable exceptional state, Invoke loop conditions, raw
helper CFGs, escaping owners, broader iterators, unguarded Bootstrap defaults,
the native application driver and full native Bootstrap remain unfinished.

Evidence: `../test-results/2026-09-25-callable-continuations` relative to the
monorepo root, including focused logs, failed attempts, source selector, review,
preservation and remote hash reports, and verified `SHA256SUMS`.
