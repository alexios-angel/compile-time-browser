# Checked ordinary global owners

The scalar export gate now admits **1/1 native functions**:

```js
var host = {};
host.slot = 42;
var trace = host.slot;
```

Pass an explicit driver contract with
`--ctnative-lower-to-emitc="host-manifest=driver.json"`. The JSON uses the
existing [host contract](native-host-slots.md) format, fingerprinting the
prepared module after global resolution and CFG structuring, with `host.slot`
as its requested root and `trace` as its observation. Without this option the
scalar specimen keeps its earlier **0/1** refusal. Reports cannot enable it.

This first consumer requires one complete straight-line script function, one
fresh ordinary object, one global binding initialization and one fixed numeric
field initialization. Field initialization may precede publication, as in
`var host = {slot: 42}`. Every global load follows binding initialization and
every field read follows the field write. Repeated global loads retain the
same source allocation; matching field schemas never establish identity.

The [live query](../lib/CTNative/Analysis/OwnedGlobalRoots.h) first requires the
complete `HostContractAnalysis` proof, then checks the stricter owner use graph.
Both share `host-max-steps` (default 100000). Exhaustion exposes no owner or
field edges. Its unit fixture completes at 164 charged steps, and every smaller
budget refuses atomically. Fingerprinting itself retains the host analysis's
existing whole-module hashing behavior.

The native entry preserves the fingerprinted input before admission when
`host-manifest` is supplied: default precomputation, reachability pruning,
closure lifting and provider preparation do not rewrite it. Explicitly run
any desired preparation before creating the driver manifest. The compiler
never silently rebinds a stale manifest to rewritten IR. It rebuilds the query
before final admission; printed success attributes are ignored.

Type inference gives the owner and its global loads a nominal
`!ctnative.global_object<binding>` type and subscribes each field read to the
proved initializing value. Admission separately requires a definite numeric
field and valid target identifiers. A type alone cannot authorize ownership.
The shape census connects the checked aliases, and only the accepted function
commits an emission plan.

Generated storage is `std::shared_ptr` to the concrete field class. Allocation,
field initialization and publication remain runtime operations in source
order. Global loads copy the owning handle; they never borrow the script's
stack allocation. Scalar observation storage remains separate, and output
contains only the driver's selected observations. An unrelated numeric global
can exist without appearing in that output.

Escape analysis is unchanged: the source allocation still escapes through
`StoredGlobal`, and ordinary `load_global` alias analysis remains external.
A binding named `window` receives the same explicit ordinary-root proof as any
other name; its spelling is no confinement exemption or realm-identity proof.

## Measured gate

[The source regression](../test/CTNative/native-owned-globals.test) covers six
complete programs at **1/1 native each**, including initialization before and
after publication, fractional numbers, repeated loads, the ordinary `window`
binding and an unobserved numeric global. Six selected observations match Node,
the interpreter and standalone GCC 13/Clang 18 in explicit/deduced forms.
Source and binary VM-symbol checks include an interpreter positive control.

Both generated forms pass ASan/UBSan, stack-use-after-return, use-after-scope
and leak checks. The lifetime harness saves a handle after entry returns,
releases the original global, churns allocations, invokes entry again and
checks that both live allocations are distinct. Releasing the saved owner
expires its weak witness; no stack pointer or leaked owner can satisfy that gate.

Fifteen source refusals retain the boundary: missing/late initialization,
field/root replacement, additional fields, another published alias, conditional
initialization, deletion, dynamic keys, unknown calls, accessors, prototype
mutation, cycles and boolean/string fields. Stale manifests, forged reports,
reruns and incomplete budgets also refuse. The live-query unit controls add
semantic mutation, unimported source and distinct allocation checks.

## Next boundary

The uncaptured exported getter now has a complete host callable proof and
[owning source graph](native-owned-global-methods.md), including its exact
factory/table/field identity. It remains **1/3 native** with and without an
explicit manifest; a completed Map-backed publication remains **0/4**. Next
consume the graph in returned-table flow, owning field emission and final
call-component admission. A startup prefix cannot supply future argument types
or an open typed export ABI. Complete host analysis still refuses the captured
Map/provider path.

The exact Bootstrap Data probes remain **0/7 native** in CommonJS/browser/
realm-fallback modes. This scalar owner does not implement a realm owner,
captured Map exports, callback effects or full Bootstrap initialization. See
[the export boundary](native-export-boundary.md) and
[the next Bootstrap work](bootstrap-provider-next.md).
