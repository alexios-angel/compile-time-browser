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
Boolean/String values now support numeric conversion and exact String
concatenation, with typed signature and global transport described below.
Closed Number/String values use `ctnative::number_string` (`std::variant<js_num, js_string>`) through
parameters, returns, conditionals, loops and global stores. Generic primitive `+`
chooses concatenation only when an actual operand is String; numeric arithmetic,
truthiness, `typeof` and String concatenation consume that result without losing
its tag. See [the focused addition handoff](handoff/2026-09-20-native-generic-addition.md).

Optional Number/String source values now use `ctnative::nullable_number_string`,
exactly `std::variant<undefined_t, js_null_t, js_num, js_string>`. It carries
parameters, explicit returns, branch/loop edges, early global reads and saved
copies without collapsing absence. Global storage starts as undefined, and
proved definite observations retain checked extraction. The old optional-storage
helper remains a compatibility overload. The new source fixture checks 53
observations in eight native modes; see [the optional transport handoff](handoff/2026-09-20-native-optional-number-string.md).

Closed Boolean/String values use `ctnative::boolean_string`, exactly
`std::variant<js_boolean_t, js_string>`. Their optional carrier is
`ctnative::nullable_boolean_string`, exactly
`std::variant<undefined_t, js_null_t, js_boolean_t, js_string>`. Parameters,
explicit returns, branch/loop edges and global reads/writes retain actual tags.
Early global reads remain undefined; saved Strings own their contents across
later writes. Numeric conversion, generic addition, concatenation, truthiness
and `typeof` reuse the existing primitives and public Core operations.

The focused transport fixture checks 50 main observations in eight native modes
and the two original return observations on GCC/Clang. Existing Map storage
remains raw String storage with exact per-operation proofs; typed String values
unwrap only after that proof. See [the Boolean/String handoff](handoff/2026-09-20-native-boolean-string.md).

Strict/loose equality now supports all nine admitted primitive carriers through
`primitive_strict_equal` and `primitive_equal`, returning `js_boolean_t`.
Strict equality retains the JavaScript kind; loose equality compares two Strings
textually, rejects nullish/String matches and reuses scalar equality and Core
numeric parsing. NaN never equals itself; signed zeros compare equal. Existing
variants are visited directly and String payloads are borrowed for the comparison.
No new universal carrier is introduced. Exact String pairs keep direct C++ `==`.

The equality fixture checks 48 main observations in eight native modes, plus
six observations from the unchanged optional Number/String and Boolean/String
witnesses on GCC/Clang. Five refusal controls and two mutations pass.
See [the equality handoff](handoff/2026-09-20-native-primitive-equality.md).

The five String-containing carriers now support shared variables and capture
pointers. The existing type join proves each store fits its selected carrier;
scalar conversion widens single alternatives and compatible sub-unions. Owning
reads preserve snapshots across later writes. Initial undefined is skipped only
when the existing dominance proof makes it unobservable.

`Closures/primitive-cells.test` checks 44 observations in eight native modes,
plus the unchanged `shared-mixed.js` witness on GCC/Clang. Five refusals and two
mutations pass. See [the cell handoff](handoff/2026-09-21-native-primitive-cells.md).
Next reuse bounded structured-exit normalization for the retained `index-switch.js`
refusal. Captured sibling-function calls, ordering, broader unions and mixed
container storage remain separate. This is not arbitrary variant lowering.
