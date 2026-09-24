# Original iterator recovery and computed Number division, 2026-09-24 UTC

Resumed clean `8e971bbf` from the latest HANDOFF, Current native work, journal and
both areas' commit history. `codex-wip-20260907` was already an ancestor; no
unfinished tree patch needed recovery. Multiple agents investigated source,
escape and review work. Rate limits interrupted them repeatedly; the parent
resumed their saved checkpoints and completed the focused gates and commits.

## Landed

`cb59fd8a` adds a regression for the unchanged original observing iterator getter
`1a7fb1661163b19674ee92084bc52ded3f96417279039131df31282ca6d813b4` and method
`7acf503b5734264de1d4d390cfb239a63273cd9052c078c66fd60166bdc2c6d3`.
Existing `recoverPrimitiveExceptionRegion(..., CheckedInvocations)` already
recovers both raw sources. Each has four reachable protocol calls, fifteen
pre-call registers per invocation, 33 original status checks and 57,944 recovery
steps. The new test checks original call sites, callee/receiver/argument slots,
separate normal results, failure payload/state forwarding and the complete
original rollback snapshot. Corrupt failure state, changed normal state and zero
budget retain their source and refuse. `33ed9338` adds the independent argument
count assertion identified during review, so a truncated operand comparison
cannot hide a removed argument.

This resolves uncertainty in the prior handoff: the structural recovery already
exists. Its chain-only inspection helper was narrower than the underlying
recovery implementation. No duplicate inspector or recovery implementation was
added. This test does not admit the observing iterator to native output, prove
its protected effects, or give a borrowed node an escaping exception owner.

`f5d2e65e` reuses LLVM APFloat for independent binary64 division snapshots,
rounding each operation to nearest, ties to even. Only original Number literals
or independent Number snapshots supply operands. The existing 64-operation depth
limit remains. Core supplies ToUint32, whose bits never reconstruct arithmetic
operands or property keys. Exact integer division remains independent; remainder
gains no computed snapshot. Loop proof uses the existing arithmetic-demand path,
and array replay charges newly proved bits just as Add/Sub/Mul do.

Raw controls cover fractional nonidentity division by 0.5, a following addition,
saved children, unknown operands, direct property-key refusal, table mutations
before/after reads and the 64/65 depth boundary. Three historical Number Div-one
controls and the unchanged 3/2 shift-base control now prove their bitwise results.
All 159 historical source function bodies remain unchanged; three new source
functions cover ordinary overwrites, a saved child and mutation.

## Measured focused validation

- Final exact `ctcompile_exception_recovery`: **1/1 PASS**, 4.92 s, 4.93 s total.
  The earlier final regression passed at 4.86 s; only this exact CTest was rerun
  after review added the argument-count assertion.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS**, 3.79 s, 3.81 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS**,
  0.17 s, 357 excluded.
- Unchanged source
  `57db40f7d3dfebab29134056222b05b4b00b2e0f0cd35cdd753641d86cb963aa`, program
  `73139a70dad89e41`: child function 1/site 4 Stored -> **Confined**; key table
  Passed -> **Confined**. Two of four total sites are statically confined, with
  zero soundness violations. The identical-source baseline from iteration 144
  was retained without replay. Node returns `[0,0,0]`; the VM observes the child
  once, confined, with zero unresolved or unchecked instances.
- The next remainder witness was measured once through the same focused
  claims/oracle check: four observed sites, zero violations, zero statically
  confined sites. Node again returns `[0,0,0]`; the VM observes the child confined
  once. This is a measured next boundary, not an implemented remainder feature.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**.
  `git diff --check` passes. All **nine final code/test SHA256 hashes** match
  the devbox and the completed independent review.
- Independent final review: **no unresolved findings**. Its two findings were
  fixed: charge Div's computed bits and check native call argument count.
  The reviewer did not run or replay the parent's tests.

Builds used locked `tools/remote-build.sh` with explicit affected targets:
`ctcompile-test-exception-recovery`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`.

The first native probe build found a new const MLIR wrapper error, fixed before
its first run. The strengthened native test then counted an unreachable
`hasAttribute('stop')` call among the four reachable protocol calls. Its census
now uses original CFG reachability; the unreachable operation remains in the
retained source snapshot. No production change was required. The first arrays
run failed seven assertions: six arose from the now-proved unchanged 3/2
shift-base witness, and one expected 64 work where 32 independently proved Div
results now cost 96. The input programs were retained; only expectations changed.
Production remained unchanged during these failed arrays runs.

Skipped: full CTest/compiler lit, native caught-node/custom-iterator execution
fixtures, unaffected native replay, broad corpus/matrices, full Bootstrap,
browser WPT/test262, Windows and sanitizers. No native runtime or browser behavior
changed. There was no new generated native C++ or native-execution measurement in
this slice. No local C++ build, push or history rewrite occurred.

Linux process inspection read twelve executable identities with sixty access
errors; Windows Get-CimInstance returned 349 records. Neither found an actual
Claude identity. Availability remained uncertain, so concurrent-area rules
applied throughout. No browser/shared implementation files were edited.

## Exact next boundaries

For original getter `1a7fb166` and method `7acf503b`, reuse the existing raw
`CheckedInvocations` recovery. Connect its per-call flag/result/payload/state
completions to custom-iterator normalization before general CFG simplification
loses the complete original register vectors. The current protocol consumer
accepts only a resultless abrupt-close Invoke with unused payload, no state and
empty continuations. It must retain observed completion tuples, cleanup writes,
normal exhaustion and saved return state. Every discarded non-call status edge
still needs an independent effect proof; structural recovery or final result
kinds cannot supply it. The current native entry still refuses these outer
catches. Escaping borrowed node exceptions still require an owner lasting through
the exception's lifetime.

Escape source
`38de654ca93c52a916b27e820f8e4c00751d0c3100c04482067a2333e0a0f7e5`, program
`f037892d3c412d79`, uses `((keys[i % 2] - 0.25 + 0.25) % 3) | 0`.
Its child remains Stored and key table Passed, despite the measured confined VM
child and Node `[0,0,0]`. Add an independent computed Number remainder proof;
do not use ToUint32 bits as a Number or property key. The older fractional-property
Node/VM discrepancy remains separate.

Broader protected observers and iterators, unguarded Bootstrap defaults, the
application driver and full native Bootstrap remain unfinished. This session
establishes no full-Bootstrap admission or coverage gain.

Evidence: `../test-results/2026-09-24-iterator-recovery-number-division/`, containing
frozen sources/baselines, focused logs, original raw IR, final code/test hashes,
review and checksum manifest. No executable artifacts are retained.
