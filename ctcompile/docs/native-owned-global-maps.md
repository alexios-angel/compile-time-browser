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
query supplies authority. A `Map.get` can retain a local `set`'s independent
payload tag when its key is the same SSA value or a SameValueZero-equal primitive
constant. Each invocation starts with unknown contents. Bounded per-key facts
preserve definite presence and an independently optional payload tag. A possibly
aliasing `set` preserves presence and joins the old and new payload tags: equal
proved tags survive, while differing or unproved tags become unknown. A later
possible write cannot recover an unknown tag; an exact-key write replaces it.
A possibly aliasing `delete` removes definite membership while retaining the
payload tag valid whenever present. Independently disjoint keys preserve both. Different primitive tags are disjoint without
coercion; different SSA names of the same primitive type may alias. Both zero
encodings and every NaN payload denote the same Map key. Each set also records
its own key and payload independently of any earlier join. Every fact comparison
spends work budget.
Only the complete method body and use census publishes its result tag.
Unique initialized global actuals are checked against the full initialization proof.

Native Map preparation separately rederives presence for these live published
reads using its existing instance/key analysis. A proved read uses the Map's
inferred payload type and `map_get_present`; other reads remain nullable.
The host result tag supplies neither the Map schema nor presence annotations.
The presence analysis uses the same primitive-key comparison for direct deletes,
invalidating both present-key facts and cached `has` observations across the
schema family unless their keys are independently distinct. Transitive call
summaries still invalidate the entire affected family conservatively.
Every source Map lookup, producing call and consuming argument remains runtime.

This is an ownership/effects proof. Native admission must still prove supported
key, value and result carriers. The gate includes homogeneous numeric, boolean
and owning-string values with numeric, string or boolean keys. A primitive result
alone is insufficient; mixed stored payloads still require a separate carrier proof.

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

## Measured possible-alias join gate, 2026-09-07

The retained producer below advances **0/5 -> 5/5 native**, preserving
Node/interpreter `trace=1` through the same published `set(get())` entry:

```js
get() { state.set(0, 1); state.set(state.size, 2); return state.get(0); }
```

The gate passes **55 complete programs**: fifteen **4/4**, thirty-three **5/5**
and seven **6/6**, with matching Node, interpreter and standalone explicit/deduced
GCC/Clang execution. Eight new cases cover possible and actual overwrites,
saved runtime keys, repeated writes, reseeding, runtime payloads and distinct
formal arguments. The actual-overwrite witness produces **2**; replacing the
getter with its old payload produces **1**. The generated calls and Map lookups
remain runtime operations. All six existing ownership/lifetime variants pass
ASan/UBSan, use-after-scope/return and leak checks; linked-symbol gates pass.
Thirteen seeded proof refusals retain every call. Four separate carrier
refusals include exact-key reseeding after an incompatible possible write: its
host result tag recovers, while the mixed Map schema still refuses native code.

Source/prepared host join proofs complete at **2172/2258** steps; owner proofs
at **5101/4992**. Repeated owner joins complete at **5186/5085**. Every smaller
budget withholds the entire proof. First source native completion is **5774**
for the dynamic write and **16339** for the parameterized variant, with **31/32**
checked cutoffs and no natural speculative rollback interval. These are work
limits, not speedups. Ownership CTest passes in **18.21 seconds**; host CTest
passes in **1.64 seconds**, followed by the complete native program gate.
Logs: `/tmp/ctcompile-map-joins-proof.log` and
`/tmp/ctcompile-map-joins-focused.log`. The full generated gate is recorded in
[HANDOFF.md](HANDOFF.md) when complete.

## Preceding per-key gate, 2026-09-07

The producer below advances **0/5 -> 5/5 native**, preserving Node/interpreter
`trace=1` through `host.slot.set(host.slot.get())` and the final getter:

```js
get() { state.set(0, 1); state.set(1, 2); return state.get(0); }
```

The complete native gate passes **47 programs**: fifteen **4/4**, twenty-six
**5/5**, and six **6/6**, under explicit/deduced GCC and Clang output. Seven new
cases cover the earlier key, a disjoint delete, overwrites, reseeding, nine live
keys, and string/boolean keys. All producing lookups, consuming operands and
runtime calls remain. The existing six lifetime variants still pass ASan/UBSan,
use-after-scope/return and leak checks. Nine seeded proof refusals preserve every
source call; the three unsupported payload-carrier boundaries remain separate.

The per-key source/prepared host proofs complete at **2145/2230** steps; the
owner proofs at **5072/4962**. Disjoint-delete owner proofs complete at
**5116/5007**. Every smaller budget withholds the complete proof. Source native
admission first completes at **5721** for the earlier key and **5700** for the
disjoint delete, with **30/31** checked cutoffs and no natural speculative
rollback interval. These are work-limit measurements, not performance claims.
The initial focused CTest gate passes **2/2 in 14.16 seconds**, followed by all
47 native programs; log: `/tmp/ctcompile-map-keyfacts-focused2.log`.
The final frozen generated gate passes **475/475 CTests in 720.98 seconds**,
including **163/163 lit cases**; log: `/tmp/ctcompile-map-keyfacts-full.log`.
Fresh component counts and the browser baseline are in [HANDOFF.md](HANDOFF.md).

## Preceding seeded-result gate, 2026-09-07

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

## Nonempty size snapshots, 2026-09-08

The retained `seeded_dynamic_delete` uses `state.delete(state.size)` before
the producer's `return state.get(0)`. It advances **0/5 -> 5/5 native**,
retaining Node/interpreter `trace=1` and every runtime seed, size read, delete
and lookup. Both the host result proof and native presence analysis independently
establish `size >= 1` from a definite entry in the exact runtime Map. That
immutable numeric snapshot is distinct from zero, negative/subunit literals and
NaN. Later mutation cannot change the saved number. Different positive keys or
different size SSA values alone do not establish disjointness.

The gate passes **58 complete programs**, including three new size cases and
three size-key refusals with discriminating observations. Node/interpreter and
explicit/deduced GCC/Clang agree; all six existing sanitizer lifetime variants
pass. Saved-size execution returns 2, whereas replacing the saved number with
the current size returns 3. Zero-size, equal-positive-key and equal-snapshot
refusals preserve every call under fresh/stale forged markers and reruns.
Existing incompatible-overwrite tests now seed key 1 so they still exercise
actual possible aliasing. Source/prepared host proofs complete at **2212/2299**
steps and withhold the entire family at every smaller budget. Native completion
is **5753** for dynamic delete and **8607** for the saved snapshot, with **31/29**
checked cutoffs. No natural speculative rollback interval was observed.

The new independent native-presence lit test passes its three positive and
seven refusal programs, covering `has` branches, saved reads, initial/emptied
Maps, different runtime instances and recursive clearing. The focused host/owner
CTests pass. Logs: `/tmp/ctcompile-size-focused.log`,
`/tmp/ctcompile-size-native.log`, `/tmp/ctcompile-size-lit.log`.
The full combined gate is recorded in [HANDOFF.md](HANDOFF.md).

## Distinct-key size bounds, 2026-09-08

The `seeded_size_two_entries` producer seeds keys 0 and 1, deletes
`state.size`, then returns `state.get(1)`. It now advances **0/5 -> 5/5
native**, preserving Node/interpreter **`trace=2`**. Both analyses independently
construct a pairwise-distinct subset of definite entries in the exact runtime
Map. A size snapshot is at least the subset's cardinality; subsequent mutations
do not change that saved number. Distinct SSA names alone never count twice.
SameValueZero still equates signed zeros and all NaN payloads.

Each read examines at most **64 candidates**. This deliberately yields a lower
bound, not an exact size; keys outside the examined subset cannot increase it.
Host analysis charges every examined candidate and comparison to its existing
shared work budget. Native presence filters by exact runtime instance, preserves
known immutable snapshots across mutation, meets branch facts conservatively
and discards loop-carried assumptions. Neither analysis consumes a forged size
annotation. Source/prepared host proofs complete at **2278/2372 steps**, with
no callable/property proof at any smaller budget. The 64/65-candidate controls,
late seed/read/delete mutations and signed-zero/NaN duplicate controls pass.

The gate passes **63 complete native programs**, including eight size programs
and ten size-key refusals, with Node/interpreter and explicit/deduced GCC/Clang
agreement and no interpreter symbols. All six existing sanitizer lifetime
variants pass. New saved-bound observations distinguish rereading size after
growth and after deleting all seeded keys; deletion controls distinguish real
mutations from no-ops. Runtime calls, lookups and result operands remain intact.
First complete native admission is **5931** for two entries and **9391** for the
saved/emptied Map, each with **30** checked cutoffs and no natural speculative
rollback interval. The combined focused CTest gate passes **12/12 in 28.45
seconds**. The independent presence lit gate passes **seven positives and
fourteen refusals**, including `has` branches, different instances and loop
invalidation. Logs: `/tmp/ctcompile-cardinality-focused2.log` and
`/tmp/ctcompile-cardinality-lit2.log`.

## Homogeneous boolean and string payloads, 2026-09-08

The existing `result_seeded_bool` and `result_seeded_string` programs advance
**0/6 -> 6/6 native**, preserving Node/interpreter **`trace=2`** and every
producing call, Map read and consuming argument. Host owner/result proofs stay
independent of native Map inference. Storage uses the existing
`std::shared_ptr<ctnative::map_storage<K, V>>`, with `V` equal to `bool` or
`std::string`; construction uses `make_map<K, V>()`. No interpreter, collector
or new value model is involved.

Ordinary boolean reads use the existing optional scalar carrier; ordinary
string reads use the existing nullable owning string carrier. Proved-present
reads return the exact stored type by value. `false` and empty strings retain
their tags, and missing reads remain undefined. String value snapshots copy
into owning vectors after the existing immediate `Array.from` proof. Boolean
value snapshots and mixed stored payloads still refuse.

The published gate passes **69 complete programs**, including six payload
programs and all **seven sanitizer lifetime variants** in explicit/deduced
forms. False/empty and overwritten-payload witnesses return **1** while their
blinded controls return **2**. Saved strings survive source overwrite/deletion,
Map destruction, caller-buffer mutation and independent reentry. Both compilers,
Node and the interpreter agree; linked native binaries have no Script symbols.
Two new deleted-payload refusals preserve every original call under fresh/stale
forgeries and reruns. Three mixed-carrier refusals retain their complete owner
proof and the prepared producing/consuming call edge.

Native admission first completes at **7474** steps for each original boolean
and string program and **8008** for the saved-string program, checking
**32/32/30** cutoffs respectively. No natural speculative rollback interval was
observed. Log: `/tmp/ctcompile-payloads-native.log` (its initial local Map lit
attempt failed only a refusal harness's annotation comparison; the published
gate itself completed successfully). The current full gate and final local
Map results are recorded in [HANDOFF.md](HANDOFF.md).

## Closed mixed Map storage, 2026-09-08

The preceding `result_seeded_mixed_contents`, `result_seeded_join_reseed` and
`result_seeded_bool_string_contents` boundary advances **0/6 -> 6/6 native**.
Node/interpreter traces remain **2/3/2**, and the generated programs retain all
**9/10/9 calls**. Complete host owner/result proofs remain independent of the
native storage proof. Exact Bool/Number and Bool/String schemas now use
`std::variant<bool, double>` or `std::variant<bool, std::string>` for keys or
payloads. False and numeric zero remain distinct; each numeric alternative
preserves SameValueZero. A read's exact result never erases another stored type.

The must-analysis derives each mixed read's payload type from definite literal
writes, separately from membership. A same-tag branch join preserves the type;
differing tags, possible overwrites and callee writes invalidate it. `has`
establishes presence only. The type is seeded before monotone inference can
widen dependent keys or results. Emission constructs the exact alternative and
copies a proved read's scalar result; saved strings keep their owning bytes.

The published gate passes **76 complete programs**, seven mixed programs and
all **eight sanitizer lifetime variants** in explicit/deduced forms. Both
compilers agree with Node/interpreter and emit no Script symbols. Mixed
false/zero keys, empty strings, false results and saved strings have distinct
blinded controls. Two deleted-result refusals retain all calls under forged
presence/type facts and reruns. Number/String storage stays refused.

Native admission first completes at **7658/7910/7658** steps for the preceding
three specimens and **8367** for the mixed saved-string specimen, checking
**30/30/30/31** cutoffs. No natural speculative rollback interval was observed.
Log: `/tmp/ctcompile-mixed-native2.log`. The separate local representation gate
passes nine observations under both storage implementations and seven mixed-read
refusals. Full gate results are recorded in [HANDOFF.md](HANDOFF.md).

## Saved scalar reads through writes, 2026-09-08

Commit `d9a4b04` advances the preceding `saved_read_write` from **0/6 to 6/6
native** in both optimization modes, with Node/interpreter **`trace=1`** and
all **twelve calls preserved**. The independent native must-analysis retains
scalar read results separately from current Map contents. Overwriting or
removing the source entry does not change a saved Boolean, Number or owning
String. Writing that scalar to another entry establishes its exact payload
without removing any stored alternative from the schema. Possible aliasing
writes and callee effects still invalidate mutable entry facts; branch joins
intersect the saved SSA facts. Input annotations supply neither fact.

The published gate passes **83 complete programs**, including seven new saved
chains and all **nine sanitizer lifetime variants**. Missing first or second
reads and deleted results refuse in both modes under valid Bool/String forged
markers, stale manifests and reruns, preserving every source call. The saved
String chain survives overwrite/delete before the second write, another
read/overwrite/delete before return and final Map release. Native budget probes
complete at **8150/8340/8334/8871**, with **30/32/32/32** cutoffs checked.
Log: `/tmp/ctcompile-saved-gate2.log`. The local mixed gate passes **21
observations and ten refusals**, and seven targeted lit cases pass. Combined
focused CTest passes **12/12 in 29.05 seconds**. The full generated build
succeeds, with **372/372 compiler CTests** and **165/165 lit cases** passing;
overall **512/517** leaves only five existing browser failures. Final full-gate
evidence is in [HANDOFF.md](HANDOFF.md).

## Saved scalar values across conditionals, 2026-09-08

Commit `53b44b9` advances `saved_join` from **0/6 to 6/6 native** in both modes,
with Node/interpreter **`trace=3`** and all **sixteen calls preserved**. Its getter
seeds two String entries, selects
`flag ? state.get('other') : state.get('')`, then performs the saved
write/read/delete chain. Replacing the selection with the empty String read
gives **2** and remains **6/6**.

`HostContract/CapturedMapBody.cpp` proves both arms of structured `scf.if`,
including constant predicates and the implicit unchanged arm of a zero-result
conditional without `else`. It intersects mutable contents and preserves a
selected scalar tag only when both yields independently establish that tag.
The owner census still checks every use and actual argument. Factory and
publication execution remain unconditional; raw multi-block functions,
unsupported effects, Map/callee yields and nesting beyond 32 levels refuse.
Native preparation collects selected saved-read candidates before inference,
but only independent presence/payload analysis establishes their scalar types.

The gate passes **89 complete programs**, six new conditional programs, four
missing/deleted/mixed-tag refusals and all **ten lifetime sanitizer variants**.
It checks both optimization modes, GCC/Clang explicit/deduced output, forged
markers, reruns and fifteen discriminating source changes. The long String
case exercises only `false` during startup; the saved C++ getter later accepts
both flags and returns owning strings after source overwrite/deletion and
final Map release. Native budgets first complete at
**17934/18476/18476/10166**, with **32/31/31/31** cutoffs checked.
The local mixed gate passes **27 observations and twelve refusals** across both
storage implementations, and all seven targeted lit cases pass in **28.11 s**.
Host units cover **25 rows each in source and prepared forms**, all
**2501/2626** incomplete budgets and live forged-marker edits. Logs:
`/tmp/ctcompile-conditional-native.log` and
`/tmp/ctcompile-conditional-checkpoint3.log`. Full results are in
[HANDOFF.md](HANDOFF.md).

## Guarded saved reads after conditional deletion, 2026-09-08

Commit `677714b` advances the preceding `guarded_saved_read` from **0/6 to 6/6
native** in both optimization modes. Its conditional deletion followed by
`state.has('other') ? state.get('other') : state.get('')` preserves all
**eighteen calls** and Node/interpreter **`trace=2`**. The deletion-free and
straight-line empty-key controls remain **6/6**, with traces **3** and **1**.

Host and native analyses keep definite membership separate from a payload tag
valid whenever the key is present. Deletion cannot change a surviving payload;
a live `has` on the same Map/key can restore membership. Neither fact supplies
the other. Joins intersect record keys and equal tags and require membership on
both paths. An untracked key remains unknown prior contents, not an absent key.
Possible-alias writes still join payload tags; exact writes replace them. Deleted
records cannot inflate size bounds. Has observations are copied per arm and
invalidated by possibly aliasing deletion. Every arm is checked and all host
snapshot, join and guard work is budgeted. Native facts remain independent of
the host result tag, storage schema and input annotations.

The published gate passes **95 complete programs** and **eleven lifetime
sanitizer variants**. Six new guarded programs cover String, Boolean and Number
results and both flags; eight refusal families cover missing tags, wrong keys or
Maps, stale observations, mutation within the guarded arm, disagreeing tags and
literal predicates. Both optimization modes, GCC/Clang explicit/deduced C++,
forged markers, reruns and sixteen discriminating source mutations pass. A long
String getter sees only false at startup; saved native callables later use both
flags and keep owning strings through overwrite/deletion and final Map release.
The four new native budgets first complete at **18690/19232/19232/10792**, with
**29/31/31/29** cutoffs and no natural speculative rollback interval.

Host units pass **44 rows per source/prepared form**, every **2866/3004** guarded
budget and **2504/2629** conditional budget, exact endpoints and live forged
read/guard edits. Local mixed Maps pass **35 observations and seventeen
refusals** across both storage layouts. Seven targeted lit cases pass in
**28.21 seconds**; initial focused CTest passes **12/12 in 30.69 seconds**.
Log: `/tmp/ctcompile-guard-focused.log`. The combined object-copy gate and full
generated build are recorded in [HANDOFF.md](HANDOFF.md).

## Scalar short-circuit results, 2026-09-08

Commit `58decfe` advances the preceding `shortcircuit_same_tag` from **0/6 to
6/6 native** in both modes, retaining all **eighteen calls**, three getter
conditionals and Node/interpreter **`trace=2`**. The getter selects
`(state.has('other') && state.get('other')) || state.get('')` after conditional
deletion, then keeps the saved write/read/delete chain. Its intermediate
false/String result now carries independent primitive alternatives partitioned
by truthiness. The exact tested SSA value loses impossible alternatives within
each arm; this does not omit any structural arm or its effects. Empty String,
false, zero, signed zero and NaN retain JavaScript fallback behavior. Unknown
values remain unknown; unrelated conditions do not refine a saved result.

Host and native proofs use the same semantic set representation independently.
Map membership, payload tags and saved scalar alternatives remain separate.
The native write proof rederives `ctnative.map_write_type` from the live body,
never from its input attribute, a host result tag or the storage schema. A wider
SCF temporary can therefore supply an independently proved exact scalar write.
Bool/String temporaries use owning `std::variant<bool, std::string>` with a
plain truthiness visitor and copied `std::get` extraction. Numeric intermediates
reuse existing optional scalar carriers. General union function signatures,
returns, captures, fields and coercions remain refused.

The gate passes **103 complete programs**, eight new scalar short-circuit
programs, nine new guard/effect/tag refusals and all **twelve lifetime sanitizer
families**. It checks both optimization modes, explicit/deduced GCC/Clang,
Node/interpreter, all original calls and branches, fresh/stale forged markers,
reruns and **24** discriminating source mutations. Empty/false/zero witnesses
distinguish `||` from a ternary. The long String getter sees only false at
startup; saved native callables later use both flags, preserve both owning
results through overwrite/deletion and independent reentry, and survive final
Map release. First complete budgets for the four new probes are
**19555/20373/10371/11606**, with **31 cutoffs each** and no natural speculative
rollback interval. Log: `/tmp/ctcompile-shortcircuit-native.log`.

Host units pass **82 rows each in source/prepared form**, all **3356/3494**
short-circuit budget cutoffs, **3031/3169** guarded and **2567/2692** conditional
cutoffs, exact endpoints and live forged read/key/condition/yield mutations.
Local mixed Maps pass **49 observations and 21 refusals** across both storage
layouts, including inverted falsy refinement and a standalone local temporary.
All seven targeted lit cases pass in **28.51 seconds**. Focused CTest passes
**12/12 in 31.25 seconds**. Homebrew clang-format **22.1.8** passes **744 files**.
The full generated gate is running; final results belong in [HANDOFF.md](HANDOFF.md).

## Next boundary

Bootstrap's exact getter uses `(has && get) || null`. The short-circuit shape
is now proved for scalar results. Add only `|| null` to the accepted getter's
return to isolate its result contract from nullable stored payloads:

```js
var host = {};
(function(factory) {
    host.slot = factory();
})(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        get(flag) {
            state.set('', '');
            state.set('other', 'future');
            if (flag) { state.delete('other'); }
            const saved = (state.has('other') && state.get('other')) || state.get('');
            state.set(false, true);
            state.set(false, saved);
            const result = state.get(false);
            state.delete(false);
            return result || null;
        },
        set(key) { state.set(key, true); return state.size; }
    };
});
host.slot.set(host.slot.get(false));
host.slot.set(host.slot.get(true));
var trace = host.slot.size();
```

Local Node gives **`trace=3`**. Fresh devbox measurements are queued for this
source, a nullable ternary, a consumer that normalizes `key || 'missing'`,
a directly stored `(has && get) || null` value and object payloads. The
short-circuit and ternary controls give Node **2**. These new measurements must
not be confused with the preceding guarded-ternary witnesses. Source is frozen
in `/tmp/ctcompile-shortcircuit-next-sources/`; pending evidence is
`/tmp/ctcompile-shortcircuit-boundary.json`.

Current-call proofs cannot authorize arbitrary future external arguments or
establish an export ABI. Future external callers, mutable publication slots,
general realm owners, reentry and throwing provider effects remain separate
obligations. Keep observer calls as standalone statements; arithmetic on their
results tests a separate boundary. See [the next Bootstrap work](bootstrap-provider-next.md).
