# DOM exception ownership and unary table snapshots, 2026-09-23 UTC

Resumed clean `eea1416d` from HANDOFF, Current native work, detail138 and the shared
journal. `codex-wip-20260907` is already an ancestor. Recent compiler/browser/Shell
commits and unmerged branches were inspected; no predecessor changes were discarded.
Source, raw review and escape investigation were delegated. Agents hit rate limits;
the parent resumed their checkpoints, and the raw reviewer later completed both
independent reviews. No completed successful gate was replayed.

## Landed

`ee80c923` gives the original `return-object-getter-close` source, SHA-256
`67bd3ad9a155fcea1312310c573b6d97002c830969050fe3967ee0a153d596ae`, the precise
refusal **DOM thrown element requires an owner that outlives the exception**.
The method twin `f38a8b8a` has the same boundary under both providers and policies.

This is an ownership limit, not a missing primitive carrier. `HostContract.h`
promises the document remains live through the synchronous invocation;
`ctbrowser::element_ref` owns neither the document nor its node. An exception can
outlive destruction of the caller's document or generated session while unwinding.
Whitelisting an element throw would preserve identity bits but permit a dangling
borrow. The existing primitive throw verifier remains unchanged. Element and
nullable-element throws refuse before proof publication. The original raw node
specimen checks the diagnostic under both providers. Existing suppressed node
method/getter sources still discard the cleanup exception, retain the saved Number
1, preserve ordered writes, and skip cleanup on exhaustion.

`b8024348` adds a result-only ToUint32 snapshot to `ContentsValue`. Unary Plus keeps
it and Neg applies unsigned modulo negation; the result retains its own SSA
identity. Reads, containers and successor transport copy the snapshot by value.
A later mutation cannot change a saved conversion. Only `boundedConvertedBits`
consumes it: property keys and arithmetic still need their separate exact proofs.
Loop range proof propagates bitwise demand through the original unary expression.
Table identity, own bounds, singleton refinement and complete mutation census
remain. Unary depth saturates at 65 and admits snapshots through 64 operations;
new facts are charged to the existing work budget.

The frozen seven-function source remains SHA-256
`887fae3bf56d3734163a15b8936d35f1a05959fc5081e9f5cbd040d9a35e76c4`, program
`1997b7ecdba6adcf`, before and after. Child sites 1–4 change from Stored to Confined;
saved-child, mutated-table and original-property sites 5–7 remain Stored. All seven
are observed once with zero unresolved/unchecked instances. Across all 28 sites,
10 are now confined with zero soundness violations. These are this witness's
measurements, not corpus or Bootstrap coverage. The VM truncates the original
fractional property indices and reports site 7 confined; Node retains that child.
The compiler still refuses that precision claim, and no runtime code changed.
All 141 historical functions/calls remain; the seven frozen functions are appended
as 142–148 in the selected lit fixture.

## Focused validation

- Source preflight: **20 provider/policy checks, 8 admissions and 12 refusals**.
  Both node-throwing return sources assert the new lifetime diagnostic.
- Selected native gate: **32 executions, 12 refusals, 8 Node/VM observations**.
  Two existing suppressed-node method/getter sources cover both providers,
  both optimization policies, explicit/deduced output, GCC/Clang, standalone
  compilation and linked-symbol checks. All **16 generated C++ files** contain
  no Script namespace. The executed and formatted Python fixtures have identical
  ASTs. The source checkpoint also retains five syntax checks and four local
  Node observations; they were not replayed.
- Exact `ctcompile_host_contract`: **1/1 PASS**, 2.71 s, 2.72 s total.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS**, 3.37 s, 3.38 s total.
  Saved pre-mutation snapshots, Plus/Neg/String inputs, separate property/arithmetic
  refusals and existing Number/String/Undefined 64/65 tests are covered.
- Selected `Analysis/Escape/escape-claims/table-index-overwrite.test`:
  **1/1 PASS**, 0.17 s, 356 excluded. Same-source standalone baseline/final checks
  also pass. The new next-source probe passes its oracle check and remains Stored.
- `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
  `git diff --check` passes. Nine final code/test hashes match the devbox; the
  remaining LoopProof file differs only by its checked explanatory modulo comment.
  All final files match the independent review hashes.

Targets built through `tools/remote-build.sh`: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-claims`, `ctcompile-test-escape-analysis-arrays` and
`ctcompile-test-type-oracle`. Builds and remote checks held the devbox lock.
Git staging/commits held the Git lock and used explicit paths.

The first arrays gate had 45 failed assertions: six historical Plus-after-read
inputs now admitted (42 assertions), one new expected array ordering, and two
exact work-count expectations for the new conversion fact. Only those expectations
were corrected; source constructors and binary arithmetic refusals remain. The
first lit run placed new calls after an existing deliberate mixed-BigInt throw,
so their entries were zero. Moving those calls before it fixes observation without
changing any function body. The standalone witness and successful arrays/native
checks were not repeated. No production logic changed after the first arrays gate.

Skipped: full CTest/compiler lit, the complete iterator fixture, unaffected native
replays, broad corpus/matrices, full Bootstrap, browser WPT/test262, Windows and
sanitizers. No local C++ build, browser/shared implementation edit, push or history
rewrite occurred. Linux inspection found 11 readable process identities and 60
access errors; Windows Get-CimInstance returned 344 records. Zero actual Claude
matches were found, but the errors made availability uncertain, so concurrent-area
rules applied throughout.

## Next boundaries

Uncaught node exceptions remain refused until an actual caller/owner proof covers
exception lifetime or a catch consumes the node while its owner is still live.
Do not replace this with a raw borrowed throw, document copy, reference-counted
graph or VM fallback. Protected cleanup with a later state observer, nested custom
iterators, unguarded Bootstrap defaults, the application driver and full native
Bootstrap remain separate work.

Escape's exact next witness is `escape/next.js`, SHA-256
`ffcfb029f3e2a15ad11ba23c5983463ece0363b0c35b4269ad3f520b1dc363f5`, program
`822031e8512b11f1`. It uses `(keys[i % 2] - 0) | 0` with the original `[0.9, 2.9]`
table. Its child remains Stored, though Node returns `[0,0,0]` and the VM observes
it confined once with no unresolved/unchecked instance. Arithmetic-result evidence
must be independent of the modulo-only snapshot. Other recorded escape bounds and
the VM fractional-property discrepancy remain. No full-Bootstrap gain is claimed.

Evidence: `../test-results/2026-09-23-node-exception-unary-snapshots/`, relative to
the repo, with SHA256SUMS. Checkpoints: `/tmp/ctcompile-native139`,
`/tmp/ctcompile-tests139`, `/tmp/ctcompile-raw139`, `/tmp/ctcompile-escape139`.
