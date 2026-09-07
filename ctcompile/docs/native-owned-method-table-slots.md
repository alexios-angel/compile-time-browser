# Owning method-table fields on confined objects

**Status: Implemented.** This is a local-storage prerequisite for native publication
flow. It does not implement host exports, admit the exact Bootstrap Data module,
or extend the effects permitted by the host contract.

The [returned method-table lowering](native-method-tables.md) owns a
table and its captured state after factory return. This increment allows a fixed own-data field
on a fresh, confined ordinary object to hold the existing owning table carrier.
Returning the field's value preserves the table after its local container
dies. The container itself remains confined.

## Native fixture

This program has six imported functions, counting the script entry. The
implemented gate is **6/6 native with `result=4211`**, with no skipped or pruned
functions and default optimizations disabled.

```js
function makeData(seed) {
    const state = new Map();
    state.set("value", seed);
    return {
        get() { return state.get("value") + 0; },
        set(value) { state.set("value", value); return 0; }
    };
}
function publish(seed) {
    const ns = {exports: makeData(seed)};
    return ns.exports;
}
function run() {
    const first = publish(40), second = publish(10);
    first.set(42);
    second.set(11);
    return first.get() * 100 + second.get();
}
var result = run();
```

`exports` is an ordinary property name in this fixture, not a privileged host
binding. The same proof must work with another supported property name. The
two factory calls share a schema but allocate separate tables and Maps.

The 2026-09-06 baseline at `031f62a`, including the provider-read and callable-naming
changes, confirms interpreter output `result=4211`. Its native
coverage is **0/6 claimed, 6 refused, 0 skipped, 0 pruned**. Refusals identify
table storage into another object (one), the unsupported field carrier (one),
implicit closure use (two) and unresolved boxed values (two). The source and
census were recorded as `/tmp/ctcompile-slot-next.{js,json}` on the devbox,
with the orchestration log at `/tmp/ctcompile-slot-next.log`. This baseline
does not include the slot implementation.

## Live proof requirements

- The owner is a fresh ordinary object in the current function. Its complete
  use graph contains only checked constant-key accesses and inert bookkeeping;
  the owner is never captured, returned, stored elsewhere, passed to a call or
  inspected for identity. No global, realm or unknown object can supply it.
- The field is a supported ordinary own-data property. One initialization
  dominates every read. Conditional initialization, reads before initialization,
  subsequent writes through any alias, deletion, dynamic keys, accessors and
  possible prototype/descriptor changes refuse the field. An inherited property
  or a familiar property spelling does not establish an own-data slot.
- Every incoming table belongs to one proved returned-table schema. The existing
  requirements for closed factory/call flow, initialized method fields, concrete
  signatures and visible invocations remain mandatory. Mixed table/scalar values,
  incompatible schemas, detached or inspected methods, and unsupported escapes
  remain refused.
- Captures must satisfy the existing immutable owning-capture proof. The table
  owns its callable environments, and those environments own their Map handles.
  There is no reference into the dead container or factory frame. Mutable or
  late-initialized captures, ownership cycles and separately escaping resources
  receive no new exemption.
- The result is derived from current IR, with an explicit work bound. Report
  attributes and host-prefix retention rows cannot authorize it. Reruns clear
  derived annotations; semantic mutation or exhausted work withholds the proof.

Slot edges may be discovered before table inference settles, but discovery alone
must not admit a field. Commit the carrier and any derived annotations only when
the complete slot, table, capture and type checks succeed. Preserve factory calls,
allocations and field initialization in source order; the proof does not make
them pure, nonthrowing or eligible for early execution.

## Integration

The private `Analysis/OwnedMethodTableSlots` query returns checked owner,
initialization and read operations. It charges a bounded module/environment scan
and each owner use, field and dominance check. Exhaustion exposes no successful
prefix. It reads no proof annotations. Initialization must share the creation
block and dominate every read, excluding conditional or loop-only initialization
of an owner created outside that region.

Only `Lowering/ClosureLifting/MethodTables.cpp` requests the query's explicit
store-to-load edges from `Analysis/ClosedValueFlow.h`; other flow consumers keep
their previous escape rules. Schema equivalence does not imply runtime identity.
The returned-table census accepts those specific storage and load uses while
retaining its other restrictions, and reconstructs the query after each lift.

After ordinary owning-closure lifting exposes capture parameters, the existing
Map proof and type inference establish the resource carriers. Existing
`Analysis/TypeInference.cpp` field indexing and dominance checks already carry
the definite `MethodTableType`; no new inference exception is needed.
`NativeMap.cpp`'s instance-use and standard-intrinsic checks remain mandatory.
Final admission rebuilds the slot query against the current IR and requires the
stored table and every field read to have the same definite schema.

Field planning stores the existing `std::shared_ptr<ctnative::method_table_N>`
carrier by value. A field read copies the owning handle. Default construction
creates an empty handle before source initialization, which precedes every
proved read. Method-table definitions precede the ordinary classes containing
them. Sites with the same field names can instantiate one class template using
different table carriers or scalar carriers; heterogeneous values within one
slot remain refused. This needs neither a new callable representation nor
global storage, and preserves source allocation and initialization order.

## Validation and remaining boundaries

`test/native-owned-method-table-slot-fixture.js` is the exact six-function gate.
`test/CTNative/Lowering/native-owned-method-table-slots.mlir` and its Python
checker cover three standalone programs:

- **6/6:** the specimen above, returning 4211.
- **14/14:** five lifetime observations for saved tables, shared aliases,
  independent factories, nested closed-call/slot transport, long captured strings
  and allocation churn.
- **10/10:** one field-name family with two different table schemas and a scalar
  site, returning 4223 and checking the three template instantiations.

Each program compares with the interpreter, compiles in explicit and deduced
forms with GCC and Clang, and checks source and binary VM-symbol absence with
an interpreter positive control. Both forms run under ASan/UBSan with
stack-use-after-return and leak detection.

Twenty-one refusal tests cover the proof boundaries above, including alias writes,
conditional initialization, escaped owners, unknown calls, different incoming
schemas, mutable captures, descriptor/prototype changes and forged annotations
on initial runs and reruns. A direct analysis test sweeps work limits through
completion, checking both lookup APIs and absence of successful-prefix leakage
after exhaustion. Existing method-table and Map refusals remain covered.

The complete 2026-09-06 devbox gate passes **457/457 CTests** and **146/146 lit
cases** in 533.78 seconds. All 541 C++ files pass formatting. External sample
`../ctcompile-samples/07-owning-method-table-fields` contains the six-function
source and 10,128-byte generated C++; all seven sample pairs agree on 18
observations under GCC, Clang and the interpreter with no VM symbols.

The [host-prefix analysis](native-host-prefix.md) separately proves selected
wrapper calls, normal-return resource retention and current publication targets.
Those prefix facts do not close future call sites or establish a native owner.
Public factory parameters, global/realm storage, mutable publication slots,
provider/error effects and unchecked mixed-result property access remain outside
this increment. Do not mark published functions private from a prefix observation.

Keep the fixture's six-function denominator separate from the exact Bootstrap
fragment's seven-function CommonJS/browser/realm-fallback probes. With provider
read summaries those exact probes still remain 0/7 native. Passing this
local-storage gate does not by itself change that result or establish native
Bootstrap execution.
