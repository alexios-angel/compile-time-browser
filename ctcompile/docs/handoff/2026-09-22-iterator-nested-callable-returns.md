# Nested callable returns and exact mixed shift gaps, 2026-09-22

Continued clean **94371e90** and the exact saved `bd1a8aa5` boundary from
HANDOFF/current00 and the iteration 66 journal. Earlier interrupted work was
already committed; `codex-wip-20260907` was not unmerged. Other branches were
preserved. A new interruption preserved four drafts; replacement agents resumed
those files and their artifacts. Later rate-limit interruptions left complete
escape evidence in the journal, which was checked and reused.

## Landed

**4eec52db** composes the existing explicit argument-return summary through fixed
helper calls. An immutable formal supplies each summary; an unseeded cycle cannot
invent one. Every invocation still passes exact arity, direct-target, receiver,
recursion, effect and observer checks before expansion. Appending capture
parameters preserves the original formal indexes, and inlining retains the
original effect order and scalar argument snapshots.

Both symbolic-use scopes are queried once for the complete helper family. Each
use still resolves to its exact helper and must appear in that helper's proved
call list. Both unknown-use checks and the charge before query allocation remain.
No new callable representation or runtime dependency is emitted.

The unchanged saved source is SHA-256
`bd1a8aa59364be2280050cad8430963ba5db2070260e8eed96107435ab5f9291`.
It and the zero-trip source return **2729 normally / 3603 on stop**. The nested
effect/snapshot source returns **886698 / 980596**. All actually traverse the
iterator, with `data-closed=false`. Omitting the nested effect changes the snapshot
result to **181308 / 211052**; erroneously executing one zero-trip call changes
that witness to **7287 / 8857**. Ordinary/direct raw twins retain all five state
transport assertions and hostile argument, observer and target controls.
All 183 historical native source bodies, 64 positive metadata rows and the
historical raw construction prefix remain unchanged. The nonprogressing historical
source remains compile-only and was never executed.

**3ec40a4f** refines conservative overlap for mixed rounded shifts using the
existing index transfer at each exact induction visit, spending the shared budget.
The complete store/reload census still precedes refinement, and actual-write
replay determines which children survive. No union range domain was introduced.
New signed/unsigned witnesses write **0, 2, 5**, preserving their count at index 1;
a finite actual-overlap control writes **0, 2, 4**, changing its count. Saved
child snapshots and untouched children remain escaping. All 101 historical
right-shift source bodies and their call tails remain unchanged. The older
`unevenReloadGap` body now proves confined: writes **0, 1, 3** leave its count
at index 2 unchanged and clear every stored child.

## Focused validation

| Check | Result |
| --- | --- |
| Three new native sources, both optimization policies | All admitted |
| Four-source subset plus all 123 refusal sources | 64 native executions, 342 refusals, two nonexecuted admissions, 32 Node/VM observations |
| Exact `ctcompile_host_contract` | 1/1 PASS, 2.09 s test / 2.10 s total |
| Exact `ctcompile_escape_analysis_arrays` | 1/1 PASS, 1.96 s test / 1.97 s total |
| Exact right-shift index-overwrite lit case | 1/1 PASS, 0.13 s |
| Complete formatter | PASS: 1124 C++, 157 Python, 114 web files |

The source subset contains the three new forwarded-call positives and the prior
`entry-captured-sibling-returned-loop-call-snapshots` witness. The complete custom
iteration execution case was not run. All seven final code/test hashes match the
devbox; all 32 generated C++ files pass Script/VM symbol checks. The nested snapshot
output was inspected: ordinary scalars and branches call the existing public DOM
helpers. Local source checks cover 191 distinct syntax programs and 16 exact Node
traces; escape evidence covers 105 functions and the focused exact states above.

The first native build failed because a deduced typed MLIR value could not accept
a general value; the variable now explicitly uses `mlir::Value`. Initial admission
then reached the shared 100,000-step budget. Temporary markers measured 30 steps
for summary composition and 18,640 for repeated symbolic-use scans. The shared
census removes repeated work; no budget was raised and no saved source changed.
Markers were removed before the successful gates. An independent read-only proof
review found no blocker in either the summary or census change.

The first array gate failed eight assertions in two additional historical gap
refusals newly proved by refinement (2.15 s total). Their exact constructions and
the corresponding source body at case 49 were preserved; expected states were
independently checked with Node and promoted. Production did not change after that
failure. The final array/lit gate above passes.

Explicit devbox targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-tool`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. Exact CTests used anchored names and
`--output-on-failure --no-tests=error`. Lit used the generated build configuration
and filter `^ctcompile :: Analysis/Escape/escape-claims/right-shift-index-overwrite[.]test$`.
All remote builds/tests and Git mutations used their shared locks. Exact scripts,
logs, source evidence, hashes and generated C++ are in
`../../../../test-results/2026-09-22-iterator-nested-callable-returns/`.

Full CTest/compiler lit, complete custom iteration execution, unchanged DOM cases,
broad corpus/native matrices, WPT/test262, Windows, additional sanitizers and local
C++ builds were skipped. Nothing was pushed. No full Bootstrap coverage gain is
claimed. Linux inspection found 74 readable identities, no errors or Claude match;
Windows CIM found 350 identities, only six standard WSL sessions among candidates,
and no Claude executable/Node CLI/loop. Claude was confirmed stopped; no browser,
shared implementation or runtime-oracle edits were made. Idle timer active/enabled.

## Exact next boundary

`entry-captured-sibling-returned-loop-forwarded-branch`, SHA-256
`191c2e50f8b59be95494f2a5e58b90db445408502218f847c903937428d3fe98`, uses:

```js
const forward = (writer) => {
    let selected = writer;
    if (emitted > 0) selected = other;
    return selected;
};
const keep = (writer) => forward(writer);
```

Node returns **2729 normally / 3603 on stop**, `data-closed=false`. Both native
policies return status 1, `DOM iterator callable branch has an unproved arm`;
both diagnostics are saved. No native boundary execution was claimed. Prove
branch-dependent return summaries through loop backedges while retaining complete
effects, snapshots and observers. Nested custom opens, abrupt close, unguarded
Bootstrap defaults, literal range-for printing and the application driver remain.
Full native Bootstrap is unfinished. Larger mixed shift gaps remain limited by the
existing proof budget; inverse transfer is the documented upgrade path.
