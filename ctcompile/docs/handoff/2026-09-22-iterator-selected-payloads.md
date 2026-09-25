# Selected completion payloads and direct mask gaps, 2026-09-22

Resumed clean **ee141a82** and the saved `body-return-branch-expression` source
`2b611964` from HANDOFF/current00 and iteration 71's final journal. All earlier
drafts were committed; `codex-wip-20260907` was not unmerged. Other branches were
preserved. Native regression, read-only completion review and Part 25 work ran
in parallel. The parent finished two historical escape-test promotions after
that agent hit a rate limit.

## Landed

**2618a169** extends completion normalization with a complete use census for each
inactive payload on an exact loop exit. Every use must belong to an unselected
post-loop dispatch arm, including nested uses. A selected-arm, after-region or
external observer still refuses. Existing defined-slot substitution, proof
budgets, operation census and downstream DOM proof remain. Effects stay in their
original arms; no runtime completion representation is introduced.

Raw tests exercise two exclusive Number snapshots and first-exit exhaustion for
both providers. They check saved values before close and reject selected poison,
cross-arm reads, external observers and after-region reads. Two historical
counted/crossed effectful constructions now admit unchanged, retaining both
conditional and loop writes. The source case `ccb433e4` returns **1 normally,
3 on stop, 0 when already exhausted**, while close changes captured state after
the return snapshot. Inspection showed that this case uses captured state rather
than an exit-only local, so **e16b8352** adds the direct loop-local witness
`aa0b8d36`: `const snapshot = count + 2; if (stop) return snapshot;`. It preserves
the same results and close order. All 214 original source bodies and 77 positive
metadata rows survive; the follow-up preserves all 217 sources already committed
in this iteration. The historical nonprogressing source remains compile-only.

**bcf17b3a** enables existing bounded subrange refinement for fresh non-affine
AND/OR/XOR enclosures, including masks without an earlier shift. Complete
reload/store checks, actual-write replay and the shared budget remain. Seven
new source witnesses and seven raw rows per CFG/SCF shape cover gaps, retained
children, snapshots, real overlaps and later stores. All 134 historical source
bodies/checks remain unchanged. Two historical unit-stride AND/OR constructions
now admit with identical source and exact replay expectations.

## Focused validation

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.08 s test / 2.09 s total**.
- Initial four-source native subset: **64 native executions, 372 refusals,
  two nonexecuted admissions and 32 Node/VM observations**. Sources:
  `body-return-branch-number`, `body-return-branch-ordered`,
  `body-return-ordered`, `effect-only-break`, plus all 138 refusal sources.
- New loop-local source alone: **16 native executions, 24 refusal controls,
  zero nonexecuted admissions and eight Node/VM observations**. The already
  passing refusal corpus, compile-only case and host CTest were not replayed.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.13 s test / 2.14 s total**.
- Exact right-shift index-overwrite lit case: **1/1 PASS, 0.14 s**.
- Complete `tools/format.sh --check`: **1124 C++, 157 Python, 114 web files PASS**.

All seven final code/test hashes match the devbox. All 40 generated C++ files
pass Script/VM-protocol scans. The loop-local output uses scalar storage and
public DOM calls; its saved Number is computed before the close write. Initial
source evidence includes 217 syntax checks, 17 exact Node observations and three
mutations; the follow-up adds five exact Node observations and three mutations.
Escape evidence includes 141 parsed/executed functions, seven exact outputs and
three direct-mask traces. Node separately confirms historical unit-stride writes
**0,1,0,1,4,5,4,5** and **2,3,2,3,6,7,6,7**, disjoint from their mask reloads.

The first host run failed six assertions (**2.13 s test / 2.14 s total**): four
new Boolean snapshots joined with Number fillers, and two historical effectful
constructions now admitted. Only new raw snapshot conversions and historical
expectations changed. The first arrays run failed 16 assertions (**2.12 s total**)
in the two newly proved historical unit-stride cases; original construction
expressions were preserved and their exact read/final-state expectations added.
Production did not change after either failure. The original saved source was
initially promoted in the test draft, then restored unchanged when native
preflight exposed the distinct borrowed-element restriction. Its newly created
snapshot twin remains unchanged as a refusal too.

Explicit targets were `ctjs-opt`, `ctjs-translate`, `ctcompile-tool`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. Commands, logs, hashes, Node evidence and generated
C++ are in `../../../../test-results/2026-09-22-iterator-selected-payloads/`.

Full CTest/compiler lit, complete custom execution, broad corpus/native matrices,
full Bootstrap, WPT/test262, browser suites, Windows, sanitizers and local C++
builds were skipped. Nothing was pushed. WSL-root process inspection found
86 readable executable identities with no errors or Claude matches; Windows CIM
found 345 identities without a Claude executable, CLI or loop. No browser/shared
implementation or runtime-oracle changes were made. The idle timer is active/enabled.

## Exact next boundary

`body-return-branch-expression`, SHA-256
`2b611964a4d429490157397bee57f6f9e5692325fdd7d93db54300d514afc19f`, remains
unchanged. The inactive-slot proof now succeeds, but both policies refuse
**DOM entry branch cannot carry a borrowed or callable value**. Its exit-only
payload is a saved DOM element used by the comma expression, not merely a scalar
return result. The DOM control-flow proof currently permits scalar transport;
any extension needs compatible inactive slots and a document/lifetime proof for
the element alias. Do not simply admit arbitrary borrowed/callable kinds or move
the return read across close. No native execution of this boundary is claimed.

Eight Node/VM observations still agree: normal completion true/no close, stop
false/one close writing `true`, stop without advance the same, and already
exhausted false/no close. Snapshot twin `36ff39c8` and multiple-return source
`879dbdf6` remain refusals. Additional completion arms, general body/return
expression throws, crossing-finally cleanup, nested custom opens, unguarded
Bootstrap defaults, literal range-for printing, the application driver and full
native Bootstrap remain unfinished. Difficult index subranges remain bounded
by the shared proof budget.
