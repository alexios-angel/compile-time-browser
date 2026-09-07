# Next Bootstrap provider proof

Status: design following the measured `599296c` provider-mutation increment.
The stages below are not implemented or measured. Native exception lowering
does not establish the callback effect proof described here.

The current exact CommonJS, browser and browser-with-script-this-fallback
probes each resolve **13 calls** and complete **11 provider summaries**. Those
summaries contain 31 Map reads, five sets, one unsuccessful delete and two
distinct nested Map allocations. Discovery stops at
``unsupported provider path at `ctjs.load_global` (console)``. All 19/19/24
observations agree across Node and the boxed/interpreter differential, including
GC stress. Native admission remains **0/7** in each mode.

[Private Map mutation summaries](native-provider-mutations.md) records the
implementation, provenance and reproduction commands. The checked vendor
fragment is 1,101 bytes, SHA-256
`3eebf5b59691eff3c32478e06fb44a5c9eb554bc045d2ff2e20901813a9f99c8`.
The exact UMD/Data source is retained; the remainder of Bootstrap's factory is
replaced with `return e`. This is a Data prerequisite probe, not complete
Bootstrap initialization.

## Stage A: evaluate the diagnostic arguments

The conflict arm in the current imported IR evaluates these operations in
order: console lookup, error member lookup, Array.from member lookup,
Map.keys lookup and call, Array.from call, indexed extraction, string
concatenation, then the error call. Preserve the saved callee and receiver
while evaluating the argument. The current exact message is:

```text
Bootstrap doesn't allow more than one instance per element. Bound instance: bs.alert.
```

Extend the transaction-local reader with these bounded proofs:

- Resolve console through the current initialized source global and its live
  ordinary own-data `error` slot. Refuse an unknown receiver, accessor or
  replacement that lacks the same proof. The spelling `console.error` grants
  no authority.
- Recognize standard Map.keys and Array.from only with checked intrinsic,
  method, receiver and iterator identities. Initially accept an immediately
  consumed private iterator with no intervening mutation, callback or escape.
  Copy the Map's insertion-ordered keys into a distinct snapshot; replacement,
  deletion and reinsertion retain their existing order semantics. Require the
  indexed entry to exist and be a string for this first diagnostic slice.
- Permit only proved string concatenation for message construction. Object
  coercion, customized iteration and arbitrary Array.from arguments remain
  unsupported. Charge snapshot entries, string work and all traversal against
  the existing budget.

Stop at the actual error call. Stage A should retain 13 resolved entry calls
and 11 completed provider summaries, while naming the later internal boundary.
An incomplete attempt must not publish its read/snapshot facts as a completed
summary. Every runtime lookup, allocation, iteration, concatenation and call
remains in the reusable source method; normal-return facts do not remove
allocation failures or authorize an exceptional continuation.

## Stage B: prove the current recorder's effects

The exact probe installs a source callback that increments `traceErrorCount`
and assigns `traceErrorMessage` after a primitive string comparison. A bounded
path interpreter can prove that particular invocation's effects without
classifying every console as inert.

Bind the proof to the actual source closure, current own-data slot, call,
receiver and evaluated primitive arguments. Initially require a capture-free
callback, a complete supported normal-return path and no effective-this,
new.target, raw-arguments or identity inspection. Support constants, proved
primitive arithmetic/comparison/control and writes to initialized ordinary
scalar global slots whose identities are disjoint from provider, publication
and intrinsic state. Infer the write set from live IR; do not recognize
`trace*` names or trust annotations as an effect contract.

The provider transaction must include the scalar global changes, or invalidate
those facts before later discovery. Commit the provider and global post-state
only after the enclosing Data method completes normally. Preserve the runtime
recorder call and both writes. Repeated invocation needs fresh call identities
and the current scalar values, not a cached first-call result.

Unknown calls, reentry into Data, provider/publication writes, property effects,
mutable captures, throws and unsupported control refuse the whole attempted
summary. Do not continue from the old pre-call Map state after refusal.
`PrefixIdentity.cpp::identitySafeFunction` is not this effect proof: it checks
caller-identity exposure and permits operations such as global stores and
constant throws. C++ exception support likewise cannot prove that a callback
leaves Maps or exports unchanged.

**Unmeasured prediction: 20 resolved entry targets and 18 completed provider
summaries.** This counts one factory, eighteen completed Data calls and the
next boundary call. After the conflicting set, six further primitive Data
calls should complete before
`set(element, "bs.collapse", instance)` reaches the unsupported ordinary
object payload. Measure before adding count assertions. Internal snapshot and
recorder calls need separate accounting; do not inflate the entry-target count
with calls discovered inside a reusable body. Object payload/field identity
and native export ownership remain separate work.

## Acceptance gate

Keep both stages opt-in, preserving default and existing flag behavior. Use
the unchanged three programs and vendor hashes, source function denominator,
runtime method/recorder bodies and observer branches. Require all 19/19/24
Node/interpreter/boxed observations and GC-stress runs, including one error
with the exact message and unchanged original/rejected entries. Report native
admission independently; neither stage supplies a native ownership proof.

Add focused tests for insertion order after replacement/delete/reinsert;
empty/out-of-range snapshots; detached/wrong receivers; customized Array.from,
Map.keys or iterator behavior; a console getter changing publication; callback
replacement; a recorder that removes and reinserts Data; a recorder changing
exports; and a throw after a trace write. These effects must remain at runtime
and refuse post-state discovery in the first implementation. Test repeated
recorder calls and budget exhaustion after tentative snapshot/global work,
plus stale contracts and forged annotations.

Retain all eleven scenarios in
[the exact host audit](../../tools/check/bootstrap-host-contract-audit.py).
Its `1234` callee-order observation and throwing/reentrant sink cases specify
behavior to preserve, including effects outside this proof's supported scope.
The native type/owner requirements in plan parts 24 and 25 remain mandatory;
prefix observations never close future exported callers or external mutation.
