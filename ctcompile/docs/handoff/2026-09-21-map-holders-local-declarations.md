# Local Map holders and global helper declarations, 2026-09-21 UTC

Continued clean `ab3fe6c6` from its captured-holder/Data handoff. No unmerged
`codex-wip-20260907` or unfinished predecessor code remained. Two later process
interruptions preserved this session's drafts; replacement agents and root
completed them. Startup inspected 71 Linux process identities and 358 Windows
CIM identities, with no Claude executable, Node CLI or loop. No browser or shared
repository file changed.

## Landed

- `ba1edab6` extends captured Map helper normalization to exact fresh local
  callable holders. The existing `analyzeLocalCallableObject` census checks all
  slots and raw uses. Each selected read must retain its original receiver;
  every call follows Map/capture initialization in the owner's entry block.
  Complete helper body, closure identity, symbol-use and budget checks precede
  expansion at the original call positions. Only the selected slot, reads and
  helper retire; unused sibling bodies remain subject to the full source census.
  Existing Map membership and concrete record-owner proofs run unchanged.
- `7660166b` admits local declarations inside exact global intrinsic helpers.
  A charged scan of the original receiver uses selects the existing complete
  local closure proof when those uses are only roots or closure metadata.
  Observable receiver uses retain the restricted direct-helper path. Original
  intrinsic preflight, exact enclosures, capture checks, private expansion and
  final typed reproof remain required. No runtime or native carrier changed.

Parallel agents supplied holder fixtures and the intrinsic change. Independent
design and final concrete reviews found no correctness blocker in the holder
rewrite; root reviewed and reconciled the code and all tests.

## Focused validation

Compilation and final execution ran on the devbox under the shared build lock.
Explicit targets were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract` and `ctcompile-test-native-reference`.
The first build compiled the holder change in three steps; the next fixture sync
compiled the intrinsic change in three steps; the final build relinked the
host-contract test. Exact `ctcompile_host_contract` passed initially in **0.58 s**
and finally in **0.56 s**, total 0.57 s for the last CTest invocation.

Four distinct selected lit cases passed across corrected runs:

| Case under CTNative/ | Result |
| --- | --- |
| Lowering/Objects/class-terminal-publication.test | Passed in the initial **1/2**, 144.26 s selection. |
| Lowering/Objects/class-captured-map-helpers.test | Final PASS, 106.50 s. |
| Exports/native-intrinsic-symbols.test | Final PASS, 203.23 s. |
| Browser/native-dom-symbols.test | Final PASS, 77.26 s. |

The final three-case selection passed **3/3 in 203.24 s**.
The initial captured-holder run passed the new positive/refusal cases, then
reached an outdated expectation: `class-map-record-captured-helper-alias-observer`
now has a complete proof for both its direct and own-holder calls. Its original
file-35 source was kept byte-for-byte and promoted to a native-positive case.
No compiler guard was relaxed to address that test failure.

The holder fixture checks **37 source observations, 96 main native executions,
74 unprepared refusals and 43 preparation refusals**. Four new holder bodies
plus the unchanged former refusal add **40 native executions**. The ten new
refusals cover escaped/replaced holders, extracted methods, receiver observation,
early publication, regions, captured aliases, missing membership and unused
ambient/receiver-observing slots. First-complete budget/rollback checks pass at
904 steps for the new holder, 802 for the free helper and 678 for constructor
helper publication. Existing auxiliary controls add 16 constructed-method
executions/20 refusals and eight original-helper executions/four refusals.

Intrinsic exports check **232 native executions, 221 refusals and two mutations**,
with **37 Node/VM observations**. Four new positive bodies cover local
declarations, primitive argument kinds, Symbol identity/description, loops and
evaluation order. Eight new refusal bodies run in both optimization modes;
four new work-budget controls and a stale nested-body fingerprint control pass.
Adjacent DOM Symbols retain **40 executions, 42 refusals and one mutation**.
Generated programs retain the existing no-Script and subsystem-link checks.

All **seven final code/test SHA-256 hashes** match the devbox. Two C++ and four
Python files pass scoped formatting; syntax, whitespace and original file-30,
file-33 and file-35 preservation checks pass. The fixture agent also checked
the 14 new Node observations locally before the final devbox differential gate.
Required `tools/format.sh --check` retains **16 existing diagnostics** in
untouched `ctbrowser/tools/ctdrive/ctdrive.cpp`, `HostContract/ProviderPaths.h`,
`PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp`; it stops before repository
Python/web formatting.

Some interrupted shell gates completed only their builds. The final build's
heredoc input was consumed by its SSH-based helper, so root ran the missing
CTest/lit commands from a script file with stdin closed. Completed builds and
passing tests were not repeated without a changed dependency or failed case.

Skipped: full CTest/compiler lit, broad corpus/native matrices, full Bootstrap,
WPT/test262, Windows and new sanitizers. No local build or push.
Evidence: `/tmp/ctcompile-holder-locals-{build1,class2,class3,gate4,tests5,format-final,evidence}.log`,
`/tmp/ctcompile-holder-locals-lit5.json` and
`/tmp/ctcompile-holder-locals{,-remote}.sha256`.

## Exact next boundary

Original Bootstrap B/Data+B still refuses at `class own-key snapshot constructor
observes its receiver`. Preserve its complete `e.set`, `e.remove`, `P.off`,
configuration and disposal bodies. This change handles local holder calls after
record construction; it grants no cross-frame or nested-Map ownership authority.

Next compose exact holder capture/dispatch with constructor-time Data publication,
preserving partial initialization, exceptions and reentry. Conditional nested
Map creation, conflict checks, nullable gets and child/empty-parent cleanup then
need every concrete owner and observer proved together. Keep the direct
`nativeMapRecordPayload`/`nativeMapRecordOrigin` assumptions until all inference,
admission, identity and emission consumers support wider origins.

Captured intrinsic helpers, description String-method narrowing, registry/keyed
fields and custom hooks remain separate. String ordering, document views and
the application driver remain unfinished. No full-Bootstrap admission or coverage
gain is claimed.
