# Parallel work toward native Bootstrap

The compiler still requires proof before emitting native C++. Checkpoints on
work branches are not approval, and a successful focused gate is not a whole
repository pass. The current measured boundary is in [HANDOFF.md](../HANDOFF.md).

## Working arrangement

Use up to four agents: two authors, one validator and one integrator. Give each
editing agent an isolated branch/worktree, disjoint claims and a distinct
`DEVBOX_DIR`. Keep the integration checkout clean. The validator alone syncs to
its reserved remote directory and holds `/tmp/ctbrowser-devbox-build.lock`
through each build, test and evidence collection. Reuse that directory's build
cache; do not copy or rewrite its absolute-path CMake cache.

An author submits an immutable commit, parent and tree hash, explicit changed
paths, intended behavior, focused targets/tests and known limits. While it is
tested, that author may prepare one dependent feature. Corrections are new
commits, never amendments or rebases of submitted history. A failed checkpoint
blocks landing of its descendants until the corrected combined tree passes.
Review, test preparation and tracing the next blocker can continue without
adding more untested features.

The validator assembles the actual candidate tree in its own worktree and may
combine ready independent changes to avoid redundant builds. Run inexpensive
lowering preflights before native execution matrices. Keep baseline failures
separate from regressions. Record exact commands, results, local/remote source
hashes and the tested tree. The integrator lands only that approved tree under
`/tmp/ctbrowser-repo-git.lock`; changed integration content requires an affected
recheck. Use explicit paths for every commit, preserve peer work, update the
handoff and release claims. No push is implied.

## First feature wave (landed 2026-10-04)

Combined commit `c277883bd` passed the focused gate: 304 native executions,
343 refusals, 18 Node/VM source comparisons and five browser test executables.
See the handoff for exact commands, limits and the observed Bootstrap refusal.

| Lane | Checkpoint | Dependency and acceptance |
| --- | --- | --- |
| Compiler | Fixed primitive captures in synchronous local callbacks | Existing immutable-cell census; exact calls, no identity/escape, full bodies; differential execution and hidden-writer refusals. |
| Compiler | Calls inside structured conditionals and loops | Primitive-capture checkpoint; preceding local definition and supported region ancestry; both branches, repeated calls and effect order. |
| Browser | Public `shell::timer_queue` metadata | Ordinary values and borrowed read-only views; registration order, cancellation, interval deadlines and duplicate IDs; direct native unit and no-Script check. |
| Browser | VM timer adapter | Public queue; preserve callback roots, realms, exceptions, microtasks and host task filtering; focused browser parity tests. Land the complete extraction atomically. |
| Build | Executable script modes | Make direct script execution work in clean Linux worktrees; no script content change. |

The timer API owns scheduling metadata. Callback objects and their lifetimes
remain a separate proof. The primitive capture feature copies only proved
literal values; it does not authorize arbitrary captured cells.

## Second feature wave (landed 2026-10-04)

Combined commit `47beeaf52` passed 272 native executions, 322 refusals,
16 Node/VM source comparisons and nine browser test executables. The unchanged
closed Button now refuses at a nonlocal/unordered class-cell use. The detailed
source/IR trace identifies a branch-local listener capture as the first such
use; conditional writes and shared state are separate later hurdles.

The compiler checkpoint proves fixed primitive captures forwarded through
exact enclosing slots. It uses a class-local, budgeted use census and keeps
mutable, escaping, function/self and deferred captures as diagnostics. New
mixed-capture/helper cases and the original forwarded source exercise it.

The browser checkpoints extract `shell::listener_registry<Payload>` and adapt
all existing listener consumers. One vector owns typed payloads and metadata;
the existing dispatcher retains propagation, realms, exceptions and microtasks.
Native/VM tests preserve identity-based phase snapshots and nested once behavior.
A separate before/after witness verifies copying the selected passive flag
before a handler getter can invalidate the registration row.

The validator tested A3 plus the registry first while the adapter author kept
working. It then assembled all frozen commits for the final focused gate. See
the handoff for exact revisions, commands, counts and remaining limitations.

## Third feature wave (landed 2026-10-04)

Combined commit `07a33a29e` passed 400 native executions, 579 refusals and
24 Node/VM source comparisons across six focused cases. The validator first
approved the conditional-capture stack while the Boolean author continued,
then checked the combined tree. See the handoff for exact commands, hashes,
baseline failures, test-only corrections and skipped coverage.

The class checkpoint proves fixed primitive captures created and synchronously
called inside a later `if` arm. It projects capture positions in both ordering
checks, retaining the full writer/escape census and conditional-write refusals.
The three new source cases exercise both arms, nested branches, aliases and
distinct documents.

The independent DOM checkpoint proves one Boolean cell borrowed by exact
private direct leaf helpers, uses the existing call inliner, then replaces
reads and writes with scalar values in source order. Complete bodies and
symbolic callers are checked before mutation; all remaining effects are
reproved. Five direct-call IR cases are paired with JavaScript oracles. The
last refused budget and unknown-effect cases preserve the original source.
The generated output contains scalar values and public DOM calls, with no
runtime cell or callback object. This does not yet retain mutable class
captures through the earlier class normalization stage.

## Next features, based on source and IR traces

1. Prove conditional default selection for the class cell holding transition
   parameter `n`. The unchanged Button diagnostic remains nonlocal/unordered;
   the source walk now passes the first two parameter capture positions and
   reaches `%964`'s conditional write. This attribution is a source/IR inference,
   not an instrumented failing-operation report. Preserve a supplied false
   argument versus an omitted one through explicit SSA selection. Do not map
   the conditional store to an unconditional fixed value. Keep initial reads
   used by the undefined test distinct from later selected-value reads, and
   preserve the original default expression and all producers/effects.
2. Independently extend the DOM Boolean normalizer through bounded `if` arms.
   Start with already initialized state and exact leaf calls in the original
   arm. Yield each arm's final state as an extra result; an absent else retains
   incoming state. Preserve existing result positions and the complete
   use/body/symbol census. Use the existing iterator state-edge algorithm as a
   reference. Loops, incoming/multiple cells and deferred calls remain later.
   This lane touches DOM source files and can run separately from class work.
3. Retain one class-local Boolean cell and exact leaf callbacks through class
   normalization and ordinary closure lifting, reusing the new DOM proof.
   Keep mutable storage out of fixed-value maps and remap its records when
   methods are cloned. Begin with straight-line read/mark/read and fresh state
   on repeated calls, then conditional state. This overlaps the class-default
   proof files, so stack it after that lane rather than editing them in parallel.
4. Prove typed callable and self identity, then a session-owned transition
   record shared by timer/listener handles. It needs explicit element/document
   borrows, cancellation and destruction rules; a stack borrow cannot survive
   an entry return. A unique record owned until the existing timeout fires,
   with the listener borrowing it, is one source-based candidate. Completion
   removes the listener and sets the flag, making the later timeout a no-op.
   This remains a design inference pending cancellation, reentrancy and
   document-lifetime proofs. Test real completion, fallback, recursive dispatch
   and teardown before claiming deferred support.

The current measured evidence and proposed follow-up details are archived in
`/home/alex/.ctbrowser-auto/codex/ctcompile/artifacts/2026-10-04-native-stack-3/`:
`next-boundary.md`, `next-wave-plan.md` and `validation/combined-07a33a29/`.
Future admissions listed above have not been measured.

The public timer/listener cores are prerequisites, not native callback ownership
proofs. Preserve the original vendor Button probe, all source bodies/effects,
and Node/VM behavior as each boundary advances. Native Bootstrap remains
incomplete.
