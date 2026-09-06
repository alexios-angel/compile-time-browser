# Const bindings in native C++

Native C++ qualifies initialized value bindings and by-value parameters when
their uses permit it. For example, the sample's score becomes:

```cpp
constexpr js_num score_1 = -1.0;
js_num score_3;
// Branches assign score_3 and their own join storage.
js_num const score_2 = score_3;
return score_2;
```

The independent [constant-expression analysis](native-constexpr-bindings.md)
promotes the immutable literal seed to `constexpr`. The returned snapshot stays
`const` because its initializer depends on runtime branch storage.

A backward data-flow analysis starts with potentially immutable bindings, marks
writes, address escapes and unknown uses, and propagates those requirements
through member/subscript views and expression captures. Each binding becomes
mutable at most once, so the worklist reaches a fixed point even when views are
shared or cyclic. Loads and value copies create independent snapshots. Pointer
dereferences affect the pointee; they do not make the pointer binding mutable.

An SSA result alone does not settle its C++ use. An opaque call may accept a
mutable reference or choose a different overload for a const argument. Native
helper construction records `ctnative.const_operands`, an array of operand
indices whose C++ ABI accepts binding const. This contract describes by-value
or const-reference arguments; it says nothing about purity, exceptions or heap
effects. For example, Map helpers accept a const owning handle and still mutate
its Map. `vec_push` retains a mutable lvalue for its vector. Unknown opaque
calls and verbatim operands conservatively require mutable bindings.

The emitter supports scalars, pointers and the known copyable native value
carriers. Arbitrary opaque types can hide references, move-only values and
const-sensitive operators, so they retain their existing qualification.
Uninitialized storage, loop induction variables, join variables, multiple
results assigned through `std::tie`, and hoisted local declarations stay
mutable. Fields, globals and return types keep their existing policies.

Qualification follows the complete type: `T* const pointer` freezes the pointer
binding while preserving a writable `T`. Deduced declarations use `auto const`.
Their type pins expect the exact corresponding const type; the checks never
strip qualifiers from `decltype`. Explicit and deduced output must agree on
qualification as well as observable behavior.

Native lowering enables the policy with the nearest module's unit attribute
`ctnative.const_bindings`. Unmarked EmitC retains upstream output. The analysis
does not change IR types or JavaScript semantics and runs independently of
source-name allocation and optimization settings.

Implementation is in `lib/Target/Cpp/Const/` and the compiler-owned helper
construction in `lib/CTNative/Lowering/EmitC/Calls.cpp`. The Target regression
compiles and executes mutation, overload, expression-capture, pointer, loop and
hoisting cases under GCC and Clang. Negative pin checks reject both lost binding
const and incorrect pointee const. The native sample regression also checks
const `catalog`/`score` bindings while observing mutations through shared Maps.

Generated parameter warning suppressions are decided after final IR cleanup.
Used parameters lose their redundant `static_cast<void>`; unused parameters keep
it even when canonicalization erased their last real use. Expression aliases
and explicitly selected opaque-call operands are followed to determine whether
the parameter actually appears in the printed body. User-written casts and
unknown opaque carriers retain their original behavior.
