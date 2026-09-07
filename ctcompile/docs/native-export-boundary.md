# Native export ownership and calls

**Status: scalar, getter and captured Map owners, 2026-09-07.** The explicit
[ordinary global owner](native-owned-globals.md) now admits the scalar specimen
at **1/1 native** under a live driver contract. The baseline regression below
intentionally runs without that option and retains **0/1**. Provider-prefix
discovery and native export admission remain separate proofs.

The exported getter's live callable and owning source graph now feed native
table storage and call admission: **3/3 native** with its explicit manifest,
**1/3** without. Global confinement remains unchanged; published functions
cannot become private from a prefix observation.

The [captured Map getter](native-owned-global-maps.md) now admits **4/4** with
the explicit manifest and standard Map identity; its default remains **0/4**.

## Measured boundary

The original baseline was `6760a8b`; the current getter proof below is the
2026-09-07 increment. Native counts use `--ctnative-lower-to-emitc=optimize=false`.
No source function is skipped or pruned. `CTNative/native-export-boundary.test` keeps these measurements
reproducible and checks reports, final admission and reruns.

| Source | Complete host/prefix evidence | Native admission | Node/interpreter |
|---|---|---|---|
| One ordinary global root holding a number | Complete `HostContractAnalysis`: one usable write/read edge | **0/1** | `trace=42` |
| Exported table with an uncaptured constant getter | Complete current callable and fixed owning source graph | **1/3** by default; **3/3** with manifest | `trace=42` |
| Exported Map-backed getter below | Complete live immutable capture and owning source graph with explicit standard Map identity; startup prefix also completes | **0/4** by default; **4/4** with manifest and Map identity | `trace=0` |
| Confined local root holding the same kind of owning table | Existing confined-field proof | **4/4** | `trace=0` |

The scalar specimen isolates the owner consumer:

```js
var host = {};
host.slot = 42;
var trace = host.slot;
```

Its complete, live host proof succeeds, including one initialized source root,
one field write and one field read. Without `host-manifest`, native admission reports:

```text
an object literal that escapes - it reaches `ctjs.store_global`
```

The four-function published specimen is maintained in
[`test/CTNative/native-export-boundary.js`](../test/CTNative/native-export-boundary.js):

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

With all publication/provider options enabled, its prefix report has no
remaining entry or provider boundary. It records one runtime Map allocation,
one capture edge, one publication and one Map `size` read. Allocation, closure
creation and publication operations remain in the IR. Without the explicit
standard Map identity, the complete host contract still refuses
`property receiver lacks a fresh own-data object proof` and exposes zero usable
slot edges. Supplying that identity now enables the separate live ownership
proof and native consumer.

Without a native host manifest, all four refusals remain after the prefix rewrite:

- Script entry: `an object literal that escapes - it reaches ctjs.store_global`.
- Wrapper: `global host is !ctnative.boxed, not a number`.
- Factory: `a closure used as a value: returned method table is written through an alias or stored into another object`.
- Getter: `uses its own closure`.

The regression also checks that a successful scalar host report, forged
`ctnative.host_*` success attributes and repeated lowering cannot supply native
ownership. It compares four observations against both Node and the interpreter.
The complete scalar report must identify the actual `host.slot` root, one
write/read edge and one observation store; complete analysis of the published
source before and after prefix rewriting must refuse with zero usable edges
when standard Map identity is absent. Its positive contract now admits all four
functions. The ordinary-owner, exported-getter and captured-Map gates execute standalone native
programs and check post-entry owning lifetimes. Existing confined-table
execution and lifetime checks remain in the
[owning field gate](native-owned-method-table-slots.md).

The exact Bootstrap Data probes remain the separate seven-function denominator:
**0/7 native** in each CommonJS/browser/realm-fallback mode, despite 24 resolved
entry calls and 23 completed provider summaries with object following enabled.
The tiny specimens above do not replace that census or establish Bootstrap
execution.

## Implemented consumer: an owned ordinary global root

The bounded `OwnedGlobalRoots` query and its native consumer implement the
scalar **0/1 -> 1/1** gate. See [the implementation and measured checks](native-owned-globals.md).
The requirements below remain the contract for this consumer and future extensions.

The query requires the complete `HostContractAnalysis` proof and further
restricts it to one source-created ordinary root with one unconditional binding
initialization, one fixed own-data field initialization and only supported
reads. Every read needs definite initialization. Existing host contracts allow
some ordered replacement; the native owner consumer explicitly refuses it.
Alias loads refer to the same allocation; schema equality never equates objects.

The root uses a module `shared_ptr` to its concrete native class. Source
allocation, initialization and publication keep their runtime order. Global
loads copy the checked owner; they never reference the old script stack frame. Keep the
escape verdict `StoredGlobal` and the global/external rule from plan part 25 R3.
A root named `host`, `module`, `globalThis` or `window` receives no special
confinement exemption.

Owned roots now have separate typed storage from scalar observations; output
prints only the driver's selected observation roots. The first field carrier
is a definite number. A host manifest is an environment contract, not a proof of
future argument types or effects.

The integration points are:

- `Analysis/OwnedMethodTableSlots.cpp` intentionally accepts only confined local
  owners. `Analysis/OwnedGlobalRoots.cpp` is a distinct query.
- `Analysis/TypeInference.cpp` consumes the proved owner/field types and definite
  initialization at these loads. Its ordinary global rule still includes absence;
  neither a familiar binding name nor one observed prefix can narrow that rule.
- `Lowering/Admission/Operations.cpp` admits numeric global loads/stores;
  `Lowering/Admission/Objects.cpp` owns object escape admission. Both need the
  live owned-root result, not a report attribute, through a separate admission path.
- `Lowering/EmitC/Expressions.cpp`, `EmitC/Module.cpp` and `EmitC/Operations.cpp`
  implement global storage and observations. `EmitC/OwnedGlobals.cpp` commits
  the typed owner plan only after whole-component admission.

Rebuild the query after semantic changes and before final admission. The
supplied fingerprint must still match; never silently rebind a stale manifest
to the changed module. Exhaustion or an incomplete environment exposes no
usable owner or field edge.

## Implemented consumer: owning tables and current callees

The smallest three-function callable specimen carries the existing owning
method-table handle in its fixed field:

```js
var host = {};
function make() { return {get() { return 42; }}; }
host.slot = make();
var trace = host.slot.get();
```

It measures **3/3 native** with an explicit manifest. Without one, only the
independent getter is admitted (**1/3**). Its complete host contract proves the
current property callee, and `OwnedGlobalRoots` proves the exact
factory/table/field source graph. Checked preparation preserves source receiver
and runtime effects while reconstructing native facts on a speculative clone.
The standalone and post-entry lifetime gates pass; see
[the implementation and measured budgets](native-owned-global-methods.md).

`Analysis/ClosedValueFlow.h` and
`Lowering/ClosureLifting/MethodTables.cpp` explicitly consume both the local
slot query and the checked exported edge. The latter preserves the table,
callable and admitted immutable Map environment after factory and script-entry
return.

The published Map specimen now has a complete callable/environment proof.
`HostContractAnalysis::property()` and `callable()` return no edge after any
refusal. Callable lookup follows the method property to its actual stored
source closure, the immutable capture binding and its standard empty Map.
The actual indirect wrapper factory call is proved before preparation; no
source call is silently resolved to make the original proof succeed. The
current getter body is restricted to `size`. Map mutations and broader provider
effects remain outside this tier. A prefix's retained factory/call rows cannot
substitute for the live checks. See [the measured gate](native-owned-global-maps.md).

Resolve each field read to its actual preceding stored closure and numeric
source identity, preserving receiver, evaluated arguments and source order.
Require every admitted call's concrete signature and complete native call
component. New invocations after startup still need proof. Open external
callers require an explicit typed export ABI; a startup observation cannot close
them. The current four-function program is a closed-source gate, not that ABI.
Do not use a generic private-symbol rewrite to bypass this distinction.

## Acceptance and refusal controls

For each implemented stage, require complete source-function accounting,
Node/interpreter agreement, standalone GCC and Clang in explicit/deduced forms,
VM-symbol absence with a positive control, and ASan/UBSan lifetime execution.
The table stage must exercise a saved handle after factory and entry return,
shared aliases, two independent factory invocations and allocation churn.
Keep exact Data's original source/vendor hashes, observations and census.

Refuse missing or conditional initialization, field/root replacement through an
alias, deletion, dynamic keys, accessor/prototype mutation, unknown calls,
reentry, unproved throwing paths, mixed incoming schemas, mutable captures,
separately escaped Maps and unsupported object/Map cycles. Distinct invocation
identities, stale/forged contracts, semantic mutation and every incomplete work
budget are mandatory controls. Failed ownership or call proof must retain the
source effects and named native refusals; no partial owner plan may escape.

After that, the remaining Bootstrap work still includes nested Map payload
ownership, presence/refinement for mixed results, error/callback effects,
realm publication and the full initialization graph. See
[the next provider boundary](bootstrap-provider-next.md),
[checked host slots](native-host-slots.md), and plan parts 24 and 25.
