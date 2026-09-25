# Ordered continuations and unary loop indices, 2026-09-24 UTC

Resumed clean `c26df86e`, latest153 HANDOFF/Current native work, synchronization
journals and both areas' history. `codex-wip-20260907` is already an ancestor.
Parallel agents preserved original native sources, traced protocol discovery,
completed the interrupted final153 escape review and implemented the next unary
key proof. Rate limits interrupted their turns; the parent continued from
checkpoints. A fresh independent review completed on all five final files.

## Landed

`2ec0f332` preserves ordered normal continuation effects after an independently
nonthrowing attribute call. The entire original function and both continuations
are verified before rewriting. The call moves first, the normal result argument
binds to its actual result, nested normal invocations normalize recursively, and
the original operations and complete successful yield follow in order. Only
the independent existing attribute proof authorizes discarding unwind state.
Normal continuations are outside the original invocation's protection; later
effects and throws retain that meaning. Each operation move is charged, including
repeated movement through nested invocations. Recursion remains bounded at 64.

The shared inert tuple projector is unchanged. The raw regression retains all
three tuple observers and a normal effect, and complete DOM preparation checks
nested reads, two writes using the same Boolean snapshot and an independent
saved return. Wrong receiver, unknown value, unproved global effects and budget
controls retain atomic source/contract refusal. Final DOM analysis still guards
publication from a disposable clone. No emitter or runtime code changed.
This is structural support; no new JavaScript source admission is claimed.

`cb065f5f` uses the existing unary Number snapshot proof and exact array-index
query for certified-loop PropertyKey demand. Plus/Neg can preserve a computed
integral index without granting Number authority to converted bits. Failed
singleton attempts retain the existing conversion/range fallback. The complete
mutation census, refinement, replay, depth and work limits remain; ordinary
scalar and array-length authority is unchanged.

Frozen source SHA-256
`ac6560798908570d01f56f36c6950ede8d325148e1d534f9631b86dc3e53a3a7`,
program `e72ab328ce2ed714`, uses `a[-(keys[i % 2] - 0.25)] = 0` with keys
`[0.25,-1.75]`. Before: child Stored, table Passed, zero of four sites confined.
After: both Confined, two of four total sites, all four observed, zero violations.
Unlimited VM recording observes both confined sites; Node returns `[0,0,0]`.
Saved-child and mutation controls return `{}` and `[0,0,{}]`. All 185 historical
escape source function bodies remain byte-identical; three new functions are
appended. All 34 frozen native artifacts also match their original hashes.

## Exact focused validation

- `ctcompile_exception_recovery`: final **1/1 PASS, 5.33 s**. Initial
  **5.47 s** pass preceded final formatting/hash verification.
- `ctcompile_host_contract`: **1/1 PASS, 2.95 s**, retained without replay.
  Initial recovery/host run took **8.43 s**.
- `ctcompile_escape_analysis_arrays`: **1/1 PASS, 4.91 s**.
  Final arrays/recovery run took **10.25 s**.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS,
  0.21 s**, 357 excluded, using the generated build-tree lit configuration.
- Frozen source before/after claims and unlimited VM recording: **zero violations**.
- Selected `native_dom_caught_node.py` sources `invocation-return` and
  `write-boolean-snapshot`, all refusal controls and original outer controls:
  **32 native executions, 92 refusals, eight Node/VM observations PASS**.
  No whole caught-node fixture pass. Sixteen generated C++ files contain no
  Script/AOT/ctjs namespace; harness binary-symbol checks pass.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**.
  `git diff --check` passes. All five final code/test hashes match the devbox
  and completed independent review.

All builds used locked `tools/remote-build.sh` with explicit targets:
`ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
`ctcompile-test-type-oracle`, `ctjs-opt`, `ctjs-translate` and
`ctcompile-test-native-reference`. No local C++ build ran.

Skipped: full CTest/compiler lit, complete native fixtures, unaffected native
replay, broad corpus/matrices, full Bootstrap, WPT/test262, Windows and sanitizers.
Linux inspection read 13 executable identities with 60 permission failures;
Windows Get-CimInstance returned 342 records. Neither found actual Claude, but
status was uncertain, so concurrent rules remained in force. No browser/runtime/
shared implementation, push or history rewrite occurred.

## Exact next boundary

Original observing getter `1a7fb166` and method `7acf503b` still refuse in
`DOMIteratorClose.cpp` before general recovery consumption.
`DOMCustomIteration.cpp` requires a root-local open and unused, state-free
suppressed close, discovers next/close through direct open-result users, and
threads state only through its admitted structured regions. Recovered calls
instead carry payload and fifteen saved registers through invocation tuples.
Connect those aliases and completions through discovery/state rewriting without
discarding a non-call status edge before its independent effect proof.

Actual next reads once, writes twice and constructs a result record; close
writes then throws the anchor. Preserve exhaustion, cleanup, saved return values
and caught-node identity. The ordered attribute consumer now handles effectful
normal continuations after helper binding; it does not establish that missing
entry/protocol correspondence. Escaping nodes still need exception-lifetime
owners. The unary loop-key witness is complete; no next witness was measured.
General powers, broader iterators, unguarded Bootstrap defaults, the application
driver and full native Bootstrap remain unfinished.

Evidence: `../test-results/2026-09-24-ordered-continuations-unary-indices/` holds
the frozen sources, before/after measurements, native output, focused logs,
final hashes and review. `SHA256SUMS` verifies the preserved files.
