# Proving partial-evaluation call targets

`ctjs.call_direct` has two descriptions of its callee: the symbol used by native
lowering, and the boxed callable used by mixed-mode dispatch. Specialization
deliberately changes the symbol while retaining the original callable. Partial
evaluation must prove that executing the selected body preserves both the result
and the heap effects of the actual callable for the arguments being evaluated.

For example, a factory creates `{value: 1}` and calls a variant that returns
`undefined`. The original callable also returns `undefined`, but writes
`value = 2`. Comparing only the return value would fold the factory's subsequent
read to `1`, changing the boxed execution. A matching signature, a specialized
name, or a `ctnative.specialized_from` annotation cannot rule this out.

## Callable identity

The evaluator resolves an evaluated closure through its unique numeric function
identity. A global callee load must have exactly one closure declaration in the
script entry's hoisting prologue, with closed direct-callee uses of every load.
Reassigned, missing and late declarations do not establish that identity. The
existing closed-declaration analysis supplies this contract; it is checked from
the current operations each time, without trusting input provenance attributes.

When the actual callable and symbol name the same body, evaluation proceeds
directly. Handwritten native-only IR retains its existing inert `undefined`
callee convention for symbols with no numeric bytecode identity. An actual
callable for another function never qualifies for that convention. Ordinary
calls also require an evaluated `undefined` new-target operand.

## Comparing alternate bodies

For an alternate symbol, the evaluator saves the pre-call heap, executes the
candidate, and executes the actual callable from a second copy of the same heap
with the same argument values. Both bodies must satisfy the existing closed,
capture-free execution checks. Unsupported execution, unknown results, escaping
effects and failures all reject the proof.

Successful executions are compared as follows:

* Primitive values must have exactly equal CTJS attributes. This preserves
  primitive kinds, signed zero and the represented NaN bits. JavaScript equality
  and Map SameValueZero are insufficient here.
* Every heap identity that existed before the call is fixed to itself. Its
  contents are checked even if it is not reachable from the return value or an
  explicit argument; the caller may still hold an alias or closure capture.
* Fresh nodes reachable from those identities or the result may be renamed by
  one bijection. The mapping works in both directions, so neither merging two
  distinct children nor splitting one shared child can pass.
* Node kinds, ordered object fields and Map entries, key identities, values,
  cell contents and initialization state, closure targets and capture edges all
  participate. Returning a heap reference is subject to the same mapping.

The graph walk is iterative and records visited pairs, so repeated aliases and
cycles do not cause unbounded recursion. Unreachable fresh scratch allocations
need not match. Allocation locations are diagnostic data, not runtime identity.
This is deliberately conservative: all pre-call nodes remain anchors, and two
different closure code identities are not equated by recursively proving their
future behavior. The ordinary residual ownership check still rejects live cycles.

The candidate execution, original execution and graph comparison share the same
step budget. The existing depth and heap-node bounds apply to each execution.
Proof exhaustion rejects the whole attempt rather than falling back to another
attempt with a reset budget. A successful proof retains the candidate heap and
result, including the total steps consumed by the proof.

## Rollback and specialization

A failed alternate-call proof restores its pre-call snapshot. Whole-function
evaluation contributes no state to the independent prefix attempt. Binding-time
analysis treats alternate callable symbols conservatively, so prefix evaluation
stops before the call, emits any proved initialization, and retains the call and
its suffix exactly once. The `{value: 1}` example therefore retains the call
against `1`, instead of retaining a speculative mutation from either execution.

The proof does not certify the variant for every possible input. It establishes
equivalence for this evaluation's concrete argument tuple and pre-call heap.
Existing closed-call and binding-time rules still decide when a whole function
may be specialized from that evaluation. Bodies may already have changed through
specialization or a prior PE pass; comparison uses their current operations.

## Regression coverage

`test/CTNative/partial-call-proof.mlir` covers equal returns with unequal effects,
equivalent mutations in different bodies, fixed pre-call identities, fresh graph
renaming, shared versus distinct children in both directions, Map insertion order,
signed zero, repeated evaluation and heap-node exhaustion. The negative effect
case includes forged provenance and checks the retained pre-call value.

`test/CTNative/partial-call-targets.mlir` covers closed global declarations and
missing, reassigned and late bindings; unknown returns; shared execution and
comparison limits; call-depth exhaustion; the cheaper exact-target path; and a
real source-specialized variant evaluated through its original boxed global
callee. Existing native specialization and PE differential fixtures check the
combined passes against ctbrowser execution.

Focused devbox verification passes all eight PE/specialization lit files and 13
CTests: the four ordinary native differential fixtures, their changed-output
controls, GCC/Clang compilation checks, and boxed specialization dispatch. All
four regenerated native pipelines admit every function. This focused run does
not replace the full repository gate or its deduced-output checks.
