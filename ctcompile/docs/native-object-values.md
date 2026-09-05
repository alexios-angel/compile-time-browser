# Owning object identities in native Map values

Implemented, 2026-09-05. Bootstrap Data can store a component and return it through
`t.has(e) && t.get(e).get(i) || null`. The intermediate result includes booleans,
null, undefined and the component; the exact vendor probe also stores numbers.
The bounded carrier supports this union for property-free object identities.
Objects with fields and host-backed components remain a later extension.

Keep the existing scalar carrier unchanged. A separate `object_value` contains
an owning identity or the existing tagged scalar. Map entries and saved results
retain the owner independently, including after replacement, deletion, clearing
and return from a factory. Fresh allocations remain distinct; no object contains
outgoing ownership edges, so this extension cannot introduce an ownership cycle.

Recheck every producer and use in the closed value-flow family. Connect proved
Map stores/lookups and structured region edges for schema checking, never as a
claim of runtime alias identity. Unknown producers, property access, publication,
callbacks and mutable captures remain refused. Native type inference still joins
the actual Map stores; no annotation supplies a trusted value type.

Support strict equality, truthiness, `typeof`, calls/returns and structured
control. Loose equality is allowed only when no object-to-primitive conversion
can occur (object/absence pairs or a definitely null/undefined operand). Numeric
coercion, ordering and loose object/number equality remain native diagnostics.
Object-valued snapshots and object/primitive key unions remain outside the slice.

Gate the exact getter expression with absent entries, scalar/object replacements,
alias identity, null versus undefined, numeric edge cases, separate factories and
retained values after deletion. Require standalone GCC/Clang and deduced C++,
reference/no-VM checks, lifetime sanitizers, and forged-proof/unsupported-use
controls. Measure exact Data probe coverage separately: this carrier does not
establish a host publication or console contract.

The source fixture retains the getter verbatim, crosses direct calls and immutable
closure captures, and checks zero, one and multiple loop iterations. Shared lift
placeholders cannot merge unrelated object/scalar schemas: a bounded forwarding
walk excludes poison only when it has no observable use. When a structured arm
yields both a placeholder and a false continuation flag, the proof still follows
the loop exit value and excludes only the after-region edge. Unknown flags,
observed exits, live backedges and exhausted bounds retain refusal.

The scalar helper remains unchanged; object values use a separate helper and
owning Map overload. Map-set lowering widens stored values before template
argument deduction. Its original result supplies the inferred Map schema because
the receiver may already have been replaced with EmitC SSA.

The integration fixture lowers all 16 functions and checks 47 numeric
observations. It includes both union-valued and definite-identity loops; each
has zero-iteration and repeated-iteration checks. The raw poison controls select
the hazardous arm and cover an observed exit, an observed next iteration and a
continuation flag unrelated to the payload's branch.

All six fixture CTests pass: ordinary/deduced native C++, GCC/Clang, numeric
reference comparison, altered-output rejection, no-VM and printing/type-pin
checks. The generated program also passes ASan/UBSan with leak detection and
matches all 47 interpreter observations. The integrated gate passes 406/406
CTests, including 123/123 lit cases, with all 482 C++ files formatted.
