# Observed helper chains and result records, 2026-09-24 UTC

Resumed clean `faffbb7f`, latest154 HANDOFF/Current native work, synchronization
journals and both areas' history. `codex-wip-20260907` is already an ancestor.
No unfinished ctcompile edits remained. Parallel agents preserved the original
sources and traced the entry/protocol boundary. Rate limits interrupted the
initial reviewers and both escape attempts before any escape candidate or
baseline. A replacement independent reviewer completed the final native review.

## Landed

`d661ef8d` extends observed local helper expansion from one attribute call to
bounded straight sequences of attribute calls and fresh own-data records.
Every leaf retains an exact call/exit invocation. Its failure carries the
original caller register snapshot and cloned unwind payload/effects; helper
locals never replace caller saved state. Nested successful continuations keep
source order, so the original read snapshot feeds the first write and result
without rereading after mutation. The helper's own return feeds the original
normal continuation only after all helper calls succeed.

The original normal operations move intact, retaining calls already queued by
helper expansion. Only a call in Invoke's call body is protected; calls in its
normal or unwind continuation take the ordinary inlining path. Unwind calls and
closures that would duplicate the existing call/creation census refuse before
rewriting. Target operands, complete invocation copies/moves and generated
operations are charged; nesting remains bounded. Complete independent attribute
proof still precedes flattening. The existing own-field/use census then forwards
confined result records before final DOM reproof. No emitter or runtime changed.

The new raw regression uses the actual next-method shape: `hasAttribute`, two
ordered `setAttribute` calls and `{done, value}`. It checks all three failure
payloads, saved-state vectors and effects, normal result identity, write/read
snapshot order, complete DOM preparation, and queued normal helper calls.
Wrong receivers, invalid names, extra effects, zero budget and duplicated unwind
calls preserve the original module and contract. Two historical raw extra-call
refusals now prove two distinct protected calls with their bodies unchanged.
Original outer getter/method source bodies and 34 frozen artifacts are unchanged.

This is helper completion support, not a new JavaScript source admission. Original
outer getter `1a7fb166` and method `7acf503b` still refuse. The frozen standalone
helper source `2b755d49` was traced statically to the local checked-call effect
boundary; it has no new compiler/VM baseline or admission measurement.

## Exact focused validation

- Devbox builds through locked `tools/remote-build.sh`, explicit targets:
  `ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`, `ctjs-opt`,
  `ctjs-translate`, `ctcompile-test-native-reference`. No local C++ build.
- `ctcompile_exception_recovery`: final **1/1 PASS, 5.40 s**, **5.41 s total**.
  Earlier corrected passes: **5.34 s** and **5.51 s**. The initial run failed the
  newly exposed result-field forwarding and two now-supported historical
  extra-call expectations; production forwarding and exact expectations were
  corrected before the successful gates.
- `ctcompile_host_contract`: **1/1 PASS, 2.98 s**, retained from the preceding
  candidate gate; the initial **2.89 s** pass also succeeded. The combined
  preceding recovery/host run took **8.50 s**. Final changes after that host gate
  concern Invoke body-region classification and missing-argument work accounting;
  final recovery and selected native checks cover them.
- Selected `native_dom_caught_node.py` sources `invocation-return` and
  `write-boolean-snapshot`, all refusal controls and original outer controls:
  **32 native executions, 92 refusals, eight Node/VM observations PASS**.
  The selection passed before and after the final continuation amendments.
  This is not a whole caught-node fixture or lit pass.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**.
  `git diff --check` passes. Three final code/test hashes match the devbox and
  completed independent review. Sixteen emitted C++ files contain no
  Script/AOT/ctjs namespace; harness binary-symbol checks pass.

Skipped: full CTest/compiler lit, complete native fixtures, unaffected native
replay, broad corpus/matrices, full Bootstrap, WPT/test262, Windows, sanitizers
and escape tests (no escape implementation changed).

Linux inspection read 11 executable identities with 60 permission failures;
Windows Get-CimInstance returned 342 records. Neither found actual Claude, but
status was uncertain, so concurrent rules remained in force. No browser/runtime/
shared implementation, push or history rewrite occurred.

## Exact next boundary

`DOMIteratorClose.cpp` still rejects the original observing entry before general
recovery. Simply bypassing that refusal does not supply the missing proof:
`normalizeDOMCaughtThrow` requires direct independently nonthrowing attribute
calls, while `DOMCustomIteration.cpp` expects a root-local open, direct record
users and state-free suppressed close. Original recovered calls carry payloads
and fifteen saved registers through invocation tuples.

Connect entry recovery, the original protocol aliases and iterator state under
independent non-call effect proof. Then preserve exhaustion, both close sites,
saved returns and caught-node identity through helper expansion. The next helper's
read/two-write/result-record sequence now has an observed completion consumer;
the close helper writes and throws the anchor. Exception-lifetime owners remain
necessary for escaping nodes. Broader iterators, unguarded Bootstrap defaults,
the application driver and full native Bootstrap remain unfinished. The latest
unary loop-key witness is complete; no further escape witness was measured.

Evidence: `../test-results/2026-09-24-observed-helper-chains/` contains 294 files
and a verifying `SHA256SUMS`: original source artifacts, selected native output,
focused logs, final hashes and independent review.
