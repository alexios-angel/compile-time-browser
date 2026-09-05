# Conditional heap-effect queries

Binding-time analysis can preserve a fresh local heap across a call that writes
only a proved disjoint argument graph. The call and its result remain dynamic;
the analysis describes which later heap reads still have static inputs. It does
not execute the call, remove it, assume its return value, or relax native and
partial-evaluation environment checks.

For example, the imported-source regression contains:

```js
function write(object, value) {
  object.value = value;
  return value;
}
function untouched(value) {
  const changed = {value: 1};
  const kept = {value: 42};
  write(changed, value);
  return kept.value;
}
```

The call writes `changed`, while the later read of `kept.value` remains static.
The same query mechanism covers proved native Maps. This is a prerequisite for
reasoning about independent Data structures; it is not a claim that Bootstrap's
host-dependent Data implementation executes natively.

## Contract

The analysis rederives native Map facts from current IR before constructing the
query cache. Existing `ctnative.*` annotations are reports, not proof inputs.
`Effects.cpp` summarizes a visible helper as a sequence of conditional access
clauses. A clause identifies an explicit argument, zero or more constant own
field names, an object-field or native-Map access, and whether it writes.

Summaries currently require private, capture-free, single-block functions with
no entry backedge. Implicit receiver, new-target and callee arguments must be
unobserved except for rooting. Every operation must be a supported primitive
operation, a conditional heap access, inert bookkeeping, or a direct call with a
proved summary. Constructor calls, callbacks, recursion, nested control flow,
unknown coercions and ambiguous receiver paths remain unresolved.

An ordinary object read extends a path only before any write. Calls to summarized
helpers also precede writes. This avoids describing a post-mutation receiver
using a pre-call path. Previously loaded SSA aliases can still be written after
a write, because their paths denote the original objects. The first implementation
rejects adding a missing own field: an inherited setter must never be treated as
a plain local assignment.

At each caller, `EffectQuery.cpp` discharges every clause against the current
heap facts:

1. Every receiver and intermediate path node must be a clean static allocation
   in that caller. Object paths must follow known own fields; Map targets must
   have a freshly proved native Map identity.
2. Every written receiver and all of its reachable references are invalidated.
   Traversal includes object fields, Map values and keys, cells, and closure
   captures. Different SSA values reaching the same allocation are aliases.
3. All conditions and graph edges must resolve before any fact changes. An
   unknown, nonlocal or already dynamic reference, or an exhausted query budget,
   falls back to ordinary broad heap invalidation.

This deliberately invalidates more than the precise write set. For example, a
closure reachable from a written object causes its captured local objects to be
invalidated, even if the helper merely replaces a scalar field. It never uses a
schema family as evidence that two runtime allocations are the same or disjoint.
Deleted Map keys may remain in the conservative reachability set; a proved
`clear` removes the retained keys and contents.

A separate module scan establishes the closed environment used by the query.
It rejects public runtime object arguments, unknown globals/calls/constructors,
prototype escape routes, reflection, accessors and unsupported implicit calls.
The standard initial-prototype assumption is the existing closed-program
assumption. This additional query does not weaken any other module-wide guard.

## Direct-call consistency

`ctjs.call_direct` carries both a symbolic target and the original callee value.
Boxed dispatch can use the latter. A specialization-provenance annotation cannot
prove that the two bodies have equivalent effects.

The BTA query and BTA's existing complete-static-call path now require a matching
unique bytecode closure identity, either directly or through a uniquely stored
closed function declaration. Unknown callable provenance and alternate symbolic
targets retain broad invalidation. Handwritten native-only functions with no
numeric bytecode identity retain the established inert `undefined` callee-slot
convention; supplying an actual different callable does not qualify for it.

This contract applies to BTA. Whole-factory partial evaluation has its own direct
call evaluator and admission rules; this change does not establish a general
cross-tier equivalence proof for arbitrary inconsistent raw IR. Native variants
must continue to preserve the original boxed dispatch behavior.

## Dependency and termination bounds

The per-analysis cache is module-local and rebuilt after IR changes. Summaries
start unknown. A reverse direct-call dependency worklist reanalyses callers when
a callee's complete access sequence changes; no attributes persist a trusted
summary across transformations. An unknown recursive component cannot bootstrap
a known summary.

The initial implementation bounds summary construction to 100,000 operation
visits, at most `N * (N + 1)` function visits, 64 access clauses per summary, and
8 fields per receiver path. Construction exhaustion discards all summaries.
Caller queries permit 4,096 path/graph steps and 256 reachable allocations.
Exhaustion is an optimization refusal, not a runtime assumption or deoptimization
mechanism.

## Validation

`CTNative/binding-time-effect-queries.mlir` checks disjoint object and Map heaps,
transitive own-field paths, Map-key and closure/cell reachability, and repeated
analysis. It also checks conservative behavior for missing own fields, dirty
reachable objects, read-after-write paths, unknown receivers, prototype access,
recursion and public inputs. Two alternate-callee witnesses
cover both the dynamic-argument query path and the all-static-argument path.
The imported JavaScript case exercises real closure/global declaration lowering.

The test asserts that calls and affected reads remain dynamic while disjoint
reads become static. It does not claim a native optimization merely because an
analysis annotation was emitted. The final focused BTA/PE run passes all nine lit cases, including 16 commands
in the effect-query case. The combined devbox gate passes 406/406 CTests and
123/123 lit cases; see the [integration record](native-pe-roadmap.md).
