# Return completion paths and bounded unary chains, 2026-09-23

Resumed clean **9108f946** and exact source `7224b890` from HANDOFF, Current native
work, the latest detail and journal. Recent compiler/browser commits and unmerged
branches were inspected; `codex-wip-20260907` is already an ancestor. Native source
regressions, raw tests/review and escape work ran in disjoint agent tasks. Rate-limit
interruptions resumed saved checkpoints; the parent completed integration and
validation without replaying successful execution gates.

## Landed

**078be297** delays a source-result join when a later branch in the same block
uses the exact same SSA predicate. Existing charged continuation copying maps
that predicate on each path and retains source effects, dominance, the complete
visited-operation census and private publication. A zero-result cleanup branch
may select its literal Boolean condition after both arms receive complete typed
effect proof. A selected throwing arm marks its enclosing path nonreturning;
only inert constants and checked frame exits may follow that branch. Direct
saved throws retain their stricter tail rule, and ordinary scalar joins retain
their prior conservative alternatives. No new runtime carrier is introduced.

The unchanged Number-return/getter-throw source now executes, along with its
method sibling and a return-expression write that must happen before cleanup.
Five modes include both false and true Boolean exhaustion results. Three saved
body-throw controls retain their exceptions, including an object thrown by the
cleanup method. The latter was already admitted at baseline; its historical
refusal expectation is corrected without changing the source.

All 348 historical source constructions, 152 saved cases, 143 general refusals
and 20 prior normal oracle constructions remain. Two new raw method/getter
Boolean-exhaustion controls share the existing payload/frame, both-provider and
incomplete-budget checks. A nonthrowing twin still refuses incompatible live
Boolean/Number alternatives. All 47 historical raw MLIR blocks remain exact.
Independent production review is complete and clean. Final C++ formatting after
review is whitespace-only, with the added/removed token streams checked equal.

**25552339** follows at most 64 original Number Plus/Neg operations, preserving
sign parity and using public Core ToUint32. The constant ceiling matches the
existing loop-proof ceiling; longer traversal needs a charged proof. Strings,
loads and arithmetic cannot borrow literal Number provenance. Property spelling,
mutation and arithmetic keep their separate checks. Raw controls cover 64/65,
odd/even negations, nonliteral origins and mutations.

Frozen source SHA-256:
`10e664f0b8dd59a0e953f92b22cb9140b0ccce289f655f21a1de23e65c220a54`.
Baseline and final bytes match, identifying program `9d254d34be12927c`.
Functions 121–126 change from six stored children to **three confined and three
stored**. Every child is made once with zero unresolved/unchecked observations;
claims agree with the recorder. All 120 historical functions/calls, 120 claim
checks and 144 RECORD lines remain. Six actual-source Node witnesses pass.
The preceding nested-unary implementation's independent review also completed.

## Focused validation

- Normal return cleanup: **48 native executions, 30 Node/VM observations**.
- Saved body exceptions: **48 native executions, 12 Node/VM observations**,
  **20 refusals and four historical admissions**. The initial run completed
  two saved cases (32 executions) before a new test-helper classification failed;
  the successful resumed run selected the remaining saved case and three normal
  cases (64 executions). Completed executions were not replayed.
- Source preflight: **20 baseline checks** (6 admissions/14 refusals),
  **22 final checks** (12 admissions/10 refusals), and **eight next-boundary
  refusals**. Intermediate failing implementation checks were diagnostic runs.
- **15 source syntax checks and 15 local normal-case Node observations**, plus
  the six escape witnesses and their source syntax check.
- `ctcompile_host_contract`: **1/1 PASS**, 2.66 s / 2.67 s total.
- `ctcompile_escape_analysis_arrays`: **1/1 PASS**, 3.23 s / 3.24 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS**,
  0.15 s, 356 excluded, using the generated build-tree lit configuration.
- Final `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**;
  `git diff --check` passes. Formatting-only native C++ changes after the host
  gate were synchronized; no execution replay was needed.
- All **48 generated C++ files** contain no Script namespace. Standalone source
  and linked-symbol checks pass for GCC/Clang, explicit/deduced declarations,
  both native policies and both DOM providers. All eight final code/test hashes
  match the devbox, and all three admitted source hashes match the frozen inputs.

Initial projection overreached into an existing poison control; exact-predicate
mapping was restricted to the repeated-condition case. Initial literal facts
also narrowed a nullable join and relaxed a direct-throw tail; the final proof
restricts literal selection to zero-result branches and structural tails to
nonreturning branches. Those original controls pass unchanged. The raw
nonthrowing control needed its DOM provider set. A new object-close test label
was incorrectly treated as a counter snapshot; its classification was corrected.
A transient wrong-tuple edit was caught by preservation and repaired before the
successful gate. Formatter diagnostics were fixed. No runtime oracle changed.

Explicit devbox targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. All devbox work held the shared build lock and Git
writes held the Git lock with explicit staged paths. No local C++ build, browser
implementation edit, push or history rewrite occurred. Initial process checks
read 14 Linux identities and 343 Windows records, with zero actual Claude
matches and 60 Linux access errors: uncertain, so concurrent-area rules applied.

Skipped: full CTest/compiler lit, complete iterator fixtures, unaffected native
replays, broad corpus/matrices, full Bootstrap, browser WPT/test262, Windows and
sanitizers. These are focused checks, not full-Bootstrap measurements.
Checksum-verified evidence is at
`../test-results/2026-09-23-return-completion-unary-chains/` relative to the repo.
Working checkpoints: `/tmp/ctcompile-native134`, `/tmp/ctcompile-tests134`,
`/tmp/ctcompile-raw134` and `/tmp/ctcompile-escape134`.

## Next boundary

Unchanged `return-mutable-throwing-close`, SHA-256
`8bab7f2405c0207389a47e816d08fb3432f8383ca3edb644b80e76268de3334e`,
refuses **DOM iterator primitive close requires saved-throw suppression** under
both policies. The body increments captured `count` and saves its return value;
cleanup adds ten, writes `data-closed`, then throws Number 2. Preserve return
expression timing, cleanup state and DOM effects, the cleanup exception and
normal exhaustion. Mutable nonliteral and mixed throw/return cleanup have the
same measured restriction. A normal object-throwing getter separately refuses
because its payload is not an owning primitive.

Nested custom iterators, unguarded Bootstrap defaults, the application driver
and full native Bootstrap remain. Escape excludes chains over 64, general
computed Number proofs, unsafe String exponents, Strings over 32 bytes, general
powers and legacy SCF retention. The VM fractional-index discrepancy is separate.
