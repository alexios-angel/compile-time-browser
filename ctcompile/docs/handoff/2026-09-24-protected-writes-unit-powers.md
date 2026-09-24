# Protected DOM mutations and computed unit powers, 2026-09-24 UTC

Resumed clean `41021e75` from the latest HANDOFF, Current native work, journal and both areas'
recent commits. The old `codex-wip-20260907` is already an ancestor. There was no dirty or unmerged
predecessor implementation to land. Continued the unfinished observing-iterator completion boundary
and the frozen escape power witness. Three agents independently investigated source controls, escape
proof and review; rate limits required parent resumption from their checkpoints.

## Landed

`e9bb1abc` extends the existing local catch completion consumer to protected `setAttribute` calls.
The original contract must prove the Element and exact method/receiver; both arguments must be
literal Strings. The public ctbrowser attribute-name validator proves validation cannot throw before
mutation, and its byte scan is charged to the existing budget. No browser behavior is copied or
changed.

Recovery can forward an unchanged Element through a branch result. Only the exact same SSA value in
both yields supplies that identity, through at most 64 joins. Both branch effects remain in place.
Differing origins, invalid names, coercion, missing contracts and insufficient budget still refuse
transactionally. Complete DOM reproof follows the existing normal-completion projection, which
preserves write order, caught-node identity and successful saved state. No borrowed node becomes an
escaping C++ exception.

The source gate adds four unchanged frozen witnesses: write followed by caught read, normal return,
conditional mutation and saved state. Native document write logs check the selected mutation and the
absence of writes on the skipped arm; Node/VM checks include read/write counts. All 23 previous
local sources and both original outer iterator sources are byte-identical.

`b818ceed` retains independent binary64 Number snapshots through `x ** 1`, copying the Number
directly rather than approximating power. The exponent must itself be independently proved Number
one. Negative zero and nonfinite Number values remain Number evidence; converted bits cannot supply
general arithmetic or property-key authority. The existing 64-operation bound and replay charge
remain. All 165 historical escape function bodies are unchanged; three new functions cover ordinary,
saved-child and mutated-table behavior.

## Measured focused validation

- Exact `ctcompile_exception_recovery`: **1/1 PASS**, 5.01 s, 5.02 s total.
- `CTNative/Browser/native-dom-caught-node.test` and targeted continuations: **432 native
  executions, 76 refusals, 66 Node/VM observations**, completed across the original run and targeted
  continuations; **no whole native-lit pass is claimed**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS**, 4.07 s, 4.08 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS**, 0.18 s, 357 excluded.
- Frozen source **1e33691869b9a209c461a34f1324a3b418c61dc9ce5a7025811695af907a4d79**, program
  **a30ea12594e7fddf**: child Stored -> **Confined**, key table Passed -> **Confined**, **two of
  four total sites**, zero violations. The original measured baseline was retained without replay.
  Node returns `[0,0,0]`; the VM observes the child once, confined, with zero unresolved or
  unchecked instances.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**. `git diff --check`
  passes. Final nine-file independent review is complete and clean; all nine final code/test hashes
  match the devbox and reviewed files. All **216 emitted C++ files** contain no Script/AOT symbols.

All builds used locked `tools/remote-build.sh` with explicit targets:
`ctcompile-test-exception-recovery`, `ctjs-opt`, `ctjs-translate`, `ctcompile-tool`,
`ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`.

The first recovery run exposed the unchanged-Element branch result described above. One compile
error in the fix used an MLIR TypedValue local where the traversal needs `mlir::Value`; corrected
before the passing recovery run. The first format check found whitespace in the new escape test; the
repo formatter corrected it. Passing escape tests were not replayed after formatting. The native
selected lit completed all 368 historical executions and 54 observations, then its first new write
trace exposed the reference printer's percent-encoding of colons (474.59 s). The comparator now uses
the same reference-only decoding as the existing outer checker. A targeted continuation completed
all 64 new executions and eight observations, then its temporary source-selection wrapper hid a
historical key required to construct a refusal. The corrected remaining-only wrapper retained the
full source mapping and completed 76 refusals plus four outer observations. No successful native
executions were replayed, and the final repository harness was unchanged by that temporary-wrapper
fix. An empty stale Git index lock, dated 10:25:46 UTC, was removed under the repository flock only
after Linux `ps`/`fuser` and Windows process inspection found no Git owner.

Skipped: full CTest/compiler lit, the separate complete custom-iterator fixture, unaffected native
replay, broad corpus/matrices, full Bootstrap, browser WPT/test262, Windows and sanitizers. No
browser/runtime/shared implementation, local C++ build, push or history rewrite occurred. Linux
inspection found twelve readable executable identities and sixty access errors; Windows
Get-CimInstance returned 341 records with no actual Claude match. Status remained uncertain and
concurrent-area rules applied.

## Exact next boundaries

Original observing outer getter **1a7fb166** and method **7acf503b** remain refused. Recovery
already retains their four protocol calls, original payloads and complete saved registers. The
remaining work is the coordinated consumer for recovered open/next/close tuples: protocol discovery
currently requires a root-local open and state-free suppressed close, and expansion/final DOM proof
need payload/state, cleanup, exhaustion and saved-return correspondence. Every discarded non-call
status edge needs an independent effect proof. Protected mutations now have that proof for the
literal String/direct Element slice; the original iterator itself is not newly admitted. Escaping
nodes still need exception-lifetime ownership.

Escape source **97308b398ba5f6f405d2ec130e0bb82ac2e594d6e1d2d7796796a61737af0f6b**, program
**fa9803402523b206**, uses the same computed remainder raised to zero, with the child at array index
one. The child remains Stored and key table Passed: zero of four sites statically confined, zero
violations. Node returns `[0,0,0]`; the VM observes the child once, confined, with zero
unresolved/unchecked instances. The next proof is the Number zero-exponent identity over
independently computed Number inputs; converted bits still cannot establish it.

Broader iterators/protected observers, unguarded Bootstrap defaults, the application driver and full
native Bootstrap remain unfinished. No full-Bootstrap admission or coverage gain is claimed.

Evidence: `../test-results/2026-09-24-protected-writes-unit-powers/`, including frozen sources and
baseline, focused logs, generated C++ (no executables), final hashes, independent review and
checksum manifest.
