# Owning method-table fields on confined objects

**Status: Planned.** This is a local-storage prerequisite for native publication
flow. It does not implement host exports, admit the exact Bootstrap Data module,
or extend the effects permitted by the host contract.

The existing [returned method-table lowering](native-method-tables.md) owns a
table and its captured state after factory return. It refuses storing that
table into another object. This increment would allow one fixed own-data field
on a fresh, confined ordinary object to hold the existing owning table carrier.
Returning the field's value would preserve the table after its local container
dies. The container itself would remain confined.

## Proposed fixture

This program has six imported functions, counting the script entry. The proposed
gate is 6/6 native with `result=4211`; native admission is still an acceptance
target, not a completed result.

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

The 2026-09-06 devbox baseline, including the provider-read and callable-naming
changes, confirms interpreter output `result=4211`. Native
coverage is **0/6 claimed, 6 refused, 0 skipped, 0 pruned**. Refusals identify
table storage into another object (one), the unsupported field carrier (one),
implicit closure use (two) and unresolved boxed values (two). The source and
census were recorded as `/tmp/ctcompile-slot-next.{js,json}` on the devbox,
with the orchestration log at `/tmp/ctcompile-slot-next.log`. This baseline
does not include the planned slot implementation.

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

Add a private `Analysis/OwnedMethodTableSlots` query returning checked owner,
initialization and read operations. Let `Analysis/ClosedValueFlow.h` consume its
store-to-load edges without treating schema equivalence as runtime identity.
`Lowering/ClosureLifting/MethodTables.cpp` would accept those specific storage and
load uses while retaining its other returned-table restrictions.

After ordinary owning-closure lifting exposes capture parameters, the existing
Map proof and type inference should establish the resource carriers. Do not
bypass `NativeMap.cpp`'s instance-use or standard-intrinsic checks. Extend field
indexing in `Analysis/TypeInference.cpp` as needed, then the scalar-only field
guard in `Lowering/Admission/Operations.cpp` and field-type planning to accept
the proved `MethodTableType`. The field stores the existing `shared_ptr` carrier;
this increment needs neither a new callable representation nor global storage.

## Validation and remaining boundaries

Require the fixture to compile standalone under GCC and Clang, produce the
expected result, agree with the interpreter and contain no VM symbols. Check
both explicit and deduced output. Exercise saved table lifetime after the local
container dies, shared aliases, distinct factory instances and allocation churn
under ASan/UBSan and leak checks.

Refusal tests should cover every proof boundary above, including alias writes,
conditional initialization, escaped owners, unknown calls, different incoming
schemas, mutable captures, descriptor/prototype changes and forged annotations
on initial runs and reruns. Preserve existing method-table and Map refusals.

The [host-prefix analysis](native-host-prefix.md) separately proves selected
wrapper calls, normal-return resource retention and current publication targets.
Those prefix facts do not close future call sites or establish a native owner.
Public factory parameters, global/realm storage, mutable publication slots,
provider/error effects and unchecked mixed-result property access remain outside
this increment. Do not mark published functions private from a prefix observation.

Keep the fixture's six-function denominator separate from the exact Bootstrap
fragment's seven-function CommonJS/browser/realm-fallback probes. With provider
read summaries those exact probes still remain 0/7 native. Passing this planned
local-storage gate would not by itself change that result or establish native
Bootstrap execution.
