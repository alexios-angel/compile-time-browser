# Source calls at the native exception boundary

The next source exception increment is a protected direct call whose return is
assigned to a local. The existing `ctjs.invoke` representation and live type
queries cover its two completions; source recovery and native emission do not
consume them yet. Ordinary lowering must retain its throwing-call refusal until
both consumers preserve those completions. See [native exceptions](native-exceptions.md).

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
the **474/474 CTest** full gate. See [HANDOFF.md](HANDOFF.md) for the build
baseline and `/tmp/ctcompile-native-integrated-full-gate.log` for the run.

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

## Extend the existing recovery transaction

Use [Exceptions/Recovery.cpp](../lib/CTNative/Lowering/Exceptions/Recovery.cpp),
which already clones, bounds and structures an acyclic handler CFG. Do not add
a parallel exception-recovery pass. Its current `cloneTail` converts every
`check` into its normal edge, with the explicit requirement that later native
admission prove all discarded exceptional operations nonthrowing.

A direct throwing call needs a different clone plan:

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
