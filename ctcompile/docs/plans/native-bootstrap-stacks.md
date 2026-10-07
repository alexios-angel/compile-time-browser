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
lowering preflights before native execution matrices, including existing
negative cases whose admission boundary changed. Keep baseline failures
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

## Fourth feature wave (landed 2026-10-04)

Combined commit `520e907d9` adds conditional parameter-default selection,
Boolean state through bounded `if` arms, and uncaptured locals with a unique
conditional store dominating every read in its arm. Original producers,
effects, snapshots and source checks remain. Neither class normalization nor
DOM lowering creates a runtime cell or callback object.

The first frozen five-case gate passed 400 native executions while the author
prepared the single-store successor and the other author reviewed it. Review
found that deleting roots could conceal invalid frame placement. A separate
new commit preserves root positions and contexts. The validator measured the
old failure and corrected refusal, then ran four affected class cases on the
final tree. Two unchanged Boolean cases were reused by exact DOM subtree and
fixture identities, with their route bypassing class normalization checked
explicitly.

Unique validated coverage is six cases, 480 native executions, 639 refusals,
30 Node/VM source comparisons and 240 generated C++ files with no Script
runtime. The final four-case run took 268.54 seconds; the earlier five-case run
took 342.40 seconds. See the handoff for exact commands and skipped coverage.
The example demonstrates why a passing matrix does not override a concrete
review finding and why submitted revisions stay immutable.

The unchanged closed Button now reports `class conditional cell escapes its
exact reads`. Its function bodies are unchanged. Source-order inspection
points past duration cell `%970` to shared flag `%972`; the diagnostic itself
does not identify the operation, so that attribution remains an inference.

## Fifth feature wave (landed 2026-10-04)

Combined commit `933f934c9` retains one shared Boolean class cell for exact
synchronous leaf callbacks and admits direct Boolean literals or numeric
`!0`/`!1` origins. The existing closure lifter and DOM state proof reduce these
to scalar values. Mutable state stays out of the fixed-value map. Complete
creator, caller, slot, effect, ordering and root-lifetime proofs remain.

The origin author froze a disjoint DOM-source checkpoint while the class
author finished capture transport. The validator first approved three raw-DOM
cases, then four affected class cases on the combined tree. A real-JavaScript
composition fixture exercises both changes. The final production needed no
repair during validation; subsequent commits corrected test observations.

The original switch fixture coalesced instead of exercising method cloning.
Explicit raw dispatch and real-JavaScript completion-loop controls reach
unsupported downstream arithmetic, so the final tests require clean refusal
and complete source rollback. Successful runtime coverage for shared state
through method cloning is not claimed. Source review covers charged remapping.

Seven selected cases passed: 656 native executions, 902 refusals, 41 Node/VM
source comparisons and 328 generated C++ files with no Script runtime. The
final four-class run took 293.61 seconds; the retained three-case raw-DOM run
took 307.75 seconds. Its exact DOM subtree, 36 source/helper hashes and 493
contracts without the class intrinsic justify reuse. See the handoff for
commands, independent controls, hashes and skipped coverage.

The unchanged closed Button now reports `class Boolean state requires ordered
owner-block uses`. All 320552 bytes of prepared function bodies match the
preceding probe. Source inspection still points to shared flag `%972`, whose
initialization and users are in an else arm, with multi-slot/deferred captures
later. The diagnostic itself does not identify the operation.

## Sixth feature wave (landed 2026-10-04)

Combined commit `b7136ba92` admits class Boolean uses under bounded `if`,
callback creation confined to a dominating arm, and raw DOM state initialized
and used entirely inside one arm. The class proof still requires unconditional
owner initialization; composing it with the narrower DOM scope is the next
step. Original capture slots, callers, root lifetimes, effects and snapshots
remain checked. Output uses scalar values and public DOM operations.

The first seven-case gate ran while the class author prepared one dependent
creator-scope commit. After merging that commit, the validator reran four
class cases. Four raw-DOM results were retained by the exact DOM subtree,
35 source/helper hashes and 690 contracts without the class intrinsic. Unique
coverage passed: eight cases, 43 Node/VM sources, 688 native executions and
1,087 rejection/control checks (nine verifier-only). All 344 selected C++
outputs and their native binaries pass the no-Script gates. The runs took
526.08 and 247.81 seconds. See the handoff for exact commands and skipped checks.

The unchanged closed Button now stops at exact local leaf capture slots.
All 320552 prepared function-body bytes match the preceding probe. The next
small class witness is measured separately: all state and two callbacks are
inside one initializing arm, and all four native variants refuse unconditional
initialization. No callable cell obscures that witness.

## Seventh feature wave (landed 2026-10-04)

Combined commit `f73350ff1` retains Boolean class state initialized entirely
inside one arm, proves conditional synchronous Boolean borrowers, and carries
those conditional bodies through class capture transport. The existing closure
lifter and DOM scalarizer produce native values and branches. Complete original
slots, callers, frames, effects and source rollback remain checked.

The two independent commits were frozen and combined for a nine-case gate while
the class author prepared one successor. The first gate passed in 695.54 seconds.
The successor then passed the five affected class cases in 399.09
seconds. The five raw-DOM results were retained by exact subtree identity,
41 source/helper hashes and 845 contracts without the class intrinsic. Unique
coverage is ten cases, 61 Node/VM sources, 976 native
executions and 1,397 rejection/control checks, including
13 verifier-only controls. All 488 selected generated
C++ files and the native binaries passed no-Script gates. See the handoff for
commands, preflights, root controls and skipped coverage.

The unchanged closed Button still refuses exact local leaf capture slots; all
320552 prepared function-body bytes match wave6. A new synchronous two-slot
witness agrees in Node/VM and refuses in all four native modes. Its two callbacks
share a mutable Boolean at different original slot indices, alongside immutable
Boolean data. This is a smaller measured boundary than the complete deferred
listener/timer pair.

## Eighth feature wave (landed 2026-10-07)

Commit `1ce2d7f41` adds mixed fixed/mutable Boolean captures, exact
Boolean results from synchronous helper branches, and the dependent class
transport. All original captures are proved before fixed reads are consumed
and the retained local state slot is compacted. The existing lifter, Boolean
proof and scalarizer remain responsible for the native program.

The initial preflight found fixed-cell roots being erased after frame exit.
The shared retirement fix preserves their original position and context.
The first runtime gate then caught an explicit Boolean conversion missing in
the test client. Both corrections are separate commits; expectations remain
strict. The final class-result successor was authored while the first gate ran.

The final two-case gate passed in 186.29 s after the four-case gate exposed an
old conditional-return negative that now has a fully proved Boolean-result
shape. Its unchanged source and consumed returns passed a separate differential
check before fixture reclassification; trailing effects/types and roots after
arm frame exits still refuse. The original two runtime gates remain recorded
with their failures, and only justified passing evidence is reused.

Accepted coverage is eight cases, 54 Node/VM source-case checks, 864 native
executions and 1109 controls (including 13 verifier checks). All 432 selected
C++ files and native binaries pass no-Script gates. The final fixture-only
build took 7 s with no Ninja work; full formatting passes. See the handoff for
exact filters, source hashes, rejected checkpoints and skipped coverage.

The unchanged closed Button still stops at exact local leaf capture slots,
with identical 320552-byte prepared function bodies. The next small source
forwards a fixed outer Boolean alongside local mutable state. It agrees in
Node/VM and refuses all four native modes on this candidate. The actual reader
metadata is `enclosing_indices = [0, -1]`; the writer uses local slot 0.

## Next features, based on source and IR traces

1. **Class lane: forwarded fixed Boolean with local shared state.** Start from
   `results-author/next-forwarded-boolean.js`, SHA256
   `90d5173b81686931d5c41656c6024956bfde265474cb7f7e1985da914d8df182`.
   Reuse `callbackPrimitive` and `fixedCaptureSlot` for exact ancestor slots and
   the complete dormant-writer census. The retained mutable slot must still
   have enclosing index -1. Require exact undefined placeholders and bounds for
   every forwarded slot; resolve every original fixed capture before any table
   is compacted. Keep parent, creator, caller, root and rollback proofs. Do not
   merely remove `hasOuterCapture`. Likely production path:
   `ClassInitialization/BooleanCaptures.cpp`; claim exact needed fixtures.
2. **Parallel Bootstrap trace and ownership lane.** Preserve the complete
   vendor prefix and dormant `_queueCallback`. Prepare the next exact typed
   witness without competing for the class lane's source path. The listener
   has six captures and an event formal; the timer has three captures. Trace
   String, element/event and callable/self identities through registration,
   completion, fallback, nested dispatch and teardown. A native owning record
   with borrowing registrations is still a design candidate, not an established
   lifetime proof. Reuse public timer/listener metadata APIs; unlink before
   destruction, keep an owner through reentry and never retain a registry vector
   row across callback execution. Any needed browser extraction requires claims,
   process checks, isolation and focused browser/native parity.
3. **Optional independent DOM backlog: computed Boolean stores.** The existing
   `computed-result-store` refusal is measured, SHA256
   `fec278ab2479003df2a42da54f74a80032197670311615c4b3398d33a3356063`.
   It writes a proved Boolean branch result back to its cell. Both helper proof
   and post-inline owner census currently require literal or `!0`/`!1` stores.
   A future change needs complete Boolean facts, ordered initialization and
   saved-read semantics through both stages. Exact disjoint paths are
   `DOM/Source/LocalState.cpp` and `native_dom_boolean_borrowers.py`. Bootstrap's
   observed transition writes are already literal; this is not its current
   blocker and should not displace the first two lanes.

The validator can prepare source, root and budget controls while authors work,
then test immutable commits serially in the warm build directory. Keep at most
one dependent feature per author while a gate runs. Full combined proof must
pass before landing a composition.

Current evidence is in
`/home/alex/.ctbrowser-auto/codex/ctcompile/artifacts/2026-10-07-native-stack-8/`:
`validation/APPROVAL.md`, `validation/acceptance-gate/`,
`validation/final-preflight/`, `validation/first-gate/`,
`results-author/next-forwarded-boolean.md`, and
`results-author/peer-and-boundary.md`. General completion arithmetic and
successful retained-state method cloning remain unproved; preserve their
negative controls. Native Bootstrap remains incomplete.
