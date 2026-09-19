# Script Vm Properties contracts

<a id="contract-1"></a>

`[[nodiscard]] static double exponentiate(double base, double exponent);`

Number::exponentiate (6.1.6.1.3), which is NOT C's `pow`. C99 F.10.4.4
defines pow(+-1, y) as 1 for EVERY y - including NaN and both infinities -
where the specification requires NaN for exactly those. Shared by
`Math.pow` and the `**` opcode, which had the same bug in two places.

<a id="contract-2"></a>

`[[nodiscard]] double to_number_value(value v);`

ToNumber, 7.1.4, INCLUDING the object case - an object coerces through
its own valueOf and then toString, which the static `to_number` cannot do
because it cannot call back into the VM. Every built-in whose spec text
reads `? ToNumber(x)` must use this one: `Number([])` is 0.

<a id="contract-3"></a>

`[[nodiscard]] std::partial_ordering compare_relational(value a, value b);`

Abstract Relational Comparison, 7.2.13 - the ONE comparison that `<`,
`>`, `<=` and `>=` each ask a different question of.

It is not a numeric comparison. Two STRINGS compare as text, and coercing
them to numbers instead makes `"a" < "b"` false - along with `>`, `<=`
and `>=`, because ToNumber("a") is NaN and every comparison against NaN
is false. `unordered` is the specification's `undefined` result, which is
what makes all four operators false on a NaN.

<a id="contract-4"></a>

`[[nodiscard]] bool bigint_binary(op kind, value a, value b, value & out);`

BIGINT ARITHMETIC, and the rule that makes it safe.

A BigInt and a Number CANNOT be mixed in arithmetic - `1n + 1` is a
TypeError - and that is the feature rather than an omission: an engine
that quietly coerced would round exactly where the type exists to stay
exact. So the dispatch is: both bigint, do it; one bigint and one
anything-else, throw.

Returns true when it HANDLED the operation (including by throwing), so
the caller falls through to the Number path only when neither side is a
bigint. `kind` is the opcode being executed.

<a id="contract-5"></a>

`[[nodiscard]] value binary_op_static(op kind, value lhs, value rhs);`

THE SEVEN NON-RE-ENTERING BINARY OPERATIONS, in one place.

add, bit_and, bit_or, bit_xor, shl, shr and ushr. They are together
because they are the same function: try the BigInt arm, and otherwise
apply a STATIC conversion - to_number for add, to_int32/to_uint32 for the
six bitwise - which is what makes them non-re-entering. None of them can
run a user valueOf or toString, so a backend that has proven both
operands are Numbers may drop the call, the exception edge AND the
safepoint.

`add` BELONGS HERE AND NOT WITH THE RE-ENTERING FAMILY, which looks wrong
and is not: compile_binary maps source `+` to add_generic, and op::add
comes only from `++` and three internal counters. So `x++` on
{valueOf: () => 3} is NaN and never runs user code. That is a deviation
from the specification rather than an optimisation, and aot_helpers.def
records it as one - if it is ever fixed, these seven move.

ct_aot_binary_op_static calls exactly this, so the two tiers cannot drift.

<a id="contract-6"></a>

`[[nodiscard]] value binary_op(op kind, value lhs, value rhs);`

AND THE SEVEN THAT CAN RUN PAGE JAVASCRIPT.

sub, mul, div, mod, pow, add_generic and concat. They are the same seven
shapes as `binary_op_static` above except that their conversions are the
RE-ENTERING ones - to_primitive, to_number_value, to_string - each of
which runs a user `valueOf` or `toString` when handed an object. So every
caller must have its live values reachable from a root for the duration,
which is what is_safepoint means on these rows.

THE THREE `+` OPCODES STAY THREE, and that is the trap in this
extraction. `add` is the static-family immediate above; `add_generic`
makes both sides primitive and then lets the operands decide, with the
string test taking first refusal so `1n + "a"` concatenates while
`1n + 1` is a TypeError; `concat` unconditionally ToStrings both and is
emitted only by template literals. Routing concat through bigint_binary
would send `${1n}` to a switch that has no case for it and throw "BigInts
have no unsigned right shift".

<a id="contract-7"></a>

`[[nodiscard]] value negate_value(value v);`

UNARY MINUS, AND ITS TWO ARMS ARE NOT ONE OPERATION.

It is NOT to_number_value plus a negation, which is the lowering the
shape invites: a BigInt operand is negated as an unbounded integer and
ALLOCATES, so `-0n` is `0n` - a BigInt has one zero - and the result may
be an unrooted heap value. That is why this answers with a `value` where
ct_aot_to_number answers with a double, and it is the whole reason the
ABI gives the two separate rows.

THE BIGINT TEST COMES FIRST AND THAT ORDERING IS LOAD-BEARING: it is what
makes to_number_value's own "Cannot convert a BigInt value to a number"
TypeError unreachable from `-x`. `+1n` throws; `-1n` does not.

<a id="contract-8"></a>

`[[nodiscard]] value bit_not_value(value v);`

BITWISE NOT, and the same split for a different reason.

`~1n` is `-2n` on the unbounded two's-complement value: there is no
ToInt32 step, because a BigInt has no width to truncate to. The Number
arm does have one, so the two arms genuinely differ rather than one being
the other's fast path.

<a id="contract-9"></a>

`[[nodiscard]] value numeric_operand(value v);`

ToNumeric (7.1.3) of an OBJECT operand, for the interpreter's six
bitwise operations, `~` and the `++`/`--` add: ToPrimitive with hint
number, and a BigInt out of it stays one. A primitive comes back as it
is - to_int32/to_number take it from there without re-entering, which
keeps binary_op_static's contract (and ct_aot_binary_op_static's row)
exactly what it was for every primitive operand. Null with a
TypeError in flight when valueOf threw.

<a id="contract-10"></a>

`enum class proto_kind : std::uint8_t {`

--- prototypes ---------------------------------------------------------

A string is not an object_object, so there is nowhere on it to put a
method: each VALUE KIND gets a prototype object, and property lookup
falls back to it (see implicit_prototypes).

<a id="contract-11"></a>

`generator_function,`

%GeneratorFunction.prototype%, %AsyncGeneratorFunction.prototype%
and %AsyncFunction.prototype% (27.3.3, 27.4.3, 27.7.3): what a
`function*`, `async function*` or `async function` closure's
[[Prototype]] is instead of Function.prototype.

<a id="contract-12"></a>

`static constexpr double fixed_epoch_base = 1767225600000.0;`

WHAT TIME A PAGE THINKS IT IS. Deterministic by default for the reason
Math.random is seeded - goldens are byte-compared - but it ADVANCES: a
FIXED BASE plus the page's own monotonic time under `tick()`. An embedder
that wants real time installs one (`browser::set_clock`, which the SDL
app does).

<a id="contract-13"></a>

`[[nodiscard]] value lookup_property(value target, const std::string & name);`

One property lookup, shared by get_prop, get_index-with-a-string-key and
call_method. Three copies of this is three chances for `a.length` and
`a["length"]` to disagree.
NOT const: an accessor on the chain is called, and that re-enters the VM.

<a id="contract-14"></a>

`[[nodiscard]] value key_value(const std::string & key);`

A PROPERTY KEY AS A VALUE: the string, or the symbol rebuilt from its
key - which IS its identity (value.hpp), so it is `===` to the one the
property was defined with. What a proxy trap, Reflect.ownKeys and
Object.getOwnPropertySymbols hand to script.

<a id="contract-15"></a>

`void store_property(value target, const std::string & name, value v);`

`target.name = v`, the whole write path: a proxy's `set` trap, then a
setter on the prototype chain, then an own data property.

One function because set_prop and set_index are the same operation with
the key arriving differently, and the two copies had already drifted -
only one of them consulted a proxy. `element.style.width = "10px"` goes
through a proxy and it can be written either way.

<a id="contract-16"></a>

`[[nodiscard]] value interned_string(const void * site, std::uint32_t slot,`

A STRING LITERAL, MEMOISED PER SITE - AND THE MEMO IS PART OF THE ABI
RATHER THAN AN OPTIMISATION. `allocations_` is a lifetime budget, so a
string literal in a per-pixel loop compiled WITHOUT the memo reaches the
40,000,000 ceiling in about a second - an UNCATCHABLE failure on a
program the interpreter runs forever.

site == nullptr MEANS DO NOT MEMOISE. POINTER AND LENGTH, not a C string:
a JavaScript string may contain an embedded NUL. String IDENTITY is
unobservable - strict equality compares text - so sharing one object is
safe; it is the ceiling, not identity, that makes it required.

<a id="contract-17"></a>

`[[nodiscard]] value interned_bigint_literal(const void * site, std::uint32_t slot,`

A BIGINT LITERAL, PARSED ONCE PER SITE, for the same reason. IT TAKES THE
SOURCE TEXT, not digits: the parse is bigint_from_literal's and must not
be duplicated, or `0x1fn`, `0b..n` and the 1.5n-to-0n substitution drift
between the two tiers.

<a id="contract-18"></a>

`[[nodiscard]] value lookup_index(value target, value key);`

`target[key]` for an arbitrary key value. Numeric keys index an array or
a string; anything else is a named lookup. Shared by get_index and by
computed method calls, because `a[0]()` and `a['push']()` must both work
and they take different branches.

<a id="contract-19"></a>

`void store_index(value target, value key, value v);`

`target[key] = v` for an arbitrary key value - the write twin of
lookup_index, and the same shape: array-with-a-NUMBER is the fast path,
with the typed-array and owning arms inside it, and everything else is a
named write through store_property with to_string of the key.

THE SLOW PATH IS NOT "THE NON-ARRAY CASE". The guard is is_array() AND
is_number(), so `a['foo'] = 1` arrives there on an array and hits
store_property's drop-everything-but-length arm.

<a id="contract-20"></a>

`void pass_new_target(value from);`

WHAT super(...) HANDS THE BASE CONSTRUCTOR. The next frame pushed gets
THIS frame's new.target instead of undefined.

IT MUST REPRODUCE THE LEAK, NOT REPAIR IT. Only two places clear
pending_new_target_ and both are JS-closure frame pushes, so a native or
generator callee leaves the flag set and the next ordinary call anywhere
sees a truthy new.target. That is the interpreter's behaviour and the two
tiers have to agree on it.

IT TAKES THE VALUE rather than the frame only because call_frame is
declared further down this class. The ABI helper above it is
zero-operand on purpose - reading THIS frame's field is the point, and an
explicit ABI parameter would let a backend hand over a stale one.

<a id="contract-21"></a>

`void copy_own_properties(value target, value source);`

`{...o}` AND `{a, ...rest}` - object spread, both directions.

THE SOURCE'S ENTRIES ARE COPIED FIRST, and that is not a micro-optimisation
to undo: set() can reallocate the target's storage, and target and source
may be the SAME object.

<a id="contract-22"></a>

`void define_accessor(value target, const std::string & name, value getter, value setter);`

`get x()` AND `set x(v)`, in a class or an object literal.

BOTH OPCODES ARE THIS ONE MEMBER, because the runtime primitive is
already fused: accessor_table::define SKIPS undefined halves, which is
exactly what merges a get/set pair into one entry. The discriminator is
the OPCODE, read out of in.code by the interpreter and known statically
at a compiled site - so it never reaches here; the caller passes
undefined for the half it does not have.

ONE ASYMMETRY IS PRESERVED RATHER THAN TIDIED. object_object's arm
erase()s a shadowing data property first and closure_object's does NOT,
and lookup_property checks find() before find_accessor - so a
`static get x()` on a class that already has a static data property `x`
never runs. Harmonising the two would change observable behaviour.

AND THE CLOSURE ARM MUST STAY: a `static get` installs onto the
CONSTRUCTOR closure, so testing is_object() alone would silently drop
every static accessor.

<a id="contract-23"></a>

`[[nodiscard]] value lookup_along(object_object * from, value receiver,`

THE EXPLICIT CHAIN FROM `from` UPWARD, for `receiver`: a data member or an
accessor called with the receiver, the implicit Object.prototype at the
top, an explicit null ending it, and a link that is not a plain object
(an array, a function) carrying on in its own arm of lookup_property.
Shared by an object's walk and an array with a prototype of its own.

<a id="contract-24"></a>

`void delete_named(value target, const std::string & name);`

`delete o.k` - the NAMED form. delete_index is the computed one and they
are separate opcodes because the key arrives differently: a name is a
constant-pool index here and a VALUE there, and converting a value key
runs to_string, which for an object runs user JavaScript.

<a id="contract-25"></a>

`[[nodiscard]] value own_keys(value source);`

THE OWN STRING KEYS OF AN OBJECT, AS AN ARRAY - what `for (k in o)`
iterates. for-in compiles to a for-of over this array, which is how the
runtime keeps ONE iteration mechanism instead of two.

IN DEFINITION ORDER, data and accessors interleaved, because that is what
a page sees and what Object.keys has to match. An ARRAY source enumerates
its indices as strings and a PROXY enumerates its target; anything else
yields an empty array rather than throwing.

<a id="contract-26"></a>

`[[nodiscard]] value get_prototype(value target);`

THE PROTOTYPE LINK, READ AND WRITTEN - what `super` walks.

is_object() is heap_kind::object EXACTLY, so both report or ignore an
array, a string, a proxy, a native and a CLOSURE, whose chain is
closure_object::proto_link and is never this field. And a fresh object's
prototype is value::null() while `extends` is what sets it, so
`super.m()` in a BASE-class method reads null - which a backend that
folded super-dispatch would get wrong.

<a id="contract-27"></a>

`[[nodiscard]] bool has_property(value target, value key);`

THREE OPCODE BODIES shared with the compiled tier so `key in obj` cannot
disagree between them. Each keeps its opcode's own quirks. `in` on an
ARRAY asks about an index, so the key must parse as a whole number and
consume the whole string - "1x" is not index 1. `instanceof` walks the
explicit prototype chain and THEN the implicit tables, but the second
pass is object-like only, because `5 instanceof Number` is false in
JavaScript however many methods a primitive resolves. `delete` on
anything that is not an object is a silent no-op.

<a id="contract-28"></a>

`struct property_descriptor {`

--- PROPERTY DESCRIPTORS, one shape for every kind of value -----------

A property lives in four different tables here - object_object's, a
closure's statics, a native's statics, and an array's elements - plus a
handful of synthesised ones (`length` on an array or a string, `name` on
a function). Every operation that has to REASON about a property rather
than read it - getOwnPropertyDescriptor, defineProperty, freeze, seal,
hasOwnProperty, propertyIsEnumerable - needs the same answer from all of
them.

`has_*` says which fields the descriptor MENTIONS, which is the whole
difference between "define x as undefined" and "change only x's
attributes": ValidateAndApplyPropertyDescriptor (10.1.6.3) is written in
terms of absent fields, and writing undefined for an absent one is what
made `Object.defineProperty(C, "prototype", {writable: false})` wipe a
transpiled class's prototype.

<a id="contract-29"></a>

`bool virtual_slot = false;`

A SYNTHESISED property - an array's `length`, a string's `length`, a
native's `name`. It can be read and it can be enumerated, but there
is no slot to redefine or delete, so the operations that would write
one refuse rather than pretending.

<a id="contract-30"></a>

`[[nodiscard]] value from_property_descriptor(const property_descriptor & from);`

6.2.6.4 FromPropertyDescriptor and 6.2.6.5 ToPropertyDescriptor: the
descriptor OBJECT a page sees, and the one it hands back. Members of the
context rather than of the standard library because a proxy's
getOwnPropertyDescriptor and defineProperty traps speak in these objects
too, and those are called from inside own_property / define_own_property.
Only the fields the descriptor MENTIONS are written; a field of the
object is read with HasProperty then Get, so an inherited or accessor
field counts - test262 devotes ~250 files in built-ins/Object/
defineProperties (15.2.3.7-5-b-*) to descriptors inheriting a field
through a prototype getter. `from` must be an object - the callers check.

<a id="contract-31"></a>

`[[nodiscard]] bool private_element_present(value target, const std::string & key);`

PrivateElementFind (7.3.30) over this engine's spelling: a private
FIELD is an own property under its `@#name:class` key; a private
METHOD or ACCESSOR lives on the prototype (or the constructor when
static) under its key, and an object carries it only when it carries
the class's BRAND - the own `@#:class` key the class's initialiser
adds to every instance it constructs (and to the constructor itself,
for the statics). So `Object.create(C.prototype)` and a subclass
constructor fail the brand check, as PrivateBrandCheck says.

<a id="contract-32"></a>

`void array_append(value target, value v);`

PUSH ONE ELEMENT ONTO AN ARRAY LITERAL UNDER CONSTRUCTION.

SILENT ON A NON-ARRAY, which is the row's (0, 0, 0) rather than an
oversight: the bytecode only ever emits this against an array it has just
built, so the guard is a belt on a thing that cannot happen and answering
rather than faulting is what keeps the row free of an exception edge.

<a id="contract-33"></a>

`using root_visitor = std::function<void(value)>;`

ROOTS THE VM CANNOT SEE. The DOM bindings hold every event listener,
every timer callback and every element wrapper in C++ containers, and
nothing in the register file, the globals table or a call frame refers to
them. A collection without this frees a page's listeners while the page
is still using them - which is why collection never ran at all.
