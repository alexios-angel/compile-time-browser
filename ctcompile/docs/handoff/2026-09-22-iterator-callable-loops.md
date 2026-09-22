# Iterator callable loops and signed shift residues, 2026-09-22

Resumed interrupted iteration 65. Parser safety **4dafb9c4** was already
committed; the frozen replacement repair landed first as **b6f6f963** after
review and verification against its completed focused gates. See
[crash recovery](2026-09-22-crash-recovery.md) for the remaining two conformance
failures. Subsequent interruptions preserved all native/escape drafts; the
parent resumed their agents and completed the final assertions and gates.

## Landed

**e24bf692** proves callable identities through loop initializers, backedges,
condition arguments, results and both branch arms. A bounded graph traversal
collects only known callable leaves; every incoming edge and observer must be
proved. Cycles with unknown/scalar leaves or no known identity remain refused.
The same traversal resolves the ordinary scalar tags after helper inlining.
Per-invocation target sets, source-order arguments and current captured state
remain distinct. No callable object, VM context, collector or Script dependency
is emitted.

The saved `entry-captured-sibling-returned-callable-loop-join` source is unchanged,
SHA-256 `ac87adf7deaa8b873c0cbe6fe47e244b864b6b5c8af7620dba3a4135bcdc903e`.
It returns **2729 normally / 3603 on stop**, with `data-closed=false`.
The new zero-trip source returns the same values; the state/argument snapshot
source returns **181308 / 211052**. Raw ordinary/direct zero/two-trip twins
check loop state transport, tag selection, argument snapshots and complete
incoming/observer controls. All 165 historical complete sources and 58 positive
metadata rows remain unchanged. The historical nonprogressing source remains
compile-only and was never executed.

**9a036e1a** reuses the existing signed residue lattice for right shifts across
ToInt32 conversion jumps. `gcd(stride, 2^32)` retains invariant input bits before
the existing right-shift transfer. A previously dense enclosure crossed a
reloaded array slot; the proved residue now excludes it. Upper/lower boundary
witnesses write indices **3, 1, 3**. Complete reload/store checks and actual-write
replay retain unwritten children and pre-loop snapshots. Unsigned conversion
jumps keep their conservative enclosure. Nine new source witnesses preserve
all 84 historical function bodies.

## Focused validation

| Check | Result |
| --- | --- |
| Three new native sources, both optimization policies | All admitted |
| Four-source subset, including prior branch snapshots, plus all 111 refusals | 64 native executions, 318 refusals, two nonexecuted admissions, 32 Node/VM observations |
| Exact `ctcompile_host_contract` | 1/1 PASS, 1.64 s test / 1.65 s total |
| Exact `ctcompile_escape_analysis_arrays` | 1/1 PASS, 1.92 s test / 1.93 s total |
| Exact right-shift index-overwrite lit case | 1/1 PASS, 0.13 s |
| Complete formatter | PASS: 1124 C++, 157 Python, 114 web files |

The source subset exercises borrowed/owned DOM, both optimization policies,
explicit/deduced C++ and GCC/Clang. All seven final code/test hashes match the
devbox. All 32 generated C++ files passed Script-symbol checks; the loop snapshot
output was inspected. The devbox idle timer is active/enabled.

Local checks preserved 173 distinct complete native source programs (174 syntax
compilations, including one repeated source), with 32 exact Node traces.
Escape checks covered 93 functions, nine exact outputs and upper/lower index
traces. The source fixture now contains 61 positives, 111 refusals and one
compile-only program.

One new raw assertion initially used a const MLIR operation with non-const
getters; its declaration was fixed. Two host runs then failed eight new loop
snapshot assertions (1.69 and 1.72 s total). The existing scalarizer carries
iterator completion state as well as count/extra, producing five loop results;
state also flows through both selector arms. The final assertion checks that
complete transport. Production did not change to satisfy these assertions.
Temporary dumps were removed. A new zero-trip source initially eliminated all
invocations of its alternate helper and correctly refused; retaining its original
alternate caller fixed the witness. The saved source never changed.

Explicit devbox build targets were `ctjs-opt`, `ctjs-translate`, `ctcompile-tool`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. CTest selections used exact anchored names and
`--output-on-failure --no-tests=error`. Lit used the generated build configuration
and exact filter
`^ctcompile :: Analysis/Escape/escape-claims/right-shift-index-overwrite[.]test$`.
Every devbox/Git operation held its shared lock. Commands, logs, hashes, generated
C++, source observations and next-boundary diagnostics are in
`../../../../test-results/2026-09-22-iterator-callable-loops/`.

Full CTest/compiler lit, the complete custom iterator execution case, unchanged
DOM cases, broad corpus/native matrices, full WPT/test262, Windows and local C++
builds were skipped. Sanitizers ran only for the earlier crash repair. No full
Bootstrap admission or coverage gain is claimed. The native and escape changes
made no browser/runtime-oracle edits.

## Exact next boundary

`entry-captured-sibling-returned-loop-call-result`, SHA-256
`580dfa93aaf660d6d6abd5b7374cb2a729770863d440b3cc5c52f68d7bf046c1`, adds
`selected = keep(selected)` on the backedge. Node returns **2729 normally / 3603
on stop**, with `data-closed=false`. Both native policies return status 1:

```text
native DOM source: DOM iterator callable branch has an unproved arm
```

Both diagnostics are saved; this boundary was not executed natively. Prove
returned-callable dependencies around a loop while retaining every actual/formal
edge, effect and snapshot. Nested custom opens, abrupt iterator close, unguarded
Bootstrap defaults, literal range-for printing and the application driver remain.
Part 25 retains unsigned jump residues and mixed sparse-gap precision as separate
work. Full native Bootstrap remains unfinished.
