# Native Maps containing Maps

The native backend accepts finite, acyclic Map schemas with primitive keys
and numeric leaves. A Map may own another Map, and retrieving a stored child
copies its owning handle. Mutating the child through any alias changes the
same allocation; replacing the parent's entry leaves previously retrieved
handles alive. Child handles can cross closed calls and returns and can be
captured by the existing owning closure environments.

A numeric leaf retains `std::shared_ptr<ctnative::number_map<K>>`. A parent
uses `std::shared_ptr<ctnative::map_storage<K, V>>`, where `V` is the child's
owning handle type. Numeric `keys()` snapshots remain available on parents.
Snapshots of Map values remain refused because the array tier currently
carries only numbers.

## Schema and ownership proof

The existing schema graph connects corresponding closed call arguments,
returns, immutable captures and identity-preserving `set` results. It now
also connects every stored payload and `get` result for each container
schema. This process repeats until nested receivers stop discovering new
families. Every Map-family producer must still be a proved allocation or
proved closed flow. A scalar or absent producer mixed with a child Map is a
compile-time refusal.

A separate directed graph records which Map schema contains which child
schema. Every cycle is refused before type inference. This includes direct
self-insertion, mutual insertion and cycles introduced by sharing a schema
through parameters. Some programs whose runtime allocations form a DAG can
still be refused when their static schemas recur; this initial representation
requires a finite C++ type. The accepted ownership graph cannot create a
`shared_ptr` cycle, and generated programs use neither the VM nor a collector.

## Presence proof for child reads

A child `get` must have a dominating `set` with the same receiver identity
and key, in the same function. Receiver identity follows only exact SSA
values and the identity-preserving result of `set`; schema-family equality
never substitutes for instance equality. Keys must be the same SSA value or
equal primitive constants. Any `delete` or `clear` in the container's entire
schema family prevents this initial presence proof, including mutations in
closed callees.

Successful reads carry a derived `ctnative.map_present` annotation and return
a definite child Map. The generated `map_get_present` helper copies the
stored handle; its unreachable missing-key path terminates rather than
inventing an absent Map representation. Input annotations are erased before
proof, including when the lowering pass runs again. Ordinary numeric reads
retain their existing optional-number behavior.

The proof deliberately refuses a set performed only in one branch, a set in
another function, and an earlier set on a different instance sharing the
same schema. Bootstrap Data's `has` / conditional `set` / `get` initialization
pattern needs a subsequent path-sensitive presence proof. Object-identity
keys and component-instance values are also still outstanding.

## Validation

`native-nested-map-fixture.js` compares nine numeric observations with the
independent interpreter: shared aliases, replacement, child lifetime after
parent return, independent factory allocations, three nesting levels,
primitive key carriers, numeric key snapshots, retained closure captures,
and deletion/clearing when no child lookup requires a presence proof.

`native-nested-maps.mlir` pins source-derived refusals for ownership cycles,
mixed scalar/Map payloads, absent keys, branch-only stores, deletion, mutation
through a callee, distinct allocations with one schema, cross-function
ordering and nonnumeric snapshots. `native-nested-map-proof.mlir` checks that
forged presence annotations cannot bypass proof on the first or repeated
lowering pass. The standard native pipeline supplies interpreter comparison,
standalone/no-VM checks, GCC/Clang warning checks, deduced-type comparison and
an off-by-one negative control.

The fixture admits **14/14** functions, resolves **12** globals and records
**25** resolver direct calls plus **1** native-lift rewrite. All nine numeric
observations agree in plain and deduced builds and under Clang ASan/UBSan,
with leak detection and halt-on-error enabled. GCC 13 and Clang 18 compile
cleanly; all **37** deduced declarations remain pinned. The pin mutation,
`lifetime42` off-by-one and excessive 15-function claim floor each fail as
intended.
