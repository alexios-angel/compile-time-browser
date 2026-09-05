# Native returned closures

Closed factories can return a function retaining immutable captured numbers,
booleans, owning strings and primitive-key numeric Maps. It can be called after
the factory returns, forwarded through closed functions, or passed to a helper
whose callable parameter has one proved target.

```js
function makeStore(seed) {
    const state = new Map();
    state.set("value", seed);
    return delta => {
        state.set("value", state.get("value") + delta);
        return state.get("value") + 0;
    };
}
const add = makeStore(40);
add(2); // 42, after makeStore's frame has ended
```

Each function value carries an owning `std::tuple` environment. Its code target
is proved statically, so calls extract the captures and invoke the lifted C++
function directly. The tuple owns strings and copies Map shared pointers; it
never borrows a factory's stack. Factory invocations have separate environments,
while aliases of one Map still observe shared mutations. Generated programs
need neither the VM nor a collector.

This is the monomorphic case of Phase 59A. Stored or polymorphic callables still
need `std::function` signatures, method-table ownership and further flow proofs.

## Proof and lowering

`ClosureLifting/ReturnedClosures.cpp` follows corresponding actual/formal
arguments and returns through direct calls. Every producer must be the same
closure creation site, a closed function parameter, or a closed call result.
A second target or absent/scalar input refuses the family. Every use must be
a proved invocation, closed argument position or closed return. Other direct
callers of the target also prevent changing its signature.

The census repeats after each productive lifting round: a local lift can close
a factory/helper and shift its arguments. Environment extractions are separate
from lexical upvalue reads, so lifting an enclosing IIFE cannot rewrite them
into that IIFE's parameters.

Captures must be immutable, and both their values and assignments must dominate
environment creation. The local lift can instead supply a value at each call.
Mutable or late-initialized bindings need owning shared cells and remain
refused. A frame-local capture pointer cannot enter an owning environment.
Arrays, ordinary objects and captured functions also remain outside this
representation, excluding ownership cycles here.

The nominal `!ctnative.closure<"target">` type records code identity. Capture
reads subscribe to their creation site's inferred values; the nominal type
does not recursively contain capture types. Map inference connects each slot
with its extractions without conflating distinct slots or runtime allocations.
C++ environment aliases follow Map definitions and precede function prototypes.

Input environment annotations are cleared before proof. Unproved globals,
method fields, function identity inspection, lexical `this`, `new.target`,
surplus arguments and structured merges remain diagnostic boundaries.

## Validation and Bootstrap

The fixture admits all **35 functions**, with **13 numeric observations**
compared against the interpreter. It covers factory lifetime, aliases,
independent instances, external Map mutation, distinct capture schemas,
heap-backed strings, boolean captures, missing arguments, nested factories and
lifted IIFEs. One IIFE regression would otherwise compute 52 instead of 42
while still producing valid C++.

The standard fixture gates check standalone GCC/Clang compilation, absence of
VM symbols, deduced-type pins and an off-by-one negative control. Source and
IR tests pin refusals for mixed producers, mutation, late initialization, open
boundaries, inspection and forged annotations. Type tests cover nominal
identity, joins, optionality and escaped target names.

The 2026-09-05 devbox gate passes **299/299 CTests**, including **98/98 lit
tests**. Standalone ASan/UBSan execution matches all 13 observations without
leaks; a deliberately excessive 36-function coverage floor fails. Bootstrap,
p5 and Phaser retain compile coverage of **19/574**, **39/4754** and
**43/7725**, respectively. The source-derived Data probes still refuse all
6 CommonJS/browser and 7 AMD functions. Boxed Bootstrap output remains
byte-identical.

Bootstrap's complete factory still requires returned method tables, owning
mutable cells, host publication, object-identity Map keys and nested
Map/component values. This change supplies an owning capture environment;
the exported Data methods and full bundle remain future work.

The next narrow gate is a returned object whose get/set/delete methods share
one owning Map environment and remain callable after the factory returns.
Object-identity keys and nested values, shared mutable bindings, and typed
CommonJS/browser/AMD publication can then advance as separate workstreams.
