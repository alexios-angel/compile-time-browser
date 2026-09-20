# Native scalar unions

The native backend carries closed boolean/number unions in the existing
`ctnative::nullable_scalar` type. The same representation handles their
nullable forms. The inferred type still records the exact alternatives;
using this carrier does not add nullability to a definite union. Definite
numbers and booleans now use `ctnative::js_num` and `ctnative::js_boolean_t`;
raw storage and host interfaces use explicit adapters.

Calls, returns, conditional and loop edges, local fields, shared cells and
returned closure environments preserve the tag. Numeric addition is proved
only when every alternative converts numerically. Strict equality, loose
equality, truthiness and `typeof` retain their JavaScript behavior. Array
indices distinguish numeric zero from boolean false and null, and a numeric
NaN remains distinct from both null and undefined.

Field storage joins every scalar write and read before selecting the C++
type. A field stored both a number and a boolean uses tagged storage even
when no code reads it. This keeps shape selection independent of SSA
use-list order. Standalone globals also join every source store: a closed
boolean/number union prints its actual tag, including any proved nullish
alternatives. Definite numeric observations retain their Number tag check;
the wider carrier cannot satisfy that check with a Boolean.

`ctcompile/test/CTNative/Fixtures/Scalars/scalar-unions.js` retains Bootstrap's getter expression
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

The measurements above describe the original Boolean/Number slice. Closed
local Boolean/String temporaries now support numeric conversion and exact String
concatenation; their signatures/globals remain refused. Closed Number/String
values use `ctnative::number_string` (`std::variant<js_num, js_string>`) through
parameters, returns, conditionals, loops and global stores. Generic primitive `+`
chooses concatenation only when an actual operand is String; numeric arithmetic,
truthiness, `typeof` and String concatenation consume that result without losing
its tag. See [the focused addition handoff](handoff/2026-09-20-native-generic-addition.md).

Optional Number/String source values, including changing-global reads, still
need distinct null/undefined transport. Global `std::optional<number_string>`
only guards missing initialization; it is not the source optional carrier.
Equality/ordering of Number/String unions, broader unions and mixed scalar Map
keys/payloads or array storage retain their existing refusal boundaries. This
does not implement arbitrary `std::variant` lowering.
