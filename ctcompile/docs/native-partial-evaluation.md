# Native partial evaluation and heap residualisation

Bootstrap does substantial work while creating module state. Native compilation
should evaluate initialization whose inputs and effects are known, then emit
ordinary C++ for the live state and the work that still depends on runtime inputs.
An initialization result is a graph: two Map entries may own the same child, and
two equal-looking object keys may have different identities.

## Design

The partial evaluator runs over CTJS before native ownership and type inference.
It has a compiler-owned heap of object identities and insertion-ordered Maps,
with primitive constants on edges. Heap references are stable node IDs, never
addresses in the compiler process. Writes update this heap; reads and arithmetic
consume its current state. Overwritten entries and unreachable temporary objects
need no emitted initialization.

Residualisation traces from the returned value, allocates each reachable node
once, then emits its final fields and entries. Shared edges reuse the same
allocation. The existing native analyses must still prove a C++ representation;
they select local structs or owning handles and diagnose unsupported graphs.
There is no serialized VM heap, runtime interpreter or collector in the output.

For example, a closed factory that builds a Map, overwrites one entry, removes
another, and returns the Map can become a factory containing only the surviving
allocation and stores. Allocations stay inside the runtime factory: separate
invocations must receive fresh mutable state. This is not a process-wide singleton.

The general direction is to evaluate a static region until an effect or unknown
input is reached, then preserve that boundary and all reachable state. The first
slice deliberately commits a result only after an entire closed factory has
been evaluated. A failed attempt leaves its body intact. This avoids speculative
effect duplication and supplies a testable foundation for later region splitting.

## Reusing ctbrowser

The compiler links `ctbrowser::script` for its existing primitive operations:
`context::binary_op`, `binary_op_static`, `negate_value`, `to_number_value`,
`truthy`, `type_of`, `loose_equals` and `compare_relational`. Strict equality and
SameValueZero come from `script::value`; the engine's Map and Set builtins share
the same SameValueZero implementation. This preserves the engine's established
semantics, including its documented deviations, without a second coercion engine.

A narrow adapter converts CTJS primitive attributes into temporary runtime values
and copies primitive results back. It canonicalizes all incoming NaN payloads
before NaN boxing. Strings have C++ ownership, with a 64 KiB limit for primitive
execution. Heap-key string equality compares interned attributes directly and
remains exact for larger literal keys. Each helper context has no program,
builtins or host callbacks. Evaluator object references never enter it, so
object conversion hooks cannot run. No bytecode dispatcher is invoked.

The compiler retains its own graph, effect checks, evaluation budgets and
residualisation logic. Those concerns require static identities, unknown values
and rollback that the runtime heap does not provide. The generated C++ retains
ordinary native ownership and links no engine, VM or collector.

## First slice

`--ctnative-partial-evaluate` requires private functions with visible direct calls
whose explicit arguments are the same primitive constants at every call site.
Private visibility is insufficient by itself: numeric-index closure uses are
rechecked. Only exact direct-callee uses and single hoisting declarations with
closed global reads are allowed. Escaping closures, accessors, implicit callback
invocation and unproved function targets block specialization.

Receiver, constructor state, closure captures, host reads/writes and unknown
invocations cannot be evaluated. Nested direct calls may operate on allocations
created during the same attempt. Standard Map operations require the existing
constructor and instance proof; annotations are rederived before evaluation.

The default limits are 10,000 evaluated operations, 32 nested calls and 256
fresh heap nodes per factory. Unsupported
operations, unknown inputs, cycles in a returned ownership graph and exhausted
budgets retain the original function and record a reason. Input result/reason
annotations confer no authority. The pass is opt-in while this boundary is
being established; ordinary native lowering remains the final admission gate.

The initial emitted graph uses CTJS allocations, constants and stores, so the
existing native C++ backend handles naming, ownership and type deduction. Later
work can select immutable static tables where identity and mutation proofs allow
them. Host objects, component state and event registration remain runtime effects
until explicit contracts describe them.

## Validation

`test/native-partial-evaluation-fixture.js` evaluates factory initialization and
keeps heap-consuming observers as runtime functions. It exercises a shared child
Map, distinct object keys, fresh allocations on repeated calls, insertion order,
overwrite/delete/clear, constant branches and loops, primitive coercion and
NaN/signed-zero Map keys. The fixture has 14 source functions including the script
entry: eight are partially evaluated into nine residual heap nodes, while five heap-consuming
observers stay at runtime. All 14 pass native admission. Its eight numeric
observations agree with ctbrowser, including under ASan/UBSan with leak detection.
The opt-in pipeline requires at least one successful evaluation before accepting
its native output.

The lit cases inspect the residual graph, repeat the pass, and require intact
bodies with reasons for cycles, unknown values, external effects, prototype
access, escaping closures, differing caller arguments and exhausted budgets.
A tag-colliding IEEE NaN verifies the adapter boundary. The ordinary native pipeline checks numeric
observations against ctbrowser, absence of VM symbols, both GCC and Clang,
type-deduction pins and a deliberately changed output.

The combined devbox gate passes 347/347 CTests, including 110/110 lit cases.
The pinned formatter passes all 440 C++ files.

Full Bootstrap initialization is still outside this first slice: its host and
prototype interactions require effect contracts and static/dynamic region
splitting. Default native corpus coverage therefore remains a separate measure
from the opt-in factory evaluation tests.
