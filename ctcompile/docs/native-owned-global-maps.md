# Native captured Maps across global publication

The four-function [publication specimen](../test/CTNative/native-export-boundary.js)
now admits **4/4 native** with a fingerprinted `host-manifest` that selects
`host.slot`, the numeric `trace` observation and `initial_intrinsics: ["Map"]`.
Without the manifest, or without its explicit standard Map identity, admission
remains **0/4**. The method may now mutate its Map with standard `set`, `get`,
`has` and `delete` operations over primitive contents. Full native Bootstrap
Data is still unfinished.

```js
var host = {};
(function(factory) {
    host.slot = factory();
})(function() {
    const state = new Map();
    return {get() { state.set("x", 1); return state.size; }};
});
var trace = host.slot.get();
```

The compiler proves one ordinary root, one wrapper invocation, one factory,
one immutable capture binding and one standard Map allocation, initially empty.
The returned table has one fixed method, and every current call uses its actual
table receiver. A complete body census retains every Map read and call, permits
only primitive keys/contents and checks exact method receivers and arities.
Repeated capture loads and fluent `set` returns alias the same Map. Publication
and all four source functions remain; allocation, mutations, calls and the
observation execute at runtime. No previous invocation supplies a later result.

This is an ownership/effects proof. Native admission must still prove supported
key, value and result carriers; the gate uses numeric values and numeric, string
or boolean keys. A primitive result alone is insufficient.

The imported wrapper calls `factory()` indirectly. The host query follows
argument 3 of the entry's sole direct wrapper invocation to the exact supplied
uncaptured closure. It checks every transport use, source-program provenance,
argument count and receiver before following that call's return. This live
source proof succeeds before native preparation starts. A unit containing the
actual public, framed importer shape guards this boundary alongside the
previous direct-call specimens.

Preparation validates the supplied fingerprint, then works on a disposable
clone. Existing callback specialization, closure lifting and immutable-cell
unboxing connect the Map to a typed owning environment. They preserve the
wrapper/factory chain and the getter's real receiver. Native facts are rebuilt;
input annotations cannot erase a field initialization or observation store.
Each changed graph must pass a fresh complete ownership query. Map preparation
may pass only host reads identified by that live ordinary-root proof; unrelated
host reads still prevent the standard Map identity proof. Final admission
reconstructs ownership again.

Generated C++ reuses the shared root, shared method table, shared Map handle
and `std::function<js_num()>` carriers. The getter captures the Map owner by
value. Native output links neither the interpreter nor the collector. Escape
analysis still reports global publication as `StoredGlobal`; general global
loads remain external. The new owner does not require a weaker escape verdict.

## Measured gate, 2026-09-07

`CTNative/native-owned-global-maps.test` covers **14 programs at 4/4 native**:
the six previous publication variants plus mutation, growing keys, repeated
growth, primitive operations, overwrite/deletion, fluent calls, and boolean
`has`/`delete` results used by later mutations. All fourteen match Node, the
interpreter and standalone explicit/deduced GCC 13 and Clang 18 binaries.
Linked-symbol checks reject VM use. The new mutation specimen advances
**0/4 -> 4/4**, preserving `trace=1`.

The ordinary, mutating and growing methods pass ASan/UBSan, use-after-scope,
stack-use-after-return and leak checks in both C++ forms. The harness retains
owner, table and callable independently, releases the global, churns allocations
and invokes the callable after owner/table release. Weak witnesses observe the
actual Map allocation without retaining it. Reentry makes a distinct Map; each
Map expires when its last callable owner is released. The growing method uses
`state.set(state.size, 1)` on every call: saved and fresh environments retain
independent changing sizes through **1024 further invocations each**. A constant
result or a startup summary cannot satisfy this witness.

Thirty source refusals cover mutable captures, non-primitive contents, cycles,
wrong method receivers/arities, detached/escaping methods, Map publication,
replaced intrinsics/prototypes, constructor arguments, additional allocation or
factory invocation, observable receivers/arguments, effects and throws.
Stale/forged contracts, reruns, missing intrinsic identity and incomplete work
retain the source operations and signatures. Three further controls establish
that complete ownership still cannot supply a numeric export for nullable or
boolean results, or a supported nullable Map key.

| Budget specimen | First complete admission | Cutoffs checked |
|---|---:|---:|
| Ordinary getter | 1012 | 31 |
| Sixteen getter calls | 8257 | 31 |
| Growing Map method | 1113 | 32 |

Each checks the sixteen budgets immediately below completion. None naturally
reaches a cutoff where the original proof succeeds and the prepared clone's
proof exhausts. These are failed-attempt preservation checks, not a new
Map-specific speculative rollback measurement. Existing scalar/table rollback
controls remain.

The focused devbox build and six ownership/host/type/escape CTests pass in
**1.37 seconds**. The standalone gate passes all fourteen programs, three
lifetime variants, thirty source refusals and three carrier refusals. Logs are
`/tmp/ctcompile-map-effects-recovery-focused3.log`. See [HANDOFF.md](HANDOFF.md)
for the full-session gate and corpus measurements.

## Next boundary

A table with both `set()` and `get()` methods sharing the captured Map measures
**0/5 native**. Node and the interpreter both produce `trace=1` after the setter
and getter run. This five-function specimen is retained in the gate. The live
capture census currently permits the cell to feed only the selected closure;
the owning source graph also requires one method and four functions. Extend
both proofs to all methods and their shared owner before broadening to
Bootstrap Data's method arguments/results.

Separately, `state.get(1)` used as a later key still infers a nullable numeric
key, even after a local `set(1, 3)`. Its numeric observation is `trace=1` in both
references, but native admission remains **0/4** with complete ownership and a
named unsupported-carrier refusal. It needs presence/type evidence, not a
weaker ownership check.

Future external callers, a typed export ABI, mutable publication slots,
general realm owners, reentry and throwing provider effects remain separate
obligations. See [the next Bootstrap work](bootstrap-provider-next.md).
