# Native returned method tables

A closed factory can return an object whose methods retain immutable numbers,
booleans, owning strings and Maps. The methods remain callable after the
factory returns, and aliases of the table share their captured Map state.

```js
function makeData(seed) {
    const state = new Map();
    state.set("value", seed);
    return {
        get(delta) { return state.get("value") + delta; },
        set(value) { state.set("value", value); return value; }
    };
}
const data = makeData(40);
data.set(41);
data.get(1); // 42, after the factory's frame has ended
```

Each table owns a generated struct through `std::shared_ptr`. Its fields hold
`std::function<R(Args...)>` with concrete inferred signatures. Generated
lambdas explicitly capture values by ownership and invoke the lifted native
functions. Separate factory calls create separate environments; copying a
table handle preserves its identity and shared state. No frame pointer, VM
or collector enters the generated program.

## Proof boundary

`ClosureLifting/MethodTables.cpp` follows closed direct-call arguments and
returns with the same flow graph used by returned closures. A table family
has one literal creation site. Each field is written exactly once, directly
on that literal, with a closure created and stored only there. Every field
initialization must dominate every read, argument handoff and return in the
creating function. Flow joins describe schemas, never runtime identity.

[Confined owning fields](native-owned-method-table-slots.md) also transport a
table through a fresh local object's fixed own-data slot. The slot needs one
initialization dominating every read and a complete nonescaping owner-use proof.
Loading the field copies the table handle, preserving ownership after that local
container dies. This adds no permission for global/realm publication or mutable
slots.

Every field read must be a call with the same table as receiver. Each target
needs visible invocations proving its parameter types and no callers outside
the field flow. Methods cannot read `this` or `new.target`. Captures and
signatures carry supported scalars, owning strings and acyclic Maps. Signatures
also admit proved identity-only object keys; captures of those keys are refused.
Capturing tables, functions, ordinary objects or arrays is refused, excluding
ownership cycles in this representation.

Alias writes, repeated or conditional initialization, dynamic or unsupported
keys, missing fields, detached methods, object inspection, open publication,
mixed producers and mutable captured bindings are diagnostic boundaries.
The accepted key alphabet is ASCII letters, digits and underscore; generated
members have an `m_` prefix, and `__proto__` is refused. A field with no visible
invocation still lacks a proved concrete callable signature.

Table annotations are cleared before every proof round. Ordinary closure
lifts can expose more closed flow, so a previous successful prefix cannot
survive a later refusal. Input annotations cannot bypass proof. Retaining a
callable also creates a native-admission dependency on its code target.

## Inference and emission

The nominal `!ctnative.method_table<"site">` distinguishes table schemas.
Callable targets retain `!ctnative.closure<"target">`; stored targets receive
`std::function` carrier aliases while ordinary returned closures keep tuples.

Synthetic capture reads preserve the lifted function's direct-call interface
for type and Map inference. They subscribe to the original owning captures.
Emission invokes the stored callable with its original arguments and removes
the unused synthetic reads. They remain distinct from lexical upvalues, so
lifting an enclosing IIFE cannot change their meaning. Map definitions and
callable aliases precede table declarations; callable builders follow native
function prototypes.

Generated table types and callable aliases live in `ctnative`, separate from
ordinary object shapes. Source fields such as `table_0` and `env_fn_2` cannot
collide with those generated ownership types.

## Validation and next Bootstrap work

The fixture admits **28/28 functions** with **seven numeric observations**
checked against the independent interpreter. It covers post-factory lifetime,
aliases, separate instances through common helpers, owning strings, mixed
callable signatures, empty environments, missing arguments and lifted IIFEs.
Ordinary object fields also exercise the generated-name collision boundary.
Standalone GCC/Clang, deduced-type, no-VM, off-by-one and ASan/UBSan/leak checks
pass. Source tests pin the lifetime and mutation boundaries; IR tests reject
forged proof annotations and callers outside the proved table flow.

The parallel [nested Map work](native-nested-maps.md) supports finite Map
payloads with numeric leaves. The next Data-specific steps are component
values, general object/host identities and typed host publication.
[Conditional presence](native-map-presence.md) and
[property-free object keys](native-object-keys.md) are now implemented. Shared
mutable captured bindings remain a separate ownership extension. The
[Bootstrap Data probe](bootstrap-data-probe.md)
measures the original source in CommonJS, browser and delayed-AMD environments;
passing its interpreter observations does not establish native Bootstrap.
