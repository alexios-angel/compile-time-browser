# Native captured Maps across global publication

The four-function [publication specimen](../test/CTNative/native-export-boundary.js)
now admits **4/4 native** with a fingerprinted `host-manifest` that selects
`host.slot`, the numeric `trace` observation and `initial_intrinsics: ["Map"]`.
Without the manifest, or without its explicit standard Map identity, admission
remains **0/4**. Full native Bootstrap Data is still unfinished.

```js
var host = {};
(function(factory) {
    host.slot = factory();
})(function() {
    const state = new Map();
    return {get() { return state.size; }};
});
var trace = host.slot.get();
```

The compiler proves one ordinary root, one wrapper invocation, one factory,
one immutable capture binding and one empty standard Map allocation. The
returned table has one fixed getter, and every current getter call uses its
actual table receiver. Publication and all four source functions remain;
allocation, calls, Map reads and the observation execute at runtime.

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

## Measured gate

`CTNative/native-owned-global-maps.test` covers six programs: ordinary,
already-resolved getter, stale store/field markers with fresh fingerprints,
repeated calls and an ordinary root named `window`. Each retains four source
functions and admits **4/4**. Node, the interpreter and explicit/deduced GCC 13
and Clang 18 binaries agree on `trace=0`; linked-symbol checks reject VM use.

Both C++ forms pass ASan/UBSan, use-after-scope, stack-use-after-return and leak
checks. The harness retains owner, table and callable independently, releases
the global, churns allocations and invokes the callable after owner and table
release. Weak witnesses observe the actual Map allocation without retaining it.
Reentry makes a distinct Map; each Map expires when its last callable owner is
released. A leaked Map or a getter that merely returns constant zero cannot
satisfy these lifetime assertions.

Twenty-two source refusals cover mutated captures, Map operations outside the
size-only body, separate publication, replaced intrinsics/prototypes, constructor
arguments, additional allocation/invocation, detached values, receiver/argument
use, effects and throws. Stale/forged contracts, reruns, missing intrinsic
identity and work limits preserve the source boundary. Budget tests find the
first complete admission cutoff for ordinary and sixteen-call specimens,
checking source operations and signatures on each failed attempt.

The complete lit suite passes **161/161 cases** through the focused CTest gate
in **49.27 seconds**. Final whole-monorepo validation is recorded in
[HANDOFF.md](HANDOFF.md).

## Next boundary

The getter proof permits only the captured Map's `size` read. A body that first
executes `state.set("x", 1)` still refuses. Extend the complete live callable
proof to supported standard Map operations and their effects before broadening
publication to Bootstrap's multi-method Data table and its arguments/results.
The existing native Map/type machinery can be reused after those source proofs
succeed; a completed startup summary supplies no authority for later calls.

Future external callers, a typed export ABI, mutable publication slots,
general realm owners, reentry and throwing provider effects remain separate
obligations. See [the next Bootstrap work](bootstrap-provider-next.md).
