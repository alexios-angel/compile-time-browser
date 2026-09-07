# Native ownership for an exported getter

The exported constant-getter specimen now admits **3/3 functions** with explicit
`--ctnative-lower-to-emitc="host-manifest=driver.json"`. Its ordinary global root,
table field and stored callable use owning native C++ carriers. Without the
manifest it remains **1/3 native**.

```js
var host = {};
function make() { return {get() { return 42; }}; }
host.slot = make();
var trace = host.slot.get();
```

Prepare with `--ctjs-resolve-globals --ctjs-lift-to-scf` before creating the
fingerprinted manifest. The driver file remains unchanged. Host callable evidence is described in
[the current getter proof](native-host-callables.md).

`OwnedGlobalRoots` now returns an optional `OwnedGlobalMethodTable` alongside
the root. It identifies the actual factory call, source table allocation,
method initialization, stored closure, target function and every checked
invocation. The ordinary root's operation index still covers only its own
allocation, publication and field accesses. A table or call cannot acquire
numeric-global admission by appearing in that index.

This tier requires exactly three straight-line source functions, one ordinary
root, one factory invocation, one fresh returned table and one fixed method.
The factory must initialize the method before returning the table. Root loads
follow publication; table reads follow the sole field initialization. Every
owner/table use belongs to that graph. Extra publications, allocations,
factory invocations, schema extension and field replacement refuse it.
The host proof additionally requires an uncaptured literal-return getter,
matching receiver, no explicit arguments and no observed implicit arguments.

The query shares `host-max-steps` with complete host analysis. Partial owner or
callable plans never escape a refusal or exhausted budget. Rebuild it after
semantic changes; forged diagnostic attributes cannot replace the query.
Escape analysis still reports `StoredGlobal`, and general global loads remain
external. The proof does not establish an external typed export ABI.

The dedicated unit checks the indirect getter, an already resolved direct
getter and repeated calls on the same table. It exercises source mutation,
detached table/callable publication, reordered initialization, extra factory
invocations, receiver observation, forged reports and stale manifests. The
base fixture completes at **361 charged steps**; all **361 incomplete budgets**
withhold every owner edge. The scalar fixture now measures **164 steps** after
the host property-query accounting change (previously 162).

`ClosedValueFlow` now connects the checked field initializer to its reads only
for the returned-table census. Preparation first validates the original manifest
and owner graph, then works on a speculative clone. It reconstructs native
source-operation facts and prepares only the uncaptured getter, preserving the
actual receiver when making its call direct. A complete live query must prove
the transformed graph before the clone is used. Final type admission rebuilds
that query again and checks the whole retained-callable component. General
closure lifting and default optimizations remain skipped on this manifest path.

Clearing old native facts also protects existing scalar owners. A forged
`ctnative.method` on a real field initialization or observation store must not
erase that operation or omit its field type. Fresh-fingerprint execution
controls cover both owners; input reports never supply authority. Reusing the
original manifest after native lowering still fails its fingerprint check.

The owner is a `std::shared_ptr` to its concrete field class. Its field holds
the existing shared method-table carrier, whose getter is an owning
`std::function<js_num()>`. Source allocation, publication and getter execution
remain runtime. Driver-selected observations stay separate from owner storage.

The standalone gate covers **eight complete 3/3 programs**, including direct
and indirect calls, repeated calls, fractional results, initialization before
binding publication, an ordinary `window` binding and stale native markers.
Node, the interpreter and explicit/deduced GCC/Clang output agree. Linked
binaries contain no VM symbols. Both generated forms pass ASan/UBSan,
use-after-scope, stack-use-after-return and leak checks. The harness retains
owner, table and callable independently after entry returns, resets the global,
churns allocations and invokes entry again. It checks distinct live identities,
weak expiry after each final release, and a copied callable after table release.

Twenty-four source refusals, four unsupported observation-result types and
stale/forged/rerun controls retain the boundary. The imported source specimen
needs **499 steps** for its original ownership proof and **510** after
preparation. Budgets **499 through 509** discard the speculative rewrite,
preserving source allocations, field operations and calls at **1/3 native**;
510 admits all three. These source counts differ from the smaller 361-step
handwritten query unit above. The scalar execution gate now covers eight
complete **1/1** programs, including both stale-marker controls.

The next boundary is a captured Map table: prove its live environment and Map
ownership through the global field and future calls, then consume those facts
in native table/capture admission. The existing publication specimen remains
**0/4** and exact Bootstrap Data remains **0/7** per mode. A typed external
export ABI, mutable slots and general realm ownership remain further work.
