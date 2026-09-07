# Native captured Maps across global publication

The [publication specimen](../test/CTNative/native-export-boundary.js)
admits **4/4 native** with a fingerprinted `host-manifest` that selects
`host.slot`, the numeric `trace` observation and `initial_intrinsics: ["Map"]`.
Without the manifest, or without its explicit standard Map identity, admission
remains **0/4**. The method may now mutate its Map with standard `set`, `get`,
`has` and `delete` operations over primitive contents. A published setter and
getter sharing that Map now advance **0/5 -> 5/5 native**, preserving `trace=1`.
A three-method variant admits **6/6**. Full native Bootstrap Data is unfinished.
The shared specimen stays **0/5** without the manifest or standard Map identity.

```js
var host = {};
(function(factory) {
    host.slot = factory();
})(function() {
    const state = new Map();
    return {
        get() { return state.size; },
        set() { state.set("x", 1); return state.size; }
    };
});
host.slot.set();
var trace = host.slot.get();
```

The compiler proves one ordinary root, one wrapper invocation, one factory,
one immutable capture binding and one standard Map allocation, initially empty.
The returned table has distinct fixed methods, each with a current call using
its actual table receiver. A complete family census follows every closure that
captures the same binding, checks its publication and body, and retains every
Map read and call. All methods must use primitive keys/contents and exact
method receivers and arities. An uncalled or unsafe sibling withholds the
complete owning plan. Separate Map allocations cannot inherit a shared owner.
Repeated capture loads and fluent `set` returns alias the same Map. Publication
and the entire source function chain remain; allocation, mutations, calls and the
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
Every method's plan is checked before lifting any member. Cell unboxing waits
until all closures have acquired their Map environment; source and prepared
capture stages cannot mix. Each changed graph must pass a fresh complete
ownership query. Map preparation may pass only host reads identified by that
live ordinary-root proof; unrelated
host reads still prevent the standard Map identity proof. Final admission
reconstructs ownership again.

Generated C++ reuses the shared root, shared method table, shared Map handle
and `std::function<js_num()>` carriers. Each method captures the same Map owner
by value. Native output links neither the interpreter nor the collector. Escape
analysis still reports global publication as `StoredGlobal`; general global
loads remain external. The new owner does not require a weaker escape verdict.

## Measured gate, 2026-09-07

`CTNative/native-owned-global-maps.test` covers **19 complete native programs**:
fourteen at **4/4**, four shared setter/getter variants at **5/5**, and one
three-method variant at **6/6**. They cover publication, mutation, growing keys,
repeated growth, primitive operations, overwrite/deletion, fluent calls, and
boolean `has`/`delete` results used by later mutations. Shared variants check
reads before mutation and repeated calls. All nineteen match Node, the
interpreter and standalone explicit/deduced GCC 13 and Clang 18 binaries.
Linked-symbol checks reject VM use.

The ordinary, mutating and growing methods pass ASan/UBSan, use-after-scope,
stack-use-after-return and leak checks in both C++ forms. The harness retains
owner, table and callable independently, releases the global, churns allocations
and invokes the callable after owner/table release. Weak witnesses observe the
actual Map allocation without retaining it. Reentry makes a distinct Map; each
Map expires when its last callable owner is released. The growing method uses
`state.set(state.size, 1)` on every call: saved and fresh environments retain
independent changing sizes through **1024 further invocations each**. A constant
result or a startup summary cannot satisfy this witness.

The shared growing-Map variant adds a fourth lifetime gate in both C++ forms.
It saves setter and getter independently, releases root/table/global owners,
churns **4096 allocations**, then reenters to create a distinct Map. Mutation
through the saved setter is visible to the saved getter and independent of the
fresh entry through **1024 further calls**. Releasing the setter retains the
Map through the getter; copying and releasing the last getter expires it.
The fresh getter likewise survives its root/table and frees its Map on release.

Thirty source refusals cover mutable captures, non-primitive contents, cycles,
wrong method receivers/arities, detached/escaping methods, Map publication,
replaced intrinsics/prototypes, constructor arguments, additional allocation or
factory invocation, observable receivers/arguments, effects and throws.
Stale/forged contracts, reruns, missing intrinsic identity and incomplete work
retain the source operations and signatures. Three further controls establish
that complete ownership still cannot supply a numeric export for nullable or
boolean results, or a supported nullable Map key.

Six shared-family source refusals cover an uncalled sibling, unknown effects,
Map return, method replacement, detached publication and a parameterized setter.
Proof units cover source/prepared two- and three-method families, every
incomplete work budget, mixed capture stages, live sibling receiver/upvalue
mutations and distinct Map identities. Both stale and freshly fingerprinted
mutations must refuse; restoring the source restores the proof.

The integrated gate measures these current admission budgets:

| Budget specimen | First complete admission | Cutoffs checked |
|---|---:|---:|
| Ordinary getter | 1031 | 32 |
| Sixteen getter calls | 8516 | 30 |
| Growing Map method | 1132 | 31 |
| Shared growing Map methods | 1906 | 33 |

Each checks the sixteen budgets immediately below completion. None naturally
reaches a cutoff where the original proof succeeds and the prepared clone's
proof exhausts. These are failed-attempt preservation checks, not a new
Map-specific speculative rollback measurement. Existing scalar/table rollback
controls remain.

The source/prepared owner units also check every incomplete budget: **1582/1521**
for two methods and **2367/2279** for three methods. The focused gate passes
**8/8 CTests** in **79.86 seconds**. The full generated devbox build and gate
pass **474/474 CTests** in **622.05 seconds**, including the nineteen-program
gate among **163/163 lit cases**. Logs are
`/tmp/ctcompile-native-integrated-focused2.log` and
`/tmp/ctcompile-native-integrated-full-gate.log`. See [HANDOFF.md](HANDOFF.md)
for the corpus measurements and frozen browser baseline.

## Next boundary

Changing the setter to `set(key) { state.set(key, 1); return state.size; }`
and calling `host.slot.set("x")` retains five functions and Node/interpreter
`trace=1`, but measures **0/5 native**. This exact source boundary is retained
in the gate. Extend live callable arities and per-method primitive parameter
proofs over every current actual call. The prepared setter needs the Map
environment plus its explicit key argument, while the getter stays zero-argument.
Existing capture lifting already transports the environment before explicit
arguments; it needs a complete new proof, not a replacement owner carrier.
Current-call proofs alone cannot authorize arbitrary future external arguments.

Separately, `state.get(1)` used as a later key still infers a nullable numeric
key, even after a local `set(1, 3)`. Its numeric observation is `trace=1` in both
references, but native admission remains **0/4** with complete ownership and a
named unsupported-carrier refusal. It needs presence/type evidence, not a
weaker ownership check.

Future external callers, a typed export ABI, mutable publication slots,
general realm owners, reentry and throwing provider effects remain separate
obligations. See [the next Bootstrap work](bootstrap-provider-next.md).
