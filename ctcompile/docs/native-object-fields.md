# Owned scalar component fields

Implemented, 2026-09-05. Bootstrap Data already retains component identity through
Map insertion, lookup, replacement and removal. The next native slice adds
ordinary scalar fields to those same owners. A saved component must observe
mutations through any alias and survive removal of its last Map entry.

Generate explicit named members on `identity_object`, each holding the existing
number/boolean/null/undefined carrier. The module's accepted field names form a
sorted, fixed layout; byte encoding gives arbitrary constant names distinct C++
identifiers. This is deliberately bounded to scalar leaves: fields cannot hold
another owner, so the extension adds no ownership cycles or generic property
dictionary. Missing fields start as undefined, which is a value representation,
not a claim about JavaScript own-property presence.

Rebuild the closed object-family proof from every producer and use. Allow only
constant ordinary field keys. Reject prototype/accessor mutation, reflection,
inherited Object.prototype names, dynamic keys, publication and unknown effects.
Derived field-group annotations index stores for inference and are cleared before
each proof. Join all stores of a key in its schema family, seeded with undefined;
schema membership never asserts that different allocations alias at runtime.

Each field operation separately requires a definitely owning receiver. A mixed
Map result may be accessed only inside a structured branch that proves strict
identity with a definitely owning value. An optional identity can also use its
true truthiness branch. Arbitrary scalar truthiness, loose equality and unchecked
nullable receivers do not establish that proof. Guards refer to exact SSA values,
so an intervening new lookup or mutation of a local binding is not silently
refined. There is no new exception bridge: accesses that could throw still refuse.

Keep the existing stack-shape lowering and property-free emitted helper unchanged
when this slice is unused. Test factory freshness, mutation through aliases,
retained owners after Map deletion/clear, absent and mixed scalar fields, exact
scalar tags, guarded lookup access, and negative nullable/prototype/ownership
cases. Require standalone and deduced GCC/Clang output, interpreter comparison,
no-VM checks and lifetime sanitizers. Re-run exact Bootstrap Data probes separately;
UMD publication, console effects and unguarded mixed-result fields remain distinct
prerequisites.

The source fixture admits all 15 functions and compares 29 numeric observations,
including inverted guards, scalar and absent lookup alternatives, exact tags,
encoded property names and distinct allocations sharing one field schema. Its six
native CTests pass ordinary/deduced GCC/Clang output, interpreter comparison,
altered-output rejection, no-VM and printing checks. Generated C++ also passes
ASan/UBSan with leak detection. The dedicated field lit test covers unchecked
receivers, scalar truthiness, loose equality, stale lookup guards, wrong branches,
non-scalar leaves, cycles, dynamic keys, inherited names and accessors. Earlier
identity-proof controls now forge prototype-field tags, which are discarded and
refused; ordinary scalar field construction is a positive case.
