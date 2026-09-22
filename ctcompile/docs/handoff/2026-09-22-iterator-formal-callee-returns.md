# Formal return callees and partitioned shift gaps, 2026-09-22

Continued clean **2bfd3fbd** and saved formal-callee source `ee851de6` from
HANDOFF/current00 and iteration 68's final journal. Earlier interrupted work was
already committed; `codex-wip-20260907` was not unmerged. Other branches were
preserved. Test and escape agents worked independently; the parent finished their
preserved fixture fixes after rate-limit interruptions.

## Landed

**a1d1c85f** snapshots helper return dependencies in an immutable graph of
formal positions, fixed helper identities, complete transport edges and calls.
Each invocation binds actual arguments lazily, resolving its callee separately
from its returned values. Returning through `keep(writer, chooser) =>
chooser(writer)` now works on a loop backedge, including selected callees.
Unknown demanded leaves and ungrounded cycles remain refused. The existing
arity, receiver, direct-target, recursion, effect and complete observer checks
still precede expansion. Stable graph indices survive capture erasure; no
callable runtime or VM dependency is emitted. The proof budget is unchanged.

The exact saved source is SHA-256
`ee851de60c75d3a3d9c859f5cd94b9a9295c8a7417e4dfee05ce20a9cb522fd4`.
It and the zero-trip witness return **2729 normally / 3603 on stop**. The selected
callee/state/argument snapshot witness returns **886698 / 980596**. Forcing its
callee selection changes this to **494861 / 547651**; removing its effect gives
**181308 / 211052**. Giving the zero-trip witness two trips yields
**15749 / 18279**. Positive sources execute the iterator and keep
`data-closed=false` on stop. Raw ordinary/direct twins retain the existing scalar
and loop snapshot assertions plus unknown/scalar, missing-actual, publication,
equality, recursion and wrong-direct controls. All 200 historical source bodies
and 70 positive metadata rows are unchanged. The historical nonprogressing source
remains compile-only.

Parallel **ab9d43b1** replaces per-visit mixed-shift refinement with bounded
bisection of the induction lattice. Each subrange uses the existing whole-key
Number transfer; only a proved disjoint interval/stride skips visits. Interior
hits still refuse, and the complete reload/store census and actual-write replay
remain unchanged. Paired CFG/SCF regressions cover signed/unsigned shifts,
nonzero starts, unwritten children, earlier snapshots and later count writes.
A 120-visit regression passes with the same constant-count baseline plus 1024
proof steps; incomplete 128-step runs publish no evidence. No budget was raised.
All 113 historical shift sources and existing raw construction text are unchanged.

## Focused validation

- Three new native sources: admitted under both optimization policies.
- Exact `ctcompile_host_contract`: **1/1 PASS, 2.11 s test / 2.12 s total**.
- Four-source subset: **64 native executions, 368 refusals, two nonexecuted
  source admissions and 32 Node/VM observations**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.04 s test / 2.05 s total**.
- Exact right-shift index-overwrite lit case: **1/1 PASS, 0.13 s**.
- Complete `tools/format.sh --check`: **1124 C++, 157 Python, 114 web files PASS**.

The source subset contains the three new witnesses and the prior branch snapshot
witness, plus all 136 refusal sources. The complete custom execution case was
not run. Additional source-only checks cover 210 syntax programs, 12 exact Node
traces and behavior-changing mutations. Escape checks cover 120 functions and
seven exact new outputs. Its independent enclosure model checks 7,200 subranges
and 178,800 members; the 120-visit model uses 21 refinement evaluations. These
model counts are not compiler timing measurements.

All seven final code/test hashes match the devbox. All 32 generated C++ files
pass Script/VM symbol checks; selected-callee output was inspected and uses
ordinary scalar state, branches and public DOM calls. A read-only review found
no concrete graph, lifetime or budget defect.

Initial host gate crashed (**1.67 s total**), reproduced under gdb: the new raw
fixture applied both ordinary/direct string rewrites; a missing match returned
an empty string, which parsed as an empty module. Conditional rewriting and a
nonempty-source guard fixed it. The next host run (**2.09 s total**) failed only
two new wrong-direct controls whose target needed a fourth argument. They now
retain a structurally valid wrong target. The first array gate (**2.03 s total**)
failed four new budget assertions that expected 120 writes, omitting 360 array
initializer writes. Correct accounting is 480. Production and JS sources did
not change for these repairs; no historical assertion or budget was weakened.

Explicit devbox targets were `ctjs-opt`, `ctjs-translate`, `ctcompile-tool`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. CTests used exact anchored names with
`--output-on-failure --no-tests=error`. Lit used the generated configuration and
filter `^ctcompile :: Analysis/Escape/escape-claims/right-shift-index-overwrite[.]test$`.
Builds/tests and Git writes used their shared locks. Commands, logs, hashes,
Node evidence and generated source are in
`../../../../test-results/2026-09-22-iterator-formal-callee-returns/`.

Full CTest/compiler lit, complete custom execution, unchanged DOM cases, broad
corpus/native matrices, full Bootstrap, WPT/test262, Windows, additional sanitizers
and local C++ builds were skipped. Nothing was pushed. No full Bootstrap coverage
gain is claimed. Linux inspection found 75 readable identities with no errors or
Claude match; Windows CIM found 353 identities with no Claude executable, Node
CLI or loop. No browser/shared implementation or runtime-oracle changes were
made. The devbox idle timer is active/enabled.

## Exact next boundary

Resume existing `body-return`, SHA-256
`9bd2d5baa11a458f4801bd5e103efcb445085b378c8743776a0ff1dd445c37c2`.
Its loop body returns `anchor.hasAttribute('data-visited')`. Node returns false
and records `data-next=false;data-yielded=yes;data-closed=yes;` when the body runs.
If already exhausted, it records `data-next=true;data-yielded=yes;` without close.
Both native policies return status 1: **DOM iterator close must follow its
complete traversal**. Source, four Node observations and both diagnostics are
saved; no native execution of this boundary is claimed.

Prove abrupt IteratorClose through the source completion boundary before
admitting body returns or throws. The existing fixture comment records a source
compiler close gap; preserve the runtime oracle and check current journals
before changing its semantics. Nested custom opens, unguarded Bootstrap defaults,
literal range-for printing, the application driver and full native Bootstrap
remain unfinished. Shift refinement can still exhaust the shared budget on
subranges whose conservative lattices overlap; tighter inverse transfer remains
separate work.
