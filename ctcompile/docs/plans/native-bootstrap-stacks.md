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

## Next features, based on the retained Bootstrap trace

1. Prove synchronous sibling callbacks sharing one Boolean cell, reusing native
   closure lifting and primitive-cell storage. A witness should read `false`,
   call a sibling that stores `true`, then observe `true`. Hidden writers,
   escape, delayed calls and object/input captures must remain diagnostics.
2. Extract listener registration/membership and phase snapshots into a public
   Shell core while retaining the existing event dispatcher. Preserve exact
   callback/capture identity, duplicate registration, once-before-invoke,
   removal/re-addition, nested dispatch and passive/stop semantics. Native code
   must not grow a second DOM dispatcher.
3. Prove forwarded capture slots and a typed, session-owned transition record
   shared by timer/listener handles. It needs explicit element/document borrows, callable and self
   identity, cancellation and destruction rules; a stack borrow cannot survive
   an entry return. Test real transition completion, timeout fallback, recursive
   dispatch and teardown before claiming deferred callback support. One source-based
   candidate is a unique transition record owned until the existing timeout fires,
   with the listener borrowing it; completion removes the listener and sets the
   flag, so the later timeout becomes a no-op. This remains a design inference
   pending cancellation, reentrancy and document-lifetime proofs.

These are prerequisite boundaries, not a claim that complete native Bootstrap
works. The retained vendor Button probe must continue to check its original
source, effects and Node/VM behavior as each proof advances.
