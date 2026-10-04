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

## Next features, based on source and IR traces

1. **Class lane:** retain the original mutable cell when initialization and
   every meaningful read, write and capture belong to one dominating arm.
   Start from the measured `arm-only-state` refusal. Keep exact creator-to-call
   containment and the complete symbolic-caller/body census. Roots may be
   irrelevant to value initialization but must keep their original positions
   and frame obligations. Do not hoist initialization or enter mutable state
   into the fixed-cell map. Test then/else initialization, nested uses,
   snapshots, repeated calls, aliases and distinct documents; refuse reads,
   captures or calls before initialization, sibling/join uses and escapes.
   Own `ClassInitialization/BooleanCaptures.cpp` and the class scope fixtures.
2. **Independent DOM lane:** measure and then prove exact synchronous Boolean
   borrowers with bounded zero-result `if` bodies, Boolean predicates, literal
   or `!0`/`!1` stores and one top-level return. Preserve all original helper
   bodies, formal uses, callers, frames and effects before inlining. The current
   branch set and initialization census precede inlining: newly cloned helper
   branches require a fresh actual-cell ancestry and initialization proof
   afterward. Reserve recursive clone work; `operationWork` alone is not a
   recursive charge. Reuse the existing inliner and scalarizer, preserving
   complete rollback on late failure. Own `DOM/Source/LocalState.cpp`, a new
   borrower fixture and the raw cells/branches/origins refusal promotions.
   This lane is source-reviewed design only, not an observed admission. It
   does not change the separate class flat-body rule.
3. **Later composition and ownership:** prove typed multiple captures and
   callable/self identity, then complete conditional callback bodies and an
   owned transition record shared by listener/timer handles. Bootstrap's
   listener captures six slots and takes an event formal; its timer captures
   three slots. A unique record held until the pending timeout completes,
   with listeners borrowing a stable pointee, remains a design candidate.
   Cancellation must unlink before deletion, reentry needs an active owner,
   and document/element borrows must remain valid. Never borrow a registry
   vector row across callback execution. Completion, fallback, recursive
   dispatch and teardown require native/browser parity before admission.

The first two lanes have disjoint production and test paths and independently
measurable boundaries. The validator can prepare their unchanged witnesses,
root controls and budget/rollback cases while authors work, then check the
frozen commits serially. Keep the limit of one dependent feature per author
while a gate runs. A composition checkpoint must pass on the actual merged
tree; successful prerequisites alone do not establish it.

General completion arithmetic and positive runtime coverage of retained state
through method cloning remain separate compiler prerequisites. Keep their
negative dispatch controls until the corresponding behavior is proved.

Current evidence and follow-up traces are in
`/home/alex/.ctbrowser-auto/codex/ctcompile/artifacts/2026-10-04-native-stack-6/`:
`root/next-boundary.md`, `branches-author/next-boundary.md`,
`state-author/conditional-borrowers-feasibility.md`, `validation/scope-preflight/`
and `validation/final-gate/`. Proposed admissions above have not been measured.

The public timer/listener cores supply metadata; they do not establish native
callback ownership. Preserve the original vendor Button probe, all source
bodies/effects and Node/VM behavior as each boundary advances. Native Bootstrap
remains incomplete.
