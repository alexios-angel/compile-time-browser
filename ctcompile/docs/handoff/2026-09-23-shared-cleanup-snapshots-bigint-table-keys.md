# Shared cleanup snapshots and BigInt table keys, 2026-09-23

Resumed clean **e998a46a**, the retained `8f5b1ef8` source and iteration 120's
handoff/journal. No dirty drafts or unmerged `codex-wip-20260907` remained;
unrelated branches were preserved. Source, raw and escape agents worked in
parallel. Repeated rate limits and user continuations interrupted them; the
parent finished from saved candidates, scripts, logs and measured baselines.
Completed compilation and preflight checks were reused.

## Native change

**6f9b4f46** retains completed feeding reads in the existing read set and queries
membership without consuming it. Initial and later protected writes may select
the same result. The existing complete use census authorizes each value operand;
method reuse, leaks, attribute-name uses and incomplete method lookups remain
refused. The unreachable final selection branch was removed. Ordered cloning
keeps one original read and all writes under their source guard; consumed selector
validation and private complete DOM/Style reproof still precede publication.
The original body exception wins over cleanup failures. No runtime/ownership
interface changed. Parent review covered all three `inlineCall` callers and the
private preparation/reproof path; independent review was interrupted before a
completed report, so none is claimed.

The original source SHA-256 is
`8f5b1ef8950d3a15e5f4a607df05523728293f3859585fb0f8b969e09f0ce8da`.
Getter `e18b40d3`, order `6d985204` and cleanup-throw `448e7e71` also execute.
The initial preflight passed 24 checks, then found the proposed cleanup-throw
refusal admitted. That exact source became a fourth execution case. Only its
unchecked optimized policy and two new conditional-write policies were run next:
**28 unique preflight checks**, without replaying the first 24. The final native
fixture passes **64 executions, 88 refusals and 32 Node/VM observations**.

Raw checks retain all **28 historical MLIR literal bodies**. They promote **22
exact historical reuse inputs**, add five shared-read cases, nine structural
controls and three expansion-budget cases. Shared-value identity, guard/order,
read/write counts, complete typed evidence and exact proof budgets are checked.
Source preservation retains **233 historical bodies, 86 metadata rows, 111 saved
cases, 257 retained refusal bodies and 111 oracle constructions**. The original
local checkpoint ran 13 syntax checks and 12 Node observations; after the fourth
case was added, preservation/syntax checks covered 14 sources. Historical oracle
construction was compared without replaying historical executions.

## Escape change

**846522b9** applies existing `ownArrayIndex` to primitive values selected
from invariant tables when they serve as property keys. BigInt keys therefore
use the same existing own-property proof as String/Number keys. Arithmetic
recursion retains its separate Number/conversion requirements. Actual replay
preserves original primitives and child identities. The full mutation census,
receiver reload exclusions, bounded singleton refinement and shared work budgets
are unchanged. Parent review covered CFG/SCF callers, property versus arithmetic
contexts and ordinary replay.

All six baseline child sites were stored (program `e525b2433545e45b`). The same
six bodies appear as functions 43–48 in final program `606884532de70525`: **three
confined, three stored**. BigInt, mixed primitive and nested table keys become
confined. Saved children, mutated tables and mixed BigInt/Number arithmetic stay
stored. Six Node cases pass; derived captures additionally check original child
and array identity, primitive types and own keys. Raw contents tests check exact
read sequences, retained children, bounded keys, receiver gaps and arithmetic/
mutation refusals. All 42 historical source functions and the six baseline bodies
are unchanged. Noncanonical String conversion was investigated only: its shared
root is `Primitives.cpp::boundedConvertedNumber`, with existing callers and tests
across induction, structured and primitive analysis; no parser was added.

## Validation and evidence

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.53 s / 2.54 s total**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 3.02 s / 3.03 s total**.
- Selected `Analysis/Escape/escape-claims/table-index-overwrite.test`:
  **1/1 PASS, 0.13 s, 356 excluded**.
- Final `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
  It ran before the native commit and after the independent escape changes.
  Scoped formatting, scratch shell syntax and `git diff --check` pass.
- Six final code/test hashes match the devbox. All **32 generated native C++
  files** contain no `ctbrowser::script`; the fixture checks standalone and linked
  native output. Native executions were not replayed after the escape change.

Explicit devbox targets were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. Every devbox operation held the shared build lock.
The initial host wrapper completed its build but SSH consumed later stdin lines;
the exact CTest was then invoked separately, without rebuilding. The same issue
skipped a baseline-copy command; its already measured claims were fetched later.
The final escape gate uses a script file, null build stdin and `ssh -n`.

Commands, candidates, logs, source checks, generated C++, claims and hashes are
preserved in `../test-results/2026-09-23-shared-cleanup-snapshots-bigint-table-keys/`
relative to the repo, with `SHA256SUMS`. Working checkpoints are
`/tmp/ctcompile-native121`, `/tmp/ctcompile-tests121`, `/tmp/ctcompile-raw121` and
`/tmp/ctcompile-escape121`.

Initial process inspection covered 17 accessible Linux and 343 Windows processes;
final inspection covered 15 Linux and 346 Windows processes. No actual Claude
executable/CLI/loop matched; 61 initial and 60 final Linux executable-link errors
kept status uncertain. Concurrent-area rules applied. No browser/shared
implementation write, push, history rewrite or local C++ build occurred.

Full CTest/compiler lit, complete iterator fixtures, unaffected native replays,
broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and sanitizers
were skipped. These are focused results, not a full-suite or full-Bootstrap gain.

**Next:** retained `unsupported-selector-guards-second-write`, SHA-256
`c90a81f2a4dd36032c31ae7767970c4e59ddb305ddcc043d6ff0ea455091dccb`, refuses
**DOM protected helper needs an independent inert-body proof** under both policies.
The saved selector controls the second cleanup write; preserve its original
condition, both write positions and original body exception. Broader single-write
cleanup, nonterminal exceptional state, multiple protected regions, implicit
cleanup, nested custom iterators, unguarded Bootstrap defaults, application driver,
full native Bootstrap, general powers and legacy SCF retention remain unfinished.
