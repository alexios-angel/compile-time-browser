# Nested catch dispatch and computed Number addition, 2026-09-24 UTC

Resumed clean `8a9b5b4c` from the latest handoff, Current native work, journal and
both areas' commit history. The previous iteration was committed; the old
`codex-wip-20260907` branch was already an ancestor. Three agents investigated
native source regressions, escape analysis and independent review in parallel.
Rate limits interrupted them. The parent resumed saved source/escape checkpoints;
the review agent resumed and completed native and escape production reviews.

## Landed

`3a376ea3` extends confined local DOM catches through nested completion dispatch.
The original recovered graph carries a six-slot branch result, an integer tag
cast/comparison, and a later normal/throw dispatch. The projection now follows
every original yield with its complete result mapping, then resumes that branch's
original continuation. Literal throwing leaves clone the catch with their own
payload and state; normal leaves retain their original result. Only the exact
integer dispatch operations use upstream constant folding. Source conditions,
comparisons and reads keep their order.

An independent nonthrowing check still covers every original protected arm.
Copies of mappings, continuations and operation subtrees consume the shared work
budget; recursion remains capped at 64. The existing isolated candidate and
complete DOM reproof guard publication. No borrowed exception carrier is added.
Frozen nested state source `fcd90a50` now executes, alongside identity `d2cfb347`
and catch-read `69e86a30`, at all four flag combinations. All 11 previous successful
local bodies and both outer iterator bodies remain byte-identical. Raw controls
cover reversed inner conditions, protected reads, rethrows, borrowed returns and
zero/finite work-budget rollback.

`af3188b4` reuses one private binary64 helper for Add and Sub snapshots. Only
original Number literals or prior independently proved Add/Sub results supply its
operands. LLVM APFloat preserves each intermediate with round-to-nearest,
ties-to-even; the public Core implementation supplies ToUint32. Converted bits
and bounded integer facts never reconstruct a binary64 value. String addition,
property keys and other arithmetic keep their separate proofs. Read-time values
travel through existing container and successor copies; dependent arithmetic
chains remain capped at 64. Both dynamic and static Add charge the new fact.

Independent review found that the loop's unary Neg transfer could keep a prior
sum's positive snapshot after negating its integer range. The fix clears the
binary64 and converted-bit snapshots whenever Neg changes the represented value,
including zero. Raw regressions cover actual 2/0 writes and refusal of negative
property writes. Other tests cover both Add/Sub orders, dynamic/static Add, saved
children, String concatenation, mutation and the 64/65 boundary. All 153 historical
escape source function bodies remain; three new bodies cover Sub/Add, a saved
child and a mutated table.

## Measured focused validation

- `CTNative/Browser/native-dom-caught-node.test`: **1/1 PASS**, 285.58 s,
  357 excluded. **224 native executions, 48 refusals, 38 Node/VM observations**;
  both providers, optimization policies, explicit/deduced output and GCC/Clang.
  All **112 generated C++ files** contain no Script/runtime symbols; the harness
  also checks linked symbols.
- Exact `ctcompile_exception_recovery`: **1/1 PASS**, 4.80 s, 4.81 s total.
  An earlier run passed before adding the new raw controls; it was rerun because
  those tests changed. Native executions were not replayed.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS**, 3.71 s, 3.72 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS**,
  0.17 s, 357 excluded.
- Frozen source `b3eae66b1ddb93268438369071605c7631db8dfc52dbb170a977cb6c7c7abfc9`,
  program `43ee068c775dc0cc`: child function 1/site 4 Stored -> **Confined**;
  key table also confined, **two of four total sites**, zero soundness violations.
  The previous baseline was preserved, not rerun.
- Review witness `1ab8065f6c88046450c0d9e49d20d1ed8095c3c5b2293c39b062e021cd25e8eb`,
  program `df363421614a2c28`: child Stored -> **Confined** after the Neg correction;
  **two of four total sites** confined, zero violations. Its baseline used the
  new Add code before the correction. Both source witnesses return `[0,0,0]` in
  Node; the VM observes each child once, without unresolved/unchecked instances.
- Five native source preflights: **three admitted, two original outer refusals**.
  Source-agent syntax/Node checks cover all four nested flag pairs. Three new
  escape Node witnesses and the two standalone arithmetic witnesses pass.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**;
  `git diff --check` passes. All **ten final code/test hashes** match the devbox.
- Independent native three-file and escape six-file reviews are complete and
  clean. The parent reviewed the final two escape test-only corrections after
  the reviewer's final resumption was rate-limited; those final tests passed.

All builds used locked `tools/remote-build.sh` with explicit targets:
`ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
`ctcompile-test-exception-recovery`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims`, and `ctcompile-test-type-oracle`.
The first arrays run found seven stale assertions for Number Add-zero admission
and a new depth-test construction using a first-match replacement incorrectly.
The second found two new duplicate-SSA parse errors, fourteen stale assertions
for wide/nonfinite Number Add-zero admissions and one changed Add work charge.
Only those test constructions/expectations changed; original source bodies remain.

Skipped: full CTest/compiler lit, the complete iterator fixture, unaffected native
replays, broad corpus/matrices, full Bootstrap, browser WPT/test262, Windows and
sanitizers. No browser/runtime/shared implementation changed, no local C++ build,
push or history rewrite occurred. Linux inspection read 12 executable identities
but encountered 61 permission errors; Windows Get-CimInstance returned 340
records. Neither found Claude identities. Availability remained uncertain, so
concurrent-area rules applied throughout.

## Exact next boundaries

Outer iterator getter
`1a7fb1661163b19674ee92084bc52ded3f96417279039131df31282ca6d813b4`
and method
`7acf503b5734264de1d4d390cfb239a63273cd9052c078c66fd60166bdc2c6d3`
remain refused: **DOM iterator observing catch requires call/check payload and
state proof**. Model the original open/next/close call family and every removed
status edge, retaining failure payloads, complete pre-call state, independent
effects, cleanup writes and exhaustion. Uncaught borrowed node exceptions still
require an owner that outlives the exception. Do not widen the synchronous owner
contract or unobserved suppression proof to admit them.

Escape source
`1d605beafb439db9d8144e1672a7cc1b9c354126f4beec42b168f843970de354`, program
`ccf117847c4cd802`, uses `((keys[i % 2] - 0.25 + 0.25) * 1) | 0`.
The child remains Stored; Node returns `[0,0,0]`, and the VM observes it confined
once with zero unresolved/unchecked instances and zero soundness violations.
Multiplication needs separate computed Number evidence. Converted bits remain
insufficient; the earlier fractional-property Node/VM discrepancy stays separate.

Protected observers, broader/nested iterators, unguarded Bootstrap defaults,
the application driver and full native Bootstrap remain unfinished. These focused
results establish no full-Bootstrap admission or coverage gain.

Evidence: `../test-results/2026-09-24-nested-catch-number-addition/`, including
source checkpoints, original baselines, focused logs, generated sources, review
patches/hashes and a checksum manifest. No executable artifacts are retained.
