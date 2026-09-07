# Native export ownership and calls

**Status: measured boundary and implementation design, 2026-09-07.** No native
export consumer is implemented by this increment. Provider-prefix discovery and
native export admission answer different questions: a completed startup trace
can retain the same native refusals.

The next implementation should begin with an explicit owner for one checked
ordinary global root and a fixed own-data field. Then connect a returned owning
method table through that field to a live callable proof. Do not change global
confinement or make published functions private from a prefix observation.

## Measured boundary

The baseline is `6760a8b`, using existing devbox compiler binaries with
`--ctnative-lower-to-emitc=optimize=false`. No source function is skipped or
pruned. The new `CTNative/native-export-boundary.test` keeps these measurements
reproducible and checks reports, final admission and reruns.

| Source | Complete host/prefix evidence | Native admission | Node/interpreter |
|---|---|---|---|
| One ordinary global root holding a number | Complete `HostContractAnalysis`: one usable write/read edge | **0/1** | `trace=42` |
| Exported table with an uncaptured constant getter | Complete contract refuses the ordinary property call | **1/3** | `trace=42` |
| Exported Map-backed getter below | Entire startup prefix completes: two resolved calls, one factory and one completed provider summary | **0/4** | `trace=0` |
| Confined local root holding the same kind of owning table | Existing confined-field proof | **4/4** | `trace=0` |

The scalar specimen is the smallest missing ownership consumer:

```js
var host = {};
host.slot = 42;
var trace = host.slot;
```

Its complete, live host proof succeeds, including one initialized source root,
one field write and one field read. Native admission still reports:

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
creation and publication operations remain in the IR. The complete host
contract still refuses `property receiver lacks a fresh own-data object proof`
and exposes zero usable slot edges. The two analyses are separate.

All four native refusals are unchanged after the prefix rewrite:

- Script entry: `an object literal that escapes - it reaches ctjs.store_global`.
- Wrapper: `global host is !ctnative.boxed, not a number`.
- Factory: `a closure used as a value: returned method table is written through an alias or stored into another object`.
- Getter: `uses its own closure`.

The regression also checks that a successful scalar host report, forged
`ctnative.host_*` success attributes and repeated lowering cannot supply native
ownership. It compares four observations against both Node and the interpreter.
The complete scalar report must identify the actual `host.slot` root, one
write/read edge and one observation store; complete analysis of the published
source before and after prefix rewriting must refuse with zero usable edges.
It does not claim a new standalone native export binary. Existing confined-table
execution and lifetime checks remain in the
[owning field gate](native-owned-method-table-slots.md).

The exact Bootstrap Data probes remain the separate seven-function denominator.
The checkpoint preceding this audit reports **0/7 native** in each
CommonJS/browser/realm-fallback mode, despite 20 resolved entry calls. The tiny
specimens above do not replace that census or establish Bootstrap execution.

## First consumer: an owned ordinary global root

Implement a bounded live query over the current module and driver-supplied
contract. Start with the scalar specimen; making it **1/1 native is a proposed
gate, not a measured result**.

The query must require the complete `HostContractAnalysis` proof and further
restrict it to one source-created ordinary root with one unconditional binding
initialization, one fixed own-data field initialization and only supported
reads. Every read needs definite initialization. Existing host contracts allow
some ordered replacement; the first native owner consumer should explicitly
refuse replacement instead of inheriting a broader storage model accidentally.
Alias loads refer to the same allocation; schema equality never equates objects.

Give this root an explicit module owner, for example an owning `shared_ptr` to
its concrete native class. Preserve source allocation, initialization and
publication order. Global loads copy or borrow that checked owner according to
its proved lifetime; they never reference the old script stack frame. Keep the
escape verdict `StoredGlobal` and the global/external rule from plan part 25 R3.
A root named `host`, `module`, `globalThis` or `window` receives no special
confinement exemption.

This is more than a field-flow exception. Admission currently supports only
numeric/nullable globals, and emitted globals also serve as numeric observations.
The implementation must carry proved owned roots separately from scalar
observation storage, emit their real carrier, and print only the driver's
observation roots. A host manifest is an environment contract, not a proof of
future argument types or effects.

The existing integration points are:

- `Analysis/OwnedMethodTableSlots.cpp` intentionally accepts only confined local
  owners. Keep that query's contract intact; add a distinct owned global query.
- `Analysis/TypeInference.cpp` needs the proved owner/field types and definite
  initialization at these loads. Its ordinary global rule includes absence;
  neither a familiar binding name nor one observed prefix can narrow that rule.
- `Lowering/Admission/Operations.cpp` admits numeric global loads/stores;
  `Lowering/Admission/Objects.cpp` owns object escape admission. Both need the
  live owned-root result, not a report attribute.
- `Lowering/EmitC/Expressions.cpp`, `EmitC/Module.cpp` and `EmitC/Operations.cpp`
  implement current global storage and observations. They need a typed owner
  plan committed only after whole-component admission.

Rebuild the query after semantic changes and before final admission. The
supplied fingerprint must still match; never silently rebind a stale manifest
to the changed module. Exhaustion or an incomplete environment exposes no
usable owner or field edge.

## Following consumer: owning tables and current callees

After the scalar owner gate, carry the existing owning method-table handle in
its fixed field. Start with the smallest three-function callable specimen:

```js
var host = {};
function make() { return {get() { return 42; }}; }
host.slot = make();
var trace = host.slot.get();
```

It measures **1/3 native**, with global-owner escape and returned-table storage
refusals; the independent constant getter is the one admitted function. Its
complete host contract refuses `unsupported provider behavior through ctjs.call`
and exposes zero usable edges. This isolates the property-callee consumer before
adding Map or capture effects. A complete **3/3** is a proposed gate.

`Analysis/ClosedValueFlow.h` and
`Lowering/ClosureLifting/MethodTables.cpp` already explicitly consume the local
slot query. The analogous exported edge must be requested explicitly by this
consumer, and must preserve the table, its callable environments and their Map
handles after factory and script-entry return.

The published specimen also needs a complete callable/environment proof. The
current `HostContractAnalysis::property()` returns no edge after any refusal;
its callable lookup does not follow a method property to the supplied closure.
The complete analyzer does not yet admit Map/capture/provider behavior. A
prefix's retained factory/call rows cannot substitute for those missing checks.
First implement a closed, effect-checked callable path or a dedicated live
export analysis with equally explicit obligations; then let the table consumer
use the resulting edges.

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
