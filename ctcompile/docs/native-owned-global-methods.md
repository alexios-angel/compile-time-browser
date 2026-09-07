# Owning source graph for an exported getter

The exported constant-getter specimen now has a complete live host proof and
an owning source graph. Native admission remains **1/3 functions**, including
when `host-manifest` is supplied. Its table field still needs a native carrier
and a complete admitted call component.

```js
var host = {};
function make() { return {get() { return 42; }}; }
host.slot = make();
var trace = host.slot.get();
```

Prepare with `--ctjs-resolve-globals --ctjs-lift-to-scf` before creating the
fingerprinted manifest. Neither query changes source operations, visibility,
call arguments or the manifest. Host callable evidence is described in
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

The source export regression compares `trace=42` with Node and the interpreter.
Explicit-manifest lowering reports the live owner proof but retains the
numeric-field and closure-value native refusals. Allocation, publication,
property and call operation counts remain unchanged. Reusing the original
manifest after partial native lowering fails its fingerprint check. This gate
does not claim a standalone native table or a new lifetime sanitizer result.

The next consumer is `ClosedValueFlow` and the returned-method-table census:
explicitly connect this fixed field to its source table and current callable,
then admit and emit the existing owning table carrier in global storage. The
host-manifest path currently skips closure lifting to preserve the input
fingerprint. Any new preparation must validate that input first and reconstruct
valid proof for its transformed IR; it cannot refresh a stale manifest silently.
Final admission must recheck the entire callable component and field type.
The **3/3** standalone GCC/Clang and post-entry lifetime gate remains proposed.
Captured Map publication (**0/4**) and exact Bootstrap Data (**0/7** per mode)
remain subsequent boundaries.
