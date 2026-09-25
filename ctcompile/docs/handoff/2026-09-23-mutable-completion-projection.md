# Mutable cleanup completion projection, 2026-09-23 UTC

Resumed clean `517a0878` and unchanged `mixed-body-throw-return-mutable-close`,
SHA-256 `b705b1fbae55b8a2cfa1a8d201f566e650acf222e1c259c8dcce1528c0113df7`,
from HANDOFF, Current native work, the previous detailed handoff and sync journal.
`codex-wip-20260907` is already an ancestor. No predecessor edits or branches
were discarded. Source, raw regression and escape review work ran in parallel;
their checkpoints survived user interruptions and agent rate limits.

## Landed

`ecdfdfc7` admits the original mutable method and getter (`13d9387b`). Structured
protected cleanup yields its previously evaluated Boolean exception and a literal
completion tag. The next equality selects a region beginning with the saved
throw. The existing close-coverage tag proof now also proves this private-state
continuation. It accepts exact parent results, integer equality/inequality in
either operand order, intervening arithmetic constants and the immediate branch.
No state reader or effect can intervene before the selected throw. The existing
direct saved-throw check, confinement, budget and complete typed DOM proofs remain.

Cleanup receives current captured state. The saved Boolean body exception wins
at protected cleanup; Number 13 from cleanup replaces the normal return. Count
updates, original DOM reads/writes, return snapshots and exhaustion are checked.
Two raw method/getter variants reuse payload, frame, state and budget assertions.
Six negative constructions cover normal/caught outcome observers, state reads
before and after projection, a wrong tag and a missing selected throw, through
both providers. Independent native production/raw review is complete and clean,
bound to the final hashes. All 53 historical raw MLIR literal constructions,
355 historical source tuples, 153 saved sources, 143 general refusals and 36
earlier normal oracles remain unchanged.

## Focused validation

- Selected native source gate: **64 executions, 28 expected refusals and 46
  Node/VM observations**. It selects the two newly admitted mutable sources, the
  prior mixed-break getter and one prior saved mutable source. Coverage includes
  both native policies, explicit/deduced output, both DOM providers, GCC/Clang,
  standalone compilation and linked-symbol checks.
- Exact `ctcompile_host_contract`: **1/1 PASS**, 2.75 s, 2.76 s total.
- `tools/format.sh --check`: **1126 C++, 157 Python and 114 web files PASS**.
  `git diff --check` passes. Executed candidate and committed Python have identical
  ASTs; three final code/test hashes match the devbox. All **32 generated C++
  files** contain no Script namespace.
- Final source preflight: **8 admissions and 8 refusals**, from eight sources
  under both policies. The initial debug build reproduced the original mutable
  refusal and exposed the exact structured continuation. The final preflight
  completed its eight admissions before a new catch-observer source failed import.
  That new source and its getter twin remain checkpoint-only; they are omitted
  from the committed fixture. Raw controls check actual state observers. Only the
  eight remaining policy refusals ran afterward; completed admissions were not
  replayed. Local preparation recorded 10 syntax checks and 21 Node observations,
  including the initial candidate set.

Targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference` and
`ctcompile-test-host-contract`. Builds/tests ran on the devbox under its lock.
Git staging/commits used its lock and explicit paths. No local C++ build,
browser/runtime/shared implementation change, push or history rewrite occurred.
Linux process inspection found 11 readable identities and 61 access errors;
Windows Get-CimInstance returned 345 records. No actual Claude match was found,
but Linux access errors made availability uncertain. Concurrent-area rules held.

Skipped: full CTest/compiler lit, the complete iterator fixture, unaffected native
replays, escape CTest/lit, broad corpus/matrices, full Bootstrap, browser
WPT/test262, Windows and sanitizers. These are focused results.

## Parallel escape review and next proof

The review of `9b83e175` found no correctness defect in literal Undefined unary
conversion or its shared complement, bitwise, table, mask and shift-count
consumers. Original provenance, the 64-edge bound and separate arithmetic/property
authority remain. No escape implementation or repository test changed.

Seven frozen functions in `escape/source.js`, SHA-256
`887fae3bf56d3734163a15b8936d35f1a05959fc5081e9f5cbd040d9a35e76c4`, pass Node
syntax and result checks. They place unary conversion after an original table
read, such as `a[(+keys[i % 2]) | 0] = 0` with fractional Number keys. Saved-child,
mutation and original fractional-property controls accompany the candidates.
These are Node observations only; no compiler baseline or precision improvement
was measured.

The next escape task must first measure that exact source. `LoopProof.cpp` selects
ordinary conversion for unary table operands, while `ArrayContents.cpp` retains
only bounded integral facts after unary reads. Both need a consistent read-time
Number snapshot or equivalent shared proof. Forwarding bitwise use alone is
insufficient; never use modulo bits as arithmetic or property-key Number facts.
The full review, file hashes and runnable witness are in the evidence directory.

## Exact next native boundary

Unchanged `return-object-getter-close`, SHA-256
`67bd3ad9a155fcea1312310c573b6d97002c830969050fe3967ee0a153d596ae`, refuses
**DOM saved throw requires a preceding owning primitive** under both policies.
Its getter writes `data-closed = yes`, then throws the original DOM anchor.
Any extension must prove node identity and document lifetime while preserving
the getter's effects and the exhausted path. No VM/GC fallback is allowed.

Protected cleanup with a later state observer, nested custom iterators,
unguarded Bootstrap defaults, the application driver and full native Bootstrap
remain. Other recorded escape limits and the VM fractional-index discrepancy
remain separate. No full-Bootstrap gain is claimed.

Evidence: `../test-results/2026-09-23-mutable-completion-projection/`, relative to
the repo, with SHA256SUMS. Checkpoints: `/tmp/ctcompile-native138`,
`/tmp/ctcompile-tests138`, `/tmp/ctcompile-raw138` and `/tmp/ctcompile-escape138`.
