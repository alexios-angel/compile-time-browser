# Owning object identities as native Map keys

Property-free object literals can be used as Map keys and carried through
closed function arguments, returns and proved immutable captures. Each creation makes a distinct owning
allocation, even when multiple objects share a source site or an empty shape.
Aliases keep the same identity; Map entries retain their keys after the
creating frame and all other aliases end.

```js
function makeKey() { return {}; }
const first = makeKey();
const other = makeKey();
const map = new Map();
map.set(first, 40);
map.set(other, 2); // A second entry, despite the identical empty shapes.
```

## Proof and representation

`Analysis/NativeObjectIdentity.cpp` follows closed exact-arity calls and
returns using the common `Analysis/ClosedValueFlow.h` graph. Every producer
must be an object literal or that proved flow. Every use must be a key of
a proved standard Map operation, a closed argument/return edge, or a proved
immutable native environment slot. Mixed,
missing and open producers cannot obtain an identity carrier.

The nominal `!ctnative.object_identity` type lowers to
`std::shared_ptr<ctnative::identity_object>`. The empty generated struct lives
in the runtime namespace; ordinary object shapes retain their existing
representation. Native Map key equality compares the owning handles, and
the insertion-ordered storage holds a strong copy of each key. Allocations
cannot be recycled while a Map still owns them. There is no integer address
surrogate, VM object or collector in the output.

Property reads/writes, inspection, mutable captures, object-valued payloads, host
values and structured identity merges remain refused. The identity has no
outgoing ownership edges, so adding it as a strong Map key cannot create a
reference cycle. Input identity annotations are discarded before proof;
an IR test verifies that a forged annotation cannot erase a real property.
Immutable capture environments own a shared handle with the same identity as the
Map key. The closure heap fixture separately checks capture lifetime and rejects
property writes or identity inspection through the captured handle.

## Validation and Bootstrap boundary

The fixture admits **14/14 functions**, with **five numeric observations**:
fresh identities and aliases, has/delete/clear, strong retention of keys,
post-factory lifetime and a returned Data-style method table with nested Maps.
It resolves **10** globals, **22** resolver calls and **12** native-lift calls.
The Data-style table uses conditional initialization and guarded reads through
the [presence proof](native-map-presence.md).

All observations agree in plain/deduced GCC/Clang builds and under ASan/UBSan
with leak detection. Standalone/no-VM, type-pin, off-by-one and excessive
15-function floor checks pass their expected outcomes.

This is a first object-identity carrier for property-free keys. Component
objects, DOM/host identities, general mutable object state and native host
publication need additional representations and proofs. The unchanged
Bootstrap Data source is still measured separately by its
[publication probe](bootstrap-data-probe.md).
