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

When the solved capture, argument and result carriers have a concrete callable
signature, each function value uses a `std::function` alias and a named lambda.
The binder emits the source body directly in that lambda. A shortened rendering
of the example above is:

```cpp
using js_num = double;
using ctn_env_fn_2 = std::function<js_num(js_num)>;

ctn_env_fn_2 ctn_bind_fn_2(
    std::shared_ptr<ctnative::string_to_number_map> capture_state) {
    ctn_env_fn_2 const ctn_lambda =
        [capture_state = std::move(capture_state)](js_num const argument_delta) -> js_num {
            std::string const key("value", 5);
            js_num const next = ctnative::to_number(ctnative::map_get(capture_state, key))
                                + argument_delta;
            ctnative::map_set(capture_state, key, next);
            return ctnative::to_number(ctnative::map_get(capture_state, key)) + 0.0;
        };
    return ctn_lambda;
}
```

The explicit init-capture owns the Map handle. A reference capture would not
extend the lifetime of the factory's local binding; copying the shared pointer
does. The lambda's const call operator keeps that handle immutable while the
Map's contents remain shared and mutable. Strings are owned by value as well.
Factory invocations retain separate environments; copies of one callable retain
the same Map. Generated programs need neither the VM nor a collector.

The source-derived `capture_` and `argument_` names distinguish the binder's
locals from the source function's locals. Keywords, unsupported identifier
bytes and collisions are handled within that generated scope. Callable builders
retain the returned function's source location. Signatures outside the concrete
callable carrier set keep the previous owning `std::tuple` representation and
direct lifted calls.

The body remains EmitC IR until final C++ emission. It shares the ordinary
function printer's source names, const/constexpr analysis, structured control,
hoisting and deduced-type pins. If another emitted call or address references
the lifted function, that definition remains available too. If the final use
analysis requires a writable captured binding, the lambda forwards to the lifted
function so each call still receives its own parameter copy. That fallback never
turns the capture into persistent mutable state.

This is the monomorphic case of Phase 59A. Proved
[returned method tables](native-method-tables.md) now carry stored callables
with concrete `std::function` signatures. Other stored or polymorphic callable
flows still need further proofs.

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
Only after admission and type inference does EmitC select a concrete callable
representation. That selection does not broaden the accepted closure flow or
trust source annotations. Synthetic capture reads still carry inference facts;
the callable invocation drops them and the existing dead-read sweep erases them.

Input environment annotations are cleared before proof. Unproved globals and
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

`CTNative/Lowering/native-owning-callables.mlir` adds a focused source regression
for named owning lambdas. Its checker compares nine observations in ordinary
and deduced output under GCC and Clang, and repeats both with Clang ASan/UBSan
and stack-use-after-return detection. It covers retained factory results,
forwarded aliases, independent Maps, external Map mutation, strings exceeding
small-string storage, structured loops and C++ keyword source names. A separate scalar/string
translation unit checks that callables request their own standard headers. A
third unit combines a tuple-backed closure accepting a callable parameter with
a scalar callable, checking that the prior representation remains available.
The C++ target regression separately checks retained direct/address references,
forwarding for writable captures, repeated calls and invalid body metadata.

The 2026-09-05 devbox gate passes **299/299 CTests**, including **98/98 lit
tests**. Standalone ASan/UBSan execution matches all 13 observations without
leaks; a deliberately excessive 36-function coverage floor fails. Bootstrap,
p5 and Phaser retain compile coverage of **19/574**, **39/4754** and
**43/7725**, respectively. The source-derived Data probes still refuse all
7 CommonJS/browser and 8 AMD functions, including the error recorder in the
strengthened probe. Boxed Bootstrap output remains
byte-identical.

The subsequent method-table and nested-Map extensions build on this capture
environment. Bootstrap's complete factory still requires component-instance
values, general object/host identities and typed host publication. Conditional
Map presence and property-free object keys are now
implemented. Shared mutable captured bindings remain a separate
ownership extension. The exported Data methods and full bundle remain
future work.
