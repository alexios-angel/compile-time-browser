# Source calls at the native exception boundary

The next source exception increment is a protected direct call whose return is
assigned to a local. The existing `ctjs.invoke` representation and live type
queries cover its two completions. An internal structural recovery mode now
connects them to the enclosing try; native admission and emission do not consume
that mode yet. An additional effect-checked mode validates the operations whose
status edges would disappear before adopting the recovered body. Ordinary
lowering retains its throwing-call refusal. See
[native exceptions](native-exceptions.md).

[The source/import regression](../test/CTJS/Import/invocation-state.mlir) keeps
four small programs at that boundary. It compares Node and the interpreter,
checks the actual imported register edges before and after global resolution,
and requires ordinary/default native lowering and reruns to retain the original
exception operations and source-function denominator. This gate emits no native
program and makes no new Bootstrap coverage claim.

| Source case | Required observation | State needed by the catch |
|---|---|---|
| `mark = 10; mark = choose(flag)` | caught 42, normal 20 | 10, rather than try-entry 0 or the unavailable returned value |
| Two assignments through `choose`, first returning 20 | caught 52, normal 20 | The first call's normally published result |
| `mark = choose(flag, (mark = 14))` | caught 28, normal 20 | 14, after argument evaluation and before invocation |
| `receiver()[key()](argument(flag))` with an observable getter | caught 42, normal 20, order 12345 | Original receiver identity; receiver, key, getter, argument and invocation in that order |

The three direct-call specimens also retain complete register correspondence
after `ctjs-lift-to-scf`. The method specimen checks raw/resolved CFG because its
property effects deliberately exclude it from the primitive preservation filter.
The checker substitutes an unavailable result into a real direct call's unwind
edge and requires its state checks to reject that corruption.

The integrated devbox gate passes all four programs, **16 source functions**
and **11 Node/interpreter observations**, with plain/default native refusals
and reruns intact. The regression is one of **163/163 passing lit cases** in
the **475/475 CTest** full gate. See [HANDOFF.md](HANDOFF.md) for the build
baseline and `/tmp/ctcompile-arguments-full-gate.log` for the run.

## What the imported edges say

The importer inserts a `ctjs.check` after each protected bytecode instruction.
Its normal vector is the completed register file; its unwind vector is saved
before that instruction. A call overwrites a scratch register on normal return.
Later move instructions publish the assignment into `mark`; they are already
on the normal continuation. The selected callee, receiver and arguments have
been evaluated before this call instruction starts.

This is why one saved vector at handler installation is insufficient. It is
also why a call's normal vector cannot stand in for its unwind vector. Neither
the old `mark` nor the argument's mutation may be reconstructed from the call's
result. Global resolution must keep the original callee operand while naming
the direct target; a name alone does not prove evaluation order or identity.

## Structural recovery prerequisite

`ExceptionRecoveryMode::CheckedInvocations` extends the existing disposable
recovery transaction. It accepts one checked direct call per status block with
an unpublished result and the complete pre-call register vector. Preparation
in that block must be ODS-pure or root bookkeeping; a fallible prefix requires
its own earlier check. Each normal vector differs from its saved vector only
at the call's single scratch-result position.

The recovered invoke returns a value-only completion tuple: a JavaScript
boolean, normal result, thrown payload and saved registers. Its unwind arm
carries the implicit payload and pre-call state, with poison in the unavailable
normal-result position. Outside the invoke, `ctjs.truthy` converts the boolean
to the enclosing try's `i1` flag. Only normal completion reaches the original
assignment continuation; unwind reaches `ctjs.try_exit` with the saved state.
No branch leaves a region and no native runtime carrier is introduced.

This mode is an internal structural prerequisite, with no CLI or ordinary
native caller. The default remains `ExplicitThrows`. Other original status
edges remain in the returned rollback snapshot; the structurally recovered
clone is not executable permission to discard them. Live effect admission,
supported parameter/result/payload carriers and the complete native call
component must succeed before this mode can be adopted. Failure must restore
the entire original graph.

The focused `ctcompile_exception_recovery` unit imports the three direct-call
source fixtures, verifies the resulting IR and checks normal/unwind SSA wiring
against independently supplied helper completions. It checks the second call's
saved state, argument-side mutation, malformed state vectors, result publication,
fallible preparation, unresolved calls, work limits and exact rollback/reruns.
These are structural checks, not native execution measurements. The four-source
interpreter/import regression above remains the execution reference.

The focused devbox unit passes all three direct-call fixtures: **one/two/one
recovered invokes** and **seven/ten/nine original checks**, respectively.
Normal and unwind results, the second call's saved state and argument mutation
match the independent completion inputs. Every rollback restores the original
graph and the rerun remains valid. This unit is one of **8/8 passing focused
CTests** in `/tmp/ctcompile-arguments-integrated-focused4.log` and also passes
in the full **475/475** gate.

## Live effect validation before adoption

`ExceptionRecoveryMode::EffectCheckedInvocations` adds a bounded validation to
the same recovery transaction. It first collects the original acyclic normal
and catch tails, then checks every operation outside the exact represented
calls. Every original `check` still has its normal and handler successors while
this proof runs. An unsupported operation or exhausted budget rejects the
transaction before a recovered body is constructed or adopted.

The proof accepts declaratively pure operations without throw/reentry/suspend
traits, handler/frame bookkeeping, total tag operations, and primitive
arithmetic/conversions. It uses the existing `staticResultType` query for
unconditional normal-result facts and follows every original predecessor
operand for other primitive values. A join with an unknown input refuses.
Numbers, booleans, strings, null and undefined exclude object, Symbol and BigInt
coercion; `ToObject` remains unsupported because nullish values throw.

Result facts are separate from effects: unary plus always returns a Number
when it returns, but its operand may run `valueOf` or throw. The producer is
still visited by the effect scan. No stored type, nothrow or prior recovery
annotation is authority. Both normal and catch continuations are scanned;
an unprotected call in either continuation cannot borrow the represented
invocation's unwind edge.

The source fixtures' `ctjs.load_global` callee lookups now have a separate
bounded declaration and binding proof. A complete current module census must
find one source entry, every indexed body, one hoisting store for each closure,
and no missing body. The closure must retain its exact enclosing source and
zero-capture target. Every load stays in root bookkeeping or a direct callee
operand whose full predecessor closure has that exact load as its origin.
The current named target, implicit operand and arity must agree. A second store,
late initialization, escaping callee, observed implicit callee, duplicate
identity or unknown/host load rejects the proof.

After the identity census, every current body is checked for effects that could
change bindings indirectly. Property reads/writes, accessors, unknown calls,
non-entry global mutation and unknown coercions remain unsupported. Source
declarations and entry publication retain their original operations; no getter
is replaced by a guessed value. The existing register-flow helpers report exact
work consumed, sharing recovery's budget through the complete use/origin walks.
The proof reads original module operations and their exact disposable clones;
there is no persisted binding marker or symbol-only completion permission.
Throw payloads must also be primitive, since observing a thrown object can
reenter. A formal payload is checked against every current actual through the
closed call family; an uncalled helper with an unknown payload refuses the
whole binding proof.

The catch arithmetic also needs facts about the helper's completions. A bounded
query scans current returns and explicit throws through the closed source call
family, rejecting unknown effects and nested regions. Primitive operands follow
all predecessor edges; helper formals map through the exact chain of actual
call operands. The normal return and thrown payload are separate facts, and
both remain separate from proving the call nonthrowing. Other normal/catch
effects still need the full effect scan. This admits structural recovery of
the direct source fixtures without admitting the native call component or its
payload/state carriers.

The `ctcompile_exception_recovery` additions cover a closed primitive CFG with
two status edges, both edges of one conditional targeting the same block,
normal/catch effects, a numeric-result coercion that can throw, property and
global reads, publication, allocation, nullish `ToObject`, an unprotected
direct call, forged markers, every incomplete work budget, exact completion,
and exact rollback before a fresh mutated proof. The source tests run both
internal modes, check their original state/argument observations, every
incomplete source-proof budget and exact completion, and then mutate live
stores, initialization order, targets, operands, predecessor state, closure
origin, helper completions and getter/effect paths after a successful rollback.
Forged prior-proof markers are present in every mutation. These are compiler
structure and proof tests, not native execution measurements; the ordinary
native throwing-call refusals remain in force.

The focused devbox gate passes **2/2 CTests in 130.38 seconds**, including
**163/163 lit cases** and the recovery unit in **0.43 seconds**. The three
imported sources pass both internal modes. Effect-checked recovery retains
**one/two/one invokes** with **seven/ten/nine original checks** in the rollback
snapshots, completing at **3521/6068/4875 steps** for assignment, sequential
calls and argument mutation. Every one of the assignment's **3521 incomplete
budgets** and the closed effect fixture's **937 incomplete budgets** refuses
without changing the source; exact completion and rollback pass. All nineteen
live binding mutations and both uncalled-throw variants pass. Log:
`/tmp/ctcompile-map-presence-invocations.log`. No native program or new Bootstrap
component is admitted by this increment.

## Transitive source completion prerequisite

`EffectCheckedInvocations` now follows `guarded -> choose -> forward -> leaf`
without replacing any runtime call. Before using transitive completion facts,
the existing declaration/use proof inventories the entire current source call
graph and rejects recursive families. Its bounded topological walk includes
every declared body, unused recursive helpers and repeated call edges. No
optimistic primitive seed or persisted completion marker can authorize a cycle.

The completion walk distinguishes returns from throws: only returns of the
invoked body supply its normal result, while an uncaught throw in any descendant
can supply its unwind payload. It scans all descendant bodies even when a
descendant result is unused. A nested handler, missing body, changed callee,
unknown effect or exhausted budget refuses before adopting the recovered clone.
The existing primitive effect rules still apply; this is not a proof that the
primitive alternatives share a supported native carrier.

Each pending completion retains an interned chain of its exact direct calls.
Mapping a formal to an actual pops exactly one call context. A body-wide effect
check without a selected invocation instead inspects every current caller.
This distinction lets an unrelated call return an unknown value while the
protected call's own returned state is proved primitive; adding an unknown
actual to the protected family removes that proof. Context depth has a separate
limit of 32, in addition to the shared work budget. Neither limit invokes C++
recursion or mutates the source on failure.

The recovery unit adds transitive variants of assignment, sequential calls and
argument mutation, checking the same one/two/one invokes and pre-call state.
A fourth variant distinguishes selected actuals from an unrelated unknown
normal return. Its controls cover fresh and mutual recursion, an unused
recursive declaration, an unknown caller payload, descendant property effects,
late declaration/callee/missing-body mutations, a late unknown selected actual,
every incomplete transitive budget, exact completion and 32/33-deep call chains.
All original bodies and status edges remain available for byte-identical
rollback. The ordinary `ExplicitThrows` refusal, native census and emission
path remain unchanged; complete source admission and owning payload/state
lowering remain the next integration boundary.

The devbox recovery CTest passes in **1.19 seconds**. Transitive assignment,
sequential and argument-state proofs complete at **3737/6601/5106** steps with
**1/2/1 invokes** and **7/10/9 original checks** available for rollback. The
selected-actual variant completes at **9031** steps with two invokes and twelve
original checks. All **5106** incomplete transitive budgets, the depth controls
and nine new refusals pass, alongside all **3534** current source-binding and
**937** effect-budget prefixes. Log: `/tmp/ctcompile-map-keyfacts-invocations.log`;
the final compiler/full gate is recorded in [HANDOFF.md](HANDOFF.md).

## Remaining native recovery integration

Use [Exceptions/Recovery.cpp](../lib/CTNative/Lowering/Exceptions/Recovery.cpp),
which already clones, bounds and structures an acyclic handler CFG. Do not add
a parallel exception-recovery pass. Its `cloneTail` converts non-call `check`s
into normal edges. The structural-only mode requires later native admission
to prove the discarded exceptional operations nonthrowing; the effect-checked
mode validates them first and conservatively refuses unproved effects.

The complete native path requires all five stages. The structural mode implements
stages 1–3; the new mode supplies a narrow pre-adoption effect check for stage 4.
Complete source admission and stage 5 emission remain unfinished:

1. Identify its actual `call_direct` and corresponding check, keeping the full
   pre-instruction vector. Prove there is no intervening fallible computation or
   publication. Method lookup and argument calls have their own effect/exception
   obligations; one invoke cannot absorb them just because they share syntax.
2. Put only that direct call in the invocation body. `invoke_exit` receives its
   normal result and the external pre-call state. The normal continuation alone
   may publish the assignment. The unwind continuation receives its implicit
   payload and saved registers, with no read of the unavailable normal result.
3. Connect both continuations to the enclosing recovered try completion. This
   requires a deliberate completion representation or continuation factoring:
   `invoke_yield` terminates the invocation, whereas `try_exit` terminates the
   enclosing try. A cross-region branch or replacing `invoke_yield` with
   `try_exit` is invalid. The current invocation results are JavaScript values;
   the try completion's `i1` dispatch flag cannot simply be appended to them.
4. Retain other status edges until their live nonthrowing proofs succeed.
   Invoke normal/unwind type flow supplies carriers, not permission to erase
   property errors, callback effects or throws in either continuation. Admit
   the complete closed call component, including standalone primitive throws,
   before committing the recovered body. Failure restores the original CFG.
5. Lower the accepted invocation using owning payload/catch state and native
   C++ unwinding. Save catch-visible state before the call and assign its result
   only after normal return. Existing target exception verification is a useful
   final check, but does not replace the source effect proof.

Removing the current `throws == 0` guard alone violates step 4. Wrapping only
the call in an invocation violates step 3 unless its exceptional continuation
actually reaches the active catch with the correct state.

Before changing admission, turn the three direct-call source controls into
complete native positives with the same denominators and observations, add
owning-string lifetime/sanitizer variants, and keep late callee mutations,
mixed payloads, work exhaustion, foreign failures and reentry as independent
controls. Nested source handlers, general finally and uncaught entry adapters
remain separate boundaries.
