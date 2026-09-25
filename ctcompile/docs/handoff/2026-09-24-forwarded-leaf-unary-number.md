# Observed DOM leaf forwarding and unary Number snapshots, 2026-09-24 UTC

Resumed clean `355a3033` from the latest HANDOFF, Current native work, journal and
both areas' commits. `codex-wip-20260907` is already an ancestor; no dirty work was
discarded. Three parallel agents preserved original sources, identified the next
escape witness and reviewed the changes. User continuations and agent rate limits
required parent resumption; the final independent nine-file review completed clean.

## Landed

`546281ef` extends the observed helper consumer beyond inert bodies. A helper must
directly return one exact `hasAttribute` or `setAttribute` call; only constants,
proved capture loads, the exact method lookup and frame/root operations may accompany
it. The existing inliner places the leaf inside the original `Invoke`. Its original
normal and unwind continuations, effects and saved registers survive unchanged;
the leaf's actual result replaces the helper's normal result. Verification is
charged. Complete DOM reproof still guards publication and proves the actual
receiver, initial method and arguments after binding.

This is a structural prerequisite, **not new native source admission**. General
observed attribute tuples still fail final DOM proof. The unchanged original outer
getter `1a7fb166` and method `7acf503b` remain refused. No source observation was
removed to manufacture admission.

`bd98d9a0` shares the existing independent Number lookup with unary Plus/Neg.
Signed zero and nonfinite binary64 values retain their identity; String coercions
and converted bits supply no Number authority. The existing 64-operation ceiling,
work charges, singleton refinement, mutation census and replay remain in force.
Arithmetic demand follows varying table operands through unary signs.

Frozen source SHA-256
`4a2f0ab5699aab7928b6ffcbc97db80ceda32cbb5c1b135df3efa4fec39b8e01`,
program `77c88ee532829818`, computes
`a[(-(keys[i % 2] - 0.25) + 3) | 0] = 0`.
Before: child Stored, key table Passed, **zero of four total sites confined**.
After: both **Confined**, **two of four total sites**, zero soundness violations.
Node returns `[0,0,0]`; the VM observes the child confined once with no unresolved
or unchecked instances. The original baseline was retained without replay.
All 176 historical escape function bodies are unchanged; three new functions cover
the original, a saved child and mutation.

## Exact focused validation

- `ctcompile_exception_recovery`: **1/1 PASS**, 5.55 s.
- `ctcompile_host_contract`: **1/1 PASS**, 3.18 s; 8.75 s combined.
- Selected `native_dom_caught_node.py` sources `invocation-return` and
  `write-boolean-snapshot`, plus all refusal controls and both original outer
  sources: **32 native executions, 92 refusals, eight Node/VM observations**.
  A temporary dictionary view selected only source iterations while retaining
  keys needed by refusal generation. **No whole caught-node lit pass is claimed.**
  Sixteen emitted C++ files contain no Script/AOT; harness binary symbol checks pass.
- `ctcompile_escape_analysis_arrays`: final **1/1 PASS**, 4.18 s, 4.19 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS**,
  0.18 s, 357 excluded, using the generated build-tree lit configuration.
- Frozen unary source: before/after escape claims and unlimited VM recording,
  zero violations; Node syntax/result preflight. Source hash unchanged.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**.
  `git diff --check` passes. Nine final code/test hashes match the devbox and
  the completed independent review.

Every build used locked `tools/remote-build.sh` with explicit targets:
`ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`, `ctjs-opt`,
`ctjs-translate`, `ctcompile-tool`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. The initial native build needed the explicit MLIR
Verifier include. The first arrays run passed the new cases and exposed three
old unary refusal expectations plus the old work charge. Those original raw
bodies now have success expectations; the BitOr-only refusal remains. Plus/Neg
charge four steps per result, including their separate Number snapshot, versus
BitNot's three. No production change followed that arrays failure, and no
successful gate was replayed.

Skipped: full CTest/compiler lit, complete caught-node/custom-iterator fixtures,
unaffected native replay, broad corpus/matrices, full Bootstrap, browser WPT/test262,
Windows and sanitizers. No browser/runtime/shared implementation, local C++ build,
push or history rewrite occurred. Linux process inspection read eleven executable
identities with sixty permission failures; Windows Get-CimInstance returned 341
records and no actual Claude match. Status remained uncertain; concurrent rules applied.

## Exact next boundary

`DOMIteratorClose.cpp` still rejects the original observing catch before general
recovery. `DOMCustomIteration.cpp` expects a root-local open and only unused,
state-free suppression; the recovered original has four calls and fifteen saved
registers per call. Connect those recovered tuples to protocol discovery and
helper expansion without dropping non-call status edges without independent effect
proofs. The actual next method reads then writes twice and constructs a result
record; close writes then throws the anchor. The new single-leaf forwarding and
previous inert normal projection do not prove that full protocol. Exhaustion,
cleanup, saved returns and caught-node identity must all survive. Escaping nodes
still require exception-lifetime ownership.

The measured unary escape witness is complete. Start further precision work from
another measured refusal. General powers, broader iterators, unguarded Bootstrap
defaults, the application driver and full native Bootstrap remain unfinished.

Evidence: `../test-results/2026-09-24-forwarded-leaf-unary-number/`, **283 files**
plus a verified `SHA256SUMS`, containing sources/baselines, generated C++, exact
focused logs, final hashes and independent review.
