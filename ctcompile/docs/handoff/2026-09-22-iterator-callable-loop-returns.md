# Callable loop returns and unsigned shift residues, 2026-09-22

Continued clean **f99f5aea** and the saved `keep(selected)` backedge source from
the latest handoff and iteration 65 journal. Earlier interrupted work was already
committed; `codex-wip-20260907` was merged. An interruption during this iteration
preserved seven drafts and their artifacts; replacement agents resumed them.

## Landed

**c2cb26fa** records a helper's explicit argument index when its single root
return returns that argument. The existing callable traversal follows the
corresponding actuals around loop backedges, including every possible target.
This is an immutable argument snapshot even when the helper writes captured state.
The complete call, arity, direct-target, effect, observer and recursion proofs
still run before expansion. Unknown leaves remain refusals; cycles and each
dependency consume the existing budget. The explicit argument index remains valid
when capture parameters are appended. No new callable object or runtime is emitted.

The saved `entry-captured-sibling-returned-loop-call-result` is byte-identical,
SHA-256 `580dfa93aaf660d6d6abd5b7374cb2a729770863d440b3cc5c52f68d7bf046c1`.
It and the zero-trip witness return **2729 normally / 3603 on stop**. The final
effect/snapshot witness returns **886698 / 980596**. All execute the iterator;
`data-closed=false`. Dropping its helper effect changes the normal result to
181308; passing the wrong scalar actual changes it to 134042. Raw ordinary/direct
zero/two-trip twins retain the existing five-result transport assertions and
complete hostile incoming/observer controls. All 173 historical source bodies,
61 positive metadata rows and historical raw constructions remain unchanged.
The nonprogressing historical source stays compile-only and was never executed.

**3e06bd9a** extends the existing converted residue lattice to unsigned bounds
`[0, 2^32-1]`. Across a ToUint32 jump, `gcd(stride, 2^32)` retains the invariant
residue before the existing right-shift transfer. Complete reload/store checks
and actual-write replay retain unwritten children and earlier snapshots. The
previous unsigned refusal now admits with its original body. Eight new source
functions preserve all 93 historical functions; ascending/reversed witnesses
write **3, 1, 3** and **1, 3, 1**. No new range domain was added.

## Focused validation

| Check | Result |
| --- | --- |
| Three new native sources, both optimization policies | All admitted |
| Four-source subset plus all 118 refusal sources | 64 native executions, 332 refusals, two nonexecuted admissions, 32 Node/VM observations |
| Exact `ctcompile_host_contract` | 1/1 PASS, 1.96 s test / 1.98 s total |
| Exact `ctcompile_escape_analysis_arrays` | 1/1 PASS, 2.17 s test / 2.19 s total |
| Exact right-shift index-overwrite lit case | 1/1 PASS, 0.14 s |
| Complete formatter | PASS: 1124 C++, 157 Python, 114 web files |

The source subset comprises the three new positives and the previous loop
snapshot source, using borrowed/owned DOM, both optimization policies,
explicit/deduced C++ and GCC/Clang. All seven final code/test hashes match the
devbox. All 32 generated C++ files passed Script-symbol checks; the snapshot
output was inspected and uses scalar state and public DOM calls. The devbox idle
timer is active/enabled. Local checks cover 183 native source syntax checks and
16 exact Node traces; the fixture contains 64 positives, 118 refusals and one
compile-only source. Escape artifacts record 101 functions, nine exact outputs
and 8,233,344 sampled lattice-membership checks.

The first exact host run failed in 1.98 s: new nested wrong-target replacements
produced empty modules, causing four false-success assertions, and a direct call
with an omitted argument failed parsing. The corrected controls choose the
applicable rewrite, guard against empty/unchanged results, and preserve the direct
call's undefined padding. Production and historical fixture bodies did not change.
A new snapshot initially skipped the iterator through unconditional state growth;
a conditional revision exceeded the existing budget. Passing the emitted-state
snapshot to the unconditional effect preserves iterator execution and admits
without changing the budget. The saved historical source never changed.

Explicit devbox targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-tool`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. Exact CTests used anchored names and
`--output-on-failure --no-tests=error`. Lit used the generated build configuration
and filter `^ctcompile :: Analysis/Escape/escape-claims/right-shift-index-overwrite[.]test$`.
Every remote/Git operation used its shared lock. Commands, logs, hashes, generated
C++ and observations are in
`../../../../test-results/2026-09-22-iterator-callable-loop-returns/`.

Full CTest/compiler lit, complete custom iteration execution, unchanged DOM
cases, broad corpus/native matrices, WPT/test262, Windows, additional sanitizers
and local C++ builds were skipped. No full Bootstrap coverage gain is claimed.
Linux inspection found 74 readable process identities and no errors; Windows CIM
found 352 identities, with no Claude executable/CLI/loop on either platform.
Claude was confirmed stopped; no browser/shared implementation or oracle changed.

## Exact next boundary

`entry-captured-sibling-returned-loop-call-forwarded`, SHA-256
`bd1a8aa59364be2280050cad8430963ba5db2070260e8eed96107435ab5f9291`, replaces
the saved helper with:

```js
const forward = (writer) => writer;
const keep = (writer) => forward(writer);
```

Node returns **2729 normally / 3603 on stop**, `data-closed=false`. Both native
policies return status 1, `DOM iterator callable branch has an unproved arm`;
both diagnostics are saved. This boundary was not executed natively. Prove nested
return dependencies without dropping effects, snapshots or complete observers.
Nested custom opens, abrupt close, unguarded Bootstrap defaults, literal range-for
printing and the application driver remain. Full native Bootstrap is unfinished.
