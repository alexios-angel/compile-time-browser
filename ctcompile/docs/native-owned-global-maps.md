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
The setter may take independently proved primitive arguments, including the
live result of its sibling getter. The `set(key)` version below advances
**0/5 -> 5/5**, with `trace=1` and the getter still zero-argument.

```js
var host = {};
(function(factory) {
    host.slot = factory();
})(function() {
    const state = new Map();
    return {
        get() { return state.size; },
        set(key) { state.set(key, 1); return state.size; }
    };
});
host.slot.set(host.slot.get());
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

Before checking any family body, the query discovers and validates every current
method call. A bounded dependency worklist classifies actuals from primitive
source expressions or already completed producer proofs. Every formal must
receive the same primitive tag at every call; the proof retains each live SSA
operand, formal and tag. Distinct strings or numbers need not have the same value.
The body may treat a formal as primitive only after this evidence succeeds.
Source and prepared functions retain exact arities, receiver/callee identity,
and the prepared Map environment before the explicit arguments. Only a complete
body/effect/use census publishes a result tag: `size` is numeric, `has`/`delete`
are boolean, and literal or typed-formal returns retain their category. A
consumer declared before its producer waits for a later worklist pass. No
optimistic tag seeds a circular dependency, and no recursive `propertyCall`
query supplies authority. A `Map.get` can now retain the last local `set`'s
independent payload tag when its key is the same SSA value or equal primitive
constant. Each invocation starts with unknown contents. Every later `set`
replaces the fact; a delete clears it, including a delete with another key.
The bounded proof deliberately forgets an earlier key after any later write.
Only the complete method body and use census publishes its result tag.
Unique initialized global actuals are checked against the full initialization proof.

Native Map preparation separately rederives presence for these live published
reads using its existing instance/key analysis. A proved read uses the Map's
inferred payload type and `map_get_present`; other reads remain nullable.
The host result tag supplies neither the Map schema nor presence annotations.
Every source Map lookup, producing call and consuming argument remains runtime.

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
and typed `std::function` carriers, including `js_num(std::string)` for the
setter and `js_num()` for the getter. Each method captures the same Map owner
by value. Native output links neither the interpreter nor the collector. Escape
analysis still reports global publication as `StoredGlobal`; general global
loads remain external. The new owner does not require a weaker escape verdict.

## Measured seeded-result gate, 2026-09-07

Commit `b2466a0` advances the seeded producer below from **0/5 to 5/5 native**,
retaining Node/interpreter `trace=1`:

```js
get() { state.set(0, 1); return state.get(0); }
// The entry retains host.slot.set(host.slot.get()) and the final getter.
```

The gate passes **40 complete native programs**: fifteen **4/4**, nineteen
**5/5**, and six **6/6**, in explicit/deduced GCC and Clang forms. Five new
producer cases cover the seed, repeated calls, overwrite, a runtime key and
distinct formal actuals. The previously nullable-key specimen is now **4/4**:
its local write independently proves the later lookup before using its result
as a key. The sixth lifetime variant seeds from the current Map size and reads
the new value after every call. Saved and fresh callables retain independent
Maps through 1024 further invocations each, then free them; both forms pass
ASan/UBSan, use-after-scope/return and leak checks.

Seven new contents/presence refusals preserve every source call. Three further
cases establish complete host ownership but refuse unsupported string, boolean
or mixed Map payload carriers. Missing intrinsic identity, stale/fresh forged
presence, reruns and work limits cannot bypass the live proofs.
Source/prepared host proofs complete at **2075/2153** steps; the owner proofs
complete at **5016/4899**. Every smaller budget withholds the complete family.
The seeded native specimen first completes at **5558**, with **30** checked
cutoffs; no natural speculative rollback interval is reached.

The integrated focused gate passes **7/7 CTests in 18.26 seconds**, followed
by the complete program/lifetime gate. Log: `/tmp/ctcompile-map-presence-integrated.log`.
The full generated devbox gate passes **475/475 CTests in 693.86 seconds**,
including **163/163 lit cases**. Log: `/tmp/ctcompile-map-presence-full.log`;
corpus and boundary measurements are recorded in [HANDOFF.md](HANDOFF.md).

## Preceding result gate, 2026-09-07

Commit `c18b94b` passes **34 complete native programs**: fourteen **4/4**,
fifteen **5/5**, and five **6/6**, matching Node, the interpreter, and standalone
explicit/deduced GCC 13 and Clang 18 binaries. Nine new result cases cover the
exact `set(get())` boundary, reversed method declaration order, repeated calls,
local aliases, two mutating actuals, boolean `has`/`delete`, an owning string,
and a typed-formal return. The two-actual witness observes **3**; reversing its
arguments observes **4**. Generated C++ retains every producing call, consuming
SSA operand and final observation in order, and all linked-symbol gates pass.

The five existing lifetime variants below pass unchanged in both C++ forms;
no new runtime carrier was introduced. Nine new result-proof refusals retain
every original call. An additional implicit-undefined result establishes a
complete owner but refuses its unsupported Map key at **0/6 native**. That
control checks the retained prepared calls and their result operands; valid
ownership preparation is allowed to precede a carrier refusal.

Source/prepared result units check every incomplete budget at **4789/4656**,
including changed producer return tags, stale/fresh fingerprints and unknown
returns. Ordinary two-method, three-method and parameter units complete at
**2877/2785**, **6603/6418**, and **2883/2792**. The six source admission
budget specimens complete at **1239/50004/1353/3297/3261/5336**, checking
**31/31/31/29/32/29** cutoffs. None reaches a natural speculative rollback
interval; these numbers do not claim improved analysis performance.

Focused CTests pass **3/3 in 7.50 seconds**, followed by the full native
program/lifetime/refusal gate. Log: `/tmp/ctcompile-map-results-integrated2.log`.
The full frozen generated build passes **475/475 CTests in 674.94 seconds**,
including **163/163 lit cases in 113.29 seconds**. Log:
`/tmp/ctcompile-map-results-full.log`; evidence:
`/tmp/ctcompile-map-results-evidence.json`. [HANDOFF.md](HANDOFF.md) records the
browser baseline and the separate evidence-collector Node-path correction.

## Preceding argument gate, 2026-09-07

`CTNative/native-owned-global-maps.test` covers **25 complete native programs**:
fourteen at **4/4**, ten shared setter/getter variants at **5/5**, and one
three-method variant at **6/6**. They cover publication, mutation, growing keys,
repeated growth, primitive operations, overwrite/deletion, fluent calls, and
boolean `has`/`delete` results used by later mutations. Shared variants check
reads before mutation and repeated calls. Six parameterized variants add string,
number and boolean keys, distinct repeated actuals, a method-local alias, and
two parameters for key and value. All twenty-five match Node, the
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

A fifth lifetime gate retains the typed string setter and getter. It changes
the caller's string buffer after insertion, then supplies long changing keys
to saved and fresh callables through **1024 further mutations each**. The
independent Maps retain their own string contents after root/table release
and expire with the final callable. Both C++ forms pass the same sanitizers.

Thirty source refusals cover mutable captures, non-primitive contents, cycles,
wrong method receivers/arities, detached/escaping methods, Map publication,
replaced intrinsics/prototypes, constructor arguments, additional allocation or
factory invocation, observable receivers/arguments, effects and throws.
Stale/forged contracts, reruns, missing intrinsic identity and incomplete work
retain the source operations and signatures. Three further controls establish
that complete ownership still cannot supply a numeric export for nullable or
boolean results, or a supported nullable Map key.

Five shared-family source refusals cover an uncalled sibling, unknown effects,
Map return, method replacement and detached publication. Ten argument refusals
cover missing/extra/mixed actuals, objects, callbacks, unproved properties,
call-result actuals and both positions of a two-parameter method. They preserve
every current call operand. A freshly fingerprinted forged contract cannot
admit a mixed-tag family.
Proof units cover source/prepared two- and three-method families, every
incomplete work budget, mixed capture stages, live sibling receiver/upvalue
mutations and distinct Map identities. Both stale and freshly fingerprinted
mutations must refuse; restoring the source restores the proof.
Parameterized units additionally check live formal/actual evidence, both
explicit argument positions, environment offsets, initialization order,
duplicate/later global stores, sibling-result recursion and uncalled methods.

The integrated gate measures these current admission budgets:

| Budget specimen | First complete admission | Cutoffs checked |
|---|---:|---:|
| Ordinary getter | 1236 | 31 |
| Sixteen getter calls | 49476 | 30 |
| Growing Map method | 1350 | 32 |
| Shared growing Map methods | 3285 | 29 |
| Parameterized shared setter | 3249 | 33 |

Each checks the sixteen budgets immediately below completion. None naturally
reaches a cutoff where the original proof succeeds and the prepared clone's
proof exhausts. These are failed-attempt preservation checks, not a new
Map-specific speculative rollback measurement. Existing scalar/table rollback
controls remain.

The source/prepared owner units also check every incomplete budget: **2865/2773**
for two methods, **6576/6391** for three methods and **2871/2780** for a
parameterized setter/getter. The expanded independent call census increases
proof work, particularly for the sixteen-call case; no performance improvement
is claimed. The focused gate passes **8/8 CTests** in **14.10 seconds**, followed
by the complete twenty-five-program native execution/lifetime gate. Log:
`/tmp/ctcompile-arguments-integrated-focused4.log`. See [HANDOFF.md](HANDOFF.md)
for the corpus measurements and frozen browser baseline. The full generated
devbox build passes **475/475 CTests** in **652.00 seconds**, including
**163/163 lit cases** in **91.45 seconds**; log:
`/tmp/ctcompile-arguments-full-gate.log`.

## Next boundary

The retained `seeded_earlier_key` adds `state.set(1, 2)` before the producer's
`return state.get(0)`. It remains **0/5 native** with every source call intact
and no owner proof; fresh Node/interpreter runs both produce `trace=1`.
The last-write proof forgets key 0. The next contents increment needs bounded
per-key facts and independent key-disjointness evidence, conservatively
invalidating possibly aliasing writes and deletes. The unseeded
`get() { return state.get(0); }` remains refused. Neither an earlier observed
invocation nor an incomplete family establishes the result.

String, boolean and mixed Map payload carriers remain separate **0/6**
boundaries even after complete host proofs. Current-call proofs cannot authorize
arbitrary future external arguments or establish an export ABI.

Future external callers, a typed export ABI, mutable publication slots,
general realm owners, reentry and throwing provider effects remain separate
obligations. See [the next Bootstrap work](bootstrap-provider-next.md).
