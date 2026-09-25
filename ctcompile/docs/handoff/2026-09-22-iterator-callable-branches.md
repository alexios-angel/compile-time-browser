# Iterator callable branch joins and wrapped complements, 2026-09-22

Resumed clean `efbab738` and source `5936d993` from HANDOFF, plan 00 and iteration
62's journal. Previous native/escape work was committed; `codex-wip-20260907` was
already merged. Other branches were preserved. Linux inspection found 15 readable
executable identities, 56 errors and no Claude match; Windows CIM found no match
among 346 processes. Availability stayed uncertain; concurrent-agent rules applied.
No browser, shared implementation or runtime-oracle semantics changed.

## Landed

**4ad7e94c** carries finite callable identities through complete `if` results at
each invocation. Every arm, target, actual/formal edge and result observer is
checked before expansion. Unknown or scalar arms, mutable/escaping callables,
recursion and mismatched direct targets refuse. A private scalar discriminator
preserves the selected identity until ordinary conditional calls are inlined.
The existing state rewrite carries their ordered scalar updates. Selection and
evaluated arguments retain their original positions; native output has no callable
object, lookup table, Script value, VM context or collector.

The saved source remains byte-identical, SHA-256
`5936d99329018b5c6edbfee9451e9965edbe4cb3da26aacaf62dea660347cb6a`, returning
**2729 normally / 3603 on stop**, `data-closed=false`. The snapshot source returns
**166142 / 192687** and forwarding source **6305 / 7671**. Raw ordinary/direct
selector twins check both writer effects, scalar snapshots, repeated final state
and selection before an argument changes the captured value. Six new raw and six
new source hostile cases retain complete confinement checks. All 156 earlier
complete sources, 55 positive metadata tuples and both native assertion strings
remain; the historical nonprogressing source is compile-only and was not executed.

Generated C++ inspection found an encoding mistake after the initial commit:
`NumberAttr::get` accepts IEEE bit patterns, so tag 1 had printed as `5e-324`.
**8d6cb131** uses the existing bit-cast convention to emit ordinary integer tags;
a raw regression checks their decoded values. The affected sources were rerun.

**78040bbc** reuses the left-shift signed residue enclosure for larger unary
complements across ToInt32 conversion jumps. Negating the input step modulo 2^32
preserves the residue modulo `gcd(stride, 2^32)`. Exact two-point bounds remain.
Actual-write replay retains unwritten children and pre-loop snapshots; full
reload/store and work-budget checks remain. Eight new CFG/SCF rows and eight
source witnesses cover even/odd residues, nested complements, reload gaps and
hostile overlapping/later stores. All 32 previous source bodies and call tails
remain unchanged. Two existing raw refusals now prove the retained child with
the same construction bodies.

## Focused validation

| Check | Result |
| --- | --- |
| Explicit affected devbox builds | PASS |
| Three branch admissions, both optimization policies | PASS |
| All 106 source refusal preflights | PASS |
| Initial seven-source subset plus all refusals | 112 native executions, 380 refusals, two nonexecuted admissions, 56 Node/VM observations |
| Final three-source subset after tag encoding | 48 executions, 284 refusals, two nonexecuted admissions, 24 Node/VM observations |
| Final exact `ctcompile_host_contract` | 1/1 PASS; 1.49 s test / 1.50 s total |
| Exact `ctcompile_escape_analysis_arrays` | 1/1 PASS; 1.91 s test / 1.93 s total |
| Bitnot/left-shift index-overwrite lit | 2/2 PASS; 0.13 s |

The source subsets cover borrowed/owned DOM, both optimization policies,
explicit/deduced C++ and GCC/Clang. Initial seven-source execution predates only
the discriminator encoding correction; the final replay selects its three affected
branch sources and retains every refusal and compile-only check.

Two initial host runs each failed four new snapshot assertions (1.50 and 1.55 s
total). The selector's `if` carries an unchanged state value through both arms;
the assertion incorrectly required the original SSA identity directly. The final
assertion checks both yielded identities and strengthens selector timing. No
production change was needed for those failures. The corrected raw fixture passed
in 1.52 s total before the later tag correction; the final host result above
includes the integer-tag regression. Temporary IR dumps were removed.

Local validation: 165 native-source syntax checks and 32 exact Node traces;
40 complement-source executions, eight exact new results and 29,902 sampled
residue memberships. All seven final code/test hashes match local/devbox files.
The 56 initial and 24 final generated C++ files pass Script/VM protocol checks;
final branch-snapshot C++ has ordinary integer selection and public DOM calls.
The devbox idle timer is active/enabled. Every remote command held the shared
build lock. Explicit build targets were:

```text
ctjs-opt ctjs-translate ctcompile-tool ctcompile-test-native-reference
ctcompile-test-host-contract ctcompile-test-escape-analysis-arrays
ctcompile-test-escape-claims ctcompile-test-type-oracle
```

CTest selections used exact anchored names and `--output-on-failure --no-tests=error`.
Lit used the generated `build/ctcompile/test` configuration and exact filter
`^ctcompile :: Analysis/Escape/escape-claims/(bitnot|left-shift)-index-overwrite[.]test$`.
Commands, logs, hashes, preserved sources, generated C++ and next-boundary stderr
are in `../test-results/2026-09-22-iterator-callable-branches/`.

Required `tools/format.sh --check` retains sixteen existing diagnostics in
`ctbrowser/tools/ctdrive/ctdrive.cpp`, `ctcompile/lib/CTNative/HostContract/ProviderPaths.h`,
`ctcompile/lib/CTNative/PartialEvaluation/Heap.h` and
`ctcompile/lib/CTNative/Symbolic/Facts.cpp`. Changed C++ passes the pinned formatter;
Python passes Black/AST, diff whitespace and temporary shell syntax pass.
Documentation needs no build or CTest.

Three agents worked on source tests, raw tests and escape analysis. Source/raw
agents hit rate limits after their drafts/checks; the parent finished promotion,
snapshot diagnosis, tag correction and final gates. Escape work completed normally.
No full CTest/compiler lit, complete custom case, unchanged nested/dataset case,
broad corpus/native matrix, WPT/test262, Windows, additional sanitizer, local C++
build or push ran. No full-Bootstrap admission or vendor coverage gain is claimed.

## Exact next boundary

`entry-captured-sibling-returned-callable-loop-join`, SHA-256
`ac87adf7deaa8b873c0cbe6fe47e244b864b6b5c8af7620dba3a4135bcdc903e`, carries its
selected writer through a bounded two-trip loop. Node returns **2729 normally /
3603 on stop**, with `data-closed=false`. Both native policies return status 1:

```text
native DOM source: DOM iterator callable branch has an unproved arm
```

Both stderr captures are saved; the boundary has not executed natively. Next
prove loop-carried callable identities and every transport/call/observer edge
without changing selection time or scalar/state snapshots. Nested custom opens,
abrupt iterator close, literal range-for printing, unguarded Bootstrap defaults
and the application driver remain unfinished. Part 25 retains mixed sparse-gap
unions and precision beyond the current enclosures as separate work.
