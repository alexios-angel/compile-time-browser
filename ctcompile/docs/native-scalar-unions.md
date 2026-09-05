# Native scalar unions

The native backend carries closed boolean/number unions in the existing
`ctnative::nullable_scalar` type. The same representation handles their
nullable forms. The inferred type still records the exact alternatives;
using this carrier does not add nullability to a definite union. Definite
numbers and booleans retain `double` and `bool` signatures and storage.

Calls, returns, conditional and loop edges, local fields, shared cells and
returned closure environments preserve the tag. Numeric addition is proved
only when every alternative converts numerically. Strict equality, loose
equality, truthiness and `typeof` retain their JavaScript behavior. Array
indices distinguish numeric zero from boolean false and null, and a numeric
NaN remains distinct from both null and undefined.

Field storage joins every scalar write and read before selecting the C++
type. A field stored both a number and a boolean uses tagged storage even
when no code reads it. This keeps shape selection independent of SSA
use-list order. A numeric standalone global still requires a definite number:
the wider carrier must not allow a boolean/number union to pass that guard.

`native-scalar-unions-fixture.js` retains Bootstrap's getter expression
verbatim:

```js
get: (e, i) => t.has(e) && t.get(e).get(i) || null
```

Its table owns a nested Map keyed by object identity. The fixture calls the
getter after its factory returns and after unrelated factory calls. It checks
absent elements, distinct keys, a missing component key, zero and NaN falling
back to null, removal, and reinsertion. This is a native prerequisite for the
vendor Data table; Bootstrap's component objects, library error branch and
host publication still need further work.

All 24 fixture functions are native. Its 39 numeric observations agree with
the interpreter, including both scalar alternatives, nullable cases, signed
zero, retained closures and cells. The census records 18 resolved bindings,
87 direct calls before closure lifting and 18 additional lifted calls. The
pipeline gates cover plain and deduced C++, GCC and Clang, no VM symbols,
numeric mutation and printing. The type oracle checks 138 observed registers
with zero violations and zero unvisited live values. All 39 observations also
match under ASan/UBSan with leak detection. Native compile coverage
stays Bootstrap 19/574, p5 39/4754 and Phaser 45/7725.

Unions containing strings, objects or unknown values remain refused. Mixed
scalar Map keys, Map payloads and array storage are also refused: admitting
scalar SSA values does not change the concrete storage contracts of those
containers. This slice does not implement general `std::variant` lowering.
