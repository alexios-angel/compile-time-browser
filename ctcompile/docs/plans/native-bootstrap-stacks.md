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

## Next features, based on source and IR traces

1. Prove a fixed literal captured by a synchronous callback created/called
   inside a later `if` arm. Project exact capture uses to their enclosing
   position in both the class cell census and literal-order proof. Preserve
   the full writer/escape/use census and test both branches. Do not apply that
   relaxation to conditional cell writes: a default assignment must not erase
   a supplied argument. This is the smallest proposed construct suggested by
   the latest Button refusal; typed parameter/deferred captures remain later.
2. Prove one local Boolean cell borrowed by exact synchronous direct helpers
   in DOM source expansion. Reuse the existing call inliner, then replace reads
   and writes with the current SSA value in source order. Start with one owning
   block and literal initialization/stores. Keep the other operations for the
   complete DOM effect/type proof. Unknown callees, escaped/forwarded addresses,
   non-Boolean stores, reads before initialization and unsupported regions
   remain diagnostics. Caller-supplied native annotations provide no proof.
3. Preserve that cell and exact leaf callbacks through class normalization and
   existing closure lifting. Class fixed-value maps currently erase storage;
   the class and DOM stages both need explicit support. The positive witness
   reads `false`, calls a sibling that stores `true`, then observes `true`, with
   fresh state on repeated method calls. Test hidden writers, shallow budget
   exhaustion, late failure/rollback and complete dormant bodies.
4. Prove typed callable and self identity, then a session-owned transition
   record shared by timer/listener handles. It needs explicit element/document
   borrows, cancellation and destruction rules; a stack borrow cannot survive
   an entry return. A unique record owned until the existing timeout fires,
   with the listener borrowing it, is one source-based candidate. Completion
   removes the listener and sets the flag, making the later timeout a no-op.
   This remains a design inference pending cancellation, reentrancy and
   document-lifetime proofs. Test real completion, fallback, recursive dispatch
   and teardown before claiming deferred support.

The detailed read-only trace and schematic IR are archived in
`/home/alex/.ctbrowser-auto/codex/ctcompile/artifacts/2026-10-04-native-stack-2/next-boundary.md`.
These future admissions have not been measured.

The public timer/listener cores are prerequisites, not native callback ownership
proofs. Preserve the original vendor Button probe, all source bodies/effects,
and Node/VM behavior as each boundary advances. Native Bootstrap remains
incomplete.
